#include "ics/config/config.hpp"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <tl/expected.hpp>
#include <toml++/toml.hpp>

#include "ics/config/subset.hpp"

namespace ics::config {
namespace {

tl::unexpected<Errors> file_error(std::string message) {
  return tl::unexpected(Errors{ConfigError{"", std::move(message)}});
}

std::string describe(const toml::parse_error& error) {
  const toml::source_position& where = error.source().begin;
  std::string message = "line " + std::to_string(where.line) + ", column " + std::to_string(where.column) + ": ";
  message.append(error.description());
  return message;
}

}  // namespace

tl::expected<Table, Errors> parse(const std::string_view text) {
  if (text.size() > kMaxConfigBytes) {
    return file_error("is larger than the limit of " + std::to_string(kMaxConfigBytes) + " bytes");
  }
  // toml++ 3.4.0 has undefined behaviour on some invalid TOML (found by
  // fuzz/config_fuzz.cpp), so it only ever sees text in the ICS subset.
  std::optional<std::string> outside = subset_error(text);
  if (outside.has_value()) {
    return file_error(std::move(*outside));
  }
  toml::parse_result result = toml::parse(text);
  if (!result) {
    return file_error(describe(result.error()));
  }
  return std::move(result).table();
}

tl::expected<Table, Errors> load(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  // is_regular_file is false for a directory, and on any error.
  std::error_code error;
  if (!file.is_open() || !std::filesystem::is_regular_file(path, error)) {
    return file_error("cannot be read as a file");
  }
  // One byte past the limit is enough for parse to reject the file.
  std::string text(kMaxConfigBytes + 1, '\0');
  file.read(text.data(), static_cast<std::streamsize>(text.size()));
  text.resize(static_cast<std::size_t>(file.gcount()));
  return parse(text);
}

std::string format_errors(const std::string_view source, const Errors& errors) {
  std::string text;
  for (const ConfigError& error : errors) {
    text.append(source).append(": ");
    if (!error.field.empty()) {
      text.append(error.field).append(": ");
    }
    text.append(error.message).push_back('\n');
  }
  return text;
}

}  // namespace ics::config
