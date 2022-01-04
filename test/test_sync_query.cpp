/// Copyright 2022 Peter Backman

#include "testing.h"

#include <minicoros/coroutine.h>
#include <minicomps/component.h>
#include <minicomps/component_base.h>
#include <minicomps/broker.h>
#include <minicomps/messaging.h>
#include <minicomps/executor.h>
#include <minicomps/testing.h>

#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <iostream>

using namespace testing;
using namespace mc;

DECLARE_QUERY(Sum, int(int, int));
DECLARE_QUERY(Print, void(int));

namespace {

class recv_component : public component_base<recv_component> {
public:
  recv_component(broker& broker, executor_ptr executor)
    : component_base("receiver", broker, executor)
    {}

  virtual void publish() override {
    publish_sync_query<Sum>([this](int t1, int t2) {
      called = true;
      return t1 + t2;
    });

    publish_sync_query<Print>([this](int val) {
      print_called_with = val;
    });
  }

  bool called = false;
  int print_called_with = 0;
};

class send_component : public component_base<send_component> {
public:
  send_component(broker& broker, executor_ptr executor)
    : component_base("sender", broker, executor)
    , sum(lookup_sync_query<Sum>())
    , print(lookup_sync_query<Print>())
    {}

  sync_query<Sum> sum;
  sync_query<Print> print;
};

TEST(sync_query, reachable_returns_false_when_function_is_missing) {
  // Given
  broker broker;
  executor_ptr exec = std::make_shared<executor>();
  component_registry registry;
  auto sender = registry.create<send_component>(broker, exec);

  // When/Then
  ASSERT_EQ(sender->sum.reachable(), false);
}

TEST(sync_query, reachable_returns_true_when_function_has_receiver) {
  // Given
  broker broker;
  executor_ptr exec = std::make_shared<executor>();
  component_registry registry;
  auto sender = registry.create<send_component>(broker, exec);
  auto receiver = registry.create<recv_component>(broker, exec);

  // When/Then
  ASSERT_EQ(sender->sum.reachable(), true);
}

TEST(sync_query, reachable_returns_false_when_receiver_is_deleted) {
  // Given
  broker broker;
  executor_ptr exec = std::make_shared<executor>();
  component_registry registry;
  auto sender = registry.create<send_component>(broker, exec);
  auto receiver = registry.create<recv_component>(broker, exec);
  sender->sum.reachable(); // To trigger caching

  // When
  receiver->unpublish();
  receiver.reset();

  // Then
  ASSERT_EQ(sender->sum.reachable(), false);
}

TEST(sync_query, invocation_calls_fallback_when_function_is_missing) {
  // Given
  broker broker;
  executor_ptr exec = std::make_shared<executor>();
  component_registry registry;
  auto sender = registry.create<send_component>(broker, exec);

  sender->sum.set_fallback_handler([](int a, int b) {return 8086; });

  // When/Then
  ASSERT_EQ(sender->sum.reachable(), false);
  ASSERT_EQ(sender->sum(3, 4), 8086);
}

TEST(sync_query, invocation_calls_component) {
  // Given
  broker broker;
  executor_ptr exec = std::make_shared<executor>();
  component_registry registry;
  auto sender = registry.create<send_component>(broker, exec);
  auto receiver = registry.create<recv_component>(broker, exec);

  // When
  int result = sender->sum(444, 555);

  // Then
  ASSERT_EQ(result, 999);
  ASSERT_TRUE(receiver->called);
}

TEST(sync_query, can_invoke_function_returning_void) {
  // Given
  broker broker;
  executor_ptr exec = std::make_shared<executor>();
  component_registry registry;
  auto sender = registry.create<send_component>(broker, exec);
  auto receiver = registry.create<recv_component>(broker, exec);

  // When
  sender->print(123);

  // Then
  ASSERT_EQ(receiver->print_called_with, 123);
}
// TODO: test for calling a function that returns a coroutine

}
