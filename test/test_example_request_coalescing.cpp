// Copyright 2022 Peter Backman

#include "testing.h"

#include <minicoros/coroutine.h>
#include <minicomps/component.h>
#include <minicomps/component_base.h>
#include <minicomps/broker.h>
#include <minicomps/messaging.h>
#include <minicomps/executor.h>
#include <minicomps/testing.h>

#include <unordered_map>

using namespace testing;
using namespace mc;

namespace {

DECLARE_QUERY(LongOperation, int(int)); DEFINE_QUERY(LongOperation);

class receiver_component : public component_base<receiver_component> {
public:
  receiver_component(broker& broker, executor_ptr executor)
    : component_base("receiver", broker, executor)
    {}

  template<typename MessageType, typename T>
  using coalescing_callback_map = std::unordered_map<T, std::vector<callback_result<typename signature_util<typename query_info<MessageType>::signature>::return_type>>>;

  template<typename MessageType, typename KeyLookupCallbackType>
  void publish_coalescing_async_query(void(receiver_component::*memfun)(int for_value, callback_result<int>&& result), coalescing_callback_map<MessageType, int>& callback_map, KeyLookupCallbackType&& key_lookup) {
    publish_async_query<LongOperation>([this, memfun, &callback_map, key_lookup = std::move(key_lookup)] (int for_value, callback_result<int>&& result) {
      auto lookup_key = key_lookup(for_value);
      auto iter = callback_map.find(lookup_key);

      if (iter == std::end(callback_map)) {
        // No existing operation, create one
        callback_map[lookup_key].push_back(std::move(result));

        // Call the function
        callback_result<int> result_handler{
          nullptr,
          default_lifetime.create_weak_ptr(),
          this,
          this,
          mc::get_message_info<LongOperation>(),
          [this, for_value, lookup_key, &callback_map] (mc::concrete_result<int>&& result) {
            for (auto&& callback : callback_map[lookup_key]) {
              mc::concrete_result<int> copied_result{result};
              callback(std::move(copied_result));
            }
            // TODO: remove from map
            callback_map[lookup_key].clear();
          }
        };

        // TODO: propagate the lifetime expiration signal from the clients
        (static_cast<receiver_component*>(this)->*memfun)(for_value, std::move(result_handler));
      }
      else {
        callback_map[lookup_key].push_back(std::move(result));
      }
    });
  }

  virtual void publish() override {
    publish_coalescing_async_query<LongOperation>(&receiver_component::long_operation, callbacks_for_long_operation_, [](int value) {return value; });

    // Alternatively:
    // publish_async_query<LongOperation>(&receiver_component::long_operation);
    // coalescify<LongOperation>(callbacks_for_long_operation_, [](int value) {return value; });
  }

  void long_operation(int for_value, mc::callback_result<int>&& result) {
    ++invocation_count;
    result_callback = std::make_shared<mc::callback_result<int>>(std::move(result)); // TODO: make it possible to use with optional
  }

  std::shared_ptr<mc::callback_result<int>> result_callback;
  int invocation_count = 0;

private:
  coalescing_callback_map<LongOperation, int> callbacks_for_long_operation_;
};

class send_component : public component_base<send_component> {
public:
  send_component(broker& broker, executor_ptr executor)
    : component_base("sender", broker, executor)
    , long_operation(lookup_async_query<LongOperation>())
    {}

  async_query<LongOperation> long_operation;
};

TEST(test_example_request_coalescing, multiple_concurrent_requests_are_coalesced) {
  // Given
  broker broker;
  executor_ptr exec = std::make_shared<executor>();
  component_registry registry;
  auto sender = registry.create<send_component>(broker, exec);
  auto receiver = registry.create<receiver_component>(broker, exec);
  int response1 = 0;
  int response2 = 0;

  // When
  sender->long_operation(123)
    .with_callback([&] (mc::concrete_result<int> result) {response1 = *result.get_value(); });
  sender->long_operation(123)
    .with_callback([&] (mc::concrete_result<int> result) {response2 = *result.get_value(); });

  // Then
  ASSERT_EQ(receiver->invocation_count, 1);

  (*receiver->result_callback)({535});

  ASSERT_EQ(response1, 535);
  ASSERT_EQ(response2, 535);

}

}
