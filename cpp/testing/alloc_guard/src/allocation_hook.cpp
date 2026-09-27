// Global operator new and delete that count allocations, for
// ics::testing::NoAllocationScope (ICS-005).
//
// Every replaceable form is defined here, not only the two that the others
// forward to by default: the ASan and TSan runtimes interpose each form, and
// one left to them would pair their operator delete with this file's malloc.
#include "ics/testing/no_allocation_scope.hpp"

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <new>

#include <gtest/gtest.h>

namespace {

std::atomic<std::size_t>& allocation_count() noexcept {
  static std::atomic<std::size_t> count{0};
  return count;
}

void* allocate(std::size_t size) noexcept {
  allocation_count().fetch_add(1, std::memory_order_relaxed);
  return std::malloc(size == 0 ? 1 : size);
}

void* allocate_aligned(std::size_t size, std::align_val_t alignment) noexcept {
  allocation_count().fetch_add(1, std::memory_order_relaxed);
  const auto align = static_cast<std::size_t>(alignment);
  if (size > std::numeric_limits<std::size_t>::max() - align) {
    return nullptr;
  }
  // aligned_alloc needs a nonzero size that is a multiple of the alignment.
  const std::size_t rounded = ((size + align - 1) / align) * align;
  return std::aligned_alloc(align, rounded == 0 ? align : rounded);
}

void* or_throw(void* memory) {
  if (memory == nullptr) {
    throw std::bad_alloc{};
  }
  return memory;
}

}  // namespace

void* operator new(std::size_t size) { return or_throw(allocate(size)); }
void* operator new[](std::size_t size) { return or_throw(allocate(size)); }
void* operator new(std::size_t size, const std::nothrow_t& /*tag*/) noexcept { return allocate(size); }
void* operator new[](std::size_t size, const std::nothrow_t& /*tag*/) noexcept { return allocate(size); }

void* operator new(std::size_t size, std::align_val_t alignment) {
  return or_throw(allocate_aligned(size, alignment));
}
void* operator new[](std::size_t size, std::align_val_t alignment) {
  return or_throw(allocate_aligned(size, alignment));
}
void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t& /*tag*/) noexcept {
  return allocate_aligned(size, alignment);
}
void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t& /*tag*/) noexcept {
  return allocate_aligned(size, alignment);
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t /*size*/) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t /*size*/) noexcept { std::free(memory); }
void operator delete(void* memory, const std::nothrow_t& /*tag*/) noexcept { std::free(memory); }
void operator delete[](void* memory, const std::nothrow_t& /*tag*/) noexcept { std::free(memory); }

void operator delete(void* memory, std::align_val_t /*alignment*/) noexcept { std::free(memory); }
void operator delete[](void* memory, std::align_val_t /*alignment*/) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t /*size*/, std::align_val_t /*alignment*/) noexcept {
  std::free(memory);
}
void operator delete[](void* memory, std::size_t /*size*/, std::align_val_t /*alignment*/) noexcept {
  std::free(memory);
}
void operator delete(void* memory, std::align_val_t /*alignment*/, const std::nothrow_t& /*tag*/) noexcept {
  std::free(memory);
}
void operator delete[](void* memory, std::align_val_t /*alignment*/, const std::nothrow_t& /*tag*/) noexcept {
  std::free(memory);
}

namespace ics::testing {

NoAllocationScope::NoAllocationScope() noexcept : start_{allocation_count().load(std::memory_order_relaxed)} {}

NoAllocationScope::~NoAllocationScope() {
  const std::size_t count = allocations();
  if (count > 0) {
    ADD_FAILURE() << count << " allocation(s) after initialization inside a NoAllocationScope";
  }
}

std::size_t NoAllocationScope::allocations() const noexcept {
  return allocation_count().load(std::memory_order_relaxed) - start_;
}

}  // namespace ics::testing
