// Copyright 2022 Peter Backman

#include "testing.h"

#include <minicoros/coroutine.h>
#include <minicomps/component.h>
#include <minicomps/component_base.h>
#include <minicomps/broker.h>
#include <minicomps/messaging.h>
#include <minicomps/executor.h>
#include <minicomps/testing.h>

#include <string>

using namespace testing;
using namespace mc;

namespace {

DECLARE_EVENT(UserUpdated, {std::string user_name; }); DEFINE_EVENT(UserUpdated);

class receiver_component : public component_base<receiver_component> {
public:
  receiver_component(broker& broker, executor_ptr executor)
    : component_base("receiver", broker, executor)
    {}

  virtual void publish() override {
    subscribe_event<UserUpdated>([] (const UserUpdated&) {});
  }

private:
};

class send_component : public component_base<send_component> {
public:
  send_component(broker& broker, executor_ptr executor)
    : component_base("sender", broker, executor)
    , user_updated_(lookup_event<UserUpdated>())
    {}

private:
  event<UserUpdated> user_updated_;
};

bool events_are_fulfilled(std::vector<std::shared_ptr<component>>&& components) {
  // Checks that:
  // - All event subscriptions will succeed
  // - Each event has max 1 publisher

  std::unordered_set<message_id> published_events;

  // Collect all the event exports
  for (std::shared_ptr<component> comp : components) {
    for (const dependency_info& info : comp->describe_dependencies()) {
      if (info.type == dependency_info::ASYNC_POLY && info.direction == dependency_info::EXPORT) {
        // Check for duplicate exporters
        if (published_events.find(info.msg_info.id) != std::end(published_events))
          return false;

        published_events.insert(info.msg_info.id);
      }
    }
  }

  // Check that event imports are fulfilled
  for (std::shared_ptr<component> comp : components) {
    for (const dependency_info& info : comp->describe_dependencies()) {
      if (info.type == dependency_info::ASYNC_POLY && info.direction == dependency_info::IMPORT) {
        // Check that the exporter exists
        if (published_events.find(info.msg_info.id) == std::end(published_events))
          return false;
      }
    }
  }

  return true;
}

TEST(test_example_dep_verification, events_can_be_checked_for_fulfillment) {
  // Given
  broker broker;
  executor_ptr exec = std::make_shared<executor>();
  component_registry registry;
  auto sender = registry.create<send_component>(broker, exec);
  auto receiver = registry.create<receiver_component>(broker, exec);
  int response1 = 0;
  int response2 = 0;

  ASSERT_TRUE(events_are_fulfilled({sender, receiver}));  // They match
  ASSERT_TRUE(events_are_fulfilled({sender}));            // It's ok to export events that no one is listening to
  ASSERT_FALSE(events_are_fulfilled({receiver}));         // Will fail the check since there's no sender for the event
  ASSERT_FALSE(events_are_fulfilled({sender, sender}));   // Will fail the check since you cannot have multiple senders
}

// TODO: how do we build access permission checks?

}
