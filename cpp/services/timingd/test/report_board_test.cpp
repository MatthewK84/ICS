#include "ics/timingd/report_board.hpp"

#include <chrono>
#include <cstddef>
#include <span>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "ics/testing/no_allocation_scope.hpp"

namespace {

using ics::timingd::ReportBoard;
using Wait = ics::timingd::ReportBoard::Wait;

constexpr std::chrono::milliseconds kNoWait{0};
constexpr std::chrono::seconds kLongWait{30};
const std::vector<std::byte> kFirst{std::byte{1}};
const std::vector<std::byte> kSecond{std::byte{2}, std::byte{3}};

TEST(ReportBoard, HasNothingBeforeTheFirstPost) {
  ReportBoard board;
  board.reserve(kSecond.size());
  std::vector<std::byte> out;
  EXPECT_EQ(board.take_newer(0, out, kNoWait).wait, Wait::kTimeout);
  EXPECT_TRUE(out.empty());
}

TEST(ReportBoard, HoldsOnlyTheLatestReport) {
  ReportBoard board;
  board.reserve(kSecond.size());
  board.post(kFirst);
  board.post(kSecond);
  std::vector<std::byte> out;
  const ReportBoard::Taken taken = board.take_newer(0, out, kNoWait);
  EXPECT_EQ(taken.wait, Wait::kReport);
  EXPECT_EQ(taken.number, 2U);
  EXPECT_EQ(out, kSecond);
  // Nothing newer than the report taken.
  EXPECT_EQ(board.take_newer(taken.number, out, kNoWait).wait, Wait::kTimeout);
  board.post(kFirst);
  EXPECT_EQ(board.take_newer(taken.number, out, kNoWait).number, 3U);
  EXPECT_EQ(out, kFirst);
}

// The poll loop's thread posts without allocating while a subscriber's
// thread waits. (The tests' gRPC threads may allocate at any time, so the
// scope counts this thread alone.)
TEST(ReportBoard, WakesAWaitingThreadWithoutAllocating) {
  ReportBoard board;
  board.reserve(kSecond.size());
  ReportBoard::Taken taken;
  std::vector<std::byte> out;
  std::thread waiter([&board, &taken, &out] { taken = board.take_newer(0, out, kLongWait); });
  {
    const ics::testing::NoAllocationScope no_allocation{ics::testing::ThisThreadOnly{}};
    board.post(kSecond);
    waiter.join();
  }
  EXPECT_EQ(taken.wait, Wait::kReport);
  EXPECT_EQ(out, kSecond);
}

TEST(ReportBoard, ClosingWakesTheWaitingThreads) {
  ReportBoard board;
  board.reserve(kFirst.size());
  ReportBoard::Taken taken;
  std::vector<std::byte> out;
  std::thread waiter([&board, &taken, &out] { taken = board.take_newer(0, out, kLongWait); });
  board.close();
  waiter.join();
  EXPECT_EQ(taken.wait, Wait::kClosed);
  // Closed is closed, even with a report the taker has not seen.
  board.post(kFirst);
  EXPECT_EQ(board.take_newer(0, out, kNoWait).wait, Wait::kClosed);
  EXPECT_TRUE(out.empty());
}

}  // namespace
