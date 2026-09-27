// expect: ThreadSanitizer: data race
// Seeded defect (ICS-008): two threads write one variable without
// synchronization. ThreadSanitizer must report it. See policy/check-dynamic.sh.
#include <thread>

int main() {
  int counter = 0;
  std::thread writer([&counter] { ++counter; });
  ++counter;
  writer.join();
  return counter == 2 ? 0 : 1;
}
