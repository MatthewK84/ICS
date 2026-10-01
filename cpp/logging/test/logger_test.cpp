#include "ics/logging/logger.hpp"

#include <chrono>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "ics/common/units.hpp"

namespace {

using ics::logging::Level;
using ics::logging::Logger;

constexpr std::int64_t kUtcNs = 1'790'848'430'123'456'789;

ics::UtcTime fixed_clock() noexcept { return ics::utc_from_ns(kUtcNs); }

std::string line(const std::string_view level, const std::string_view event, const std::string_view rest) {
  std::string text = R"({"ts":"2026-10-01T09:53:50.123456789Z","level":")";
  text.append(level).append(R"(","service":"ics-test","event":")").append(event).append("\"");
  text.append(rest).append("}\n");
  return text;
}

TEST(Logger, WritesOneJsonLinePerEvent) {
  std::ostringstream out;
  const Logger logger = Logger::to_stream("ics-test", Level::kDebug, out, &fixed_clock);
  logger.info("frame_dropped", {{"camera", "north"}, {"count", 3}});
  logger.error("disk_full");
  EXPECT_EQ(out.str(),
            line("info", "frame_dropped", R"(,"camera":"north","count":3)") + line("error", "disk_full", ""));
}

TEST(Logger, WritesEachLevel) {
  std::ostringstream out;
  const Logger logger = Logger::to_stream("ics-test", Level::kDebug, out, &fixed_clock);
  logger.debug("a");
  logger.info("b");
  logger.warn("c");
  logger.error("d");
  EXPECT_EQ(out.str(), line("debug", "a", "") + line("info", "b", "") + line("warn", "c", "") + line("error", "d", ""));
}

TEST(Logger, SkipsEventsBelowTheThreshold) {
  std::ostringstream out;
  const Logger logger = Logger::to_stream("ics-test", Level::kWarn, out, &fixed_clock);
  EXPECT_EQ(logger.threshold(), Level::kWarn);
  EXPECT_FALSE(logger.enabled(Level::kInfo));
  EXPECT_TRUE(logger.enabled(Level::kWarn));
  logger.debug("hidden");
  logger.info("hidden");
  logger.warn("shown", {{"ok", true}});
  EXPECT_EQ(out.str(), line("warn", "shown", R"(,"ok":true)"));
}

TEST(Logger, NamesALevelOutsideTheEnumUnknown) {
  std::ostringstream out;
  const Logger logger = Logger::to_stream("ics-test", Level::kDebug, out, &fixed_clock);
  logger.log(static_cast<Level>(9), "odd", {});
  EXPECT_EQ(out.str(), line("unknown", "odd", ""));
}

TEST(Logger, FallsBackToTheSystemClockWithoutAClock) {
  std::ostringstream out;
  ::testing::internal::CaptureStderr();
  const Logger logger = Logger::to_stream("ics-test", Level::kInfo, out, nullptr);
  const std::string report = ::testing::internal::GetCapturedStderr();
  EXPECT_NE(report.find("ICS check failed at "), std::string::npos) << report;
  const ics::UtcTime before = ics::logging::system_now();
  logger.info("started");
  EXPECT_EQ(out.str().rfind(R"({"ts":")", 0), 0U) << out.str();
  // The year is the system clock's, not the fixed clock's.
  const std::chrono::year_month_day date{std::chrono::floor<std::chrono::days>(before)};
  EXPECT_EQ(out.str().substr(7, 4), std::to_string(static_cast<int>(date.year())));
}

TEST(Logger, WritesToStderr) {
  const Logger logger = Logger::to_stderr("ics-test", Level::kInfo);
  ::testing::internal::CaptureStderr();
  logger.info("started", {{"version", "0.1.0"}});
  const std::string written = ::testing::internal::GetCapturedStderr();
  EXPECT_EQ(written.rfind(R"({"ts":")", 0), 0U) << written;
  EXPECT_NE(written.find(R"("service":"ics-test","event":"started","version":"0.1.0"})"), std::string::npos)
      << written;
  EXPECT_EQ(written.back(), '\n');
}

}  // namespace
