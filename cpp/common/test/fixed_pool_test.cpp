#include "ics/common/fixed_pool.hpp"

#include <cstdint>
#include <functional>

#include <gtest/gtest.h>

#include "ics/testing/no_allocation_scope.hpp"

// Instantiates every member, so one no test calls shows up as uncovered.
template class ics::FixedPool<int, 3>;

namespace {

using Pool = ics::FixedPool<int, 3>;

// Acquires value and returns its handle, failing the test if the pool refuses.
ics::PoolHandle acquire_value(Pool& pool, const int value) {
  const ics::Result<ics::PoolHandle> handle = pool.acquire(value);
  EXPECT_TRUE(handle.has_value());
  return handle.value_or(ics::PoolHandle{});
}

// The value behind a handle, read through a const pool, or -1 if get fails.
int value_at(const Pool& pool, const ics::PoolHandle handle) {
  const ics::Result<std::reference_wrapper<const int>> value = pool.get(handle);
  EXPECT_TRUE(value.has_value());
  return value.has_value() ? value->get() : -1;
}

TEST(FixedPool, StartsEmpty) {
  const Pool pool;
  EXPECT_EQ(Pool::capacity(), 3U);
  EXPECT_EQ(pool.size(), 0U);
}

TEST(FixedPool, AcquiresChangesAndReleasesWithoutAllocating) {
  Pool pool;
  const ics::testing::NoAllocationScope no_allocation;
  const ics::PoolHandle handle = acquire_value(pool, 7);
  EXPECT_EQ(pool.size(), 1U);
  EXPECT_EQ(value_at(pool, handle), 7);

  const ics::Result<std::reference_wrapper<int>> value = pool.get(handle);
  ASSERT_TRUE(value.has_value());
  value->get() = 8;
  EXPECT_EQ(value_at(pool, handle), 8);

  EXPECT_TRUE(pool.release(handle).has_value());
  EXPECT_EQ(pool.size(), 0U);
}

TEST(FixedPool, FillsNeverUsedSlotsInOrder) {
  Pool pool;
  const ics::testing::NoAllocationScope no_allocation;
  for (std::uint32_t expected = 0; expected < 3U; ++expected) {
    const ics::PoolHandle handle = acquire_value(pool, 1);
    EXPECT_EQ(handle.index, expected);
    EXPECT_EQ(handle.generation, 0U);
  }
  EXPECT_EQ(pool.size(), 3U);
}

TEST(FixedPool, RefusesAnAcquireWhenFull) {
  Pool pool;
  for (int value = 0; value < 3; ++value) {
    acquire_value(pool, value);
  }
  const ics::testing::NoAllocationScope no_allocation;
  const ics::Result<ics::PoolHandle> handle = pool.acquire(3);
  ASSERT_FALSE(handle.has_value());
  EXPECT_EQ(handle.error(), ics::Error::kFull);
}

TEST(FixedPool, ReusesAReleasedSlotUnderANewGeneration) {
  Pool pool;
  const ics::testing::NoAllocationScope no_allocation;
  const ics::PoolHandle first = acquire_value(pool, 1);
  ASSERT_TRUE(pool.release(first).has_value());
  const ics::PoolHandle second = acquire_value(pool, 2);
  EXPECT_EQ(second.index, first.index);
  EXPECT_EQ(second.generation, first.generation + 1);
  EXPECT_EQ(value_at(pool, second), 2);

  // The old handle names the same slot, now holding another object.
  const ics::Result<std::reference_wrapper<int>> stale = pool.get(first);
  ASSERT_FALSE(stale.has_value());
  EXPECT_EQ(stale.error(), ics::Error::kStaleHandle);
}

TEST(FixedPool, RejectsAReleasedHandle) {
  Pool pool;
  const ics::PoolHandle handle = acquire_value(pool, 1);
  ASSERT_TRUE(pool.release(handle).has_value());
  const Pool& view = pool;
  const ics::testing::NoAllocationScope no_allocation;
  const ics::Status released_again = pool.release(handle);
  ASSERT_FALSE(released_again.has_value());
  EXPECT_EQ(released_again.error(), ics::Error::kStaleHandle);
  const ics::Result<std::reference_wrapper<const int>> value = view.get(handle);
  ASSERT_FALSE(value.has_value());
  EXPECT_EQ(value.error(), ics::Error::kStaleHandle);
}

TEST(FixedPool, RejectsAHandleNoAcquireReturned) {
  Pool pool;
  const ics::PoolHandle forged{3, 0};
  const ics::testing::NoAllocationScope no_allocation;
  const ics::Result<std::reference_wrapper<int>> value = pool.get(forged);
  ASSERT_FALSE(value.has_value());
  EXPECT_EQ(value.error(), ics::Error::kInvalidArgument);
  const ics::Status released = pool.release(forged);
  ASSERT_FALSE(released.has_value());
  EXPECT_EQ(released.error(), ics::Error::kInvalidArgument);
}

}  // namespace
