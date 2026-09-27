// expect: -W(error=)?unused-result
// Seeded violation (ICS-005): ignoring a [[nodiscard]] result. See CMakeLists.txt.
namespace ics::seeded {

[[nodiscard]] int fallible() { return 1; }

void ignore_result() { fallible(); }

}  // namespace ics::seeded
