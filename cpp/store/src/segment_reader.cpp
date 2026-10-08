#include "ics/store/segment_reader.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

#include "ics/common/check.hpp"
#include "status.hpp"

namespace ics::store {

using detail::status_of;

namespace {

constexpr std::size_t kBufferBytes = kMaxEntryBytes + SegmentReader::kReadBytes;

// Cuts the file at path to kept bytes and syncs it.
[[nodiscard]] Status cut(const std::filesystem::path& path, const std::uint64_t kept) noexcept {
  const timing::Fd fd(::open(path.c_str(), O_WRONLY | O_CLOEXEC));
  return status_of(fd.valid(), Error::kUnwritable)
      .and_then([&] { return status_of(::ftruncate(fd.get(), static_cast<off_t>(kept)) == 0, Error::kUnwritable); })
      .and_then([&] { return status_of(::fsync(fd.get()) == 0, Error::kUnwritable); });
}

}  // namespace

SegmentReader::SegmentReader(timing::Fd fd) : fd_(std::move(fd)) { buffer_.reserve(kBufferBytes); }

Result<SegmentReader> SegmentReader::open(const std::filesystem::path& path) {
  timing::Fd fd(::open(path.c_str(), O_RDONLY | O_CLOEXEC));
  return status_of(fd.valid(), Error::kUnreadable).map([&fd] { return SegmentReader(std::move(fd)); });
}

std::span<const std::byte> SegmentReader::unread() const noexcept { return std::span(buffer_).subspan(used_); }

// Moves the unread bytes to the front of the buffer, then reads until the
// buffer is full or the file ends. A regular file returns fewer bytes than
// asked only at its end, so two reads reach it.
Status SegmentReader::fill() {
  static_cast<void>(ics::check(used_ <= buffer_.size()));
  buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(used_));
  used_ = 0;
  Status read;
  for (std::size_t attempt = 0; attempt < 2 && read && buffer_.size() < kBufferBytes; ++attempt) {
    const std::size_t start = buffer_.size();
    buffer_.resize(kBufferBytes);
    const ssize_t count = ::read(fd_.get(), std::span(buffer_).subspan(start).data(), kBufferBytes - start);
    buffer_.resize(start + static_cast<std::size_t>(std::max<ssize_t>(count, 0)));
    read = status_of(count >= 0, Error::kUnreadable);
  }
  return read;
}

// Checks the magic at the start of a first fill. Anything but the whole magic
// stops the reader.
Status SegmentReader::read_magic() {
  const Found found = parse_magic(unread());
  has_magic_ = found == Found::kEntry;
  stopped_ = !has_magic_;
  used_ = has_magic_ ? kSegmentMagic.size() : 0;
  static_cast<void>(ics::check(used_ <= buffer_.size()));
  sound_bytes_ = used_;
  return status_of(found != Found::kInvalid, Error::kMalformed);
}

Status SegmentReader::read_more() {
  return fill().and_then([this] { return has_magic_ ? Status{} : read_magic(); });
}

Result<std::optional<Entry>> SegmentReader::next() {
  if (stopped_) {
    return std::optional<Entry>{};
  }
  Parsed parsed = parse_entry(unread());
  if (parsed.found == Found::kIncomplete) {
    const Status read = read_more();
    if (!read) {
      return fail(read.error());
    }
    parsed = stopped_ ? Parsed{} : parse_entry(unread());
  }
  // A full buffer holds a whole entry, so after a fill an incomplete one is
  // all the file holds so far.
  stopped_ = stopped_ || parsed.found == Found::kInvalid;
  if (parsed.found != Found::kEntry) {
    return std::optional<Entry>{};
  }
  static_cast<void>(ics::check(parsed.size <= unread().size()));
  used_ += parsed.size;
  sound_bytes_ += parsed.size;
  return std::optional<Entry>{parsed.entry};
}

Result<std::uint64_t> SegmentReader::skip_to_end() {
  Result<std::optional<Entry>> entry = next();
  while (entry && entry->has_value()) {
    entry = next();
  }
  return entry.map([this](const std::optional<Entry>&) { return sound_bytes_; });
}

Result<std::uint64_t> cut_torn_tail(const std::filesystem::path& path) {
  return SegmentReader::open(path).and_then([](SegmentReader reader) { return reader.skip_to_end(); })
      .and_then([&path](const std::uint64_t kept) { return cut(path, kept).map([kept] { return kept; }); });
}

}  // namespace ics::store
