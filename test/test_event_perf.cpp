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
#include <thread>

using namespace testing;
using namespace mc;

namespace {

DECLARE_EVENT(SummationFinished, {
  int sum;
});

DEFINE_EVENT(SummationFinished);


DECLARE_EVENT(ReceiverFinished, {});
DEFINE_EVENT(ReceiverFinished);

class send_component : public component_base<send_component> {
public:
  send_component(broker& broker, executor_ptr executor)
    : component_base("sender", broker, executor)
    , summation_finished(lookup_event<SummationFinished>())
    {}

  virtual void publish() {
    subscribe_event<ReceiverFinished>(&send_component::on_receiver_finished);
  }

  void on_receiver_finished(const ReceiverFinished&) {
    receiver_finished = true;
  }

  bool receiver_finished = false;

  event<SummationFinished> summation_finished;
};

class recv_component : public component_base<recv_component> {
public:
  recv_component(broker& broker, executor_ptr executor)
    : component_base("receiver", broker, executor)
    , receiver_finished(lookup_event<ReceiverFinished>())
    {}

  virtual void publish() override {
    subscribe_event<SummationFinished>([this] (const SummationFinished& info) {
      if (events_received++ >= 10000000) {
        receiver_finished({});
        finished = true;
      }
    });
  }

  int events_received = 0;
  bool finished = false;

  event<ReceiverFinished> receiver_finished;
};

TEST(async_event_perf, same_executor) {
  // Given
  broker broker;
  executor_ptr exec = std::make_shared<executor>();
  component_registry registry;
  auto sender = registry.create<send_component>(broker, exec);
  auto receiver = registry.create<recv_component>(broker, exec);

  measure_with_allocs([&] {
    for (int i = 0; i <= 10000000; ++i) {
      sender->summation_finished({i});
    }
  });

  ASSERT_TRUE(sender->receiver_finished);
  // 93 ms on my computer = 107 527 000/s
}

TEST(async_event_perf, spsc_one_consumer_two_threads) {
  // Given
  broker broker;
  executor_ptr sender_executor = std::make_shared<executor>();
  executor_ptr receiver_executor = std::make_shared<executor>();
  component_registry registry;
  auto sender = registry.create<send_component>(broker, sender_executor);
  auto receiver = registry.create<recv_component>(broker, receiver_executor);

  auto sender_thread = std::thread([sender, sender_executor] {
    measure_with_allocs([&] {
      for (int i = 0; i <= 10000000; ++i) {
        sender->summation_finished({i});
        sender_executor->execute();
      }

      // Wait until the receiver has received everything, otherwise we're just measuring send time
      while (!sender->receiver_finished) {
        sender_executor->execute();
      }
    });
  });

  auto receiver_thread = std::thread([receiver, receiver_executor] {
    while (!receiver->finished) {
      receiver_executor->execute();
    }
  });

  sender_thread.join();
  receiver_thread.join();

  // 2264 ms on my computer = 4 417 000/s
}

}
