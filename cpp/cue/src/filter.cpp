// filter.cpp: the cue's Kalman filter, its gate and its re-filtering of late reports (ICS-037).
// JSF deviation D-6: ics::check, ics::fail, ics::Duration and ics::UtcTime come from ics::common's .hpp headers.
#include <array>
#include <cstddef>

#include <ics/common/check.hpp>
#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/cue/filter.h>
#include <ics/cue/kinematics.h>
#include <ics/cue/measurement.h>

namespace
{

using ics::float64;
using ics::int32;
using ics::cue::Estimate;
using ics::cue::Matrix;
using ics::cue::Measurement;

// A report's checkpoint, and whether it passed the gate.
struct Step
{
  // Whether the report was applied.
  bool applied;
  // The estimate after it, or the prior when it was gated.
  Estimate checkpoint;
};

// How many state values a report measures: position, and velocity when it has one.
int32 measured(const Measurement& report)
{
  return report.has_velocity ? (2 * ics::cue::axes) : ics::cue::axes;
}

// A report's measured value at a state index below measured().
float64 observed(const Measurement& report, const int32 position)
{
  const int32 axis = position % ics::cue::axes;
  const ics::cue::Vector3& values = (position < ics::cue::axes) ? report.position : report.velocity;
  return values[static_cast<std::size_t>(axis)];
}

// The covariance of a report's measured values, in their leading block.
Matrix noise_of(const Measurement& report)
{
  const float64 horizontal = report.horizontal_sigma_m * report.horizontal_sigma_m;
  const float64 velocity = report.velocity_sigma_m_s * report.velocity_sigma_m_s;
  Matrix noise;
  noise.put(0, 0, horizontal);
  noise.put(1, 1, horizontal);
  noise.put(2, 2, report.vertical_sigma_m * report.vertical_sigma_m);
  for (int32 position = ics::cue::axes; position < measured(report); ++position)
  {
    noise.put(position, position, velocity);
  }
  return noise;
}

// The estimate a first report gives: its position and velocity, and no acceleration.
Estimate initial_estimate(const Measurement& report, const ics::cue::Filter_settings& settings)
{
  Estimate start{report.time, {}, Matrix{}};
  const float64 velocity_sigma = report.has_velocity ? report.velocity_sigma_m_s : settings.initial_velocity_sigma_m_s;
  const float64 acceleration_sigma = settings.initial_acceleration_sigma_m_s2;
  const Matrix noise = noise_of(report);
  for (int32 axis = 0; axis < ics::cue::axes; ++axis)
  {
    const int32 velocity = ics::cue::index(1, axis);
    const int32 acceleration = ics::cue::index(2, axis);
    ics::cue::set_component(start.state, axis, observed(report, axis));
    ics::cue::set_component(start.state, velocity, report.has_velocity ? observed(report, velocity) : 0.0);
    start.covariance.put(axis, axis, noise.at(axis, axis));
    start.covariance.put(velocity, velocity, velocity_sigma * velocity_sigma);
    start.covariance.put(acceleration, acceleration, acceleration_sigma * acceleration_sigma);
  }
  return start;
}

// The innovation covariance: the prediction's covariance plus the report's, in the measured block.
Matrix spread_of(const Matrix& covariance,
                 const Matrix& noise,
                 const int32 size)
{
  Matrix spread;
  for (int32 row = 0; row < size; ++row)
  {
    for (int32 column = 0; column < size; ++column)
    {
      spread.put(row, column, covariance.at(row, column) + noise.at(row, column));
    }
  }
  return spread;
}

// The squared Mahalanobis distance of a report's position from the prediction.
float64 gate_distance(const Estimate& predicted,
                      const Measurement& report,
                      const Matrix& position_inverse)
{
  float64 distance = 0.0;
  for (int32 row = 0; row < ics::cue::axes; ++row)
  {
    const float64 left = observed(report, row) - ics::cue::component(predicted.state, row);
    for (int32 column = 0; column < ics::cue::axes; ++column)
    {
      const float64 right = observed(report, column) - ics::cue::component(predicted.state, column);
      distance += left * position_inverse.at(row, column) * right;
    }
  }
  return distance;
}

// The Kalman gain: the prediction's covariance against the measured values, times the spread's inverse.
Matrix gain_of(const Matrix& covariance,
               const Matrix& spread_inverse,
               const int32 size)
{
  Matrix gain;
  for (int32 row = 0; row < ics::cue::state_size; ++row)
  {
    for (int32 column = 0; column < size; ++column)
    {
      float64 sum = 0.0;
      for (int32 inner = 0; inner < size; ++inner)
      {
        sum += covariance.at(row, inner) * spread_inverse.at(inner, column);
      }
      gain.put(row, column, sum);
    }
  }
  return gain;
}

// The estimate after applying a report to the prediction at its time, with the covariance in Joseph form.
Estimate update(const Estimate& predicted,
                const Measurement& report,
                const Matrix& noise,
                const Matrix& spread_inverse)
{
  const int32 size = measured(report);
  const Matrix gain = gain_of(predicted.covariance, spread_inverse, size);
  Estimate updated = predicted;
  Matrix keep = Matrix::identity();
  for (int32 row = 0; row < ics::cue::state_size; ++row)
  {
    float64 correction = 0.0;
    for (int32 column = 0; column < size; ++column)
    {
      correction += gain.at(row, column) * (observed(report, column) - ics::cue::component(predicted.state, column));
      keep.put(row, column, keep.at(row, column) - gain.at(row, column));
    }
    ics::cue::set_component(updated.state, row, ics::cue::component(predicted.state, row) + correction);
  }
  const Matrix kept = ics::cue::multiply_transposed(ics::cue::multiply(keep, predicted.covariance), keep);
  const Matrix added = ics::cue::multiply_transposed(ics::cue::multiply(gain, noise), gain);
  const Matrix sum = ics::cue::add(kept, added);
  for (int32 row = 0; row < ics::cue::state_size; ++row)
  {
    for (int32 column = 0; column < ics::cue::state_size; ++column)
    {
      updated.covariance.put(row, column, (sum.at(row, column) + sum.at(column, row)) / 2.0);
    }
  }
  return updated;
}

// Whether a report passes the gate from a prior, and the checkpoint after it.
// A report whose spread cannot be inverted, such as one with a NaN sigma, is gated.
Step step(const Estimate& prior,
          const Measurement& report,
          const ics::cue::Filter_settings& settings)
{
  Step result{false, prior};
  const Estimate predicted = ics::cue::predict(prior, report.time, settings.noise);
  const Matrix noise = noise_of(report);
  const Matrix spread = spread_of(predicted.covariance, noise, measured(report));
  const ics::Result<Matrix> position_inverse = ics::cue::inverse_symmetric(spread, ics::cue::axes);
  const ics::Result<Matrix> spread_inverse = ics::cue::inverse_symmetric(spread, measured(report));
  if (position_inverse.has_value() && spread_inverse.has_value())
  {
    if (!(gate_distance(predicted, report, *position_inverse) > settings.gate))
    {
      result = Step{true, update(predicted, report, noise, *spread_inverse)};
    }
  }
  return result;
}

}  // namespace

namespace ics::cue
{

Cue_filter::Cue_filter(const Filter_settings& settings)
  : settings_(settings), entries_(), count_(0), has_base_(false), base_(), sequence_(0)
{
  if (!check((settings.capacity >= 1) && (settings.capacity <= max_history)))
  {
    settings_.capacity = (settings.capacity < 1) ? 1 : max_history;
  }
}

Outcome Cue_filter::add(const Measurement& measurement)
{
  Outcome outcome = Outcome::too_old;
  const UtcTime newest = (count_ > 0) ? entry(count_ - 1).measurement.time : measurement.time;
  const bool before_history = measurement.time < (newest - settings_.history);
  const bool before_base = has_base_ && (measurement.time < base_.time);
  if ((!before_history) && (!before_base))
  {
    int32 position = count_;
    while ((position > 0) && (entry(position - 1).measurement.time > measurement.time))
    {
      entry(position) = entry(position - 1);
      --position;
    }
    entry(position) = Entry{measurement, sequence_, true, Estimate{}};
    ++sequence_;
    ++count_;
    refilter(position);
    const bool applied = entry(position).applied;
    const bool last = position == (count_ - 1);
    evict();
    if (!applied)
    {
      outcome = Outcome::gated;
    }
    else if (last)
    {
      outcome = Outcome::taken;
    }
    else
    {
      outcome = Outcome::late;
    }
  }
  return outcome;
}

Result<Estimate> Cue_filter::estimate_at(const UtcTime time) const
{
  int32 position = count_;
  while ((position > 0) && (entry(position - 1).measurement.time > time))
  {
    --position;
  }
  Result<Estimate> estimate = fail(Error::kOutOfRange);
  bool found = true;
  Estimate checkpoint = base_;
  if (position > 0)
  {
    checkpoint = entry(position - 1).checkpoint;
  }
  else
  {
    found = has_base_ && (base_.time <= time);
  }
  if (found)
  {
    if ((time - checkpoint.time) > settings_.coast_limit)
    {
      estimate = fail(Error::kUnconstrained);
    }
    else
    {
      estimate = predict(checkpoint, time, settings_.noise);
    }
  }
  return estimate;
}

Result<UtcTime> Cue_filter::latest() const
{
  Result<UtcTime> newest = fail(Error::kEmpty);
  if (count_ > 0)
  {
    newest = entry(count_ - 1).measurement.time;
  }
  return newest;
}

void Cue_filter::refilter(const int32 start)
{
  for (int32 position = start; position < count_; ++position)
  {
    Entry& held = entry(position);
    if (position > 0)
    {
      const Step result = step(entry(position - 1).checkpoint, held.measurement, settings_);
      held.applied = result.applied;
      held.checkpoint = result.checkpoint;
    }
    else if (has_base_)
    {
      const Step result = step(base_, held.measurement, settings_);
      held.applied = result.applied;
      held.checkpoint = result.checkpoint;
    }
    else
    {
      held.applied = true;
      held.checkpoint = initial_estimate(held.measurement, settings_);
    }
  }
}

void Cue_filter::evict()
{
  const UtcTime newest = entry(count_ - 1).measurement.time;
  int32 drop = 0;
  while (((count_ - drop) > 1) &&
         (((count_ - drop) > settings_.capacity) || (entry(drop).measurement.time < (newest - settings_.history))))
  {
    ++drop;
  }
  if (drop > 0)
  {
    base_ = entry(drop - 1).checkpoint;
    has_base_ = true;
    for (int32 position = drop; position < count_; ++position)
    {
      entry(position - drop) = entry(position);
    }
    count_ -= drop;
  }
}

Cue_filter::Entry& Cue_filter::entry(const int32 position)
{
  return entries_[static_cast<std::size_t>(position)];
}

const Cue_filter::Entry& Cue_filter::entry(const int32 position) const
{
  return entries_[static_cast<std::size_t>(position)];
}

}  // namespace ics::cue
