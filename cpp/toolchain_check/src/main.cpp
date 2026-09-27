// Prints the FNV-1a hash of the first argument, or of the empty string.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string_view>

#include "ics/toolchain_check/checksum.hpp"

namespace {

// The first command-line argument, or an empty string when there is none.
std::string_view first_argument(std::span<char*> args) {
  return (args.size() > 1) ? std::string_view{args[1]} : std::string_view{};
}

}  // namespace

int main(int argc, char** argv) {
  const std::string_view text = first_argument(std::span<char*>{argv, static_cast<std::size_t>(argc)});
  const std::uint32_t hash = ics::toolchain_check::fnv1a32(std::as_bytes(std::span{text}));
  std::printf("%08x\n", static_cast<unsigned int>(hash));
  return 0;
}
