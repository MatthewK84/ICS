// The time-tagger's emulated PPS-injection bench (ICS-032): the clock,
// sampler and datagram run against an emulated station, and every sample's
// time is compared with the true time it was latched at.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>

#include <gtest/gtest.h>

#include "emulated_station.hpp"
#include "ics/tagger/counter.hpp"
#include "ics/tagger/datagram.hpp"
#include "ics/tagger/pps_clock.hpp"
#include "ics/tagger/sampler.hpp"

namespace {

using ics::tagger::ClockState;
using ics::tagger::PpsResult;
using ics::tagger::testing::EmulatedStation;

constexpr std::int64_t kNsPerMs = 1'000'000;
constexpr std::int64_t kMsPerSecond = 1'000;
// C18: the most a time tag may be off.
constexpr std::int64_t kC18Ns = 50'000;

// What a run varies.
struct BenchRun {
  std::int64_t seconds = 600;
  // No PPS edge for the seconds from dropout_start up to dropout_end.
  std::int64_t dropout_start = 300;
  std::int64_t dropout_end = 320;
  // A glitch: an extra edge 437 ms into every glitch_every-th second.
  std::int64_t glitch_every = 37;
};

// What a run saw.
struct Tally {
  std::int64_t acquiring = 0;
  std::int64_t locked = 0;
  std::int64_t holdover = 0;
  // Samples whose time is further from the truth than their bound.
  std::int64_t out_of_bound = 0;
  std::int64_t max_locked_error_ns = 0;
  std::int64_t max_holdover_error_ns = 0;
  std::int64_t glitches = 0;
  std::int64_t rejected = 0;
  std::int64_t restarted = 0;
  std::int64_t batches = 0;
  // Batches that did not decode, or decoded out of sequence.
  std::int64_t bad_batches = 0;
  double max_angle_error_rad = 0.0;
};

[[nodiscard]] bool dropped(const BenchRun& run, const std::int64_t second) {
  return second >= run.dropout_start && second < run.dropout_end;
}

// Checks one decoded sample against the truth at the time it was latched.
void check_sample(const ics::tagger::Sample& sample, const std::int64_t t_ns, Tally& tally) {
  const double true_azimuth = EmulatedStation::kAzimuthRate * static_cast<double>(t_ns) / 1e9;
  const double azimuth = ics::tagger::angle_rad(sample.azimuth_counts, EmulatedStation::kCountsPerRev);
  tally.max_angle_error_rad = std::max(tally.max_angle_error_rad, std::abs(azimuth - true_azimuth));
  if (sample.state == ClockState::kAcquiring) {
    ++tally.acquiring;
    return;
  }
  const std::int64_t truth = (EmulatedStation::kStartSecond * 1'000'000'000) + t_ns;
  const std::int64_t error = std::abs(sample.utc_ns - truth);
  tally.out_of_bound += error > sample.bound_ns ? 1 : 0;
  if (sample.state == ClockState::kLocked) {
    ++tally.locked;
    tally.max_locked_error_ns = std::max(tally.max_locked_error_ns, error);
  } else {
    ++tally.holdover;
    tally.max_holdover_error_ns = std::max(tally.max_holdover_error_ns, error);
  }
}

// Sends a full batch through a datagram, and checks each sample; the batch's
// first sample was latched at first_ms.
void check_batch(const ics::tagger::Batch& batch, const std::int64_t first_ms, Tally& tally) {
  std::array<std::byte, ics::tagger::kDatagramBytes> bytes{};
  static_cast<void>(ics::tagger::encode(batch, bytes));
  const auto decoded = ics::tagger::decode(bytes);
  if (!decoded || decoded->sequence != static_cast<std::uint32_t>(tally.batches)) {
    ++tally.bad_batches;
    return;
  }
  ++tally.batches;
  for (std::size_t i = 0; i < decoded->samples.size(); ++i) {
    check_sample(decoded->samples[i], (first_ms + static_cast<std::int64_t>(i)) * kNsPerMs, tally);
  }
}

void count_edge(const PpsResult result, Tally& tally) {
  tally.rejected += result == PpsResult::kRejected ? 1 : 0;
  tally.restarted += result == PpsResult::kRestarted ? 1 : 0;
}

// Runs the tagger against station: each millisecond, the PPS edge if one is
// due, a glitch if one is due, the host's label 200 ms into each second, and
// a sample.
Tally run_bench(EmulatedStation& station, const BenchRun& run) {
  ics::tagger::PpsClock clock(ics::tagger::ClockConfig{.nominal_hz = EmulatedStation::kNominalHz});
  ics::tagger::CounterExtender counter;
  ics::tagger::Sampler sampler;
  Tally tally;
  for (std::int64_t ms = 0; ms < run.seconds * kMsPerSecond; ++ms) {
    const std::int64_t t = ms * kNsPerMs;
    const std::int64_t second = ms / kMsPerSecond;
    const std::int64_t in_second = ms % kMsPerSecond;
    if (in_second == 0 && !dropped(run, second)) {
      count_edge(clock.on_pps(counter.extend(station.counter(station.pps_edge(t)))), tally);
    }
    if (in_second == 437 && second % run.glitch_every == 5) {
      ++tally.glitches;
      count_edge(clock.on_pps(counter.extend(station.counter(t))), tally);
    }
    if (in_second == 200) {
      static_cast<void>(clock.label(EmulatedStation::kStartSecond + second, counter.extend(station.counter(t))));
    }
    if (sampler.add(clock, counter.extend(station.counter(t)), EmulatedStation::azimuth_counts(t),
                    EmulatedStation::elevation_counts())) {
      check_batch(sampler.batch(), ms - static_cast<std::int64_t>(ics::tagger::kBatchSamples) + 1, tally);
    }
  }
  return tally;
}

// ICS-032's "Done when": time-tag error ≤50 µs (C18) against injected PPS,
// here emulated. Ten minutes at 1,000 samples a second, a crystal 20 ppm fast
// and wandering, 30 ns of PPS jitter, a glitch edge every 37 s, and 20 s
// without PPS.
TEST(TaggerBench, MeetsC18AgainstInjectedPps) {
  EmulatedStation station(20.0, 0.01, 30.0, 1);
  const BenchRun run;
  const Tally tally = run_bench(station, run);
  // Locked from the third edge, at 2 s.
  EXPECT_EQ(tally.acquiring, 2 * kMsPerSecond);
  EXPECT_LE(tally.max_locked_error_ns, kC18Ns);
  EXPECT_LE(tally.max_locked_error_ns, 1'000);
  // Every sample's time within its bound, holdover included.
  EXPECT_EQ(tally.out_of_bound, 0);
  EXPECT_GE(tally.holdover, 19 * kMsPerSecond);
  EXPECT_LE(tally.holdover, 20 * kMsPerSecond);
  EXPECT_LE(tally.max_holdover_error_ns, kC18Ns);
  EXPECT_EQ(tally.locked + tally.holdover + tally.acquiring, run.seconds * kMsPerSecond);
  // Every glitch rejected, and the clock never started again.
  EXPECT_EQ(tally.rejected, tally.glitches);
  EXPECT_EQ(tally.restarted, 0);
  EXPECT_EQ(tally.batches, run.seconds * kMsPerSecond / 10);
  EXPECT_EQ(tally.bad_batches, 0);
  // Within one count of the true azimuth.
  EXPECT_LE(tally.max_angle_error_rad, 2.0 * std::numbers::pi / EmulatedStation::kCountsPerRev);
  RecordProperty("max_locked_error_ns", static_cast<int>(tally.max_locked_error_ns));
  RecordProperty("max_holdover_error_ns", static_cast<int>(tally.max_holdover_error_ns));
}

// A crystal near the edge of its tolerance, slow and wandering the other
// way, with more PPS jitter.
TEST(TaggerBench, MeetsC18WithASlowNoisyCrystal) {
  EmulatedStation station(-45.0, -0.02, 200.0, 2);
  const BenchRun run{.seconds = 120, .dropout_start = 60, .dropout_end = 70, .glitch_every = 11};
  const Tally tally = run_bench(station, run);
  EXPECT_EQ(tally.acquiring, 2 * kMsPerSecond);
  EXPECT_LE(tally.max_locked_error_ns, kC18Ns);
  EXPECT_EQ(tally.out_of_bound, 0);
  EXPECT_GT(tally.holdover, 0);
  EXPECT_EQ(tally.rejected, tally.glitches);
  EXPECT_EQ(tally.bad_batches, 0);
}

}  // namespace
