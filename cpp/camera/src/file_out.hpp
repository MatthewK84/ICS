#pragma once

#include <cstddef>
#include <filesystem>
#include <span>

#include "ics/common/error.hpp"

namespace ics::camera::detail {

// Writes the bytes to a file, replacing any there. Fails with
// Error::kUnwritable when the file cannot be written.
[[nodiscard]] Status write_file(const std::filesystem::path& path, std::span<const std::byte> bytes);

}  // namespace ics::camera::detail
