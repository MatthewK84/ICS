// expect: \[cppcoreguidelines-pro-bounds-pointer-arithmetic[],]
// Seeded violation (ICS-005): indexing a raw pointer instead of a std::span. See CMakeLists.txt.
namespace ics::seeded {

int sum(const int* values, int count) {
  int total = 0;
  for (int index = 0; index < count; ++index) {
    total += values[index];
  }
  return total;
}

}  // namespace ics::seeded
