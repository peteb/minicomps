#pragma once

#include "orchestration/composition_root.h"

#include <minicomps/component_base.h>
#include <minicoros/coroutine.h>

#include <memory>

namespace mc {class broker; class executor; }

// TODO: do we want a macro for the component boilerplate?
class test_component : public mc::component_base<test_component> {
public:
  test_component(mc::broker& broker, std::shared_ptr<mc::executor> executor)
    : component_base("test", broker, executor)
    {}

  using component_base::lookup_interface;
  using component_base::subscribe_event;
};

void assert_success(composition_root& root, mc::coroutine<void>&& coro) {
  bool done = false;

  std::move(coro).chain().evaluate_into([&](mc::concrete_result<void>&& result) {
    if (!result.success()) {
      std::cout << "!! Test returned failure: " << result.get_failure()->error << std::endl;
      std::abort();
    }

    done = true;
  });

  int ticks = 0;

  while (!done) {
    if (ticks++ > 10000000) {
      std::cout << "!! Test timed out" << std::endl;
      std::abort();
    }

    root.update();
  }
}

template<typename T>
mc::coroutine<void> ignore(mc::coroutine<T>&& coro) {
  auto ptr = std::make_shared<mc::coroutine<T>>(std::move(coro)); // Wrap it in a shared_ptr since std::function needs the lambda to be copyable
  return mc::coroutine<void>([ptr = std::move(ptr)] (mc::promise<void>&& p) mutable {
    ptr.reset();
    p({});
  });
}

template<typename CallbackType>
auto async(CallbackType&& callback) -> decltype(callback()) {
  using coro_type = decltype(callback());
  using return_type = typename coro_type::type;

  return coro_type([callback = std::forward<CallbackType>(callback)] (mc::promise<return_type>&& p) {
    callback()
      .chain()
      .evaluate_into([p = std::move(p)](mc::concrete_result<return_type>&& result) {
        p(std::move(result));
      });
  });
}

template<typename R>
class query_proxy {
public:
  mc::coroutine<void> resolve(mc::concrete_result<R>&& result) {
    return mc::coroutine<void>([this, result = std::move(result)] (mc::promise<void>&& p) mutable {
      assert(result_holder_ && *result_holder_);
      (**result_holder_)(std::move(result));
      p({});
    });
  }

  void assign(mc::callback_result<R>&& callback) {
    result_holder_->emplace(std::move(callback));
    if (await_call_promise_) {
      await_call_promise_({});
      await_call_promise_ = {};
    }
  }

  mc::coroutine<void> await_call() {
    return mc::coroutine<void>([this] (mc::promise<void>&& p) {
      if (result_holder_ && *result_holder_) {
        p({});
        return;
      }

      await_call_promise_ = std::move(p);
    });
  }

private:
  std::shared_ptr<std::optional<mc::callback_result<R>>> result_holder_ = std::make_shared<std::optional<mc::callback_result<R>>>();
  mc::promise<void> await_call_promise_;
};

template<typename R, typename... Args>
query_proxy<R> intercept(mc::if_async_query<R(Args...)>& func) {
  query_proxy<R> proxy;

  func.prepend_filter([proxy] (bool* proceed, Args... arguments, mc::callback_result<R>&& result) mutable {
    *proceed = false;
    proxy.assign(std::move(result));
  });

  return proxy;
}

template<typename FilterType, typename R, typename... Args>
query_proxy<R> intercept(mc::if_async_query<R(Args...)>& func, FilterType&& filter) {
  query_proxy<R> proxy;

  func.prepend_filter([proxy, filter = std::move(filter)] (bool* proceed, Args... arguments, mc::callback_result<R>&& result) mutable {
    if (!filter(arguments...)) {
      *proceed = true;
      return;
    }

    *proceed = false;
    proxy.assign(std::move(result));
  });

  return proxy;
}

class test_fixture {
public:
  test_fixture() {
    root.verify_dependencies();
  }

  ~test_fixture() {

  }

  template<typename InterfaceType>
  mc::interface<InterfaceType> lookup_interface() {
    return test_component_->lookup_interface<InterfaceType>();
  }

  template<typename EventType>
  mc::coroutine<EventType> await_event() {
    test_component_->subscribe_event<EventType>([this] (const EventType& msg) {
      auto iter = event_promises_.find(mc::get_message_info<EventType>().id);
      if (iter == std::end(event_promises_))
        return;

      for (std::function<void(const void*)>& promise_resolver : iter->second) {
        promise_resolver(&msg);
      }

      iter->second.clear();
    });

    return mc::coroutine<EventType>([this] (mc::promise<EventType>&& p) {
      event_promises_[mc::get_message_info<EventType>().id].push_back([p = std::move(p)] (const void* result) {
        const EventType* casted_result = static_cast<const EventType*>(result);
        p({EventType{*casted_result}});
      });
    });
  }

  void assert_success(mc::coroutine<void>&& coro) {
    return ::assert_success(root, std::move(coro));
  }

  composition_root root;

private:
  std::shared_ptr<test_component> test_component_ = root.add_component<test_component>();
  std::unordered_map<mc::message_id, std::vector<std::function<void(const void*)>>> event_promises_;
};
