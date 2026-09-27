// Fuzzes fnv1a32 (ICS-008): for any input, the hash must equal the one built
// byte by byte from the published FNV-1a constants.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <numeric>
#include <span>

#include "ics/toolchain_check/checksum.hpp"

namespace {

constexpr std::uint32_t kOffsetBasis = 2166136261U;
constexpr std::uint32_t kPrime = 16777619U;

std::uint32_t reference_fnv1a32(std::span<const std::uint8_t> input) {
  return std::accumulate(input.begin(), input.end(), kOffsetBasis,
                         [](std::uint32_t hash, std::uint8_t byte) { return (hash ^ byte) * kPrime; });
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const std::span<const std::uint8_t> input{data, size};
  if (ics::toolchain_check::fnv1a32(std::as_bytes(input)) != reference_fnv1a32(input)) {
    std::abort();
  }
  return 0;
}
