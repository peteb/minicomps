/// Copyright 2022 Peter Backman

#ifndef MINICOMPS_IF_ASYNC_QUERY_H_
#define MINICOMPS_IF_ASYNC_QUERY_H_

#include <minicomps/messaging.h>
#include <minicomps/lifetime.h>
#include <minicomps/component.h>
#include <minicomps/callback.h>
#include <minicomps/interface.h>

#include <functional>
#include <tuple>
#include <iostream>

#define ASYNC_QUERY(name, signature) mc::if_async_query<signature> name{MINICOMPS_STR(name)}

namespace mc {

template<typename Signature>
class if_async_query;

template<typename R, typename... ArgumentTypes>
class if_async_query<R(ArgumentTypes...)> {
  using callback_inner_type = typename signature_util<R(ArgumentTypes...)>::callback_inner_signature;
  using return_type = R;

public:
  if_async_query(const char* name) : name_(name) {}
  if_async_query() = default;

  /// Called when the client component has looked up the handler component. Used to create a
  /// local view of the handling interface.
  if_async_query(if_async_query& other) {
    linked_query_ = &other;
    sending_component_ = get_current_component();
    sending_lifetime_ = get_current_lifetime();

    if (!sending_component_)
      std::abort();

    // We cache the pointers
    linked_handling_component_ = other.handling_component_.lock().get(); // TODO: check for null
    linked_executor_ = other.handling_executor_.lock().get();
    msg_info_.name = other.name_;
    mutual_executor_ = linked_executor_ == sending_component_->default_executor.get();
  }

  class query_invoker {
  public:
    query_invoker(if_async_query& async_query, lifetime_weak_ptr lifetime, ArgumentTypes&&... arguments)
      : if_async_query_(async_query)
      , lifetime_(std::move(lifetime))
      , arguments_(std::forward<ArgumentTypes>(arguments)...)
      {}

    ~query_invoker() {
      if_async_query_.execute(std::move(callback_), std::move(lifetime_), std::move(arguments_));
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
    if_async_query& if_async_query_;
    lifetime_weak_ptr lifetime_;
    std::function<void(mc::concrete_result<return_type>&&)> callback_;
    std::tuple<ArgumentTypes...> arguments_;
    component* sender_;
  };

  /// Creates an invocation object that will call this function. The object is needed because C++ doesn't allow
  /// non-pack parameters (the callback) after a parameter pack.
  query_invoker call(ArgumentTypes... arguments) {
    return query_invoker(*this, sending_lifetime_, std::forward<ArgumentTypes>(arguments)...);
  }

  mc::coroutine<return_type> operator() (ArgumentTypes... arguments) {
    std::tuple<ArgumentTypes...> copied_arguments = std::make_tuple(arguments...);

    return mc::coroutine<return_type>([this, copied_arguments = std::move(copied_arguments)](mc::promise<return_type>&& promise) mutable {
      execute([promise = std::move(promise)](mc::concrete_result<return_type>&& result) {
        promise(std::move(result));
      }, sending_lifetime_, std::move(copied_arguments));
    });
  }

  /// Called by the handling component's publish function
  template<typename CallbackType>
  void publish(CallbackType&& callback, std::weak_ptr<component>&& handling_component, std::weak_ptr<executor>&& executor) {
    // TODO: check that we haven't already been published
    handler_ = std::move(callback);
    handling_component_ = std::move(handling_component);
    handling_executor_ = std::move(executor);
  }

  template<typename CallbackType>
  void prepend_filter(CallbackType&& handler) {
    if (linked_query_) {
      linked_query_->prepend_filter(std::forward<CallbackType>(handler));
      // TODO: invalidate target component in the broker
      return;
    }

    auto previous_handler = std::move(handler_);

    handler_ = [handler = std::forward<CallbackType>(handler), previous_handler = std::move(previous_handler)] (ArgumentTypes&&... args, callback_result<return_type>&& result) mutable {
      handler(std::forward<ArgumentTypes>(args)..., std::move(result), previous_handler);
    };
  }

private:
  /// Called from the client component
  template<typename CallbackType>
  void execute(CallbackType&& callback, lifetime_weak_ptr lifetime, std::tuple<ArgumentTypes...>&& arguments) {
    if (!linked_query_)
      std::abort();

    if (!linked_query_->handler_)
      std::abort();

    // TODO: refactor this code duplication vs async_query.h
    if (mutual_executor_) {
      if (linked_handling_component_->listener)
        linked_handling_component_->listener->on_invoke(sending_component_, linked_handling_component_, msg_info_, message_type::REQUEST);

      callback_result<return_type> result_handler{nullptr, std::move(lifetime), sending_component_, linked_handling_component_, msg_info_, std::move(callback)};
      std::apply(linked_query_->handler_, std::tuple_cat(std::move(arguments), std::make_tuple(std::move(result_handler))));
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

      request_data request{std::move(arguments), std::move(callback), sending_component_->default_executor, std::move(lifetime), sending_component_, linked_handling_component_};

      // Note: handler as captured here could become a dangling pointer if the message handler is removed/replaced
      auto request_task = [linked_query = linked_query_, msg_info = msg_info_] (void* data) {
        request_data& request = *static_cast<request_data*>(data);
        // TODO: don't capture msg_info

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

        std::apply(linked_query->handler_, std::tuple_cat(std::move(request.arguments), std::make_tuple(std::move(result_handler))));
      };

      linked_executor_->enqueue_work(std::move(request_task), std::move(request));

      if (linked_handling_component_->listener)
        linked_handling_component_->listener->on_enqueue(sending_component_, linked_handling_component_, msg_info_, message_type::REQUEST);
    }
  }

private:
  // Fields set on the handling side
  const char* name_ = nullptr;
  std::function<callback_inner_type> handler_;
  std::weak_ptr<component> handling_component_;
  std::weak_ptr<executor> handling_executor_;

  // Fields set on the client side. These can be pointers since the broker will invalidate the receiver set
  // if the target goes out of scope, and it's impossible to unregister an interface.
  if_async_query* linked_query_ = nullptr;
  component* linked_handling_component_ = nullptr;
  executor* linked_executor_ = nullptr;
  lifetime_weak_ptr sending_lifetime_;
  component* sending_component_ = nullptr;
  message_info msg_info_; // TODO: storing message_info like this and passing it in callback handlers isn't safe!
  bool mutual_executor_ = false;
};

}

#endif // MINICOMPS_IF_ASYNC_QUERY_H_
