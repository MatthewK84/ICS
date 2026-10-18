// filter.h: the cue's Kalman filter for one target (ICS-037), as
// python/ics_models/cue/filter.py gives it. It keeps the reports of the last
// few seconds in time order, each with the estimate after it: its checkpoint.
// A report older than the newest is put in its place and every report from
// there on is applied again from the checkpoint before it, so the result is the
// same as if the reports had come in order. A report whose position is too far
// from the prediction, by the chi-squared distance with three degrees of
// freedom, is gated: held, but not applied. Everything is held in fixed arrays,
// so the filter allocates nothing.
#ifndef ICS_CUE_FILTER_H
#define ICS_CUE_FILTER_H

// JSF deviation D-6: ics::Result, ics::Duration and ics::UtcTime come from ics::common's .hpp headers.
#include <array>
#include <cstddef>

#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/cue/kinematics.h>
#include <ics/cue/measurement.h>

namespace ics::cue
{

// The most reports a filter holds.
constexpr int32 max_history = 64;

// What the filter did with a report.
enum class Outcome
{
  taken,
  late,
  gated,
  too_old
};

// How the filter weighs, gates and keeps reports.
struct Filter_settings
{
  // The white-jerk process noise.
  Process_noise noise;
  // The squared distance past which a report is gated: 16.27 for 99.9% with three degrees of freedom.
  float64 gate;
  // How far behind the newest report a report may be and still be applied.
  Duration history;
  // How far past the last report an estimate may be predicted.
  Duration coast_limit;
  // The most reports held, from 1 to max_history.
  int32 capacity;
  // The velocity sigma of a first report without a velocity, in m/s.
  float64 initial_velocity_sigma_m_s;
  // The acceleration sigma of a first report, in m/s^2.
  float64 initial_acceleration_sigma_m_s2;
};

// The filter for one target.
class Cue_filter
{
public:
  // An empty filter. A capacity outside 1 to max_history fails a check and is clamped into it.
  explicit Cue_filter(const Filter_settings& settings);

  // Applies a report in its time order, and says what became of it.
  [[nodiscard]] Outcome add(const Measurement& measurement);

  // The estimate predicted to time from the checkpoint of the latest report at
  // or before it. Fails with kOutOfRange for a time before every report held,
  // and kUnconstrained for one past the coast limit after that report.
  [[nodiscard]] Result<Estimate> estimate_at(const UtcTime time) const;

  // The time of the newest report held. Fails with kEmpty before the first.
  [[nodiscard]] Result<UtcTime> latest() const;

private:
  // A report held: its arrival order, whether it was applied, and the estimate after it.
  struct Entry
  {
    // The report.
    Measurement measurement;
    // Its place in arrival order, which orders reports of the same time.
    int64 sequence;
    // Whether it passed the gate.
    bool applied;
    // The estimate after it; the one before it when gated.
    Estimate checkpoint;
  };

  // Applies every report from start on again, from the checkpoint before it.
  void refilter(const int32 start);
  // Drops the reports past the history's length or the capacity.
  void evict();
  // The entry at position.
  [[nodiscard]] Entry& entry(const int32 position);
  [[nodiscard]] const Entry& entry(const int32 position) const;

  // The settings, with the capacity clamped.
  Filter_settings settings_;
  // The reports held in time order, with one spare slot for the report being added.
  std::array<Entry, static_cast<std::size_t>(max_history + 1)> entries_;
  // How many entries_ holds.
  int32 count_;
  // Whether a report has been dropped, so base_ holds the checkpoint of the last one dropped.
  bool has_base_;
  // The checkpoint of the last report dropped.
  Estimate base_;
  // The arrival number of the next report.
  int64 sequence_;
};

}  // namespace ics::cue

#endif  // ICS_CUE_FILTER_H
