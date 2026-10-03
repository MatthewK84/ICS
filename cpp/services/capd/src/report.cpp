#include "ics/capd/report.hpp"

#include <cstdint>

namespace ics::capd {
namespace {

// Log fields are signed; no counter reaches 2^63.
[[nodiscard]] std::int64_t field(const std::uint64_t count) noexcept { return static_cast<std::int64_t>(count); }

}  // namespace

capture::Counters since(const capture::Counters& now, const capture::Counters& before) noexcept {
  return capture::Counters{now.received - before.received, now.dropped - before.dropped,
                           now.interface_dropped - before.interface_dropped};
}

void log_closed(const logging::Logger& logger, const std::string_view interface, const capture::ClosedFile& file,
                const Result<capture::Counters>& counted) {
  logger.info("file_closed", {{"interface", interface},
                              {"path", file.path.native()},
                              {"sha256", file.sha256},
                              {"packets", field(file.packets)},
                              {"bytes", field(file.bytes)}});
  if (!counted) {
    logger.warn("counters_unavailable", {{"interface", interface}, {"error", to_string(counted.error())}});
    return;
  }
  logger.info("counters", {{"interface", interface},
                           {"received", field(counted->received)},
                           {"dropped", field(counted->dropped)},
                           {"interface_dropped", field(counted->interface_dropped)}});
  if (counted->dropped + counted->interface_dropped > 0) {
    logger.error("drops", {{"interface", interface},
                           {"dropped", field(counted->dropped)},
                           {"interface_dropped", field(counted->interface_dropped)}});
  }
}

}  // namespace ics::capd
