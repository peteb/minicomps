#ifndef MINICOMPS_CALLBACK_H
#define MINICOMPS_CALLBACK_H

#include <minicoros/types.h>

#include <functional>

namespace mc {

template<typename T>
class callback_result {
public:
  callback_result(executor_ptr&& executor, std::function<void(mc::concrete_result<T>&&)>&& callback)
    : receiving_executor_(std::move(executor))
    , callback_(std::move(callback))
    {}

  void operator()(mc::concrete_result<T>&& result) {
    if (receiving_executor_) {
      // An executor is specified if the result should be handled asynchronously
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
    }
    else {
      // Direct invocation for synchronous result handling
      callback_(std::move(result));
    }
  }

private:
  executor_ptr receiving_executor_;
  std::function<void(mc::concrete_result<T>&&)> callback_;
};

}


#endif // MINICOMPS_CALLBACK_H
