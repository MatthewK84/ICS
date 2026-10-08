#pragma once

#include "ics/common/error.hpp"

namespace ics::store::detail {

// The result of a system call as a Status: ok, or error. Every system call in
// ics::store that tests cannot make fail on its own, such as fsync, goes
// through this one branch with one that they can, as in ics::capture. Chain
// the steps that follow with and_then.
[[nodiscard]] inline Status status_of(const bool ok, const Error error) noexcept { return ok ? Status{} : fail(error); }

}  // namespace ics::store::detail
