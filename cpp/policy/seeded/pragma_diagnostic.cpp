// expect: :6:
// Seeded violation (ICS-005): a pragma that silences a compiler warning. The
// suppression scan must report line 6. See CMakeLists.txt.
#include <cstdint>

#pragma GCC diagnostic ignored "-Wconversion"

namespace ics::seeded {

std::int16_t silenced_narrow(std::int32_t value) { return value; }

}  // namespace ics::seeded
