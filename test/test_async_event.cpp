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

DECLARE_EVENT(SummationFinished, {
  int term1;
  int term2;
  int sum;
});

DEFINE_EVENT(SummationFinished);

class send_component : public component_base<send_component> {
public:
  send_component(broker& broker, executor_ptr executor)
    : component_base("sender", broker, executor)
    , summation_finished(lookup_async_event<SummationFinished>())
    {}

  async_event<SummationFinished> summation_finished;
};

class recv_component : public component_base<recv_component> {
public:
  recv_component(broker& broker, executor_ptr executor)
    : component_base("receiver", broker, executor)
    {}

  virtual void publish() override {
    publish_async_event_listener<SummationFinished>([this] (const SummationFinished& info) {
      received_event = info;
    });
  }

  std::optional<SummationFinished> received_event;
};

TEST(async_event, events_can_be_received) {
  // Given
  broker broker;
  executor_ptr sender_executor = std::make_shared<executor>();
  executor_ptr receiver_executor = std::make_shared<executor>();
  component_registry registry;
  auto sender = registry.create<send_component>(broker, sender_executor);
  auto receiver = registry.create<recv_component>(broker, receiver_executor);

  SummationFinished event;
  event.term1 = 10;
  event.term2 = 5;
  event.sum = 15;
  sender->summation_finished(std::move(event));

  // When
  receiver_executor->execute();

  // Then
  ASSERT_TRUE(!!receiver->received_event);
  ASSERT_EQ(receiver->received_event->term1, 10);
}

}
