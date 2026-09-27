// expect: :6:
// Seeded violation (ICS-005): a comment that silences clang-tidy. The
// suppression scan must report line 6. See CMakeLists.txt.
namespace ics::seeded {

int silenced(int value) { return value; }  // NOLINT

}  // namespace ics::seeded
