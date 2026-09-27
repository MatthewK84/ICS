// expect: -W(error=)?(implicit-int-)?conversion
// Seeded violation (ICS-005): an implicit narrowing conversion. See CMakeLists.txt.
#include <cstdint>

namespace ics::seeded {

std::int16_t narrow(std::int32_t value) { return value; }

}  // namespace ics::seeded
