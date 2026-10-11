#pragma once

#include <cstddef>

namespace ics::testing {

// Selects the NoAllocationScope that counts only its own thread.
struct ThisThreadOnly {
  explicit ThisThreadOnly() = default;
};

// Fails the running GoogleTest test if anything allocates while the scope is
// alive (ICS-005). Linking ics_alloc_guard, which ics_add_gtest does for every
// test, replaces the global operator new and delete so that every allocation
// on every thread is counted. Open a scope once a component is initialized and
// run its steady-state path inside it:
//
//   Pipeline pipeline = make_pipeline(config);  // may allocate
//   {
//     const ics::testing::NoAllocationScope no_allocation;
//     pipeline.process(frame);                  // must not allocate
//   }
//
// When the scope ends it records one test failure naming the number of
// allocations. Allocations made through malloc directly are not counted;
// cppcoreguidelines-no-malloc keeps them out of ICS code.
//
// Given ThisThreadOnly, the scope counts only the allocations of the thread
// that opens it, which must also close it. That is for a steady-state path
// that runs beside threads that may allocate, such as the gRPC threads that
// send ics-timingd's reports (#150):
//
//   const ics::testing::NoAllocationScope no_allocation{ics::testing::ThisThreadOnly{}};
//   service.step(logger);                      // this thread must not allocate
class NoAllocationScope {
 public:
  NoAllocationScope() noexcept;
  explicit NoAllocationScope(ThisThreadOnly only) noexcept;
  ~NoAllocationScope();

  NoAllocationScope(const NoAllocationScope&) = delete;
  NoAllocationScope& operator=(const NoAllocationScope&) = delete;
  NoAllocationScope(NoAllocationScope&&) = delete;
  NoAllocationScope& operator=(NoAllocationScope&&) = delete;

  // Allocations counted since the scope opened.
  [[nodiscard]] std::size_t allocations() const noexcept;

 private:
  const bool this_thread_;
  const std::size_t start_;
};

}  // namespace ics::testing
