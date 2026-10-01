#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace ics::config {

// The TOML subset ICS config files are written in (ICS-016). toml++ 3.4.0 has
// undefined behaviour on some invalid TOML, so parse() checks text against
// this subset first, and toml++ only ever reads simple, valid TOML.
//
// The text holds only printable ASCII, tabs and line breaks (LF or CR LF).
// Each line is blank, a # comment, or one of these, with an optional comment:
//
//   [table]  or  [table.sub]     a table header of bare keys
//   key = value  or  a.b = value  a setting
//
// A bare key is letters, digits, '_' and '-'. A value is one of:
//
//   "text"     with the escapes \" \\ \b \f \n \r \t
//   'text'     taken as written
//   42  -7  1_000            decimal integers
//   1.5  -2e-3  inf  nan     floats
//   true  false
//   [1, "two", 3.0]          a list of the above on one line
//
// Not in the subset: quoted keys, multi-line text, \u escapes, hexadecimal,
// octal and binary integers, dates and times, inline tables, nested lists and
// [[arrays of tables]].
//
// Returns why text is outside the subset, as "line L, column C: reason", or
// nullopt when it is inside.
[[nodiscard]] std::optional<std::string> subset_error(std::string_view text);

}  // namespace ics::config
