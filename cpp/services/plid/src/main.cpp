// ics-plid (ICS-030): stores the vehicles' PLI from every feed and answers
// queries for it. See cpp/README.md.

#include <cstddef>
#include <span>

#include "ics/plid/run.hpp"

namespace plid = ics::plid;

int main(const int argc, char* argv[]) {
  return plid::run(std::span<const char* const>(argv, static_cast<std::size_t>(argc)), plid::kConfigPath);
}
