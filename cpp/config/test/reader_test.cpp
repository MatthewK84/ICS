#include "ics/config/reader.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

#include <gtest/gtest.h>
#include <tl/expected.hpp>

#include "ics/common/units.hpp"
#include "ics/config/config.hpp"

namespace {

using ics::config::Choice;
using ics::config::ConfigError;
using ics::config::Errors;
using ics::config::Reader;
using ics::config::Table;

Table table_of(const std::string_view text) {
  tl::expected<Table, Errors> table = ics::config::parse(text);
  EXPECT_TRUE(table.has_value()) << text;
  return table.has_value() ? std::move(*table) : Table();
}

Errors one(const std::string_view field, const std::string_view message) {
  return Errors{ConfigError{std::string(field), std::string(message)}};
}

// Expects one failed ics::check while reading, and returns the errors.
template <typename Read>
Errors errors_with_failed_check(const Table& table, Read read) {
  Reader reader(table);
  ::testing::internal::CaptureStderr();
  read(reader);
  const std::string report = ::testing::internal::GetCapturedStderr();
  EXPECT_NE(report.find("ICS check failed at "), std::string::npos) << report;
  return reader.finish();
}

TEST(Reader, ReadsAnIntegerInRange) {
  const Table table = table_of("count = 7");
  Reader reader(table);
  EXPECT_EQ(reader.integer("count", 1, 10), 7);
  EXPECT_EQ(reader.finish(), Errors{});
}

TEST(Reader, RejectsAnIntegerOfTheWrongTypeOrOutOfRange) {
  const Table table = table_of("low = 0\nhigh = 11\ntext = \"7\"\nfloat = 7.0");
  Reader reader(table);
  EXPECT_EQ(reader.integer("low", 1, 10), 1);
  EXPECT_EQ(reader.integer("high", 1, 10), 1);
  EXPECT_EQ(reader.integer("text", 1, 10), 1);
  EXPECT_EQ(reader.integer("float", 1, 10), 1);
  EXPECT_EQ(reader.integer("missing", 1, 10), 1);
  EXPECT_EQ(reader.finish(), (Errors{
                                 {"low", "must be from 1 to 10, not 0"},
                                 {"high", "must be from 1 to 10, not 11"},
                                 {"text", "must be an integer"},
                                 {"float", "must be an integer"},
                                 {"missing", "is required"},
                             }));
}

TEST(Reader, ReadsANumberAndTakesAnIntegerAsOne) {
  const Table table = table_of("ratio = 0.25\nwhole = 2");
  Reader reader(table);
  EXPECT_EQ(reader.number("ratio", 0.0, 1.0), 0.25);
  EXPECT_EQ(reader.number("whole", 0.0, 3.0), 2.0);
  EXPECT_EQ(reader.finish(), Errors{});
}

TEST(Reader, RejectsANumberOfTheWrongTypeOutOfRangeOrNan) {
  const Table table = table_of("low = -0.5\nhigh = 1.5\nnot_a_number = nan\nflag = true");
  Reader reader(table);
  EXPECT_EQ(reader.number("low", 0.0, 1.0), 0.0);
  EXPECT_EQ(reader.number("high", 0.0, 1.0), 0.0);
  EXPECT_EQ(reader.number("not_a_number", 0.0, 1.0), 0.0);
  EXPECT_EQ(reader.number("flag", 0.0, 1.0), 0.0);
  EXPECT_EQ(reader.number("missing", 0.0, 1.0), 0.0);
  EXPECT_EQ(reader.finish(), (Errors{
                                 {"low", "must be from 0 to 1, not -0.5"},
                                 {"high", "must be from 0 to 1, not 1.5"},
                                 {"not_a_number", "must be from 0 to 1, not nan"},
                                 {"flag", "must be a number"},
                                 {"missing", "is required"},
                             }));
}

TEST(Reader, ReportsAnEmptyRangeInTheSchema) {
  EXPECT_EQ(errors_with_failed_check(table_of("count = 1"),
                                     [](Reader& reader) { return reader.integer("count", 2, 1); }),
            one("count", "has an empty range in the schema"));
  EXPECT_EQ(errors_with_failed_check(table_of("ratio = 1.0"),
                                     [](Reader& reader) { return reader.number("ratio", 1.0, 0.0); }),
            one("ratio", "has an empty range in the schema"));
}

TEST(Reader, ReadsABoolean) {
  const Table table = table_of("on = true\noff = false\nnumber = 1");
  Reader reader(table);
  EXPECT_TRUE(reader.boolean("on"));
  EXPECT_FALSE(reader.boolean("off"));
  EXPECT_FALSE(reader.boolean("number"));
  EXPECT_FALSE(reader.boolean("missing"));
  EXPECT_EQ(reader.finish(), (Errors{{"number", "must be true or false"}, {"missing", "is required"}}));
}

TEST(Reader, ReadsTextThatIsNotEmpty) {
  const Table table = table_of("name = \"north\"\nempty = \"\"\nnumber = 3");
  Reader reader(table);
  EXPECT_EQ(reader.text("name"), "north");
  EXPECT_EQ(reader.text("empty"), "");
  EXPECT_EQ(reader.text("number"), "");
  EXPECT_EQ(reader.text("missing"), "");
  EXPECT_EQ(reader.finish(), (Errors{
                                 {"empty", "must not be empty"},
                                 {"number", "must be text"},
                                 {"missing", "is required"},
                             }));
}

TEST(Reader, ReadsAChoiceByName) {
  constexpr std::array<Choice<int>, 3> kSizes{{{"small", 1}, {"medium", 2}, {"large", 3}}};
  const Table table = table_of("size = \"large\"\nwrong = \"huge\"\nnumber = 2");
  Reader reader(table);
  EXPECT_EQ(reader.choice("size", kSizes), 3);
  EXPECT_EQ(reader.choice("wrong", kSizes), 1);
  EXPECT_EQ(reader.choice("number", kSizes), 1);
  EXPECT_EQ(reader.choice("missing", kSizes), 1);
  EXPECT_EQ(reader.finish(), (Errors{
                                 {"wrong", "must be one of small, medium, large, not \"huge\""},
                                 {"number", "must be text"},
                                 {"missing", "is required"},
                             }));
}

TEST(Reader, ReadsSettingsWithUnits) {
  const Table table = table_of("timeout_ns = 5000\nheight_m = 12.5\nheading_rad = 1.5\nlatitude_deg = -33.9");
  Reader reader(table);
  EXPECT_EQ(reader.duration("timeout_ns", ics::Duration(0), ics::Duration(10'000)), ics::Duration(5000));
  EXPECT_EQ(reader.meters("height_m", ics::Meters(0.0), ics::Meters(100.0)), ics::Meters(12.5));
  EXPECT_EQ(reader.radians("heading_rad", ics::Radians(-3.5), ics::Radians(3.5)), ics::Radians(1.5));
  EXPECT_EQ(reader.degrees("latitude_deg", ics::Degrees(-90.0), ics::Degrees(90.0)), ics::Degrees(-33.9));
  EXPECT_EQ(reader.finish(), Errors{});
}

TEST(Reader, ChecksTheRangeOfASettingWithAUnit) {
  const Table table = table_of("timeout_ns = 20000\nheight_m = -1");
  Reader reader(table);
  EXPECT_EQ(reader.duration("timeout_ns", ics::Duration(1), ics::Duration(10'000)), ics::Duration(1));
  EXPECT_EQ(reader.meters("height_m", ics::Meters(0.0), ics::Meters(100.0)), ics::Meters(0.0));
  EXPECT_EQ(reader.finish(), (Errors{
                                 {"timeout_ns", "must be from 1 to 10000, not 20000"},
                                 {"height_m", "must be from 0 to 100, not -1"},
                             }));
}

TEST(Reader, RejectsASchemaKeyWithoutItsUnit) {
  // Each setting is valid; only the schema's name for it is wrong.
  const std::string message = "is read with a unit, so the schema must name it with the suffix ";
  EXPECT_EQ(errors_with_failed_check(
                table_of("timeout = 0"),
                [](Reader& reader) { return reader.duration("timeout", ics::Duration(0), ics::Duration(1)); }),
            one("timeout", message + "_ns"));
  EXPECT_EQ(errors_with_failed_check(
                table_of("height = 0.5"),
                [](Reader& reader) { return reader.meters("height", ics::Meters(0.0), ics::Meters(1.0)); }),
            one("height", message + "_m"));
  EXPECT_EQ(errors_with_failed_check(
                table_of("angle_deg = 0.5"),
                [](Reader& reader) { return reader.radians("angle_deg", ics::Radians(0.0), ics::Radians(1.0)); }),
            one("angle_deg", message + "_rad"));
  EXPECT_EQ(errors_with_failed_check(
                table_of("angle_rad = 0.5"),
                [](Reader& reader) { return reader.degrees("angle_rad", ics::Degrees(0.0), ics::Degrees(1.0)); }),
            one("angle_rad", message + "_deg"));
}

TEST(Reader, NamesNestedFieldsByTheirDottedPath) {
  const Table table = table_of("[camera.lens]\nfocal_length_m = 0.5\naperture = \"wide\"");
  Reader reader(table);
  Reader lens = reader.section("camera").section("lens");
  EXPECT_EQ(lens.meters("focal_length_m", ics::Meters(0.0), ics::Meters(0.2)), ics::Meters(0.0));
  EXPECT_EQ(reader.finish(), (Errors{
                                 {"camera.lens.focal_length_m", "must be from 0 to 0.2, not 0.5"},
                                 {"camera.lens.aperture", "is not a known setting"},
                             }));
}

TEST(Reader, ReportsAMissingSectionOnce) {
  const Table table = table_of("");
  Reader reader(table);
  Reader log = reader.section("log");
  EXPECT_EQ(log.text("service"), "");
  EXPECT_EQ(log.integer("count", 1, 2), 1);
  EXPECT_EQ(reader.finish(), one("log", "is required"));
}

TEST(Reader, RejectsASectionThatIsNotATable) {
  const Table table = table_of("log = 5");
  Reader reader(table);
  Reader log = reader.section("log");
  EXPECT_FALSE(log.boolean("enabled"));
  EXPECT_EQ(reader.finish(), one("log", "must be a table"));
}

TEST(Reader, ReportsEveryUnknownSetting) {
  const Table table = table_of("name = \"a\"\nnmae = \"b\"\n[log]\nlevel = 1\ncolour = true\n[stations]\nnorth = 1");
  Reader reader(table);
  EXPECT_EQ(reader.text("name"), "a");
  Reader log = reader.section("log");
  EXPECT_EQ(log.integer("level", 0, 3), 1);
  const Errors expected{
      {"nmae", "is not a known setting"},
      {"stations", "is not a known setting"},
      {"log.colour", "is not a known setting"},
  };
  EXPECT_EQ(reader.finish(), expected);
  // finish only reports, so a second call gives the same errors.
  EXPECT_EQ(log.finish(), expected);
}

struct Station {
  std::string name;
  ics::Meters height;
};

Station read_station(Reader& reader) {
  return Station{.name = reader.text("name"), .height = reader.meters("height_m", ics::Meters(0.0), ics::Meters(1e4))};
}

TEST(Read, ReturnsTheConfigWhenEverySettingIsValid) {
  const tl::expected<Station, Errors> station = ics::config::read(table_of("name = \"north\"\nheight_m = 12.5"),
                                                                  &read_station);
  ASSERT_TRUE(station.has_value());
  EXPECT_EQ(station->name, "north");
  EXPECT_EQ(station->height, ics::Meters(12.5));
}

TEST(Read, ReturnsEveryErrorOtherwise) {
  const tl::expected<Station, Errors> station = ics::config::read(table_of("height_m = \"high\"\nextra = 1"),
                                                                  &read_station);
  ASSERT_FALSE(station.has_value());
  EXPECT_EQ(station.error(), (Errors{
                                 {"name", "is required"},
                                 {"height_m", "must be a number"},
                                 {"extra", "is not a known setting"},
                             }));
}

}  // namespace
