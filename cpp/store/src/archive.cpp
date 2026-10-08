#include "ics/store/archive.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "files.hpp"
#include "ics/store/parquet_writer.hpp"
#include "ics/store/segment_format.hpp"
#include "ics/store/segment_reader.hpp"
#include "ics/v1/pli.pb.h"
#include "status.hpp"

namespace ics::store {

using detail::status_of;

namespace {

[[nodiscard]] std::filesystem::path partial_path(const std::filesystem::path& path) {
  return path.string() + ".partial";
}

// The rows of one Parquet file, written a row group at a time.
template <typename Message>
class Rows {
 public:
  // Starts the file at path, replacing a .partial left there.
  [[nodiscard]] static Result<Rows> start(std::filesystem::path path, const RowGroupLimits limits) {
    std::error_code ignored;
    std::filesystem::remove(partial_path(path), ignored);
    return ParquetWriter::create(partial_path(path), *Message::descriptor()).map([&path, limits](ParquetWriter writer) {
      return Rows(std::move(path), std::move(writer), limits);
    });
  }

  // Adds the message serialized in payload; writes a full row group.
  [[nodiscard]] Status add(const std::span<const std::byte> payload) {
    Message message;
    if (!message.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
      return fail(Error::kMalformed);
    }
    bytes_ += payload.size();
    rows_.push_back(std::move(message));
    return rows_.size() < limits_.rows && bytes_ < limits_.bytes ? Status{} : flush();
  }

  // Writes the last row group and the footer, then renames the file.
  [[nodiscard]] Status finish() {
    return flush().and_then([this] { return writer_.close(); }).and_then([this] {
      std::error_code error;
      std::filesystem::rename(partial_path(path_), path_, error);
      return status_of(!error, Error::kUnwritable);
    });
  }

  [[nodiscard]] std::uint64_t count() const noexcept { return writer_.rows(); }

 private:
  Rows(std::filesystem::path path, ParquetWriter writer, const RowGroupLimits limits)
      : path_(std::move(path)), writer_(std::move(writer)), limits_(limits) {}

  [[nodiscard]] Status flush() {
    return writer_.write_row_group(rows_).map([this] {
      rows_.clear();
      bytes_ = 0;
    });
  }

  std::filesystem::path path_;
  ParquetWriter writer_;
  RowGroupLimits limits_;
  std::vector<Message> rows_;
  std::size_t bytes_ = 0;
};

// Adds each sound entry of the segment to its file's rows.
[[nodiscard]] Status add_all(SegmentReader& reader, Rows<v1::PliRecord>& records, Rows<v1::PliEvent>& events) {
  Status added;
  Result<std::optional<Entry>> entry = reader.next();
  while (added && entry && entry->has_value()) {
    const Entry& next = **entry;
    added = next.kind == EntryKind::kRecord ? records.add(next.payload) : events.add(next.payload);
    entry = reader.next();
  }
  return added.and_then([&entry] { return entry.map([](const std::optional<Entry>&) {}); });
}

}  // namespace

ArchivePaths archive_paths(const std::filesystem::path& segment) {
  const std::string stem = segment.stem().string();
  return {.records = segment.parent_path() / (stem + ".records.parquet"),
          .events = segment.parent_path() / (stem + ".events.parquet")};
}

bool is_archived(const std::filesystem::path& segment) noexcept {
  const ArchivePaths paths = archive_paths(segment);
  std::error_code error;
  return std::filesystem::exists(paths.records, error) && std::filesystem::exists(paths.events, error);
}

Result<ArchiveCounts> archive_segment(const std::filesystem::path& segment, const RowGroupLimits limits) {
  Result<SegmentReader> reader = SegmentReader::open(segment);
  if (!reader) {
    return fail(reader.error());
  }
  const ArchivePaths paths = archive_paths(segment);
  Result<Rows<v1::PliRecord>> records = Rows<v1::PliRecord>::start(paths.records, limits);
  if (!records) {
    return fail(records.error());
  }
  Result<Rows<v1::PliEvent>> events = Rows<v1::PliEvent>::start(paths.events, limits);
  if (!events) {
    return fail(events.error());
  }
  return add_all(*reader, *records, *events)
      .and_then([&records] { return records->finish(); })
      .and_then([&events] { return events->finish(); })
      .and_then([&segment] {
        std::error_code ignored;
        return detail::sync_folder(std::filesystem::absolute(segment, ignored).parent_path());
      })
      .map([&records, &events] { return ArchiveCounts{.records = records->count(), .events = events->count()}; });
}

}  // namespace ics::store
