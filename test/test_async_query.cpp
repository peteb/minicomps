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

using namespace testing;
using namespace mc;

namespace {

DECLARE_QUERY(Sum, int(int, int)); DEFINE_QUERY(Sum);
DECLARE_QUERY(Print, void(int)); DEFINE_QUERY(Print);

class recv_component : public component_base<recv_component> {
public:
  recv_component(broker& broker, executor_ptr executor)
    : component_base("receiver", broker, executor)
    {}

  virtual void publish() override {
    publish_async_query<Sum>([this](int t1, int t2, callback_result<int>&& result) {
      called = true;
      return result(t1 + t2);
    });

    publish_async_query<Print>(&recv_component::print);
  }

  void print(int val, callback_result<void>&& result) {
    print_called_with = val;
    result({});
  }

  bool called = false;
  int print_called_with = 0;
};

class send_component : public component_base<send_component> {
public:
  send_component(broker& broker, executor_ptr executor)
    : component_base("sender", broker, executor)
    , sum(lookup_async_query<Sum>())
    , print(lookup_async_query<Print>())
    {}

  async_query<Sum> sum;
  async_query<Print> print;
};

// TODO: what happens if we call a message that no one receives?
// TODO: reachable
// TODO: call a function returning a coroutine
// TODO: what happens if the participating components go out of scope at various points? Ie, after enqueuing the request, or the result?
// TODO: verify that reference parameters are copied when async

TEST(async_query, same_executor_executes_query_synchronously) {
  // Given
  broker broker;
  executor_ptr exec = std::make_shared<executor>();
  component_registry registry;
  auto sender = registry.create<send_component>(broker, exec);
  auto receiver = registry.create<recv_component>(broker, exec);
  int response = 0;

  // When
  sender->sum(444, 555)
    .with_callback([&] (mc::concrete_result<int> result) {response = *result.get_value(); });

  // Then
  ASSERT_TRUE(receiver->called);
  ASSERT_EQ(response, 999);
}

TEST(async_query, different_executor_enqueues_on_executors) {
  // Given
  broker broker;
  executor_ptr sender_executor = std::make_shared<executor>();
  executor_ptr receiver_executor = std::make_shared<executor>();
  component_registry registry;
  auto sender = registry.create<send_component>(broker, sender_executor);
  auto receiver = registry.create<recv_component>(broker, receiver_executor);
  int response = 0;

  sender->sum(444, 555)
    .with_callback([&] (mc::concrete_result<int> result) {response = *result.get_value(); });

  // When/Then
  ASSERT_FALSE(receiver->called);
  ASSERT_EQ(response, 0);

  receiver_executor->execute();

  ASSERT_TRUE(receiver->called);
  ASSERT_EQ(response, 0);

  sender_executor->execute();

  ASSERT_EQ(response, 999);
}

TEST(async_query, can_call_query_returning_void) {
  // Given
  broker broker;
  executor_ptr exec = std::make_shared<executor>();
  component_registry registry;
  auto sender = registry.create<send_component>(broker, exec);
  auto receiver = registry.create<recv_component>(broker, exec);
  bool returned = false;

  // When
  sender->print(432).with_callback([&](mc::concrete_result<void>) {returned = true; });

  // Then
  ASSERT_TRUE(returned);
  ASSERT_EQ(receiver->print_called_with, 432);
}

}

