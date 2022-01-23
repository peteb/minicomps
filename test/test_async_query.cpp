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
#include <optional>

using namespace testing;
using namespace mc;

namespace {

DECLARE_QUERY(Sum, int(int, int)); DEFINE_QUERY(Sum);
DECLARE_QUERY(Print, void(int)); DEFINE_QUERY(Print);
DECLARE_QUERY(SaveCallbackResult, void()); DEFINE_QUERY(SaveCallbackResult);
DECLARE_QUERY(FlowControlledFunction, void()); DEFINE_QUERY(FlowControlledFunction);

class recording_listener : public component_listener {
public:
  virtual void on_enqueue(const component* sender, const component* receiver, const message_info& info, message_type type) override {
    if (!receiver)
      std::abort();
    if (!sender)
      std::abort();

    on_enqueue_called = true;
  }

  virtual void on_invoke(const component* sender, const component* receiver, const message_info& info, message_type type) override {
    if (!receiver)
      std::abort();
    if (!sender)
      std::abort();

    on_invoke_called = true;
  }

  bool on_enqueue_called = false;
  bool on_invoke_called = false;
};

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

    publish_async_query<SaveCallbackResult>(&recv_component::save_callback_result);
    publish_async_query<Print>(&recv_component::print);
    publish_async_query<FlowControlledFunction>(&recv_component::flow_controlled_function, flow_executor);
  }

  void print(int val, callback_result<void>&& result) {
    print_called_with = val;
    result({});
  }

  void save_callback_result(callback_result<void>&& result) {
    saved_callback_result = std::make_shared<callback_result<void>>(std::move(result));
  }

  void flow_controlled_function(callback_result<void>&& result) {
    flow_function_called = true;
    result({});
  }

  std::shared_ptr<callback_result<void>> saved_callback_result;

  bool called = false;
  int print_called_with = 0;
  bool flow_function_called = false;
  executor_ptr flow_executor = std::make_shared<executor>();
};

class send_component : public component_base<send_component> {
public:
  send_component(broker& broker, executor_ptr executor)
    : component_base("sender", broker, executor)
    , sum(lookup_async_query<Sum>())
    , print(lookup_async_query<Print>())
    , save_callback_result(lookup_async_query<SaveCallbackResult>())
    , flow_controlled_function(lookup_async_query<FlowControlledFunction>())
    {}

  async_query<Sum> sum;
  async_query<Print> print;
  async_query<SaveCallbackResult> save_callback_result;
  async_query<FlowControlledFunction> flow_controlled_function;
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
  sender->sum.call(444, 555)
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

  sender->sum.call(444, 555)
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
  sender->print.call(432).with_callback([&](mc::concrete_result<void>) {returned = true; });

  // Then
  ASSERT_TRUE(returned);
  ASSERT_EQ(receiver->print_called_with, 432);
}

TEST(async_query, invocation_across_different_executors_triggers_enqueue_listener) {
  // Given
  recording_listener sender_listener, receiver_listener;

  broker broker;
  executor_ptr exec = std::make_shared<executor>();
  executor_ptr exec2 = std::make_shared<executor>();
  component_registry registry;
  auto sender = registry.create<send_component>(broker, exec);
  auto receiver = registry.create<recv_component>(broker, exec2);
  receiver->listener = &receiver_listener;
  sender->listener = &sender_listener;

  // When
  sender->print.call(432).with_callback([&](mc::concrete_result<void>) {});

  exec->execute();
  exec2->execute();

  // Then
  ASSERT_TRUE(receiver_listener.on_enqueue_called);
  ASSERT_TRUE(sender_listener.on_enqueue_called)
}

TEST(async_query, lifetime_expiration_stops_callback) {
  // Given
  broker broker;
  executor_ptr exec1 = std::make_shared<executor>();
  executor_ptr exec2 = std::make_shared<executor>();
  component_registry registry;
  auto sender = registry.create<send_component>(broker, exec1);
  auto receiver = registry.create<recv_component>(broker, exec2);
  bool returned = false;
  lifetime lifetime;

  // When
  sender->print.call(432)
    .with_lifetime(lifetime)
    .with_callback([&](mc::concrete_result<void>) {returned = true; });

  lifetime.reset();
  exec2->execute();
  exec1->execute();

  // Then
  ASSERT_FALSE(returned);
  ASSERT_EQ(receiver->print_called_with, 432);
}

TEST(async_query, cancellation_status_is_propagated_to_callback_result_and_works) {
  // Given
  broker broker;
  executor_ptr exec = std::make_shared<executor>();
  component_registry registry;
  auto sender = registry.create<send_component>(broker, exec);
  auto receiver = registry.create<recv_component>(broker, exec);
  bool returned = false;
  lifetime lifetime;

  // When
  sender->save_callback_result.call()
    .with_lifetime(lifetime)
    .with_callback([&](mc::concrete_result<void>) {returned = true; });

  // ... check that the canceled information is propagated when we reset the lifetime
  ASSERT_FALSE(returned);
  ASSERT_FALSE(receiver->saved_callback_result->canceled());

  lifetime.reset();
  ASSERT_TRUE(receiver->saved_callback_result->canceled());

  // ... check that we don't trigger the local callback
  (*receiver->saved_callback_result)({});

  ASSERT_FALSE(returned);
}

TEST(async_query, with_custom_executor_triggers_function_later) {
  // Given
  broker broker;
  executor_ptr exec = std::make_shared<executor>();
  component_registry registry;
  auto sender = registry.create<send_component>(broker, exec);
  auto receiver = registry.create<recv_component>(broker, exec);

  // When/Then
  sender->flow_controlled_function.call().with_callback([] (mc::concrete_result<void>&&) {}); // Components are on same executor, so request would be sync
  ASSERT_FALSE(receiver->flow_function_called);

  receiver->flow_executor->execute();
  ASSERT_TRUE(receiver->flow_function_called);
}

// TODO: test async call for function with customized executor but components are on different executors
// TODO: test sync call for function with customized executor

TEST(async_query, looked_up_queries_show_up_in_dependencies) {
  broker broker;
  executor_ptr exec = std::make_shared<executor>();
  component_registry registry;
  auto sender = registry.create<send_component>(broker, exec);

  auto deps = sender->describe_dependencies();

  ASSERT_EQ(deps.size(), 4);

}
// TODO: more extensive callback testing
}

