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

#include <unordered_map>
#include <memory>

using namespace testing;
using namespace mc;

namespace {

DECLARE_QUERY(LongOperation, int(int)); DEFINE_QUERY(LongOperation);

class receiver_component : public component_base<receiver_component> {
public:
  receiver_component(broker& broker, executor_ptr executor)
    : component_base("receiver", broker, executor)
    {}

  virtual void publish() override {
    publish_async_query<LongOperation>(&receiver_component::long_operation);
  }

  void long_operation(int for_value, mc::callback_result<int>&& result) {
    ++invocation_count;
    result_callback = std::make_shared<mc::callback_result<int>>(std::move(result)); // TODO: make it possible to use with optional
  }

  std::shared_ptr<mc::callback_result<int>> result_callback;
  int invocation_count = 0;
};

class send_component : public component_base<send_component> {
public:
  send_component(broker& broker, executor_ptr executor)
    : component_base("sender", broker, executor)
    , long_operation(lookup_async_query<LongOperation>())
    {}

  class session {
  public:
    session(const async_query<LongOperation>& long_operation)
      : long_operation(long_operation, lifetime_)
      {}

    void frob() {
      long_operation(123)
        .then([this](int value) {
          received_value = true;
        });
    }

    lifetime lifetime_;
    coroutine_query<LongOperation> long_operation;
    bool received_value = false;
  };

  void create_session() {
    current_session = std::make_shared<session>(long_operation);
  }

  async_query<LongOperation> long_operation;
  std::shared_ptr<session> current_session;
};

TEST(test_example_subsessions, responses_are_ignored_when_session_goes_out_of_scope) {
  // Given
  broker broker;
  executor_ptr exec = std::make_shared<executor>();
  component_registry registry;
  auto sender = registry.create<send_component>(broker, exec);
  auto receiver = registry.create<receiver_component>(broker, exec);
  int response1 = 0;
  int response2 = 0;

  sender->create_session();
  sender->current_session->frob();

  ASSERT_FALSE(sender->current_session->received_value);
  sender->current_session->lifetime_.reset();

  (*receiver->result_callback)(123);

  ASSERT_FALSE(sender->current_session->received_value);
}

}
