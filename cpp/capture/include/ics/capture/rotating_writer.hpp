#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "ics/capture/pcap_file.hpp"
#include "ics/capture/sha256.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/timing/unix_socket.hpp"

namespace ics::capture {

// When a file is closed and the next one started.
struct RotationLimits {
  // A file spans at most this long, from its first packet.
  Duration interval{};
  // A file holds at most this many bytes.
  std::uint64_t max_bytes = 0;
};

struct WriterSettings {
  // Where the files go. It must exist.
  std::filesystem::path directory;
  // The interface captured, which starts each file's name.
  std::string interface;
  std::uint32_t snaplen = 0;
  std::uint32_t link_type = kLinkEthernet;
  RotationLimits limits;
  // The bytes buffered before a write, at least one full-size record.
  std::size_t buffer_bytes = 0;
};

// A file the writer finished: flushed, synced to disk, and hashed, with the
// hash in path + ".sha256" in sha256sum's format.
struct ClosedFile {
  std::filesystem::path path;
  Digest sha256{};
  std::uint64_t packets = 0;
  std::uint64_t bytes = 0;
};

// Writes one interface's packets to a sequence of pcap files (ICS-020). Each
// file is named <interface>-<UTC time of its first packet>.pcap, for example
// tap0-20261003T170000.123456789Z.pcap, and is never overwritten. Its SHA-256
// is computed over the bytes as they are written, so nothing is read back.
// The buffer is allocated when the writer is made; writing a packet that
// does not close a file allocates nothing.
class RotatingWriter {
 public:
  // kInvalidArgument for settings that cannot work: an interface name that
  // is empty or holds a '/', no time limit, or room for less than one
  // full-size record in the file or the buffer. kUnwritable when the
  // directory cannot be opened; kUnavailable when OpenSSL cannot hash.
  [[nodiscard]] static Result<RotatingWriter> make(WriterSettings settings);

  // Writes packet, first closing the current file when packet comes its
  // limits.interval or more after the file's first packet, or would take it
  // past limits.max_bytes; a file is opened for the first packet after one
  // is closed. The file closed, if any. kUnwritable when a file cannot be
  // created, written or finished; the writer is then unusable.
  [[nodiscard]] Result<std::optional<ClosedFile>> write(const Packet& packet);

  // Closes the current file if its limits.interval has passed at now, so a
  // quiet interface still rotates on time.
  [[nodiscard]] Result<std::optional<ClosedFile>> close_if_due(UtcTime now);

  // Closes the current file, if one is open.
  [[nodiscard]] Result<std::optional<ClosedFile>> close();

  [[nodiscard]] bool file_open() const noexcept { return file_.valid(); }

 private:
  RotatingWriter(WriterSettings settings, timing::Fd directory, Sha256 hash);
  [[nodiscard]] bool due(const Packet& packet) const noexcept;
  [[nodiscard]] Status open_file(UtcTime first);
  void append(std::span<const std::byte> bytes) noexcept;
  [[nodiscard]] Status add(const Packet& packet);
  [[nodiscard]] Status flush() noexcept;

  WriterSettings settings_;
  timing::Fd directory_;
  Sha256 hash_;
  std::vector<std::byte> buffer_;
  std::size_t buffered_ = 0;
  timing::Fd file_;
  std::filesystem::path path_;
  UtcTime first_{};
  std::uint64_t packets_ = 0;
  std::uint64_t bytes_ = 0;
};

}  // namespace ics::capture
