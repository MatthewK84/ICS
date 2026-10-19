// tracker.h: the closed-loop tracker for one station (ICS-038, CPP-19). It
// takes each frame of the tracking camera, finds the target where it should
// be, tracks its sky direction, and gives the rate loop (ICS-033) the demand
// that follows it: the tracker is a mount::RateSource.
//
// Its modes:
// - cueing: no track. Each frame is searched in the acquisition window
//   around where the cue (ICS-037) puts the target, or around the boresight
//   with no cue. The demand follows the cue; with no cue there is none, and
//   the rate loop stops the mount once the last demand is stale.
// - acquiring: a target was found, and the track started from it with the
//   cue's rates. Each frame is searched in the track window around the
//   track's prediction; confirm_frames detections in a row within gate_px of
//   the prediction make it tracking, and a miss returns to cueing.
// - tracking: the demand follows the track. A miss starts coasting.
// - coasting: the track is carried on at its rates. A detection within the
//   gate resumes tracking; coast_limit without one returns to cueing.
//
// The demand is made each frame, for the moment a command sent now takes
// effect: now plus command_latency.
#ifndef ICS_TRACKER_TRACKER_H
#define ICS_TRACKER_TRACKER_H

// JSF deviation D-6: ics::Result, ics::Duration, ics::UtcTime, ics::encoder and ics::mount come from .hpp headers.
#include <memory>
#include <mutex>
#include <optional>

#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/encoder/pointing_model.hpp>
#include <ics/encoder/track.hpp>
#include <ics/mount/rate_loop.hpp>
#include <ics/tracker/camera.h>
#include <ics/tracker/centroid.h>
#include <ics/tracker/estimator.h>
#include <ics/tracker/geometry.h>

namespace ics::tracker
{

// Where the mount's axes pointed at a time: ics-mountd answers from the
// encoder track (ICS-036) its receiver fills.
class Axis_source
{
public:
  Axis_source() = default;
  Axis_source(const Axis_source&) = delete;
  Axis_source& operator=(const Axis_source&) = delete;
  Axis_source(Axis_source&&) = delete;
  Axis_source& operator=(Axis_source&&) = delete;
  virtual ~Axis_source() = default;

  // The axis angles and rates at when, as encoder::AngleTrack::at gives them.
  [[nodiscard]] virtual Result<encoder::Interpolated> axes_at(const UtcTime when) = 0;
};

// Where the cue puts the target at a time: its sky direction and rates.
// ics-mountd answers from its Cue_extrapolator (ICS-037).
class Cue_source
{
public:
  Cue_source() = default;
  Cue_source(const Cue_source&) = delete;
  Cue_source& operator=(const Cue_source&) = delete;
  Cue_source(Cue_source&&) = delete;
  Cue_source& operator=(Cue_source&&) = delete;
  virtual ~Cue_source() = default;

  // The cue at when; an error when there is none.
  [[nodiscard]] virtual Result<Angle_state> cue_at(const UtcTime when) = 0;
};

// UTC now.
class Time_source
{
public:
  Time_source() = default;
  Time_source(const Time_source&) = delete;
  Time_source& operator=(const Time_source&) = delete;
  Time_source(Time_source&&) = delete;
  Time_source& operator=(Time_source&&) = delete;
  virtual ~Time_source() = default;

  [[nodiscard]] virtual UtcTime now() = 0;
};

// The host's clock, which ics-timingd keeps on UTC.
class System_time final : public Time_source
{
public:
  [[nodiscard]] UtcTime now() override;
};

// How the tracker finds, follows and steers.
struct Tracker_settings
{
  Camera_geometry camera;
  Centroid_settings centroid;
  Estimator_settings estimator;
  // The windows searched while cueing, and while there is a track.
  Extent acquire_window;
  Extent track_window;
  // How far a detection may be from the track's prediction, in pixels.
  float64 gate_px;
  // Detections in a row that confirm a track.
  int32 confirm_frames;
  // How long a track is carried on without a detection.
  Duration coast_limit;
  // From sending a mount command to its taking effect.
  Duration command_latency;
  // How fast the demand closes the pointing error, in reciprocal seconds.
  float64 gain;
};

// The settings a tracker runs with: kInvalidArgument otherwise.
[[nodiscard]] Status valid(const Tracker_settings& settings);

enum class Mode : uint8
{
  cueing,
  acquiring,
  tracking,
  coasting
};

// What a frame did.
enum class Take : uint8
{
  // The target was found.
  detected,
  // It was not.
  missed,
  // The frame could not be searched: the mount's axes at its time are not
  // known, or it is no later than the last detection.
  unusable
};

// What the tracker has done since it was made.
struct Tracker_counts
{
  int64 frames;
  int64 detections;
  int64 misses;
  int64 unusable;
  // Tracks confirmed, and tracks lost after coasting.
  int64 acquisitions;
  int64 losses;
};

class Tracker final : public mount::RateSource
{
public:
  // A tracker for valid() settings, kInvalidArgument otherwise. The model,
  // axes, cue and time must outlive it.
  [[nodiscard]] static Result<std::unique_ptr<Tracker>> make(const Tracker_settings& settings,
                                                             const encoder::PointingModel& model,
                                                             Axis_source& axes,
                                                             Cue_source& cue,
                                                             Time_source& time);

  // Takes a frame from the camera's thread.
  [[nodiscard]] Take take(const Frame& frame);
  // The latest demand, for the rate loop's thread.
  [[nodiscard]] std::optional<mount::RateDemand> latest() override;
  [[nodiscard]] Mode mode() const;
  [[nodiscard]] Tracker_counts counts() const;

private:
  Tracker(const Tracker_settings& settings,
          const encoder::PointingModel& model,
          Axis_source& axes,
          Cue_source& cue,
          Time_source& time);

  // Where the target should be at a time: the cue while cueing, the track after.
  [[nodiscard]] Result<Angle_state> expected_at(const UtcTime time);
  // Searches the frame, and moves the mode on.
  [[nodiscard]] Take look(const Frame& frame, const encoder::Direction& sight);
  [[nodiscard]] Take found(const UtcTime time,
                           const encoder::Direction& measured,
                           const Result<Angle_state>& cue);
  void missed(const UtcTime time);
  // Makes the demand for a command sent now.
  void steer(const UtcTime now, const Angle_state& mount);

  Tracker_settings settings_;
  const encoder::PointingModel& model_;
  Axis_source& axes_;
  Cue_source& cue_;
  Time_source& time_;
  Centroider centroider_;
  Angle_estimator estimator_;
  Mode mode_ = Mode::cueing;
  int32 confirmations_ = 0;
  UtcTime coast_start_{};
  Tracker_counts counts_{};
  std::optional<mount::RateDemand> demand_;
  mutable std::mutex mutex_;
};

}  // namespace ics::tracker

#endif  // ICS_TRACKER_TRACKER_H
