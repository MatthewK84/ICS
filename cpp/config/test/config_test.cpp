#include "ics/config/config.hpp"

#include <filesystem>
#include <string>

#include <gtest/gtest.h>
#include <tl/expected.hpp>

#include "temp_file.hpp"

namespace {

using ics::config::ConfigError;
using ics::config::Errors;
using ics::config::Table;
using ics::config::testing::TempFile;

const std::string kTooLarge = "is larger than the limit of 1048576 bytes";

TEST(ConfigError, ComparesTheFieldAndTheMessage) {
  const ConfigError error{"log.level", "is required"};
  EXPECT_EQ(error, (ConfigError{"log.level", "is required"}));
  EXPECT_NE(error, (ConfigError{"log.service", "is required"}));
  EXPECT_NE(error, (ConfigError{"log.level", "must be text"}));
}

TEST(Parse, ReadsATomlDocument) {
  const tl::expected<Table, Errors> table = ics::config::parse("[log]\nlevel = \"info\"\n");
  ASSERT_TRUE(table.has_value());
  EXPECT_EQ(table->at_path("log.level").value_or(std::string()), "info");
}

TEST(Parse, NamesTheLineAndColumnOfASyntaxError) {
  const tl::expected<Table, Errors> table = ics::config::parse("[log]\nlevel = \"info\"\nlevel = \"warn\"\n");
  ASSERT_FALSE(table.has_value());
  EXPECT_EQ(table.error(), (Errors{ConfigError{
                               "", "line 3, column 9: Error while parsing key-value pair: cannot redefine existing "
                                   "string 'level'"}}));
}

TEST(Parse, AcceptsTextUpToTheLimit) {
  const std::string comment = "#" + std::string(ics::config::kMaxConfigBytes - 1, 'x');
  EXPECT_TRUE(ics::config::parse(comment).has_value());
  const tl::expected<Table, Errors> table = ics::config::parse(comment + "x");
  ASSERT_FALSE(table.has_value());
  EXPECT_EQ(table.error(), (Errors{ConfigError{"", kTooLarge}}));
}

TEST(Load, ReadsAFile) {
  const TempFile file("[log]\nservice = \"ics-test\"\n");
  const tl::expected<Table, Errors> table = ics::config::load(file.path());
  ASSERT_TRUE(table.has_value());
  EXPECT_EQ(table->at_path("log.service").value_or(std::string()), "ics-test");
}

TEST(Load, RejectsAMissingFileOrAFolder) {
  const Errors expected{ConfigError{"", "cannot be read as a file"}};
  const tl::expected<Table, Errors> missing = ics::config::load("/nonexistent/ics.toml");
  ASSERT_FALSE(missing.has_value());
  EXPECT_EQ(missing.error(), expected);
  const tl::expected<Table, Errors> folder = ics::config::load(std::filesystem::temp_directory_path());
  ASSERT_FALSE(folder.has_value());
  EXPECT_EQ(folder.error(), expected);
}

TEST(Load, RejectsAFileOverTheLimit) {
  const TempFile file("#" + std::string(ics::config::kMaxConfigBytes, 'x'));
  const tl::expected<Table, Errors> table = ics::config::load(file.path());
  ASSERT_FALSE(table.has_value());
  EXPECT_EQ(table.error(), (Errors{ConfigError{"", kTooLarge}}));
}

TEST(FormatErrors, WritesOneLinePerError) {
  const Errors errors{ConfigError{"log.level", "is required"}, ConfigError{"", "line 1, column 2: oops"}};
  EXPECT_EQ(ics::config::format_errors("app.toml", errors),
            "app.toml: log.level: is required\napp.toml: line 1, column 2: oops\n");
  EXPECT_EQ(ics::config::format_errors("app.toml", {}), "");
}

}  // namespace
