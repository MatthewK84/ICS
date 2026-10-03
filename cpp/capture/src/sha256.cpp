#include "ics/capture/sha256.hpp"

#include <cstddef>
#include <string_view>

#include <openssl/evp.h>

namespace ics::capture {
namespace {

constexpr std::string_view kHexDigits = "0123456789abcdef";
constexpr unsigned kNibbleBits = 4;
constexpr unsigned kNibbleMask = 0x0FU;

}  // namespace

DigestHex to_hex(const Digest& digest) noexcept {
  DigestHex out{};
  for (std::size_t i = 0; i < digest.size(); ++i) {
    const auto value = std::to_integer<unsigned>(digest[i]);
    out[2 * i] = kHexDigits[value >> kNibbleBits];
    out[2 * i + 1] = kHexDigits[value & kNibbleMask];
  }
  return out;
}

void Sha256::ContextFree::operator()(evp_md_ctx_st* const context) const noexcept { EVP_MD_CTX_free(context); }

Sha256::Sha256(evp_md_ctx_st* const context) noexcept : context_(context) {}

Result<Sha256> Sha256::make() noexcept {
  Sha256 hash(EVP_MD_CTX_new());
  if (!hash.context_) {
    return fail(Error::kUnavailable);
  }
  hash.restart();
  return hash;
}

void Sha256::restart() noexcept { ok_ = EVP_DigestInit_ex2(context_.get(), EVP_sha256(), nullptr) == 1; }

void Sha256::update(const std::span<const std::byte> data) noexcept {
  // A context whose start failed has no digest to update.
  if (!ok_) {
    return;
  }
  ok_ = EVP_DigestUpdate(context_.get(), data.data(), data.size()) == 1;
}

Result<Digest> Sha256::finish() noexcept {
  Digest digest{};
  if (ok_) {
    unsigned int size = 0;
    ok_ = (EVP_DigestFinal_ex(context_.get(), reinterpret_cast<unsigned char*>(digest.data()), &size) == 1) &
          (size == digest.size());
  }
  const bool finished = ok_;
  restart();
  if (!finished) {
    return fail(Error::kUnavailable);
  }
  return digest;
}

}  // namespace ics::capture
