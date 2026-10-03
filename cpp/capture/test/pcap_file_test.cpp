#include "ics/capture/pcap_file.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <vector>

#include <gtest/gtest.h>

#include "ics/common/units.hpp"

namespace {

using ics::capture::Packet;

template <std::size_t N>
std::vector<unsigned> values(const std::array<std::byte, N>& bytes) {
  std::vector<unsigned> out;
  std::ranges::transform(bytes, std::back_inserter(out), [](const std::byte b) { return std::to_integer<unsigned>(b); });
  return out;
}

TEST(PcapFile, WritesTheNanosecondFileHeader) {
  // Magic a1b23c4d, version 2.4, zone and accuracy 0, snaplen 65535, Ethernet;
  // all little-endian.
  EXPECT_EQ(values(ics::capture::file_header(65535, ics::capture::kLinkEthernet)),
            (std::vector<unsigned>{0x4d, 0x3c, 0xb2, 0xa1, 2, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 0, 0, 1, 0, 0,
                                   0}));
}

TEST(PcapFile, WritesTheRecordHeader) {
  const std::array<std::byte, 3> data{};
  const Packet packet{ics::utc_from_ns(1'790'000'000'123'456'789), 60, data};
  // 1,790,000,000 s is 0x6AB1_3B80; 123,456,789 ns is 0x075B_CD15.
  EXPECT_EQ(values(ics::capture::record_header(packet)),
            (std::vector<unsigned>{0x80, 0x3b, 0xb1, 0x6a, 0x15, 0xcd, 0x5b, 0x07, 3, 0, 0, 0, 60, 0, 0, 0}));
}

TEST(PcapFile, WritesATimeBefore1970AsTheEpoch) {
  const Packet packet{ics::utc_from_ns(-5), 1, {}};
  EXPECT_EQ(values(ics::capture::record_header(packet)),
            (std::vector<unsigned>{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0}));
}

}  // namespace
