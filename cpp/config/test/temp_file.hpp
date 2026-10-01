#pragma once

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

#include <gtest/gtest.h>

namespace ics::config::testing {

// A file holding text in the temporary folder, named after the running test,
// and removed with this object.
class TempFile {
 public:
  explicit TempFile(const std::string_view text) : path_(unique_path()) {
    std::ofstream file(path_, std::ios::binary);
    file << text;
    EXPECT_TRUE(file.good()) << path_;
  }
  TempFile(const TempFile&) = delete;
  TempFile& operator=(const TempFile&) = delete;
  TempFile(TempFile&&) = delete;
  TempFile& operator=(TempFile&&) = delete;
  ~TempFile() {
    std::error_code error;
    std::filesystem::remove(path_, error);
  }

  [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

 private:
  static std::filesystem::path unique_path() {
    const ::testing::TestInfo* test = ::testing::UnitTest::GetInstance()->current_test_info();
    std::string name = "ics_config_";
    name.append(test->test_suite_name()).append("_").append(test->name()).append(".toml");
    return std::filesystem::temp_directory_path() / name;
  }

  std::filesystem::path path_;
};

}  // namespace ics::config::testing
