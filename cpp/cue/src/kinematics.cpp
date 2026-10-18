// kinematics.cpp: the constant-acceleration model and its matrix arithmetic (ICS-037).
// JSF deviation D-6: ics::check, ics::fail and ics::UtcTime come from ics::common's .hpp headers.
#include <array>
#include <cmath>
#include <cstddef>

#include <ics/common/check.hpp>
#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/cue/kinematics.h>

namespace
{

// The divisors of the white-jerk noise terms: dt^5/20, dt^4/8, dt^3/6, dt^3/3 and dt^2/2.
constexpr ics::float64 dt5_divisor = 20.0;
constexpr ics::float64 dt4_divisor = 8.0;
constexpr ics::float64 dt3_divisor = 6.0;
constexpr ics::float64 dt3_velocity_divisor = 3.0;
constexpr ics::float64 dt2_divisor = 2.0;
constexpr ics::float64 nanoseconds_per_second = 1.0E9;
constexpr std::size_t orders_length = static_cast<std::size_t>(ics::cue::orders);

// One axis's noise terms, by order: position, velocity, acceleration.
using Noise_block = std::array<std::array<ics::float64, orders_length>, orders_length>;

// The position of row and column in a matrix's values.
std::size_t cell(const ics::int32 row, const ics::int32 column)
{
  return static_cast<std::size_t>((row * ics::cue::state_size) + column);
}

// Whether an index lies in the state.
bool inside(const ics::int32 position)
{
  return (position >= 0) && (position < ics::cue::state_size);
}

// The white-jerk noise of one axis over dt_s, per unit density, by order.
Noise_block noise_block(const ics::float64 dt_s)
{
  const ics::float64 dt2 = dt_s * dt_s;
  const ics::float64 dt3 = dt2 * dt_s;
  const ics::float64 corner = (dt3 * dt2) / dt5_divisor;
  const ics::float64 next = (dt2 * dt2) / dt4_divisor;
  const ics::float64 third = dt3 / dt3_divisor;
  const ics::float64 middle = dt3 / dt3_velocity_divisor;
  const ics::float64 half = dt2 / dt2_divisor;
  return {{{corner, next, third}, {next, middle, half}, {third, half, dt_s}}};
}

// The lower Cholesky factor of the leading size block of matrix, or false
// when that block is not positive definite.
bool factor(const ics::cue::Matrix& matrix,
            const ics::int32 size,
            ics::cue::Matrix& lower)
{
  bool positive = true;
  for (ics::int32 column = 0; column < size; ++column)
  {
    ics::float64 pivot = matrix.at(column, column);
    for (ics::int32 inner = 0; inner < column; ++inner)
    {
      pivot -= lower.at(column, inner) * lower.at(column, inner);
    }
    positive = positive && std::isfinite(pivot) && (pivot > 0.0);
    const ics::float64 root = positive ? std::sqrt(pivot) : 1.0;
    lower.put(column, column, root);
    for (ics::int32 row = column + 1; row < size; ++row)
    {
      ics::float64 sum = matrix.at(row, column);
      for (ics::int32 inner = 0; inner < column; ++inner)
      {
        sum -= lower.at(row, inner) * lower.at(column, inner);
      }
      lower.put(row, column, sum / root);
    }
  }
  return positive;
}

// One column of the inverse from the Cholesky factor: solves L y = e, then L^T x = y.
void solve_column(const ics::cue::Matrix& lower,
                  const ics::int32 size,
                  const ics::int32 column,
                  ics::cue::Matrix& inverse)
{
  ics::cue::State forward{};
  for (ics::int32 row = 0; row < size; ++row)
  {
    ics::float64 sum = (row == column) ? 1.0 : 0.0;
    for (ics::int32 inner = 0; inner < row; ++inner)
    {
      sum -= lower.at(row, inner) * ics::cue::component(forward, inner);
    }
    ics::cue::set_component(forward, row, sum / lower.at(row, row));
  }
  for (ics::int32 row = size - 1; row >= 0; --row)
  {
    ics::float64 sum = ics::cue::component(forward, row);
    for (ics::int32 inner = row + 1; inner < size; ++inner)
    {
      sum -= lower.at(inner, row) * inverse.at(inner, column);
    }
    inverse.put(row, column, sum / lower.at(row, row));
  }
}

}  // namespace

namespace ics::cue
{

Result<Matrix> inverse_symmetric(const Matrix& matrix, const int32 size)
{
  Result<Matrix> inverse = fail(Error::kInvalidArgument);
  if ((size >= 1) && (size <= state_size))
  {
    Matrix lower;
    if (factor(matrix, size, lower))
    {
      Matrix solved;
      for (int32 column = 0; column < size; ++column)
      {
        solve_column(lower, size, column, solved);
      }
      inverse = solved;
    }
    else
    {
      inverse = fail(Error::kUnconstrained);
    }
  }
  return inverse;
}

float64 Matrix::at(const int32 row, const int32 column) const
{
  float64 value = 0.0;
  if (check(inside(row) && inside(column)))
  {
    value = values_[cell(row, column)];
  }
  return value;
}

void Matrix::put(const int32 row,
                 const int32 column,
                 const float64 value)
{
  if (check(inside(row) && inside(column)))
  {
    values_[cell(row, column)] = value;
  }
}

Matrix Matrix::identity()
{
  Matrix unit;
  for (int32 position = 0; position < state_size; ++position)
  {
    unit.put(position, position, 1.0);
  }
  return unit;
}

float64 seconds_between(const UtcTime earlier, const UtcTime later)
{
  const int64 nanoseconds = (later - earlier).count();
  return static_cast<float64>(nanoseconds) / nanoseconds_per_second;
}

Matrix transition(const float64 dt_s)
{
  Matrix move = Matrix::identity();
  for (int32 axis = 0; axis < axes; ++axis)
  {
    move.put(index(0, axis), index(1, axis), dt_s);
    move.put(index(0, axis), index(2, axis), (dt_s * dt_s) / dt2_divisor);
    move.put(index(1, axis), index(2, axis), dt_s);
  }
  return move;
}

Matrix process_noise(const float64 dt_s, const Process_noise& noise)
{
  const Noise_block block = noise_block(dt_s);
  Matrix spread;
  for (int32 axis = 0; axis < axes; ++axis)
  {
    const float64 density = (axis == (axes - 1)) ? noise.vertical : noise.horizontal;
    for (int32 row = 0; row < orders; ++row)
    {
      for (int32 column = 0; column < orders; ++column)
      {
        const float64 term = block[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)];
        spread.put(index(row, axis), index(column, axis), density * term);
      }
    }
  }
  return spread;
}

Estimate predict(const Estimate& estimate,
                 const UtcTime time,
                 const Process_noise& noise)
{
  Estimate predicted = estimate;
  if (check(time >= estimate.time))
  {
    const float64 dt_s = seconds_between(estimate.time, time);
    const Matrix move = transition(dt_s);
    const Matrix moved = multiply_transposed(multiply(move, estimate.covariance), move);
    predicted = Estimate{time, apply(move, estimate.state), add(moved, process_noise(dt_s, noise))};
  }
  return predicted;
}

Matrix multiply(const Matrix& left, const Matrix& right)
{
  Matrix product;
  for (int32 row = 0; row < state_size; ++row)
  {
    for (int32 column = 0; column < state_size; ++column)
    {
      float64 sum = 0.0;
      for (int32 inner = 0; inner < state_size; ++inner)
      {
        sum += left.at(row, inner) * right.at(inner, column);
      }
      product.put(row, column, sum);
    }
  }
  return product;
}

Matrix multiply_transposed(const Matrix& left, const Matrix& right)
{
  Matrix product;
  for (int32 row = 0; row < state_size; ++row)
  {
    for (int32 column = 0; column < state_size; ++column)
    {
      float64 sum = 0.0;
      for (int32 inner = 0; inner < state_size; ++inner)
      {
        sum += left.at(row, inner) * right.at(column, inner);
      }
      product.put(row, column, sum);
    }
  }
  return product;
}

Matrix add(const Matrix& left, const Matrix& right)
{
  Matrix sum;
  for (int32 row = 0; row < state_size; ++row)
  {
    for (int32 column = 0; column < state_size; ++column)
    {
      sum.put(row, column, left.at(row, column) + right.at(row, column));
    }
  }
  return sum;
}

State apply(const Matrix& matrix, const State& vector)
{
  State product{};
  for (int32 row = 0; row < state_size; ++row)
  {
    float64 sum = 0.0;
    for (int32 column = 0; column < state_size; ++column)
    {
      sum += matrix.at(row, column) * component(vector, column);
    }
    set_component(product, row, sum);
  }
  return product;
}

float64 component(const State& vector, const int32 position)
{
  float64 value = 0.0;
  if (check(inside(position)))
  {
    value = vector[static_cast<std::size_t>(position)];
  }
  return value;
}

void set_component(State& vector,
                   const int32 position,
                   const float64 value)
{
  if (check(inside(position)))
  {
    vector[static_cast<std::size_t>(position)] = value;
  }
}

}  // namespace ics::cue
