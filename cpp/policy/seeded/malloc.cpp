// expect: \[cppcoreguidelines-no-malloc[],]
// Seeded violation (ICS-005): memory from malloc. See CMakeLists.txt.
#include <cstdlib>

namespace ics::seeded {

void use_malloc() {
  void* memory = std::malloc(16);
  std::free(memory);
}

}  // namespace ics::seeded
