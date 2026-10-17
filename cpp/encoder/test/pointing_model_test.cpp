// The pointing model, held to ICS-035's golden vectors in both directions
// (ICS-036's "the model matches Python's").
#include "ics/encoder/pointing_model.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <limits>
#include <numbers>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "encoder_support.hpp"
#include "ics/common/error.hpp"

namespace {

using ics::encoder::Coefficient;
using ics::encoder::Direction;
using ics::encoder::PointingModel;
using ics::encoder::Term;
using ics::encoder::testing::sky_error;

constexpr double kGoldenRad = 1e-12;
constexpr double kTwoPi = 2.0 * std::numbers::pi;

struct GoldenRow {
  Direction encoder;
  Direction sky;
};

[[nodiscard]] std::vector<GoldenRow> golden_rows() {
  std::ifstream file(std::string(ICS_GOLDEN_POINTING_DIR) + "/vectors.csv");
  EXPECT_TRUE(file.is_open());
  std::string line;
  std::getline(file, line);
  EXPECT_EQ(line, "encoder_az_rad,encoder_el_rad,sky_az_rad,sky_el_rad");
  std::vector<GoldenRow> rows;
  while (std::getline(file, line)) {
    std::array<double, 4> values{};
    std::stringstream fields(line);
    std::string field;
    for (double& value : values) {
      std::getline(fields, field, ',');
      value = std::stod(field);
    }
    rows.push_back({.encoder = {values[0], values[1]}, .sky = {values[2], values[3]}});
  }
  return rows;
}

[[nodiscard]] PointingModel golden_model() {
  const auto loaded = ics::encoder::load_pointing_model(std::string(ICS_GOLDEN_POINTING_DIR) + "/model.json");
  EXPECT_TRUE(loaded.has_value());
  return loaded.value().model;
}

[[nodiscard]] PointingModel model_of(const std::vector<Coefficient>& coefficients) {
  return PointingModel::make(coefficients).value();
}

TEST(PointingModel, MatchesPythonFromEncoderToSky) {
  const PointingModel model = golden_model();
  const std::vector<GoldenRow> rows = golden_rows();
  ASSERT_EQ(rows.size(), 60U);
  for (const GoldenRow& row : rows) {
    const Direction sky = ics::encoder::to_sky(model, row.encoder);
    EXPECT_NEAR(ics::encoder::wrap_pi(sky.azimuth_rad - row.sky.azimuth_rad), 0.0, kGoldenRad);
    EXPECT_NEAR(sky.elevation_rad, row.sky.elevation_rad, kGoldenRad);
    EXPECT_GE(sky.azimuth_rad, 0.0);
    EXPECT_LT(sky.azimuth_rad, kTwoPi);
  }
}

TEST(PointingModel, MatchesPythonFromSkyToEncoder) {
  const PointingModel model = golden_model();
  for (const GoldenRow& row : golden_rows()) {
    const auto encoder = ics::encoder::to_encoder(model, row.sky);
    ASSERT_TRUE(encoder.has_value());
    EXPECT_NEAR(ics::encoder::wrap_pi(encoder->azimuth_rad - row.encoder.azimuth_rad), 0.0, kGoldenRad);
    EXPECT_NEAR(encoder->elevation_rad, row.encoder.elevation_rad, kGoldenRad);
  }
}

TEST(PointingModel, NamesEveryTermOnce) {
  for (std::size_t i = 0; i < ics::encoder::kTermCount; ++i) {
    const auto term = static_cast<Term>(i);
    EXPECT_EQ(ics::encoder::term_named(ics::encoder::to_string(term)), term);
  }
  EXPECT_EQ(ics::encoder::to_string(Term::kNPAE), "NPAE");
  EXPECT_EQ(ics::encoder::to_string(Term::kHECA2), "HECA2");
  EXPECT_EQ(ics::encoder::to_string(static_cast<Term>(ics::encoder::kTermCount)), "unknown");
  EXPECT_EQ(ics::encoder::term_named("npae"), std::nullopt);
  EXPECT_EQ(ics::encoder::term_named(""), std::nullopt);
}

TEST(PointingModel, RefusesATermTwiceAValueNotFiniteOrATermItDoesNotKnow) {
  const std::array<Coefficient, 2> twice{{{Term::kIA, 0.0}, {Term::kIA, 1e-3}}};
  EXPECT_EQ(PointingModel::make(twice).error(), ics::Error::kInvalidArgument);
  const std::array<Coefficient, 1> nan{{{Term::kIE, std::numeric_limits<double>::quiet_NaN()}}};
  EXPECT_EQ(PointingModel::make(nan).error(), ics::Error::kInvalidArgument);
  const std::array<Coefficient, 1> infinite{{{Term::kIE, std::numeric_limits<double>::infinity()}}};
  EXPECT_EQ(PointingModel::make(infinite).error(), ics::Error::kInvalidArgument);
  const std::array<Coefficient, 1> unknown{{{static_cast<Term>(ics::encoder::kTermCount), 0.0}}};
  EXPECT_EQ(PointingModel::make(unknown).error(), ics::Error::kInvalidArgument);
}

TEST(PointingModel, KeepsTheCoefficientsInTheOrderGiven) {
  const PointingModel model = model_of({{Term::kTF, 1e-4}, {Term::kIA, 2e-3}});
  ASSERT_EQ(model.coefficients().size(), 2U);
  EXPECT_EQ(model.coefficients()[0].term, Term::kTF);
  EXPECT_EQ(model.coefficients()[1].value_rad, 2e-3);
}

TEST(PointingModel, WithoutTermsPointsWhereTheEncodersSay) {
  const PointingModel nothing = model_of({});
  const Direction encoder{.azimuth_rad = 1.0, .elevation_rad = 0.5};
  const Direction sky = ics::encoder::to_sky(nothing, encoder);
  EXPECT_EQ(sky.azimuth_rad, 1.0);
  EXPECT_EQ(sky.elevation_rad, 0.5);
  EXPECT_EQ(ics::encoder::to_encoder(nothing, encoder)->azimuth_rad, 1.0);
}

TEST(PointingModel, IndexTermsOffsetBothAxesAndTheAzimuthWraps) {
  const PointingModel model = model_of({{Term::kIA, 2e-3}, {Term::kIE, -1e-3}});
  const Direction sky = ics::encoder::to_sky(model, {.azimuth_rad = kTwoPi - 1e-3, .elevation_rad = 0.3});
  EXPECT_NEAR(sky.azimuth_rad, 1e-3, 1e-15);
  EXPECT_NEAR(sky.elevation_rad, 0.299, 1e-15);
  const auto back = ics::encoder::to_encoder(model, sky);
  ASSERT_TRUE(back.has_value());
  EXPECT_LT(sky_error(*back, {.azimuth_rad = kTwoPi - 1e-3, .elevation_rad = 0.3}), kGoldenRad);
}

TEST(PointingModel, SaysWhenSkyToEncoderDoesNotConverge) {
  // An elevation error of 1.5 sin E, which no mount has, feeds on itself.
  const PointingModel wild = model_of({{Term::kHESE, 1.5}});
  EXPECT_EQ(ics::encoder::to_encoder(wild, {.azimuth_rad = 0.0, .elevation_rad = 0.3}).error(),
            ics::Error::kUnconstrained);
}

TEST(PointingModel, WrapsAnglesAsPythonDoes) {
  EXPECT_EQ(ics::encoder::wrap_turn(0.0), 0.0);
  EXPECT_FALSE(std::signbit(ics::encoder::wrap_turn(-0.0)));
  EXPECT_EQ(ics::encoder::wrap_turn(kTwoPi), 0.0);
  EXPECT_NEAR(ics::encoder::wrap_turn(-1e-3), kTwoPi - 1e-3, 1e-15);
  EXPECT_NEAR(ics::encoder::wrap_turn(3.0 * kTwoPi + 1.0), 1.0, 1e-14);
  EXPECT_EQ(ics::encoder::wrap_pi(std::numbers::pi), -std::numbers::pi);
  EXPECT_NEAR(ics::encoder::wrap_pi(kTwoPi - 1e-3), -1e-3, 1e-14);
}

}  // namespace
