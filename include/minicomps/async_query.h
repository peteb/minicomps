/// Copyright 2022 Peter Backman

#ifndef MINICOMPS_ASYNC_QUERY_H_
#define MINICOMPS_ASYNC_QUERY_H_

#include <minicomps/executor.h>
#include <minicomps/mono_ref.h>
#include <minicoros/coroutine.h>
#include <minicomps/messaging.h>
#include <minicomps/callback.h>

#include <tuple>
#include <memory>

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
  async_query(async_mono_ref<MessageType>* handler, executor_ptr executor) : handler_(handler), executor_(executor) {}

  using result_type = typename signature_util<typename query_info<MessageType>::signature>::return_type;

  template<typename... ArgumentTypes>
  class query_invoker {
  public:
    query_invoker(async_query& async_query, ArgumentTypes&&... arguments)
      : async_query_(async_query)
      , arguments_(std::forward<ArgumentTypes>(arguments)...)
      {}

    void with_callback(std::function<void(mc::concrete_result<result_type>&&)>&& callback) {
      auto handler = async_query_.handler_->lookup();
      auto& component = async_query_.handler_->receiver();

      if (!component || !handler) {
        // TODO: fallback handler?
        std::abort(); // TODO: make this behavior configurable
      }

      if (async_query_.handler_->mutual_executor()) {
        callback_result result_handler{nullptr, std::move(callback)};
        //(*handler)(std::forward<Args>(arguments)..., std::move(result_handler));
        std::apply(*handler, std::tuple_cat(std::move(arguments_), std::make_tuple(std::move(result_handler))));
      }
      else {
        struct request_data {
          std::tuple<ArgumentTypes...> arguments;
          std::function<void(mc::concrete_result<result_type>&&)> callback;
          executor_ptr receiver_executor;
        };

        request_data request{std::move(arguments_), std::move(callback), async_query_.executor_};

        // Note: handler as captured here could become a dangling pointer if the message handler is removed/replaced
        auto request_task = [handler] (void* data) {
          request_data& request = *static_cast<request_data*>(data);
          callback_result result_handler{std::move(request.receiver_executor), std::move(request.callback)};
          std::apply(*handler, std::tuple_cat(std::move(request.arguments), std::make_tuple(std::move(result_handler))));
        };

        // TODO: emplace
        component->executor->enqueue_work(std::move(request_task), std::move(request));
      }
    }

  private:
    async_query& async_query_;
    std::tuple<ArgumentTypes...> arguments_;
  };

  /// Creates an invocation object that will call this function. The object is needed because C++ doesn't allow
  /// non-pack parameters (the callback) after a parameter pack.
  template<typename... Args>
  query_invoker<Args...> operator() (Args&&... arguments) {
    return query_invoker<Args...>(*this, std::forward<Args>(arguments)...);
  }

private:
  executor_ptr executor_;
};

}

#endif // MINICOMPS_ASYNC_QUERY_H_
