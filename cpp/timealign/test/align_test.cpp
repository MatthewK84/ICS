#include "ics/timealign/align.hpp"

#include <cstdint>
#include <optional>

#include <gtest/gtest.h>

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

}  // namespace
}  // namespace ics::timealign
