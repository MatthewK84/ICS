#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "ics/config/logging_config.hpp"
#include "ics/config/reader.hpp"
#include "ics/encoder/angles.hpp"

namespace ics::encoder {

// Where the encoder module's inputs come from in operation (ICS-036): the
// [encoder] table of a station's config. ics-encoder-monitor reads it from
// /etc/ics/ics-encoder.toml, and ics-mountd (ICS-041) will read the same
// table:
//
//   [encoder]
//   station_id = "station-1"                         # as named in the range configuration
//   tagger_address = "0.0.0.0"                       # this host's IPv4 address the tagger sends to
//   tagger_port = 5020                               # the UDP port the tagger is set to send to
//   azimuth_counts_per_rev = 16_777_216              # 2 to 2^31
//   azimuth_reversed = false                         # true when counts fall through east
//   elevation_counts_per_rev = 16_777_216
//   elevation_reversed = false                       # true when counts fall going up
//   pointing_model_file = "/etc/ics/pointing-model.json" # ICS-035's parameter file
struct EncoderSettings {
  std::string station_id;
  std::string tagger_address;
  std::uint16_t tagger_port = 0;
  EncoderConfig encoders;
  std::filesystem::path pointing_model_file;
};

// The [encoder] table.
[[nodiscard]] EncoderSettings read_encoder_settings(config::Reader& encoder);

// ics-encoder-monitor's settings: the [log] and [encoder] tables.
struct MonitorConfig {
  config::LoggingConfig log;
  EncoderSettings encoder;
};

[[nodiscard]] MonitorConfig read_monitor_config(config::Reader& root);

}  // namespace ics::encoder
