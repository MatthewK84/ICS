// expect: \[containerOutOfBounds\]
// Seeded violation (ICS-008): reads past the end of a fixed-size array.
// Rejected by cppcheck. See CMakeLists.txt.
#include <array>

namespace ics::seeded {

int last_reading() {
  const std::array<int, 4> readings{1, 2, 3, 4};
  return readings[4];
}

}  // namespace ics::seeded
