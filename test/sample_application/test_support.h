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

  }

  ~test_fixture() {

  }

  template<typename InterfaceType>
  mc::interface<InterfaceType> lookup_interface() {
    return test_component_->lookup_interface<InterfaceType>();
  }

  void assert_success(mc::coroutine<void>&& coro) {
    return ::assert_success(root, std::move(coro));
  }

  composition_root root;

private:
  std::shared_ptr<test_component> test_component_ = root.add_component<test_component>();
};
