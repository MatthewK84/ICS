#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "ics/common/error.hpp"

namespace ics::encoder {

// The terms of a station mount's pointing model (ICS-035), in the order
// python/ics_models/pointing/model.py lists them. Each term's basis, its
// azimuth and elevation correction per radian of coefficient at encoder
// azimuth A and elevation E, is:
//
//   term   azimuth        elevation
//   IA     1              0
//   IE     0              1
//   CA     1/cos E        0
//   NPAE   tan E          0
//   AN     -sin A tan E   -cos A
//   AW     -cos A tan E   sin A
//   TF     0              -cos E
//   HASA   sin A          0
//   HACA   cos A          0
//   HASA2  sin 2A         0
//   HACA2  cos 2A         0
//   HESA   0              sin A
//   HECA   0              cos A
//   HESA2  0              sin 2A
//   HECA2  0              cos 2A
//   HESE   0              sin E
enum class Term : std::uint8_t {
  kIA,
  kIE,
  kCA,
  kNPAE,
  kAN,
  kAW,
  kTF,
  kHASA,
  kHACA,
  kHASA2,
  kHACA2,
  kHESA,
  kHECA,
  kHESA2,
  kHECA2,
  kHESE,
};

inline constexpr std::size_t kTermCount = 16;

// The term's TPOINT name, such as "NPAE".
[[nodiscard]] std::string_view to_string(Term term) noexcept;

// The term with this TPOINT name, or nothing.
[[nodiscard]] std::optional<Term> term_named(std::string_view name) noexcept;

// A direction from the station, in radians: azimuth from north through
// east, elevation up from the horizon. The axis angles a mount's encoders
// read are a Direction too, before the pointing model.
struct Direction {
  double azimuth_rad = 0.0;
  double elevation_rad = 0.0;
};

// One term of a model and its coefficient, in radians.
struct Coefficient {
  Term term = Term::kIA;
  double value_rad = 0.0;
};

// A pointing model: the line of sight is sky = encoder + sum of each
// coefficient times its term's basis at the encoder angles.
class PointingModel {
 public:
  // kInvalidArgument for a term given twice or a coefficient that is not
  // finite. No coefficients is a model that points where the encoders say.
  [[nodiscard]] static Result<PointingModel> make(std::span<const Coefficient> coefficients) noexcept;

  // The model's coefficients, in the order given.
  [[nodiscard]] std::span<const Coefficient> coefficients() const noexcept;

 private:
  PointingModel() noexcept = default;

  std::array<Coefficient, kTermCount> coefficients_{};
  std::size_t count_ = 0;
};

// An angle in [0, 2 pi), as Python's angle % (2 pi) gives it.
[[nodiscard]] double wrap_turn(double angle_rad) noexcept;

// An angle in [-pi, pi).
[[nodiscard]] double wrap_pi(double angle_rad) noexcept;

// Where the line of sight points for these encoder angles; the azimuth in
// [0, 2 pi).
[[nodiscard]] Direction to_sky(const PointingModel& model, Direction encoder) noexcept;

// The encoder angles that point the line of sight at sky, by the fixed-point
// iteration ICS-035's to_encoder uses: at most 10 passes, to 1e-13 rad.
// kUnconstrained when it has not converged by then, which happens only for
// corrections far larger than a mount's or near the zenith.
[[nodiscard]] Result<Direction> to_encoder(const PointingModel& model, Direction sky) noexcept;

// A pointing model file's contents, as ICS-035 writes it
// (schemas/pointing-model.schema.json).
struct ParsedModel {
  PointingModel model;
  std::string model_id;
  std::string station_id;
};

// The largest parameter file read; a fitted model is about 2 KB.
inline constexpr std::size_t kMaxModelFileBytes = 1U << 20U;

// Reads a pointing model file's text, with protobuf's JSON parser. It checks
// what the module relies on: format "ics-pointing-model", format_version 1, a
// model_id and station_id that are not empty, and at least one term, each
// with a known name, given once, a finite value_rad and a sigma_rad of 0 or
// more. kMalformed otherwise.
[[nodiscard]] Result<ParsedModel> parse_pointing_model(std::string_view json);

// A pointing model file, loaded, with what a run record's parameter_files
// entry needs.
struct LoadedModel {
  PointingModel model;
  std::string model_id;
  std::string station_id;
  // The file's SHA-256, as 64 lowercase hexadecimal digits, from OpenSSL.
  std::string sha256_hex;
  std::uint64_t size_bytes = 0;
};

// Reads and parses the file at path. kUnreadable when it cannot be read or
// is larger than kMaxModelFileBytes; kUnavailable when OpenSSL cannot hash
// it; otherwise as parse_pointing_model.
[[nodiscard]] Result<LoadedModel> load_pointing_model(const std::filesystem::path& path);

}  // namespace ics::encoder
