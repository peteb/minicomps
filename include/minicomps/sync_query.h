/// Copyright 2022 Peter Backman

#ifndef MINICOMPS_SYNC_QUERY_H_
#define MINICOMPS_SYNC_QUERY_H_

#include <minicomps/mono_ref.h>

#include <tuple>
#include <memory>
#include <mutex>

namespace mc {

/// Proxy for syncronously invoking a function on a component.
///   - The target function cannot return a coroutine. (TODO: what happens then?)
///   - If the receiving and sending components are on different executors, the receiving component's
///     lock will be taken.
template<typename MessageType>
class sync_query {
  // Reference to the handler in the receiving component. The monoref is owned by the sending component.
  sync_mono_ref<MessageType>* handler_;

public:
  sync_query(sync_mono_ref<MessageType>* handler_ref, component* owning_component)
    : handler_(handler_ref)
    , owning_component_(owning_component)
    , msg_info_(get_message_info<MessageType>()) {}

  /// Invokes the function on the responding component. If no component has registered for this message,
  /// the fallback handler will be invoked. If fallback handler is missing, std::abort is raised.
  template<typename... Args>
  auto operator() (Args&&... arguments) -> decltype(handler_->invoke(std::forward<Args>(arguments)...)) {
    auto handler = handler_->lookup();

    if (!handler) {
      if (fallback_handler_)
        return fallback_handler_(std::forward<Args>(arguments)...);
      std::abort(); // TODO: make this behavior configurable
    }

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

    component_listener* listener = handler_->receiver()->listener;

    if (handler_->mutual_executor()) {
      if (listener)
        listener->on_invoke(owning_component_, handler_->receiver().get(), msg_info_, message_type::REQUEST);

      // We can skip the lock since the same executor is never updated from different threads
      listener_invoker invoker(listener, handler_->receiver().get(), owning_component_, msg_info_, message_type::RESPONSE);
      return (*handler)(std::forward<Args>(arguments)...);
    }
    else {
      if (listener)
        listener->on_invoke(owning_component_, handler_->receiver().get(), msg_info_, message_type::LOCKED_REQUEST);

      listener_invoker invoker(listener, handler_->receiver().get(), owning_component_, msg_info_, message_type::LOCKED_RESPONSE);
      std::lock_guard<std::recursive_mutex> lg(handler_->receiver()->lock);
      return (*handler)(std::forward<Args>(arguments)...);
    }
  }

  /// Checks whether any component is responding to this message.
  bool reachable() const {
    return handler_->lookup();
  }

  /// Registers a function to be called as a fallback in case no component is responding to this message.
  void set_fallback_handler(typename sync_mono_ref<MessageType>::handler_type&& handler) {
    fallback_handler_ = std::move(handler);
  }

private:
  typename sync_mono_ref<MessageType>::handler_type fallback_handler_;
  component* owning_component_;
  const message_info& msg_info_;
};

}

#endif // MINICOMPS_SYNC_QUERY_H_
