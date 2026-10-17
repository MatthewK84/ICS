// ICS-036's emulated bench: ICS-032's tagger core stamps a simulated mount's
// quantized encoders against an emulated station's clock and PPS, and sends
// its batches over UDP; the encoder module receives them and gives frames
// at random times and exposures their line of sight, each compared with the
// truth: the pointing model applied to the true axis angles at the true
// mid-exposure.
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>
#include <set>
#include <string>

#include <gtest/gtest.h>

#include "bench_mount.hpp"
#include "emulated_station.hpp"
#include "encoder_support.hpp"
#include "ics/common/error.hpp"
#include "ics/encoder/angles.hpp"
#include "ics/encoder/line_of_sight.hpp"
#include "ics/encoder/pointing_model.hpp"
#include "ics/encoder/receiver.hpp"
#include "ics/encoder/track.hpp"
#include "ics/tagger/counter.hpp"
#include "ics/tagger/pps_clock.hpp"
#include "ics/tagger/sampler.hpp"
#include "ics/v1/camera_frame_meta.pb.h"
#include "udp_sender.hpp"

namespace {

using ics::encoder::AngleTrack;
using ics::encoder::DatagramReceiver;
using ics::encoder::Direction;
using ics::encoder::PointingModel;
using ics::tagger::ClockState;
using ics::tagger::testing::EmulatedStation;
using ics::v1::CameraFrameMeta;
using namespace ics::encoder::testing;

constexpr std::int64_t kNsPerMs = 1'000'000;
constexpr std::int64_t kNsPerSecond = 1'000'000'000;
constexpr std::int64_t kMsPerSecond = 1'000;
constexpr std::int64_t kMsPerBatch = 10;
// ICS-036's "Done when".
constexpr double kDoneWhenRad = 5e-6;
// IRIG-B frames are within a microsecond of UTC (the card's assumption 2).
constexpr std::int64_t kIrigErrorNs = 1'000;
constexpr std::int64_t kMinExposureNs = 1'000;
constexpr std::int64_t kMaxExposureNs = 5'000'000;
// Frames ask for any time in the last 1.5 s, from 4 s on, once the clock
// has locked; and at least 10 us before the newest sample, which is more
// than the sample's and the frame's time errors together, so that a frame
// the truth puts in the history is in it.
constexpr double kMinLookbackMs = 0.01;
constexpr double kLookbackMs = 1'500.0;
constexpr std::int64_t kFirstFrameMs = 4'000;
constexpr std::chrono::milliseconds kArrival{2'000};

// What a run varies.
struct BenchRun {
  std::int64_t seconds = 60;
  // No PPS edge from dropout_start up to dropout_end, in seconds.
  std::int64_t dropout_start = 30;
  std::int64_t dropout_end = 50;
  // Every drop_every-th datagram is lost, and every host_every-th frame is
  // timed by the host.
  std::uint32_t drop_every = 97;
  std::int64_t host_every = 50;
  std::uint64_t seed = 1;
};

// What a run saw.
struct Tally {
  std::int64_t frames = 0;
  std::int64_t seen = 0;
  std::int64_t holdover = 0;
  std::int64_t refused_in_gap = 0;
  std::int64_t refused_host = 0;
  // Frames refused for any other reason, or host-timed frames taken.
  std::int64_t wrongly_refused = 0;
  // Frames further from the truth than their bound allows.
  std::int64_t over_bound = 0;
  std::int64_t undelivered = 0;
  double max_error_rad = 0.0;
  double max_holdover_error_rad = 0.0;
  double max_bound_rad = 0.0;
};

// The tagger's side of the bench: the emulated station's clock and PPS,
// the tagger core, and the simulated mount's encoders.
class Tagger {
 public:
  Tagger(EmulatedStation& station, const BenchRun& run) : station_(station), run_(run) {}

  // The tagger's millisecond at true time ms: the PPS edge and the host's
  // label when due, and a sample. True when it fills a batch.
  [[nodiscard]] bool tick(const std::int64_t ms) {
    const std::int64_t t = ms * kNsPerMs;
    const std::int64_t second = ms / kMsPerSecond;
    const std::int64_t in_second = ms % kMsPerSecond;
    const bool dropped = second >= run_.dropout_start && second < run_.dropout_end;
    if (in_second == 0 && !dropped) {
      static_cast<void>(clock_.on_pps(counter_.extend(station_.counter(station_.pps_edge(t)))));
    }
    if (in_second == 200) {
      static_cast<void>(clock_.label(EmulatedStation::kStartSecond + second, counter_.extend(station_.counter(t))));
    }
    const double seconds = static_cast<double>(t) / kNsPerSecond;
    return sampler_.add(clock_, counter_.extend(station_.counter(t)), mount_.azimuth_counts(seconds),
                        mount_.elevation_counts(seconds));
  }

  [[nodiscard]] const ics::tagger::Batch& batch() const { return sampler_.batch(); }
  [[nodiscard]] const BenchMount& mount() const { return mount_; }

 private:
  EmulatedStation& station_;
  const BenchRun& run_;
  ics::tagger::PpsClock clock_{ics::tagger::ClockConfig{.nominal_hz = EmulatedStation::kNominalHz}};
  ics::tagger::CounterExtender counter_;
  ics::tagger::Sampler sampler_;
  BenchMount mount_;
};

// The host's side: the receiver, the track, and the frames it is asked about.
struct Host {
  explicit Host(const PointingModel& with) : model(with) {}

  DatagramReceiver receiver = DatagramReceiver::make("127.0.0.1", 0).value();
  AngleTrack track = AngleTrack::make(config(false, true)).value();
  PointingModel model;
  std::set<std::int64_t> dropped_batches;
};

// A frame whose true mid-exposure is mid_ns, as its camera reports it.
[[nodiscard]] CameraFrameMeta frame_at(const std::int64_t mid_ns, std::mt19937_64& random, const bool host_timed) {
  std::uniform_int_distribution<std::int64_t> exposure(kMinExposureNs, kMaxExposureNs);
  std::uniform_int_distribution<std::int64_t> irig_error(-kIrigErrorNs, kIrigErrorNs);
  const std::int64_t duration = exposure(random);
  CameraFrameMeta frame;
  frame.set_exposure_duration_ns(duration);
  frame.set_exposure_start_utc_ns((EmulatedStation::kStartSecond * kNsPerSecond) + mid_ns - (duration / 2) +
                                  irig_error(random));
  frame.set_time_source(host_timed ? CameraFrameMeta::TIME_SOURCE_HOST : CameraFrameMeta::TIME_SOURCE_IRIG);
  return frame;
}

// Whether a time falls between samples that a lost batch left more than
// kMaxGap apart.
[[nodiscard]] bool in_gap(const Host& host, const std::int64_t mid_ns) {
  const std::int64_t before = mid_ns / kNsPerMs;
  return host.dropped_batches.contains(before / kMsPerBatch) ||
         host.dropped_batches.contains((before + 1) / kMsPerBatch);
}

// How far a frame may be from the truth: its bound, and what the bound
// leaves to the caller: the frame's own time error, and the curvature of
// the path between samples, a / 8 of a sample step squared.
[[nodiscard]] double allowance(const ics::encoder::FrameLineOfSight& seen, const BenchMount& mount) {
  const double cos_e = std::cos(seen.axes.encoder.elevation_rad);
  const double sky_rate = std::hypot(seen.axes.azimuth_rate_rad_per_s * cos_e, seen.axes.elevation_rate_rad_per_s);
  const double step_s = static_cast<double>(kNsPerMs) / kNsPerSecond;
  const double curvature = std::max(mount.azimuth.accel(), mount.elevation.accel()) * step_s * step_s / 8.0;
  return seen.axes.bound_rad + (sky_rate * static_cast<double>(kIrigErrorNs) / kNsPerSecond) + curvature;
}

// Asks for one frame's line of sight and checks it against the truth.
void check_frame(const Host& host, const BenchMount& mount, const std::int64_t mid_ns, const bool host_timed,
                 std::mt19937_64& random, Tally& tally) {
  ++tally.frames;
  const auto seen = ics::encoder::line_of_sight(host.track, host.model, frame_at(mid_ns, random, host_timed));
  if (host_timed) {
    tally.refused_host += !seen && seen.error() == ics::Error::kUnconstrained ? 1 : 0;
    tally.wrongly_refused += seen ? 1 : 0;
    return;
  }
  if (!seen) {
    const bool gap = in_gap(host, mid_ns) && seen.error() == ics::Error::kUnconstrained;
    tally.refused_in_gap += gap ? 1 : 0;
    tally.wrongly_refused += gap ? 0 : 1;
    return;
  }
  ++tally.seen;
  const Direction truth =
      ics::encoder::to_sky(host.model, mount.truth(static_cast<double>(mid_ns) / kNsPerSecond));
  const double error = sky_error(seen->sky, truth);
  tally.over_bound += error > allowance(*seen, mount) ? 1 : 0;
  tally.max_error_rad = std::max(tally.max_error_rad, error);
  tally.max_bound_rad = std::max(tally.max_bound_rad, seen->axes.bound_rad);
  if (seen->axes.state == ClockState::kHoldover) {
    ++tally.holdover;
    tally.max_holdover_error_rad = std::max(tally.max_holdover_error_rad, error);
  }
}

// Sends a full batch over UDP, or loses it, and adds what arrives.
[[nodiscard]] bool deliver(Host& host, const UdpSender& sender, const ics::tagger::Batch& batch,
                           const BenchRun& run, Tally& tally) {
  if (batch.sequence % run.drop_every == run.drop_every - 1) {
    host.dropped_batches.insert(batch.sequence);
    return false;
  }
  const auto received = sender.send(batch) ? host.receiver.receive(kArrival) : ics::fail(ics::Error::kUnavailable);
  if (!received || received->sequence != batch.sequence) {
    ++tally.undelivered;
    return false;
  }
  host.track.add(*received);
  return true;
}

[[nodiscard]] Tally run_bench(EmulatedStation& station, const BenchRun& run, Host& host) {
  Tagger tagger(station, run);
  const UdpSender sender(host.receiver.port());
  std::mt19937_64 random(run.seed);
  std::uniform_real_distribution<double> lookback(kMinLookbackMs, kLookbackMs);
  Tally tally;
  for (std::int64_t ms = 0; ms < run.seconds * kMsPerSecond; ++ms) {
    if (!tagger.tick(ms) || !deliver(host, sender, tagger.batch(), run, tally) || ms < kFirstFrameMs) {
      continue;
    }
    // One frame a batch, its middle up to 1.5 s before the newest sample.
    const auto mid_ns = static_cast<std::int64_t>(std::llround((static_cast<double>(ms) - lookback(random)) *
                                                               static_cast<double>(kNsPerMs)));
    check_frame(host, tagger.mount(), mid_ns, (ms / kMsPerBatch) % run.host_every == 0, random, tally);
  }
  return tally;
}

[[nodiscard]] PointingModel golden_model() {
  return ics::encoder::load_pointing_model(std::string(ICS_GOLDEN_POINTING_DIR) + "/model.json").value().model;
}

void record(const Tally& tally) {
  ::testing::Test::RecordProperty("frames", static_cast<int>(tally.frames));
  ::testing::Test::RecordProperty("max_error_nrad", static_cast<int>(tally.max_error_rad * 1e9));
  ::testing::Test::RecordProperty("max_holdover_error_nrad", static_cast<int>(tally.max_holdover_error_rad * 1e9));
  ::testing::Test::RecordProperty("max_bound_nrad", static_cast<int>(tally.max_bound_rad * 1e9));
}

// ICS-036's "Done when": 5 urad or less at 0.1 rad/s. A minute of slews
// through north and back at 0.1 rad/s on both axes, 20 s of it without PPS,
// a datagram in 97 lost, and a frame each batch.
TEST(EncoderBench, MeetsFiveMicroradiansAtPointOneRadianASecond) {
  EmulatedStation station(20.0, 0.01, 30.0, 1);
  const BenchRun run;
  Host host(golden_model());
  const Tally tally = run_bench(station, run, host);
  EXPECT_LE(tally.max_error_rad, kDoneWhenRad);
  EXPECT_EQ(tally.over_bound, 0);
  EXPECT_EQ(tally.wrongly_refused, 0);
  EXPECT_EQ(tally.undelivered, 0);
  EXPECT_GT(tally.seen, 5'000);
  EXPECT_GT(tally.holdover, 1'000);
  EXPECT_GT(tally.refused_in_gap, 0);
  EXPECT_GT(tally.refused_host, 0);
  EXPECT_EQ(tally.seen + tally.refused_in_gap + tally.refused_host, tally.frames);
  EXPECT_EQ(host.track.stats().lost_batches, host.dropped_batches.size());
  EXPECT_EQ(host.receiver.stats().malformed, 0U);
  record(tally);
}

// A crystal near the edge of its tolerance, with more PPS jitter, and no
// datagrams lost.
TEST(EncoderBench, MeetsItWithASlowNoisyCrystal) {
  EmulatedStation station(-45.0, -0.02, 200.0, 2);
  const BenchRun run{.seconds = 30, .dropout_start = 15, .dropout_end = 25, .drop_every = 100'000, .seed = 2};
  Host host(golden_model());
  const Tally tally = run_bench(station, run, host);
  EXPECT_LE(tally.max_error_rad, kDoneWhenRad);
  EXPECT_EQ(tally.over_bound, 0);
  EXPECT_EQ(tally.wrongly_refused, 0);
  EXPECT_EQ(tally.refused_in_gap, 0);
  EXPECT_EQ(host.track.stats().lost_batches, 0U);
  record(tally);
}

}  // namespace
