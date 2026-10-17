// ics-encoder-monitor, the operational test of the encoder path: what it
// reports, and its command.
#include "ics/encoder/monitor.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <google/protobuf/util/delimited_message_util.h>
#include <gtest/gtest.h>

#include "encoder_support.hpp"
#include "ics/common/units.hpp"
#include "ics/encoder/pointing_model.hpp"
#include "ics/encoder/receiver.hpp"
#include "ics/encoder/track.hpp"
#include "ics/logging/logger.hpp"
#include "ics/v1/camera_frame_meta.pb.h"
#include "udp_sender.hpp"

namespace {

using ics::encoder::AngleTrack;
using ics::encoder::PointingModel;
using ics::logging::Logger;
using ics::v1::CameraFrameMeta;
using namespace ics::encoder::testing;
using std::chrono::milliseconds;

constexpr std::int64_t kHostLagNs = 3'000'000;

[[nodiscard]] ics::UtcTime fixed_clock() noexcept { return ics::utc_from_ns(kStartNs); }

[[nodiscard]] Logger logger_to(std::ostringstream& out) {
  return Logger::to_stream("ics-encoder-monitor", ics::logging::Level::kDebug, out, &fixed_clock);
}

[[nodiscard]] PointingModel no_model() { return PointingModel::make({}).value(); }

[[nodiscard]] AngleTrack track_with(const std::uint32_t batches) {
  AngleTrack track = AngleTrack::make(config()).value();
  for (std::uint32_t i = 0; i < batches; ++i) {
    track.add(batch({.sequence = i, .first_ns = kStartNs + (static_cast<std::int64_t>(i) * kBatchNs)}));
  }
  return track;
}

[[nodiscard]] CameraFrameMeta frame_at(const std::int64_t start_ns, const CameraFrameMeta::TimeSource source) {
  CameraFrameMeta frame;
  frame.set_camera_id("north-hs");
  frame.set_segment_id("cine-000");
  frame.set_frame_index(42);
  frame.set_exposure_start_utc_ns(start_ns);
  frame.set_exposure_duration_ns(kSampleNs);
  frame.set_time_source(source);
  return frame;
}

[[nodiscard]] std::string meta_file(const std::vector<CameraFrameMeta>& frames) {
  std::ostringstream out;
  for (const CameraFrameMeta& frame : frames) {
    EXPECT_TRUE(google::protobuf::util::SerializeDelimitedToOstream(frame, &out));
  }
  return out.str();
}

TEST(Monitor, ReportsStatusBeforeAndAfterSamples) {
  std::ostringstream out;
  const Logger logger = logger_to(out);
  ics::encoder::report_status(track_with(0), {}, no_model(), fixed_clock(), logger);
  EXPECT_NE(out.str().find(R"("event":"encoder_status","datagrams":0)"), std::string::npos) << out.str();
  EXPECT_EQ(out.str().find("utc_ns"), std::string::npos);
  out.str("");
  const AngleTrack track = track_with(2);
  const ics::UtcTime host_now = track.newest()->utc + ics::Duration(kHostLagNs);
  ics::encoder::report_status(track, {.datagrams = 2, .malformed = 1}, no_model(), host_now, logger);
  EXPECT_NE(out.str().find(R"("datagrams":2,"malformed":1,"batches":2)"), std::string::npos) << out.str();
  EXPECT_NE(out.str().find(R"("clock_state":"locked")"), std::string::npos);
  EXPECT_NE(out.str().find(R"("lag_ns":3000000)"), std::string::npos);
}

TEST(Monitor, ReportsAFramesLineOfSightOrWhyNot) {
  std::ostringstream out;
  const Logger logger = logger_to(out);
  const AngleTrack track = track_with(2);
  ics::encoder::report_frame(track, no_model(), frame_at(kStartNs + kSampleNs, CameraFrameMeta::TIME_SOURCE_IRIG),
                             logger);
  EXPECT_NE(out.str().find(R"("event":"frame_line_of_sight","camera_id":"north-hs","segment_id":"cine-000",)"
                           R"("frame_index":42)"),
            std::string::npos)
      << out.str();
  out.str("");
  ics::encoder::report_frame(track, no_model(), frame_at(kStartNs, CameraFrameMeta::TIME_SOURCE_HOST), logger);
  EXPECT_NE(out.str().find(R"("event":"frame_refused")"), std::string::npos);
  EXPECT_NE(out.str().find(R"("error":"unconstrained")"), std::string::npos);
}

TEST(Monitor, ListensAndAddsWhatComes) {
  ics::encoder::DatagramReceiver receiver = ics::encoder::DatagramReceiver::make("127.0.0.1", 0).value();
  const UdpSender sender(receiver.port());
  for (std::uint32_t i = 0; i < 20; ++i) {
    ASSERT_TRUE(sender.send(batch({.sequence = i, .first_ns = kStartNs + (static_cast<std::int64_t>(i) * kBatchNs)})));
  }
  AngleTrack track = track_with(0);
  std::ostringstream out;
  ics::encoder::listen(receiver, track, no_model(), std::chrono::seconds(1), logger_to(out));
  EXPECT_EQ(track.stats().batches, 20U);
  EXPECT_EQ(receiver.stats().datagrams, 20U);
  EXPECT_NE(out.str().find(R"("batches":20)"), std::string::npos) << out.str();
}

TEST(Monitor, ReportsTheFramesInEachMetaFile) {
  const AngleTrack track = track_with(2);
  const std::filesystem::path good = write_temp("cine-000.meta", meta_file({
      frame_at(kStartNs + kSampleNs, CameraFrameMeta::TIME_SOURCE_IRIG),
      frame_at(kStartNs - kBatchNs, CameraFrameMeta::TIME_SOURCE_IRIG),
  }));
  static_cast<void>(write_temp("notes.txt", "not frames"));
  std::ostringstream out;
  EXPECT_TRUE(ics::encoder::report_frames(good.parent_path(), track, no_model(), logger_to(out)));
  EXPECT_NE(out.str().find("frame_line_of_sight"), std::string::npos) << out.str();
  EXPECT_NE(out.str().find(R"("error":"out of range")"), std::string::npos);
  const std::filesystem::path bad = write_temp("cine-001.meta", std::string("\x05\x08", 2));
  out.str("");
  EXPECT_FALSE(ics::encoder::report_frames(bad.parent_path(), track, no_model(), logger_to(out)));
  EXPECT_NE(out.str().find(R"("event":"meta_unreadable","file":"cine-001.meta")"), std::string::npos) << out.str();
}

// A free UDP port on the loopback address, as the tagger's port in a test
// config.
[[nodiscard]] std::uint16_t free_port() { return ics::encoder::DatagramReceiver::make("127.0.0.1", 0)->port(); }

// A config for the golden model, whose station is "golden".
[[nodiscard]] std::filesystem::path config_file(const std::string_view station, const std::string_view address,
                                                const std::uint16_t port) {
  const std::string text = "[log]\nlevel = \"error\"\nservice = \"ics-encoder-monitor\"\n\n[encoder]\n"
                           "station_id = \"" + std::string(station) + "\"\ntagger_address = \"" + std::string(address) +
                           "\"\ntagger_port = " + std::to_string(port) +
                           "\nazimuth_counts_per_rev = 16_777_216\nazimuth_reversed = false\n"
                           "elevation_counts_per_rev = 16_777_216\nelevation_reversed = false\n"
                           "pointing_model_file = \"" + std::string(ICS_GOLDEN_POINTING_DIR) + "/model.json\"\n";
  return write_temp("ics-encoder.toml", text);
}

[[nodiscard]] int run(const std::vector<const char*>& args, const std::filesystem::path& config) {
  return ics::encoder::run(args, config, config.parent_path());
}

TEST(Monitor, RefusesABadCommandLineOrConfig) {
  const std::filesystem::path config = config_file("golden", "127.0.0.1", free_port());
  EXPECT_EQ(run({"ics-encoder-monitor"}, config), ics::encoder::kExitUsage);
  for (const char* seconds : {"0", "601", "x", "5s", ""}) {
    EXPECT_EQ(run({"ics-encoder-monitor", seconds}, config), ics::encoder::kExitUsage) << seconds;
  }
  EXPECT_EQ(run({"ics-encoder-monitor", "1"}, config.parent_path() / "missing.toml"), ics::encoder::kExitUsage);
}

TEST(Monitor, FailsWithoutItsModelOrItsSocket) {
  std::filesystem::path other = config_file("station-1", "127.0.0.1", free_port());
  EXPECT_EQ(run({"ics-encoder-monitor", "1"}, other), ics::encoder::kExitFailed);
  other = config_file("golden", "not-an-address", free_port());
  EXPECT_EQ(run({"ics-encoder-monitor", "1"}, other), ics::encoder::kExitFailed);
  const std::string missing_model = "[log]\nlevel = \"error\"\nservice = \"m\"\n\n[encoder]\nstation_id = \"golden\"\n"
                                    "tagger_address = \"127.0.0.1\"\ntagger_port = 5020\n"
                                    "azimuth_counts_per_rev = 2\nazimuth_reversed = false\n"
                                    "elevation_counts_per_rev = 2\nelevation_reversed = false\n"
                                    "pointing_model_file = \"missing.json\"\n";
  EXPECT_EQ(run({"ics-encoder-monitor", "1"}, write_temp("missing-model.toml", missing_model)),
            ics::encoder::kExitFailed);
}

TEST(Monitor, SaysWhenNoTaggerWasHeard) {
  EXPECT_EQ(run({"ics-encoder-monitor", "1"}, config_file("golden", "127.0.0.1", free_port())),
            ics::encoder::kExitSilent);
}

TEST(Monitor, ListensToTheTaggerAndReadsTheFrames) {
  const std::uint16_t port = free_port();
  const std::filesystem::path config = config_file("golden", "127.0.0.1", port);
  static_cast<void>(write_temp("cine-000.meta", meta_file({frame_at(kStartNs, CameraFrameMeta::TIME_SOURCE_IRIG)})));
  std::atomic<bool> done{false};
  std::thread tagger([port, &done] {
    const UdpSender sender(port);
    for (std::uint32_t i = 0; !done.load(); ++i) {
      static_cast<void>(sender.send(batch({.sequence = i, .first_ns = kStartNs + (static_cast<std::int64_t>(i) * kBatchNs)})));
      std::this_thread::sleep_for(milliseconds(10));
    }
  });
  const int heard = run({"ics-encoder-monitor", "1"}, config);
  // A .meta file that is not frames fails the run, after listening.
  static_cast<void>(write_temp("cine-001.meta", std::string("\x05\x08", 2)));
  const int unreadable = run({"ics-encoder-monitor", "1"}, config);
  done.store(true);
  tagger.join();
  EXPECT_EQ(heard, ics::encoder::kExitHeard);
  EXPECT_EQ(unreadable, ics::encoder::kExitFailed);
}

}  // namespace
