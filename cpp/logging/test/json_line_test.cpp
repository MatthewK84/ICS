#include "ics/logging/json_line.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "ics/common/units.hpp"

namespace {

using ics::logging::Field;
using ics::logging::Level;

// 2026-10-01T09:53:50.123456789Z.
constexpr std::int64_t kUtcNs = 1'790'848'430'123'456'789;
constexpr std::string_view kPrefix =
    R"({"ts":"2026-10-01T09:53:50.123456789Z","level":"info","service":"ics-test","event":"started")";

// The line for one field, with everything before it checked.
std::string line_with(const Field& field) {
  const std::array<Field, 1> fields{field};
  const std::string line = ics::logging::format_line(ics::utc_from_ns(kUtcNs), Level::kInfo, "ics-test", "started",
                                                     fields);
  EXPECT_EQ(line.substr(0, kPrefix.size()), kPrefix);
  return line.substr(kPrefix.size());
}

TEST(FormatUtc, WritesRfc3339WithNanoseconds) {
  EXPECT_EQ(ics::logging::format_utc(ics::utc_from_ns(kUtcNs)), "2026-10-01T09:53:50.123456789Z");
  EXPECT_EQ(ics::logging::format_utc(ics::utc_from_ns(0)), "1970-01-01T00:00:00.000000000Z");
  EXPECT_EQ(ics::logging::format_utc(ics::utc_from_ns(-1)), "1969-12-31T23:59:59.999999999Z");
  // 2024-02-29T23:59:59.5Z, a leap day.
  EXPECT_EQ(ics::logging::format_utc(ics::utc_from_ns(1'709'251'199'500'000'000)), "2024-02-29T23:59:59.500000000Z");
}

TEST(FormatUtc, CoversTheWholeInt64Range) {
  EXPECT_EQ(ics::logging::format_utc(ics::utc_from_ns(std::numeric_limits<std::int64_t>::min())),
            "1677-09-21T00:12:43.145224192Z");
  EXPECT_EQ(ics::logging::format_utc(ics::utc_from_ns(std::numeric_limits<std::int64_t>::max())),
            "2262-04-11T23:47:16.854775807Z");
}

TEST(Level, NamesEveryLevel) {
  EXPECT_EQ(ics::logging::to_string(Level::kDebug), "debug");
  EXPECT_EQ(ics::logging::to_string(Level::kInfo), "info");
  EXPECT_EQ(ics::logging::to_string(Level::kWarn), "warn");
  EXPECT_EQ(ics::logging::to_string(Level::kError), "error");
  EXPECT_EQ(ics::logging::to_string(static_cast<Level>(9)), "unknown");
}

TEST(FormatLine, WritesTheCommonKeysInOrder) {
  const std::string line =
      ics::logging::format_line(ics::utc_from_ns(kUtcNs), Level::kWarn, "ics-timingd", "clock_lost", {});
  EXPECT_EQ(line,
            R"({"ts":"2026-10-01T09:53:50.123456789Z","level":"warn","service":"ics-timingd","event":"clock_lost"})");
}

TEST(FormatLine, WritesEveryValueType) {
  EXPECT_EQ(line_with({"frames", 12}), R"(,"frames":12})");
  EXPECT_EQ(line_with({"offset_ns", std::numeric_limits<std::int64_t>::min()}),
            R"(,"offset_ns":-9223372036854775808})");
  EXPECT_EQ(line_with({"ratio", 0.1}), R"(,"ratio":0.1})");
  EXPECT_EQ(line_with({"height_m", -1.5e300}), R"(,"height_m":-1.5e+300})");
  EXPECT_EQ(line_with({"locked", true}), R"(,"locked":true})");
  EXPECT_EQ(line_with({"locked", false}), R"(,"locked":false})");
  EXPECT_EQ(line_with({"station", "north"}), R"(,"station":"north"})");
}

TEST(FormatLine, WritesNonFiniteNumbersAsStrings) {
  EXPECT_EQ(line_with({"ratio", std::numeric_limits<double>::quiet_NaN()}), R"(,"ratio":"NaN"})");
  EXPECT_EQ(line_with({"ratio", std::numeric_limits<double>::infinity()}), R"(,"ratio":"Infinity"})");
  EXPECT_EQ(line_with({"ratio", -std::numeric_limits<double>::infinity()}), R"(,"ratio":"-Infinity"})");
}

TEST(FormatLine, EscapesText) {
  EXPECT_EQ(line_with({"path", R"(C:\a "b")"}), R"(,"path":"C:\\a \"b\""})");
  EXPECT_EQ(line_with({"text", "a\nb\rc\td"}), R"(,"text":"a\nb\rc\td"})");
  EXPECT_EQ(line_with({"text", std::string_view("\x01\x1f\x7f", 3)}), ",\"text\":\"\\u0001\\u001f\x7f\"}");
  EXPECT_EQ(line_with({"text", std::string_view("\0", 1)}), R"(,"text":"\u0000"})");
  // UTF-8 passes through unchanged.
  EXPECT_EQ(line_with({"text", "\xc3\xa9t\xc3\xa9"}), ",\"text\":\"\xc3\xa9t\xc3\xa9\"}");
}

TEST(FormatLine, EscapesTheServiceAndEvent) {
  const std::string line =
      ics::logging::format_line(ics::utc_from_ns(0), Level::kError, "a\"b", "line\nbreak", {});
  EXPECT_EQ(line, R"({"ts":"1970-01-01T00:00:00.000000000Z","level":"error","service":"a\"b","event":"line\nbreak"})");
}

TEST(FormatLine, LeavesOutAFieldWithAnInvalidKeyAndReportsIt) {
  for (const std::string_view key : {"", "9lives", "Upper", "kebab-case", "ts", "level", "service", "event"}) {
    ::testing::internal::CaptureStderr();
    const std::string rest = line_with({key, 1});
    const std::string report = ::testing::internal::GetCapturedStderr();
    EXPECT_EQ(rest, "}") << key;
    EXPECT_NE(report.find("ICS check failed at "), std::string::npos) << key;
  }
}

TEST(FormatLine, KeepsTheValidFieldsAroundAnInvalidOne) {
  const std::array<Field, 3> fields{Field{"a1", 1}, Field{"Bad", 2}, Field{"snake_case_2", 3}};
  ::testing::internal::CaptureStderr();
  const std::string line = ics::logging::format_line(ics::utc_from_ns(0), Level::kDebug, "s", "e", fields);
  ::testing::internal::GetCapturedStderr();
  EXPECT_EQ(line, R"({"ts":"1970-01-01T00:00:00.000000000Z","level":"debug","service":"s","event":"e",)"
                  R"("a1":1,"snake_case_2":3})");
}

}  // namespace
