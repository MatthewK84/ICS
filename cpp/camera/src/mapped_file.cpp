#include "ics/camera/mapped_file.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <span>
#include <utility>

#include "ics/common/check.hpp"
#include "ics/common/error.hpp"

namespace ics::camera {

Result<MappedFile> MappedFile::open(const std::filesystem::path& path) {
  const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    return fail(Error::kUnreadable);
  }
  // An open file always has a status. mmap refuses no bytes, and anything
  // but a regular file, such as a directory.
  struct stat status {};
  static_cast<void>(check(::fstat(fd, &status) == 0));
  const auto size = static_cast<std::size_t>(std::max<off_t>(status.st_size, 0));
  void* const address = size == 0 ? MAP_FAILED : ::mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
  static_cast<void>(check(::close(fd) == 0));
  if (address == MAP_FAILED) {
    return size == 0 ? Result<MappedFile>(MappedFile(std::span<std::byte>())) : fail(Error::kUnreadable);
  }
  return MappedFile(std::span<std::byte>(static_cast<std::byte*>(address), size));
}

MappedFile::MappedFile(MappedFile&& other) noexcept : bytes_(std::exchange(other.bytes_, {})) {}

MappedFile& MappedFile::operator=(MappedFile&& other) noexcept {
  if (this != &other) {
    unmap();
    bytes_ = std::exchange(other.bytes_, {});
  }
  return *this;
}

MappedFile::~MappedFile() { unmap(); }

void MappedFile::unmap() noexcept {
  if (!bytes_.empty()) {
    static_cast<void>(check(::munmap(bytes_.data(), bytes_.size()) == 0));
  }
  bytes_ = {};
}

}  // namespace ics::camera
