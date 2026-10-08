#include "ics/retime/retime.hpp"

#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "retime_support.hpp"

namespace {

using ics::retime::LogTiming;
using ics::retime::Sortie;
using ics::retime::TimedLog;
using ics::retime::Vehicles;
using ics::retime::testing::kUtcNs;
using ics::retime::testing::pairs;
using ics::retime::testing::position;

constexpr std::int64_t kSecondUs = 1'000'000;
constexpr std::int64_t kTooLate = std::numeric_limits<std::int64_t>::max() / 2;

// Ten seconds of clock pairs and a position, and with far, a position whose
// boot time gives no UTC time ICS takes.
Sortie sortie(const std::int64_t first_us, const std::int64_t wobble_us, const bool far = false) {
  Sortie out;
  out.samples = pairs(first_us, 10, wobble_us);
  out.positions = {position(first_us + kSecondUs)};
  if (far) {
    out.positions.push_back(position(kTooLate));
  }
  return out;
}

ics::flightlog::LogContents log(const std::uint32_t system, const std::int64_t first_us) {
  ics::flightlog::LogContents out;
  out.system_id = system;
  ics::flightlog::LogRecord state;
  state.boot_us = first_us;
  state.record.set_entity_id(std::to_string(system));
  out.states.push_back(state);
  ics::flightlog::LogRecord fix = state;
  fix.boot_us = first_us + (3 * kSecondUs);
  out.gnss.push_back(fix);
  ics::flightlog::LogEvent event;
  event.boot_us = first_us + kSecondUs;
  out.events.push_back(event);
  // The log's own GNSS time sits 36 ms after the captures' clock.
  out.gnss_times.push_back({.boot_us = first_us + (2 * kSecondUs), .utc = ics::utc_from_ns(kUtcNs + ((first_us + (2 * kSecondUs)) * 1'000) + 36'000'000)});
  out.gnss_times.push_back({.boot_us = kTooLate, .utc = ics::utc_from_ns(kUtcNs)});
  return out;
}

Vehicles vehicles(const std::int64_t wobble_us) {
  Vehicles out;
  out[1].sorties = {sortie(0, wobble_us), sortie(100 * kSecondUs, wobble_us)};
  return out;
}

TEST(StraightModel, TakesOnlyAFitOnAStraightLine) {
  EXPECT_TRUE(ics::retime::straight_model(ics::timealign::fit_clock(pairs(0, 10))).has_value());
  EXPECT_FALSE(ics::retime::straight_model(ics::timealign::fit_clock(pairs(0, 10, 2'000))).has_value());
  EXPECT_FALSE(ics::retime::straight_model(ics::timealign::fit_clock({})).has_value());
}

TEST(AlignedPositions, TimesAStraightSortieByItsFit) {
  const std::vector<ics::v1::PliRecord> aligned = ics::retime::aligned_positions(sortie(0, 0, true));
  // The second position's boot time gives no UTC time ICS takes.
  ASSERT_EQ(aligned.size(), 1U);
  EXPECT_EQ(aligned[0].time_basis(), ics::v1::PLI_TIME_BASIS_VEHICLE_ALIGNED);
  EXPECT_EQ(aligned[0].valid_utc_ns(), kUtcNs + (kSecondUs * 1'000));
  EXPECT_TRUE(ics::retime::aligned_positions(sortie(0, 2'000, true)).empty());
}

TEST(TimeLog, MatchesTheSortieTheLogOverlapsMost) {
  const Vehicles straight = vehicles(0);
  const TimedLog timed = ics::retime::time_log(log(1, 101 * kSecondUs), straight);
  ASSERT_TRUE(timed.sortie.has_value());
  EXPECT_EQ(*timed.sortie, 1U);
  EXPECT_EQ(timed.pairs.size(), 10U);
  EXPECT_TRUE(timed.fit.has_value());
  EXPECT_EQ(ics::retime::timed_by(timed.timing), "fit");
  EXPECT_EQ(ics::retime::log_offset_ns(log(1, 101 * kSecondUs), timed.timing), std::optional<std::int64_t>(36'000'000));
}

TEST(TimeLog, FallsBackToTheClockPairsThenTheLog) {
  const Vehicles wobbly = vehicles(2'000);
  const TimedLog by_pairs = ics::retime::time_log(log(1, 2 * kSecondUs), wobbly);
  EXPECT_EQ(ics::retime::timed_by(by_pairs.timing), "pairs");
  EXPECT_TRUE(ics::retime::log_offset_ns(log(1, 2 * kSecondUs), by_pairs.timing).has_value());
  for (const ics::flightlog::LogContents& unmatched :
       {log(2, 0), log(300, 0), log(1, 1'000 * kSecondUs)}) {
    const TimedLog by_log = ics::retime::time_log(unmatched, wobbly);
    EXPECT_FALSE(by_log.sortie.has_value());
    EXPECT_TRUE(by_log.pairs.empty());
    EXPECT_FALSE(by_log.fit.has_value());
    EXPECT_EQ(ics::retime::timed_by(by_log.timing), "log");
    EXPECT_FALSE(ics::retime::log_offset_ns(unmatched, by_log.timing).has_value());
  }
}

TEST(Retimed, TimesARecordOrEventAsTheTimingSays) {
  const Vehicles straight = vehicles(0);
  const LogTiming fit = ics::retime::time_log(log(1, 0), straight).timing;
  const LogTiming stepped = ics::retime::time_log(log(1, 0), vehicles(2'000)).timing;
  const LogTiming none;
  ics::v1::PliRecord record;
  record.set_valid_utc_ns(7);
  ics::v1::PliEvent event;
  event.set_time_utc_ns(7);
  EXPECT_EQ(ics::retime::retimed(record, kSecondUs, fit).time_basis(), ics::v1::PLI_TIME_BASIS_VEHICLE_ALIGNED);
  EXPECT_EQ(ics::retime::retimed(event, kSecondUs, fit).time_basis(), ics::v1::PLI_TIME_BASIS_VEHICLE_ALIGNED);
  EXPECT_EQ(ics::retime::retimed(record, kSecondUs, stepped).time_basis(), ics::v1::PLI_TIME_BASIS_VEHICLE_GNSS);
  EXPECT_EQ(ics::retime::retimed(event, kSecondUs, stepped).time_basis(), ics::v1::PLI_TIME_BASIS_VEHICLE_GNSS);
  EXPECT_EQ(ics::retime::retimed(record, kSecondUs, none).valid_utc_ns(), 7);
  EXPECT_EQ(ics::retime::retimed(event, kSecondUs, none).time_utc_ns(), 7);
  // A boot time the timing gives no UTC time for leaves them as they were.
  EXPECT_EQ(ics::retime::retimed(record, kTooLate, fit).valid_utc_ns(), 7);
  EXPECT_EQ(ics::retime::retimed(event, kTooLate, stepped).time_utc_ns(), 7);
}

}  // namespace
