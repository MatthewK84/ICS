// ics-pli-import (ICS-030): imports one sortie's captures and onboard logs
// into the PLI store. See cpp/README.md.

#include <cstddef>
#include <filesystem>
#include <span>
#include <system_error>

#include "ics/plid/import.hpp"

namespace plid = ics::plid;

int main(const int argc, char* argv[]) {
  std::error_code error;
  // Without a working folder, the import finds nothing to read.
  const std::filesystem::path folder = std::filesystem::current_path(error);
  return plid::run_import(std::span<const char* const>(argv, static_cast<std::size_t>(argc)), plid::kImportConfigPath,
                          folder);
}
