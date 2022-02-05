/// Copyright 2022 Peter Backman

#ifndef MINICOMPS_IF_SYNC_QUERY_H_
#define MINICOMPS_IF_SYNC_QUERY_H_

#include <minicomps/messaging.h>
#include <minicomps/component.h>
#include <minicomps/interface.h>

#include <functional>

#define SYNC_QUERY(name, signature) mc::if_sync_query<signature> name{MINICOMPS_STR(name)}

namespace mc {

template<typename Signature>
class if_sync_query;

/// Represents either:
///   * the actual handling code in the target component
///   * or a proxy to another if_sync_query (which holds the actual code)
///
/// The reason for why they're the same class is that it optimizes for syntax and less typing for the user.
template<typename R, typename... ArgumentTypes>
class if_sync_query<R(ArgumentTypes...)> {
  using Signature = R(ArgumentTypes...);
  using return_type = typename signature_util<Signature>::return_type;

public:
  if_sync_query(const char* name) : name_(name) {}
  if_sync_query() = default;

  /// Called when the client component has looked up the handler component. Used to create a
  /// local view of the handling interface.
  if_sync_query(if_sync_query& other) {
    linked_query_ = &other;
    sending_component_ = get_current_component();

    if (!sending_component_)
      std::abort();

    // We cache the pointers
    linked_handling_component_ = other.handling_component_.lock().get(); // TODO: check for null
    linked_executor_ = other.handling_executor_.lock().get();
    msg_info_.name = other.name_;
    mutual_executor_ = linked_executor_ == sending_component_->default_executor.get();
  }

  return_type operator() (ArgumentTypes&&... arguments) {
    if (!linked_query_)
      std::abort();

    if (!linked_query_->handler_)
      std::abort();

    class listener_invoker {
    public:
      listener_invoker(component_listener* listener, component* sender, component* receiver, const message_info& msg_info, message_type msg_type)
        : listener_(listener), sender_(sender), receiver_(receiver), msg_info_(msg_info), msg_type_(msg_type) {}

      ~listener_invoker() {
        if (listener_)
          listener_->on_invoke(sender_, receiver_, msg_info_, msg_type_);
      }

    private:
      component_listener* listener_;
      component* sender_;
      component* receiver_;
      const message_info& msg_info_;
      message_type msg_type_;
    };

    component_listener* listener = linked_handling_component_->listener;

    if (mutual_executor_) {
      if (listener)
        listener->on_invoke(sending_component_, linked_handling_component_, msg_info_, message_type::REQUEST);

      // We can skip the lock since the same executor is never updated from different threads
      listener_invoker invoker(listener, linked_handling_component_, sending_component_, msg_info_, message_type::RESPONSE);
      return linked_query_->handler_(std::forward<ArgumentTypes>(arguments)...);
    }
    else {
      if (listener)
        listener->on_invoke(sending_component_, linked_handling_component_, msg_info_, message_type::LOCKED_REQUEST);

      listener_invoker invoker(listener, linked_handling_component_, sending_component_, msg_info_, message_type::LOCKED_RESPONSE);
      std::lock_guard<std::recursive_mutex> lg(linked_handling_component_->lock);
      return linked_query_->handler_(std::forward<ArgumentTypes>(arguments)...);
    }
  }

  /// Called by the handling component's publish function
  template<typename CallbackType>
  void publish(CallbackType callback, std::weak_ptr<component>&& handling_component, std::weak_ptr<executor>&& executor) {
    // TODO: check that we haven't already been published
    handler_ = std::move(callback);
    handling_component_ = std::move(handling_component);
    handling_executor_ = std::move(executor);
  }

  template<typename CallbackType>
  void prepend_filter(CallbackType handler) {
    if (linked_query_) {
      linked_query_->prepend_filter(std::forward<CallbackType>(handler));
      // TODO: invalidate target component in the broker
      return;
    }

    if (std::shared_ptr<component> this_component = handling_component_.lock()) {
      this_component->lock.lock();

      auto previous_handler = std::move(handler_);

      handler_ = [handler = std::forward<CallbackType>(handler), previous_handler = std::move(previous_handler)] (ArgumentTypes&&... args) mutable -> R {
        return handler(std::forward<ArgumentTypes>(args)..., previous_handler);
      };

      this_component->lock.unlock();
    }
  }

private:
  // Fields set on the handling side
  const char* name_ = nullptr;
  std::function<Signature> handler_;
  std::weak_ptr<component> handling_component_;
  std::weak_ptr<executor> handling_executor_;

  // Fields set on the client side. These can be pointers since the broker will invalidate the receiver set
  // if the target goes out of scope, and it's impossible to unregister an interface.
  if_sync_query* linked_query_ = nullptr;
  component* linked_handling_component_ = nullptr;
  executor* linked_executor_ = nullptr;
  component* sending_component_ = nullptr;
  message_info msg_info_; // TODO: storing message_info like this and passing it in callback handlers isn't safe!
  bool mutual_executor_ = false;
};

}

#endif // MINICOMPS_IF_SYNC_QUERY_H_
