// expect: AddressSanitizer: heap-buffer-overflow
// Seeded defect (ICS-008): a parser bug behind the magic prefix "ICS!".
// libFuzzer must find it within its time budget. See policy/check-dynamic.sh.
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const std::span<const std::uint8_t> input{data, size};
  if (input.size() >= 4 && input[0] == 'I' && input[1] == 'C' && input[2] == 'S' && input[3] == '!') {
    const std::vector<std::uint8_t> copy(input.begin(), input.end());
    // Reads one byte past the copy; volatile keeps the read.
    const volatile std::uint8_t past_end = copy[copy.size()];
    return past_end == 0 ? 0 : 0;
  }
  return 0;
}
