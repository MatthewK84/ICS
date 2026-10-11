// ics-cam-offload (ICS-031): arms and offloads every camera of the station
// through ics-camd. See include/ics/camd/command_client.hpp.

#include <cstddef>
#include <cstdio>
#include <span>

#include "ics/camd/command_client.hpp"

int main(const int argc, char* argv[]) {
  return ics::camd::run_offload_client(std::span<const char* const>(argv, static_cast<std::size_t>(argc)), stdout);
}
