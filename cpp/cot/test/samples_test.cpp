// The sample feeds (ICS-022): each file in samples/ is the payload of one UDP
// datagram, as ATAK, WinTAK, a TAK Server or a UAS ground station sends it.
// They are synthetic, written from the public CoT schema; see
// samples/README.md. Each is read directly, and again from a pcap file of all
// of them written as ics-capd writes one (ICS-020).
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "ics/capture/capture.hpp"
#include "ics/capture/datagram.hpp"
#include "ics/capture/rotating_writer.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/cot/adapter.hpp"
#include "ics/cot/event.hpp"
#include "ics/frames/enu.hpp"
#include "ics/frames/geodetic.hpp"
#include "support.hpp"
#include "udp_support.hpp"

namespace ics::cot {
namespace {

using capture::testing::Bytes;
using std::chrono::milliseconds;
using std::chrono::seconds;

// 2026-10-04T12:00:00Z, and a second later, when each sample arrives.
const UtcTime kNoon = utc_from_ns(1'791'115'200'000'000'000);
const UtcTime kReceived = kNoon + seconds(1);

// CE90 and LE90 to one sigma (uncertainty_test.cpp).
constexpr double kCeFactor = 2.145966026289347;
constexpr double kLeFactor = 1.6448536269514715;

constexpr std::array<std::string_view, 10> kSamples{
    "atak-sa.xml",         "uas-track.xml",  "hostile-estimate.xml", "tak-server.xml",     "two-events.xml",
    "chat-and-delete.xml", "not-events.xml", "malformed.xml",        "invalid-points.xml", "bom.xml"};

Bytes sample(const std::string_view name) {
  std::ifstream in(std::filesystem::path(ICS_COT_SAMPLES) / name, std::ios::binary);
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  EXPECT_FALSE(text.empty()) << name;
  const std::span<const std::byte> bytes = std::as_bytes(std::span(text));
  return {bytes.begin(), bytes.end()};
}

// The range origin is 40 N, 100 W, 700 m; the UAS is the target.
const Adapter& adapter() {
  static const Adapter cot =
      Adapter::make({.roles = {{.uid = "UAS-EXAMPLE-7", .role = v1::ENTITY_ROLE_TARGET}}},
                    frames::EnuFrame(frames::Geodetic::make(Degrees(40.0), Degrees(-100.0), Meters(700.0)).value()))
          .value();
  return cot;
}

// What one payload gave: its events, and the records made from them.
struct Fed {
  Read read;
  std::vector<v1::PliRecord> records;
};

Fed feed(const std::span<const std::byte> payload, const UtcTime received) {
  Fed out{.read = read_events(payload), .records = {}};
  for (const Event& event : out.read.events) {
    std::optional<v1::PliRecord> record = adapter().record(event, received);
    if (record) {
      out.records.push_back(std::move(*record));
    }
  }
  return out;
}

Fed fed(const std::string_view name) {
  const Bytes payload = sample(name);
  return feed(payload, kReceived);
}

std::array<std::size_t, 3> counts(const ReadCounts& read) { return {read.malformed, read.not_events, read.invalid}; }

// The only record a sample gave.
v1::PliRecord only_record(const std::string_view name) {
  const Fed got = fed(name);
  EXPECT_EQ(got.records.size(), 1U) << name;
  return got.records.empty() ? v1::PliRecord() : got.records.front();
}

struct Expected {
  std::string_view name;
  std::size_t events = 0;
  std::size_t records = 0;
  ReadCounts counts;
};

TEST(SampleFeeds, GiveTheEventsRecordsAndCountsExpected) {
  for (const Expected& expected : {
           Expected{.name = "atak-sa.xml", .events = 1, .records = 1, .counts = {}},
           Expected{.name = "uas-track.xml", .events = 1, .records = 1, .counts = {}},
           Expected{.name = "hostile-estimate.xml", .events = 1, .records = 1, .counts = {}},
           Expected{.name = "tak-server.xml", .events = 1, .records = 1, .counts = {}},
           Expected{.name = "two-events.xml", .events = 2, .records = 2, .counts = {}},
           Expected{.name = "chat-and-delete.xml", .events = 2, .records = 0, .counts = {}},
           Expected{.name = "not-events.xml", .events = 0, .records = 0, .counts = {.not_events = 2}},
           Expected{.name = "malformed.xml", .events = 0, .records = 0, .counts = {.malformed = 1}},
           Expected{.name = "invalid-points.xml", .events = 0, .records = 0, .counts = {.invalid = 5}},
           Expected{.name = "bom.xml", .events = 1, .records = 1, .counts = {}},
       }) {
    const Fed got = fed(expected.name);
    EXPECT_EQ(got.read.events.size(), expected.events) << expected.name;
    EXPECT_EQ(got.records.size(), expected.records) << expected.name;
    EXPECT_EQ(counts(got.read.counts), counts(expected.counts)) << expected.name;
  }
}

TEST(SampleFeeds, AtakSituationalAwareness) {
  const v1::PliRecord record = only_record("atak-sa.xml");
  EXPECT_EQ(record.entity_id(), "ANDROID-EXAMPLE-0001");
  EXPECT_EQ(record.role(), v1::ENTITY_ROLE_OTHER);
  EXPECT_EQ(record.valid_utc_ns(), to_utc_ns(kNoon));
  EXPECT_EQ(record.time_basis(), v1::PLI_TIME_BASIS_VEHICLE_GNSS);
  EXPECT_EQ(record.position().latitude_deg(), 40.001);
  EXPECT_EQ(record.position().longitude_deg(), -100.002);
  EXPECT_EQ(record.position().height_ellipsoid_m(), 681.5);
  // Due east at 1.5 m/s, 200 m from the range origin, whose axes barely differ.
  EXPECT_NEAR(record.velocity_enu_mps().east(), 1.5, 1e-3);
  EXPECT_NEAR(record.velocity_enu_mps().north(), 0.0, 1e-3);
  EXPECT_NEAR(record.velocity_enu_mps().up(), 0.0, 1e-3);
  EXPECT_NEAR(record.horizontal_sigma_m(), 4.9 / kCeFactor, 1e-12);
  EXPECT_FALSE(record.has_vertical_sigma_m());
  EXPECT_EQ(record.fix_type(), v1::PliRecord::FIX_TYPE_THREE_DIMENSIONAL);
}

TEST(SampleFeeds, UasTrack) {
  const v1::PliRecord record = only_record("uas-track.xml");
  EXPECT_EQ(record.entity_id(), "UAS-EXAMPLE-7");
  EXPECT_EQ(record.role(), v1::ENTITY_ROLE_TARGET);
  EXPECT_EQ(record.valid_utc_ns(), to_utc_ns(kNoon + milliseconds(250)));
  EXPECT_EQ(record.time_basis(), v1::PLI_TIME_BASIS_VEHICLE_GNSS);
  EXPECT_EQ(record.position().height_ellipsoid_m(), 735.0);
  // North-east at 12 m/s, over the range origin.
  EXPECT_NEAR(record.velocity_enu_mps().east(), 12.0 / std::sqrt(2.0), 1e-9);
  EXPECT_NEAR(record.velocity_enu_mps().north(), 12.0 / std::sqrt(2.0), 1e-9);
  EXPECT_NEAR(record.velocity_enu_mps().up(), 0.0, 1e-9);
  // CE90 2.146 m and LE90 3.29 m are one-sigma errors of about 1 m and 2 m.
  EXPECT_NEAR(record.horizontal_sigma_m(), 1.0, 1e-4);
  EXPECT_NEAR(record.vertical_sigma_m(), 2.0, 1e-3);
}

TEST(SampleFeeds, HostileEstimateFromASenderWhoseClockIsWrong) {
  const v1::PliRecord record = only_record("hostile-estimate.xml");
  EXPECT_EQ(record.entity_id(), "EST-EXAMPLE-3");
  EXPECT_EQ(record.valid_utc_ns(), to_utc_ns(kReceived));
  EXPECT_EQ(record.time_basis(), v1::PLI_TIME_BASIS_RECEIPT);
  EXPECT_EQ(record.position().height_ellipsoid_m(), 0.0);
  EXPECT_FALSE(record.has_velocity_enu_mps());
  EXPECT_NEAR(record.horizontal_sigma_m(), 50.0 / kCeFactor, 1e-12);
  EXPECT_FALSE(record.has_vertical_sigma_m());
  EXPECT_EQ(record.fix_type(), v1::PliRecord::FIX_TYPE_OTHER);
}

TEST(SampleFeeds, TakServerRelayWithAnOffsetTime) {
  const v1::PliRecord record = only_record("tak-server.xml");
  EXPECT_EQ(record.entity_id(), "WINTAK-EXAMPLE-0002");
  EXPECT_EQ(record.valid_utc_ns(), to_utc_ns(kNoon + std::chrono::microseconds(250)));
  EXPECT_EQ(record.time_basis(), v1::PLI_TIME_BASIS_VEHICLE_GNSS);
  EXPECT_EQ(record.position().height_ellipsoid_m(), 690.25);
  // Standing still.
  EXPECT_TRUE(record.has_velocity_enu_mps());
  EXPECT_NEAR(std::hypot(record.velocity_enu_mps().east(), record.velocity_enu_mps().north(),
                         record.velocity_enu_mps().up()),
              0.0, 1e-12);
  EXPECT_NEAR(record.horizontal_sigma_m(), 10.0 / kCeFactor, 1e-12);
  EXPECT_NEAR(record.vertical_sigma_m(), 16.449 / kLeFactor, 1e-12);
  EXPECT_EQ(record.fix_type(), v1::PliRecord::FIX_TYPE_THREE_DIMENSIONAL);
}

TEST(SampleFeeds, TwoEventsInOneDatagram) {
  const Fed got = fed("two-events.xml");
  ASSERT_EQ(got.records.size(), 2U);
  EXPECT_EQ(got.records[0].entity_id(), "ANDROID-EXAMPLE-0004");
  EXPECT_NEAR(got.records[0].velocity_enu_mps().north(), -8.0, 1e-3);
  EXPECT_EQ(got.records[1].entity_id(), "ANDROID-EXAMPLE-0005");
  EXPECT_FALSE(got.records[1].has_velocity_enu_mps());
}

TEST(SampleFeeds, ByteOrderMarkCarriageReturnsAndPaddedNumbers) {
  const v1::PliRecord record = only_record("bom.xml");
  EXPECT_EQ(record.entity_id(), "BOM-EXAMPLE-1");
  EXPECT_EQ(record.valid_utc_ns(), to_utc_ns(kNoon + milliseconds(500)));
  EXPECT_EQ(record.position().latitude_deg(), 40.005);
  EXPECT_EQ(record.position().longitude_deg(), -100.005);
  EXPECT_EQ(record.position().height_ellipsoid_m(), -12.5);
  EXPECT_FALSE(record.has_horizontal_sigma_m());
  EXPECT_FALSE(record.has_vertical_sigma_m());
  EXPECT_EQ(record.fix_type(), v1::PliRecord::FIX_TYPE_THREE_DIMENSIONAL);
}

// Each sample as one datagram to the SA multicast group, a millisecond after
// the last, in a pcap file written as ics-capd writes one; returns its path.
std::filesystem::path write_capture(const timing::testing::TempDir& folder) {
  capture::RotatingWriter writer =
      capture::RotatingWriter::open(folder / "", "cot", {65535, 1}, {std::chrono::hours(1), 1'000'000}, kReceived)
          .value();
  for (std::size_t i = 0; i < kSamples.size(); ++i) {
    const Bytes frame = capture::testing::ethernet(
        capture::testing::ipv4_udp(sample(kSamples.at(i)), {.destination = {239, 2, 3, 1}, .destination_port = 6969}));
    const UtcTime at = kReceived + milliseconds(i);
    EXPECT_TRUE(writer.write({at, static_cast<std::uint32_t>(frame.size()), frame}, at).has_value());
  }
  return writer.close().value().path;
}

// Feeds each packet's datagram, as received at the packet's time.
class Replay final : public capture::PacketSink {
 public:
  Status accept(const capture::Packet& packet) override {
    const std::optional<capture::Datagram> datagram = capture::udp_datagram(packet.bytes);
    EXPECT_TRUE(datagram.has_value());
    if (datagram) {
      fed_.push_back(feed(datagram->payload, packet.time));
    }
    return {};
  }

  std::vector<Fed> fed_;
};

TEST(SampleFeeds, ReadTheSameFromACapture) {
  const timing::testing::TempDir folder;
  std::string reason;
  Result<capture::Capture> capture = capture::Capture::open_file(write_capture(folder), reason);
  ASSERT_TRUE(capture.has_value()) << reason;
  Replay replay;
  EXPECT_EQ(capture->dispatch(replay, 100).value(), kSamples.size());
  ASSERT_EQ(replay.fed_.size(), kSamples.size());
  std::size_t records = 0;
  for (std::size_t i = 0; i < kSamples.size(); ++i) {
    const Bytes payload = sample(kSamples.at(i));
    const Fed direct = feed(payload, kReceived + milliseconds(i));
    const Fed& replayed = replay.fed_.at(i);
    EXPECT_EQ(replayed.read.events.size(), direct.read.events.size()) << kSamples.at(i);
    EXPECT_EQ(counts(replayed.read.counts), counts(direct.read.counts)) << kSamples.at(i);
    ASSERT_EQ(replayed.records.size(), direct.records.size()) << kSamples.at(i);
    for (std::size_t j = 0; j < direct.records.size(); ++j) {
      EXPECT_EQ(replayed.records.at(j).SerializeAsString(), direct.records.at(j).SerializeAsString());
    }
    records += replayed.records.size();
  }
  EXPECT_EQ(records, 7U);
}

}  // namespace
}  // namespace ics::cot
