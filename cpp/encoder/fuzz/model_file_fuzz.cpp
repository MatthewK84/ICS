// Fuzzes the pointing model file's reader (ICS-036) with any text, as a
// file from another tool could hold. It may not overflow or crash, and a
// model it reads must hold what the format allows: one to 16 terms, each
// once, with finite values, and names that are not empty.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>

#include "ics/encoder/pointing_model.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  // The text's bytes are chars to the parser.
  const std::string_view text(reinterpret_cast<const char*>(data), size);
  const auto parsed = ics::encoder::parse_pointing_model(text);
  if (!parsed) {
    return 0;
  }
  const auto coefficients = parsed->model.coefficients();
  if (coefficients.empty() || coefficients.size() > ics::encoder::kTermCount || parsed->model_id.empty() ||
      parsed->station_id.empty()) {
    std::abort();
  }
  for (const ics::encoder::Coefficient& coefficient : coefficients) {
    if (!std::isfinite(coefficient.value_rad)) {
      std::abort();
    }
  }
  const ics::encoder::Direction sky = ics::encoder::to_sky(parsed->model, {.azimuth_rad = 1.0, .elevation_rad = 0.5});
  static_cast<void>(ics::encoder::to_encoder(parsed->model, sky));
  return 0;
}
