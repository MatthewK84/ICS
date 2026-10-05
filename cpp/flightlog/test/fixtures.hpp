#pragma once

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace ics::flightlog::testing {

// A sample log's bytes (logs/README.md).
inline std::vector<std::byte> log_file(const std::string& name) {
  std::ifstream in(std::string(ICS_FLIGHTLOG_LOGS) + "/" + name, std::ios::binary);
  EXPECT_TRUE(in.is_open()) << name;
  const std::vector<char> chars{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
  std::vector<std::byte> bytes(chars.size());
  std::ranges::transform(chars, bytes.begin(), [](const char c) { return static_cast<std::byte>(c); });
  return bytes;
}

// One line of logs/expected.tsv: what pyulog or pymavlink read in a sample
// log. values are the fields after the file and the key.
struct Expected {
  std::vector<std::string> values;

  // The value of a NAME=VALUE field, or "" if there is none.
  [[nodiscard]] std::string named(const std::string& name) const {
    const auto found =
        std::ranges::find_if(values, [&name](const std::string& value) { return value.starts_with(name + "="); });
    return found == values.end() ? std::string() : found->substr(name.size() + 1);
  }
  [[nodiscard]] double number(const std::string& name) const { return std::stod(named(name)); }
};

// Every line of logs/expected.tsv for a file and key, in order.
inline std::vector<Expected> expected(const std::string& file, const std::string& key) {
  std::ifstream in(std::string(ICS_FLIGHTLOG_LOGS) + "/expected.tsv");
  EXPECT_TRUE(in.is_open());
  std::vector<Expected> out;
  std::string line;
  while (std::getline(in, line)) {
    std::vector<std::string> fields;
    std::stringstream stream(line);
    std::string field;
    while (std::getline(stream, field, '\t')) {
      fields.push_back(field);
    }
    if (fields.size() >= 2 && fields[0] == file && fields[1] == key) {
      out.push_back(Expected{.values = {fields.begin() + 2, fields.end()}});
    }
  }
  return out;
}

// The single value of a FILE KEY VALUE line.
inline std::string expected_value(const std::string& file, const std::string& key) {
  const std::vector<Expected> lines = expected(file, key);
  EXPECT_EQ(lines.size(), 1U) << file << " " << key;
  return lines.empty() || lines[0].values.empty() ? std::string() : lines[0].values[0];
}

// Each topic's sample count: the FILE count TOPIC N lines.
inline std::map<std::string, std::size_t> expected_counts(const std::string& file) {
  std::map<std::string, std::size_t> counts;
  for (const Expected& line : expected(file, "count")) {
    counts[line.values.at(0)] = std::stoul(line.values.at(1));
  }
  return counts;
}

}  // namespace ics::flightlog::testing
