// Copyright 2022 Peter Backman

#include "testing.h"

#include <minicoros/coroutine.h>
#include <minicomps/component.h>
#include <minicomps/component_base.h>
#include <minicomps/broker.h>
#include <minicomps/messaging.h>
#include <minicomps/executor.h>
#include <minicomps/testing.h>
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
  SYNC_QUERY(frobnicate, int(int));
  SYNC_QUERY(frobnicate2, void(int));
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
    publish_sync_query(receiver_.frobnicate, &receiver_component_impl::frobnicate_impl);
    publish_sync_query(receiver_.frobnicate2, &receiver_component_impl::frobnicate2_impl);
  }

  int frobnicate_impl(int value) {
    received_value = value;
    return value * 2;
  }

  void frobnicate2_impl(int value) {
    received_value = value;
  }

  int received_value = 0;

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


TEST(test_interface_sync, same_executor_call_works) {
  // Given
  broker broker;
  executor_ptr exec = std::make_shared<executor>();
  component_registry registry;

  std::shared_ptr<receiver_component_impl> recv_comp_impl = registry.create<receiver_component_impl>(broker, exec);
  std::shared_ptr<sender_component_impl> sender = registry.create<sender_component_impl>(broker, exec);

  // When/Then
  int received_result = sender->receiver->frobnicate(123);

  ASSERT_EQ(recv_comp_impl->received_value, 123);
  ASSERT_EQ(received_result, 246);
}

TEST(test_interface_sync, can_invoke_function_returning_void) {
  // Given
  broker broker;
  executor_ptr exec = std::make_shared<executor>();
  component_registry registry;

  std::shared_ptr<receiver_component_impl> recv_comp_impl = registry.create<receiver_component_impl>(broker, exec);
  std::shared_ptr<sender_component_impl> sender = registry.create<sender_component_impl>(broker, exec);

  // When/Then
  sender->receiver->frobnicate2(123);

  ASSERT_EQ(recv_comp_impl->received_value, 123);
}

// TODO: listeners

}
