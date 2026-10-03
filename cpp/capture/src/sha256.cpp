#include "ics/capture/sha256.hpp"

#include <array>
#include <string>
#include <utility>

#include <openssl/evp.h>

#include "status.hpp"

namespace ics::capture {

using detail::status_of;

void Sha256::Free::operator()(evp_md_ctx_st* const context) const noexcept { EVP_MD_CTX_free(context); }

Sha256::Sha256(Context context) noexcept : context_(std::move(context)) {}

Result<Sha256> Sha256::make() noexcept {
  Sha256 hash{Context(EVP_MD_CTX_new())};
  return status_of(hash.context_ != nullptr, Error::kUnavailable)
      .and_then([&hash] { return hash.restart(); })
      .map([&hash] { return std::move(hash); });
}

Status Sha256::update(const std::span<const std::byte> bytes) noexcept {
  if (finished_) {
    return fail(Error::kUnavailable);
  }
  return status_of(EVP_DigestUpdate(context_.get(), bytes.data(), bytes.size()) == 1, Error::kUnavailable);
}

Result<Sha256Digest> Sha256::finish() noexcept {
  if (finished_) {
    return fail(Error::kUnavailable);
  }
  finished_ = true;
  Sha256Digest digest{};
  unsigned int size = 0;
  // The digest is unsigned char in OpenSSL and std::byte here.
  const int done = EVP_DigestFinal_ex(context_.get(), reinterpret_cast<unsigned char*>(digest.data()), &size);
  return status_of(done == 1, Error::kUnavailable).map([&digest] { return digest; });
}

Status Sha256::restart() noexcept {
  finished_ = false;
  return status_of(EVP_DigestInit_ex2(context_.get(), EVP_sha256(), nullptr) == 1, Error::kUnavailable);
}

std::string to_hex(const Sha256Digest& digest) {
  constexpr std::array<char, 16> kDigits{'0', '1', '2', '3', '4', '5', '6', '7',
                                         '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
  constexpr unsigned kNibble = 4;
  constexpr unsigned kLowNibble = 0x0F;
  std::string out;
  out.reserve(2 * digest.size());
  for (const std::byte byte : digest) {
    const auto value = std::to_integer<unsigned>(byte);
    out.push_back(kDigits[value >> kNibble]);
    out.push_back(kDigits[value & kLowNibble]);
  }
  return out;
}

}  // namespace ics::capture
