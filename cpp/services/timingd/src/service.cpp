#include "ics/timingd/service.hpp"

#include <chrono>
#include <cstdint>
#include <utility>

#include "ics/timingd/report.hpp"

namespace ics::timingd {

Service::Service(timing::PtpClient client, timing::Publisher publisher, const Config& config)
    : client_(std::move(client)),
      publisher_(std::move(publisher)),
      tracker_(config.model),
      poll_timeout_(config.poll_interval),
      buffer_(max_report_size(config.station_id)) {
  report_.set_station_id(config.station_id);
}

Result<Service> Service::open(const Config& config) {
  Result<timing::PtpClient> client =
      timing::PtpClient::open(config.ptp4l_socket, config.client_socket, config.ptp_domain);
  if (!client) {
    return fail(client.error());
  }
  Result<timing::Publisher> publisher = timing::Publisher::open(config.publish_socket);
  if (!publisher) {
    return fail(publisher.error());
  }
  return Service(std::move(*client), std::move(*publisher), config);
}

void Service::step(const logging::Logger& logger) {
  const Result<timing::Snapshot> snapshot = client_.poll(poll_timeout_);
  const timing::SteadyTime now = std::chrono::steady_clock::now();
  log_answering(logger, snapshot);
  static_cast<void>(snapshot ? tracker_.update(*snapshot, now) : tracker_.lost(now));
  const timing::Quality quality = tracker_.quality(now);
  log_state(logger, quality);
  fill(quality, logging::system_now(), report_);
  publisher_.publish(serialize(report_, buffer_));
}

void Service::log_answering(const logging::Logger& logger, const Result<timing::Snapshot>& polled) {
  if (answering_ == polled.has_value()) {
    return;
  }
  answering_ = polled.has_value();
  if (polled) {
    logger.info("ptp4l_answering");
    return;
  }
  logger.warn("ptp4l_unavailable", {{"error", to_string(polled.error())}});
}

void Service::log_state(const logging::Logger& logger, const timing::Quality& quality) {
  if (logged_state_ == quality.state) {
    return;
  }
  logged_state_ = quality.state;
  logger.info("clock_state", {{"state", timing::to_string(quality.state)},
                              {"ptp_offset_ns", quality.ptp_offset.count()},
                              {"error_bound_ns", quality.error_bound.count()}});
}

}  // namespace ics::timingd
