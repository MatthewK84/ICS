#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "ics/common/error.hpp"

namespace ics::lattice {

// Lattice tokens (ICS-023). A station keeps each token in its own file, which
// only the service's account may read: the token never goes in a config file,
// on a command line or into a log.

// Whether text could be a token: printable ASCII without spaces, so it cannot
// end a header and add another.
[[nodiscard]] bool valid_token(std::string_view text) noexcept;

// The token in the file at path, without the white space around it. Fails
// with Error::kUnreadable when there is no regular file at path, and with
// Error::kInvalidArgument when its group or other users have any access to
// it, or it holds no valid token.
[[nodiscard]] Result<std::string> read_token(const std::filesystem::path& path);

}  // namespace ics::lattice
