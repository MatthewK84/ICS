// Global operator new and delete that count allocations, for
// ics::testing::NoAllocationScope (ICS-005).
//
// Every replaceable form is defined here, not only the two that the others
// forward to by default: the ASan and TSan runtimes interpose each form, and
// one left to them would pair their operator delete with this file's malloc.
//
// This is the one file that owns raw memory, so it marks ownership with
// gsl::owner as the C++ Core Guidelines intend, which is what
// cppcoreguidelines-owning-memory checks. Its .clang-tidy allows malloc here.
#include "ics/testing/no_allocation_scope.hpp"

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <new>

#include <gtest/gtest.h>

// The Guidelines Support Library's ownership marker. Defined here rather than
// taken from Microsoft GSL, since nothing else needs it.
namespace gsl {
template <typename T>
using owner = T;
}  // namespace gsl

namespace {

std::atomic<std::size_t>& allocation_count() noexcept {
  static std::atomic<std::size_t> count{0};
  return count;
}

// This thread's allocations alone, for the scopes given ThisThreadOnly.
// Constant-initialized, so reading it allocates nothing.
std::size_t& thread_allocation_count() noexcept {
  thread_local std::size_t count = 0;
  return count;
}

void count_allocation() noexcept {
  allocation_count().fetch_add(1, std::memory_order_relaxed);
  ++thread_allocation_count();
}

gsl::owner<void*> allocate(std::size_t size) noexcept {
  count_allocation();
  return std::malloc(size == 0 ? 1 : size);
}

gsl::owner<void*> allocate_aligned(std::size_t size, std::align_val_t alignment) noexcept {
  count_allocation();
  const auto align = static_cast<std::size_t>(alignment);
  if (size > std::numeric_limits<std::size_t>::max() - align) {
    return nullptr;
  }
  // aligned_alloc needs a nonzero size that is a multiple of the alignment.
  const std::size_t rounded = ((size + align - 1) / align) * align;
  return std::aligned_alloc(align, rounded == 0 ? align : rounded);
}

gsl::owner<void*> or_throw(gsl::owner<void*> memory) {
  if (memory == nullptr) {
    throw std::bad_alloc{};
  }
  return memory;
}

}  // namespace

gsl::owner<void*> operator new(std::size_t size) { return or_throw(allocate(size)); }
gsl::owner<void*> operator new[](std::size_t size) { return or_throw(allocate(size)); }
gsl::owner<void*> operator new(std::size_t size, const std::nothrow_t& /*tag*/) noexcept { return allocate(size); }
gsl::owner<void*> operator new[](std::size_t size, const std::nothrow_t& /*tag*/) noexcept { return allocate(size); }

gsl::owner<void*> operator new(std::size_t size, std::align_val_t alignment) {
  return or_throw(allocate_aligned(size, alignment));
}
gsl::owner<void*> operator new[](std::size_t size, std::align_val_t alignment) {
  return or_throw(allocate_aligned(size, alignment));
}
gsl::owner<void*> operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t& /*tag*/) noexcept {
  return allocate_aligned(size, alignment);
}
gsl::owner<void*> operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t& /*tag*/) noexcept {
  return allocate_aligned(size, alignment);
}

void operator delete(gsl::owner<void*> memory) noexcept { std::free(memory); }
void operator delete[](gsl::owner<void*> memory) noexcept { std::free(memory); }
void operator delete(gsl::owner<void*> memory, std::size_t /*size*/) noexcept { std::free(memory); }
void operator delete[](gsl::owner<void*> memory, std::size_t /*size*/) noexcept { std::free(memory); }
void operator delete(gsl::owner<void*> memory, const std::nothrow_t& /*tag*/) noexcept { std::free(memory); }
void operator delete[](gsl::owner<void*> memory, const std::nothrow_t& /*tag*/) noexcept { std::free(memory); }

void operator delete(gsl::owner<void*> memory, std::align_val_t /*alignment*/) noexcept { std::free(memory); }
void operator delete[](gsl::owner<void*> memory, std::align_val_t /*alignment*/) noexcept { std::free(memory); }
void operator delete(gsl::owner<void*> memory, std::size_t /*size*/, std::align_val_t /*alignment*/) noexcept {
  std::free(memory);
}
void operator delete[](gsl::owner<void*> memory, std::size_t /*size*/, std::align_val_t /*alignment*/) noexcept {
  std::free(memory);
}
void operator delete(gsl::owner<void*> memory, std::align_val_t /*alignment*/, const std::nothrow_t& /*tag*/) noexcept {
  std::free(memory);
}
void operator delete[](gsl::owner<void*> memory, std::align_val_t /*alignment*/,
                       const std::nothrow_t& /*tag*/) noexcept {
  std::free(memory);
}

namespace ics::testing {

NoAllocationScope::NoAllocationScope() noexcept
    : this_thread_{false}, start_{allocation_count().load(std::memory_order_relaxed)} {}

NoAllocationScope::NoAllocationScope(ThisThreadOnly /*only*/) noexcept
    : this_thread_{true}, start_{thread_allocation_count()} {}

NoAllocationScope::~NoAllocationScope() {
  const std::size_t count = allocations();
  if (count > 0) {
    ADD_FAILURE() << count << " allocation(s) after initialization inside a NoAllocationScope";
  }
}

std::size_t NoAllocationScope::allocations() const noexcept {
  const std::size_t now = this_thread_ ? thread_allocation_count() : allocation_count().load(std::memory_order_relaxed);
  return now - start_;
}

}  // namespace ics::testing
