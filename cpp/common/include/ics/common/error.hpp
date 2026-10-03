#pragma once

#include <cstdint>
#include <string_view>

#include <tl/expected.hpp>

namespace ics {

// Why an ICS operation failed (ICS-015). One enum serves all of ICS. Codes
// reach logs and run records, so later issues append values and never
// renumber or reuse one.
enum class Error : std::uint16_t {
  kInvalidArgument = 1,  // An argument no valid call passes, such as a forged pool handle.
  kOutOfRange = 2,       // An index at or past the end.
  kFull = 3,             // No room for another element.
  kEmpty = 4,            // Nothing to take.
  kStaleHandle = 5,      // A handle whose object was already released.
  kUnreadable = 6,       // A file that cannot be opened or read (ICS-017).
  kMalformed = 7,        // Data that does not follow its format, such as a corrupt geoid grid.
  kUnavailable = 8,      // A peer that did not answer in time, or refused, such as ptp4l (ICS-019).
  kUnwritable = 9,       // A file that cannot be created or written, such as on a full disk (ICS-020).
};

// The name of an error, such as "full"; "unknown" for a value not listed above.
[[nodiscard]] std::string_view to_string(Error error) noexcept;

// The result of a fallible call: a T, or the Error that prevented it.
// tl::expected is [[nodiscard]], so an ignored result does not compile.
// It cannot hold a reference; return std::reference_wrapper instead.
template <typename T>
using Result = tl::expected<T, Error>;

// The result of a fallible call that returns nothing.
using Status = Result<void>;

// Returns an error from a function whose return type is a Result:
//
//   return ics::fail(ics::Error::kFull);
[[nodiscard]] constexpr tl::unexpected<Error> fail(const Error error) noexcept {
  return tl::unexpected<Error>(error);
}

}  // namespace ics
