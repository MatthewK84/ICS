#pragma once

#include <cstddef>
#include <filesystem>
#include <span>

#include "ics/common/error.hpp"

namespace ics::camera {

// A file mapped read-only into memory, so that reading the metadata of a
// cine of many gigabytes touches only the pages it reads.
class MappedFile {
 public:
  // Fails with Error::kUnreadable when the file cannot be opened or mapped.
  // An empty file maps to no bytes.
  [[nodiscard]] static Result<MappedFile> open(const std::filesystem::path& path);

  MappedFile(const MappedFile&) = delete;
  MappedFile& operator=(const MappedFile&) = delete;
  MappedFile(MappedFile&& other) noexcept;
  MappedFile& operator=(MappedFile&& other) noexcept;
  ~MappedFile();

  [[nodiscard]] std::span<const std::byte> bytes() const noexcept { return bytes_; }

 private:
  explicit MappedFile(std::span<std::byte> bytes) noexcept : bytes_(bytes) {}
  void unmap() noexcept;

  // The mapping, which munmap takes back as mmap gave it.
  std::span<std::byte> bytes_;
};

}  // namespace ics::camera
