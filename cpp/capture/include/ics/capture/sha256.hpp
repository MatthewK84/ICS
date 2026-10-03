#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <span>

#include "ics/common/error.hpp"

// OpenSSL's digest context, EVP_MD_CTX.
struct evp_md_ctx_st;

namespace ics::capture {

inline constexpr std::size_t kDigestSize = 32;
using Digest = std::array<std::byte, kDigestSize>;
using DigestHex = std::array<char, 2 * kDigestSize>;

// The digest in lower-case hexadecimal, as sha256sum prints it.
[[nodiscard]] DigestHex to_hex(const Digest& digest) noexcept;

// SHA-256 over data fed in pieces, computed by OpenSSL (ICS-020).
class Sha256 {
 public:
  // kUnavailable when OpenSSL cannot make a digest context.
  [[nodiscard]] static Result<Sha256> make() noexcept;

  void update(std::span<const std::byte> data) noexcept;

  // The digest of everything fed since make or the last finish, which
  // starts over. kUnavailable when OpenSSL failed at any step since then.
  [[nodiscard]] Result<Digest> finish() noexcept;

 private:
  struct ContextFree {
    void operator()(evp_md_ctx_st* context) const noexcept;
  };

  explicit Sha256(evp_md_ctx_st* context) noexcept;
  void restart() noexcept;

  std::unique_ptr<evp_md_ctx_st, ContextFree> context_;
  // False once an OpenSSL call has failed, until the next finish.
  bool ok_ = false;
};

}  // namespace ics::capture
