#include "ics/retime/sorties.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "capture_support.hpp"
#include "ics/capture/pcap_format.hpp"
#include "ics/frames/geodetic.hpp"
#include "support.hpp"
#include "udp_support.hpp"

namespace {

using ics::retime::collect;
using ics::retime::Vehicles;
using ics::timing::testing::TempDir;

const std::filesystem::path kCrossing = ICS_MAVLINK_FIXTURES "/crossing.pcap";

const ics::frames::Egm96& geoid() {
  static const ics::frames::Egm96 grid = ics::frames::Egm96::load(ICS_EGM96_PATH).value();
  return grid;
}

const ics::frames::EnuFrame kRange(
    ics::frames::Geodetic::make(ics::Degrees(-35.363), ics::Degrees(149.165), ics::Meters(600.0)).value());

// A pcap file of frames, each stamped time, with this link type.
void write_pcap(const std::filesystem::path& path, const std::vector<ics::capture::testing::Bytes>& frames,
                const std::uint32_t linktype = 1) {
  std::ofstream out(path, std::ios::binary);
  const auto header = ics::capture::encode_file_header({.snaplen = 65535, .linktype = linktype});
  out.write(reinterpret_cast<const char*>(header.data()), header.size());
  for (const ics::capture::testing::Bytes& frame : frames) {
    const auto record = ics::capture::encode_record_header(ics::utc_from_ns(1'790'000'000'000'000'000),
                                                           static_cast<std::uint32_t>(frame.size()),
                                                           static_cast<std::uint32_t>(frame.size()))
                            .value();
    out.write(reinterpret_cast<const char*>(record.data()), record.size());
    out.write(reinterpret_cast<const char*>(frame.data()), static_cast<std::streamsize>(frame.size()));
  }
}

TEST(Collect, SortsTheSitlCaptureByVehicleAndSortie) {
  std::string reason;
  const ics::mavlink::AdapterSettings settings{
      .roles = {{.system = 2, .role = ics::v1::ENTITY_ROLE_INTERCEPTOR}}};
  const Vehicles vehicles = collect(std::span(&kCrossing, 1), settings, geoid(), kRange, reason).value();
  ASSERT_EQ(vehicles.size(), 2U);
  for (const auto& entry : vehicles) {
    ASSERT_FALSE(entry.second.sorties.empty()) << static_cast<int>(entry.first);
    EXPECT_FALSE(entry.second.sorties[0].samples.empty());
  }
  ASSERT_FALSE(vehicles.at(2).sorties[0].positions.empty());
  EXPECT_EQ(vehicles.at(2).sorties[0].positions[0].record.role(), ics::v1::ENTITY_ROLE_INTERCEPTOR);
  EXPECT_EQ(vehicles.at(1).sorties[0].positions[0].record.role(), ics::v1::ENTITY_ROLE_OTHER);
}

TEST(Collect, CountsACaptureReplayedAgainAsANewSortie) {
  std::string reason;
  const std::vector<std::filesystem::path> twice{kCrossing, kCrossing};
  const Vehicles vehicles = collect(twice, {}, geoid(), kRange, reason).value();
  EXPECT_EQ(vehicles.at(2).sorties.size(), 2U);
}

TEST(Collect, SkipsFramesThatAreNotUdp) {
  const TempDir folder;
  const ics::capture::testing::Bytes arp =
      ics::capture::testing::ethernet(ics::capture::testing::ipv4_udp({std::byte{0}}), false, 0x0806);
  write_pcap(folder / "arp.pcap", {arp});
  const std::filesystem::path path = folder / "arp.pcap";
  std::string reason;
  EXPECT_TRUE(collect(std::span(&path, 1), {}, geoid(), kRange, reason).value().empty());
}

TEST(Collect, ExplainsACaptureItCannotReplay) {
  const TempDir folder;
  std::string reason;
  const std::filesystem::path missing = folder / "missing.pcap";
  EXPECT_EQ(collect(std::span(&missing, 1), {}, geoid(), kRange, reason).error(), ics::Error::kUnreadable);
  EXPECT_FALSE(reason.empty());
  const std::filesystem::path loopback = folder / "loopback.pcap";
  write_pcap(loopback, {}, 0);
  EXPECT_EQ(collect(std::span(&loopback, 1), {}, geoid(), kRange, reason).error(), ics::Error::kMalformed);
  EXPECT_NE(reason.find("Ethernet"), std::string::npos);
  // A capture cut off inside a record.
  const std::filesystem::path cut = folder / "cut.pcap";
  std::filesystem::copy_file(kCrossing, cut);
  std::filesystem::resize_file(cut, std::filesystem::file_size(kCrossing) - 5);
  EXPECT_EQ(collect(std::span(&cut, 1), {}, geoid(), kRange, reason).error(), ics::Error::kUnreadable);
  EXPECT_NE(reason.find("partway"), std::string::npos);
}

}  // namespace
