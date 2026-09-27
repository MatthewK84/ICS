#include "ics/toolchain_check/checksum.hpp"

namespace ics::toolchain_check {

namespace {
constexpr std::uint32_t kOffsetBasis = 2166136261U;
constexpr std::uint32_t kPrime = 16777619U;
}  // namespace

std::uint32_t fnv1a32(std::span<const std::byte> bytes) noexcept {
  std::uint32_t hash = kOffsetBasis;
  for (const std::byte byte : bytes) {
    hash ^= std::to_integer<std::uint32_t>(byte);
    hash *= kPrime;
  }
  return hash;
}

}  // namespace ics::toolchain_check
