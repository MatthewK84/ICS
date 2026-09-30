// expect: assertion density 0.00 is below the floor 1.00
// Seeded defect (ICS-015): a fully covered function longer than three lines
// with no ics::check. The coverage stage gates this file at an
// assertion-density floor of 1 and must fail it. See policy/check-dynamic.sh.
#include <array>

namespace {

int sum(const std::array<int, 3>& values) {
  int total = 0;
  for (const int value : values) {
    total += value;
  }
  return total;
}

}  // namespace

int main() { return sum({1, 2, 3}) - 6; }
