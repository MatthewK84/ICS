// expect: \[cppcoreguidelines-avoid-goto[],]
// Seeded violation (ICS-005): a backward goto. See CMakeLists.txt.
namespace ics::seeded {

int count_to(int limit) {
  int count = 0;
again:
  if (count < limit) {
    ++count;
    goto again;
  }
  return count;
}

}  // namespace ics::seeded
