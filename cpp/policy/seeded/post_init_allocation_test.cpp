// expect: 1 allocation\(s\) after initialization
// Seeded violation (ICS-005): allocating after initialization. See CMakeLists.txt.
#include <new>

#include <gtest/gtest.h>

#include "ics/testing/no_allocation_scope.hpp"

namespace {

TEST(SeededViolation, AllocatesAfterInitialization) {
  const ics::testing::NoAllocationScope no_allocation;
  void* memory = ::operator new(16);
  ::operator delete(memory);
}

}  // namespace
