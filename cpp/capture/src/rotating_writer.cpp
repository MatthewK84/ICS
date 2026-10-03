#include "ics/capture/rotating_writer.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <span>
#include <string>
#include <utility>

#include "ics/common/check.hpp"
#include "status.hpp"

namespace ics::capture {

using detail::status_of;

namespace {

// Writes the .sha256 file beside the capture file at path, as sha256sum
// prints it: the hash, two spaces and the file's name.
[[nodiscard]] Status write_sidecar(const std::filesystem::path& path, const std::string& hex) {
  const std::string line = hex + "  " + path.filename().native() + "\n";
  Result<OutputFile> file = OutputFile::create(path.native() + ".sha256");
  return file.and_then([&line](OutputFile& out) {
    return out.write(std::as_bytes(std::span(line))).and_then([&out] { return out.sync(); });
  });
}

}  // namespace

std::string file_name(const std::string_view prefix, const UtcTime time) {
  const auto seconds = std::chrono::floor<std::chrono::seconds>(time);
  return std::format("{}-{:%Y%m%dT%H%M%S}.{:09}Z.pcap", prefix, seconds, (time - seconds).count());
}

RotatingWriter::RotatingWriter(std::filesystem::path folder, std::string prefix, const FileFormat format,
                               const RotationLimits limits, Sha256 hash)
    : folder_(std::move(folder)), prefix_(std::move(prefix)), format_(format), limits_(limits), hash_(std::move(hash)) {
  buffer_.reserve(kWriteBufferBytes);
}

Result<RotatingWriter> RotatingWriter::open(std::filesystem::path folder, std::string prefix, const FileFormat format,
                                            const RotationLimits limits, const UtcTime now) {
  Result<Sha256> hash = Sha256::make();
  return hash.and_then([&](Sha256& made) {
    RotatingWriter writer(std::move(folder), std::move(prefix), format, limits, std::move(made));
    return writer.start(now).map([&writer] { return std::move(writer); });
  });
}

Status RotatingWriter::start(const UtcTime now) {
  // Several files can close for their size in one step, all at the same now:
  // each opens at least 1 ns after the last, so no two share a name.
  opened_ = std::max(now, opened_ + Duration(1));
  path_ = folder_ / file_name(prefix_, opened_);
  packets_ = 0;
  bytes_ = 0;
  buffer_.clear();
  Result<OutputFile> file = OutputFile::create(path_);
  if (!file) {
    return fail(file.error());
  }
  file_.emplace(std::move(*file));
  const FileHeader header = encode_file_header(format_);
  return hash_.restart().and_then([&] { return append(header); });
}

Status RotatingWriter::append(const std::span<const std::byte> bytes) {
  static_cast<void>(ics::check(bytes.size() <= buffer_.capacity()));
  const Status room = buffer_.size() + bytes.size() > buffer_.capacity() ? flush() : Status{};
  return room.map([&] {
    buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
    bytes_ += bytes.size();
  });
}

Status RotatingWriter::flush() {
  return hash_.update(buffer_).and_then([this] { return file_->write(buffer_); }).map([this] { buffer_.clear(); });
}

Result<std::optional<ClosedFile>> RotatingWriter::write(const Packet& packet, const UtcTime now) {
  if (!file_) {
    return fail(Error::kInvalidArgument);
  }
  const Result<RecordHeader> header =
      encode_record_header(packet.time, static_cast<std::uint32_t>(packet.bytes.size()), packet.original_length);
  if (!header) {
    return fail(header.error());
  }
  std::optional<ClosedFile> closed;
  if (packets_ > 0 && bytes_ + kRecordHeaderSize + packet.bytes.size() > limits_.max_bytes) {
    Result<ClosedFile> rotated = rotate(now);
    if (!rotated) {
      return fail(rotated.error());
    }
    closed = std::move(*rotated);
  }
  ++packets_;
  return append(*header).and_then([&] { return append(packet.bytes); }).map([&closed] { return std::move(closed); });
}

bool RotatingWriter::due(const UtcTime now) const noexcept { return now - opened_ >= limits_.max_age; }

Result<ClosedFile> RotatingWriter::rotate(const UtcTime now) {
  Result<ClosedFile> closed = finish();
  if (!closed) {
    return closed;
  }
  return start(now).map([&closed] { return std::move(*closed); });
}

Result<ClosedFile> RotatingWriter::close() { return finish(); }

// Ends the current file whatever happens, so a failed file is never written
// to again: syncs its bytes, then writes and syncs its .sha256 file.
Result<ClosedFile> RotatingWriter::finish() {
  if (!file_) {
    return fail(Error::kInvalidArgument);
  }
  const Status synced = flush().and_then([this] { return file_->sync(); });
  file_.reset();
  return synced.and_then([this] { return hash_.finish(); }).and_then([this](const Sha256Digest& digest) {
    std::string hex = to_hex(digest);
    return write_sidecar(path_, hex).map([&] { return ClosedFile{path_, std::move(hex), packets_, bytes_}; });
  });
}

}  // namespace ics::capture
