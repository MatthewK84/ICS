#pragma once

#include "ics/common/error.hpp"

namespace ics::encoder::detail {

// The result of a call into the system or OpenSSL as a Status: ok, or error.
// Every such call in ics::encoder goes through this one branch, so the calls
// tests can make fail (binding a port already taken, reading a socket that
// is closed) cover the failure path of those only an exhausted heap can make
// fail, such as OpenSSL's digest. Chain the steps that follow with and_then.
[[nodiscard]] inline Status status_of(const bool ok, const Error error) noexcept { return ok ? Status{} : fail(error); }

}  // namespace ics::encoder::detail
