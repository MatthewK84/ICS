#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "ics/capture/capture.hpp"
#include "ics/capture/output_file.hpp"
#include "ics/capture/pcap_format.hpp"
#include "ics/capture/sha256.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"

namespace ics::capture {

// When a capture file is closed and the next one opened.
struct RotationLimits {
  // The longest a file stays open.
  Duration max_age{};
  // The largest a file grows. A record that would pass it goes in the next
  // file, unless the file holds no record yet.
  std::uint64_t max_bytes = 0;
};

// A capture file that was closed: its bytes are on the disk, and so is its
// SHA-256 in <path>.sha256, in sha256sum's format.
struct ClosedFile {
  std::filesystem::path path;
  std::string sha256;
  std::uint64_t packets = 0;
  std::uint64_t bytes = 0;
};

// The bytes buffered before they are hashed and written.
inline constexpr std::size_t kWriteBufferBytes = std::size_t{1} << 20U;

// Writes one interface's packets to a series of pcap files (ICS-020), named
// <prefix>-<UTC time it opened>.pcap, such as
// tap0-20261003T171500.123456789Z.pcap, in one folder. A file that opens in
// the same nanosecond as the last is named 1 ns later. Each file is hashed as
// its bytes are written, so nothing is read back, and closed by rotate() or
// close(): its bytes are synced, then its .sha256 file written and synced.
// Allocates when a file opens, not per packet.
class RotatingWriter {
 public:
  // Opens the first file at now. Fails as OutputFile::create, Sha256::make
  // and write() do.
  [[nodiscard]] static Result<RotatingWriter> open(std::filesystem::path folder, std::string prefix,
                                                   FileFormat format, RotationLimits limits, UtcTime now);

  // Adds packet to the current file. When it would pass max_bytes, closes
  // the file first and opens the next at now, returning the closed one.
  // kOutOfRange for a packet time a pcap file cannot hold; kUnwritable and
  // kUnavailable as OutputFile and Sha256 fail.
  [[nodiscard]] Result<std::optional<ClosedFile>> write(const Packet& packet, UtcTime now);

  // Whether the current file has been open for max_age at now.
  [[nodiscard]] bool due(UtcTime now) const noexcept;

  // Closes the current file and opens the next at now.
  [[nodiscard]] Result<ClosedFile> rotate(UtcTime now);

  // Closes the current file and opens none; write() and rotate() then fail
  // with kInvalidArgument.
  [[nodiscard]] Result<ClosedFile> close();

  [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

 private:
  RotatingWriter(std::filesystem::path folder, std::string prefix, FileFormat format, RotationLimits limits,
                 Sha256 hash);
  [[nodiscard]] Status start(UtcTime now);
  [[nodiscard]] Status append(std::span<const std::byte> bytes);
  [[nodiscard]] Status flush();
  [[nodiscard]] Result<ClosedFile> finish();

  std::filesystem::path folder_;
  std::string prefix_;
  FileFormat format_;
  RotationLimits limits_;
  Sha256 hash_;
  std::vector<std::byte> buffer_;
  std::optional<OutputFile> file_;
  std::filesystem::path path_;
  // When the current file opened; before any, the earliest time there is.
  UtcTime opened_ = UtcTime::min();
  std::uint64_t packets_ = 0;
  std::uint64_t bytes_ = 0;
};

// The name of the file a writer opens at time, such as
// tap0-20261003T171500.123456789Z.pcap.
[[nodiscard]] std::string file_name(std::string_view prefix, UtcTime time);

}  // namespace ics::capture
