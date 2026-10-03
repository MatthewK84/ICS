#include "ics/capture/rotating_writer.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <utility>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace ics::capture {
namespace {

constexpr mode_t kFileMode = 0640;

// For example tap0-20261003T170000.123456789Z.pcap.
[[nodiscard]] std::string file_name(const std::string& interface, const UtcTime first) {
  return std::format("{}-{:%Y%m%dT%H%M%S}Z.pcap", interface, first);
}

// Writes text to a new file at path and syncs it; false when it cannot. Each
// call runs even after one fails (a bad descriptor fails harmlessly), so one
// branch covers every failure.
[[nodiscard]] bool write_new_file(const std::filesystem::path& path, const std::string& text) noexcept {
  const timing::Fd file(::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, kFileMode));
  const bool written = ::write(file.get(), text.data(), text.size()) == static_cast<ssize_t>(text.size());
  const bool synced = ::fsync(file.get()) == 0;
  return written & synced;
}

}  // namespace

RotatingWriter::RotatingWriter(WriterSettings settings, timing::Fd directory, Sha256 hash)
    : settings_(std::move(settings)),
      directory_(std::move(directory)),
      hash_(std::move(hash)),
      buffer_(settings_.buffer_bytes) {}

Result<RotatingWriter> RotatingWriter::make(WriterSettings settings) {
  const std::uint64_t record = kRecordHeaderSize + settings.snaplen;
  const bool valid = !settings.interface.empty() && settings.interface.find('/') == std::string::npos &&
                     settings.limits.interval > Duration::zero() &&
                     settings.limits.max_bytes >= kFileHeaderSize + record && settings.buffer_bytes >= record;
  if (!valid) {
    return fail(Error::kInvalidArgument);
  }
  timing::Fd directory(::open(settings.directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
  if (!directory.valid()) {
    return fail(Error::kUnwritable);
  }
  Result<Sha256> hash = Sha256::make();
  if (!hash) {
    return fail(hash.error());
  }
  return RotatingWriter(std::move(settings), std::move(directory), std::move(*hash));
}

Result<std::optional<ClosedFile>> RotatingWriter::write(const Packet& packet) {
  if (packet.data.size() > settings_.snaplen) {
    return fail(Error::kInvalidArgument);
  }
  std::optional<ClosedFile> closed;
  if (file_.valid() && due(packet)) {
    Result<std::optional<ClosedFile>> previous = close();
    if (!previous) {
      return fail(previous.error());
    }
    closed = std::move(*previous);
  }
  if (!file_.valid()) {
    const Status opened = open_file(packet.time);
    if (!opened) {
      return fail(opened.error());
    }
  }
  const Status added = add(packet);
  if (!added) {
    return fail(added.error());
  }
  return closed;
}

Result<std::optional<ClosedFile>> RotatingWriter::close_if_due(const UtcTime now) {
  if (!file_.valid() || now - first_ < settings_.limits.interval) {
    return std::optional<ClosedFile>{};
  }
  return close();
}

Result<std::optional<ClosedFile>> RotatingWriter::close() {
  if (!file_.valid()) {
    return std::optional<ClosedFile>{};
  }
  const bool flushed = flush().has_value();
  const bool synced = ::fsync(file_.get()) == 0;
  file_ = timing::Fd();
  const Result<Digest> digest = hash_.finish();
  const bool hashed = digest.has_value();
  // Every step runs even after one fails, so one branch covers them all.
  if (!(flushed & synced & hashed)) {
    return fail(Error::kUnwritable);
  }
  const DigestHex hex = to_hex(*digest);
  std::filesystem::path sidecar = path_;
  sidecar += ".sha256";
  const bool recorded = write_new_file(sidecar, std::string(hex.begin(), hex.end()) + "  " +
                                                    path_.filename().string() + "\n");
  const bool listed = ::fsync(directory_.get()) == 0;
  if (!(recorded & listed)) {
    return fail(Error::kUnwritable);
  }
  return ClosedFile{std::move(path_), *digest, packets_, bytes_};
}

bool RotatingWriter::due(const Packet& packet) const noexcept {
  return packet.time - first_ >= settings_.limits.interval ||
         bytes_ + kRecordHeaderSize + packet.data.size() > settings_.limits.max_bytes;
}

Status RotatingWriter::open_file(const UtcTime first) {
  path_ = settings_.directory / file_name(settings_.interface, first);
  file_ = timing::Fd(::open(path_.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, kFileMode));
  if (!file_.valid()) {
    return fail(Error::kUnwritable);
  }
  first_ = first;
  packets_ = 0;
  bytes_ = kFileHeaderSize;
  append(file_header(settings_.snaplen, settings_.link_type));
  return {};
}

void RotatingWriter::append(const std::span<const std::byte> bytes) noexcept {
  std::ranges::copy(bytes, std::span(buffer_).subspan(buffered_).begin());
  buffered_ += bytes.size();
}

Status RotatingWriter::add(const Packet& packet) {
  const RecordHeader header = record_header(packet);
  if (buffer_.size() - buffered_ < header.size() + packet.data.size()) {
    const Status flushed = flush();
    if (!flushed) {
      return flushed;
    }
  }
  append(header);
  append(packet.data);
  ++packets_;
  bytes_ += header.size() + packet.data.size();
  return {};
}

Status RotatingWriter::flush() noexcept {
  std::span<const std::byte> pending = std::span<const std::byte>(buffer_).first(buffered_);
  hash_.update(pending);
  buffered_ = 0;
  // Each pass writes at least one byte or returns, so the loop ends.
  while (!pending.empty()) {
    const ssize_t written = ::write(file_.get(), pending.data(), pending.size());
    if (written <= 0) {
      return fail(Error::kUnwritable);
    }
    pending = pending.subspan(static_cast<std::size_t>(written));
  }
  return {};
}

}  // namespace ics::capture
