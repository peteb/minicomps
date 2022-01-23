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
  }

  void print(int val, callback_result<void>&& result) {
    print_called_with = val;
    result({});
  }

  void save_callback_result(callback_result<void>&& result) {
    saved_callback_result = std::make_shared<callback_result<void>>(std::move(result));
  }

  std::shared_ptr<callback_result<void>> saved_callback_result;

  bool called = false;
  int print_called_with = 0;
};

class send_component : public component_base<send_component> {
public:
  send_component(broker& broker, executor_ptr executor)
    : component_base("sender", broker, executor)
    , sum(lookup_async_query<Sum>())
    , print(lookup_async_query<Print>())
    , save_callback_result(lookup_async_query<SaveCallbackResult>())
    {}

  async_query<Sum> sum;
  async_query<Print> print;
  async_query<SaveCallbackResult> save_callback_result;
};

TEST(coroutine_query, same_executor_triggers_coroutine_directly) {
  // Given
  broker broker;
  executor_ptr exec = std::make_shared<executor>();
  component_registry registry;
  auto sender = registry.create<send_component>(broker, exec);
  auto receiver = registry.create<recv_component>(broker, exec);
  int response = 0;

  // When
  sender->sum(444, 555)
    .then([&] (int result) {
      response = result;
    });

  // Then
  ASSERT_TRUE(receiver->called);
  ASSERT_EQ(response, 999);

}
// TODO: more tests
}

