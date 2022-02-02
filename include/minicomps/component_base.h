/// Copyright 2022 Peter Backman

#ifndef MINICOMPS_COMPONENT_BASE_H_
#define MINICOMPS_COMPONENT_BASE_H_

#include <minicoros/coroutine.h>
#include <minicomps/component.h>
#include <minicomps/messaging.h>
#include <minicomps/broker.h>
#include <minicomps/executor.h>
#include <minicomps/sync_query.h>
#include <minicomps/async_query.h>
#include <minicomps/event.h>
#include <minicomps/mono_ref.h>
#include <minicomps/poly_ref.h>
#include <minicomps/interface_ref.h>
#include <minicomps/interface.h>
#include <minicomps/if_async_query.h>
#include <minicomps/if_sync_query.h>

#include <cstdint>
#include <iostream>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <type_traits>

namespace mc {

/// Utilities for communicating with other components
template<typename SubclassType, typename GroupType = void>
class component_base : public component, public std::enable_shared_from_this<SubclassType> {
  using std::enable_shared_from_this<SubclassType>::shared_from_this;

protected:
  component_base(const char* name, broker& broker, executor_ptr executor)
    : component(name, executor), broker_(broker) {

    if constexpr (!std::is_void<GroupType>()) {
      add_dependency_info({mc::dependency_info::EXPORT, mc::dependency_info::GROUP, mc::get_message_info<GroupType>(), {}});
    }
  }

  ~component_base() {
    if (published_) {
      unpublish();
    }
  }

public:
  virtual void publish() override {
    published_ = true;
  }

  virtual void unpublish() override {
    // TODO: check if there are any sync query dependencies on this component. In that case, warn!
    broker_.disassociate_everything(this);
    // NOTE! It's important that we remove our associations here (and cached queries etc), but we SHOULDN'T REMOVE OUR OWN HANDLERS HERE since
    // other components might still have direct pointer references to that data
    published_ = false;
  }

  virtual std::vector<dependency_info> describe_dependencies() override {
    std::vector<dependency_info> infos;

    for (auto& mono : mono_refs_) {
      mono->force_resolve();
      infos.push_back(mono->create_dependency_info());
    }

    for (auto& poly : poly_refs_) {
      poly->force_resolve();
      infos.push_back(poly->create_dependency_info());
    }

    for (auto& interface : interface_refs_) {
      interface->force_resolve();
      infos.push_back(interface->create_dependency_info());
    }

    for (const dependency_info& info : published_dependencies_) {
      infos.push_back(info);
    }

    return infos;
  }

protected:
  /// Publishes a callable as a synchronous query
  template<typename MessageType, typename CallbackType>
  void publish_sync_query(CallbackType&& handler) {
    const message_id msg_id = get_message_id<MessageType>();
    broker_.associate(msg_id, shared_from_this());

    using wrapper_type = typename query_info<MessageType>::handler_wrapper_type;
    sync_handlers_[msg_id] = std::make_shared<wrapper_type>(std::move(handler));
    // TODO: remove from queryHandlers
    published_dependencies_.push_back({dependency_info::EXPORT, dependency_info::SYNC_MONO, get_message_info<MessageType>(), {}});
  }

  /// Publishes a member function as a synchronous query
  template<typename MessageType, typename ReturnType, typename... ArgumentTypes>
  void publish_sync_query(ReturnType(SubclassType::*memfun)(ArgumentTypes...)) {
    publish_sync_query<MessageType>([this, memfun] (ArgumentTypes&&... arguments) {
      return (static_cast<SubclassType*>(this)->*memfun)(arguments...);
    });
  }

  /// Publishes a callable as an asynchronous query
  template<typename MessageType, typename CallbackType>
  void publish_async_query(CallbackType&& handler, executor_ptr executor_override = nullptr) {
    const message_id msg_id = get_message_id<MessageType>();
    broker_.associate(msg_id, shared_from_this());

    using async_wrapper_type = typename query_info<MessageType>::async_handler_wrapper_type;
    async_handlers_[msg_id] = std::make_shared<async_wrapper_type>(std::move(handler));
    async_executor_overrides_[msg_id] = executor_override;
    // TODO: remove from queryHandlers
    published_dependencies_.push_back({dependency_info::EXPORT, dependency_info::ASYNC_MONO, get_message_info<MessageType>(), {}});
  }

  /// Publishes a member function as an asynchronous query
  template<typename MessageType, typename... ArgumentTypes>
  void publish_async_query(void(SubclassType::*memfun)(ArgumentTypes...), executor_ptr executor_override = nullptr) {
    publish_async_query<MessageType>([this, memfun] (ArgumentTypes&&... arguments) {
      (static_cast<SubclassType*>(this)->*memfun)(std::forward<ArgumentTypes>(arguments)...);
    }, std::move(executor_override));
  }

  template<typename InterfaceType>
  void publish_interface(InterfaceType& impl) {
    const message_id msg_id = get_message_id<InterfaceType>();
    broker_.associate(msg_id, shared_from_this());
    interfaces_[msg_id] = &impl;
    published_dependencies_.push_back({dependency_info::EXPORT, dependency_info::INTERFACE, get_message_info<InterfaceType>(), {}});
  }

  /// Publish a callback_result member function as an interface async query
  template<typename Signature, typename... ArgumentTypes>
  void publish_async_query(if_async_query<Signature>& interface_query, void(SubclassType::*memfun)(ArgumentTypes...), executor_ptr executor_override = nullptr) {
    std::weak_ptr<executor> chosen_executor = executor_override ? executor_override : default_executor;

    interface_query.publish([this, memfun] (ArgumentTypes&&... arguments) {
      (static_cast<SubclassType*>(this)->*memfun)(std::forward<ArgumentTypes>(arguments)...);
    }, shared_from_this(), std::move(chosen_executor));
  }

  /// Publish a minicoros member function as an interface async query
  template<typename Signature, typename... ArgumentTypes>
  void publish_async_query(if_async_query<Signature>& interface_query, typename signature_util<Signature>::coroutine_type(SubclassType::*memfun)(ArgumentTypes...), executor_ptr executor_override = nullptr) {
    std::weak_ptr<executor> chosen_executor = executor_override ? executor_override : default_executor;
    using return_type = typename signature_util<Signature>::return_type;

    interface_query.publish([this, memfun] (ArgumentTypes&&... arguments, mc::callback_result<return_type>&& result_reporter) {
      (static_cast<SubclassType*>(this)->*memfun)(std::forward<ArgumentTypes>(arguments)...)
        .chain()
        .evaluate_into(std::move(result_reporter));
    }, shared_from_this(), std::move(chosen_executor));
  }

  /// Publish a member function as an interface sync query
  template<typename Signature, typename... ArgumentTypes>
  void publish_sync_query(if_sync_query<Signature>& interface_query, typename signature_util<Signature>::return_type(SubclassType::*memfun)(ArgumentTypes...)) {
    interface_query.publish([this, memfun] (ArgumentTypes&&... arguments) {
      return (static_cast<SubclassType*>(this)->*memfun)(std::forward<ArgumentTypes>(arguments)...);
    }, shared_from_this(), default_executor);
  }

  /// Adds a handler "on top" of an existing handler. Decides whether the next
  /// handler should run or not.
  template<typename MessageType, typename CallbackType>
  void prepend_async_query_filter(CallbackType&& handler) {
    const message_id msg_id = get_message_id<MessageType>();
    using async_wrapper_type = typename query_info<MessageType>::async_handler_wrapper_type;
    using async_handler_type = typename query_info<MessageType>::async_handler_type;

    if (async_handlers_.find(msg_id) == std::end(async_handlers_))
      std::abort();

    auto previous_handler = std::move(async_handlers_[msg_id]);

    async_handlers_[msg_id] = std::make_shared<async_wrapper_type>([handler = std::move(handler), previous_handler = std::move(previous_handler)] (auto&&... args) mutable {
      bool proceed = true;
      handler(proceed, std::move(args)...); // TODO: we can't move both here and in the proceed phase
      if (proceed)
        (*static_cast<async_handler_type*>(previous_handler->get_handler_ptr()))(std::move(args)...);
    });

    // Existing references will have to be updated since we've updated the handler pointer
    broker_.invalidate(msg_id);

    // TODO: remove from queryHandlers
  }

  /// Adds a handler "on top" of an existing handler. Decides whether the next
  /// handler should run or not.
  template<typename Signature, typename CallbackType>
  void prepend_async_query_filter(if_async_query<Signature>& interface_query, CallbackType&& handler) {
    interface_query.prepend_filter(std::forward<CallbackType>(handler));

    // Existing references will have to be updated since we've updated the handler pointer. We need to invalidate
    // the interface that this function belongs to, but since we don't have any way to map function -> interface, we
    // just invalidate all of them.
    for (const auto& [interface_msg_id, _] : interfaces_) {
      broker_.invalidate(interface_msg_id);
    }
  }

  /// Publishes a callable as an event listener for asynchronous events.
  template<typename MessageType, typename CallbackType>
  void subscribe_event(CallbackType&& handler) {
    const message_id msg_id = get_message_id<MessageType>();
    broker_.associate(msg_id, shared_from_this());

    using handler_type = decltype(message_handler_event_impl<MessageType>{}.handler);
    async_handlers_[msg_id] = std::make_shared<message_handler_event_impl<MessageType>>(std::move(handler));
    // TODO: remove from queryHandlers
    published_dependencies_.push_back({dependency_info::IMPORT, dependency_info::ASYNC_POLY, get_message_info<MessageType>(), {}});
  }

  /// Publishes a member function as an event listener for asynchronous events.
  template<typename MessageType>
  void subscribe_event(void(SubclassType::*memfun)(const MessageType& message)) {
    subscribe_event<MessageType>([this, memfun] (const MessageType& message) {
      (static_cast<SubclassType*>(this)->*memfun)(message);
    });
  }

  template<typename MessageType>
  sync_query<MessageType> lookup_sync_query() {
    auto handler_ref = std::make_shared<sync_mono_ref<MessageType>>(broker_, *this);
    mono_refs_.push_back(handler_ref);
    return sync_query<MessageType>(handler_ref.get(), this);
  };

  template<typename MessageType>
  async_query<MessageType> lookup_async_query() {
    auto handler_ref = std::make_shared<async_mono_ref<MessageType>>(broker_, *this);
    mono_refs_.push_back(handler_ref);
    return async_query<MessageType>(handler_ref.get(), this);
  };

  template<typename MessageType>
  event<MessageType> lookup_event() {
    auto handler_ref = std::make_shared<poly_ref_base<MessageType>>(broker_, *this);
    poly_refs_.push_back(handler_ref);
    return event<MessageType>(handler_ref.get(), this);
  };

  template<typename InterfaceType>
  interface<InterfaceType> lookup_interface() {
    auto interface_ref = std::make_shared<interface_ref_base<InterfaceType>>(broker_, *this, default_lifetime.create_weak_ptr());

    // TODO: clean this up
    interface_ref->clone = [this] (lifetime life) {
      // TODO: we need to clean up interface_refs_ at some point
      auto interface_ref = std::make_shared<interface_ref_base<InterfaceType>>(broker_, *this, life.create_weak_ptr());
      interface_refs_.push_back(interface_ref);
      return interface_ref.get();
    };

    interface_refs_.push_back(interface_ref);
    return interface<InterfaceType>(interface_ref.get());
  }

  virtual void* lookup_sync_handler(message_id msg_id) override {
    std::lock_guard<std::recursive_mutex> lg(lock);

    auto iter = sync_handlers_.find(msg_id);
    if (iter == std::end(sync_handlers_))
      return nullptr;

    return iter->second->get_handler_ptr();
  }

  virtual void* lookup_async_handler(message_id msg_id) override {
    std::lock_guard<std::recursive_mutex> lg(lock);

    auto iter = async_handlers_.find(msg_id);
    if (iter == std::end(async_handlers_))
      return nullptr;

    return iter->second->get_handler_ptr();
  }

  virtual void* lookup_interface(message_id msg_id) override {
    std::lock_guard<std::recursive_mutex> lg(lock);

    auto iter = interfaces_.find(msg_id);
    if (iter == std::end(interfaces_))
      return nullptr;

    return iter->second;
  }

  virtual executor_ptr lookup_executor_override(message_id msg_id) override {
    std::lock_guard<std::recursive_mutex> lg(lock);

    auto iter = async_executor_overrides_.find(msg_id);
    if (iter == std::end(async_executor_overrides_))
      return nullptr;

    return iter->second;
  }

  void add_dependency_info(dependency_info&& info) {
    published_dependencies_.push_back(std::move(info));
  }

private:
  broker& broker_;
  std::unordered_map<message_id, std::shared_ptr<message_handler>> sync_handlers_;
  std::unordered_map<message_id, std::shared_ptr<message_handler>> async_handlers_;
  std::unordered_map<message_id, void*> interfaces_;
  std::unordered_map<message_id, executor_ptr> async_executor_overrides_;

  std::vector<std::shared_ptr<mono_ref>> mono_refs_; // Reset shared_ptrs in mono_refs to avoid memory leaks at shutdown
  std::vector<std::shared_ptr<poly_ref>> poly_refs_; // ... and in poly_refs TODO: common base class
  std::vector<std::shared_ptr<interface_ref>> interface_refs_;

  std::vector<dependency_info> published_dependencies_;
  bool published_ = false;
};

}

#endif // MINICOMPS_COMPONENT_BASE_H_
