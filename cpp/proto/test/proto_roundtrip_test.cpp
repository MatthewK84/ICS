#include "ics/toolchain_check/v1/sample.pb.h"
#include "ics/toolchain_check/v1/sample_set.pb.h"

#include <cstdint>
#include <string>

#include <gtest/gtest.h>

namespace {

using ics::toolchain_check::v1::Sample;
using ics::toolchain_check::v1::SampleSet;

constexpr std::int64_t kTimeUtcNs = 1'790'000'000'123'456'789;
constexpr std::uint32_t kReadingCount = 4;

SampleSet make_sample_set() {
  SampleSet set;
  set.set_station_id("north-1");
  Sample& ranged = *set.add_samples();
  ranged.set_time_utc_ns(kTimeUtcNs);
  ranged.set_kind(Sample::KIND_RANGE);
  ranged.add_values(1523.5);
  ranged.add_values(1524.25);
  ranged.mutable_position()->set_east_m(10.0);
  ranged.mutable_position()->set_north_m(-5.0);
  ranged.mutable_position()->set_up_m(2.5);
  ranged.set_reading_count(kReadingCount);
  Sample& angled = *set.add_samples();
  angled.set_kind(Sample::KIND_ANGLE);
  angled.set_note("sun in view");
  return set;
}

// Serializes and parses back; a failure in either step fails the test.
template <typename Message>
Message round_trip(const Message& original) {
  std::string wire;
  EXPECT_TRUE(original.SerializeToString(&wire));
  Message parsed;
  EXPECT_TRUE(parsed.ParseFromString(wire));
  return parsed;
}

TEST(ProtoRoundTrip, KeepsEveryField) {
  const SampleSet parsed = round_trip(make_sample_set());
  ASSERT_EQ(parsed.samples_size(), 2);
  const Sample& ranged = parsed.samples(0);
  EXPECT_EQ(parsed.station_id(), "north-1");
  EXPECT_EQ(ranged.time_utc_ns(), kTimeUtcNs);
  EXPECT_EQ(ranged.kind(), Sample::KIND_RANGE);
  ASSERT_EQ(ranged.values_size(), 2);
  EXPECT_DOUBLE_EQ(ranged.values(1), 1524.25);
  EXPECT_DOUBLE_EQ(ranged.position().north_m(), -5.0);
  EXPECT_EQ(ranged.detail_case(), Sample::kReadingCount);
  EXPECT_EQ(ranged.reading_count(), kReadingCount);
  EXPECT_EQ(parsed.samples(1).note(), "sun in view");
}

TEST(ProtoRoundTrip, ReadsAnEmptyMessageAsDefaults) {
  Sample parsed;
  EXPECT_TRUE(parsed.ParseFromString(std::string{}));
  EXPECT_EQ(parsed.kind(), Sample::KIND_UNSPECIFIED);
  EXPECT_EQ(parsed.detail_case(), Sample::DETAIL_NOT_SET);
  EXPECT_FALSE(parsed.has_position());
}

}  // namespace
