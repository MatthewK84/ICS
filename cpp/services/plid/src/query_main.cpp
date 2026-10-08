// ics-pli-query (ICS-030): asks ics-plid for records or events and writes
// them as JSON lines. See cpp/README.md.

#include <cstddef>
#include <cstdio>
#include <span>

#include "ics/plid/query_client.hpp"

namespace plid = ics::plid;

int main(const int argc, char* argv[]) {
  return plid::run_query_client(std::span<const char* const>(argv, static_cast<std::size_t>(argc)),
                                plid::kQuerySocket, stdout);
}
