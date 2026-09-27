// expect: \[cppcoreguidelines-owning-memory[],]
// Seeded violation (ICS-005): an owning raw pointer from new. See CMakeLists.txt.
namespace ics::seeded {

int owning_raw_pointer() {
  int* value = new int{42};
  const int result = *value;
  delete value;
  return result;
}

}  // namespace ics::seeded
