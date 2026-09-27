// expect: runtime error: signed integer overflow
// Seeded defect (ICS-008): a signed integer overflow, which is undefined
// behaviour. UndefinedBehaviorSanitizer must stop it: the program exits 0 if
// it runs on past the overflow, so a sanitizer that only reports fails the
// check. See policy/check-dynamic.sh.
#include <limits>

int main() {
  // Volatile, so the compiler cannot fold the overflow away.
  volatile int largest = std::numeric_limits<int>::max();
  const int next = largest + 1;
  return next == 0 ? 1 : 0;
}
