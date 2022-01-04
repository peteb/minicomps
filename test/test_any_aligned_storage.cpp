/// Copyright 2022 Peter Backman

#include "testing.h"

#include <minicomps/component.h>
#include <minicomps/component_base.h>
#include <minicomps/broker.h>
#include <minicomps/executor.h>
#include <minicomps/any_aligned_storage.h>

#include <memory>

using namespace testing;
using namespace mc;

class destructable {
public:
  destructable(bool* destroyed) : destroyed(destroyed) {}
  destructable(destructable&& other) : destroyed(other.destroyed) {
    other.destroyed = nullptr;
  }

  ~destructable() {
    if (destroyed)
      *destroyed = true;
  }

  destructable& operator =(destructable&& other) {
    this->destroyed = other.destroyed;
    other.destroyed = nullptr;
    return *this;
  }

  bool* destroyed;
};

TEST(any_aligned_storage, can_store_and_access_int) {
  any_aligned_storage<64> aas;

  aas = 123;

  ASSERT_EQ(*aas.get<int>(), 123);
}

TEST(any_aligned_storage, empty_cleans_up_cleanly) {
  any_aligned_storage<64> aas;
}

TEST(any_aligned_storage, calls_destructor_when_assigned) {
  bool destroyed = false;
  any_aligned_storage<64> aas;
  aas = destructable(&destroyed);

  ASSERT_EQ(destroyed, false);
  aas = 123;
  ASSERT_EQ(destroyed, true);
}

TEST(any_aligned_storage, does_not_allocate) {
  alloc_counter allocs;

  any_aligned_storage<64> aas;
  aas = 123;
  aas = false;
  aas = 123;
  aas = false;

  ASSERT_EQ(allocs.total_allocation_count(), 0);
}

TEST(any_aligned_storage, moving_keeps_value) {
  any_aligned_storage<64> s1;
  s1 = 12345;

  any_aligned_storage<64> s2 = std::move(s1);

  ASSERT_EQ(*s2.get<int>(), 12345);
}
