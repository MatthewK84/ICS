#pragma once

#include <string>

#include "ics/config/reader.hpp"
#include "ics/logging/json_line.hpp"

namespace ics::config {

// The [log] table every service's config has (ICS-016):
//
//   [log]
//   level = "info"          # debug, info, warn or error
//   service = "ics-timingd" # the service field of each log line
struct LoggingConfig {
  logging::Level level;
  std::string service;
};

// Reads the [log] table under root.
[[nodiscard]] LoggingConfig read_logging(Reader& root);

}  // namespace ics::config
