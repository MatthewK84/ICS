#include "ics/testing/no_allocation_scope.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <thread>

#include <gtest/gtest-spi.h>
#include <gtest/gtest.h>

// The Guidelines Support Library's ownership marker, as in the hook itself:
// these tests own raw memory on purpose.
namespace gsl {
template <typename T>
using owner = T;
}  // namespace gsl

namespace {

using ics::testing::NoAllocationScope;
using ics::testing::ThisThreadOnly;

constexpr std::size_t kSize = 24;
constexpr std::size_t kCount = 2;
constexpr std::align_val_t kAlignment{64};

// Allocated with new-expressions, so every allocation pairs with a delete in
// the analysis (CodeQL); each operator delete form is then called directly.
// The expressions use () rather than {}: cppcheck 2.13 misreads a braced
// initializer after placement new.
struct Plain {
  std::array<std::byte, kSize> bytes{};
};

struct alignas(static_cast<std::size_t>(kAlignment)) Wide {
  std::array<std::byte, kSize> bytes{};
};

// Writes the pointer to a volatile and reads it back, so the compiler cannot
// drop an allocation it can see is never used.
template <typename T>
gsl::owner<T*> kept(gsl::owner<T*> pointer) {
  void* volatile sink = pointer;
  static_cast<void>(sink);
  return pointer;
}

bool is_aligned(const void* memory) {
  return reinterpret_cast<std::uintptr_t>(memory) % static_cast<std::size_t>(kAlignment) == 0;
}

// Too large for any allocator. Read through volatile so the compiler cannot
// see the constant and warn about the allocation size.
std::size_t impossible_size() {
  const volatile std::size_t size = std::numeric_limits<std::size_t>::max();
  return size;
}

// Six allocations, one through each unaligned operator new form, each freed
// through a different unaligned operator delete form.
std::size_t allocate_plain_forms() {
  const NoAllocationScope scope;
  const gsl::owner<Plain*> single = kept(new Plain());
  const gsl::owner<Plain*> sized_single = kept(new Plain());
  const gsl::owner<Plain*> array = kept(new Plain[kCount]());
  const gsl::owner<Plain*> sized_array = kept(new Plain[kCount]());
  const gsl::owner<Plain*> nothrow_single = kept(new (std::nothrow) Plain());
  const gsl::owner<Plain*> nothrow_array = kept(new (std::nothrow) Plain[kCount]());
  ::operator delete(single);
  ::operator delete(sized_single, sizeof(Plain));
  ::operator delete[](array);
  ::operator delete[](sized_array, kCount * sizeof(Plain));
  ::operator delete(nothrow_single, std::nothrow);
  ::operator delete[](nothrow_array, std::nothrow);
  return scope.allocations();
}

// The same for the aligned forms, which over-aligned types use.
std::size_t allocate_aligned_forms() {
  const NoAllocationScope scope;
  const gsl::owner<Wide*> single = kept(new Wide());
  const gsl::owner<Wide*> sized_single = kept(new Wide());
  const gsl::owner<Wide*> array = kept(new Wide[kCount]());
  const gsl::owner<Wide*> sized_array = kept(new Wide[kCount]());
  const gsl::owner<Wide*> nothrow_single = kept(new (std::nothrow) Wide());
  const gsl::owner<Wide*> nothrow_array = kept(new (std::nothrow) Wide[kCount]());
  const bool all_aligned = is_aligned(single) && is_aligned(sized_single) && is_aligned(array) &&
                           is_aligned(sized_array) && is_aligned(nothrow_single) && is_aligned(nothrow_array);
  ::operator delete(single, kAlignment);
  ::operator delete(sized_single, sizeof(Wide), kAlignment);
  ::operator delete[](array, kAlignment);
  ::operator delete[](sized_array, kCount * sizeof(Wide), kAlignment);
  ::operator delete(nothrow_single, kAlignment, std::nothrow);
  ::operator delete[](nothrow_array, kAlignment, std::nothrow);
  EXPECT_TRUE(all_aligned);
  return scope.allocations();
}

// Another thread allocates once while a scope for every thread and one for
// this thread alone are open. Returns what the first counted, and sets own to
// what the second did.
std::size_t allocations_beside_another_thread(std::size_t& own) {
  std::atomic<bool> go{false};
  std::thread other([&go] {
    go.wait(false);
    const gsl::owner<Plain*> value = kept(new Plain());
    delete value;
  });
  const NoAllocationScope every_thread;
  const NoAllocationScope this_thread{ThisThreadOnly{}};
  go.store(true);
  go.notify_one();
  other.join();
  own = this_thread.allocations();
  return every_thread.allocations();
}

TEST(NoAllocationScope, PassesWithoutAllocation) {
  const NoAllocationScope scope;
  EXPECT_EQ(scope.allocations(), 0U);
}

TEST(NoAllocationScope, FailsTheTestOnAllocation) {
  EXPECT_NONFATAL_FAILURE(
      {
        const NoAllocationScope scope;
        const gsl::owner<Plain*> value = kept(new Plain());
        delete value;
      },
      "1 allocation(s) after initialization");
}

TEST(NoAllocationScope, ThisThreadOnlyCountsItsOwnThread) {
  EXPECT_NONFATAL_FAILURE(
      {
        const NoAllocationScope scope{ThisThreadOnly{}};
        const gsl::owner<Plain*> value = kept(new Plain());
        delete value;
      },
      "1 allocation(s) after initialization");
}

TEST(NoAllocationScope, ThisThreadOnlyIgnoresOtherThreads) {
  std::size_t own = 1;
  EXPECT_NONFATAL_FAILURE(EXPECT_GE(allocations_beside_another_thread(own), 1U), "allocation(s) after initialization");
  EXPECT_EQ(own, 0U);
}

TEST(NoAllocationScope, CountsEveryPlainForm) {
  EXPECT_NONFATAL_FAILURE(EXPECT_EQ(allocate_plain_forms(), 6U), "6 allocation(s) after initialization");
}

TEST(NoAllocationScope, CountsEveryAlignedForm) {
  EXPECT_NONFATAL_FAILURE(EXPECT_EQ(allocate_aligned_forms(), 6U), "6 allocation(s) after initialization");
}

TEST(AllocationHook, ZeroSizeReturnsUsableMemory) {
  const gsl::owner<std::byte*> plain = kept(new std::byte[0]);
  const gsl::owner<Wide*> aligned = kept(new Wide[0]);
  EXPECT_NE(plain, nullptr);
  EXPECT_NE(aligned, nullptr);
  EXPECT_TRUE(is_aligned(aligned));
  delete[] plain;
  delete[] aligned;
}

TEST(AllocationHook, ReportsImpossibleSizes) {
  EXPECT_THROW(static_cast<void>(::operator new(impossible_size())), std::bad_alloc);
  EXPECT_THROW(static_cast<void>(::operator new(impossible_size(), kAlignment)), std::bad_alloc);
  EXPECT_EQ(::operator new(impossible_size(), std::nothrow), nullptr);
  EXPECT_EQ(::operator new(impossible_size(), kAlignment, std::nothrow), nullptr);
}

}  // namespace
