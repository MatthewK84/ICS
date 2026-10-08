#pragma once

#include <cstdint>
#include <vector>

#include "ics/retime/sorties.hpp"
#include "ics/timealign/clock_fit.hpp"

namespace ics::retime::testing {

// 2026-09-21T14:13:20Z, in nanoseconds.
inline constexpr std::int64_t kUtcNs = 1'790'000'000'000'000'000;

// Clock pairs from boot time first_us, a second apart, on a straight line
// with no drift, each off it by wobble_us times -1, 0 or 1 in turn.
inline std::vector<timealign::ClockSample> pairs(const std::int64_t first_us, const int count,
                                                 const std::int64_t wobble_us = 0) {
  constexpr std::int64_t kSecondUs = 1'000'000;
  constexpr std::int64_t kNsPerUs = 1'000;
  std::vector<timealign::ClockSample> out;
  for (int index = 0; index < count; ++index) {
    const std::int64_t boot_us = first_us + (index * kSecondUs);
    const std::int64_t wobble = ((index % 3) - 1) * wobble_us;
    out.push_back({.boot_us = boot_us, .utc_ns = kUtcNs + ((boot_us + wobble) * kNsPerUs)});
  }
  return out;
}

// A position of system 1 at boot time boot_us.
inline Positioned position(const std::int64_t boot_us) {
  Positioned out;
  out.record.set_entity_id("1");
  out.record.set_time_basis(v1::PLI_TIME_BASIS_RECEIPT);
  out.boot_us = boot_us;
  return out;
}

}  // namespace ics::retime::testing
