#include "ics/timingd/service.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include "ics/common/check.hpp"
#include "ics/timingd/report.hpp"

namespace ics::timingd {

Service::Service(timing::PtpClient client, std::unique_ptr<ReportServer> server, const Config& config)
    : client_(std::move(client)),
      server_(std::move(server)),
      tracker_(config.model),
      offsets_(config.camera_offsets_file),
      poll_timeout_(config.poll_interval) {
  report_.set_station_id(config.station_id);
  buffer_.resize(max_report_size(report_));
  server_->reserve(buffer_.size());
}

Result<Service> Service::open(const Config& config) {
  Result<timing::PtpClient> client =
      timing::PtpClient::open(config.ptp4l_socket, config.client_socket, config.ptp_domain);
  if (!client) {
    return fail(client.error());
  }
  Result<std::unique_ptr<ReportServer>> server = ReportServer::open(config.publish_socket);
  if (!server) {
    return fail(server.error());
  }
  return Service(std::move(*client), std::move(*server), config);
}

void Service::step(const logging::Logger& logger) {
  const Result<timing::Snapshot> snapshot = client_.poll(poll_timeout_);
  const timing::SteadyTime now = std::chrono::steady_clock::now();
  log_answering(logger, snapshot);
  static_cast<void>(snapshot ? tracker_.update(*snapshot, now) : tracker_.lost(now));
  const timing::Quality quality = tracker_.quality(now);
  log_state(logger, quality);
  update_offsets(logger);
  fill(quality, logging::system_now(), report_);
  server_->post(serialize(report_, buffer_));
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

void Service::update_offsets(const logging::Logger& logger) {
  const std::optional<Result<timing::CameraOffsets>> changed = offsets_.poll();
  if (!changed) {
    return;
  }
  report_.clear_camera_offsets();
  const Result<timing::CameraOffsets>& read = *changed;
  const bool ours = read && read->station_id == report_.station_id();
  if (ours) {
    for (const v1::TimeQuality::CameraOffset& offset : read->offsets) {
      *report_.add_camera_offsets() = offset;
    }
    logger.info("camera_offsets", {{"count", static_cast<std::int64_t>(read->offsets.size())}});
  } else if (!read && read.error() == Error::kEmpty) {
    logger.info("camera_offsets_none");
  } else {
    logger.warn("camera_offsets_unusable", {{"error", read ? "another station's" : to_string(read.error())}});
  }
  buffer_.resize(max_report_size(report_));
  server_->reserve(buffer_.size());
  // The buffer holds the report at its longest, so serialize never fails.
  static_cast<void>(check(buffer_.size() >= report_.ByteSizeLong()));
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
