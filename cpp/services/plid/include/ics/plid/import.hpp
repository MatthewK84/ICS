#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/config/logging_config.hpp"
#include "ics/config/reader.hpp"
#include "ics/frames/egm96.hpp"
#include "ics/logging/logger.hpp"
#include "ics/plid/config.hpp"

namespace ics::plid {

// The settings of ics-pli-import (ICS-030), from its TOML file:
//
//   [log]
//   level = "info"
//   service = "ics-pli-import"
//
//   [import]
//   store_folder = "/var/lib/ics/pli"         # ics-plid's store_folder
//   roles = ["1=target", "2=interceptor"]     # SYSTEM=ROLE, as for [mavlink]
//
//   [range]                                   # as ics-plid's
//   latitude_deg = 32.9
//   longitude_deg = -106.4
//   height_m = 1200.0
struct ImportConfig {
  config::LoggingConfig log;
  std::filesystem::path store_folder;
  std::vector<std::string> roles;
  RangeOrigin range;
};

[[nodiscard]] ImportConfig read_import_config(config::Reader& root);

// What one import stored.
struct ImportCounts {
  std::size_t captures = 0;
  std::size_t logs = 0;
  // Capture positions timed by their sortie's clock fit.
  std::uint64_t aligned_records = 0;
  std::uint64_t log_records = 0;
  std::uint64_t log_events = 0;
  std::filesystem::path segment;
};

// Imports one sortie from folder, which holds its TAP captures (*.pcap, from
// ics-capd) and its vehicles' onboard logs (*.ulg from PX4, *.bin from
// ArduPilot), into the store as one new segment opened at now, then
// archives it (ICS-030):
// - the captures' MAVLink positions timed by each sortie's clock fit, with
//   PLI_TIME_BASIS_VEHICLE_ALIGNED, for each sortie whose clock pairs lie on
//   a straight line (ICS-026). ics-plid stored them live already, timed by
//   the vehicle's GNSS; these stand beside them.
// - each log's records and events (ICS-025), timed by the clock pairs of the
//   sortie whose boot times they overlap (retime::time_log).
// Fails, saying why in reason: kEmpty when the folder holds none of those
// files; kInvalidArgument for a role that does not parse; as
// retime::collect fails for a capture; kUnreadable or as import_log fails
// for a log; kUnwritable when the segment cannot be written; and as
// archive_segment fails.
[[nodiscard]] Result<ImportCounts> import_sortie(const ImportConfig& config, const std::filesystem::path& folder,
                                                 const frames::Egm96& geoid, UtcTime now, std::string& reason);

// The config file ics-pli-import reads.
inline constexpr std::string_view kImportConfigPath = "/etc/ics/ics-pli-import.toml";

// Imports the sortie in folder as config says, with the EGM96 grid at geoid,
// logging "imported" with the counts or "import_failed" with why. Returns
// kExitStopped (0) or kExitFailed (1).
[[nodiscard]] int import_with(const ImportConfig& config, const std::filesystem::path& geoid,
                              const std::filesystem::path& folder, const logging::Logger& logger);

// ics-pli-import, which takes no arguments: reads the config file at path
// (kImportConfigPath, from main) and imports the sortie in folder (the
// working folder, from main) with the grid where the ICS images install it,
// logging to stderr. kExitUsage for a bad command line or config file.
[[nodiscard]] int run_import(std::span<const char* const> args, const std::filesystem::path& path,
                             const std::filesystem::path& folder);

}  // namespace ics::plid
