#include "ics/timealign/align.hpp"

#include <cstdint>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/timealign/clock_fit.hpp"
#include "ics/v1/pli.pb.h"

namespace ics::timealign {
namespace {

// 2026-10-05T00:00:00Z.
constexpr std::int64_t kEpochNs = 1'791'158'400'000'000'000;

TEST(Align, TimesARecordAndAnEventByTheirBootTimes) {
  const ClockModel model(0, kEpochNs, 0.0);
  v1::PliRecord record;
  record.set_entity_id("2");
  record.set_valid_utc_ns(5);
  record.set_received_utc_ns(kEpochNs + 9);
  record.set_time_basis(v1::PLI_TIME_BASIS_RECEIPT);
  const std::optional<v1::PliRecord> timed = aligned(record, 3, model);
  ASSERT_TRUE(timed.has_value());
  EXPECT_EQ(timed->valid_utc_ns(), kEpochNs + 3'000);
  EXPECT_EQ(timed->time_basis(), v1::PLI_TIME_BASIS_VEHICLE_ALIGNED);
  EXPECT_EQ(timed->received_utc_ns(), kEpochNs + 9);
  EXPECT_EQ(timed->entity_id(), "2");
  v1::PliEvent event;
  event.set_kind(v1::PliEvent::KIND_ARMED);
  const std::optional<v1::PliEvent> stamped = aligned(event, 4, model);
  ASSERT_TRUE(stamped.has_value());
  EXPECT_EQ(stamped->time_utc_ns(), kEpochNs + 4'000);
  EXPECT_EQ(stamped->time_basis(), v1::PLI_TIME_BASIS_VEHICLE_ALIGNED);
  EXPECT_EQ(stamped->kind(), v1::PliEvent::KIND_ARMED);
}

TEST(Align, LeavesUntimedWhatTheModelCannotTime) {
  const ClockModel model(0, kEpochNs, 0.0);
  EXPECT_EQ(aligned(v1::PliRecord(), -1, model), std::nullopt);
  EXPECT_EQ(aligned(v1::PliEvent(), -1, model), std::nullopt);
}

TEST(SteppedClock, TimesByTheLatestPairAtOrBeforeABootTime) {
  // Given out of order, two at the same boot time.
  const std::vector<ClockSample> pairs{{.boot_us = 2'000'000, .utc_ns = kEpochNs + 2'000'000'500},
                                       {.boot_us = 1'000'000, .utc_ns = kEpochNs + 1'000'000'000},
                                       {.boot_us = 2'000'000, .utc_ns = kEpochNs + 2'000'000'700}};
  const Result<SteppedClock> clock = SteppedClock::make(pairs);
  ASSERT_TRUE(clock.has_value());
  // Before the first pair, the first pair's offset.
  EXPECT_EQ(clock->utc(0), utc_from_ns(kEpochNs));
  // At or after a pair, that pair's offset.
  EXPECT_EQ(clock->utc(1'000'000), utc_from_ns(kEpochNs + 1'000'000'000));
  EXPECT_EQ(clock->utc(1'999'999), utc_from_ns(kEpochNs + 1'999'999'000));
  // Of two pairs at one boot time, the one given last.
  EXPECT_EQ(clock->utc(3'000'000), utc_from_ns(kEpochNs + 3'000'000'700));
}

TEST(SteppedClock, TimesOnlyBootTimesAndPairsIcsTakes) {
  EXPECT_EQ(SteppedClock::make({}).error(), Error::kEmpty);
  for (const ClockSample& refused : {ClockSample{.boot_us = -1, .utc_ns = kEpochNs},
                                     ClockSample{.boot_us = kMaxBootUs + 1, .utc_ns = kEpochNs},
                                     ClockSample{.boot_us = 0, .utc_ns = -1},
                                     ClockSample{.boot_us = 0, .utc_ns = kMaxUtcNs + 1}}) {
    const std::vector<ClockSample> pairs{{.boot_us = 0, .utc_ns = kEpochNs}, refused};
    EXPECT_EQ(SteppedClock::make(pairs).error(), Error::kInvalidArgument) << refused.boot_us;
  }
  const std::vector<ClockSample> one{{.boot_us = 1'000'000, .utc_ns = kEpochNs}};
  const Result<SteppedClock> clock = SteppedClock::make(one);
  ASSERT_TRUE(clock.has_value());
  EXPECT_EQ(clock->utc(-1), std::nullopt);
  EXPECT_EQ(clock->utc(kMaxBootUs + 1), std::nullopt);
  // From 2100, or before 1970.
  EXPECT_EQ(clock->utc(kMaxBootUs), std::nullopt);
  const std::vector<ClockSample> early{{.boot_us = 10'000'000, .utc_ns = 1'000}};
  EXPECT_EQ(SteppedClock::make(early)->utc(0), std::nullopt);
}

TEST(SteppedClock, TimesARecordAndAnEventAsTheLiveAdaptersDo) {
  const std::vector<ClockSample> pair{{.boot_us = 1'000'000, .utc_ns = kEpochNs + 1'250'000'000}};
  const Result<SteppedClock> clock = SteppedClock::make(pair);
  ASSERT_TRUE(clock.has_value());
  v1::PliRecord record;
  record.set_received_utc_ns(kEpochNs + 9);
  record.set_time_basis(v1::PLI_TIME_BASIS_UNSPECIFIED);
  const std::optional<v1::PliRecord> timed = stepped(record, 1'500'000, *clock);
  ASSERT_TRUE(timed.has_value());
  EXPECT_EQ(timed->valid_utc_ns(), kEpochNs + 1'750'000'000);
  EXPECT_EQ(timed->time_basis(), v1::PLI_TIME_BASIS_VEHICLE_GNSS);
  EXPECT_EQ(timed->received_utc_ns(), kEpochNs + 9);
  const std::optional<v1::PliEvent> stamped = stepped(v1::PliEvent(), 2'000'000, *clock);
  ASSERT_TRUE(stamped.has_value());
  EXPECT_EQ(stamped->time_utc_ns(), kEpochNs + 2'250'000'000);
  EXPECT_EQ(stamped->time_basis(), v1::PLI_TIME_BASIS_VEHICLE_GNSS);
  EXPECT_EQ(stepped(v1::PliRecord(), -1, *clock), std::nullopt);
  EXPECT_EQ(stepped(v1::PliEvent(), -1, *clock), std::nullopt);
}

}  // namespace
}  // namespace ics::timealign
