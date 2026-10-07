#include "ics/timing/camera_offsets.hpp"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

#include "ics/common/check.hpp"
#include "ics/common/error.hpp"
#include "ics/timing/unix_socket.hpp"
#include "ics/v1/time_quality.pb.h"

namespace ics::timing {
namespace {

constexpr mode_t kFileMode = 0644;

[[nodiscard]] bool usable(const v1::TimeQuality::CameraOffset& offset) noexcept {
  return !offset.camera_id().empty() && offset.offset_sigma_ns() >= 0 && offset.measured_utc_ns() > 0;
}

// Reads an open file to its end, or to one byte past the largest file
// read.
[[nodiscard]] Result<std::vector<std::byte>> read_bounded(const int fd) {
  std::vector<std::byte> out(kMaxCameraOffsetsBytes + 1);
  std::size_t done = 0;
  for (ssize_t got = 1; got > 0 && done < out.size();) {
    got = ::read(fd, std::span(out).subspan(done).data(), out.size() - done);
    if (got < 0) {
      return fail(Error::kUnreadable);
    }
    done += static_cast<std::size_t>(got);
  }
  out.resize(done);
  return out;
}

// Writes all of bytes to an open file, and syncs it.
[[nodiscard]] bool write_all(const int fd, const std::span<const std::byte> bytes) {
  std::size_t done = 0;
  while (done < bytes.size()) {
    const ssize_t put = ::write(fd, bytes.subspan(done).data(), bytes.size() - done);
    if (put <= 0) {
      return false;
    }
    done += static_cast<std::size_t>(put);
  }
  return ::fsync(fd) == 0;
}

}  // namespace

Result<CameraOffsets> parse_camera_offsets(const std::span<const std::byte> bytes) {
  v1::TimeQuality file;
  if (bytes.size() > kMaxCameraOffsetsBytes || !file.ParseFromArray(bytes.data(), static_cast<int>(bytes.size()))) {
    return fail(Error::kMalformed);
  }
  // Only station_id and camera_offsets: anything else, unknown fields
  // included, makes the message longer than they do alone.
  v1::TimeQuality only;
  only.set_station_id(file.station_id());
  *only.mutable_camera_offsets() = file.camera_offsets();
  const bool sound = !file.station_id().empty() && only.ByteSizeLong() == file.ByteSizeLong() &&
                     std::ranges::all_of(file.camera_offsets(), usable);
  if (!sound) {
    return fail(Error::kMalformed);
  }
  return CameraOffsets{.station_id = file.station_id(), .offsets = {file.camera_offsets().begin(), file.camera_offsets().end()}};
}

Result<CameraOffsets> read_camera_offsets(const std::filesystem::path& file) {
  const Fd fd(::open(file.c_str(), O_RDONLY | O_CLOEXEC));
  if (!fd.valid()) {
    return fail(errno == ENOENT ? Error::kEmpty : Error::kUnreadable);
  }
  const Result<std::vector<std::byte>> bytes = read_bounded(fd.get());
  return bytes ? parse_camera_offsets(*bytes) : fail(bytes.error());
}

Status write_camera_offsets(const std::filesystem::path& file, const CameraOffsets& offsets) {
  v1::TimeQuality message;
  message.set_station_id(offsets.station_id);
  for (const v1::TimeQuality::CameraOffset& offset : offsets.offsets) {
    *message.add_camera_offsets() = offset;
  }
  std::vector<std::byte> bytes(message.ByteSizeLong());
  static_cast<void>(check(message.SerializeToArray(bytes.data(), static_cast<int>(bytes.size()))));
  std::filesystem::path temporary = file;
  temporary += ".tmp";
  bool written = false;
  {
    const Fd fd(::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, kFileMode));
    written = fd.valid() && write_all(fd.get(), bytes);
  }
  if (!written || ::rename(temporary.c_str(), file.c_str()) != 0) {
    static_cast<void>(::unlink(temporary.c_str()));
    return fail(Error::kUnwritable);
  }
  return {};
}

CameraOffsets with_offset(CameraOffsets offsets, const v1::TimeQuality::CameraOffset& offset) {
  const auto same = std::ranges::find_if(
      offsets.offsets, [&offset](const v1::TimeQuality::CameraOffset& o) { return o.camera_id() == offset.camera_id(); });
  if (same == offsets.offsets.end()) {
    offsets.offsets.push_back(offset);
  } else {
    *same = offset;
  }
  return offsets;
}

std::optional<Result<CameraOffsets>> CameraOffsetsWatcher::poll() {
  std::error_code error;
  const std::filesystem::file_time_type time = std::filesystem::last_write_time(file_, error);
  const std::optional<std::filesystem::file_time_type> modified =
      error ? std::nullopt : std::optional<std::filesystem::file_time_type>(time);
  if (polled_ && modified == modified_) {
    return std::nullopt;
  }
  polled_ = true;
  modified_ = modified;
  return read_camera_offsets(file_);
}

}  // namespace ics::timing
