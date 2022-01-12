/// Copyright 2022 Peter Backman

#ifndef MINICOMPS_ASYNC_QUERY_H_
#define MINICOMPS_ASYNC_QUERY_H_

#include <minicomps/executor.h>
#include <minicomps/mono_ref.h>
#include <minicoros/coroutine.h>
#include <minicomps/messaging.h>
#include <minicomps/callback.h>
#include <minicomps/component.h>

#include <tuple>
#include <memory>
#include <utility>

namespace mc {

/// Proxy for asynchronously invoking a function on a component.
///   - The target function can return a coroutine but it can also return a value immediately
///   - If the receiving and sending components are on the same executor, the call will be done synchronously
///   - If the components are on different executors, the request will be enqueued on the receiving component's
///     executor and the promise resolving is enqueued on the sending component's executor
template<typename MessageType>
class async_query {
  async_mono_ref<MessageType>* handler_;

public:
  async_query(async_mono_ref<MessageType>* handler, component* owning_component)
    : handler_(handler)
    , owning_component_(owning_component)
    , msg_info_(get_message_info<MessageType>()) {}

  using result_type = typename signature_util<typename query_info<MessageType>::signature>::return_type;

  template<typename... ArgumentTypes>
  class query_invoker {
  public:
    query_invoker(async_query& async_query, ArgumentTypes&&... arguments)
      : async_query_(async_query)
      , arguments_(std::forward<ArgumentTypes>(arguments)...)
      {}

    void with_callback(std::function<void(mc::concrete_result<result_type>&&)>&& callback) {
      async_query_.execute_with_callback(std::move(callback), std::move(arguments_));
    }

    template<typename OuterResultType, typename CallbackType>
    void with_successful_callback(OuterResultType&& outer_result, CallbackType&& callback) {
      with_callback([outer_result = std::move(outer_result), callback = std::move(callback)](mc::concrete_result<result_type>&& inner_result) mutable {
        if (!inner_result.success()) {
          outer_result(std::move(*inner_result.get_failure()));
          return;
        }

        callback(*inner_result.get_value(), std::move(outer_result));
      });
    }

  private:
    async_query& async_query_;
    std::tuple<ArgumentTypes...> arguments_;
    component* sender_;
  };

  /// Creates an invocation object that will call this function. The object is needed because C++ doesn't allow
  /// non-pack parameters (the callback) after a parameter pack.
  template<typename... Args>
  query_invoker<Args...> operator() (Args&&... arguments) {
    return query_invoker<Args...>(*this, std::forward<Args>(arguments)...);
  }

private:
  template<typename CallbackType, typename... ArgumentTypes>
  void execute_with_callback(CallbackType&& callback, std::tuple<ArgumentTypes...>&& arguments) {
    auto handler = handler_->lookup();
    auto& receiving_component = handler_->receiver();

    if (!receiving_component || !handler) {
      // TODO: fallback handler?
      std::abort(); // TODO: make this behavior configurable
    }

    if (handler_->mutual_executor()) {
      if (receiving_component->listener)
        receiving_component->listener->on_invoke(owning_component_, receiving_component.get(), msg_info_, message_type::REQUEST);

      callback_result result_handler{nullptr, owning_component_, receiving_component.get(), msg_info_, std::move(callback)};
      std::apply(*handler, std::tuple_cat(std::move(arguments), std::make_tuple(std::move(result_handler))));
    }
    else {
      struct request_data {
        std::tuple<ArgumentTypes...> arguments;
        std::function<void(mc::concrete_result<result_type>&&)> callback;
        executor_ptr receiver_executor; // We have to capture executor as a shared_ptr to protect against lifetime issues
        component* receiver; // Only used by the listener
        component* sender;   // Only used by the listener
      };

      request_data request{std::move(arguments), std::move(callback), owning_component_->executor, owning_component_, receiving_component.get()};

      // Note: handler as captured here could become a dangling pointer if the message handler is removed/replaced
      auto request_task = [handler] (void* data) {
        request_data& request = *static_cast<request_data*>(data);
        const message_info& msg_info = get_message_info(static_cast<MessageType*>(nullptr));

        // The callback_result is the object that gets called by the application to return a value to the calling component. Sender and receiver
        // is only used by the listener.
        callback_result result_handler{std::move(request.receiver_executor), request.receiver, request.sender, msg_info, std::move(request.callback)};
        std::apply(*handler, std::tuple_cat(std::move(request.arguments), std::make_tuple(std::move(result_handler))));
      };

      receiving_component->executor->enqueue_work(std::move(request_task), std::move(request));

      if (receiving_component->listener)
        receiving_component->listener->on_enqueue(owning_component_, receiving_component.get(), msg_info_, message_type::REQUEST);
    }
  }

  component* owning_component_;
  const message_info& msg_info_;
};

}

#endif // MINICOMPS_ASYNC_QUERY_H_
