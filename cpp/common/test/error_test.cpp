#include "ics/common/error.hpp"

#include <cstdint>

#include <gtest/gtest.h>

// Codes reach logs and run records, so none may ever be renumbered.
static_assert(static_cast<std::uint16_t>(ics::Error::kInvalidArgument) == 1);
static_assert(static_cast<std::uint16_t>(ics::Error::kOutOfRange) == 2);
static_assert(static_cast<std::uint16_t>(ics::Error::kFull) == 3);
static_assert(static_cast<std::uint16_t>(ics::Error::kEmpty) == 4);
static_assert(static_cast<std::uint16_t>(ics::Error::kStaleHandle) == 5);

namespace {

TEST(Error, NamesEveryCode) {
  EXPECT_EQ(ics::to_string(ics::Error::kInvalidArgument), "invalid argument");
  EXPECT_EQ(ics::to_string(ics::Error::kOutOfRange), "out of range");
  EXPECT_EQ(ics::to_string(ics::Error::kFull), "full");
  EXPECT_EQ(ics::to_string(ics::Error::kEmpty), "empty");
  EXPECT_EQ(ics::to_string(ics::Error::kStaleHandle), "stale handle");
}

TEST(Error, NamesAnUnlistedCodeUnknown) {
  // The enum has a fixed underlying type, so any 16-bit value is valid.
  EXPECT_EQ(ics::to_string(static_cast<ics::Error>(0)), "unknown");
}

TEST(Error, FailMakesAnErrorResult) {
  const ics::Result<int> result = ics::fail(ics::Error::kEmpty);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), ics::Error::kEmpty);
}

}  // namespace
