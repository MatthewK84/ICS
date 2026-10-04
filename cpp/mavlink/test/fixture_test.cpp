// The MAVLink adapter against real SITL traffic (ICS-021). fixtures/
// crossing.pcap is part of the TAP capture of a crossing engagement the SITL
// rig (ICS-018) flew with PX4 v1.17.0 (system 1) and ArduCopter 4.7.1
// (system 2): its first 6 s, and 39.5 s to 42 s, when ArduCopter gets GPS
// time and arms; only packets holding a message ICS reads are kept.
// fixtures/crossing.tsv lists what the rig's own decoder logged from the same
// packets, each line of which is in the run's truth.jsonl: every position,
// status text and refused command. See fixtures/README.md.
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "ics/capture/capture.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/frames/egm96.hpp"
#include "ics/frames/enu.hpp"
#include "ics/frames/geodetic.hpp"
#include "ics/mavlink/adapter.hpp"
#include "ics/mavlink/datagram.hpp"
#include "ics/mavlink/frame.hpp"

namespace ics::mavlink {
namespace {

using Row = std::vector<std::string>;

const frames::Egm96& geoid() {
  static const Result<frames::Egm96> grid = frames::Egm96::load(ICS_EGM96_PATH);
  return *grid;
}

// Feeds every packet's frames to the adapter.
class Replay final : public capture::PacketSink {
 public:
  explicit Replay(Adapter& adapter) : adapter_(adapter) {}

  Status accept(const capture::Packet& packet) override {
    adapter_.tick(packet.time, out_);
    const std::optional<Datagram> datagram = udp_datagram(packet.bytes);
    if (datagram) {
      FrameReader reader(datagram->payload);
      for (std::optional<Frame> frame = reader.next(); frame; frame = reader.next()) {
        adapter_.receive(*frame, packet.time, out_);
      }
    }
    return {};
  }

  Output out_;

 private:
  Adapter& adapter_;
};

Output replay_fixture() {
  Adapter adapter({.roles = {{.system = 1, .role = v1::ENTITY_ROLE_TARGET},
                             {.system = 2, .role = v1::ENTITY_ROLE_INTERCEPTOR}}},
                  geoid(), frames::EnuFrame(frames::Geodetic::make(Degrees(40.0), Degrees(-100.0), Meters(700.0)).value()));
  std::string reason;
  Result<capture::Capture> capture = capture::Capture::open_file(ICS_MAVLINK_FIXTURES "/crossing.pcap", reason);
  EXPECT_TRUE(capture.has_value()) << reason;
  Replay replay(adapter);
  if (!capture) {
    return {};
  }
  Result<std::size_t> count = capture->dispatch(replay, 1000);
  while (count && *count > 0) {
    count = capture->dispatch(replay, 1000);
  }
  return std::move(replay.out_);
}

const Output& replayed() {
  static const Output out = replay_fixture();
  return out;
}

std::vector<Row> logged(const std::string& kind) {
  std::ifstream in(ICS_MAVLINK_FIXTURES "/crossing.tsv");
  std::vector<Row> rows;
  for (std::string line; std::getline(in, line);) {
    Row row;
    std::istringstream fields(line);
    for (std::string field; std::getline(fields, field, '\t');) {
      row.push_back(field);
    }
    if (row.at(0) == kind) {
      rows.push_back(row);
    }
  }
  return rows;
}

TEST(CrossingFixture, ReproducesEveryLoggedPosition) {
  std::map<std::pair<std::string, std::uint32_t>, const v1::PliRecord*> records;
  for (const Position& position : replayed().positions) {
    records[{position.record.entity_id(), position.time_boot_ms}] = &position.record;
  }
  const std::vector<Row> positions = logged("position");
  ASSERT_EQ(positions.size(), 115U);
  for (const Row& row : positions) {
    const auto found = records.find({row.at(1), static_cast<std::uint32_t>(std::stoul(row.at(2)))});
    ASSERT_NE(found, records.end()) << "system " << row.at(1) << " at " << row.at(2) << " ms";
    const v1::GeodeticPoint& point = found->second->position();
    EXPECT_DOUBLE_EQ(point.latitude_deg(), static_cast<double>(std::stol(row.at(3))) * 1e-7);
    EXPECT_DOUBLE_EQ(point.longitude_deg(), static_cast<double>(std::stol(row.at(4))) * 1e-7);
    const frames::Geodetic where = frames::Geodetic::make(Degrees(point.latitude_deg()), Degrees(point.longitude_deg()),
                                                          Meters(point.height_ellipsoid_m()))
                                       .value();
    EXPECT_NEAR(geoid().msl_height(where).value(), static_cast<double>(std::stol(row.at(5))) * 1e-3, 1e-6);
    EXPECT_EQ(found->second->role(), row.at(1) == "1" ? v1::ENTITY_ROLE_TARGET : v1::ENTITY_ROLE_INTERCEPTOR);
  }
}

TEST(CrossingFixture, ReportsEveryLoggedTextAndRefusal) {
  std::map<std::tuple<std::string, std::string>, int> texts;
  std::map<std::tuple<std::string, std::uint32_t, std::uint32_t>, int> acks;
  for (const v1::PliEvent& event : replayed().events) {
    if (event.kind() == v1::PliEvent::KIND_STATUS_TEXT) {
      ++texts[{event.entity_id(), event.detail()}];
    } else if (event.kind() == v1::PliEvent::KIND_COMMAND_ACK) {
      ++acks[{event.entity_id(), event.command(), event.command_result()}];
    }
  }
  const std::vector<Row> logged_texts = logged("text");
  ASSERT_EQ(logged_texts.size(), 44U);
  for (const Row& row : logged_texts) {
    const std::tuple<std::string, std::string> key{row.at(1), row.at(2)};
    EXPECT_GE(texts[key]--, 1) << row.at(2);
  }
  const std::vector<Row> refusals = logged("refused");
  ASSERT_EQ(refusals.size(), 9U);
  for (const Row& row : refusals) {
    const auto key = std::make_tuple(row.at(1), static_cast<std::uint32_t>(std::stoul(row.at(2))),
                                     static_cast<std::uint32_t>(std::stoul(row.at(3))));
    EXPECT_GE(acks[key]--, 1) << row.at(2);
  }
}

TEST(CrossingFixture, ReportsArmingAndModes) {
  std::map<std::string, std::vector<std::string>> modes;
  std::map<std::string, int> armed;
  for (const v1::PliEvent& event : replayed().events) {
    if (event.kind() == v1::PliEvent::KIND_MODE_CHANGED) {
      modes[event.entity_id()].push_back(event.detail());
    } else if (event.kind() == v1::PliEvent::KIND_ARMED) {
      ++armed[event.entity_id()];
    }
  }
  EXPECT_EQ(modes["1"], (std::vector<std::string>{"AUTO.LOITER", "AUTO.MISSION"}));
  EXPECT_EQ(modes["2"], (std::vector<std::string>{"STABILIZE", "GUIDED", "AUTO"}));
  EXPECT_EQ(armed["1"], 1);
  EXPECT_EQ(armed["2"], 1);
}

TEST(CrossingFixture, TimesRecordsByTheVehiclesClockOnceItHasUtc) {
  std::map<std::string, std::vector<v1::PliTimeBasis>> bases;
  for (const Position& position : replayed().positions) {
    const v1::PliRecord& record = position.record;
    bases[record.entity_id()].push_back(record.time_basis());
    if (record.time_basis() == v1::PLI_TIME_BASIS_VEHICLE_GNSS) {
      const auto lag = std::chrono::nanoseconds(record.received_utc_ns() - record.valid_utc_ns());
      EXPECT_GE(lag, std::chrono::nanoseconds(0));
      EXPECT_LT(lag, std::chrono::seconds(1));
    }
  }
  // PX4 has UTC from the start; ArduCopter only once it has GPS, at about 41 s.
  EXPECT_EQ(bases["1"].front(), v1::PLI_TIME_BASIS_VEHICLE_GNSS);
  EXPECT_EQ(bases["2"].front(), v1::PLI_TIME_BASIS_RECEIPT);
  EXPECT_EQ(bases["2"].back(), v1::PLI_TIME_BASIS_VEHICLE_GNSS);
}

}  // namespace
}  // namespace ics::mavlink
