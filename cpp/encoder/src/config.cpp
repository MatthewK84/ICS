#include "ics/encoder/config.hpp"

#include <cstdint>
#include <limits>
#include <string_view>

#include "ics/config/logging_config.hpp"
#include "ics/config/reader.hpp"
#include "ics/encoder/angles.hpp"

namespace ics::encoder {
namespace {

// The most counts a revolution the tagger's signed 32-bit counts can hold.
constexpr std::int64_t kMaxCountsPerRev = std::int64_t{1} << 31U;

[[nodiscard]] AxisConfig read_axis(config::Reader& encoder, const std::string_view counts_key,
                                   const std::string_view reversed_key) {
  return AxisConfig{
      .counts_per_rev = static_cast<std::uint32_t>(encoder.integer(counts_key, kMinCountsPerRev, kMaxCountsPerRev)),
      .reversed = encoder.boolean(reversed_key)};
}

}  // namespace

EncoderSettings read_encoder_settings(config::Reader& encoder) {
  EncoderSettings out;
  out.station_id = encoder.text("station_id");
  out.tagger_address = encoder.text("tagger_address");
  out.tagger_port = static_cast<std::uint16_t>(encoder.integer("tagger_port", 1, std::numeric_limits<std::uint16_t>::max()));
  out.encoders.azimuth = read_axis(encoder, "azimuth_counts_per_rev", "azimuth_reversed");
  out.encoders.elevation = read_axis(encoder, "elevation_counts_per_rev", "elevation_reversed");
  out.pointing_model_file = encoder.text("pointing_model_file");
  return out;
}

MonitorConfig read_monitor_config(config::Reader& root) {
  MonitorConfig out;
  out.log = config::read_logging(root);
  config::Reader encoder = root.section("encoder");
  out.encoder = read_encoder_settings(encoder);
  return out;
}

}  // namespace ics::encoder
