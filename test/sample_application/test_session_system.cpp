#include <iostream>

#include "orchestration/composition_root.h"
#include "session/session_system.h"
#include "user/user_system.h"

#include <minicomps/component_base.h>
#include <testing.h>
#include <optional>

using namespace ::testing;

namespace mc {class broker; class executor; }

class test_component : public mc::component_base<test_component> {
public:
  test_component(mc::broker& broker, std::shared_ptr<mc::executor> executor)
    : component_base("test", broker, executor)
    , session_system(lookup_interface<session_system::interface>())
    , user_system(lookup_interface<user_system::interface>())
    {}

  mc::interface<session_system::interface> session_system;
  mc::interface<user_system::interface> user_system;
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

  func.prepend_filter([proxy] (bool& proceed, Args... arguments, mc::callback_result<R>&& result) mutable {
    proceed = false;
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

TEST(test_session_system, destroying_a_session_before_user_data_returned_does_not_crash) {
  composition_root root;
  auto test = root.add_component<test_component>();

  // FINDING: benefit of mock: namespace is correct automatically
  // FINDING: I have to read the implementations to see how to use functions. Make it easier
  // FINDING: can we create a coroutine using `failure`

  // TODO: generate dependency graph
  // TODO: test class support

  // Intercepts are useful for returning custom responses. Similar to a mock.
  auto get_user = intercept(test->user_system->get_user, [](std::string username) {return username == "user"; });

  root.enable_sequence_diagram_gen();

  assert_success(root,
    test->session_system->create_session()
      .then([&] (int session_id) -> mc::result<int> {
        // Start authenticating
        test->session_system->authenticate_session(session_id, "user", "pass");
        return mc::make_successful_coroutine<int>(session_id);
      })
      .then([&] (int session_id) -> mc::result<void> {
        // Immediately destroy the session
        return test->session_system->destroy_session(session_id);
      })
      .then(get_user.await_call())
      .then(get_user.resolve({user_system::user_info{123, "user", "pass"}}))
      .then([&] {
        std::cout << root.dump_and_disable_sequence_diagram_gen() << std::endl;
      })
    );
}

