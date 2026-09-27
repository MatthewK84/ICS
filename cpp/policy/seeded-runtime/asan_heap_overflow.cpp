// expect: AddressSanitizer: heap-buffer-overflow
// Seeded defect (ICS-008): reads one element past the end of a heap array.
// AddressSanitizer must stop it. See policy/check-dynamic.sh.
#include <cstddef>
#include <vector>

int main(int argc, char** /*argv*/) {
  const std::vector<int> values(4, 1);
  // Index 4 when run without arguments, so the compiler cannot prove it.
  const std::size_t index = static_cast<std::size_t>(argc) + 3U;
  return values[index];
}
