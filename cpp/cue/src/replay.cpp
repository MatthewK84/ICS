// replay.cpp: ics-cue-replay's work: segments in, the station's cues out as JSON lines (ICS-037).
// JSF deviation D-6: ics::config, ics::store and ics::common's Result and Duration come from .hpp headers.
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <iomanip>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/config/config.hpp>
#include <ics/config/reader.hpp>
#include <ics/cue/config.h>
#include <ics/cue/extrapolator.h>
#include <ics/cue/pointing.h>
#include <ics/cue/replay.h>
#include <ics/store/segment_format.hpp>
#include <ics/store/segment_reader.hpp>
#include <ics/store/segment_writer.hpp>
#include <ics/v1/pli.pb.h>

namespace
{

using ics::int32;
using ics::int64;

// How often a cue is written, and the digits that let each number round-trip.
constexpr int64 tick_ns = 20000000;
constexpr int32 digits = 17;
constexpr std::string_view usage = "usage: ics-cue-replay\n"
                                   "Replays the .icspli segment files in the working folder through the cue\n"
                                   "extrapolator set up by /etc/ics/ics-cue.toml's [cue] table, and writes the\n"
                                   "station's cue every 20 ms as a JSON line.\n";

// A record, and when it was received: its receipt time, or its valid time without one.
struct Received
{
  // When it was received, in UTC nanoseconds.
  int64 receipt_ns;
  // The record.
  ics::v1::PliRecord record;
};

// The segment files in a folder, by name, which orders them in time.
std::vector<std::filesystem::path> segments_in(const std::filesystem::path& folder)
{
  std::vector<std::filesystem::path> found;
  std::error_code error;
  std::filesystem::directory_iterator item(folder, error);
  const std::filesystem::directory_iterator end;
  while ((!error) && (item != end))
  {
    if (ics::store::is_segment_name(item->path().filename().string()))
    {
      found.push_back(item->path());
    }
    item.increment(error);
  }
  std::ranges::sort(found);
  return found;
}

// Keeps an entry's record, if it holds one that parses.
void collect(const ics::store::Entry& entry, std::vector<Received>& records)
{
  if (entry.kind == ics::store::EntryKind::kRecord)
  {
    ics::v1::PliRecord record;
    const int32 size = static_cast<int32>(entry.payload.size());
    if (record.ParseFromArray(entry.payload.data(), size))
    {
      const int64 receipt = record.has_received_utc_ns() ? record.received_utc_ns() : record.valid_utc_ns();
      records.push_back(Received{receipt, record});
    }
  }
}

// The records of the segments, in the order they were received.
ics::Result<std::vector<Received>> records_in(const std::vector<std::filesystem::path>& segments)
{
  std::vector<Received> records;
  ics::Status status{};
  for (const std::filesystem::path& segment : segments)
  {
    const ics::Result<ics::uint64> sound = ics::store::read_segment(segment,
                                                                    [&records](const ics::store::Entry& entry)
                                                                    {
                                                                      collect(entry, records);
                                                                    });
    if (!sound.has_value())
    {
      status = ics::fail(sound.error());
    }
  }
  std::ranges::stable_sort(records, {}, &Received::receipt_ns);
  ics::Result<std::vector<Received>> read = ics::fail(ics::Error::kEmpty);
  if (!status.has_value())
  {
    read = ics::fail(status.error());
  }
  else if (!records.empty())
  {
    read = records;
  }
  else
  {
    read = ics::fail(ics::Error::kEmpty);
  }
  return read;
}

// One cue as a JSON line.
void write_cue(std::ostream& out,
               const int64 now_ns,
               const ics::Result<ics::cue::Cue>& cue,
               const ics::cue::Counts& counts)
{
  out << "{\"time_utc_ns\":" << now_ns;
  if (cue.has_value())
  {
    out << ",\"ok\":true,\"azimuth_rad\":" << cue->azimuth_rad << ",\"elevation_rad\":" << cue->elevation_rad
        << ",\"azimuth_rate_rad_s\":" << cue->azimuth_rate_rad_s
        << ",\"elevation_rate_rad_s\":" << cue->elevation_rate_rad_s << ",\"range_m\":" << cue->range_m
        << ",\"sigma_rad\":" << cue->sigma_rad;
  }
  else
  {
    out << ",\"ok\":false,\"error\":\"" << ics::to_string(cue.error()) << "\"";
  }
  out << ",\"taken\":" << counts.taken << ",\"late\":" << counts.late << ",\"gated\":" << counts.gated
      << ",\"too_old\":" << counts.too_old << ",\"not_target\":" << counts.not_target
      << ",\"unusable\":" << counts.unusable << "}\n";
}

// Replays the records through the extrapolator, writing a cue every tick.
void replay(const std::vector<Received>& records,
            ics::cue::Cue_extrapolator& cue,
            std::ostream& out)
{
  const int64 count = static_cast<int64>(records.size());
  const int64 last_ns = records.back().receipt_ns;
  int64 next = 0;
  out << std::setprecision(digits);
  for (int64 now_ns = records.front().receipt_ns; now_ns <= last_ns; now_ns += tick_ns)
  {
    while ((next < count) && (records[static_cast<std::size_t>(next)].receipt_ns <= now_ns))
    {
      const ics::cue::Take taken = cue.take(records[static_cast<std::size_t>(next)].record);
      static_cast<void>(taken);
      ++next;
    }
    write_cue(out, now_ns, cue.cue_at(ics::UtcTime{} + ics::Duration(now_ns)), cue.counts());
  }
}

// The replay, once the arguments are known to be none.
int32 run(const std::filesystem::path& config_file,
          const std::filesystem::path& folder,
          std::ostream& out,
          std::ostream& errors)
{
  int32 code = ics::cue::exit_failed;
  const auto settings = ics::config::read_file(config_file, &ics::cue::read_replay_config);
  if (settings.has_value())
  {
    code = ics::cue::replay_folder(*settings, folder, out, errors);
  }
  else
  {
    errors << ics::config::format_errors(config_file.string(), settings.error());
  }
  return code;
}

}  // namespace

namespace ics::cue
{

int32 replay_folder(const Cue_settings& settings,
                    const std::filesystem::path& folder,
                    std::ostream& out,
                    std::ostream& errors)
{
  int32 code = exit_failed;
  Result<Cue_extrapolator> cue = Cue_extrapolator::make(settings);
  const Result<std::vector<Received>> records = records_in(segments_in(folder));
  if (!cue.has_value())
  {
    errors << "the range origin or the station is no point: " << to_string(cue.error()) << "\n";
  }
  else if (!records.has_value())
  {
    errors << folder.string() << ": no PLI records to replay: " << to_string(records.error()) << "\n";
  }
  else
  {
    replay(*records, *cue, out);
    code = exit_replayed;
  }
  return code;
}

int32 run_replay(const std::span<const char* const> arguments,
                 const std::filesystem::path& config_file,
                 const std::filesystem::path& folder,
                 std::ostream& out,
                 std::ostream& errors)
{
  const int64 count = static_cast<int64>(arguments.size());
  int32 code = exit_usage;
  if (count == 1)
  {
    code = run(config_file, folder, out, errors);
  }
  else if ((count == 2) && (std::string_view(arguments[1]) == "--help"))
  {
    out << usage;
    code = exit_replayed;
  }
  else
  {
    errors << usage;
  }
  return code;
}

}  // namespace ics::cue
