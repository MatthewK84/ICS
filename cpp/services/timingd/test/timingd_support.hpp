#pragma once

// Helpers for the ics-timingd tests (ICS-019).

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

#include <grpcpp/grpcpp.h>

#include "ics/common/units.hpp"
#include "ics/logging/json_line.hpp"
#include "ics/timingd/config.hpp"
#include "ics/v1/time_quality.pb.h"
#include "ics/v1/time_quality_service.grpc.pb.h"
#include "support.hpp"

namespace ics::timingd::testing {

// The time every test logger stamps its lines with.
inline UtcTime fixed_clock() noexcept { return utc_from_ns(1'790'000'000'000'000'000); }

// A config with its sockets in dir and a 50 ms poll interval.
inline Config test_config(const timing::testing::TempDir& dir) {
  Config config;
  config.log = {logging::Level::kDebug, "ics-timingd"};
  config.station_id = "station-1";
  config.ptp4l_socket = dir / "ptp4l-ro";
  config.client_socket = dir / "client";
  config.publish_socket = dir / "time-quality";
  config.camera_offsets_file = dir / "offsets.binpb";
  config.poll_interval = std::chrono::milliseconds(50);
  config.model = {std::chrono::microseconds(1), 50.0};
  return config;
}

// Waits up to ten seconds for condition to hold, and says whether it does.
template <typename Condition>
[[nodiscard]] bool wait_until(Condition condition) {
  for (int wait = 0; wait < 1000 && !condition(); ++wait) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return condition();
}

// report serialized, as the poll loop posts it.
[[nodiscard]] inline std::vector<std::byte> serialized(const v1::TimeQuality& report) {
  std::vector<std::byte> out(report.ByteSizeLong());
  static_cast<void>(report.SerializeToArray(out.data(), static_cast<int>(out.size())));
  return out;
}

// A report from station-1 with clock_state and time_utc_ns set.
[[nodiscard]] inline v1::TimeQuality report_at(const std::int64_t time_utc_ns,
                                               const v1::TimeQuality::ClockState clock_state) {
  v1::TimeQuality report;
  report.set_station_id("station-1");
  report.set_time_utc_ns(time_utc_ns);
  report.set_clock_state(clock_state);
  return report;
}

// A gRPC subscriber to the reports ics-timingd serves at path (#150). Its
// stream ends after kTimeout at the latest, so a test that waits for a report
// that never comes fails instead of hanging.
class Subscriber {
 public:
  static constexpr std::chrono::seconds kTimeout{30};

  explicit Subscriber(const std::filesystem::path& path)
      : stub_(v1::TimeQualityService::NewStub(
            grpc::CreateChannel("unix:" + path.native(), grpc::InsecureChannelCredentials()))) {
    context_.set_deadline(std::chrono::system_clock::now() + kTimeout);
    reader_ = stub_->WatchTimeQuality(&context_, v1::WatchTimeQualityRequest());
  }

  ~Subscriber() {
    context_.TryCancel();
    static_cast<void>(finish());
  }
  Subscriber(const Subscriber&) = delete;
  Subscriber& operator=(const Subscriber&) = delete;
  Subscriber(Subscriber&&) = delete;
  Subscriber& operator=(Subscriber&&) = delete;

  // The next report; none once the stream has ended.
  [[nodiscard]] std::optional<v1::TimeQuality> next() {
    v1::WatchTimeQualityResponse response;
    if (status_ || !reader_->Read(&response)) {
      static_cast<void>(finish());
      return std::nullopt;
    }
    return response.report();
  }

  // Asks the server to end the stream; a report already sent may still come.
  void cancel() { context_.TryCancel(); }

  // Waits for the stream to end, skipping the reports still coming, and
  // returns its status.
  [[nodiscard]] grpc::StatusCode finish() {
    if (status_) {
      return *status_;
    }
    v1::WatchTimeQualityResponse skipped;
    for (bool reading = true; reading;) {
      reading = reader_->Read(&skipped);
    }
    status_ = reader_->Finish().error_code();
    return *status_;
  }

 private:
  std::unique_ptr<v1::TimeQualityService::Stub> stub_;
  grpc::ClientContext context_;
  std::unique_ptr<grpc::ClientReader<v1::WatchTimeQualityResponse>> reader_;
  std::optional<grpc::StatusCode> status_;
};

}  // namespace ics::timingd::testing
