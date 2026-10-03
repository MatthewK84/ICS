#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"

namespace ics::capture {

// The classic pcap file format (ICS-020), with the nanosecond magic number,
// so each record carries UTC to the nanosecond. ics-capd writes the bytes
// itself, rather than through pcap_dump, so it can hash them as it goes and
// sees every write error. Fields are little-endian; readers tell the byte
// order from the magic number.

inline constexpr std::uint32_t kNanosecondMagic = 0xA1B23C4DU;
inline constexpr std::uint16_t kVersionMajor = 2;
inline constexpr std::uint16_t kVersionMinor = 4;
inline constexpr std::size_t kFileHeaderSize = 24;
inline constexpr std::size_t kRecordHeaderSize = 16;

using FileHeader = std::array<std::byte, kFileHeaderSize>;
using RecordHeader = std::array<std::byte, kRecordHeaderSize>;

// What every record of a file shares: the longest a record is cut to, and the
// link-layer type (LINKTYPE_ETHERNET is 1).
struct FileFormat {
  std::uint32_t snaplen = 0;
  std::uint32_t linktype = 0;
};

// The header that starts a pcap file.
[[nodiscard]] FileHeader encode_file_header(FileFormat format) noexcept;

// The header of a record captured at time, holding captured of the packet's
// original bytes. kOutOfRange when time is before 1970 or past 2106, which
// the 32-bit seconds field cannot hold.
[[nodiscard]] Result<RecordHeader> encode_record_header(UtcTime time, std::uint32_t captured,
                                                        std::uint32_t original) noexcept;

}  // namespace ics::capture
