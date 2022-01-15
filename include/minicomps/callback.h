/// Copyright 2022 Peter Backman

#ifndef MINICOMPS_CALLBACK_H
#define MINICOMPS_CALLBACK_H

#include <minicomps/component.h>
#include <minicomps/messaging.h>
#include <minicomps/lifetime.h>

#include <minicoros/types.h>

#include <functional>
#include <memory>

namespace mc {

template<typename T>
class callback_result {
public:
  callback_result(executor_ptr&& receiving_executor, lifetime_weak_ptr lifetime_ptr, component* target_component, component* sender_component, const message_info& msg_info, std::function<void(mc::concrete_result<T>&&)>&& callback)
    : msg_info_(msg_info)
    , receiving_executor_(std::move(receiving_executor))
    , lifetime_ptr_(std::move(lifetime_ptr))
    , sender_component_(sender_component)
    , target_component_(target_component)
    , callback_(std::move(callback))
    {}

  void operator()(mc::concrete_result<T>&& result) {
    if (receiving_executor_) {
      struct response_data {
        mc::concrete_result<T> result;
        std::function<void(mc::concrete_result<T>&&)> callback;
        lifetime_weak_ptr lifetime;
      };

      response_data response{
        std::move(result),
        std::move(callback_),
        std::move(lifetime_ptr_) // Needed because the lifetime can expire while the message is enqueued
      };

      auto response_task = [](void* data) {
        response_data& response = *static_cast<response_data*>(data);

        if (response.lifetime.expired())
          return;

        response.callback(std::move(response.result));
      };

      receiving_executor_->enqueue_work(std::move(response_task), std::move(response));

      if (target_component_->listener)
        target_component_->listener->on_enqueue(sender_component_, target_component_, msg_info_, message_type::RESPONSE);
    }
    else {
      if (lifetime_ptr_.expired())
        return;

      if (target_component_->listener)
        target_component_->listener->on_invoke(sender_component_, target_component_, msg_info_, message_type::RESPONSE);

      // Direct invocation for synchronous result handling
      callback_(std::move(result));
    }
  }

  bool canceled() const {
    return lifetime_ptr_.expired();
  }

private:
  const message_info& msg_info_;
  executor_ptr receiving_executor_;
  lifetime_weak_ptr lifetime_ptr_;
  component* sender_component_;
  component* target_component_;
  std::function<void(mc::concrete_result<T>&&)> callback_;
};

}


#endif // MINICOMPS_CALLBACK_H
