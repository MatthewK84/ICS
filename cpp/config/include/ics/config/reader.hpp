#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <tl/expected.hpp>

#include "ics/common/units.hpp"
#include "ics/config/config.hpp"

namespace ics::config {

// One allowed value of a setting chosen from a fixed set, and what it means.
template <typename T>
struct Choice {
  std::string_view name;
  T value;
};

namespace detail {
struct ReadState;
}  // namespace detail

// Reads one TOML table against a schema written as code (ICS-016). Each getter
// names a key and the type, range or set of values its setting must have.
// Every setting is required.
//
// A getter never fails. On a missing or invalid setting it records an error
// naming the field and returns a placeholder, so one pass finds every problem.
// Use no value read until finish() returns no errors; read() below does this.
// The table must outlive the reader, and each section is read once.
class Reader {
 public:
  explicit Reader(const Table& table);

  // An integer from min to max.
  [[nodiscard]] std::int64_t integer(std::string_view key, std::int64_t min, std::int64_t max);
  // A number from min to max. An integer setting, such as 5, reads as 5.0.
  [[nodiscard]] double number(std::string_view key, double min, double max);
  [[nodiscard]] bool boolean(std::string_view key);
  // Text that is not empty.
  [[nodiscard]] std::string text(std::string_view key);
  // A list of min_count to max_count texts, none of them empty, such as
  // interfaces = ["tap0", "tap1"] (ICS-020).
  [[nodiscard]] std::vector<std::string> texts(std::string_view key, std::size_t min_count, std::size_t max_count);

  // Settings with a unit carry it in their name, as proto fields do: the key
  // must end in _ns, _m, _rad or _deg. A key without it is a schema bug.
  [[nodiscard]] Duration duration(std::string_view key, Duration min, Duration max);
  [[nodiscard]] Meters meters(std::string_view key, Meters min, Meters max);
  [[nodiscard]] Radians radians(std::string_view key, Radians min, Radians max);
  [[nodiscard]] Degrees degrees(std::string_view key, Degrees min, Degrees max);

  // The value of the choice whose name the setting holds.
  template <typename T, std::size_t N>
    requires(N > 0)
  [[nodiscard]] T choice(const std::string_view key, const std::array<Choice<T>, N>& choices) {
    std::array<std::string_view, N> names{};
    std::ranges::transform(choices, names.begin(), &Choice<T>::name);
    return choices[choose(key, names)].value;
  }

  // The reader of the table under key, such as [log].
  [[nodiscard]] Reader section(std::string_view key);

  // Every error so far, then one for each setting in a table this reader or a
  // section of it read that no getter asked for: it is unknown, likely a typo.
  [[nodiscard]] Errors finish() const;

 private:
  Reader(std::shared_ptr<detail::ReadState> state, std::size_t visit);

  [[nodiscard]] std::string path(std::string_view key) const;
  void add_error(std::string_view key, std::string message);
  void require_unit(std::string_view key, std::string_view suffix);
  [[nodiscard]] const toml::node* find(std::string_view key);
  [[nodiscard]] std::optional<std::string> string_value(std::string_view key);
  [[nodiscard]] std::vector<std::string> text_list(std::string_view key, const toml::array& list,
                                                   std::size_t min_count, std::size_t max_count);
  [[nodiscard]] std::size_t choose(std::string_view key, std::span<const std::string_view> names);

  std::shared_ptr<detail::ReadState> state_;
  std::size_t visit_;
};

// The config a schema builds: what Schema returns when called with a Reader&.
template <typename Schema>
using ConfigOf = std::invoke_result_t<Schema, Reader&>;

// Reads table with schema, a function that takes a Reader& and returns the
// config it builds. Fails with every error found.
template <typename Schema>
[[nodiscard]] tl::expected<ConfigOf<Schema>, Errors> read(const Table& table, Schema schema) {
  Reader reader(table);
  ConfigOf<Schema> config = schema(reader);
  Errors errors = reader.finish();
  if (!errors.empty()) {
    return tl::unexpected(std::move(errors));
  }
  return config;
}

// Loads the file at path and reads it with schema: a service's whole config
// step at start-up. On failure the service prints format_errors and exits.
template <typename Schema>
[[nodiscard]] tl::expected<ConfigOf<Schema>, Errors> read_file(const std::filesystem::path& path, Schema schema) {
  const tl::expected<Table, Errors> table = load(path);
  if (!table) {
    return tl::unexpected(table.error());
  }
  return read(*table, schema);
}

}  // namespace ics::config
