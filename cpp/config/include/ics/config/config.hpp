#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <tl/expected.hpp>
#include <toml++/toml.hpp>

namespace ics::config {

// One problem with a config file (ICS-016). field is the dotted path of the
// setting, such as "log.level", or empty when the problem is with the file as
// a whole, such as a syntax error.
struct ConfigError {
  std::string field;
  std::string message;

  // Written out rather than defaulted: Clang 17's coverage miscounts the
  // branches of a defaulted comparison, and the coverage gate needs them.
  [[nodiscard]] friend bool operator==(const ConfigError& a, const ConfigError& b) noexcept {
    return a.field == b.field && a.message == b.message;
  }
};

using Errors = std::vector<ConfigError>;

// A parsed TOML document.
using Table = toml::table;

// The largest config file accepted, so a wrong path cannot load a huge file.
inline constexpr std::size_t kMaxConfigBytes = std::size_t{1} << 20U;

// Parses TOML text in the ICS subset (see subset.hpp). A syntax error names
// its line and column.
[[nodiscard]] tl::expected<Table, Errors> parse(std::string_view text);

// Reads and parses the TOML file at path.
[[nodiscard]] tl::expected<Table, Errors> load(const std::filesystem::path& path);

// The errors for an operator, one per line: "<source>: <field>: <message>".
[[nodiscard]] std::string format_errors(std::string_view source, const Errors& errors);

}  // namespace ics::config
