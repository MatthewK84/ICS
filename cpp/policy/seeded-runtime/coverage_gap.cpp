// expect: branches 1 of 2 covered
// Seeded defect (ICS-015): a branch no test takes. The coverage stage gates
// this file at full coverage, with an assertion-density floor of 0, and must
// fail it. See policy/check-dynamic.sh.
namespace {

int magnitude(const int value) {
  if (value < 0) {
    return -value;
  }
  return value;
}

}  // namespace

int main() { return magnitude(1) - 1; }
