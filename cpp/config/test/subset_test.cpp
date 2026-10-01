#include "ics/config/subset.hpp"

#include <optional>
#include <string>
#include <string_view>

#include <gtest/gtest.h>
#include <tl/expected.hpp>

#include "ics/config/config.hpp"

namespace {

// Text in the subset, each also parsed by toml++ to show the subset is TOML.
constexpr std::string_view kInside[] = {
    "",
    "# only a comment\n\n   \t\n",
    "[log]\nlevel = \"info\" # after a value\n",
    "[ a . b ]#after a table\n",
    "a.b-c_9 = 1\r\nx=2\r\n",
    "1 = 'a digit key'",
    "int = +1_000\nneg = -0\nzero = 0\nfloat = 3.14_15\nexp = 1e-3\nbig = 2E+10\nboth = -6.0e2\n",
    "special = [inf, +inf, -inf, nan, +nan, -nan]\nflags = [true, false,]\nempty = [ ]\n",
    "mixed = [ 1 , 'two' , \"three\" ]#c\nnext = [1]\n",
    "text = \"tab\\there \\\"quoted\\\" back\\\\slash \\b\\f\\n\\r\"\nliteral = 'C:\\path \"as is\"'\n",
    "\ta = 1\n  [x]\n",
};

TEST(Subset, AcceptsEveryForm) {
  for (const std::string_view text : kInside) {
    EXPECT_EQ(ics::config::subset_error(text), std::nullopt) << text;
    EXPECT_TRUE(ics::config::parse(text).has_value()) << text;
  }
}

struct Outside {
  std::string_view text;
  std::string_view error;
};

constexpr Outside kOutside[] = {
    // Bytes
    {"x = 5\xc3\xa9", "line 1, column 6: config files may hold only printable ASCII, tabs and line breaks"},
    {"# a\n# \x01", "line 2, column 3: config files may hold only printable ASCII, tabs and line breaks"},
    {std::string_view("a = \"\0\"", 7), "line 1, column 6: config files may hold only printable ASCII, tabs and line breaks"},
    {"a = 1\rb = 2", "line 1, column 7: a carriage return must be followed by a line feed"},
    {"a = 1 # c\r", "line 1, column 11: a carriage return must be followed by a line feed"},
    // Tables
    {"[[a]]", "line 1, column 2: arrays of tables ([[name]]) are not supported"},
    {"[a", "line 1, column 3: expected ']' to close the table name"},
    {"[]", "line 1, column 2: expected a key of letters, digits, '_' or '-'"},
    {"[a.]", "line 1, column 4: expected a key of letters, digits, '_' or '-'"},
    {"[a] b", "line 1, column 5: expected a comment or the end of the line"},
    // Keys
    {"\"a\" = 1", "line 1, column 1: expected a key of letters, digits, '_' or '-'"},
    {"a 1", "line 1, column 3: expected '=' after the key"},
    {"a. = 1", "line 1, column 4: expected a key of letters, digits, '_' or '-'"},
    // Values
    {"a =", "line 1, column 4: expected a value: \"text\", 'text', a number, true, false or a [list]"},
    {"a = nope", "line 1, column 5: expected a value: \"text\", 'text', a number, true, false or a [list]"},
    {"a = {b = 1}", "line 1, column 5: expected a value: \"text\", 'text', a number, true, false or a [list]"},
    {"a = truex", "line 1, column 9: expected a space, ',', ']', a comment or the end of the line"},
    {"a = \"x\"y", "line 1, column 8: expected a space, ',', ']', a comment or the end of the line"},
    {"a = 1 2", "line 1, column 7: expected a comment or the end of the line"},
    {"a = 2026-10-01", "line 1, column 9: expected a space, ',', ']', a comment or the end of the line"},
    {"a = 0x1F", "line 1, column 6: expected a space, ',', ']', a comment or the end of the line"},
    // Text
    {"a = \"open", "line 1, column 10: text must be closed on the line it starts"},
    {"a = 'open\nb = 1", "line 1, column 10: text must be closed on the line it starts"},
    {"a = \"\\x\"", "line 1, column 7: unsupported escape; use \\\" \\\\ \\b \\f \\n \\r or \\t"},
    {"a = \"\\u0041\"", "line 1, column 7: unsupported escape; use \\\" \\\\ \\b \\f \\n \\r or \\t"},
    {"a = \"end\\", "line 1, column 10: unsupported escape; use \\\" \\\\ \\b \\f \\n \\r or \\t"},
    {"a = \"\"\"multi\"\"\"", "line 1, column 7: expected a space, ',', ']', a comment or the end of the line"},
    // Numbers
    {"a = +", "line 1, column 6: invalid number"},
    {"a = 1e", "line 1, column 7: invalid number"},
    {"a = 1.", "line 1, column 7: invalid number"},
    {"a = 1__0", "line 1, column 7: invalid number"},
    {"a = 1_", "line 1, column 7: invalid number"},
    {"a = 1.5e+", "line 1, column 10: invalid number"},
    {"a = 007", "line 1, column 6: a number must not start with 0 unless it is 0"},
    {"a = -0_1", "line 1, column 7: a number must not start with 0 unless it is 0"},
    // Lists
    {"a = [1 2]", "line 1, column 8: expected ',' or ']' in the list"},
    {"a = [1", "line 1, column 7: expected ',' or ']' in the list"},
    {"a = [1#c]", "line 1, column 7: expected ',' or ']' in the list"},
    {"a = [,]", "line 1, column 6: expected a value: \"text\", 'text', a number, true, false or a [list]"},
    {"a = [[1]]", "line 1, column 6: expected a value: \"text\", 'text', a number, true, false or a [list]"},
    {"a = [1]]", "line 1, column 8: expected a comment or the end of the line"},
    {"a = [1x]", "line 1, column 7: expected a space, ',', ']', a comment or the end of the line"},
    // Lines after the first
    {"# fine\n[a]\nb = ?", "line 3, column 5: expected a value: \"text\", 'text', a number, true, false or a [list]"},
    {"\tb = 1\r\n  [x]\nc = 'a' 'b'", "line 3, column 9: expected a comment or the end of the line"},
};

TEST(Subset, NamesTheLineColumnAndReason) {
  for (const Outside& outside : kOutside) {
    EXPECT_EQ(ics::config::subset_error(outside.text), std::string(outside.error)) << outside.text;
  }
}

// parse() rejects text outside the subset before toml++ reads it.
TEST(Subset, GuardsParse) {
  for (const Outside& outside : kOutside) {
    const tl::expected<ics::config::Table, ics::config::Errors> table = ics::config::parse(outside.text);
    ASSERT_FALSE(table.has_value()) << outside.text;
    EXPECT_EQ(table.error(), (ics::config::Errors{{"", std::string(outside.error)}})) << outside.text;
  }
}

}  // namespace
