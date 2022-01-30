#include <iostream>

#include "session/session_system.h"
#include "user/user_system.h"
#include "test_support.h"

#include <testing.h>

using namespace ::testing;

class test_session_system : public test_fixture {
public:
  test_session_system() {
    root.enable_sequence_diagram_gen();
  }

  ~test_session_system() {
    std::cout << root.dump_and_disable_sequence_diagram_gen() << std::endl;
  }

  /// Helpers
  mc::coroutine<void> create_session() {
    return session_system->create_session()
      .then([this](int session_id) {
        last_created_session_id_ = session_id;
      });
  }

  mc::coroutine<void> destroy_session() {
    return async([this] {
      return session_system->destroy_session(last_created_session_id_)
        .then([this] {
          last_created_session_id_ = 0;
        });
    });
  }

  mc::coroutine<void> authenticate_session(std::string username, std::string password) {
    return async([this, username = std::move(username), password = std::move(password)] {
      return session_system->authenticate_session(last_created_session_id_, username, password);
    });
  }

  /// Interfaces
  mc::interface<user_system::interface> user_system = lookup_interface<user_system::interface>();
  mc::interface<session_system::interface> session_system = lookup_interface<session_system::interface>();

private:
  int last_created_session_id_ = 0;
};

TEST_F(test_session_system, destroying_a_session_before_user_data_returned_does_not_crash) {
  // FINDING: benefit of mock: namespace is correct automatically
  // FINDING: I have to read the implementations to see how to use functions. Make it easier
  // FINDING: can we create a coroutine using `failure`

  // TODO: generate dependency graph

  // Intercepts are useful for returning custom responses. Similar to a mock.
  auto get_user = intercept(user_system->get_user, [](std::string username) {return username == "user"; });

  assert_success(
    session_system->create_session()
      .then([&] (int session_id) -> mc::result<int> {
        // Start authenticating
        session_system->authenticate_session(session_id, "user", "pass");
        return mc::make_successful_coroutine<int>(session_id);
      })
      .then([&] (int session_id) -> mc::result<void> {
        // Immediately destroy the session
        return session_system->destroy_session(session_id);
      })
      .then(get_user.await_call())
      .then(get_user.resolve({user_system::user_info{123, "user", "pass"}}))
    );
}

TEST_F(test_session_system, simplified_destroying_a_session_before_user_data_returned_does_not_crash) {
  auto get_user = intercept(user_system->get_user);

  assert_success(
    create_session()
    .then(ignore(authenticate_session("user", "pass")))
    .then(destroy_session()) // Immediately destroy the session
    .then(get_user.await_call())
    .then(get_user.resolve({user_system::user_info{123, "user", "pass"}}))
  );
}