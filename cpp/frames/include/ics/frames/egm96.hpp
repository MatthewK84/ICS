#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/frames/geodetic.hpp"

namespace ics::frames {

// The EGM96 geoid, which defines mean sea level (MSL) in ICS (ICS-017;
// docs/frames-and-time.md). Heights come from GeographicLib's egm96-5 grid,
// interpolated cubically exactly as GeographicLib's Geoid class does, which
// stays within 3 mm of the full EGM96 model.
//
// Load the grid once at start-up; it is about 19 MB. Lookups after that are
// noexcept, allocate nothing and are safe from several threads.
class Egm96 {
 public:
  // The grid's location in the ICS images.
  static constexpr std::string_view kDefaultPath = "/usr/share/GeographicLib/geoids/egm96-5.pgm";

  // Reads a grid file. Fails with Error::kUnreadable when the file cannot be
  // opened or is not a regular file, and as parse() otherwise.
  [[nodiscard]] static Result<Egm96> load(const std::filesystem::path& path);

  // Reads a grid from the bytes of a GeographicLib geoid file: a 16-bit PGM
  // image whose header comments give the height Offset and Scale. Fails with
  // Error::kMalformed when the bytes do not follow that format.
  [[nodiscard]] static Result<Egm96> parse(std::string_view file);

  Egm96(const Egm96&) = delete;
  Egm96& operator=(const Egm96&) = delete;
  Egm96(Egm96&&) noexcept = default;
  Egm96& operator=(Egm96&&) noexcept = default;
  ~Egm96() = default;

  // N, the height of the geoid above the ellipsoid at a point's latitude and
  // longitude; the point's own height plays no part.
  [[nodiscard]] Meters geoid_height(const Geodetic& point) const noexcept;

  // The geodetic point at an MSL height H, with ellipsoid height h = H + N.
  // Fails as Geodetic::make does.
  [[nodiscard]] Result<Geodetic> from_msl(Degrees latitude, Degrees longitude, Meters msl_height) const noexcept;

  // H = h - N, a point's height above mean sea level.
  [[nodiscard]] Meters msl_height(const Geodetic& point) const noexcept;

 private:
  Egm96(std::vector<std::uint16_t> samples, int width, int height, double offset, double scale) noexcept;

  // The sample at column x and row y (row 0 at 90 degrees north). Columns
  // wrap around the globe, and a row beyond a pole reflects across it.
  [[nodiscard]] double sample(int x, int y) const noexcept;

  std::vector<std::uint16_t> samples_;
  int width_;
  int height_;
  double offset_;
  double scale_;
};

}  // namespace ics::frames
