#include "ics/common/ring_buffer.hpp"

#include <functional>

#include <gtest/gtest.h>

#include "ics/testing/no_allocation_scope.hpp"

// Instantiates every member, so one no test calls shows up as uncovered.
template class ics::RingBuffer<int, 3>;

namespace {

using Buffer = ics::RingBuffer<int, 3>;

// Pops one element and returns it, or -1 if the pop failed.
int pop_value(Buffer& buffer) {
  const ics::Result<int> popped = buffer.pop();
  EXPECT_TRUE(popped.has_value());
  return popped.value_or(-1);
}

TEST(RingBuffer, StartsEmpty) {
  const Buffer buffer;
  EXPECT_EQ(Buffer::capacity(), 3U);
  EXPECT_EQ(buffer.size(), 0U);
  EXPECT_TRUE(buffer.empty());
  EXPECT_FALSE(buffer.full());
}

TEST(RingBuffer, ReportsEmptyWithoutACheck) {
  Buffer buffer;
  ::testing::internal::CaptureStderr();
  {
    const ics::testing::NoAllocationScope no_allocation;
    EXPECT_TRUE(buffer.pop() == ics::fail(ics::Error::kEmpty));
    EXPECT_TRUE(buffer.front() == ics::fail(ics::Error::kEmpty));
  }
  // Polling an empty buffer is ordinary, so nothing is reported.
  EXPECT_EQ(::testing::internal::GetCapturedStderr(), "");
}

TEST(RingBuffer, QueuesInOrderAcrossTheWrap) {
  Buffer buffer;
  const ics::testing::NoAllocationScope no_allocation;
  ASSERT_TRUE(buffer.push(1).has_value());
  ASSERT_TRUE(buffer.push(2).has_value());
  ASSERT_TRUE(buffer.push(3).has_value());
  EXPECT_TRUE(buffer.full());
  EXPECT_EQ(pop_value(buffer), 1);
  // The next element goes in the first slot again.
  ASSERT_TRUE(buffer.push(4).has_value());
  const ics::Result<std::reference_wrapper<const int>> oldest = buffer.front();
  ASSERT_TRUE(oldest.has_value());
  EXPECT_EQ(oldest->get(), 2);
  EXPECT_EQ(pop_value(buffer), 2);
  EXPECT_EQ(pop_value(buffer), 3);
  EXPECT_EQ(pop_value(buffer), 4);
  EXPECT_TRUE(buffer.empty());
}

TEST(RingBuffer, RefusesAPushWhenFullAndKeepsEveryElement) {
  Buffer buffer;
  for (int value = 1; value <= 3; ++value) {
    ASSERT_TRUE(buffer.push(value).has_value());
  }
  const ics::testing::NoAllocationScope no_allocation;
  const ics::Status pushed = buffer.push(4);
  ASSERT_FALSE(pushed.has_value());
  EXPECT_EQ(pushed.error(), ics::Error::kFull);
  EXPECT_EQ(buffer.size(), 3U);
  EXPECT_EQ(pop_value(buffer), 1);
}

}  // namespace
