#include "ics/lattice/token.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>

#include "ics/common/error.hpp"

namespace ics::lattice {
namespace {

constexpr std::string_view kWhiteSpace = " \t\r\n";
constexpr char kFirstPrintable = '!';
constexpr char kDelete = '\x7F';

}  // namespace

bool valid_token(const std::string_view text) noexcept {
  return !text.empty() &&
         std::ranges::all_of(text, [](const char c) { return c >= kFirstPrintable && c < kDelete; });
}

Result<std::string> read_token(const std::filesystem::path& path) {
  std::error_code error;
  const std::filesystem::file_status status = std::filesystem::status(path, error);
  if (error || !std::filesystem::is_regular_file(status)) {
    return fail(Error::kUnreadable);
  }
  constexpr std::filesystem::perms kShared = std::filesystem::perms::group_all | std::filesystem::perms::others_all;
  if ((status.permissions() & kShared) != std::filesystem::perms::none) {
    return fail(Error::kInvalidArgument);
  }
  std::ifstream in(path, std::ios::binary);
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const std::size_t first = text.find_first_not_of(kWhiteSpace);
  const std::size_t last = text.find_last_not_of(kWhiteSpace);
  const std::string_view token =
      first == std::string::npos ? std::string_view{} : std::string_view(text).substr(first, last - first + 1);
  if (!valid_token(token)) {
    return fail(Error::kInvalidArgument);
  }
  return std::string(token);
}

}  // namespace ics::lattice
