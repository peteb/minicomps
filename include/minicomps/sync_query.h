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
  sync_query(sync_mono_ref<MessageType>* handler_ref) : handler_(handler_ref) {}

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

    if (handler_->mutual_executor()) {
      // We can skip the lock since the same executor is never updated from different threads
      return (*handler)(std::forward<Args>(arguments)...);
    }
    else {
      std::lock_guard<std::recursive_mutex> lock(handler_->receiver()->lock);
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
};

}

#endif // MINICOMPS_SYNC_QUERY_H_
