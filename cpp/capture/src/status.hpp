#pragma once

#include "ics/common/error.hpp"

namespace ics::capture::detail {

// The result of a call into the system, OpenSSL or libpcap as a Status: ok,
// or error. Every such call in ics::capture goes through this one branch, so
// the calls tests can make fail (opening a missing file, writing past a file
// size limit) cover the failure path of those that only an exhausted heap
// can make fail, such as allocating a digest context. Chain the steps that
// follow with and_then.
[[nodiscard]] inline Status status_of(const bool ok, const Error error) noexcept { return ok ? Status{} : fail(error); }

}  // namespace ics::capture::detail
