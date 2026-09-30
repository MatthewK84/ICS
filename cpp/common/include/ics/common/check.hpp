#pragma once

#include <source_location>

namespace ics {

namespace detail {

// Writes "ICS check failed at <file>:<line> in <function>" to stderr, without
// allocating. Called by ics::check only.
void report_check_failure(std::source_location where) noexcept;

}  // namespace detail

// The ICS assertion (ICS-015, Power of Ten rule 5). Use it for a condition
// that holds unless there is a bug or an overload the system was sized to
// avoid. It returns the condition. When the condition is false, it writes one
// line naming the caller's file, line and function to stderr, without
// allocating, and the caller takes the recovery action, normally returning an
// error:
//
//   if (!ics::check(index < size())) {
//     return ics::fail(ics::Error::kOutOfRange);
//   }
//
// Assertions stay on in every build. An outcome a caller expects, such as a
// polling reader finding nothing to read, is an ordinary branch, not a check.
// CI counts the checks in each function (cpp/policy/coverage-gates.txt).
//
// It is inline so the compiler sees that a failed check returns false, and
// so knows the access a check guards cannot happen.
[[nodiscard]] inline bool check(const bool condition,
                                const std::source_location where = std::source_location::current()) noexcept {
  if (condition) {
    return true;
  }
  detail::report_check_failure(where);
  return false;
}

}  // namespace ics
