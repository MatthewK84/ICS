#include "ics/config/logging_config.hpp"

#include <string>

#include <gtest/gtest.h>
#include <tl/expected.hpp>

#include "ics/config/config.hpp"
#include "ics/config/reader.hpp"
#include "ics/logging/json_line.hpp"
#include "temp_file.hpp"

namespace {

using ics::config::ConfigError;
using ics::config::Errors;
using ics::config::LoggingConfig;
using ics::config::testing::TempFile;
using ics::logging::Level;

tl::expected<LoggingConfig, Errors> read_text(const std::string& text) {
  const TempFile file(text);
  return ics::config::read_file(file.path(), &ics::config::read_logging);
}

TEST(LoggingConfig, ReadsTheLogTable) {
  const tl::expected<LoggingConfig, Errors> config = read_text("[log]\nlevel = \"warn\"\nservice = \"ics-timingd\"\n");
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->level, Level::kWarn);
  EXPECT_EQ(config->service, "ics-timingd");
}

TEST(LoggingConfig, AcceptsEveryLevelByItsLogName) {
  for (const Level level : {Level::kDebug, Level::kInfo, Level::kWarn, Level::kError}) {
    const std::string name(ics::logging::to_string(level));
    const tl::expected<LoggingConfig, Errors> config =
        read_text("[log]\nlevel = \"" + name + "\"\nservice = \"ics-test\"\n");
    ASSERT_TRUE(config.has_value()) << name;
    EXPECT_EQ(config->level, level) << name;
  }
}

// The issue's "Done when": invalid config fails fast and names the field.
TEST(LoggingConfig, NamesEveryInvalidField) {
  const tl::expected<LoggingConfig, Errors> config =
      read_text("[log]\nlevel = \"verbose\"\nservice = \"\"\ncolour = true\n[extra]\n");
  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(ics::config::format_errors("app.toml", config.error()),
            "app.toml: log.level: must be one of debug, info, warn, error, not \"verbose\"\n"
            "app.toml: log.service: must not be empty\n"
            "app.toml: extra: is not a known setting\n"
            "app.toml: log.colour: is not a known setting\n");
}

TEST(LoggingConfig, RequiresTheLogTable) {
  const tl::expected<LoggingConfig, Errors> config = read_text("");
  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error(), (Errors{ConfigError{"log", "is required"}}));
}

TEST(LoggingConfig, FailsOnAFileThatCannotBeRead) {
  const tl::expected<LoggingConfig, Errors> config =
      ics::config::read_file("/nonexistent/app.toml", &ics::config::read_logging);
  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error(), (Errors{ConfigError{"", "cannot be read as a file"}}));
}

}  // namespace
