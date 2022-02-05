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

using namespace testing;
using namespace mc;

namespace {

DECLARE_INTERFACE(calculator_if); DEFINE_INTERFACE(calculator_if);

struct calculator_if {
  ASYNC_QUERY(sum, int(int, int));
};

DECLARE_INTERFACE(mapper_if); DEFINE_INTERFACE(mapper_if);

struct mapper_if {
  ASYNC_QUERY(getValueMapping, int(int));
};

class calculator_component : public component_base<calculator_component> {
public:
  calculator_component(broker& broker, executor_ptr executor)
    : component_base("calculator", broker, executor)
    , mapper_(lookup_interface<mapper_if>())
    {}

  virtual void publish() override {
    publish_interface(calc_if);
    publish_async_query(calc_if.sum, &calculator_component::sum);
  }

private:
  void sum(callback_result<int>&& sum_result, int t1, int t2) {
    mapper_->getValueMapping.call(t1).with_successful_callback(std::move(sum_result), [this, t2](int t1_mapped, auto&& sum_result) {
      mapper_->getValueMapping.call(t2).with_successful_callback(std::move(sum_result), [this, t1_mapped](int t2_mapped, auto&& sum_result) mutable {
        sum_result(t1_mapped + t2_mapped);
      });
    });
  }
  // Exposed interfaces
  calculator_if calc_if;

  // Dependencies
  interface<mapper_if> mapper_;
};

class mapping_component : public component_base<mapping_component> {
public:
  mapping_component(broker& broker, executor_ptr executor)
    : component_base("mapper", broker, executor)
    {}

  using component_base::prepend_async_query_filter;

  virtual void publish() override {
    publish_interface(map_if);
    publish_async_query(map_if.getValueMapping, &mapping_component::getValueMapping);
  }

  void getValueMapping(callback_result<int>&& result, int value) {
    was_called = true;
    result(value * 2);
  }

  bool was_called = false;
  mapper_if map_if;
};

class test_component : public component_base<test_component> {
public:
  test_component(broker& broker, executor_ptr executor)
    : component_base("sender", broker, executor)
    , calculator(lookup_interface<calculator_if>())
    {}

  interface<calculator_if> calculator;
};

TEST(test_interface_async_query_filter, prepended_query_is_invoked_and_proceeds) {
  // Given
  broker broker;
  executor_ptr exec = std::make_shared<executor>();
  component_registry registry;
  auto tester = registry.create<test_component>(broker, exec);
  auto calculator = registry.create<calculator_component>(broker, exec);
  auto mapper = registry.create<mapping_component>(broker, exec);
  int response = 0;
  int filter_was_called_with = 0;

  mapper->prepend_async_query_filter(mapper->map_if.getValueMapping, [&] (int value, callback_result<int>&& result, auto next_handler) {
    filter_was_called_with = value;
    next_handler(value, std::move(result));
  });

  // When
  tester->calculator->sum.call(444, 555)
    .with_callback([&] (mc::concrete_result<int> result) {response = *result.get_value(); });

  // Then
  ASSERT_EQ(response, 1998);
  ASSERT_EQ(filter_was_called_with, 555); // 555 is the last value
  ASSERT_TRUE(mapper->was_called);
}

TEST(test_interface_async_query_filter, prepended_query_can_stop_execution_and_return_value) {
  // Given
  broker broker;
  executor_ptr exec = std::make_shared<executor>();
  component_registry registry;
  auto tester = registry.create<test_component>(broker, exec);
  auto calculator = registry.create<calculator_component>(broker, exec);
  auto mapper = registry.create<mapping_component>(broker, exec);
  int response = 0;

  mapper->prepend_async_query_filter(mapper->map_if.getValueMapping, [&] (int value, callback_result<int>&& result, auto next_handler) {
    result(123);
  });

  // When
  tester->calculator->sum(444, 555)
    .then([&](int result) {response = result; });

  // Then
  ASSERT_EQ(response, 246);
  ASSERT_FALSE(mapper->was_called);
}

// TODO: tests for existing references to a message that then gets a filter

}
