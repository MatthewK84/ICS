#include "ics/lattice/token.hpp"

#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <string_view>
#include <system_error>

#include <gtest/gtest.h>
#include <unistd.h>

#include "ics/common/error.hpp"

namespace ics::lattice {
namespace {

namespace fs = std::filesystem;

// A folder for one test's token files, removed at the end.
class TokenFolder {
 public:
  TokenFolder() : path_(fs::temp_directory_path() / ("ics-lattice-" + std::to_string(::getpid()))) {
    fs::remove_all(path_);
    fs::create_directories(path_);
  }
  ~TokenFolder() {
    std::error_code ignored;
    fs::remove_all(path_, ignored);
  }
  TokenFolder(const TokenFolder&) = delete;
  TokenFolder& operator=(const TokenFolder&) = delete;
  TokenFolder(TokenFolder&&) = delete;
  TokenFolder& operator=(TokenFolder&&) = delete;

  // A file holding text, readable by its owner only unless perms says more.
  [[nodiscard]] fs::path write(const std::string& name, const std::string_view text,
                               const fs::perms perms = fs::perms::owner_read | fs::perms::owner_write) const {
    const fs::path file = path_ / name;
    std::ofstream(file, std::ios::binary) << text;
    fs::permissions(file, perms);
    return file;
  }

  [[nodiscard]] const fs::path& path() const noexcept { return path_; }

 private:
  fs::path path_;
};

TEST(ReadToken, ReadsTheTokenWithoutSurroundingWhiteSpace) {
  const TokenFolder folder;
  EXPECT_EQ(read_token(folder.write("plain", "abc.DEF-123_~")).value(), "abc.DEF-123_~");
  EXPECT_EQ(read_token(folder.write("padded", " \t token\r\n")).value(), "token");
  EXPECT_EQ(read_token(folder.write("owner-only", "t", fs::perms::owner_read)).value(), "t");
}

TEST(ReadToken, RefusesAFileOthersCanReachOrThatHoldsNoToken) {
  const TokenFolder folder;
  EXPECT_EQ(read_token(folder.path() / "missing").error(), Error::kUnreadable);
  EXPECT_EQ(read_token(folder.path()).error(), Error::kUnreadable);
  for (const fs::perms shared : {fs::perms::group_read, fs::perms::others_read, fs::perms::group_write,
                                 fs::perms::others_exec}) {
    EXPECT_EQ(read_token(folder.write("shared", "token", fs::perms::owner_read | shared)).error(),
              Error::kInvalidArgument);
  }
  for (const std::string_view text : std::initializer_list<std::string_view>{"", " \n", "two words", "tab\tinside", "line\r\nX-Injected: yes",
                                      "caf\xC3\xA9", std::string_view("nul\0byte", 8), "del\x7F"}) {
    EXPECT_EQ(read_token(folder.write("bad", text)).error(), Error::kInvalidArgument) << text;
  }
}

TEST(ValidToken, AllowsOnlyPrintableAsciiWithoutSpaces) {
  EXPECT_TRUE(valid_token("!~"));
  EXPECT_FALSE(valid_token(""));
  EXPECT_FALSE(valid_token(" "));
  EXPECT_FALSE(valid_token("\x7F"));
  EXPECT_FALSE(valid_token("\x80"));
}

}  // namespace
}  // namespace ics::lattice
