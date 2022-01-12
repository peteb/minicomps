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

#include <cstdint>
#include <iostream>
#include <unordered_map>
#include <vector>
#include <mutex>

namespace mc {

/// Utilities for communicating with other components
template<typename SubclassType>
class component_base : public component, public std::enable_shared_from_this<SubclassType> {
  using std::enable_shared_from_this<SubclassType>::shared_from_this;

public:
  component_base(const char* name, broker& broker, executor_ptr executor) : component(name, executor), broker_(broker) {}

  ~component_base() {
    if (published_) {
      unpublish();
    }
  }

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

  /// Publishes a callable as a synchronous query
  template<typename MessageType, typename CallbackType>
  void publish_sync_query(CallbackType&& handler) {
    const message_id msg_id = get_message_id<MessageType>();
    broker_.associate(msg_id, shared_from_this());

    using wrapper_type = typename query_info<MessageType>::handler_wrapper_type;
    sync_handlers_[msg_id] = std::make_shared<wrapper_type>(std::move(handler));
    // TODO: remove from queryHandlers
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
  void publish_async_query(CallbackType&& handler) {
    const message_id msg_id = get_message_id<MessageType>();
    broker_.associate(msg_id, shared_from_this());

    using async_wrapper_type = typename query_info<MessageType>::async_handler_wrapper_type;
    async_handlers_[msg_id] = std::make_shared<async_wrapper_type>(std::move(handler));
    // TODO: remove from queryHandlers
  }

  /// Publishes a member function as an asynchronous query
  template<typename MessageType, typename... ArgumentTypes>
  void publish_async_query(void(SubclassType::*memfun)(ArgumentTypes...)) {
    publish_async_query<MessageType>([this, memfun] (ArgumentTypes&&... arguments) {
      (static_cast<SubclassType*>(this)->*memfun)(std::forward<ArgumentTypes>(arguments)...);
    });
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
      handler(proceed, std::move(args)...);
      if (proceed)
        (*static_cast<async_handler_type*>(previous_handler->get_handler_ptr()))(std::move(args)...);
    });

    // Existing references will have to be updated since we've updated the handler pointer
    broker_.invalidate(msg_id);

    // TODO: remove from queryHandlers
  }

  /// Publishes a callable as an event listener for asynchronous events.
  template<typename MessageType, typename CallbackType>
  void subscribe_event(CallbackType&& handler) {
    const message_id msg_id = get_message_id<MessageType>();
    broker_.associate(msg_id, shared_from_this());

    using handler_type = decltype(message_handler_event_impl<MessageType>{}.handler);
    async_handlers_[msg_id] = std::make_shared<message_handler_event_impl<MessageType>>(std::move(handler));
    // TODO: remove from queryHandlers
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

  virtual void* lookup_sync_handler(message_id msgId) override {
    std::lock_guard<std::recursive_mutex> lg(lock);

    auto iter = sync_handlers_.find(msgId);
    if (iter == std::end(sync_handlers_))
      return nullptr;

    return iter->second->get_handler_ptr();
  }

  virtual void* lookup_async_handler(message_id msgId) override {
    std::lock_guard<std::recursive_mutex> lg(lock);

    auto iter = async_handlers_.find(msgId);
    if (iter == std::end(async_handlers_))
      return nullptr;

    return iter->second->get_handler_ptr();
  }

private:
  broker& broker_;
  std::unordered_map<message_id, std::shared_ptr<message_handler>> sync_handlers_;
  std::unordered_map<message_id, std::shared_ptr<message_handler>> async_handlers_;

  std::vector<std::shared_ptr<mono_ref>> mono_refs_; // Reset shared_ptrs in mono_refs to avoid memory leaks at shutdown
  std::vector<std::shared_ptr<poly_ref>> poly_refs_; // ... and in poly_refs
  bool published_ = false;
};

}

#endif // MINICOMPS_COMPONENT_BASE_H_
