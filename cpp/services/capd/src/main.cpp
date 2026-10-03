// ics-capd (ICS-020): captures the station's TAP ports to pcap files. See
// cpp/README.md.

#include <cstddef>
#include <span>

#include "ics/capd/run.hpp"

namespace capd = ics::capd;

int main(const int argc, char* argv[]) {
  return capd::run(std::span<const char* const>(argv, static_cast<std::size_t>(argc)), capd::kConfigPath);
}
