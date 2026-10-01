#pragma once

#include <initializer_list>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>

#include "ics/common/units.hpp"
#include "ics/logging/json_line.hpp"

namespace spdlog {
class logger;
}  // namespace spdlog

namespace ics::logging {

// Where a logger takes its timestamps from. Tests pass a fixed clock.
using Clock = UtcTime (*)() noexcept;

// The current UTC time from the system clock.
[[nodiscard]] UtcTime system_now() noexcept;

// A structured logger (ICS-016). Each call writes one JSON line (see
// format_line) through spdlog. A service makes one logger at start-up and
// passes it by reference to what it builds; there is no global logger and no
// spdlog registry. Logging is safe from several threads.
//
// Logging formats text and may allocate, so it is not for real-time paths.
// Real-time code counts or queues what happened, and a non-real-time thread
// logs it.
class Logger {
 public:
  // A logger writing to stderr, which journald captures under systemd.
  [[nodiscard]] static Logger to_stderr(std::string service, Level threshold);

  // A logger writing to out, which must outlive it, with timestamps from clock.
  [[nodiscard]] static Logger to_stream(std::string service, Level threshold, std::ostream& out, Clock clock);

  [[nodiscard]] Level threshold() const noexcept { return threshold_; }
  [[nodiscard]] bool enabled(const Level level) const noexcept { return level >= threshold_; }

  // Writes one line for the event when level is at or above the threshold.
  void log(Level level, std::string_view event, std::initializer_list<Field> fields) const;

  void debug(const std::string_view event, const std::initializer_list<Field> fields = {}) const {
    log(Level::kDebug, event, fields);
  }
  void info(const std::string_view event, const std::initializer_list<Field> fields = {}) const {
    log(Level::kInfo, event, fields);
  }
  void warn(const std::string_view event, const std::initializer_list<Field> fields = {}) const {
    log(Level::kWarn, event, fields);
  }
  void error(const std::string_view event, const std::initializer_list<Field> fields = {}) const {
    log(Level::kError, event, fields);
  }

 private:
  Logger(std::string service, Level threshold, Clock clock, std::shared_ptr<spdlog::logger> sink);

  std::string service_;
  Level threshold_;
  Clock clock_;
  std::shared_ptr<spdlog::logger> sink_;
};

}  // namespace ics::logging
