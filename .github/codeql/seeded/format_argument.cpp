// Seeded defect (ICS-008) for CodeQL: printf is given an int for "%s"
// (cpp/wrong-type-format-argument). See .github/workflows/codeql.yml.
#include <cstdio>

int main() {
  const int count = 3;
  std::printf("%s\n", count);
  return 0;
}
