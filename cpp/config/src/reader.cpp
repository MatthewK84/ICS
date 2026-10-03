#include "ics/config/reader.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <toml++/toml.hpp>

#include "ics/common/check.hpp"
#include "ics/common/units.hpp"
#include "ics/config/config.hpp"

namespace ics::config {
namespace detail {

// A table a reader was made for, and the keys its getters asked for.
struct Visit {
  std::string path;
  const Table* table;  // Null when the section is missing or not a table.
  std::vector<std::string> read_keys;
};

// What a reader and its sections share.
struct ReadState {
  Errors errors;
  std::vector<Visit> visits;
};

}  // namespace detail

namespace {

// Holds the shortest form of any double, which is at most 24 characters.
constexpr std::size_t kDoubleTextBytes = 32;

std::string join(const std::string_view parent, const std::string_view key) {
  if (parent.empty()) {
    return std::string(key);
  }
  std::string path(parent);
  path.append(".").append(key);
  return path;
}

std::string number_text(const double value) {
  std::array<char, kDoubleTextBytes> text{};
  const std::to_chars_result written = std::to_chars(text.begin(), text.end(), value);
  return {text.begin(), written.ptr};
}

std::string range_message(const std::int64_t min, const std::int64_t max, const std::int64_t value) {
  return "must be from " + std::to_string(min) + " to " + std::to_string(max) + ", not " + std::to_string(value);
}

std::string range_message(const double min, const double max, const double value) {
  return "must be from " + number_text(min) + " to " + number_text(max) + ", not " + number_text(value);
}

std::optional<double> number_value(const toml::node& node) {
  const toml::value<std::int64_t>* integer = node.as_integer();
  if (integer != nullptr) {
    return static_cast<double>(integer->get());
  }
  const toml::value<double>* floating = node.as_floating_point();
  if (floating != nullptr) {
    return floating->get();
  }
  return std::nullopt;
}

std::string one_of_message(const std::span<const std::string_view> names, const std::string_view value) {
  std::string message = "must be one of ";
  for (std::size_t index = 0; index < names.size(); ++index) {
    if (index > 0) {
      message.append(", ");
    }
    message.append(names[index]);
  }
  message.append(", not \"").append(value).append("\"");
  return message;
}

void append_unknown_keys(const detail::Visit& visit, Errors& errors) {
  if (visit.table == nullptr) {
    return;
  }
  for (const auto& entry : *visit.table) {
    const std::string_view key = entry.first.str();
    if (std::ranges::find(visit.read_keys, key) == visit.read_keys.end()) {
      errors.push_back(ConfigError{join(visit.path, key), "is not a known setting"});
    }
  }
}

}  // namespace

Reader::Reader(const Table& table) : state_(std::make_shared<detail::ReadState>()), visit_(0) {
  state_->visits.push_back(detail::Visit{"", &table, {}});
}

Reader::Reader(std::shared_ptr<detail::ReadState> state, const std::size_t visit)
    : state_(std::move(state)), visit_(visit) {}

std::string Reader::path(const std::string_view key) const { return join(state_->visits[visit_].path, key); }

void Reader::add_error(const std::string_view key, std::string message) {
  state_->errors.push_back(ConfigError{path(key), std::move(message)});
}

void Reader::require_unit(const std::string_view key, const std::string_view suffix) {
  if (!check(key.ends_with(suffix))) {
    add_error(key, "is read with a unit, so the schema must name it with the suffix " + std::string(suffix));
  }
}

// The setting under key, or null when it is missing. Marks the key as read.
const toml::node* Reader::find(const std::string_view key) {
  detail::Visit& visit = state_->visits[visit_];
  if (visit.table == nullptr) {
    // The section itself is missing or invalid, which is already an error.
    return nullptr;
  }
  visit.read_keys.emplace_back(key);
  const toml::node* node = visit.table->get(key);
  if (node == nullptr) {
    add_error(key, "is required");
  }
  return node;
}

std::int64_t Reader::integer(const std::string_view key, const std::int64_t min, const std::int64_t max) {
  const toml::node* node = find(key);
  if (!check(min <= max)) {
    add_error(key, "has an empty range in the schema");
    return min;
  }
  if (node == nullptr) {
    return min;
  }
  const toml::value<std::int64_t>* value = node->as_integer();
  if (value == nullptr) {
    add_error(key, "must be an integer");
    return min;
  }
  if (value->get() < min || value->get() > max) {
    add_error(key, range_message(min, max, value->get()));
    return min;
  }
  return value->get();
}

double Reader::number(const std::string_view key, const double min, const double max) {
  const toml::node* node = find(key);
  if (!check(min <= max)) {
    add_error(key, "has an empty range in the schema");
    return min;
  }
  if (node == nullptr) {
    return min;
  }
  const std::optional<double> value = number_value(*node);
  if (!value.has_value()) {
    add_error(key, "must be a number");
    return min;
  }
  // Written so that NaN, which compares false with everything, is rejected.
  if (!(*value >= min && *value <= max)) {
    add_error(key, range_message(min, max, *value));
    return min;
  }
  return *value;
}

bool Reader::boolean(const std::string_view key) {
  const toml::node* node = find(key);
  if (node == nullptr) {
    return false;
  }
  const toml::value<bool>* value = node->as_boolean();
  if (value == nullptr) {
    add_error(key, "must be true or false");
    return false;
  }
  return value->get();
}

std::optional<std::string> Reader::string_value(const std::string_view key) {
  const toml::node* node = find(key);
  if (node == nullptr) {
    return std::nullopt;
  }
  const toml::value<std::string>* value = node->as_string();
  if (value == nullptr) {
    add_error(key, "must be text");
    return std::nullopt;
  }
  return value->get();
}

std::string Reader::text(const std::string_view key) {
  std::optional<std::string> value = string_value(key);
  if (!value.has_value()) {
    return {};
  }
  if (value->empty()) {
    add_error(key, "must not be empty");
  }
  return std::move(*value);
}

std::vector<std::string> Reader::texts(const std::string_view key, const std::size_t max_count) {
  const toml::node* node = find(key);
  if (!check(max_count > 0)) {
    add_error(key, "allows no entries in the schema");
    return {};
  }
  if (node == nullptr) {
    return {};
  }
  const std::string shape = "must be a list of 1 to " + std::to_string(max_count) + " different texts, none empty";
  const toml::array* list = node->as_array();
  if (list == nullptr || list->empty() || list->size() > max_count) {
    add_error(key, shape);
    return {};
  }
  std::vector<std::string> out;
  for (const toml::node& item : *list) {
    const toml::value<std::string>* value = item.as_string();
    if (value == nullptr || value->get().empty() || std::ranges::find(out, value->get()) != out.end()) {
      add_error(key, shape);
      return {};
    }
    out.push_back(value->get());
  }
  return out;
}

std::size_t Reader::choose(const std::string_view key, const std::span<const std::string_view> names) {
  const std::optional<std::string> value = string_value(key);
  if (!value.has_value()) {
    return 0;
  }
  const auto match = std::ranges::find(names, std::string_view(*value));
  if (match == names.end()) {
    add_error(key, one_of_message(names, *value));
    return 0;
  }
  return static_cast<std::size_t>(std::distance(names.begin(), match));
}

Duration Reader::duration(const std::string_view key, const Duration min, const Duration max) {
  require_unit(key, "_ns");
  return Duration(integer(key, min.count(), max.count()));
}

Meters Reader::meters(const std::string_view key, const Meters min, const Meters max) {
  require_unit(key, "_m");
  return Meters(number(key, min.value(), max.value()));
}

Radians Reader::radians(const std::string_view key, const Radians min, const Radians max) {
  require_unit(key, "_rad");
  return Radians(number(key, min.value(), max.value()));
}

Degrees Reader::degrees(const std::string_view key, const Degrees min, const Degrees max) {
  require_unit(key, "_deg");
  return Degrees(number(key, min.value(), max.value()));
}

Reader Reader::section(const std::string_view key) {
  const toml::node* node = find(key);
  const Table* table = node == nullptr ? nullptr : node->as_table();
  if (node != nullptr && table == nullptr) {
    add_error(key, "must be a table");
  }
  state_->visits.push_back(detail::Visit{path(key), table, {}});
  return {state_, state_->visits.size() - 1};
}

Errors Reader::finish() const {
  Errors errors = state_->errors;
  for (const detail::Visit& visit : state_->visits) {
    append_unknown_keys(visit, errors);
  }
  return errors;
}

}  // namespace ics::config
