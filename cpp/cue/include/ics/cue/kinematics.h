// kinematics.h: the cue's constant-acceleration state and its prediction with
// white-jerk process noise (ICS-037), as python/ics_models/cue/kinematics.py
// gives them. The state is nine numbers in the range ENU frame: position east,
// north and up (m), then velocity (m/s), then acceleration (m/s^2). Each axis
// moves on its own, its acceleration driven by white jerk. The discrete model
// is exact, so predicting over dt1 then dt2 equals predicting over dt1 + dt2.
#ifndef ICS_CUE_KINEMATICS_H
#define ICS_CUE_KINEMATICS_H

// JSF deviation D-6: ics::Result and ics::UtcTime come from ics::common's .hpp headers.
#include <array>
#include <cstddef>

#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>

namespace ics::cue
{

// The state's axes (east, north, up) and orders (position, velocity, acceleration).
constexpr int32 axes = 3;
constexpr int32 orders = 3;
constexpr int32 state_size = axes * orders;
// The numbers in a state, and in a matrix of the state's size.
constexpr std::size_t state_length = static_cast<std::size_t>(state_size);
constexpr std::size_t matrix_length = static_cast<std::size_t>(state_size * state_size);

constexpr std::size_t axes_length = static_cast<std::size_t>(axes);

// A state: position, velocity and acceleration, each east, north and up.
using State = std::array<float64, state_length>;

// One order of the state, or any vector east, north and up.
using Vector3 = std::array<float64, axes_length>;

// A square matrix of the state's size, such as a covariance.
class Matrix
{
public:
  // The value at row and column; 0, after a failed check, outside the matrix.
  [[nodiscard]] float64 at(const int32 row, const int32 column) const;
  // Sets the value at row and column; does nothing, after a failed check, outside the matrix.
  void put(const int32 row,
           const int32 column,
           const float64 value);
  // The identity matrix.
  [[nodiscard]] static Matrix identity();

private:
  // The values, row by row.
  std::array<float64, matrix_length> values_{};
};

// The state and its covariance at a UTC time.
struct Estimate
{
  // When the estimate holds.
  UtcTime time;
  // The state at that time.
  State state;
  // Its covariance.
  Matrix covariance;
};

// The white-jerk power spectral density of each axis, in m^2/s^5.
struct Process_noise
{
  // East and north.
  float64 horizontal;
  // Up.
  float64 vertical;
};

// The state index of an order (0 position, 1 velocity, 2 acceleration) on an axis.
[[nodiscard]] constexpr int32 index(const int32 order, const int32 axis)
{
  return (order * axes) + axis;
}

// later - earlier, in seconds.
[[nodiscard]] float64 seconds_between(const UtcTime earlier, const UtcTime later);

// The state transition over dt_s seconds.
[[nodiscard]] Matrix transition(const float64 dt_s);

// The process noise added over dt_s seconds.
[[nodiscard]] Matrix process_noise(const float64 dt_s, const Process_noise& noise);

// The estimate predicted to time, which must not be before the estimate's:
// for an earlier time, a check fails and the estimate comes back unchanged.
[[nodiscard]] Estimate predict(const Estimate& estimate,
                               const UtcTime time,
                               const Process_noise& noise);

// left times right.
[[nodiscard]] Matrix multiply(const Matrix& left, const Matrix& right);

// left times the transpose of right.
[[nodiscard]] Matrix multiply_transposed(const Matrix& left, const Matrix& right);

// left plus right.
[[nodiscard]] Matrix add(const Matrix& left, const Matrix& right);

// The inverse of the leading size-by-size block of a symmetric positive-definite
// matrix, by Cholesky factorization, in that block of the result. Fails with
// kUnconstrained when the block is not positive definite, kInvalidArgument for
// a size outside 1 to state_size.
[[nodiscard]] Result<Matrix> inverse_symmetric(const Matrix& matrix, const int32 size);

// matrix times vector.
[[nodiscard]] State apply(const Matrix& matrix, const State& vector);

// The value of a state at index; 0, after a failed check, outside it.
[[nodiscard]] float64 component(const State& vector, const int32 position);

// Sets the value of a state at index; does nothing, after a failed check, outside it.
void set_component(State& vector,
                   const int32 position,
                   const float64 value);

}  // namespace ics::cue

#endif  // ICS_CUE_KINEMATICS_H
