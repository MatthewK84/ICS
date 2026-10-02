// Fuzzes the EGM96 grid reader (ICS-017). For any bytes, Egm96::parse either
// fails with Error::kMalformed or gives a grid whose height is finite at both
// poles, beside them, on the equator and on both sides of the antimeridian,
// which reaches every stencil table, column wrap and pole reflection.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/frames/egm96.hpp"
#include "ics/frames/geodetic.hpp"

namespace {

struct Probe {
  double latitude;
  double longitude;
};

constexpr std::array<Probe, 8> kProbes{{
    {90.0, 0.0},
    {89.99, 100.0},
    {89.99, -100.0},
    {0.0, -0.0001},
    {0.0, 0.0},
    {-45.0, -180.0},
    {-89.99, 170.0},
    {-90.0, -1.0},
}};

void require_finite_heights(const ics::frames::Egm96& grid) {
  for (const Probe& probe : kProbes) {
    const ics::Result<ics::frames::Geodetic> point =
        ics::frames::Geodetic::make(ics::Degrees(probe.latitude), ics::Degrees(probe.longitude), ics::Meters(0.0));
    if (!point || !std::isfinite(grid.geoid_height(*point).value())) {
      std::abort();
    }
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const std::span<const std::uint8_t> input{data, size};
  const std::string file(input.begin(), input.end());
  const ics::Result<ics::frames::Egm96> grid = ics::frames::Egm96::parse(file);
  if (!grid) {
    if (grid.error() != ics::Error::kMalformed) {
      std::abort();
    }
    return 0;
  }
  require_finite_heights(*grid);
  return 0;
}
