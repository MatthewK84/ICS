#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/logging/logger.hpp"
#include "ics/timing/clock_state.hpp"
#include "ics/timing/ptp_client.hpp"
#include "ics/timing/publisher.hpp"
#include "ics/timing/quality.hpp"
#include "ics/timingd/config.hpp"
#include "ics/v1/time_quality.pb.h"

namespace ics::timingd {

// ics-timingd's work (ICS-019): each step polls ptp4l once, tracks the clock
// state and publishes an ics.v1.TimeQuality to every subscriber. All memory
// is allocated when it opens, except for log lines.
class Service {
 public:
  // Opens the client end of ptp4l's socket and the publish socket. Fails as
  // ics::timing::PtpClient::open and ics::timing::Publisher::open do.
  [[nodiscard]] static Result<Service> open(const Config& config);

  // Polls ptp4l, waiting up to the poll interval for its answers, and
  // publishes the report. Logs "clock_state" when the state changes, and
  // "ptp4l_answering" or "ptp4l_unavailable" when ptp4l starts or stops
  // answering; a ptp4l that does not answer means free-running.
  void step(const logging::Logger& logger);

  [[nodiscard]] timing::ClockState state() const noexcept { return tracker_.state(); }
  [[nodiscard]] std::size_t subscribers() const noexcept { return publisher_.subscribers(); }

 private:
  Service(timing::PtpClient client, timing::Publisher publisher, const Config& config);
  void log_answering(const logging::Logger& logger, const Result<timing::Snapshot>& polled);
  void log_state(const logging::Logger& logger, const timing::Quality& quality);

  timing::PtpClient client_;
  timing::Publisher publisher_;
  timing::QualityTracker tracker_;
  Duration poll_timeout_;
  v1::TimeQuality report_;
  std::vector<std::byte> buffer_;
  // What the log last said, if anything.
  std::optional<bool> answering_;
  std::optional<timing::ClockState> logged_state_;
};

}  // namespace ics::timingd
