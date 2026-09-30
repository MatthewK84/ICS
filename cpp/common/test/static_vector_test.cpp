#include "ics/common/static_vector.hpp"

#include <functional>

#include <gtest/gtest.h>

#include "ics/testing/no_allocation_scope.hpp"

// Instantiates every member, so one no test calls shows up as uncovered.
template class ics::StaticVector<int, 3>;

namespace {

using Vector = ics::StaticVector<int, 3>;

// A vector holding 0, 1 and 2.
Vector full_vector() {
  Vector vector;
  for (int value = 0; value < 3; ++value) {
    EXPECT_TRUE(vector.push_back(value).has_value());
  }
  return vector;
}

TEST(StaticVector, StartsEmpty) {
  const Vector vector;
  EXPECT_EQ(Vector::capacity(), 3U);
  EXPECT_EQ(vector.size(), 0U);
  EXPECT_TRUE(vector.empty());
  EXPECT_FALSE(vector.full());
  EXPECT_TRUE(vector.span().empty());
}

TEST(StaticVector, PushesIndexesAndPopsWithoutAllocating) {
  Vector vector;
  const ics::testing::NoAllocationScope no_allocation;
  ASSERT_TRUE(vector.push_back(4).has_value());
  ASSERT_TRUE(vector.push_back(5).has_value());
  EXPECT_EQ(vector.size(), 2U);
  EXPECT_FALSE(vector.empty());

  const ics::Result<std::reference_wrapper<int>> first = vector.at(0);
  ASSERT_TRUE(first.has_value());
  first->get() = 6;
  EXPECT_EQ(vector.span().front(), 6);
  EXPECT_EQ(vector.span().back(), 5);

  const ics::Result<int> last = vector.pop_back();
  ASSERT_TRUE(last.has_value());
  EXPECT_EQ(*last, 5);
  EXPECT_EQ(vector.size(), 1U);
}

TEST(StaticVector, ReadsThroughAConstView) {
  const Vector vector = full_vector();
  const ics::testing::NoAllocationScope no_allocation;
  const ics::Result<std::reference_wrapper<const int>> last = vector.at(2);
  ASSERT_TRUE(last.has_value());
  EXPECT_EQ(last->get(), 2);
  EXPECT_EQ(vector.span().size(), 3U);
  EXPECT_EQ(vector.span()[1], 1);
}

TEST(StaticVector, RefusesAPushWhenFull) {
  Vector vector = full_vector();
  EXPECT_TRUE(vector.full());
  const ics::testing::NoAllocationScope no_allocation;
  const ics::Status pushed = vector.push_back(3);
  ASSERT_FALSE(pushed.has_value());
  EXPECT_EQ(pushed.error(), ics::Error::kFull);
  EXPECT_EQ(vector.size(), 3U);
  EXPECT_EQ(vector.span().back(), 2);
}

TEST(StaticVector, RefusesAPopWhenEmpty) {
  Vector vector;
  const ics::testing::NoAllocationScope no_allocation;
  const ics::Result<int> popped = vector.pop_back();
  ASSERT_FALSE(popped.has_value());
  EXPECT_EQ(popped.error(), ics::Error::kEmpty);
}

TEST(StaticVector, RefusesAnIndexPastTheEnd) {
  Vector vector;
  ASSERT_TRUE(vector.push_back(1).has_value());
  const Vector& view = vector;
  const ics::testing::NoAllocationScope no_allocation;
  const ics::Result<std::reference_wrapper<int>> mutable_element = vector.at(1);
  ASSERT_FALSE(mutable_element.has_value());
  EXPECT_EQ(mutable_element.error(), ics::Error::kOutOfRange);
  const ics::Result<std::reference_wrapper<const int>> const_element = view.at(3);
  ASSERT_FALSE(const_element.has_value());
  EXPECT_EQ(const_element.error(), ics::Error::kOutOfRange);
}

}  // namespace
