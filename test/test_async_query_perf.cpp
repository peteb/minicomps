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
#include <chrono>
#include <memory>
#include <thread>

using namespace testing;
using namespace mc;

DECLARE_QUERY(Sum, int(int t1, int t2)); DEFINE_QUERY(Sum);
DECLARE_QUERY(UpdateValues, int(int new_value)); DEFINE_QUERY(UpdateValues);

// TODO: Can only register one type for a message id due to the "number of receivers" check in mono_refs

namespace {

class recv_component : public component_base<recv_component> {
public:
  recv_component(broker& broker, executor_ptr executor)
    : component_base("receiver", broker, executor)
    {}

  virtual void publish() override {
    publish_async_query<Sum>(&recv_component::sum);
    publish_async_query<UpdateValues>(&recv_component::update_values);
  }

  void sum(int t1, int t2, callback_result<int>&& result) {
    result(t1 + t2);
  }

  void update_values(int new_value, callback_result<int>&& result) {
    value1 = new_value;
    value2 = new_value;
    result(value1 - value2);
  }

  int value1 = 0;
  int value2 = 0;
  bool done = false;
};

class send_component : public component_base<send_component> {
public:
  send_component(broker& broker, executor_ptr executor)
    : component_base("sender", broker, executor)
    , sum_(lookup_async_query<Sum>())
    , update_values_(lookup_async_query<UpdateValues>())
    {}

  void precache() {
    sum_(1, 3);
  }

  void send() {
    sum_(4, 5).with_callback([&](mc::concrete_result<int> result) {});
  }

  void send_update(int value) {
    if (send_count_ > 2000001)
      return;

    send_count_++;

    update_values_(value).with_callback([this](mc::concrete_result<int> cr) {
      int result = *cr.get_value();
      if (receive_count_++ >= 2000000)
        done_ = true;
      else if (result != 0)
        std::abort();
    });
  }

  bool done() const {
    return done_;
  }

private:
  bool done_ = false;
  int send_count_ = 0;
  int receive_count_ = 0;

  async_query<Sum> sum_;
  async_query<UpdateValues> update_values_;
};

TEST(async_query_perf, simple_same_executor_call) {
  broker broker;
  executor_ptr exec = std::make_shared<executor>();
  component_registry registry;

  auto c1 = registry.create<recv_component>(broker, exec);
  auto c2 = registry.create<send_component>(broker, exec);

  c2->precache();

  measure_with_allocs([c2] {
    for (int i = 0; i < 2000000; ++i) {
      c2->send();
    }
  });

  // 45 ms on my computer, 44 444 000/s
}

TEST(async_query_perf, simple_different_executor_same_thread) {
  broker broker;
  executor_ptr exec1 = std::make_shared<executor>();
  executor_ptr exec2 = std::make_shared<executor>();
  component_registry registry;

  auto c1 = registry.create<recv_component>(broker, exec1);
  auto c2 = registry.create<send_component>(broker, exec2);

  c2->precache();

  measure_with_allocs([c2, exec1, exec2] {
    for (int i = 0; i < 2000000; ++i) {
      c2->send();
      exec1->execute();
      exec2->execute();
    }
  });

  // 550 ms on my computer, = 3 636 000/s
}

TEST(async_query_perf, spsc_mt_one_producer) {
  // Given
  broker broker;
  component_registry registry;
  executor_ptr receiver_executor = std::make_shared<executor>();
  executor_ptr sender_executor = std::make_shared<executor>();

  auto receiver = registry.create<recv_component>(broker, receiver_executor);
  auto sender = registry.create<send_component>(broker, sender_executor);

  // When/Then
  std::thread receiver_thread([receiver, receiver_executor] {
    while (!receiver->done) {
      receiver_executor->execute();
    }
  });

  std::thread sender_thread([sender, sender_executor] {
    measure_with_allocs([&] {
      while (!sender->done()) {
        sender->send_update(0);
        sender_executor->execute();
      }
    });
  });

  sender_thread.join();

  receiver->done = true;
  receiver_thread.join();
  // 692 ms on my computer, = 2 874 000/s
}

TEST(async_query_perf, mpsc_mt_three_producers) {
  // Given
  broker broker;
  executor_ptr exec1 = std::make_shared<executor>();
  executor_ptr exec2 = std::make_shared<executor>();
  executor_ptr exec3 = std::make_shared<executor>();
  executor_ptr exec4 = std::make_shared<executor>();
  component_registry registry;

  auto receiver = registry.create<recv_component>(broker, exec1);

  // When/Then
  auto sender1 = registry.create<send_component>(broker, exec2);
  auto sender2 = registry.create<send_component>(broker, exec3);
  auto sender3 = registry.create<send_component>(broker, exec4);

  std::thread t1([receiver, exec1] {
    while (!receiver->done)
      exec1->execute();
  });

  std::thread t2([sender1, exec2] {
    measure_with_allocs([&] {
      int i = 50000000;
      while (!sender1->done()) {
        sender1->send_update(i++);
        exec2->execute();
      }
    });
  });

  std::thread t3([sender2, exec3] {
    int i = 10000000;
    while (!sender2->done()) {
      sender2->send_update(i++);
      exec3->execute();
    }
  });

  std::thread t4([sender3, exec4] {
    int i = 0;
    while (!sender3->done()) {
      sender3->send_update(i++);
      exec4->execute();
    }
  });

  t2.join();
  t3.join();
  t4.join();

  receiver->done = true;
  t1.join();
  // 1366 ms on my computer, = 1 464 000/s
}

}

