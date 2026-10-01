#include "ics/logging/logger.hpp"

#include <chrono>
#include <initializer_list>
#include <memory>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <spdlog/common.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/sinks/stdout_sinks.h>

#include "ics/common/check.hpp"
#include "ics/common/units.hpp"
#include "ics/logging/json_line.hpp"

namespace ics::logging {
namespace {

spdlog::level::level_enum to_spdlog(const Level level) noexcept {
  switch (level) {
    case Level::kDebug:
      return spdlog::level::debug;
    case Level::kInfo:
      return spdlog::level::info;
    case Level::kWarn:
      return spdlog::level::warn;
    case Level::kError:
      return spdlog::level::err;
  }
  return spdlog::level::err;
}

// An spdlog logger that writes each finished line as it is, and flushes it, so
// a crash loses no line already logged.
std::shared_ptr<spdlog::logger> make_sink(spdlog::sink_ptr sink) {
  auto logger = std::make_shared<spdlog::logger>("ics", std::move(sink));
  logger->set_pattern("%v");
  logger->set_level(spdlog::level::trace);
  logger->flush_on(spdlog::level::trace);
  return logger;
}

}  // namespace

UtcTime system_now() noexcept { return std::chrono::time_point_cast<Duration>(std::chrono::system_clock::now()); }

Logger Logger::to_stderr(std::string service, const Level threshold) {
  return {std::move(service), threshold, &system_now, make_sink(std::make_shared<spdlog::sinks::stderr_sink_mt>())};
}

Logger Logger::to_stream(std::string service, const Level threshold, std::ostream& out, const Clock clock) {
  auto sink = make_sink(std::make_shared<spdlog::sinks::ostream_sink_mt>(out, true));
  if (!check(clock != nullptr)) {
    return {std::move(service), threshold, &system_now, std::move(sink)};
  }
  return {std::move(service), threshold, clock, std::move(sink)};
}

Logger::Logger(std::string service, const Level threshold, const Clock clock, std::shared_ptr<spdlog::logger> sink)
    : service_(std::move(service)), threshold_(threshold), clock_(clock), sink_(std::move(sink)) {}

void Logger::log(const Level level, const std::string_view event, const std::initializer_list<Field> fields) const {
  if (!enabled(level)) {
    return;
  }
  const std::string line = format_line(clock_(), level, service_, event, std::span<const Field>(fields));
  sink_->log(to_spdlog(level), spdlog::string_view_t(line.data(), line.size()));
}

}  // namespace ics::logging
