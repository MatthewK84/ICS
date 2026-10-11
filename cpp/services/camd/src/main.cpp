// ics-camd (ICS-031): drives this station's cameras, and arms and offloads
// every one of them on one command. See cpp/README.md.

#include <cstddef>
#include <span>

#include "ics/camd/run.hpp"

namespace camd = ics::camd;

int main(const int argc, char* argv[]) {
  return camd::run(std::span<const char* const>(argv, static_cast<std::size_t>(argc)), camd::kConfigPath);
}
