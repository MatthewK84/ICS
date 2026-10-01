// Fuzzes the config loader (ICS-016). For any bytes, parsing either succeeds
// or reports at least one error, and reading the document as a service's
// [log] table either succeeds with a usable config or reports at least one
// error. Every error has a message.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string>

#include <tl/expected.hpp>

#include "ics/config/config.hpp"
#include "ics/config/logging_config.hpp"
#include "ics/config/reader.hpp"

namespace {

void require_reported(const ics::config::Errors& errors) {
  const bool unexplained = std::ranges::any_of(
      errors, [](const ics::config::ConfigError& error) { return error.message.empty(); });
  if (errors.empty() || unexplained || ics::config::format_errors("fuzz", errors).empty()) {
    std::abort();
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const std::span<const std::uint8_t> input{data, size};
  const std::string text(input.begin(), input.end());
  const tl::expected<ics::config::Table, ics::config::Errors> table = ics::config::parse(text);
  if (!table) {
    require_reported(table.error());
    return 0;
  }
  const tl::expected<ics::config::LoggingConfig, ics::config::Errors> config =
      ics::config::read(*table, ics::config::read_logging);
  if (!config) {
    require_reported(config.error());
    return 0;
  }
  if (config->service.empty()) {
    std::abort();
  }
  return 0;
}
