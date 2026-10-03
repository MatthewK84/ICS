#include "ics/capture/pcap_format.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include "ics/common/units.hpp"

namespace {

using ics::capture::encode_file_header;
using ics::capture::encode_record_header;
using ics::capture::FileFormat;

template <std::size_t N>
std::array<std::byte, N> bytes_of(const std::array<unsigned, N>& values) {
  std::array<std::byte, N> out{};
  for (std::size_t i = 0; i < N; ++i) {
    out.at(i) = static_cast<std::byte>(values.at(i));
  }
  return out;
}

TEST(PcapFormat, EncodesTheFileHeaderLittleEndianWithTheNanosecondMagic) {
  // snaplen 65535, LINKTYPE_ETHERNET.
  EXPECT_EQ(encode_file_header(FileFormat{65535, 1}),
            bytes_of<24>({0x4D, 0x3C, 0xB2, 0xA1, 2, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0,  //
                          0xFF, 0xFF, 0, 0, 1, 0, 0, 0}));
}

TEST(PcapFormat, EncodesARecordHeaderToTheNanosecond) {
  // 2026-10-03T17:15:00.123456789Z is 1791047700 s (0x6AC13814) and
  // 123456789 ns (0x075BCD15).
  const ics::UtcTime time = ics::utc_from_ns(1'791'047'700'123'456'789);
  const ics::Result<ics::capture::RecordHeader> header = encode_record_header(time, 60, 1514);
  ASSERT_TRUE(header.has_value());
  EXPECT_EQ(*header, bytes_of<16>({0x14, 0x38, 0xC1, 0x6A, 0x15, 0xCD, 0x5B, 0x07,  //
                                   60, 0, 0, 0, 0xEA, 0x05, 0, 0}));
}

TEST(PcapFormat, RefusesATimeTheSecondsFieldCannotHold) {
  constexpr std::int64_t kNanosPerSecond = 1'000'000'000;
  const std::int64_t last = std::int64_t{std::numeric_limits<std::uint32_t>::max()} * kNanosPerSecond;
  EXPECT_TRUE(encode_record_header(ics::utc_from_ns(0), 1, 1).has_value());
  EXPECT_TRUE(encode_record_header(ics::utc_from_ns(last), 1, 1).has_value());
  EXPECT_EQ(encode_record_header(ics::utc_from_ns(-1), 1, 1).error(), ics::Error::kOutOfRange);
  EXPECT_EQ(encode_record_header(ics::utc_from_ns(last + kNanosPerSecond), 1, 1).error(), ics::Error::kOutOfRange);
}

}  // namespace
