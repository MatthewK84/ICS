#pragma once

#include <chrono>
#include <cstdio>
#include <span>
#include <string_view>

namespace ics::camd {

// Where ics-camd's example config puts the socket it serves CameraService on.
inline constexpr std::string_view kCommandSocket = "/run/ics-camd/command";

// How long ics-cam-offload waits for every camera's offload.
inline constexpr std::chrono::minutes kCommandDeadline{30};

// ics-cam-offload (ICS-031): the one command that arms and offloads every
// camera of the station. It sends ArmAndOffload to ics-camd, over gRPC on its
// Unix-domain socket, and writes how each camera's offload went on out, one
// JSON object per line in protobuf's JSON form, with proto field names and
// zeros written out, as ics-time-watch does:
//
//   {"camera_id":"north-hs","verified":true,"segments":2,"frames":"600","files":[...],"error":""}
//
// Usage: ics-cam-offload RUN_ID [SOCKET]
//
// SOCKET defaults to kCommandSocket. Exits 0 when every camera's offload
// verified; 1 when ics-camd could not be reached or refused the command, or
// a camera's offload failed or did not verify (the reason goes to stderr);
// and 2 for bad arguments.
[[nodiscard]] int run_offload_client(std::span<const char* const> args, std::FILE* out);

}  // namespace ics::camd
