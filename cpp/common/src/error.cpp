#include "ics/common/error.hpp"

namespace ics {

std::string_view to_string(const Error error) noexcept {
  switch (error) {
    case Error::kInvalidArgument:
      return "invalid argument";
    case Error::kOutOfRange:
      return "out of range";
    case Error::kFull:
      return "full";
    case Error::kEmpty:
      return "empty";
    case Error::kStaleHandle:
      return "stale handle";
    case Error::kUnreadable:
      return "unreadable";
    case Error::kMalformed:
      return "malformed";
    case Error::kUnavailable:
      return "unavailable";
  }
  return "unknown";
}

}  // namespace ics
