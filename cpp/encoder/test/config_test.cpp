// The [encoder] table, where each production input is named.
#include "ics/encoder/config.hpp"

#include <set>
#include <string>

#include <gtest/gtest.h>

#include "ics/config/config.hpp"
#include "ics/config/reader.hpp"
#include "ics/logging/logger.hpp"

namespace {

TEST(EncoderConfig, ReadsTheExample) {
  const auto config = ics::config::read_file(ICS_ENCODER_EXAMPLE, &ics::encoder::read_monitor_config);
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->log.level, ics::logging::Level::kInfo);
  EXPECT_EQ(config->log.service, "ics-encoder-monitor");
  const ics::encoder::EncoderSettings& encoder = config->encoder;
  EXPECT_EQ(encoder.station_id, "station-1");
  EXPECT_EQ(encoder.tagger_address, "0.0.0.0");
  EXPECT_EQ(encoder.tagger_port, 5020);
  EXPECT_EQ(encoder.encoders.azimuth.counts_per_rev, 1U << 24U);
  EXPECT_FALSE(encoder.encoders.azimuth.reversed);
  EXPECT_EQ(encoder.encoders.elevation.counts_per_rev, 1U << 24U);
  EXPECT_FALSE(encoder.encoders.elevation.reversed);
  EXPECT_EQ(encoder.pointing_model_file, "/etc/ics/pointing-model.json");
  EXPECT_TRUE(ics::encoder::valid(encoder.encoders));
}

TEST(EncoderConfig, NamesEachBadSetting) {
  const auto table = ics::config::parse(R"(
[log]
level = "info"
service = "ics-encoder-monitor"

[encoder]
station_id = ""
tagger_address = "0.0.0.0"
tagger_port = 0
azimuth_counts_per_rev = 1
azimuth_reversed = "no"
elevation_counts_per_rev = 2_147_483_649
elevation_reversed = true
encoder_port = 5020
)");
  ASSERT_TRUE(table.has_value());
  const auto config = ics::config::read(*table, &ics::encoder::read_monitor_config);
  ASSERT_FALSE(config.has_value());
  std::set<std::string> fields;
  for (const ics::config::ConfigError& error : config.error()) {
    fields.insert(error.field);
  }
  EXPECT_EQ(fields, (std::set<std::string>{"encoder.station_id", "encoder.tagger_port",
                                           "encoder.azimuth_counts_per_rev", "encoder.azimuth_reversed",
                                           "encoder.elevation_counts_per_rev", "encoder.pointing_model_file",
                                           "encoder.encoder_port"}));
}

TEST(EncoderConfig, TakesTheLargestCountsATurnTheTaggerHolds) {
  const auto table = ics::config::parse(R"(
station_id = "station-1"
tagger_address = "192.0.2.10"
tagger_port = 65535
azimuth_counts_per_rev = 2_147_483_648
azimuth_reversed = true
elevation_counts_per_rev = 2
elevation_reversed = false
pointing_model_file = "model.json"
)");
  ASSERT_TRUE(table.has_value());
  const auto settings = ics::config::read(*table, &ics::encoder::read_encoder_settings);
  ASSERT_TRUE(settings.has_value());
  EXPECT_EQ(settings->tagger_port, 65535);
  EXPECT_EQ(settings->encoders.azimuth.counts_per_rev, 1U << 31U);
  EXPECT_TRUE(settings->encoders.azimuth.reversed);
  EXPECT_EQ(settings->encoders.elevation.counts_per_rev, 2U);
}

}  // namespace
