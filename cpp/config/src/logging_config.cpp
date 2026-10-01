#include "ics/config/logging_config.hpp"

#include <array>

#include "ics/config/reader.hpp"
#include "ics/logging/json_line.hpp"

namespace ics::config {
namespace {

constexpr std::array<Choice<logging::Level>, 4> kLevels{{
    {"debug", logging::Level::kDebug},
    {"info", logging::Level::kInfo},
    {"warn", logging::Level::kWarn},
    {"error", logging::Level::kError},
}};

}  // namespace

LoggingConfig read_logging(Reader& root) {
  Reader log = root.section("log");
  return LoggingConfig{.level = log.choice("level", kLevels), .service = log.text("service")};
}

}  // namespace ics::config
