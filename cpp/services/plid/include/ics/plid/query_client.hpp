#pragma once

#include <cstdio>
#include <filesystem>
#include <span>
#include <string_view>

namespace ics::plid {

// Where ics-plid's example config, and the SITL rig, put the query socket.
inline constexpr std::string_view kQuerySocket = "/run/ics-plid/query";

// ics-pli-query (ICS-030): asks ics-plid's PliQueryService, over gRPC on its
// Unix-domain socket (#146), for records or events and writes each on out,
// one JSON object per line in protobuf's JSON form, with proto field names
// and zeros written out, as ics-mavlink-replay does:
//
//   {"kind":"record","record":{...}}
//   {"kind":"event","event":{...}}
//   {"kind":"done","count":1530,"truncated":false}
//
// Usage: ics-pli-query records|events [START_UTC_NS END_UTC_NS [ENTITY]]
//
// Without a range it asks for all time. Exits 0 once the last batch came,
// 1 when the server did not answer or the query failed (either goes to
// stderr) or was truncated at the server's limit, and 2 for bad arguments.
[[nodiscard]] int run_query_client(std::span<const char* const> args, const std::filesystem::path& socket,
                                   std::FILE* out);

}  // namespace ics::plid
