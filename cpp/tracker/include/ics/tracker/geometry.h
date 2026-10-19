// geometry.h: a pixel's direction on the sky, and a direction's pixel
// (ICS-038), as python/ics_models/tracker/geometry.py gives them. The camera
// rides the mount's elevation axis, so its image keeps one orientation to the
// mount's axes. Its boresight is the pixel the mount's line of sight falls
// on. A pixel's offset from it, with y turned to point up, mirrored when the
// optics mirror the image, rotated by the roll and scaled by the plate scale,
// is a point on the plane tangent to the sky at the line of sight: xi toward
// increasing azimuth, eta toward increasing elevation. The gnomonic
// projection takes that point to a direction, and back.
#ifndef ICS_TRACKER_GEOMETRY_H
#define ICS_TRACKER_GEOMETRY_H

// JSF deviation D-6: ics::Result and ics::encoder::Direction come from .hpp headers.
#include <ics/common/error.hpp>
#include <ics/common/universal_types.h>
#include <ics/encoder/pointing_model.hpp>
#include <ics/tracker/centroid.h>

namespace ics::tracker
{

// The camera on the mount.
struct Camera_geometry
{
  // The pixel the mount's line of sight falls on.
  float64 boresight_x;
  float64 boresight_y;
  // Radians a pixel, at the boresight.
  float64 plate_scale_rad;
  // The angle from the image's right to increasing azimuth, counterclockwise.
  float64 roll_rad;
  // Whether the optics mirror the image.
  bool mirrored;
};

// A point on the plane tangent to the sky at the line of sight.
struct Tangent
{
  float64 xi;
  float64 eta;
};

// An azimuth in [0, 2 pi). A tiny negative angle plus 2 pi rounds to 2 pi
// itself, which is due north: 0.
[[nodiscard]] float64 wrap_azimuth(const float64 angle);

[[nodiscard]] Tangent tangent_of_pixel(const Pixel_point pixel, const Camera_geometry& camera);
[[nodiscard]] Pixel_point pixel_of_tangent(const Tangent tangent, const Camera_geometry& camera);

// The direction of a point on the plane tangent to the sky at the line of sight.
[[nodiscard]] encoder::Direction sky_of_tangent(const Tangent tangent, const encoder::Direction& line_of_sight);

// A direction's point on the plane tangent to the sky at the line of sight.
// kOutOfRange for a direction 90 degrees or more from it.
[[nodiscard]] Result<Tangent> tangent_of_sky(const encoder::Direction& direction,
                                             const encoder::Direction& line_of_sight);

// The direction a pixel looks in, with the mount's line of sight at the frame's time.
[[nodiscard]] encoder::Direction sky_of_pixel(const Pixel_point pixel,
                                              const encoder::Direction& line_of_sight,
                                              const Camera_geometry& camera);

// The pixel a direction falls on, as tangent_of_sky fails.
[[nodiscard]] Result<Pixel_point> pixel_of_sky(const encoder::Direction& direction,
                                               const encoder::Direction& line_of_sight,
                                               const Camera_geometry& camera);

}  // namespace ics::tracker

#endif  // ICS_TRACKER_GEOMETRY_H
