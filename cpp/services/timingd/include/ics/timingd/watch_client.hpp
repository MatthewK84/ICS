#pragma once

#include <cstdio>
#include <span>
#include <string_view>

namespace ics::timingd {

// Where ics-timingd's example config puts the socket it serves its reports on.
inline constexpr std::string_view kReportSocket = "/run/ics-timingd/time-quality";

// ics-time-watch (#150): watches ics-timingd's TimeQualityService, over gRPC
// on its Unix-domain socket, and writes each report on out as it comes, one
// JSON object per line in protobuf's JSON form, with proto field names and
// zeros written out, as ics-pli-query does:
//
//   {"station_id":"station-1","time_utc_ns":"1790000000000000000","clock_state":"CLOCK_STATE_LOCKED",...}
//
// Usage: ics-time-watch [SOCKET [COUNT]]
//
// SOCKET defaults to kReportSocket. With COUNT, a positive number, it stops
// after that many reports. Exits 0 once it has COUNT reports or ics-timingd
// ended the stream, 1 when ics-timingd could not be reached or refused it
// (the reason goes to stderr), and 2 for bad arguments.
[[nodiscard]] int run_watch_client(std::span<const char* const> args, std::FILE* out);

}  // namespace ics::timingd
