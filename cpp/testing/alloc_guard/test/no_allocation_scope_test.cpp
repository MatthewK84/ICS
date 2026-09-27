#include "ics/testing/no_allocation_scope.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>

#include <gtest/gtest-spi.h>
#include <gtest/gtest.h>

namespace {

using ics::testing::NoAllocationScope;

constexpr std::size_t kSize = 24;
constexpr std::align_val_t kAlignment{64};

bool is_aligned(const void* memory) {
  return reinterpret_cast<std::uintptr_t>(memory) % static_cast<std::size_t>(kAlignment) == 0;
}

// Too large for any allocator. Read through volatile so the compiler cannot
// see the constant and warn about the allocation size.
std::size_t impossible_size() {
  const volatile std::size_t size = std::numeric_limits<std::size_t>::max();
  return size;
}

// Six allocations, one freed through each unaligned form of operator delete.
std::size_t allocate_plain_forms() {
  const NoAllocationScope scope;
  void* single = ::operator new(kSize);
  void* sized_single = ::operator new(kSize);
  void* array = ::operator new[](kSize);
  void* sized_array = ::operator new[](kSize);
  void* nothrow_single = ::operator new(kSize, std::nothrow);
  void* nothrow_array = ::operator new[](kSize, std::nothrow);
  ::operator delete(single);
  ::operator delete(sized_single, kSize);
  ::operator delete[](array);
  ::operator delete[](sized_array, kSize);
  ::operator delete(nothrow_single, std::nothrow);
  ::operator delete[](nothrow_array, std::nothrow);
  return scope.allocations();
}

// Six aligned allocations, one freed through each aligned form of operator delete.
std::size_t allocate_aligned_forms() {
  const NoAllocationScope scope;
  void* single = ::operator new(kSize, kAlignment);
  void* sized_single = ::operator new(kSize, kAlignment);
  void* array = ::operator new[](kSize, kAlignment);
  void* sized_array = ::operator new[](kSize, kAlignment);
  void* nothrow_single = ::operator new(kSize, kAlignment, std::nothrow);
  void* nothrow_array = ::operator new[](kSize, kAlignment, std::nothrow);
  const bool all_aligned = is_aligned(single) && is_aligned(sized_single) && is_aligned(array) &&
                           is_aligned(sized_array) && is_aligned(nothrow_single) && is_aligned(nothrow_array);
  ::operator delete(single, kAlignment);
  ::operator delete(sized_single, kSize, kAlignment);
  ::operator delete[](array, kAlignment);
  ::operator delete[](sized_array, kSize, kAlignment);
  ::operator delete(nothrow_single, kAlignment, std::nothrow);
  ::operator delete[](nothrow_array, kAlignment, std::nothrow);
  EXPECT_TRUE(all_aligned);
  return scope.allocations();
}

TEST(NoAllocationScope, PassesWithoutAllocation) {
  const NoAllocationScope scope;
  EXPECT_EQ(scope.allocations(), 0U);
}

TEST(NoAllocationScope, FailsTheTestOnAllocation) {
  EXPECT_NONFATAL_FAILURE(
      {
        const NoAllocationScope scope;
        void* memory = ::operator new(kSize);
        ::operator delete(memory);
      },
      "1 allocation(s) after initialization");
}

TEST(NoAllocationScope, CountsEveryPlainForm) {
  EXPECT_NONFATAL_FAILURE(EXPECT_EQ(allocate_plain_forms(), 6U), "6 allocation(s) after initialization");
}

TEST(NoAllocationScope, CountsEveryAlignedForm) {
  EXPECT_NONFATAL_FAILURE(EXPECT_EQ(allocate_aligned_forms(), 6U), "6 allocation(s) after initialization");
}

TEST(AllocationHook, ZeroSizeReturnsUsableMemory) {
  void* plain = ::operator new(0);
  void* aligned = ::operator new(0, kAlignment);
  EXPECT_NE(plain, nullptr);
  EXPECT_NE(aligned, nullptr);
  EXPECT_TRUE(is_aligned(aligned));
  ::operator delete(plain);
  ::operator delete(aligned, kAlignment);
}

TEST(AllocationHook, ReportsImpossibleSizes) {
  EXPECT_THROW(static_cast<void>(::operator new(impossible_size())), std::bad_alloc);
  EXPECT_THROW(static_cast<void>(::operator new(impossible_size(), kAlignment)), std::bad_alloc);
  EXPECT_EQ(::operator new(impossible_size(), std::nothrow), nullptr);
  EXPECT_EQ(::operator new(impossible_size(), kAlignment, std::nothrow), nullptr);
}

}  // namespace
