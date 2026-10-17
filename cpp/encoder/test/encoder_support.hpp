#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <string>
#include <string_view>

#include <gtest/gtest.h>
#include <unistd.h>

#include "ics/encoder/angles.hpp"
#include "ics/encoder/pointing_model.hpp"
#include "ics/tagger/pps_clock.hpp"
#include "ics/tagger/sampler.hpp"

namespace ics::encoder::testing {

// Encoders of 2^24 counts a revolution, as the bench's.
inline constexpr std::uint32_t kCountsPerRev = 1U << 24U;
inline constexpr double kCountRad = 2.0 * std::numbers::pi / kCountsPerRev;
// 2026-09-21T13:46:40Z, in ns since the Unix epoch.
inline constexpr std::int64_t kStartNs = 1'790'000'000'000'000'000;
inline constexpr std::int64_t kSampleNs = 1'000'000;
inline constexpr std::int64_t kBatchNs = kSampleNs * static_cast<std::int64_t>(tagger::kBatchSamples);
inline constexpr std::int64_t kLockedBoundNs = 500;

[[nodiscard]] inline EncoderConfig config(const bool azimuth_reversed = false, const bool elevation_reversed = false) {
  return EncoderConfig{.azimuth = {.counts_per_rev = kCountsPerRev, .reversed = azimuth_reversed},
                       .elevation = {.counts_per_rev = kCountsPerRev, .reversed = elevation_reversed}};
}

// A batch whose counts change by a fixed step each sample.
struct Ramp {
  std::uint32_t sequence = 0;
  // The first sample's time; the rest follow at 1 ms.
  std::int64_t first_ns = kStartNs;
  std::int32_t azimuth = 0;
  std::int32_t azimuth_step = 0;
  std::int32_t elevation = 0;
  std::int32_t elevation_step = 0;
  tagger::ClockState state = tagger::ClockState::kLocked;
  std::int64_t bound_ns = kLockedBoundNs;
};

[[nodiscard]] inline tagger::Batch batch(const Ramp& ramp) {
  tagger::Batch out{.sequence = ramp.sequence};
  for (std::size_t i = 0; i < out.samples.size(); ++i) {
    const auto step = static_cast<std::int32_t>(i);
    const bool acquiring = ramp.state == tagger::ClockState::kAcquiring;
    out.samples.at(i) =
        tagger::Sample{.utc_ns = acquiring ? 0 : ramp.first_ns + (static_cast<std::int64_t>(i) * kSampleNs),
                       .bound_ns = acquiring ? 0 : ramp.bound_ns,
                       .azimuth_counts = ramp.azimuth + (step * ramp.azimuth_step),
                       .elevation_counts = ramp.elevation + (step * ramp.elevation_step),
                       .state = ramp.state};
  }
  return out;
}

// The counts of an angle, rounded.
[[nodiscard]] inline std::int32_t counts(const double rad) {
  return static_cast<std::int32_t>(std::llround(rad / kCountRad));
}

// measured minus reference on the sky, in radians: across (delta azimuth
// times cos elevation) and up.
[[nodiscard]] inline double sky_error(const Direction measured, const Direction reference) {
  const double across =
      wrap_pi(measured.azimuth_rad - reference.azimuth_rad) * std::cos(reference.elevation_rad);
  return std::hypot(across, measured.elevation_rad - reference.elevation_rad);
}

// A file in the test's own temporary folder, written with text.
[[nodiscard]] inline std::filesystem::path write_temp(const std::string_view name, const std::string_view text) {
  const ::testing::TestInfo* info = ::testing::UnitTest::GetInstance()->current_test_info();
  // Per process, so that a file an earlier run left is never read.
  const std::filesystem::path dir = std::filesystem::temp_directory_path() /
                                    ("ics-encoder-test-" + std::to_string(::getpid())) /
                                    (std::string(info->test_suite_name()) + "." + info->name());
  std::filesystem::create_directories(dir);
  const std::filesystem::path path = dir / name;
  std::ofstream(path, std::ios::binary) << text;
  return path;
}

}  // namespace ics::encoder::testing
