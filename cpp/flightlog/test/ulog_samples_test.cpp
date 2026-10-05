// The ULog sample logs (logs/README.md), read with ics::flightlog::ULog and
// compared with what PX4's pyulog read in them (logs/expected.tsv).
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <vector>

#include <gtest/gtest.h>

#include "fixtures.hpp"
#include "ics/flightlog/ulog.hpp"

namespace ics::flightlog {
namespace {

using testing::expected;
using testing::Expected;

const std::vector<std::string> kLogs{"px4-crossing.ulg", "pyulog-sample-px4-events.ulg",
                                     "pyulog-sample-logging-tagged.ulg", "pyulog-sample-appended-multiple.ulg"};

// Each NAME=VALUE of an expectation, NAME or NAME[INDEX], matches the sample.
void expect_sample(const ULogFormat& format, const std::span<const std::byte> sample, const Expected& line) {
  for (const std::string& value : line.values) {
    const std::size_t equals = value.find('=');
    std::string name = value.substr(0, equals);
    std::size_t index = 0;
    if (name.ends_with("]")) {
      index = std::stoul(name.substr(name.find('[') + 1));
      name.resize(name.find('['));
    }
    const std::optional<ULogField> field = find_field(format, name);
    ASSERT_TRUE(field.has_value()) << name;
    EXPECT_EQ(read(*field, sample, index), std::stod(value.substr(equals + 1))) << format.name << " " << value;
  }
}

// The samples of a topic for which a boolean field, if the format has it, holds.
std::vector<std::span<const std::byte>> where(const ULog& log, const std::string& topic, const std::string& flag) {
  const ULogFormat& format = log.format(topic)->get();
  const std::optional<ULogField> field = find_field(format, flag);
  std::vector<std::span<const std::byte>> out;
  std::ranges::copy_if(log.samples(topic, 0), std::back_inserter(out), [&field](const std::span<const std::byte> sample) {
    return !field || read(*field, sample) != 0.0;
  });
  return out;
}

// Runs a check on each sample log, parsed. A failed ASSERT ends the check of
// that log only.
void for_each_log(void (*check)(const std::string& name, const ULog& log)) {
  for (const std::string& name : kLogs) {
    SCOPED_TRACE(name);
    const std::vector<std::byte> bytes = testing::log_file(name);
    const Result<ULog> log = ULog::parse(bytes);
    ASSERT_TRUE(log.has_value());
    check(name, *log);
  }
}

void hold_every_sample_of_every_topic(const std::string& name, const ULog& log) {
  EXPECT_FALSE(log.counts().corrupt);
  for (const auto& [topic, count] : testing::expected_counts(name)) {
    EXPECT_EQ(log.samples(topic, 0).size(), count) << topic;
  }
  EXPECT_EQ(log.parameter("MAV_SYS_ID"), std::stod(testing::expected_value(name, "system_id")));
}

void read_the_logged_strings(const std::string& name, const ULog& log) {
  ASSERT_EQ(log.strings().size(), std::stoul(testing::expected_value(name, "strings")));
  const Expected first = expected(name, "first_string").at(0);
  EXPECT_EQ(log.strings()[0].timestamp_us, std::stoull(first.values.at(0)));
  EXPECT_EQ(log.strings()[0].text, first.values.at(1));
}

void read_positions_and_gnss(const std::string& name, const ULog& log) {
  const std::vector<std::tuple<std::string, std::string, std::string>> topics{
      {"vehicle_global_position", "lat_lon_valid", "global"}, {"vehicle_gps_position", "", "gps"}};
  for (const auto& [topic, flag, key] : topics) {
    const std::vector<Expected> first = expected(name, "first_" + key);
    if (first.empty()) {
      continue;
    }
    std::vector<std::span<const std::byte>> samples = where(log, topic, flag);
    if (key == "gps") {
      const ULogField fix = find_field(log.format(topic)->get(), "fix_type").value();
      std::erase_if(samples, [&fix](const std::span<const std::byte> s) { return read(fix, s) < 2.0; });
    }
    ASSERT_FALSE(samples.empty()) << topic;
    expect_sample(log.format(topic)->get(), samples.front(), first.at(0));
    expect_sample(log.format(topic)->get(), samples.back(), expected(name, "last_" + key).at(0));
  }
}

void read_velocity_and_attitude(const std::string& name, const ULog& log) {
  expect_sample(log.format("vehicle_attitude")->get(), log.samples("vehicle_attitude", 0).at(0),
                expected(name, "first_attitude").at(0));
  expect_sample(log.format("vehicle_local_position")->get(), log.samples("vehicle_local_position", 0).at(0),
                expected(name, "first_local").at(0));
}

void read_each_status_change(const std::string& name, const ULog& log) {
  const ULogFormat& format = log.format("vehicle_status")->get();
  const ULogField timestamp = find_field(format, "timestamp").value();
  const ULogField arming = find_field(format, "arming_state").value();
  const ULogField nav = find_field(format, "nav_state").value();
  std::vector<std::tuple<std::int64_t, double, double>> changes;
  for (const std::span<const std::byte> sample : log.samples("vehicle_status", 0)) {
    const std::tuple<std::int64_t, double, double> now{read_integer(timestamp, sample).value(),
                                                        read(arming, sample).value(), read(nav, sample).value()};
    if (changes.empty() || std::get<1>(changes.back()) != std::get<1>(now) ||
        std::get<2>(changes.back()) != std::get<2>(now)) {
      changes.push_back(now);
    }
  }
  const std::vector<Expected> lines = expected(name, "status");
  ASSERT_EQ(changes.size(), lines.size());
  for (std::size_t i = 0; i < lines.size(); ++i) {
    EXPECT_EQ(std::get<0>(changes[i]), std::stoll(lines[i].values.at(0)));
    EXPECT_EQ(std::get<1>(changes[i]), std::stod(lines[i].values.at(1)));
    EXPECT_EQ(std::get<2>(changes[i]), std::stod(lines[i].values.at(2)));
  }
}

TEST(ULogSamples, HoldEverySampleOfEveryTopic) { for_each_log(hold_every_sample_of_every_topic); }

TEST(ULogSamples, ReadTheLoggedStrings) { for_each_log(read_the_logged_strings); }

TEST(ULogSamples, ReadPositionsGnssVelocityAndAttitudeAsPyulogDoes) {
  for_each_log(read_positions_and_gnss);
  for_each_log(read_velocity_and_attitude);
}

TEST(ULogSamples, ReadEachChangeOfArmingAndNavigationState) { for_each_log(read_each_status_change); }

}  // namespace
}  // namespace ics::flightlog
