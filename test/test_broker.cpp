/// Copyright 2022 Peter Backman

#include "testing.h"

#include <minicomps/component.h>
#include <minicomps/component_base.h>
#include <minicomps/broker.h>
#include <minicomps/executor.h>

#include <memory>

using namespace testing;
using namespace mc;

class component1 : public component_base<component1> {
public:
  component1(broker& broker, executor_ptr executor) : component_base("c1", broker, executor) {}
};

TEST(broker, looking_up_unassociated_message_returns_empty) {
  // Given
  broker broker;

  // When
  std::weak_ptr<message_receivers> receivers = broker.lookup(123);

  // Then
  ASSERT_TRUE(receivers.lock());
  ASSERT_EQ(receivers.lock()->size(), 0);
}

TEST(broker, existing_lookups_are_invalidated_when_component_gets_associated) {
  // Given
  broker broker;
  executor_ptr exec = std::make_shared<executor>();
  std::shared_ptr<component> c1 = std::make_shared<component1>(broker, exec);
  std::weak_ptr<message_receivers> receivers = broker.lookup(123);

  // When
  broker.associate(123, c1);

  // Then
  ASSERT_FALSE(receivers.lock());
  receivers = broker.lookup(123);
  ASSERT_TRUE(receivers.lock());
  ASSERT_EQ(receivers.lock()->size(), 1);
}

TEST(broker, shared_ptr_to_receiver_set_stays_the_same_when_component_gets_associated) {
  // Given
  broker broker;
  executor_ptr exec = std::make_shared<executor>();
  auto c1 = std::make_shared<component1>(broker, exec);
  auto c2 = std::make_shared<component1>(broker, exec);
  broker.associate(123, c1);

  std::shared_ptr<message_receivers> receivers = broker.lookup(123).lock();

  // When
  broker.associate(123, c2);

  // Then
  ASSERT_EQ(receivers->size(), 1);
  ASSERT_EQ(broker.lookup(123).lock()->size(), 2);
  ASSERT_EQ(receivers->size(), 1);
}

TEST(broker, disassociating_removes_and_invalidates_existing_sets) {
  // Given
  broker broker;
  executor_ptr exec = std::make_shared<executor>();
  auto c1 = std::make_shared<component1>(broker, exec);
  broker.associate(123, c1);

  std::weak_ptr<message_receivers> receivers = broker.lookup(123);

  // When
  broker.disassociate(123, c1.get());

  // Then
  ASSERT_TRUE(receivers.expired());
  ASSERT_EQ(broker.lookup(123).lock()->size(), 0);
}
