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

namespace {

DECLARE_QUERY(Sum, int(int t1, int t2)); DEFINE_QUERY(Sum);
DECLARE_QUERY(UpdateValues, int(int new_value)); DEFINE_QUERY(UpdateValues);

class recv_component : public component_base<recv_component> {
public:
  recv_component(broker& broker, executor_ptr executor)
    : component_base("receiver", broker, executor)
    {}

  virtual void publish() override {
    publish_sync_query<Sum>(&recv_component::sum);

    publish_sync_query<UpdateValues>([this] (int new_value) {
      value1 = new_value;
      value2 = new_value;
      return value1 - value2;
    });
  }

  int sum(int t1, int t2) {
    return t1 + t2;
  }

  volatile int value1 = 0;
  volatile int value2 = 0;
};

class send_component : public component_base<send_component> {
public:
  send_component(broker& broker, executor_ptr executor)
    : component_base("sender", broker, executor)
    , sum_(lookup_sync_query<Sum>())
    , update_values_(lookup_sync_query<UpdateValues>())
    {}

  void precache() {
    sum_(1, 3);
  }

  void spam() {
    int sum = 0;

    for (int i = 0; i < 100000000; ++i)
      sum += sum_(4, 5);
  }

  void spam_updates() {
    for (int i = 0; i < 10000000; ++i) {
      if (update_values_(i) != 0) {
        std::abort();
      }
    }
  }

private:
  sync_query<Sum> sum_;
  sync_query<UpdateValues> update_values_;
};

TEST(sync_query_perf, simple_same_executor_call) {
  broker broker;
  executor_ptr exec = std::make_shared<executor>();
  component_registry registry;

  auto c1 = registry.create<recv_component>(broker, exec);
  auto c2 = registry.create<send_component>(broker, exec);

  c2->precache();

  alloc_counter ac;

  measure_with_allocs([c2] {
    c2->spam();
  });

  ASSERT_EQ(ac.total_allocation_count(), 0);

  // 731 ms on my computer, = 136 798 000/s
}

TEST(sync_query_perf, simple_different_executor_call) {
  broker broker;
  executor_ptr exec1 = std::make_shared<executor>();
  executor_ptr exec2 = std::make_shared<executor>();
  component_registry registry;

  auto c1 = registry.create<recv_component>(broker, exec1);
  auto c2 = registry.create<send_component>(broker, exec2);

  alloc_counter ac;

  c2->precache();

  measure_with_allocs([c2] {
    c2->spam();
  });

  ASSERT_EQ(ac.total_allocation_count(), 0);
  // 3467 ms on my computer, = 28 843 000/s
}

TEST(sync_query_perf, spsc_multithreading_seems_to_work) {
  // The idea is that multiple threads will call a function in the receiving component and update two fields,
  // value1 and value2, then return the diff. These fields are volatile so won't end up in a register. If a
  // concurrent call changes one of the fields the different.

  // Given
  broker broker;
  executor_ptr exec1 = std::make_shared<executor>();
  executor_ptr exec2 = std::make_shared<executor>();
  component_registry registry;

  auto receiver = registry.create<recv_component>(broker, exec2);

  // When/Then
  auto sender1 = registry.create<send_component>(broker, exec1);

  int allocs = 0;

  std::thread t1([sender1, &allocs] {
    alloc_counter ac;
    measure_with_allocs([&] {
      sender1->spam_updates();
    });
    allocs = ac.total_allocation_count();
  });

  t1.join();

  ASSERT_EQ(allocs, 0);

  // 324 ms = 30 864 000/s
}

TEST(sync_query_perf, multithreading_seems_to_work) {
  // The idea is that multiple threads will call a function in the receiving component and update two fields,
  // value1 and value2, then return the diff. These fields are volatile so won't end up in a register. If a
  // concurrent call changes one of the fields the different.

  // Given
  broker broker;
  executor_ptr exec1 = std::make_shared<executor>();
  executor_ptr exec2 = std::make_shared<executor>();
  component_registry registry;

  auto receiver = registry.create<recv_component>(broker, exec2);

  // When/Then
  auto sender1 = registry.create<send_component>(broker, exec1);
  auto sender2 = registry.create<send_component>(broker, exec1);
  auto sender3 = registry.create<send_component>(broker, exec1);

  std::thread t1([sender1] {
    measure_with_allocs([&] {
      sender1->spam_updates();
    });
  });

  std::thread t2([sender2] {
    sender2->spam_updates();
  });

  std::thread t3([sender3] {
    sender3->spam_updates();
  });

  t1.join();
  t2.join();
  t3.join();
  // 1947 ms on my computer, = 5 136 000/s for 3 senders
}

}

