// Prints the FNV-1a hash of the first argument, or of the empty string.
#include <cstdint>
#include <cstdio>
#include <span>
#include <string_view>

#include "ics/toolchain_check/checksum.hpp"

int main(int argc, char** argv) {
  const std::string_view text = (argc > 1) ? std::string_view{argv[1]} : std::string_view{};
  const std::uint32_t hash = ics::toolchain_check::fnv1a32(std::as_bytes(std::span{text}));
  std::printf("%08x\n", static_cast<unsigned int>(hash));
  return 0;
}
