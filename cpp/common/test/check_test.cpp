#include "ics/common/check.hpp"

#include <string>

#include <gtest/gtest.h>

#include "ics/testing/no_allocation_scope.hpp"

namespace {

// Calls ics::check with allocations counted, and returns its result.
bool check_without_allocating(const bool condition) {
  const ics::testing::NoAllocationScope no_allocation;
  return ics::check(condition);
}

TEST(Check, PassesQuietlyWhenTheConditionHolds) {
  ::testing::internal::CaptureStderr();
  const bool held = check_without_allocating(true);
  const std::string report = ::testing::internal::GetCapturedStderr();
  EXPECT_TRUE(held);
  EXPECT_EQ(report, "");
}

TEST(Check, FailsAndNamesTheCallerWhenTheConditionIsFalse) {
  ::testing::internal::CaptureStderr();
  const bool held = check_without_allocating(false);
  const std::string report = ::testing::internal::GetCapturedStderr();
  EXPECT_FALSE(held);
  EXPECT_EQ(report.rfind("ICS check failed at ", 0), 0U) << report;
  EXPECT_NE(report.find("check_test.cpp:"), std::string::npos) << report;
  EXPECT_NE(report.find("check_without_allocating"), std::string::npos) << report;
}

}  // namespace
