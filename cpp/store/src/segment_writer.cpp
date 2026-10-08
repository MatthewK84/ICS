#include "ics/store/segment_writer.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "ics/common/check.hpp"
#include "ics/store/segment_format.hpp"
#include "files.hpp"

namespace ics::store {

using detail::sync_folder;

namespace {

constexpr std::string_view kPrefix = "pli-";
constexpr std::string_view kSuffix = "Z.icspli";
// A segment's name, with # for each digit.
constexpr std::string_view kNamePattern = "pli-########T######.#########Z.icspli";

[[nodiscard]] bool matches(const char found, const char expected) noexcept {
  return expected == '#' ? found >= '0' && found <= '9' : found == expected;
}

}  // namespace

std::string segment_name(const UtcTime time) {
  const auto seconds = std::chrono::floor<std::chrono::seconds>(time);
  return std::format("{}{:%Y%m%dT%H%M%S}.{:09}{}", kPrefix, seconds, (time - seconds).count(), kSuffix);
}

bool is_segment_name(const std::string_view name) noexcept {
  return std::ranges::equal(name, kNamePattern, matches);
}

SegmentWriter::SegmentWriter(std::filesystem::path folder) : folder_(std::move(folder)) {
  buffer_.reserve(kFlushBytes + kMaxEntryBytes);
}

Result<SegmentWriter> SegmentWriter::open(std::filesystem::path folder, const UtcTime now) {
  SegmentWriter writer(std::move(folder));
  return writer.start(now).map([&writer] { return std::move(writer); });
}

Status SegmentWriter::start(const UtcTime now) {
  static_cast<void>(ics::check(!file_.has_value()));
  opened_ = std::max(now, opened_ + Duration(1));
  path_ = folder_ / segment_name(opened_);
  Result<capture::OutputFile> file = capture::OutputFile::create(path_);
  if (!file) {
    return fail(file.error());
  }
  file_.emplace(std::move(*file));
  return file_->write(kSegmentMagic).and_then([this] { return sync_folder(folder_); });
}

Status SegmentWriter::flush_if_full() {
  // Each entry is at most kMaxEntryBytes, so the buffer never grows.
  static_cast<void>(ics::check(buffer_.size() <= buffer_.capacity()));
  static_cast<void>(ics::check(file_.has_value()));
  if (buffer_.size() < kFlushBytes) {
    return {};
  }
  return file_->write(buffer_).map([this] { buffer_.clear(); });
}

Status SegmentWriter::append(const v1::PliRecord& record) {
  if (!file_) {
    return fail(Error::kInvalidArgument);
  }
  return append_entry(record, buffer_).and_then([this] { return flush_if_full(); });
}

Status SegmentWriter::append(const v1::PliEvent& event) {
  if (!file_) {
    return fail(Error::kInvalidArgument);
  }
  return append_entry(event, buffer_).and_then([this] { return flush_if_full(); });
}

Status SegmentWriter::sync() {
  if (!file_) {
    return fail(Error::kInvalidArgument);
  }
  return file_->write(buffer_).map([this] { buffer_.clear(); }).and_then([this] { return file_->sync(); });
}

bool SegmentWriter::due(const UtcTime now, const Duration max_age) const noexcept {
  return file_.has_value() && now - opened_ >= max_age;
}

Result<std::filesystem::path> SegmentWriter::close() {
  Status synced = sync();
  file_.reset();
  return synced.map([this] { return path_; });
}

Result<std::filesystem::path> SegmentWriter::rotate(const UtcTime now) {
  return close().and_then([this, now](std::filesystem::path closed) {
    return start(now).map([&closed] { return std::move(closed); });
  });
}

}  // namespace ics::store
