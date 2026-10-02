#include "ics/frames/egm96.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/frames/geodetic.hpp"
#include "ics/testing/no_allocation_scope.hpp"

namespace {

using ics::Degrees;
using ics::Meters;
using ics::frames::Egm96;
using ics::frames::Geodetic;

constexpr std::string_view kNumbers = "# Offset -1\n# Scale 0.5\n";
constexpr int kWidth = 8;
constexpr int kHeight = 5;

using SampleAt = std::function<std::uint16_t(int, int)>;

// The bytes of a geoid grid file: a header, then big-endian 16-bit samples.
std::string grid_file(const std::string_view header, const int width, const int height, const SampleAt& sample_at) {
  std::string file = "P5\n";
  file.append(header).append(std::to_string(width)).append(" ").append(std::to_string(height)).append("\n65535\n");
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const std::uint16_t sample = sample_at(x, y);
      file.push_back(static_cast<char>(sample >> 8U));
      file.push_back(static_cast<char>(sample & 0xFFU));
    }
  }
  return file;
}

std::string constant_grid(const std::string_view header) {
  return grid_file(header, kWidth, kHeight, [](int, int) { return std::uint16_t{1000}; });
}

Geodetic point(const double latitude, const double longitude) {
  return Geodetic::make(Degrees(latitude), Degrees(longitude), Meters(0.0)).value();
}

Egm96 parsed(const std::string& file) {
  ics::Result<Egm96> grid = Egm96::parse(file);
  EXPECT_TRUE(grid.has_value());
  return std::move(grid).value();
}

void expect_malformed(const std::string& file) {
  const ics::Result<Egm96> grid = Egm96::parse(file);
  ASSERT_FALSE(grid.has_value()) << file.substr(0, 80);
  EXPECT_EQ(grid.error(), ics::Error::kMalformed) << file.substr(0, 80);
}

// A constant grid interpolates to the same height everywhere, which takes
// every stencil table, every column wrap and both reflections at each pole.
TEST(Egm96, InterpolatesAConstantGridToItsValue) {
  const Egm96 grid = parsed(constant_grid(kNumbers));
  for (const double latitude : {90.0, 89.9, 45.0, 0.0, -10.0, -89.9, -90.0}) {
    for (const double longitude : {-180.0, -100.0, -0.001, 0.0, 0.001, 100.0, 179.999}) {
      EXPECT_DOUBLE_EQ(grid.geoid_height(point(latitude, longitude)).value(), -1.0 + 0.5 * 1000.0)
          << latitude << " " << longitude;
    }
  }
}

// A pole is one point, so its row of samples holds one value. The north and
// south fits leave out the terms in longitude alone, so the height at each
// pole is then the same from every side, however the rows beside it vary.
TEST(Egm96, GivesEachPoleOneHeight) {
  const Egm96 grid = parsed(grid_file(kNumbers, kWidth, kHeight, [](int x, int y) {
    const bool pole = y == 0 || y == kHeight - 1;
    return static_cast<std::uint16_t>(pole ? 500 + 300 * y : 100 * x + 7 * y * y);
  }));
  for (const double pole : {90.0, -90.0}) {
    const double first = grid.geoid_height(point(pole, -180.0)).value();
    for (const double longitude : {-90.0, -1.0, 0.0, 45.0, 135.0}) {
      EXPECT_NEAR(grid.geoid_height(point(pole, longitude)).value(), first, 1e-9) << pole << " " << longitude;
    }
  }
}

TEST(Egm96, ConvertsBetweenMslAndEllipsoidHeights) {
  const Egm96 grid = parsed(constant_grid(kNumbers));
  const ics::Result<Geodetic> point_above = grid.from_msl(Degrees(40.0), Degrees(-100.0), Meters(700.0));
  ASSERT_TRUE(point_above.has_value());
  EXPECT_EQ(point_above->height(), Meters(700.0 + 499.0));
  EXPECT_EQ(grid.msl_height(*point_above), Meters(700.0));
  const ics::Result<Geodetic> beyond_pole = grid.from_msl(Degrees(91.0), Degrees(0.0), Meters(0.0));
  ASSERT_FALSE(beyond_pole.has_value());
  EXPECT_EQ(beyond_pole.error(), ics::Error::kOutOfRange);
}

TEST(Egm96, AcceptsNotesBlankLinesAndRepeatedKeys) {
  const Egm96 grid = parsed(
      constant_grid("# Description test grid\n\n#NoSpace 1\n# Offset 7\n# Offset -1\n# Scale 0.5\n# MaxCubicError x\n"));
  EXPECT_EQ(grid.geoid_height(point(0.0, 0.0)), Meters(499.0));
}

TEST(Egm96, RejectsAHeaderGeographicLibWouldReject) {
  expect_malformed("");
  expect_malformed("P6\n");
  expect_malformed("P5\r\n# Offset -1\n# Scale 0.5\n8 5\n65535\n");
  expect_malformed(constant_grid("# Scale 0.5\n"));
  expect_malformed(constant_grid("# Offset -1\n"));
  expect_malformed(constant_grid("# Offset none\n# Scale 0.5\n"));
  expect_malformed(constant_grid("# Offset inf\n# Scale 0.5\n"));
  expect_malformed(constant_grid("# Offset -1\n# Scale 0\n"));
  expect_malformed(constant_grid("# Offset -1\n# Scale -0.5\n"));
  expect_malformed(constant_grid("# Offset -1\n# Scale nan\n"));
  expect_malformed(constant_grid("#Offset -1\n# Scale 0.5\n"));
  expect_malformed("P5\n# Offset -1\n# Scale 0.5\n");
}

TEST(Egm96, RejectsASizeOrMaxvalGeographicLibWouldReject) {
  const SampleAt zero = [](int, int) { return std::uint16_t{0}; };
  expect_malformed(grid_file(kNumbers, 7, 5, zero));
  expect_malformed(grid_file(kNumbers, 0, 5, zero));
  expect_malformed(grid_file(kNumbers, 8, 4, zero));
  expect_malformed(grid_file(kNumbers, 8, 1, zero));
  expect_malformed("P5\n# Offset -1\n# Scale 0.5\n8\n65535\n");
  expect_malformed("P5\n# Offset -1\n# Scale 0.5\nx 5\n65535\n");
  expect_malformed("P5\n# Offset -1\n# Scale 0.5\n  \n65535\n");
  expect_malformed("P5\n# Offset -1\n# Scale 0.5\n2 3\n255\n" + std::string(12, '\0'));
  expect_malformed("P5\n# Offset -1\n# Scale 0.5\n2 3\n");
  expect_malformed("P5\n# Offset -1\n# Scale 0.5\n2 3\n65535");
}

TEST(Egm96, RejectsSamplesOfTheWrongLength) {
  const std::string file = constant_grid(kNumbers);
  expect_malformed(file.substr(0, file.size() - 1));
  expect_malformed(file + '\0');
}

TEST(Egm96, ReportsAFileItCannotRead) {
  for (const std::filesystem::path& path : {std::filesystem::path("/nonexistent/egm96-5.pgm"),
                                            std::filesystem::temp_directory_path()}) {
    const ics::Result<Egm96> grid = Egm96::load(path);
    ASSERT_FALSE(grid.has_value()) << path;
    EXPECT_EQ(grid.error(), ics::Error::kUnreadable) << path;
  }
}

TEST(Egm96, LoadsTheInstalledGridAndLooksUpWithoutAllocating) {
  const ics::Result<Egm96> grid = Egm96::load(ICS_EGM96_PATH);
  ASSERT_TRUE(grid.has_value()) << ICS_EGM96_PATH;
  const Geodetic origin = point(40.0, -100.0);
  const ics::testing::NoAllocationScope no_allocation;
  // golden/frames/egm96-5.csv, range-origin.
  EXPECT_NEAR(grid->geoid_height(origin).value(), -25.0527, 1e-4);
}

}  // namespace
