#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>

#include "ics/common/error.hpp"

namespace ics::store {

// The Parquet files a segment is archived to, beside it: for
// pli-20261003T171500.123456789Z.icspli, pli-20261003T171500.123456789Z
// with .records.parquet and .events.parquet.
struct ArchivePaths {
  std::filesystem::path records;
  std::filesystem::path events;
};

[[nodiscard]] ArchivePaths archive_paths(const std::filesystem::path& segment);

// Whether both of the segment's Parquet files are there.
[[nodiscard]] bool is_archived(const std::filesystem::path& segment) noexcept;

// When a row group is full: at most this many rows, or this many bytes of
// serialized messages, which bounds the memory archiving takes and keeps
// each data page far below Parquet's 2 GiB.
struct RowGroupLimits {
  std::size_t rows = std::size_t{1} << 14U;
  std::size_t bytes = std::size_t{1} << 26U;
};

// How many rows each Parquet file holds.
struct ArchiveCounts {
  std::uint64_t records = 0;
  std::uint64_t events = 0;
};

// Writes a closed segment's sound entries to its two Parquet files (ICS-030):
// one row per ics.v1.PliRecord or PliEvent, in the segment's order. Each file
// is written as <path>.partial, synced and renamed, so a file at its final
// path is always whole; a .partial left by a crash is replaced, and so is an
// earlier archive. kUnreadable when the segment cannot be read; kMalformed
// when it is not a segment, or an entry does not hold the message its kind
// says; kUnwritable when a file cannot be written, synced or renamed.
[[nodiscard]] Result<ArchiveCounts> archive_segment(const std::filesystem::path& segment,
                                                    RowGroupLimits limits = {});

}  // namespace ics::store
