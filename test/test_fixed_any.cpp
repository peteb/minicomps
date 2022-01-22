/// Copyright 2022 Peter Backman

#include "testing.h"

#include <minicomps/component.h>
#include <minicomps/component_base.h>
#include <minicomps/broker.h>
#include <minicomps/executor.h>
#include <minicomps/fixed_any.h>

#include <memory>
#include <cstdint>

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

TEST(fixed_any, can_store_and_access_int) {
  fixed_any<64> aas;

  aas.assign(123);

  ASSERT_EQ(*aas.get<int>(), 123);
}

TEST(fixed_any, empty_cleans_up_cleanly) {
  fixed_any<64> aas;
}

TEST(fixed_any, calls_destructor_when_assigned) {
  bool destroyed = false;
  fixed_any<64> aas;
  aas.assign(destructable(&destroyed));

  ASSERT_EQ(destroyed, false);
  aas.assign(123);
  ASSERT_EQ(destroyed, true);
}

TEST(fixed_any, does_not_allocate) {
  alloc_counter allocs;

  // Given
  fixed_any<64> aas;
  testing::stop_optimizations(&aas);

  // When
  aas.assign(123);
  aas.assign(false);
  aas.assign(123);
  aas.assign(false);

  // Then
  ASSERT_EQ(allocs.total_allocation_count(), 0);
}

struct uint8_buffer_type {
  uint8_t buf[1024];
};

struct uint32_buffer_type {
  uint32_t buf[1024];
};

TEST(fixed_any, larger_than_storage_with_byte_alignment_allocates) {
  {
    // Empty storage should allocate
    alloc_counter allocs;
    fixed_any<0> aas; testing::stop_optimizations(&aas);

    aas.assign(uint8_buffer_type{});

    ASSERT_EQ(allocs.total_allocation_count(), 1);
  }

  {
    // Exactly below the requirement allocates
    alloc_counter allocs;
    fixed_any<1023> aas; testing::stop_optimizations(&aas);

    aas.assign(uint8_buffer_type{});

    ASSERT_EQ(allocs.total_allocation_count(), 1);
  }

  {
    // Exactly on the requirement doesn't allocate
    alloc_counter allocs;
    fixed_any<1024> aas; testing::stop_optimizations(&aas);

    aas.assign(uint8_buffer_type{});

    ASSERT_EQ(allocs.total_allocation_count(), 0);
  }
}

TEST(fixed_any, larger_than_storage_with_4byte_alignment_allocates) {
  {
    // Empty storage should allocate
    alloc_counter allocs;
    fixed_any<0> aas; testing::stop_optimizations(&aas);

    aas.assign(uint32_buffer_type{});

    ASSERT_EQ(allocs.total_allocation_count(), 1);
  }

  {
    // Exactly below the requirement allocates
    alloc_counter allocs;
    fixed_any<4098> aas; testing::stop_optimizations(&aas);

    aas.assign(uint32_buffer_type{});

    ASSERT_EQ(allocs.total_allocation_count(), 1);
  }

  {
    // Exactly on the requirement doesn't allocate
    alloc_counter allocs;
    fixed_any<4099> aas; testing::stop_optimizations(&aas);

    aas.assign(uint32_buffer_type{});

    ASSERT_EQ(allocs.total_allocation_count(), 0);
  }
}

TEST(fixed_any, larger_than_storage_keeps_value) {
  // Given
  uint8_buffer_type object = {};
  object.buf[1000] = '!';
  fixed_any<0> aas;

  // When
  aas.assign(std::move(object));

  // Then
  ASSERT_EQ(aas.get<uint8_buffer_type>()->buf[1000], '!');
}

TEST(fixed_any, moving_keeps_value) {
  fixed_any<64> s1;
  s1.assign(12345);

  fixed_any<64> s2 = std::move(s1);

  ASSERT_EQ(*s2.get<int>(), 12345);
}

