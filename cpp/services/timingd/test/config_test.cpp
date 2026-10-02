#include "ics/timingd/config.hpp"

#include <chrono>
#include <set>
#include <string>

#include <gtest/gtest.h>

#include "ics/config/config.hpp"
#include "ics/config/reader.hpp"
#include "ics/logging/json_line.hpp"

namespace {

using ics::timingd::Config;

TEST(TimingdConfig, ReadsTheExample) {
  const auto config = ics::config::read_file(ICS_TIMINGD_EXAMPLE, &ics::timingd::read_config);
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->log.level, ics::logging::Level::kInfo);
  EXPECT_EQ(config->log.service, "ics-timingd");
  EXPECT_EQ(config->station_id, "station-1");
  EXPECT_EQ(config->ptp4l_socket, "/var/run/ptp4l-ro");
  EXPECT_EQ(config->client_socket, "/run/ics-timingd/ptp4l-client");
  EXPECT_EQ(config->publish_socket, "/run/ics-timingd/time-quality");
  EXPECT_EQ(config->ptp_domain, 0);
  EXPECT_EQ(config->poll_interval, std::chrono::milliseconds(100));
  EXPECT_EQ(config->model.asymmetry_bound, std::chrono::microseconds(1));
  EXPECT_DOUBLE_EQ(config->model.holdover_drift_ns_per_s, 50.0);
}

TEST(TimingdConfig, NamesEachBadSetting) {
  const auto table = ics::config::parse(R"(
[log]
level = "info"
service = "ics-timingd"

[timing]
ptp4l_socket = "/var/run/ptp4l-ro"
client_socket = "/run/ics-timingd/ptp4l-client"
publish_socket = "/run/ics-timingd/time-quality"
ptp_domain = 128
poll_interval_ns = 5_000_000
asymmetry_bound_ns = -1
holdover_drift_ns_per_s = 50.0
colour = "blue"
)");
  ASSERT_TRUE(table.has_value());
  const auto config = ics::config::read(*table, &ics::timingd::read_config);
  ASSERT_FALSE(config.has_value());
  std::set<std::string> fields;
  for (const ics::config::ConfigError& error : config.error()) {
    fields.insert(error.field);
  }
  EXPECT_EQ(fields, (std::set<std::string>{"timing.station_id", "timing.ptp_domain", "timing.poll_interval_ns",
                                           "timing.asymmetry_bound_ns", "timing.colour"}));
}

}  // namespace
