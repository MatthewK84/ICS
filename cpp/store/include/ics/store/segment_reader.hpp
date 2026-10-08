#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

#include "ics/common/error.hpp"
#include "ics/store/segment_format.hpp"
#include "ics/timing/unix_socket.hpp"

namespace ics::store {

// Reads a segment's entries in order (ICS-030), a bounded buffer at a time.
// It stops at the first entry that is incomplete or fails its checks: a torn
// tail after a crash, or the part of an entry a writer has not written yet.
// next() called again later reads the entries written since, so a reader can
// follow the segment ics-plid is writing.
class SegmentReader {
 public:
  // The bytes read from the file at a time, beyond one whole entry.
  static constexpr std::size_t kReadBytes = std::size_t{1} << 16U;

  // Opens the segment at path. kUnreadable when it cannot be opened.
  [[nodiscard]] static Result<SegmentReader> open(const std::filesystem::path& path);

  // The next entry, valid until the next call; nullopt at the end of the
  // sound entries. The first call reads the magic: a file that holds only
  // part of it, or nothing, is a segment torn as it was created, with no
  // entries. kUnreadable when a read fails; kMalformed, once, when the file
  // does not start with the magic.
  [[nodiscard]] Result<std::optional<Entry>> next();

  // Reads to the end of the sound entries and returns sound_bytes(). Fails as
  // next() does.
  [[nodiscard]] Result<std::uint64_t> skip_to_end();

  // The bytes from the start of the file to the end of the last entry
  // returned, or of the magic: the part a torn tail is cut back to.
  [[nodiscard]] std::uint64_t sound_bytes() const noexcept { return sound_bytes_; }

  // Whether the reader stopped at bytes that are not an entry, or at a torn
  // or wrong magic. It reads nothing more: a writer never writes past them.
  [[nodiscard]] bool stopped() const noexcept { return stopped_; }

 private:
  explicit SegmentReader(timing::Fd fd);
  [[nodiscard]] Status fill();
  [[nodiscard]] Status read_magic();
  [[nodiscard]] Status read_more();
  [[nodiscard]] std::span<const std::byte> unread() const noexcept;

  timing::Fd fd_;
  std::vector<std::byte> buffer_;
  // The bytes of buffer_ already returned.
  std::size_t used_ = 0;
  std::uint64_t sound_bytes_ = 0;
  bool has_magic_ = false;
  bool stopped_ = false;
};

// Cuts the segment at path back to its sound entries, so a segment torn by a
// crash ends at its last whole entry, and syncs it; a torn magic is cut to an
// empty file. Returns the bytes kept. Fails as SegmentReader does, or with
// kUnwritable when the file cannot be cut or synced.
[[nodiscard]] Result<std::uint64_t> cut_torn_tail(const std::filesystem::path& path);

}  // namespace ics::store
