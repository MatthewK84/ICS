#pragma once

#include <unistd.h>

#include <filesystem>
#include <string>

#include <gtest/gtest.h>

namespace ics::camera::testing_support {

// A directory of its own for the running test, removed with it.
class ScratchDir {
 public:
  ScratchDir()
      : path_(std::filesystem::temp_directory_path() /
              ("ics-camera-" + std::to_string(::getpid()) + "-" +
               ::testing::UnitTest::GetInstance()->current_test_info()->test_suite_name() + "-" +
               ::testing::UnitTest::GetInstance()->current_test_info()->name())) {
    std::filesystem::create_directories(path_);
  }
  ScratchDir(const ScratchDir&) = delete;
  ScratchDir& operator=(const ScratchDir&) = delete;
  ScratchDir(ScratchDir&&) = delete;
  ScratchDir& operator=(ScratchDir&&) = delete;
  ~ScratchDir() { std::filesystem::remove_all(path_); }

  [[nodiscard]] const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

}  // namespace ics::camera::testing_support
