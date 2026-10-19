// demand.cpp: the axis rates for the target (ICS-038).
// JSF deviation D-6: ics::check and ics::encoder::wrap_pi come from .hpp headers.
#include <ics/common/check.hpp>
#include <ics/common/universal_types.h>
#include <ics/encoder/pointing_model.hpp>
#include <ics/tracker/demand.h>
#include <ics/tracker/estimator.h>

namespace
{

constexpr ics::float64 nanoseconds_per_second = 1.0E9;

}  // namespace

namespace ics::tracker
{

Axis_rates demand(const Angle_state& target,
                  const Angle_state& mount,
                  const float64 gain)
{
  const float64 used = check(gain >= 0.0) ? gain : 0.0;
  const float64 lead = static_cast<float64>((target.time - mount.time).count()) / nanoseconds_per_second;
  const float64 mount_azimuth = mount.azimuth_rad + (mount.azimuth_rate_rad_s * lead);
  const float64 mount_elevation = mount.elevation_rad + (mount.elevation_rate_rad_s * lead);
  const float64 azimuth_error = encoder::wrap_pi(target.azimuth_rad - mount_azimuth);
  const float64 elevation_error = target.elevation_rad - mount_elevation;
  return Axis_rates{target.azimuth_rate_rad_s + (used * azimuth_error),
                    target.elevation_rate_rad_s + (used * elevation_error)};
}

}  // namespace ics::tracker
