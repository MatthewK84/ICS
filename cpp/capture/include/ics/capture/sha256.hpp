#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <span>
#include <string>

#include "ics/common/error.hpp"

struct evp_md_ctx_st;

namespace ics::capture {

inline constexpr std::size_t kSha256Size = 32;
using Sha256Digest = std::array<std::byte, kSha256Size>;

// A SHA-256 of bytes fed in pieces (ICS-020), from OpenSSL's EVP interface,
// so a FIPS provider computes it when the station loads one. Allocates only
// in make().
class Sha256 {
 public:
  // A hash of no bytes yet. kUnavailable when OpenSSL cannot provide SHA-256.
  [[nodiscard]] static Result<Sha256> make() noexcept;

  // Adds bytes. kUnavailable after finish() until restart().
  [[nodiscard]] Status update(std::span<const std::byte> bytes) noexcept;

  // The hash of every byte added. kUnavailable when called twice without a
  // restart() between.
  [[nodiscard]] Result<Sha256Digest> finish() noexcept;

  // Starts a new hash of no bytes.
  [[nodiscard]] Status restart() noexcept;

 private:
  struct Free {
    void operator()(evp_md_ctx_st* context) const noexcept;
  };
  using Context = std::unique_ptr<evp_md_ctx_st, Free>;

  explicit Sha256(Context context) noexcept;

  Context context_;
  // Whether finish() ended the hash. Some OpenSSL 3 releases accept bytes
  // after the end, so this class refuses them itself.
  bool finished_ = false;
};

// The digest as 64 lowercase hexadecimal digits, as sha256sum prints it.
[[nodiscard]] std::string to_hex(const Sha256Digest& digest);

}  // namespace ics::capture
