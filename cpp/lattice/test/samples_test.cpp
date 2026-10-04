// The synthetic sample stream (ICS-023): read in chunks of every size from 1
// to 64 bytes, as libcurl might hand it over, then decoded and adapted. See
// samples/README.md.
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/frames/enu.hpp"
#include "ics/frames/geodetic.hpp"
#include "ics/lattice/adapter.hpp"
#include "ics/lattice/event.hpp"
#include "ics/lattice/sse.hpp"

namespace ics::lattice {
namespace {

// 2026-10-04T12:00:00Z, and a second later, when the stream arrives.
const UtcTime kNoon = utc_from_ns(1'791'115'200'000'000'000);
const UtcTime kReceived = kNoon + std::chrono::seconds(1);
constexpr std::size_t kLargestChunk = 64;

std::string sample_stream() {
  std::ifstream in(std::filesystem::path(ICS_LATTICE_SAMPLES) / "entity-stream.txt", std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

struct Read {
  std::vector<SseEvent> events;
  std::size_t malformed = 0;
  std::vector<Event> entity_events;
  std::vector<v1::PliRecord> records;
};

Read read_in_chunks(const std::string_view stream, const std::size_t chunk) {
  const Adapter adapter =
      Adapter::make({}, frames::EnuFrame(frames::Geodetic::make(Degrees(40.0), Degrees(-100.0), Meters(700.0)).value()))
          .value();
  SseReader reader;
  Read out;
  for (std::size_t at = 0; at < stream.size(); at += chunk) {
    for (SseEvent& event : reader.feed(stream.substr(at, chunk))) {
      out.events.push_back(std::move(event));
    }
  }
  for (const SseEvent& event : out.events) {
    const Result<std::optional<Event>> decoded = decode_event(event.data);
    out.malformed += decoded.has_value() ? 0U : 1U;
    if (decoded.has_value() && decoded->has_value()) {
      out.entity_events.push_back(**decoded);
      std::optional<v1::PliRecord> record = adapter.record(**decoded, kReceived);
      if (record) {
        out.records.push_back(std::move(*record));
      }
    }
  }
  return out;
}

TEST(SampleStream, ReadsTheSameInChunksOfAnySize) {
  const std::string stream = sample_stream();
  ASSERT_FALSE(stream.empty());
  const Read whole = read_in_chunks(stream, stream.size());
  ASSERT_EQ(whole.events.size(), 6U);
  EXPECT_EQ(whole.events[2].event, "entity");
  EXPECT_EQ(whole.entity_events.size(), 4U);
  EXPECT_EQ(whole.malformed, 1U);
  ASSERT_EQ(whole.records.size(), 2U);
  for (std::size_t chunk = 1; chunk <= kLargestChunk; ++chunk) {
    const Read pieces = read_in_chunks(stream, chunk);
    ASSERT_EQ(pieces.records.size(), whole.records.size()) << chunk;
    for (std::size_t i = 0; i < whole.records.size(); ++i) {
      EXPECT_EQ(pieces.records[i].SerializeAsString(), whole.records[i].SerializeAsString()) << chunk;
    }
  }
}

TEST(SampleStream, AssetWithAFullPosition) {
  const v1::PliRecord record = read_in_chunks(sample_stream(), kLargestChunk).records.at(0);
  EXPECT_EQ(record.entity_id(), "example-asset-1");
  EXPECT_EQ(record.source(), v1::PLI_SOURCE_LATTICE);
  EXPECT_EQ(record.valid_utc_ns(), to_utc_ns(kNoon));
  EXPECT_EQ(record.time_basis(), v1::PLI_TIME_BASIS_VEHICLE_GNSS);
  EXPECT_EQ(record.position().latitude_deg(), 40.001);
  EXPECT_EQ(record.position().longitude_deg(), -100.002);
  EXPECT_EQ(record.position().height_ellipsoid_m(), 735.5);
  // 200 m from the range origin, whose axes barely differ.
  EXPECT_NEAR(record.velocity_enu_mps().east(), 12.0, 1e-3);
  EXPECT_NEAR(record.velocity_enu_mps().north(), 5.0, 1e-3);
  EXPECT_NEAR(record.velocity_enu_mps().up(), -0.5, 1e-3);
  EXPECT_EQ(record.horizontal_sigma_m(), 2.0);
  EXPECT_EQ(record.vertical_sigma_m(), 3.0);
  EXPECT_EQ(record.fix_type(), v1::PliRecord::FIX_TYPE_OTHER);
}

TEST(SampleStream, TrackWithNoHeightFromASourceWhoseClockIsWrong) {
  const v1::PliRecord record = read_in_chunks(sample_stream(), kLargestChunk).records.at(1);
  EXPECT_EQ(record.entity_id(), "example-track-7");
  EXPECT_EQ(record.valid_utc_ns(), to_utc_ns(kReceived));
  EXPECT_EQ(record.time_basis(), v1::PLI_TIME_BASIS_RECEIPT);
  EXPECT_EQ(record.position().height_ellipsoid_m(), 0.0);
  EXPECT_FALSE(record.has_velocity_enu_mps());
  // The larger eigenvalue of [[100, 20], [20, 64]].
  EXPECT_NEAR(record.horizontal_sigma_m(), std::sqrt(82.0 + std::hypot(18.0, 20.0)), 1e-12);
  EXPECT_FALSE(record.has_vertical_sigma_m());
}

}  // namespace
}  // namespace ics::lattice
