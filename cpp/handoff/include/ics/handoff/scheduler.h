// scheduler.h: the hand-off scheduler for one station (ICS-039, CPP-20). A
// run plan lists engagements, each a target and the interceptor sent at it.
// The scheduler filters every vehicle's reports as the cue does (ICS-037),
// predicts each engagement's closest approach, and keeps the engagements in
// the order they will happen. It gives its tracker (ICS-038) the cue of the
// engagement the station serves. When the station turns to another, it has
// the tracker drop its track and slews the mount to the new target. When no
// engagement is left, the tracker keeps the track it has.
//
// The scheduler stands where the tracker stood: the camera's thread gives it
// each frame, and the rate loop (ICS-033) takes its demands from it. While
// its tracker has no target, cueing or acquiring, the demand is the slew's:
// the cue's rate plus the rate that closes the error as fast as the mount
// can stop in. Once the tracker is tracking, or coasting, the demand is the
// tracker's own.
//
// Every station hears the same reports and reads the same run plan, so each
// station's scheduler comes to the same order by itself.
#ifndef ICS_HANDOFF_SCHEDULER_H
#define ICS_HANDOFF_SCHEDULER_H

// JSF deviation D-6: ics::Result, ics::UtcTime, ics::encoder, ics::frames and ics::mount come from .hpp headers.
#include <array>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/cue/extrapolator.h>
#include <ics/cue/filter.h>
#include <ics/cue/pointing.h>
#include <ics/encoder/pointing_model.hpp>
#include <ics/frames/enu.hpp>
#include <ics/handoff/approach.h>
#include <ics/handoff/queue.h>
#include <ics/handoff/slew.h>
#include <ics/mount/rate_loop.hpp>
#include <ics/tracker/camera.h>
#include <ics/tracker/estimator.h>
#include <ics/tracker/tracker.h>
#include <ics/v1/pli.pb.h>

namespace ics::handoff
{

// One engagement of the run plan: its vehicles, as PliRecord.entity_id names them.
struct Pairing
{
  std::string target;
  std::string interceptor;
};

struct Handoff_settings
{
  // The range, the station, the command latency, and the filter and the
  // sigmas each vehicle's reports get (ICS-037). The target role is not
  // used: the run plan names the vehicles.
  cue::Cue_settings cue;
  // The run plan's engagements, in its order, and how many there are.
  std::array<Pairing, engagements_length> pairings;
  int32 count;
  Approach_settings approach;
  Queue_settings queue;
  Slew_limits slew;
};

// Whether a scheduler can be made with the settings: 1 to max_engagements
// engagements, each of two named vehicles that differ, and approach, queue
// and slew settings that are valid(). kInvalidArgument otherwise.
[[nodiscard]] Status valid(const Handoff_settings& settings);

// What became of a report.
enum class Take : uint8
{
  // Given to the filter of each vehicle it reports.
  given,
  // Of a vehicle the run plan does not name.
  unlisted,
  // Of a vehicle in the plan, but not a position the filter can take.
  unusable
};

struct Handoff_counts
{
  int64 given;
  int64 unlisted;
  int64 unusable;
  // How many times the station has turned to another engagement.
  int64 handoffs;
};

struct Handoff_status
{
  // The queue as of the last frame.
  Plan plan;
  // Whether the demand is the slew's, not the tracker's.
  bool slewing;
  Handoff_counts counts;
};

class Scheduler final : public tracker::Cue_source, public mount::RateSource
{
public:
  // A scheduler and its tracker, for settings that are valid() and sites on
  // the Earth; kInvalidArgument or kOutOfRange otherwise. The model, the
  // axes and the time must outlive it.
  [[nodiscard]] static Result<std::unique_ptr<Scheduler>> make(const Handoff_settings& settings,
                                                               const tracker::Tracker_settings& tracking,
                                                               const encoder::PointingModel& model,
                                                               tracker::Axis_source& axes,
                                                               tracker::Time_source& time);

  // Takes a report of any vehicle, from the thread that reads them.
  [[nodiscard]] Take take(const v1::PliRecord& record);
  // Takes a frame from the camera's thread: brings the queue up to now,
  // gives the frame to the tracker, and makes the slew's demand.
  [[nodiscard]] tracker::Take take(const tracker::Frame& frame);
  // The cue of the engagement served, for the tracker; kEmpty with none.
  [[nodiscard]] Result<tracker::Angle_state> cue_at(const UtcTime when) override;
  // The latest demand, for the rate loop's thread.
  [[nodiscard]] std::optional<mount::RateDemand> latest() override;
  [[nodiscard]] Handoff_status status() const;
  // The tracker's mode and counts.
  [[nodiscard]] tracker::Mode mode() const;
  [[nodiscard]] tracker::Tracker_counts tracker_counts() const;

private:
  Scheduler(const Handoff_settings& settings,
            const frames::EnuFrame& range,
            const cue::Station& station,
            const encoder::PointingModel& model,
            tracker::Axis_source& axes,
            tracker::Time_source& time);

  // The vehicle's name, and its filter: an engagement's target, then its interceptor.
  [[nodiscard]] const std::string& name_of(const int32 vehicle) const;
  [[nodiscard]] const cue::Cue_filter& filter_of(const int32 vehicle) const;
  // What the station knows of an engagement at a time.
  [[nodiscard]] Sighting sighting_of(const int32 engagement, const UtcTime now) const;
  // Brings the queue up to now; true when the station turns to another engagement.
  [[nodiscard]] bool advance(const UtcTime now);
  // Where the engagement served is at a time.
  [[nodiscard]] Result<tracker::Angle_state> served_at(const UtcTime when) const;
  // Makes the slew's demand for a command sent now, from the mount's axes at
  // seen, while the tracker is searching; leaves the tracker's otherwise.
  void steer(const UtcTime now,
             const UtcTime seen,
             const bool searching);
  // The slew's demand, while there is one.
  [[nodiscard]] std::optional<mount::RateDemand> slew() const;

  Handoff_settings settings_;
  frames::EnuFrame range_;
  cue::Station station_;
  const encoder::PointingModel& model_;
  tracker::Axis_source& axes_;
  tracker::Time_source& time_;
  std::vector<cue::Cue_filter> filters_;
  Queue queue_;
  Plan plan_;
  UtcTime advanced_{};
  std::optional<mount::RateDemand> slew_;
  Handoff_counts counts_{};
  std::unique_ptr<tracker::Tracker> tracker_;
  mutable std::mutex mutex_;
};

}  // namespace ics::handoff

#endif  // ICS_HANDOFF_SCHEDULER_H
