#include "ics/plid/config.hpp"

#include <chrono>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "plid_support.hpp"

namespace {

using ics::plid::Config;
using ics::plid::testing::read_text;

constexpr std::string_view kEveryFeed = R"(
[log]
level = "debug"
service = "ics-plid"

[plid]
store_folder = "/tmp/pli"
query_socket = "/tmp/pli/query"
rotate_interval_ns = 60_000_000_000
sync_interval_ns = 10_000_000
feeds = ["mavlink", "cot", "sapient", "lattice"]

[range]
latitude_deg = -35.36
longitude_deg = 149.17
height_m = 600.0

[capture]
interfaces = []
files = ["a.pcap"]

[mavlink]
roles = ["1=target"]
link_timeout_ns = 2_000_000_000
max_age_ns = 500_000_000

[cot]
port = 4242
roles = ["ANDROID-1=interceptor"]
ce_probability = 0.5
le_probability = 0.6
max_skew_ns = 5_000_000_000

[sapient]
address = "127.0.0.1"
port = 5020
roles = []
max_skew_ns = 1_000_000_000

[lattice]
url = "https://lattice.example.com"
token_file = "/etc/ics/lattice-token"
sandbox = true
sandbox_token_file = "/etc/ics/lattice-sandbox-token"
roles = ["e-1=target"]
max_skew_ns = 2_000_000_000
)";

std::set<std::string> bad_fields(const std::string_view text) {
  const auto config = read_text(text);
  std::set<std::string> fields;
  for (const ics::config::ConfigError& error : config.has_value() ? ics::config::Errors{} : config.error()) {
    fields.insert(error.field);
  }
  return fields;
}

TEST(PlidConfig, ReadsTheExample) {
  const auto config = ics::config::read_file(ICS_PLID_EXAMPLE, &ics::plid::read_config);
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->log.service, "ics-plid");
  EXPECT_EQ(config->store_folder, "/var/lib/ics/pli");
  EXPECT_EQ(config->query_socket, "/run/ics-plid/query");
  EXPECT_EQ(config->rotate_interval, std::chrono::hours(1));
  EXPECT_EQ(config->sync_interval, std::chrono::seconds(1));
  EXPECT_EQ(config->interfaces, std::vector<std::string>{"ics-tap0"});
  EXPECT_TRUE(config->files.empty());
  ASSERT_TRUE(config->mavlink.has_value());
  EXPECT_EQ(config->mavlink->roles, (std::vector<std::string>{"1=target", "2=interceptor"}));
  ASSERT_TRUE(config->cot.has_value());
  EXPECT_EQ(config->cot->port, 6969);
  EXPECT_FALSE(config->sapient.has_value());
  EXPECT_FALSE(config->lattice.has_value());
  std::string reason;
  EXPECT_TRUE(ics::plid::validate(*config, reason).has_value());
}

TEST(PlidConfig, ReadsEveryFeed) {
  const auto config = read_text(kEveryFeed);
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->range.latitude, ics::Degrees(-35.36));
  EXPECT_EQ(config->files, std::vector<std::filesystem::path>{"a.pcap"});
  EXPECT_EQ(config->mavlink->link_timeout, std::chrono::seconds(2));
  EXPECT_EQ(config->cot->ce_probability, 0.5);
  EXPECT_EQ(config->sapient->address, "127.0.0.1");
  EXPECT_EQ(config->sapient->port, 5020);
  EXPECT_EQ(config->lattice->sandbox_token_file, std::filesystem::path("/etc/ics/lattice-sandbox-token"));
  EXPECT_EQ(config->lattice->roles, std::vector<std::string>{"e-1=target"});
}

TEST(PlidConfig, ReadsALatticeFeedWithoutASandbox) {
  std::string text(kEveryFeed);
  text.replace(text.find("sandbox = true"), std::string_view("sandbox = true").size(), "sandbox = false");
  text.erase(text.find("sandbox_token_file"), text.find("roles = [\"e-1") - text.find("sandbox_token_file"));
  const auto config = read_text(text);
  ASSERT_TRUE(config.has_value());
  EXPECT_FALSE(config->lattice->sandbox_token_file.has_value());
}

TEST(PlidConfig, NamesEachBadSetting) {
  std::string text(kEveryFeed);
  text.replace(text.find("sync_interval_ns = 10_000_000"), 29, "sync_interval_ns = 1");
  text.replace(text.find("port = 4242"), 11, "port = 0");
  text.replace(text.find("ce_probability = 0.5"), 20, "ce_probability = 1.0");
  const std::set<std::string> expected{"plid.sync_interval_ns", "cot.port", "cot.ce_probability"};
  EXPECT_EQ(bad_fields(text), expected);
}

TEST(PlidConfig, ValidatesTheFeedsAndTheirSources) {
  Config config = read_text(kEveryFeed).value();
  std::string reason;
  EXPECT_TRUE(ics::plid::validate(config, reason).has_value());
  for (const std::vector<std::string>& feeds :
       {std::vector<std::string>{"mavlink", "cot", "sapient", "lattice", "radar"},
        std::vector<std::string>{"mavlink", "mavlink", "sapient", "lattice"}}) {
    Config listed = config;
    listed.feeds = feeds;
    EXPECT_EQ(ics::plid::validate(listed, reason).error(), ics::Error::kInvalidArgument);
    EXPECT_NE(reason.find("feeds"), std::string::npos);
  }
  Config both = config;
  both.interfaces = {"lo"};
  EXPECT_EQ(ics::plid::validate(both, reason).error(), ics::Error::kInvalidArgument);
  Config neither = config;
  neither.files.clear();
  EXPECT_EQ(ics::plid::validate(neither, reason).error(), ics::Error::kInvalidArgument);
  // SAPIENT and Lattice need no capture.
  Config tcp_only = neither;
  tcp_only.mavlink.reset();
  tcp_only.cot.reset();
  tcp_only.feeds = {"sapient", "lattice"};
  EXPECT_TRUE(ics::plid::validate(tcp_only, reason).has_value());
  Config cot_only = config;
  cot_only.mavlink.reset();
  cot_only.feeds = {"cot", "sapient", "lattice"};
  EXPECT_TRUE(ics::plid::validate(cot_only, reason).has_value());
}

}  // namespace
