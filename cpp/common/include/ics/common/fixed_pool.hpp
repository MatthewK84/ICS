#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <utility>

#include "ics/common/check.hpp"
#include "ics/common/error.hpp"
#include "ics/common/storable.hpp"

namespace ics {

// A handle to an object in a FixedPool. The generation tells a handle to a
// released object apart from a handle to the object that reused its slot.
struct PoolHandle {
  std::uint32_t index = 0;
  std::uint32_t generation = 0;
};

// Up to N objects held in place, reached through handles, that never
// allocates (ICS-015). All N slots live inside the object and are
// default-constructed with it. Acquiring and releasing take constant time.
//
// A slot's generation counts its releases and wraps after 2^32 of them, so a
// handle kept across that many reuses of one slot would match again.
template <Storable T, std::size_t N>
  requires(N > 0 && N <= std::numeric_limits<std::uint32_t>::max())
class FixedPool {
 public:
  [[nodiscard]] static constexpr std::size_t capacity() noexcept { return N; }
  // Objects acquired and not yet released.
  [[nodiscard]] std::size_t size() const noexcept { return size_; }

  // Stores value in a free slot and returns its handle. Fails with
  // Error::kFull when every slot is in use.
  [[nodiscard]] Result<PoolHandle> acquire(T value) noexcept {
    if (!check(size_ < N)) {
      return fail(Error::kFull);
    }
    const std::uint32_t index = take_free_slot();
    Slot& slot = slots_[index];
    slot.value = std::move(value);
    slot.in_use = true;
    ++size_;
    return PoolHandle{index, slot.generation};
  }

  // Releases the handle's object; its slot goes back to T{}. Fails as find()
  // does.
  [[nodiscard]] Status release(const PoolHandle handle) noexcept {
    const Result<std::uint32_t> index = find(handle);
    if (!index) {
      return fail(index.error());
    }
    Slot& slot = slots_[*index];
    slot.value = T{};
    slot.in_use = false;
    ++slot.generation;
    released_[released_count_] = *index;
    ++released_count_;
    --size_;
    return {};
  }

  // The handle's object. Fails as find() does.
  [[nodiscard]] Result<std::reference_wrapper<T>> get(const PoolHandle handle) noexcept {
    const Result<std::uint32_t> index = find(handle);
    if (!index) {
      return fail(index.error());
    }
    return std::ref(slots_[*index].value);
  }

  [[nodiscard]] Result<std::reference_wrapper<const T>> get(const PoolHandle handle) const noexcept {
    const Result<std::uint32_t> index = find(handle);
    if (!index) {
      return fail(index.error());
    }
    return std::cref(slots_[*index].value);
  }

 private:
  struct Slot {
    T value{};
    std::uint32_t generation = 0;
    bool in_use = false;
  };

  // The slot of a live handle. Fails with Error::kInvalidArgument for an index
  // no acquire() returns, and with Error::kStaleHandle once the handle's
  // object has been released.
  [[nodiscard]] Result<std::uint32_t> find(const PoolHandle handle) const noexcept {
    if (!check(handle.index < N)) {
      return fail(Error::kInvalidArgument);
    }
    const Slot& slot = slots_[handle.index];
    if (!check(slot.in_use && slot.generation == handle.generation)) {
      return fail(Error::kStaleHandle);
    }
    return handle.index;
  }

  // A free slot, which must exist: the most recently released one, or else
  // the first never used. Every slot below never_used_ is either in use or
  // released, so size_ < N means one of the two is available.
  [[nodiscard]] std::uint32_t take_free_slot() noexcept {
    if (released_count_ > 0) {
      --released_count_;
      return released_[released_count_];
    }
    const std::uint32_t index = never_used_;
    ++never_used_;
    return index;
  }

  std::array<Slot, N> slots_{};
  std::array<std::uint32_t, N> released_{};  // Stack of released slots.
  std::size_t released_count_ = 0;
  std::uint32_t never_used_ = 0;  // Slots at or past this index were never acquired.
  std::size_t size_ = 0;
};

}  // namespace ics
