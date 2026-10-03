// ics-timingd (ICS-019): publishes this station's time quality, read from
// ptp4l. See cpp/README.md.

#include <cstddef>
#include <span>

#include "ics/timingd/run.hpp"

namespace timingd = ics::timingd;

int main(const int argc, char* argv[]) {
  return timingd::run(std::span<const char* const>(argv, static_cast<std::size_t>(argc)), timingd::kConfigPath);
}
