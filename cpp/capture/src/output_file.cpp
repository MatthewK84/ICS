#include "ics/capture/output_file.hpp"

#include <cstddef>
#include <utility>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "status.hpp"

namespace ics::capture {

using detail::status_of;

namespace {

constexpr mode_t kFileMode = S_IRUSR | S_IWUSR | S_IRGRP;

}  // namespace

OutputFile::OutputFile(timing::Fd fd) noexcept : fd_(std::move(fd)) {}

Result<OutputFile> OutputFile::create(const std::filesystem::path& path) noexcept {
  timing::Fd fd(::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, kFileMode));
  return status_of(fd.valid(), Error::kUnwritable).map([&fd] { return OutputFile(std::move(fd)); });
}

Status OutputFile::write(std::span<const std::byte> bytes) noexcept {
  Status written;
  // A regular file takes every byte at once unless a limit or a full disk
  // stops it part way, so this loops at most twice before an error.
  while (written && !bytes.empty()) {
    const ssize_t count = ::write(fd_.get(), bytes.data(), bytes.size());
    written = status_of(count > 0, Error::kUnwritable);
    bytes = bytes.subspan(written ? static_cast<std::size_t>(count) : bytes.size());
  }
  return written;
}

Status OutputFile::sync() noexcept { return status_of(::fsync(fd_.get()) == 0, Error::kUnwritable); }

}  // namespace ics::capture
