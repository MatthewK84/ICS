#pragma once

#include <filesystem>
#include <optional>
#include <set>
#include <string>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/plid/router.hpp"
#include "ics/store/segment_writer.hpp"

namespace ics::plid {

// When the segment log is synced and rotated.
struct IngestTiming {
  Duration rotate_interval{};
  Duration sync_interval{};
};

// Appends the feeds' PLI to the segment log (ICS-030). Each tick writes what
// was stored to the segment, where queries see it, and syncs the segment once
// sync_interval has passed since the last sync, so a crash loses at most that
// much. A tick closes the segment, opening the next, once rotate_interval has
// passed since it opened, or at the end of a sortie: when the last vehicle
// armed disarms.
class Ingest {
 public:
  // Opens the first segment in folder at now. Fails as SegmentWriter::open.
  [[nodiscard]] static Result<Ingest> open(const std::filesystem::path& folder, IngestTiming timing, UtcTime now);

  // Appends pli and empties it. Fails as SegmentWriter::append; one too large
  // for an entry is dropped and counted instead.
  [[nodiscard]] Status store(Pli& pli);

  // Writes, syncs or rotates as due at now. Returns the segment closed, if
  // any.
  [[nodiscard]] Result<std::optional<std::filesystem::path>> tick(UtcTime now);

  // Closes the current segment and returns it.
  [[nodiscard]] Result<std::filesystem::path> close();

  [[nodiscard]] const std::filesystem::path& path() const noexcept { return writer_.path(); }
  [[nodiscard]] std::uint64_t stored() const noexcept { return stored_; }
  [[nodiscard]] std::uint64_t dropped() const noexcept { return dropped_; }

 private:
  Ingest(store::SegmentWriter writer, IngestTiming timing, UtcTime now);
  [[nodiscard]] Status counted(Status appended);
  void track_arming(const v1::PliEvent& event);

  store::SegmentWriter writer_;
  IngestTiming timing_;
  UtcTime synced_;
  // The vehicles armed now; a sortie ends when the last disarms.
  std::set<std::string> armed_;
  bool sortie_ended_ = false;
  std::uint64_t stored_ = 0;
  std::uint64_t dropped_ = 0;
};

}  // namespace ics::plid
