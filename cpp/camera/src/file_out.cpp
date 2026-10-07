#include "file_out.hpp"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <limits>
#include <span>

#include "ics/common/check.hpp"
#include "ics/common/error.hpp"

namespace ics::camera::detail {

Status write_file(const std::filesystem::path& path, const std::span<const std::byte> bytes) {
  // The files ICS writes are at most a cine's 1 GiB.
  static_cast<void>(check(bytes.size() <= static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())));
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  out.close();
  if (!out) {
    return fail(Error::kUnwritable);
  }
  return {};
}

}  // namespace ics::camera::detail
