// kinematics_test.cpp: the constant-acceleration model, its noise and its matrix arithmetic (ICS-037).
#include <chrono>

#include <gtest/gtest.h>

#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/cue/kinematics.h>

namespace
{

using ics::float64;
using ics::int32;
using ics::cue::Estimate;
using ics::cue::Matrix;
using ics::cue::Process_noise;

constexpr Process_noise noise{4.0, 1.0};
constexpr float64 same = 1.0E-12;
constexpr float64 dt_s = 0.7;
constexpr float64 first_s = 0.3;
// A positive-definite block: [[4, 2], [2, 3]], then 9.
constexpr float64 corner = 4.0;
constexpr float64 middle = 3.0;
constexpr float64 last = 9.0;

// A start whose state counts up and whose covariance is the identity.
Estimate counting_start()
{
  Estimate start{ics::UtcTime{}, {}, Matrix::identity()};
  for (int32 position = 0; position < ics::cue::state_size; ++position)
  {
    ics::cue::set_component(start.state, position, static_cast<float64>(position));
  }
  return start;
}

ics::UtcTime after(const float64 seconds)
{
  return ics::UtcTime{} + std::chrono::duration_cast<ics::Duration>(std::chrono::duration<float64>(seconds));
}

}  // namespace

// JSF deviation D-5: GoogleTest's TEST and EXPECT macros fix the form of the
// test names and expand to code of GoogleTest's own (AV 50, 51 and 209).
TEST(CueKinematics, PredictionComposesExactly)
{
  const Estimate start = counting_start();
  const Estimate once = ics::cue::predict(start, after(dt_s), noise);
  const Estimate twice = ics::cue::predict(ics::cue::predict(start, after(first_s), noise), after(dt_s), noise);
  for (int32 row = 0; row < ics::cue::state_size; ++row)
  {
    EXPECT_NEAR(ics::cue::component(twice.state, row), ics::cue::component(once.state, row), same);
    for (int32 column = 0; column < ics::cue::state_size; ++column)
    {
      EXPECT_NEAR(twice.covariance.at(row, column), once.covariance.at(row, column), 1.0E-11);
    }
  }
}

TEST(CueKinematics, RefusesToPredictBack)
{
  Estimate later = counting_start();
  later.time = after(1.0);
  const Estimate unchanged = ics::cue::predict(later, after(first_s), noise);
  EXPECT_EQ(unchanged.time, later.time);
  EXPECT_DOUBLE_EQ(ics::cue::component(unchanged.state, 0), 0.0);
  EXPECT_DOUBLE_EQ(unchanged.covariance.at(1, 1), 1.0);
}

TEST(CueKinematics, MovesWithConstantAcceleration)
{
  const Matrix move = ics::cue::transition(2.0);
  EXPECT_DOUBLE_EQ(move.at(ics::cue::index(0, 1), ics::cue::index(1, 1)), 2.0);
  EXPECT_DOUBLE_EQ(move.at(ics::cue::index(0, 1), ics::cue::index(2, 1)), 2.0);
  EXPECT_DOUBLE_EQ(move.at(ics::cue::index(2, 0), ics::cue::index(0, 0)), 0.0);
  const Matrix spread = ics::cue::process_noise(1.0, noise);
  EXPECT_DOUBLE_EQ(spread.at(0, 0), 4.0 / 20.0);
  EXPECT_DOUBLE_EQ(spread.at(ics::cue::index(2, 2), ics::cue::index(2, 2)), 1.0);
  EXPECT_DOUBLE_EQ(spread.at(ics::cue::index(1, 0), ics::cue::index(1, 0)), 4.0 / 3.0);
}

TEST(CueKinematics, ChecksEveryIndex)
{
  Matrix matrix = Matrix::identity();
  ics::cue::State state{};
  matrix.put(ics::cue::state_size, 0, 1.0);
  matrix.put(0, -1, 1.0);
  ics::cue::set_component(state, -1, 1.0);
  EXPECT_DOUBLE_EQ(matrix.at(-1, 0), 0.0);
  EXPECT_DOUBLE_EQ(matrix.at(0, ics::cue::state_size), 0.0);
  EXPECT_DOUBLE_EQ(ics::cue::component(state, ics::cue::state_size), 0.0);
  EXPECT_DOUBLE_EQ(ics::cue::seconds_between(after(1.0), after(0.5)), -0.5);
}

TEST(CueKinematics, InvertsAPositiveDefiniteBlock)
{
  Matrix spread;
  spread.put(0, 0, corner);
  spread.put(0, 1, 2.0);
  spread.put(1, 0, 2.0);
  spread.put(1, 1, middle);
  spread.put(2, 2, last);
  const ics::Result<Matrix> inverse = ics::cue::inverse_symmetric(spread, ics::cue::axes);
  ASSERT_TRUE(inverse.has_value());
  const Matrix product = ics::cue::multiply(spread, *inverse);
  for (int32 row = 0; row < ics::cue::axes; ++row)
  {
    for (int32 column = 0; column < ics::cue::axes; ++column)
    {
      EXPECT_NEAR(product.at(row, column), (row == column) ? 1.0 : 0.0, same);
    }
  }
  EXPECT_DOUBLE_EQ(inverse->at(3, 3), 0.0);
}

TEST(CueKinematics, RefusesWhatItCannotInvert)
{
  Matrix flat = Matrix::identity();
  flat.put(1, 1, -1.0);
  EXPECT_EQ(ics::cue::inverse_symmetric(flat, 3).error(), ics::Error::kUnconstrained);
  EXPECT_EQ(ics::cue::inverse_symmetric(Matrix::identity(), 0).error(), ics::Error::kInvalidArgument);
  EXPECT_EQ(ics::cue::inverse_symmetric(Matrix::identity(), ics::cue::state_size + 1).error(),
            ics::Error::kInvalidArgument);
}
