#pragma once

#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/util/delimited_message_util.h>
#include <gtest/gtest.h>
#include <pthread.h>
#include <signal.h>

#include "ics/camd/config.hpp"
#include "ics/camd/drivers.hpp"
#include "ics/camera/segment_camera.hpp"
#include "ics/common/units.hpp"
#include "ics/logging/logger.hpp"
#include "ics/timingd/report_server.hpp"
#include "ics/v1/camera_frame_meta.pb.h"
#include "ics/v1/time_quality.pb.h"

namespace ics::camd::testing {

using std::chrono::microseconds;
using std::chrono::milliseconds;

// The logs' timestamps.
inline UtcTime fixed_clock() noexcept { return utc_from_ns(0); }

// A logger, and what it wrote.
struct Logged {
  std::ostringstream out;
  logging::Logger logger = logging::Logger::to_stream("ics-camd", logging::Level::kDebug, out, &fixed_clock);
};

// The station's UTC in the reports the tests serve.
inline UtcTime station_now() noexcept {
  return std::chrono::time_point_cast<Duration>(std::chrono::system_clock::now());
}

// A report under which IRIG-timed frames verify.
inline v1::TimeQuality locked_report() {
  v1::TimeQuality quality;
  quality.set_station_id("station-1");
  quality.set_irig_b_locked(true);
  quality.set_time_utc_ns(to_utc_ns(station_now()));
  return quality;
}

// The station's time quality, fixed.
class FixedQuality final : public camera::TimeQualitySource {
 public:
  explicit FixedQuality(v1::TimeQuality quality) : quality_(std::move(quality)) {}
  [[nodiscard]] v1::TimeQuality current() override { return quality_; }

 private:
  v1::TimeQuality quality_;
};

// The camera bench's settings (cpp/testing/camera_bench): 64 by 32 pixels,
// segments of 500 frames, 400 of them after the trigger, IRIG-timed.
inline camera::CameraSettings settings(const std::uint32_t frame_rate, const Duration exposure) {
  return camera::CameraSettings{.width = 64,
                                .height = 32,
                                .window_x = 0,
                                .window_y = 0,
                                .frame_rate = frame_rate,
                                .exposure = exposure,
                                .segment_frames = 500,
                                .post_trigger_frames = 400,
                                .irig = true};
}

// An emulated Phantom: 5,000 frames/s, triggers 120 ms apart.
inline CameraConfig phantom(const std::string& id) {
  return CameraConfig{.id = id,
                      .settings = settings(5000, microseconds(150)),
                      .segments = 2,
                      .frames = camera::FrameRange{.first = -50, .count = 300},
                      .driver = EmulatedPhantomDriver{.trigger_interval = milliseconds(120)}};
}

// An emulated X6980: 1,004 frames/s in a window at (288, 240), stamped
// without the year, triggers 600 ms apart.
inline CameraConfig x6980(const std::string& id) {
  CameraConfig out{.id = id,
                   .settings = settings(1004, microseconds(800)),
                   .segments = 2,
                   .frames = camera::FrameRange{.first = -50, .count = 300},
                   .driver = EmulatedX6980Driver{.trigger_interval = milliseconds(600), .stamps_year = false}};
  out.settings.window_x = 288;
  out.settings.window_y = 240;
  return out;
}

// A station of the two cameras, its sockets and offload folder in dir.
inline Config test_config(const std::filesystem::path& dir) {
  Config out;
  out.log.service = "ics-camd";
  out.station_id = "station-1";
  out.command_socket = dir / "command";
  out.time_socket = dir / "time-quality";
  out.offload_folder = dir / "runs";
  out.time_wait = std::chrono::seconds(2);
  out.cameras = {phantom("north-hs"), x6980("north-mwir")};
  return out;
}

// ics-timingd's server, serving one report at socket.
inline std::unique_ptr<timingd::ReportServer> serve_report(const std::filesystem::path& socket,
                                                           const v1::TimeQuality& report) {
  auto server = timingd::ReportServer::open(socket);
  EXPECT_TRUE(server.has_value());
  const std::string bytes = report.SerializeAsString();
  (*server)->reserve(bytes.size());
  (*server)->post(std::as_bytes(std::span(bytes)));
  return std::move(*server);
}

// The messages of a metadata file.
inline std::vector<v1::CameraFrameMeta> read_meta(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  google::protobuf::io::IstreamInputStream stream(&in);
  std::vector<v1::CameraFrameMeta> out;
  v1::CameraFrameMeta frame;
  bool clean_eof = false;
  while (google::protobuf::util::ParseDelimitedFromZeroCopyStream(&frame, &stream, &clean_eof)) {
    out.push_back(frame);
  }
  EXPECT_TRUE(clean_eof) << path;
  return out;
}

// Blocks SIGTERM in this thread, and in threads it starts, while it lives;
// then discards a SIGTERM left pending and restores the signal mask. The
// tests send SIGTERM to the serving thread, not to the process, as
// ics-timingd's do: a gRPC thread could take a signal sent to the process.
class BlockedSigterm {
 public:
  BlockedSigterm() {
    sigemptyset(&sigterm_);
    sigaddset(&sigterm_, SIGTERM);
    ::pthread_sigmask(SIG_BLOCK, &sigterm_, &saved_);
  }
  ~BlockedSigterm() {
    const timespec now{};
    ::sigtimedwait(&sigterm_, nullptr, &now);
    ::pthread_sigmask(SIG_SETMASK, &saved_, nullptr);
  }
  BlockedSigterm(const BlockedSigterm&) = delete;
  BlockedSigterm& operator=(const BlockedSigterm&) = delete;
  BlockedSigterm(BlockedSigterm&&) = delete;
  BlockedSigterm& operator=(BlockedSigterm&&) = delete;

 private:
  sigset_t sigterm_{};
  sigset_t saved_{};
};

// Waits up to 5 s for a file to appear: whether it did.
inline bool wait_for_file(const std::filesystem::path& path) {
  for (int i = 0; i < 500 && !std::filesystem::exists(path); ++i) {
    std::this_thread::sleep_for(milliseconds(10));
  }
  return std::filesystem::exists(path);
}

}  // namespace ics::camd::testing
