// expect: \[misc-no-recursion[],]
// Seeded violation (ICS-005): a recursive function. See CMakeLists.txt.
namespace ics::seeded {

int factorial(int value) { return value <= 1 ? 1 : value * factorial(value - 1); }

}  // namespace ics::seeded
