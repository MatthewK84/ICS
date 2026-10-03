#pragma once

#include <cstddef>
#include <filesystem>
#include <span>

#include "ics/common/error.hpp"
#include "ics/timing/unix_socket.hpp"

namespace ics::capture {

// A new file being written (ICS-020). It is created only if nothing is at its
// path, so a capture file is never overwritten. Closed when destroyed.
class OutputFile {
 public:
  // Creates the file at path, readable by its owner and group. kUnwritable
  // when something is already there or the folder cannot be written.
  [[nodiscard]] static Result<OutputFile> create(const std::filesystem::path& path) noexcept;

  // Writes every byte, in as many calls as the system needs. kUnwritable when
  // the system refuses one, such as on a full disk.
  [[nodiscard]] Status write(std::span<const std::byte> bytes) noexcept;

  // Waits until the bytes written are on the disk. kUnwritable on failure.
  [[nodiscard]] Status sync() noexcept;

 private:
  explicit OutputFile(timing::Fd fd) noexcept;

  timing::Fd fd_;
};

}  // namespace ics::capture
