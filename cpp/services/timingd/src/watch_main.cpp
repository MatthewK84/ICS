// ics-time-watch (#150): writes ics-timingd's time quality reports as JSON
// lines as they come. See cpp/README.md.

#include <cstddef>
#include <cstdio>
#include <span>

#include "ics/timingd/watch_client.hpp"

int main(const int argc, char* argv[]) {
  return ics::timingd::run_watch_client(std::span<const char* const>(argv, static_cast<std::size_t>(argc)),
                                        stdout);
}
