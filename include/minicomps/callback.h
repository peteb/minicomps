#ifndef MINICOMPS_CALLBACK_H
#define MINICOMPS_CALLBACK_H

#include <minicoros/types.h>
#include <minicomps/component.h>
#include <minicomps/messaging.h>

#include <functional>

namespace mc {

template<typename T>
class callback_result {
public:
  callback_result(executor_ptr&& receiving_executor, component* target_component, component* sender_component, const message_info& msg_info, std::function<void(mc::concrete_result<T>&&)>&& callback)
    : msg_info_(msg_info)
    , receiving_executor_(std::move(receiving_executor))
    , sender_component_(sender_component)
    , target_component_(target_component)
    , callback_(std::move(callback))
    {}

  void operator()(mc::concrete_result<T>&& result) {
    if (receiving_executor_) {
      struct response_data {
        mc::concrete_result<T> result;
        std::function<void(mc::concrete_result<T>&&)> callback;
      };

      response_data response{std::move(result), std::move(callback_)};

      auto response_task = [](void* data) {
        response_data& response = *static_cast<response_data*>(data);
        response.callback(std::move(response.result));
      };

      // TODO: emplace_fast_work
      receiving_executor_->enqueue_work(std::move(response_task), std::move(response));

      if (target_component_->listener)
        target_component_->listener->on_enqueue(sender_component_, target_component_, msg_info_, message_type::RESPONSE);
    }
    else {
      if (target_component_->listener)
        target_component_->listener->on_invoke(sender_component_, target_component_, msg_info_, message_type::RESPONSE);

      // Direct invocation for synchronous result handling
      callback_(std::move(result));
    }
  }

private:
  const message_info& msg_info_;
  executor_ptr receiving_executor_;
  component* sender_component_;
  component* target_component_;
  std::function<void(mc::concrete_result<T>&&)> callback_;
};

}


#endif // MINICOMPS_CALLBACK_H
