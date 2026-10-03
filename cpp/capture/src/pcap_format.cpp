#include "ics/capture/pcap_format.hpp"

#include <chrono>
#include <cstdint>
#include <limits>
#include <span>

namespace ics::capture {
namespace {

constexpr unsigned kByteBits = 8;

// Writes value at the front of out, least significant byte first.
template <typename T>
void put_le(const std::span<std::byte, sizeof(T)> out, const T value) noexcept {
  for (std::size_t i = 0; i < sizeof(T); ++i) {
    out[i] = static_cast<std::byte>((value >> (kByteBits * i)) & T{0xFF});
  }
}

}  // namespace

FileHeader encode_file_header(const FileFormat format) noexcept {
  FileHeader out{};
  const std::span<std::byte, kFileHeaderSize> bytes(out);
  put_le<std::uint32_t>(bytes.subspan<0, 4>(), kNanosecondMagic);
  put_le<std::uint16_t>(bytes.subspan<4, 2>(), kVersionMajor);
  put_le<std::uint16_t>(bytes.subspan<6, 2>(), kVersionMinor);
  // Bytes 8 to 15, the time zone and accuracy, stay zero: times are UTC.
  put_le<std::uint32_t>(bytes.subspan<16, 4>(), format.snaplen);
  put_le<std::uint32_t>(bytes.subspan<20, 4>(), format.linktype);
  return out;
}

Result<RecordHeader> encode_record_header(const UtcTime time, const std::uint32_t captured,
                                          const std::uint32_t original) noexcept {
  const auto seconds = std::chrono::floor<std::chrono::seconds>(time);
  const std::int64_t whole = seconds.time_since_epoch().count();
  if (whole < 0 || whole > std::int64_t{std::numeric_limits<std::uint32_t>::max()}) {
    return fail(Error::kOutOfRange);
  }
  RecordHeader out{};
  const std::span<std::byte, kRecordHeaderSize> bytes(out);
  put_le<std::uint32_t>(bytes.subspan<0, 4>(), static_cast<std::uint32_t>(whole));
  put_le<std::uint32_t>(bytes.subspan<4, 4>(), static_cast<std::uint32_t>((time - seconds).count()));
  put_le<std::uint32_t>(bytes.subspan<8, 4>(), captured);
  put_le<std::uint32_t>(bytes.subspan<12, 4>(), original);
  return out;
}

}  // namespace ics::capture
