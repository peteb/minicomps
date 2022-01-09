// Copyright 2022 Peter Backman

#include "testing.h"

#include <minicoros/coroutine.h>
#include <minicomps/component.h>
#include <minicomps/component_base.h>
#include <minicomps/broker.h>
#include <minicomps/messaging.h>
#include <minicomps/executor.h>
#include <minicomps/testing.h>

using namespace testing;
using namespace mc;

namespace {

DECLARE_QUERY(Sum, int(int, int)); DEFINE_QUERY(Sum);
DECLARE_QUERY(GetValueMapping, int(int)); DEFINE_QUERY(GetValueMapping);

class calculator_component : public component_base<calculator_component> {
public:
  calculator_component(broker& broker, executor_ptr executor)
    : component_base("calculator", broker, executor)
    , get_value_mapping_(lookup_async_query<GetValueMapping>())
    {}

  virtual void publish() override {
    publish_async_query<Sum>([this](int t1, int t2, callback_result<int>&& sum_result) {
      get_value_mapping_(t1).with_successful_callback(std::move(sum_result), [this, t2](int t1_mapped, auto&& sum_result) {
        get_value_mapping_(t2).with_successful_callback(std::move(sum_result), [this, t1_mapped](int t2_mapped, auto&& sum_result) mutable {
          sum_result(t1_mapped + t2_mapped);
        });
      });
    });
  }

private:
  async_query<GetValueMapping> get_value_mapping_;
};

class mapping_component : public component_base<mapping_component> {
public:
  mapping_component(broker& broker, executor_ptr executor)
    : component_base("mapper", broker, executor)
    {}

  virtual void publish() override {
    publish_async_query<GetValueMapping>([this](int value, callback_result<int>&& result) {
      was_called = true;
      result(value * 2);
    });
  }

  bool was_called = false;
};

class test_component : public component_base<test_component> {
public:
  test_component(broker& broker, executor_ptr executor)
    : component_base("sender", broker, executor)
    , sum(lookup_async_query<Sum>())
    {}

  async_query<Sum> sum;
};

TEST(test_async_query_filter, prepended_query_is_invoked_and_proceeds) {
  // Given
  broker broker;
  executor_ptr exec = std::make_shared<executor>();
  component_registry registry;
  auto tester = registry.create<test_component>(broker, exec);
  auto calculator = registry.create<calculator_component>(broker, exec);
  auto mapper = registry.create<mapping_component>(broker, exec);
  int response = 0;
  int filter_was_called_with = 0;

  mapper->prepend_async_query_filter<GetValueMapping>([&] (bool& proceed, int value, callback_result<int>&& result) {
    filter_was_called_with = value;
    proceed = true;
  });

  // When
  tester->sum(444, 555)
    .with_callback([&] (mc::concrete_result<int> result) {response = *result.get_value(); });

  // Then
  ASSERT_EQ(response, 1998);
  ASSERT_EQ(filter_was_called_with, 555); // 555 is the last value
  ASSERT_TRUE(mapper->was_called);
}

TEST(test_async_query_filter, prepended_query_can_stop_execution_and_return_value) {
  // Given
  broker broker;
  executor_ptr exec = std::make_shared<executor>();
  component_registry registry;
  auto tester = registry.create<test_component>(broker, exec);
  auto calculator = registry.create<calculator_component>(broker, exec);
  auto mapper = registry.create<mapping_component>(broker, exec);
  int response = 0;

  mapper->prepend_async_query_filter<GetValueMapping>([&] (bool& proceed, int value, callback_result<int>&& result) {
    proceed = false;
    result(123);
  });

  // When
  tester->sum(444, 555)
    .with_callback([&] (mc::concrete_result<int> result) {response = *result.get_value(); });

  // Then
  ASSERT_EQ(response, 246);
  ASSERT_FALSE(mapper->was_called);
}

// TODO: tests for existing references to a message that then gets a filter

}
