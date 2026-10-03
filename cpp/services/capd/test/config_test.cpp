#include "ics/capd/config.hpp"

#include <chrono>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "ics/config/config.hpp"
#include "ics/config/reader.hpp"
#include "ics/logging/json_line.hpp"

namespace {

using ics::capd::Config;
using ics::capture::TimestampSource;

std::set<std::string> bad_fields(const std::string_view text) {
  const auto table = ics::config::parse(text);
  EXPECT_TRUE(table.has_value());
  const auto config = ics::config::read(*table, &ics::capd::read_config);
  std::set<std::string> fields;
  for (const ics::config::ConfigError& error : config.has_value() ? ics::config::Errors{} : config.error()) {
    fields.insert(error.field);
  }
  return fields;
}

constexpr std::string_view kHost = R"(
[log]
level = "info"
service = "ics-capd"

[capture]
interfaces = ["veth-cap"]
folder = "/tmp/capture"
snaplen = 128
buffer_bytes = 1_048_576
timestamps = "host"
rotate_interval_ns = 5_000_000_000
rotate_bytes = 1_048_576
)";

TEST(CapdConfig, ReadsTheExample) {
  const auto config = ics::config::read_file(ICS_CAPD_EXAMPLE, &ics::capd::read_config);
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->log.level, ics::logging::Level::kInfo);
  EXPECT_EQ(config->log.service, "ics-capd");
  EXPECT_EQ(config->interfaces, (std::vector<std::string>{"tap0", "tap1"}));
  EXPECT_EQ(config->folder, "/var/lib/ics/capture");
  EXPECT_EQ(config->snaplen, 65535U);
  EXPECT_EQ(config->buffer_bytes, 67'108'864U);
  EXPECT_EQ(config->timestamps, TimestampSource::kAdapter);
  EXPECT_EQ(config->rotation.max_age, std::chrono::hours(1));
  EXPECT_EQ(config->rotation.max_bytes, 4'000'000'000U);
  ASSERT_TRUE(config->ptp.has_value());
  EXPECT_EQ(config->ptp->ptp4l_socket, "/var/run/ptp4l-ro");
  EXPECT_EQ(config->ptp->client_socket, "/run/ics-capd/ptp4l-client");
  EXPECT_EQ(config->ptp->ptp_domain, 0);
}

TEST(CapdConfig, NeedsNoPtpTableForHostTimeStamps) {
  const auto table = ics::config::parse(kHost);
  ASSERT_TRUE(table.has_value());
  const auto config = ics::config::read(*table, &ics::capd::read_config);
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->timestamps, TimestampSource::kHost);
  EXPECT_FALSE(config->ptp.has_value());
  EXPECT_EQ(bad_fields(std::string(kHost) + "[ptp]\nptp_domain = 0\n"), (std::set<std::string>{"ptp"}));
}

TEST(CapdConfig, NamesEachBadSetting) {
  EXPECT_EQ(bad_fields(R"(
[log]
level = "info"
service = "ics-capd"

[capture]
interfaces = []
snaplen = 63
buffer_bytes = 1_073_741_825
timestamps = "adapter"
rotate_interval_ns = 999_999_999
rotate_bytes = 1_099_511_627_777
colour = "blue"

[ptp]
ptp4l_socket = "/var/run/ptp4l-ro"
ptp_domain = 128
)"),
            (std::set<std::string>{"capture.interfaces", "capture.folder", "capture.snaplen", "capture.buffer_bytes",
                                   "capture.rotate_interval_ns", "capture.rotate_bytes", "capture.colour",
                                   "ptp.client_socket", "ptp.ptp_domain"}));
}

TEST(CapdConfig, AcceptsOnlyInterfaceNamesLinuxAccepts) {
  EXPECT_TRUE(ics::capd::valid_interface_name("tap0"));
  EXPECT_TRUE(ics::capd::valid_interface_name("enp1s0f1-tap.5"));
  EXPECT_TRUE(ics::capd::valid_interface_name("fifteen-chars-x"));
  EXPECT_FALSE(ics::capd::valid_interface_name(""));
  EXPECT_FALSE(ics::capd::valid_interface_name("sixteen-chars-xx"));
  EXPECT_FALSE(ics::capd::valid_interface_name("."));
  EXPECT_FALSE(ics::capd::valid_interface_name(".."));
  EXPECT_FALSE(ics::capd::valid_interface_name("../etc"));
  EXPECT_FALSE(ics::capd::valid_interface_name("eth0:1"));
  EXPECT_FALSE(ics::capd::valid_interface_name("tap 0"));
}

}  // namespace
