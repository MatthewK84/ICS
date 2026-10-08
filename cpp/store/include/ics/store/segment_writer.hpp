#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ics/capture/output_file.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/v1/pli.pb.h"

namespace ics::store {

// The file name of the segment a writer opens at time, such as
// pli-20261003T171500.123456789Z.icspli.
[[nodiscard]] std::string segment_name(UtcTime time);

// Whether name is a segment's file name, as segment_name writes them.
[[nodiscard]] bool is_segment_name(std::string_view name) noexcept;

// Appends PLI to a series of segments in one folder (ICS-030), each named for
// the UTC time it opened. A segment that opens in the same nanosecond as the
// last is named 1 ns later. Entries are buffered and written when the buffer
// passes kFlushBytes or flush() or sync() is called; sync() also waits until
// they are on the disk, so a crash loses only what was appended since the
// last one.
// Allocates when a segment opens, not per entry.
class SegmentWriter {
 public:
  // Buffered bytes that make append() write them.
  static constexpr std::size_t kFlushBytes = std::size_t{1} << 20U;

  // Opens the first segment at now. kUnwritable when it cannot be created,
  // for example when a file already has its name.
  [[nodiscard]] static Result<SegmentWriter> open(std::filesystem::path folder, UtcTime now);

  // Adds a record or event. kInvalidArgument for one larger than an entry
  // holds; kUnwritable when buffered entries had to be written and could not
  // be. Both fail with kInvalidArgument after close().
  [[nodiscard]] Status append(const v1::PliRecord& record);
  [[nodiscard]] Status append(const v1::PliEvent& event);

  // Writes the buffered entries to the segment, where readers see them,
  // without waiting for the disk.
  [[nodiscard]] Status flush();

  // Writes the buffered entries and waits until the segment is on the disk.
  [[nodiscard]] Status sync();

  // Whether the current segment has been open for max_age at now.
  [[nodiscard]] bool due(UtcTime now, Duration max_age) const noexcept;

  // Syncs and closes the current segment, then opens the next at now.
  // Returns the closed segment's path. A segment that fails to sync is
  // closed all the same, and no next one opens.
  [[nodiscard]] Result<std::filesystem::path> rotate(UtcTime now);

  // Syncs and closes the current segment and opens none.
  [[nodiscard]] Result<std::filesystem::path> close();

  // The current segment's path, or the last one's after close().
  [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

 private:
  explicit SegmentWriter(std::filesystem::path folder);
  [[nodiscard]] Status start(UtcTime now);
  [[nodiscard]] Status flush_if_full();

  std::filesystem::path folder_;
  std::vector<std::byte> buffer_;
  std::optional<capture::OutputFile> file_;
  std::filesystem::path path_;
  // When the current segment opened; before any, the earliest time there is.
  UtcTime opened_ = UtcTime::min();
};

}  // namespace ics::store
