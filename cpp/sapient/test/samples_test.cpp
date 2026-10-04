// The sample messages (ICS-024; see samples/README.md): Dstl's ICD samples
// from the BSI Flex 335 v2.0 test harness, and a synthetic session. Each
// sample is read from its JSON, framed as a node sends it, read back from
// one stream in chunks of every size from 1 to 64 bytes, and adapted.
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "framing.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/frames/egm96.hpp"
#include "ics/frames/enu.hpp"
#include "ics/frames/geodetic.hpp"
#include "ics/sapient/adapter.hpp"
#include "ics/sapient/stream.hpp"

namespace ics::sapient {
namespace {

using std::chrono::seconds;

// 2026-10-04T12:00:00Z, and a second later, when the synthetic session is
// received.
const UtcTime kReceived = utc_from_ns(1'791'115'201'000'000'000);
constexpr std::size_t kLargestChunk = 64;

const frames::Egm96& geoid() {
  static const Result<frames::Egm96> grid = frames::Egm96::load(ICS_EGM96_PATH);
  EXPECT_TRUE(grid.has_value()) << ICS_EGM96_PATH;
  return *grid;
}

// The JSON samples in a folder and those under it, in path order.
std::vector<std::filesystem::path> samples(const std::string& folder) {
  std::vector<std::filesystem::path> paths;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(std::filesystem::path(ICS_SAPIENT_SAMPLES) /
                                                                         folder)) {
    if (entry.path().extension() == ".json") {
      paths.push_back(entry.path());
    }
  }
  std::ranges::sort(paths);
  return paths;
}

std::vector<Message> messages(const std::string& folder) {
  const std::vector<std::filesystem::path> paths = samples(folder);
  std::vector<Message> out;
  std::ranges::transform(paths, std::back_inserter(out), [](const std::filesystem::path& path) {
    return testing::from_json(testing::read_file(path.string()));
  });
  return out;
}

std::vector<std::byte> stream_of(const std::vector<Message>& sent) {
  std::vector<std::byte> stream;
  for (const Message& message : sent) {
    const std::vector<std::byte> framed = testing::frame(message);
    stream.insert(stream.end(), framed.begin(), framed.end());
  }
  return stream;
}

// The stream read in chunks of the size given; each message must come back
// as it was sent.
std::vector<Message> read_in_chunks(const std::vector<Message>& sent, const std::size_t chunk) {
  const std::vector<std::byte> stream = stream_of(sent);
  StreamReader reader;
  std::vector<Message> read;
  for (std::size_t at = 0; at < stream.size(); at += chunk) {
    std::vector<Message> fed = reader.feed(std::span(stream).subspan(at, std::min(chunk, stream.size() - at)));
    read.insert(read.end(), std::make_move_iterator(fed.begin()), std::make_move_iterator(fed.end()));
  }
  EXPECT_FALSE(reader.broken()) << chunk;
  EXPECT_EQ(read.size(), sent.size()) << chunk;
  for (std::size_t i = 0; i < std::min(read.size(), sent.size()); ++i) {
    EXPECT_EQ(read[i].SerializeAsString(), sent[i].SerializeAsString()) << chunk << " " << i;
  }
  return read;
}

Adapter adapter() {
  const frames::Geodetic origin =
      frames::Geodetic::make(Degrees(39.99999995743866), Degrees(-100.00000009493952), Meters(735.0)).value();
  return Adapter::make({.roles = {{.id = "N123AB", .role = v1::ENTITY_ROLE_TARGET}}}, geoid(),
                       frames::EnuFrame(origin))
      .value();
}

TEST(SapientSamples, ReadsEveryDstlSample) {
  const std::vector<Message> sent = messages("dstl");
  ASSERT_EQ(sent.size(), 83U);
  for (std::size_t chunk = 1; chunk <= kLargestChunk; ++chunk) {
    static_cast<void>(read_in_chunks(sent, chunk));
  }
}

TEST(SapientSamples, RefusesTheDstlDetectionLocations) {
  // Each of Dstl's detection reports with a location gives a latitude and
  // longitude as a UTM easting and northing, outside zone 30U, so none can
  // be placed. One has a range and bearing instead.
  Adapter sapient = adapter();
  for (const Message& message : messages("dstl")) {
    EXPECT_EQ(sapient.receive(message, kReceived), std::nullopt);
  }
  EXPECT_EQ(sapient.counts().unlocated, 4U);
  EXPECT_EQ(sapient.counts().range_bearing, 1U);
  EXPECT_EQ(sapient.counts().unidentified, 0U);
  EXPECT_EQ(sapient.counts().registrations_ignored, 0U);
}

std::vector<v1::PliRecord> adapt_session(const std::size_t chunk, AdapterCounts& counts) {
  Adapter sapient = adapter();
  std::vector<v1::PliRecord> records;
  for (const Message& message : read_in_chunks(messages("synthetic"), chunk)) {
    std::optional<v1::PliRecord> record = sapient.receive(message, kReceived);
    if (record) {
      records.push_back(std::move(*record));
    }
  }
  counts = sapient.counts();
  return records;
}

void expect_position(const v1::PliRecord& record, const double latitude, const double longitude,
                     const double tolerance) {
  EXPECT_NEAR(record.position().latitude_deg(), latitude, tolerance) << record.entity_id();
  EXPECT_NEAR(record.position().longitude_deg(), longitude, tolerance) << record.entity_id();
}

TEST(SapientSamples, AdaptsTheSyntheticSession) {
  for (std::size_t chunk = 1; chunk <= kLargestChunk; ++chunk) {
    AdapterCounts counts;
    const std::vector<v1::PliRecord> records = adapt_session(chunk, counts);
    ASSERT_EQ(records.size(), 4U) << chunk;
    EXPECT_EQ(counts.range_bearing, 1U);
    EXPECT_EQ(counts.unlocated, 0U);
  }
  AdapterCounts counts;
  const std::vector<v1::PliRecord> records = adapt_session(kLargestChunk, counts);
  ASSERT_EQ(records.size(), 4U);
  // In the registered zone, 14S; golden/frames/utm-geodetic.csv.
  const v1::PliRecord& utm = records[0];
  expect_position(utm, 39.99999995743866, -100.00000009493952, 1e-12);
  EXPECT_EQ(utm.position().height_ellipsoid_m(), 735.0);
  EXPECT_EQ(utm.role(), v1::ENTITY_ROLE_TARGET);
  EXPECT_EQ(utm.valid_utc_ns(), to_utc_ns(kReceived - seconds(1)));
  EXPECT_NEAR(utm.velocity_enu_mps().east(), 10.0, 1e-9);
  EXPECT_NEAR(utm.velocity_enu_mps().up(), 1.0, 1e-9);
  EXPECT_EQ(utm.horizontal_sigma_m(), 4.0);
  EXPECT_EQ(utm.vertical_sigma_m(), 5.0);
  // In zone 13S; GeoConvert gave its easting and northing for 39.99 N,
  // 102.01 W to 0.1 mm.
  const v1::PliRecord& adjacent = records[1];
  expect_position(adjacent, 39.99, -102.01, 1e-8);
  EXPECT_FALSE(adjacent.has_vertical_sigma_m());
  EXPECT_FALSE(adjacent.has_velocity_enu_mps());
  EXPECT_EQ(adjacent.role(), v1::ENTITY_ROLE_OTHER);
  // Above the geoid, which GeoidEval puts at -25.0529 m there.
  const v1::PliRecord& geoid_height = records[2];
  expect_position(geoid_height, 40.001, -99.999, 1e-12);
  EXPECT_NEAR(geoid_height.position().height_ellipsoid_m(), 1000.0 - 25.0529, 1e-4);
  EXPECT_NEAR(geoid_height.horizontal_sigma_m(), 1.110346520, 1e-6);
  EXPECT_EQ(geoid_height.vertical_sigma_m(), 2.0);
  // In radians.
  expect_position(records[3], 40.00391325603408, -99.99259440623128, 1e-12);
}

}  // namespace
}  // namespace ics::sapient
