// The DataFlash sample log (logs/README.md), read with
// ics::flightlog::DataFlash and compared with what pymavlink read in it
// (logs/expected.tsv).
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "fixtures.hpp"
#include "ics/flightlog/dataflash.hpp"

namespace ics::flightlog {
namespace {

using testing::expected;
using testing::Expected;

const std::string kLog = "ardupilot-crossing.bin";

class DataFlashSamples : public ::testing::Test {
 protected:
  void SetUp() override {
    bytes_ = testing::log_file(kLog);
    Result<DataFlash> parsed = DataFlash::parse(bytes_);
    ASSERT_TRUE(parsed.has_value());
    log_ = std::move(parsed).value();
  }
  [[nodiscard]] const DataFlash& log() const { return *log_; }

  [[nodiscard]] double value(const std::string& type, const std::span<const std::byte> message,
                             const std::string& column) const {
    return read(find_column(log().format(type)->get(), column).value(), message).value();
  }

  // Each NAME=VALUE of an expectation matches the message.
  void expect_message(const std::string& type, const std::span<const std::byte> message, const Expected& line) const {
    for (const std::string& field : line.values) {
      const std::size_t equals = field.find('=');
      EXPECT_EQ(value(type, message, field.substr(0, equals)), std::stod(field.substr(equals + 1))) << type << " " << field;
    }
  }

 private:
  std::vector<std::byte> bytes_;
  std::optional<DataFlash> log_;
};

TEST_F(DataFlashSamples, HoldsEveryMessageOfEveryType) {
  EXPECT_EQ(log().counts().skipped_bytes, 0U);
  EXPECT_EQ(log().counts().bad_formats, 0U);
  EXPECT_FALSE(log().counts().truncated);
  for (const auto& [type, count] : testing::expected_counts(kLog)) {
    EXPECT_EQ(log().messages(type).size(), count) << type;
  }
}

TEST_F(DataFlashSamples, ReadsPositionsAsPymavlinkDoes) {
  const std::vector<std::span<const std::byte>> positions = log().messages("POS");
  expect_message("POS", positions.front(), expected(kLog, "first_pos").at(0));
  expect_message("POS", positions.back(), expected(kLog, "last_pos").at(0));
}

TEST_F(DataFlashSamples, ReadsGpsTimesAsPymavlinkDoes) {
  std::vector<std::span<const std::byte>> timed;
  std::size_t fixed = 0;
  for (const std::span<const std::byte> gps : log().messages("GPS")) {
    fixed += value("GPS", gps, "Status") >= 2.0 ? 1U : 0U;
    if (value("GPS", gps, "GWk") > 0.0) {
      timed.push_back(gps);
    }
  }
  EXPECT_EQ(fixed, std::stoul(testing::expected_value(kLog, "fixed_gps")));
  ASSERT_EQ(timed.size(), std::stoul(testing::expected_value(kLog, "timed_gps")));
  expect_message("GPS", timed.front(), expected(kLog, "first_timed_gps").at(0));
  expect_message("GPS", timed.back(), expected(kLog, "last_timed_gps").at(0));
}

TEST_F(DataFlashSamples, ReadsMessagesModesArmingAndTheSystemId) {
  const std::vector<std::span<const std::byte>> messages = log().messages("MSG");
  ASSERT_EQ(messages.size(), std::stoul(testing::expected_value(kLog, "strings")));
  const DataFlashFormat& msg = log().format("MSG")->get();
  const Expected first = expected(kLog, "first_string").at(0);
  EXPECT_EQ(value("MSG", messages[0], "TimeUS"), std::stod(first.values.at(0)));
  EXPECT_EQ(read_text(find_column(msg, "Message").value(), messages[0]), first.values.at(1));
  const std::vector<Expected> modes = expected(kLog, "mode");
  ASSERT_EQ(log().messages("MODE").size(), modes.size());
  for (std::size_t i = 0; i < modes.size(); ++i) {
    EXPECT_EQ(value("MODE", log().messages("MODE")[i], "TimeUS"), std::stod(modes[i].values.at(0)));
    EXPECT_EQ(value("MODE", log().messages("MODE")[i], "ModeNum"), std::stod(modes[i].values.at(1)));
  }
  const Expected arm = expected(kLog, "arm").at(0);
  EXPECT_EQ(value("ARM", log().messages("ARM").at(0), "ArmState"), std::stod(arm.values.at(1)));
  const DataFlashFormat& parm = log().format("PARM")->get();
  std::optional<double> system_id;
  for (const std::span<const std::byte> parameter : log().messages("PARM")) {
    if (!system_id && read_text(find_column(parm, "Name").value(), parameter) == "MAV_SYSID") {
      system_id = value("PARM", parameter, "Value");
    }
  }
  EXPECT_EQ(system_id, std::stod(testing::expected_value(kLog, "system_id")));
}

}  // namespace
}  // namespace ics::flightlog
