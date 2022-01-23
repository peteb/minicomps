/// Copyright 2022 Peter Backman

#ifndef MINICOMPS_ASYNC_QUERY_H_
#define MINICOMPS_ASYNC_QUERY_H_

#include <minicomps/executor.h>
#include <minicomps/mono_ref.h>
#include <minicomps/messaging.h>
#include <minicomps/callback.h>
#include <minicomps/component.h>
#include <minicoros/coroutine.h>

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
  using signature = typename query_info<MessageType>::signature;
  using return_type = typename signature_util<signature>::return_type;

public:
  async_query(async_mono_ref<MessageType>* handler, component* owning_component)
    : handler_(handler)
    , owning_component_(owning_component)
    , msg_info_(get_message_info<MessageType>())
    , lifetime_(owning_component->default_lifetime)
    {}

  /// async_queries either have to be created using a factory function in component_base or by using an existing async_query together with a lifetime
  async_query(const async_query&) = delete;

  async_query(async_query&&) = default;

  /// Create an async_query based on another, but with a different lifetime. Useful for sessions etc.
  async_query(const async_query& other, const lifetime& life)
    : handler_(other.handler_)
    , owning_component_(other.owning_component_)
    , msg_info_(other.msg_info_)
    , lifetime_(life)
    {}

  template<typename... ArgumentTypes>
  class query_invoker {
  public:
    query_invoker(async_query& async_query, lifetime_weak_ptr&& lifetime, ArgumentTypes&&... arguments)
      : async_query_(async_query)
      , lifetime_(std::move(lifetime))
      , arguments_(std::forward<ArgumentTypes>(arguments)...)
      {}

    query_invoker(async_query& async_query, lifetime_weak_ptr&& lifetime, std::tuple<ArgumentTypes...>&& arguments)
      : async_query_(async_query)
      , lifetime_(std::move(lifetime))
      , arguments_(std::move(arguments))
      {}

    ~query_invoker() {
      async_query_.execute(std::move(callback_), std::move(lifetime_), std::move(arguments_));
    }

    query_invoker&& with_lifetime(const lifetime& life) && {
      lifetime_ = life.create_weak_ptr();
      return std::move(*this);
    }

    query_invoker&& with_callback(std::function<void(mc::concrete_result<return_type>&&)>&& callback) && {
      callback_ = std::move(callback);
      return std::move(*this);
    }

    template<typename OuterResultType, typename CallbackType>
    query_invoker&& with_successful_callback(OuterResultType&& outer_result, CallbackType&& callback) && {
      std::move(*this).with_callback([outer_result = std::move(outer_result), callback = std::move(callback)](mc::concrete_result<return_type>&& inner_result) mutable {
        if (!inner_result.success()) {
          outer_result(std::move(*inner_result.get_failure()));
          return;
        }

        callback(*inner_result.get_value(), std::move(outer_result));
      });

      return std::move(*this);
    }

  private:
    async_query& async_query_;
    lifetime_weak_ptr lifetime_;
    std::function<void(mc::concrete_result<return_type>&&)> callback_;
    std::tuple<ArgumentTypes...> arguments_;
    component* sender_;
  };

  /// Creates an invocation object that will call this function. The object is needed because C++ doesn't allow
  /// non-pack parameters (the callback) after a parameter pack.
  template<typename... Args>
  query_invoker<Args...> call(Args&&... arguments) {
    return query_invoker<Args...>(*this, lifetime_.create_weak_ptr(), std::forward<Args>(arguments)...);
  }

  template<typename... Args>
  query_invoker<Args...> call(std::tuple<Args...>&& arguments) {
    return query_invoker<Args...>(*this, lifetime_.create_weak_ptr(), std::move(arguments));
  }

  template<typename... Args>
  mc::coroutine<return_type> operator() (Args&&... arguments) {
    std::tuple<Args...> copied_arguments = std::make_tuple(std::forward<Args>(arguments)...);

    return mc::coroutine<return_type>([this, copied_arguments = std::move(copied_arguments)](mc::promise<return_type>&& promise) mutable {
      call(std::move(copied_arguments))
        .with_callback([promise = std::move(promise)] (mc::concrete_result<return_type>&& result) {
          promise(std::move(result));
        });
    });
  }

private:
  template<typename CallbackType, typename... ArgumentTypes>
  void execute(CallbackType&& callback, lifetime_weak_ptr&& lifetime, std::tuple<ArgumentTypes...>&& arguments) {
    auto handler = handler_->lookup();
    auto& receiving_component = handler_->receiver();

    if (!receiving_component || !handler) {
      // TODO: fallback handler?
      std::abort(); // TODO: make this behavior configurable
    }

    if (handler_->mutual_executor()) {
      if (receiving_component->listener)
        receiving_component->listener->on_invoke(owning_component_, receiving_component.get(), msg_info_, message_type::REQUEST);

      callback_result result_handler{nullptr, std::move(lifetime), owning_component_, receiving_component.get(), msg_info_, std::move(callback)};
      std::apply(*handler, std::tuple_cat(std::move(arguments), std::make_tuple(std::move(result_handler))));
    }
    else {
      struct request_data {
        std::tuple<ArgumentTypes...> arguments;
        std::function<void(mc::concrete_result<return_type>&&)> callback;
        executor_ptr receiver_executor; // We have to capture executor as a shared_ptr to protect against lifetime issues
        lifetime_weak_ptr lifetime;

        // Fields used only for the listener
        component* receiver;
        component* sender;
      };

      request_data request{std::move(arguments), std::move(callback), owning_component_->default_executor, std::move(lifetime), owning_component_, receiving_component.get()};

      // Note: handler as captured here could become a dangling pointer if the message handler is removed/replaced
      auto request_task = [handler] (void* data) {
        request_data& request = *static_cast<request_data*>(data);
        const message_info& msg_info = get_message_info(static_cast<MessageType*>(nullptr));

        // The callback_result is the object that gets called by the application to return a value to the calling component. Sender and receiver
        // is only used by the listener.
        callback_result result_handler{
          std::move(request.receiver_executor),
          std::move(request.lifetime),
          request.receiver,
          request.sender,
          msg_info,
          std::move(request.callback)
        };

        std::apply(*handler, std::tuple_cat(std::move(request.arguments), std::make_tuple(std::move(result_handler))));
      };

      handler_->receiver_executor()->enqueue_work(std::move(request_task), std::move(request));

      if (receiving_component->listener)
        receiving_component->listener->on_enqueue(owning_component_, receiving_component.get(), msg_info_, message_type::REQUEST);
    }
  }

  async_mono_ref<MessageType>* handler_;
  component* owning_component_;
  const message_info& msg_info_;
  const lifetime& lifetime_;
};

}

#endif // MINICOMPS_ASYNC_QUERY_H_
