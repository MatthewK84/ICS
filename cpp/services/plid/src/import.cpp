#include "ics/plid/import.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

#include "ics/common/check.hpp"
#include "ics/flightlog/import.hpp"
#include "ics/frames/enu.hpp"
#include "ics/frames/geodetic.hpp"
#include "ics/plid/roles.hpp"
#include "ics/plid/router.hpp"
#include "ics/plid/run.hpp"
#include "ics/retime/retime.hpp"
#include "ics/retime/sorties.hpp"
#include "ics/store/archive.hpp"
#include "ics/store/segment_writer.hpp"
#include "ics/timing/unix_socket.hpp"

namespace ics::plid {
namespace {

// The captures and logs in a folder, each in name order.
struct SortieFiles {
  std::vector<std::filesystem::path> captures;
  std::vector<std::filesystem::path> logs;
};

[[nodiscard]] SortieFiles sortie_files(const std::filesystem::path& folder) {
  SortieFiles out;
  std::error_code error;
  for (const std::filesystem::directory_entry& item : std::filesystem::directory_iterator(folder, error)) {
    const std::filesystem::path extension = item.path().extension();
    if (extension == ".pcap") {
      out.captures.push_back(item.path());
    }
    if (extension == ".ulg" || extension == ".bin") {
      out.logs.push_back(item.path());
    }
  }
  std::ranges::sort(out.captures);
  std::ranges::sort(out.logs);
  return out;
}

// The bytes of the file at path. A file that cannot be opened reads as one
// whose first read fails.
[[nodiscard]] Result<std::string> read_bytes(const std::filesystem::path& path, std::string& reason) {
  constexpr std::size_t kChunkBytes = std::size_t{1} << 16U;
  const timing::Fd file(::open(path.c_str(), O_RDONLY | O_CLOEXEC));
  std::string bytes;
  std::vector<char> chunk(kChunkBytes);
  ssize_t got = 1;
  while (got > 0) {
    got = ::read(file.get(), chunk.data(), chunk.size());
    bytes.append(chunk.data(), static_cast<std::size_t>(std::max<ssize_t>(got, 0)));
  }
  if (got < 0) {
    reason = "cannot read " + path.native();
    return fail(Error::kUnreadable);
  }
  return bytes;
}

// The log's records and events, timed by the captures' sorties.
[[nodiscard]] Status import_log(const std::filesystem::path& path, const flightlog::ImportSettings& settings,
                                const retime::Vehicles& vehicles, const frames::Egm96& geoid,
                                const frames::EnuFrame& range, Pli& out, std::string& reason) {
  const Result<flightlog::LogContents> contents = read_bytes(path, reason).and_then([&](const std::string& bytes) {
    return flightlog::import_log(std::as_bytes(std::span(bytes)), settings, geoid, range).map_error([&](Error e) {
      reason = "cannot import " + path.native();
      return e;
    });
  });
  return contents.map([&](const flightlog::LogContents& log) {
    const retime::TimedLog timed = retime::time_log(log, vehicles);
    for (const flightlog::LogRecord& record : log.states) {
      out.records.push_back(retime::retimed(record.record, record.boot_us, timed.timing));
    }
    for (const flightlog::LogRecord& record : log.gnss) {
      out.records.push_back(retime::retimed(record.record, record.boot_us, timed.timing));
    }
    for (const flightlog::LogEvent& event : log.events) {
      out.events.push_back(retime::retimed(event.event, event.boot_us, timed.timing));
    }
  });
}

// Each capture sortie's positions timed by its fit.
void add_aligned(const retime::Vehicles& vehicles, Pli& out) {
  // Not a structured binding: CodeQL drops the body of a range-for that
  // declares one over a map, and with it every call the body makes.
  for (const auto& entry : vehicles) {
    for (const retime::Sortie& sortie : entry.second.sorties) {
      std::ranges::move(retime::aligned_positions(sortie), std::back_inserter(out.records));
    }
  }
}

// Writes pli to a new segment opened at now, closes and archives it.
[[nodiscard]] Result<std::filesystem::path> store(const std::filesystem::path& folder, const Pli& pli,
                                                  const UtcTime now, std::string& reason) {
  Result<store::SegmentWriter> writer = store::SegmentWriter::open(folder, now);
  Status written = writer.map([](const store::SegmentWriter&) {});
  for (const v1::PliRecord& record : pli.records) {
    written = written.and_then([&] { return writer->append(record); });
  }
  for (const v1::PliEvent& event : pli.events) {
    written = written.and_then([&] { return writer->append(event); });
  }
  return written.and_then([&] { return writer->close(); })
      .and_then([](std::filesystem::path segment) {
        return store::archive_segment(segment).map([&segment](const store::ArchiveCounts&) { return segment; });
      })
      .map_error([&](const Error error) {
        reason = "cannot store the import in " + folder.native();
        return error;
      });
}

[[nodiscard]] std::vector<flightlog::RoleAssignment> log_roles(const std::vector<mavlink::RoleAssignment>& roles) {
  std::vector<flightlog::RoleAssignment> out;
  for (const mavlink::RoleAssignment& role : roles) {
    out.push_back({.system = role.system, .role = role.role});
  }
  return out;
}

// Reads the captures and logs into pli.
[[nodiscard]] Status read_sortie(const SortieFiles& files, const std::vector<mavlink::RoleAssignment>& roles,
                                 const frames::Egm96& geoid, const frames::EnuFrame& range, Pli& pli,
                                 std::string& reason) {
  const Result<retime::Vehicles> vehicles =
      retime::collect(files.captures, {.roles = roles, .link_timeout = std::chrono::seconds(3),
                                       .max_age = std::chrono::seconds(1)},
                      geoid, range, reason);
  Status read = vehicles.map([&pli](const retime::Vehicles& found) { add_aligned(found, pli); });
  const flightlog::ImportSettings settings{.roles = log_roles(roles)};
  for (const std::filesystem::path& log : files.logs) {
    read = read.and_then([&] { return import_log(log, settings, *vehicles, geoid, range, pli, reason); });
  }
  return read;
}

}  // namespace

ImportConfig read_import_config(config::Reader& root) {
  ImportConfig out;
  out.log = config::read_logging(root);
  config::Reader table = root.section("import");
  out.store_folder = table.text("store_folder");
  out.roles = table.texts("roles", 0, kMaxRoles);
  out.range = read_range(root);
  return out;
}

Result<ImportCounts> import_sortie(const ImportConfig& config, const std::filesystem::path& folder,
                                   const frames::Egm96& geoid, const UtcTime now, std::string& reason) {
  const SortieFiles files = sortie_files(folder);
  if (files.captures.empty() && files.logs.empty()) {
    reason = "no .pcap, .ulg or .bin file in " + folder.native();
    return fail(Error::kEmpty);
  }
  Pli pli;
  ImportCounts counts{.captures = files.captures.size(), .logs = files.logs.size(), .segment = {}};
  const Result<std::vector<mavlink::RoleAssignment>> roles = mavlink_roles(config.roles, reason);
  const Status read =
      roles.and_then([&](const std::vector<mavlink::RoleAssignment>&) {
             return frames::Geodetic::make(config.range.latitude, config.range.longitude, config.range.height);
           })
          .and_then([&](const frames::Geodetic& origin) {
            const Status sortie = read_sortie(files, *roles, geoid, frames::EnuFrame(origin), pli, reason);
            counts.aligned_records = static_cast<std::uint64_t>(std::ranges::count_if(pli.records, [](const auto& r) {
              return r.source() == v1::PLI_SOURCE_MAVLINK;
            }));
            return sortie;
          });
  counts.log_records = pli.records.size() - counts.aligned_records;
  counts.log_events = pli.events.size();
  return read.and_then([&] { return store(config.store_folder, pli, now, reason); })
      .map([&counts](std::filesystem::path segment) {
        counts.segment = std::move(segment);
        return counts;
      });
}

int import_with(const ImportConfig& config, const std::filesystem::path& geoid, const std::filesystem::path& folder,
                const logging::Logger& logger) {
  std::string reason = "cannot read the geoid grid " + geoid.native();
  const Result<ImportCounts> counts = frames::Egm96::load(geoid).and_then([&](const frames::Egm96& grid) {
    reason.clear();
    return import_sortie(config, folder, grid, logging::system_now(), reason);
  });
  if (!counts) {
    logger.error("import_failed", {{"error", to_string(counts.error())}, {"reason", reason}});
    return kExitFailed;
  }
  static_cast<void>(ics::check(!counts->segment.empty()));
  logger.info("imported", {{"segment", counts->segment.native()},
                           {"captures", static_cast<std::int64_t>(counts->captures)},
                           {"logs", static_cast<std::int64_t>(counts->logs)},
                           {"aligned_records", static_cast<std::int64_t>(counts->aligned_records)},
                           {"log_records", static_cast<std::int64_t>(counts->log_records)},
                           {"log_events", static_cast<std::int64_t>(counts->log_events)}});
  return kExitStopped;
}

int run_import(const std::span<const char* const> args, const std::filesystem::path& path,
               const std::filesystem::path& folder) {
  if (args.size() != 1) {
    std::fprintf(stderr, "usage: ics-pli-import\nRun it in the folder of one sortie's files. The config file is %s.\n",
                 path.c_str());
    return kExitUsage;
  }
  const auto config = config::read_file(path, &read_import_config);
  if (!config) {
    std::fputs(config::format_errors(path.native(), config.error()).c_str(), stderr);
    return kExitUsage;
  }
  const logging::Logger logger = logging::Logger::to_stderr(config->log.service, config->log.level);
  return import_with(*config, frames::Egm96::kDefaultPath, folder, logger);
}

}  // namespace ics::plid
