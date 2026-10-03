#include "ics/capd/config.hpp"

#include <chrono>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "ics/capture/live_capture.hpp"
#include "ics/config/config.hpp"
#include "ics/config/reader.hpp"
#include "ics/logging/json_line.hpp"

namespace {

TEST(CapdConfig, ReadsTheExample) {
  const auto config = ics::config::read_file(ICS_CAPD_EXAMPLE, &ics::capd::read_config);
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->log.level, ics::logging::Level::kInfo);
  EXPECT_EQ(config->log.service, "ics-capd");
  EXPECT_EQ(config->interfaces, std::vector<std::string>{"tap0"});
  EXPECT_EQ(config->output_dir, "/var/lib/ics/capture");
  EXPECT_EQ(config->snaplen, 65'535U);
  EXPECT_EQ(config->ring_bytes, 64U << 20U);
  EXPECT_EQ(config->timestamps, ics::capture::TimestampSource::kAdapter);
  EXPECT_EQ(config->rotation.interval, std::chrono::hours(1));
  EXPECT_EQ(config->rotation.max_bytes, 1ULL << 30U);
  EXPECT_EQ(config->ptp4l_socket, "/var/run/ptp4l-ro");
  EXPECT_EQ(config->client_socket, "/run/ics-capd/ptp4l-client");
  EXPECT_EQ(config->ptp_domain, 0);
}

TEST(CapdConfig, NamesEachBadSetting) {
  const auto table = ics::config::parse(R"(
[log]
level = "info"
service = "ics-capd"

[capture]
interfaces = ["tap0", "tap1", "tap2", "tap3", "tap4"]
output_dir = "/var/lib/ics/capture"
snaplen = 32
ring_bytes = 67_108_864
timestamps = "gps"
rotate_interval_ns = 0
rotate_bytes = 1_073_741_824
ptp4l_socket = "/var/run/ptp4l-ro"
client_socket = "/run/ics-capd/ptp4l-client"
ptp_domain = 0
)");
  ASSERT_TRUE(table.has_value());
  const auto config = ics::config::read(*table, &ics::capd::read_config);
  ASSERT_FALSE(config.has_value());
  std::set<std::string> fields;
  for (const ics::config::ConfigError& error : config.error()) {
    fields.insert(error.field);
  }
  EXPECT_EQ(fields, (std::set<std::string>{"capture.interfaces", "capture.snaplen", "capture.timestamps",
                                           "capture.rotate_interval_ns"}));
}

}  // namespace
