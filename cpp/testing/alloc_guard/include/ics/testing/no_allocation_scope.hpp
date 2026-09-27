#pragma once

#include <cstddef>

namespace ics::testing {

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
class NoAllocationScope {
 public:
  NoAllocationScope() noexcept;
  ~NoAllocationScope();

  NoAllocationScope(const NoAllocationScope&) = delete;
  NoAllocationScope& operator=(const NoAllocationScope&) = delete;
  NoAllocationScope(NoAllocationScope&&) = delete;
  NoAllocationScope& operator=(NoAllocationScope&&) = delete;

  // Allocations counted since the scope opened.
  [[nodiscard]] std::size_t allocations() const noexcept;

 private:
  const std::size_t start_;
};

}  // namespace ics::testing
