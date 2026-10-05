#pragma once

#include <span>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/flightlog/import.hpp"
#include "ics/frames/egm96.hpp"
#include "ics/frames/enu.hpp"
#include "ics/frames/geodetic.hpp"
#include "ics/v1/common.pb.h"

namespace ics::flightlog::testing {

inline const frames::Egm96& geoid() {
  static const Result<frames::Egm96> grid = frames::Egm96::load(ICS_EGM96_PATH);
  EXPECT_TRUE(grid.has_value()) << ICS_EGM96_PATH;
  return *grid;
}

// The range frame the tests rotate velocities into.
inline frames::EnuFrame range() {
  return frames::EnuFrame(frames::Geodetic::make(Degrees(40.0), Degrees(-100.0), Meters(700.0)).value());
}

// Settings with system 2 as the target.
inline ImportSettings settings() { return ImportSettings{.roles = {{.system = 2, .role = v1::ENTITY_ROLE_TARGET}}}; }

inline Result<LogContents> try_import(const std::span<const std::byte> log) {
  return import_log(log, settings(), geoid(), range());
}

inline LogContents imported(const std::span<const std::byte> log) {
  Result<LogContents> contents = try_import(log);
  EXPECT_TRUE(contents.has_value());
  return std::move(contents).value();
}

}  // namespace ics::flightlog::testing
