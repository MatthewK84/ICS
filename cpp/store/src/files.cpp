#include "files.hpp"

#include <filesystem>

#include <fcntl.h>
#include <unistd.h>

#include "ics/timing/unix_socket.hpp"
#include "status.hpp"

namespace ics::store::detail {

Status sync_folder(const std::filesystem::path& folder) noexcept {
  const timing::Fd fd(::open(folder.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
  return status_of(fd.valid(), Error::kUnwritable).and_then([&fd] {
    return status_of(::fsync(fd.get()) == 0, Error::kUnwritable);
  });
}

}  // namespace ics::store::detail
