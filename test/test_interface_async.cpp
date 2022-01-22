// Copyright 2022 Peter Backman

#include "testing.h"

#include <minicoros/coroutine.h>
#include <minicomps/component.h>
#include <minicomps/component_base.h>
#include <minicomps/broker.h>
#include <minicomps/messaging.h>
#include <minicomps/executor.h>
#include <minicomps/testing.h>
#include <minicomps/coroutine_query.h>
#include <minicomps/interface.h>
#include <minicomps/if_async_query.h>

#include <unordered_map>
#include <memory>

using namespace testing;
using namespace mc;

namespace {

// receiver.h
DECLARE_INTERFACE(receiver_if);

class receiver_if {
public:
  ASYNC_QUERY(frobnicate, int(int));
  ASYNC_QUERY(frobnicate2, int(int));
};

// receiver.cpp
DEFINE_INTERFACE(receiver_if);




// receiver_impl.cpp
class receiver_component_impl : public component_base<receiver_component_impl> {
public:
  receiver_component_impl(broker& broker, executor_ptr executor)
    : component_base("receiver", broker, executor)
    {}

  virtual void publish() override {
    publish_interface(receiver_);
    publish_async_query(receiver_.frobnicate, &receiver_component_impl::frobnicate_impl);
    publish_async_query(receiver_.frobnicate2, &receiver_component_impl::frobnicate2_impl);
  }

  void frobnicate_impl(int value, callback_result<int>&& result) {
    received_value = value;
    result_ptr = std::make_shared<callback_result<int>>(std::move(result));
  }

  mc::coroutine<int> frobnicate2_impl(int value) {
    received_value = value;

    return mc::coroutine<int>([this] (mc::promise<int>&& p) {
      frob2_promise = std::move(p);
    });
  }

  int received_value = 0;
  std::shared_ptr<callback_result<int>> result_ptr;
  mc::promise<int> frob2_promise;

private:
  receiver_if receiver_;
};


// sender.h

class sender_component_impl : public component_base<sender_component_impl> {
public:
  sender_component_impl(broker& broker, executor_ptr executor)
    : component_base("sender", broker, executor)
    , receiver(lookup_interface<receiver_if>())
  {}

  interface<receiver_if> receiver;
};


TEST(test_interface_async, same_executor_coroutine_gets_resolved_with_value) {
  // Given
  broker broker;
  executor_ptr exec = std::make_shared<executor>();
  component_registry registry;

  std::shared_ptr<receiver_component_impl> recv_comp_impl = registry.create<receiver_component_impl>(broker, exec);
  std::shared_ptr<sender_component_impl> sender = registry.create<sender_component_impl>(broker, exec);

  lifetime lf;
  int received_result = 0;

  // When/Then
  sender->receiver->frobnicate(123)
    .then([&](int result) {
      received_result = result;
    });

  ASSERT_EQ(recv_comp_impl->received_value, 123);
  ASSERT_EQ(received_result, 0);

  (*recv_comp_impl->result_ptr)(444);

  ASSERT_EQ(received_result, 444);
}

TEST(test_interface_async, same_executor_callback_gets_resolved_with_value) {
  // Given
  broker broker;
  executor_ptr exec = std::make_shared<executor>();
  component_registry registry;

  std::shared_ptr<receiver_component_impl> recv_comp_impl = registry.create<receiver_component_impl>(broker, exec);
  std::shared_ptr<sender_component_impl> sender = registry.create<sender_component_impl>(broker, exec);

  lifetime lf;
  int received_result = 0;

  // When/Then
  sender->receiver->frobnicate.call(123)
    .with_callback([&](mc::concrete_result<int>&& result) {
      if (!result.success())
        return;

      received_result = *result.get_value();
    });

  ASSERT_EQ(recv_comp_impl->received_value, 123);
  ASSERT_EQ(received_result, 0);

  (*recv_comp_impl->result_ptr)(444);

  ASSERT_EQ(received_result, 444);
}

TEST(test_interface_async, different_executors_coroutine_gets_resolved_with_value) {
  // Given
  broker broker;
  executor_ptr receiver_executor = std::make_shared<executor>();
  executor_ptr sender_executor = std::make_shared<executor>();
  component_registry registry;

  std::shared_ptr<receiver_component_impl> recv_comp_impl = registry.create<receiver_component_impl>(broker, receiver_executor);
  std::shared_ptr<sender_component_impl> sender = registry.create<sender_component_impl>(broker, sender_executor);

  lifetime lf;
  int received_result = 0;

  // When/Then
  sender->receiver->frobnicate(123)
    .then([&](int result) {
      received_result = result;
    });

  ASSERT_EQ(recv_comp_impl->received_value, 0);
  ASSERT_EQ(received_result, 0);

  receiver_executor->execute();

  ASSERT_EQ(recv_comp_impl->received_value, 123);
  ASSERT_EQ(received_result, 0);

  (*recv_comp_impl->result_ptr)(444);

  ASSERT_EQ(recv_comp_impl->received_value, 123);
  ASSERT_EQ(received_result, 0);

  sender_executor->execute();

  ASSERT_EQ(recv_comp_impl->received_value, 123);
  ASSERT_EQ(received_result, 444);
}

TEST(test_interface_async, same_executor_coroutine_receiver_and_coroutine_sender_gets_resolved_with_value) {
  // Given
  broker broker;
  executor_ptr exec = std::make_shared<executor>();
  component_registry registry;

  std::shared_ptr<receiver_component_impl> recv_comp_impl = registry.create<receiver_component_impl>(broker, exec);
  std::shared_ptr<sender_component_impl> sender = registry.create<sender_component_impl>(broker, exec);

  lifetime lf;
  int received_result = 0;

  // When/Then
  sender->receiver->frobnicate2(123)
    .then([&](int result) {
      received_result = result;
    });

  ASSERT_EQ(recv_comp_impl->received_value, 123);
  ASSERT_EQ(received_result, 0);

  recv_comp_impl->frob2_promise(444);

  ASSERT_EQ(received_result, 444);
}

// TODO: listeners

}
