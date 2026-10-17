#include "ics/encoder/pointing_model.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <optional>
#include <span>
#include <string_view>

#include "ics/common/check.hpp"
#include "ics/common/error.hpp"

namespace ics::encoder {
namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi;
constexpr int kMaxPasses = 10;
constexpr double kConvergedRad = 1e-13;

constexpr std::array<std::string_view, kTermCount> kNames{"IA",   "IE",    "CA",    "NPAE", "AN",   "AW",
                                                          "TF",   "HASA",  "HACA",  "HASA2", "HACA2", "HESA",
                                                          "HECA", "HESA2", "HECA2", "HESE"};

// One term's azimuth and elevation correction per radian of coefficient.
struct Basis {
  double azimuth = 0.0;
  double elevation = 0.0;
};

// Each term's basis at these encoder angles, in Term's order.
[[nodiscard]] std::array<Basis, kTermCount> basis_table(const Direction at) noexcept {
  const double sin_a = std::sin(at.azimuth_rad);
  const double cos_a = std::cos(at.azimuth_rad);
  const double sin_2a = std::sin(2.0 * at.azimuth_rad);
  const double cos_2a = std::cos(2.0 * at.azimuth_rad);
  const double sin_e = std::sin(at.elevation_rad);
  const double cos_e = std::cos(at.elevation_rad);
  const double tan_e = sin_e / cos_e;
  return {{{1.0, 0.0},
           {0.0, 1.0},
           {1.0 / cos_e, 0.0},
           {tan_e, 0.0},
           {-sin_a * tan_e, -cos_a},
           {-cos_a * tan_e, sin_a},
           {0.0, -cos_e},
           {sin_a, 0.0},
           {cos_a, 0.0},
           {sin_2a, 0.0},
           {cos_2a, 0.0},
           {0.0, sin_a},
           {0.0, cos_a},
           {0.0, sin_2a},
           {0.0, cos_2a},
           {0.0, sin_e}}};
}

// The model's azimuth and elevation correction at these encoder angles,
// summed in the model's order, as ICS-035's correction does.
[[nodiscard]] Basis correction(const PointingModel& model, const Direction encoder) noexcept {
  const std::array<Basis, kTermCount> table = basis_table(encoder);
  Basis sum;
  for (const Coefficient& coefficient : model.coefficients()) {
    const Basis& per = table.at(static_cast<std::size_t>(coefficient.term));
    sum.azimuth += coefficient.value_rad * per.azimuth;
    sum.elevation += coefficient.value_rad * per.elevation;
  }
  return sum;
}

}  // namespace

std::string_view to_string(const Term term) noexcept {
  const auto index = static_cast<std::size_t>(term);
  return check(index < kNames.size()) ? kNames.at(index) : std::string_view("unknown");
}

std::optional<Term> term_named(const std::string_view name) noexcept {
  const auto* const found = std::ranges::find(kNames, name);
  if (found == kNames.end()) {
    return std::nullopt;
  }
  return static_cast<Term>(found - kNames.begin());
}

Result<PointingModel> PointingModel::make(const std::span<const Coefficient> coefficients) noexcept {
  PointingModel model;
  std::array<bool, kTermCount> seen{};
  for (const Coefficient& coefficient : coefficients) {
    const auto index = static_cast<std::size_t>(coefficient.term);
    if (!check(index < kTermCount) || seen.at(index) || !std::isfinite(coefficient.value_rad)) {
      return fail(Error::kInvalidArgument);
    }
    seen.at(index) = true;
    model.coefficients_.at(model.count_) = coefficient;
    ++model.count_;
  }
  return model;
}

std::span<const Coefficient> PointingModel::coefficients() const noexcept {
  static_cast<void>(check(count_ <= coefficients_.size()));
  return std::span<const Coefficient>(coefficients_).first(std::min(count_, coefficients_.size()));
}

double wrap_turn(const double angle_rad) noexcept {
  const double turned = std::fmod(angle_rad, kTwoPi);
  // Python's float % takes the divisor's sign, and gives +0 for a zero.
  if (turned < 0.0) {
    return turned + kTwoPi;
  }
  return turned == 0.0 ? 0.0 : turned;
}

double wrap_pi(const double angle_rad) noexcept { return wrap_turn(angle_rad + std::numbers::pi) - std::numbers::pi; }

Direction to_sky(const PointingModel& model, const Direction encoder) noexcept {
  const Basis d = correction(model, encoder);
  return Direction{.azimuth_rad = wrap_turn(encoder.azimuth_rad + d.azimuth),
                   .elevation_rad = encoder.elevation_rad + d.elevation};
}

Result<Direction> to_encoder(const PointingModel& model, const Direction sky) noexcept {
  Direction encoder = sky;
  for (int pass = 0; pass < kMaxPasses; ++pass) {
    const Basis d = correction(model, encoder);
    const Direction nearer{.azimuth_rad = wrap_turn(sky.azimuth_rad - d.azimuth),
                           .elevation_rad = sky.elevation_rad - d.elevation};
    const double moved = std::max(std::abs(wrap_pi(nearer.azimuth_rad - encoder.azimuth_rad)),
                                  std::abs(nearer.elevation_rad - encoder.elevation_rad));
    encoder = nearer;
    if (moved < kConvergedRad) {
      return encoder;
    }
  }
  return fail(Error::kUnconstrained);
}

}  // namespace ics::encoder
