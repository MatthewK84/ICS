#pragma once

#include <filesystem>

#include "ics/common/error.hpp"

namespace ics::store::detail {

// Syncs folder, so the files created, renamed or removed in it survive a
// crash. kUnwritable when it cannot be opened or synced.
[[nodiscard]] Status sync_folder(const std::filesystem::path& folder) noexcept;

}  // namespace ics::store::detail
