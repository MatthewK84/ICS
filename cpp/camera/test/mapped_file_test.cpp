#include "ics/camera/mapped_file.hpp"

#include <unistd.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "ics/common/error.hpp"

namespace ics::camera {
namespace {

// A directory of its own for each test, removed after it.
class MappedFileTest : public testing::Test {
 protected:
  void SetUp() override {
    dir_ = std::filesystem::temp_directory_path() /
           ("ics-mapped-" + std::to_string(::getpid()) + "-" + testing::UnitTest::GetInstance()->current_test_info()->name());
    std::filesystem::create_directories(dir_);
  }
  void TearDown() override { std::filesystem::remove_all(dir_); }

  std::filesystem::path file(const std::string& name, const std::string& text) const {
    const std::filesystem::path path = dir_ / name;
    std::ofstream(path, std::ios::binary) << text;
    return path;
  }

  std::filesystem::path dir_;
};

TEST_F(MappedFileTest, MapsAFilesBytes) {
  const Result<MappedFile> mapped = MappedFile::open(file("a", "cine"));
  ASSERT_TRUE(mapped.has_value());
  ASSERT_EQ(mapped->bytes().size(), 4U);
  EXPECT_EQ(mapped->bytes()[0], std::byte{'c'});
}

TEST_F(MappedFileTest, MapsAnEmptyFileAsNoBytes) {
  const Result<MappedFile> mapped = MappedFile::open(file("empty", ""));
  ASSERT_TRUE(mapped.has_value());
  EXPECT_TRUE(mapped->bytes().empty());
}

TEST_F(MappedFileTest, RefusesWhatItCannotOpenOrMap) {
  EXPECT_EQ(MappedFile::open(dir_ / "missing").error(), Error::kUnreadable);
  // A directory with an entry has a size on every common file system, and
  // mmap refuses it.
  static_cast<void>(file("entry", "x"));
  EXPECT_EQ(MappedFile::open(dir_).error(), Error::kUnreadable);
}

TEST_F(MappedFileTest, MovesItsMapping) {
  Result<MappedFile> first = MappedFile::open(file("a", "abc"));
  Result<MappedFile> second = MappedFile::open(file("b", "de"));
  ASSERT_TRUE(first.has_value() && second.has_value());
  MappedFile moved(std::move(*first));
  EXPECT_EQ(moved.bytes().size(), 3U);
  EXPECT_TRUE(first->bytes().empty());
  moved = std::move(*second);
  EXPECT_EQ(moved.bytes().size(), 2U);
  MappedFile& same = moved;
  moved = std::move(same);
  EXPECT_EQ(moved.bytes().size(), 2U);
}

}  // namespace
}  // namespace ics::camera
