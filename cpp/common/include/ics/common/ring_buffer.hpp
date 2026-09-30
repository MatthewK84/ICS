#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <utility>

#include "ics/common/check.hpp"
#include "ics/common/error.hpp"
#include "ics/common/storable.hpp"

namespace ics {

// A first-in, first-out queue with a fixed capacity N that never allocates
// (ICS-015). All N elements live inside the object and are default-constructed
// with it. It is not thread-safe: a queue between threads needs its own type.
template <Storable T, std::size_t N>
  requires(N > 0)
class RingBuffer {
 public:
  [[nodiscard]] static constexpr std::size_t capacity() noexcept { return N; }
  [[nodiscard]] std::size_t size() const noexcept { return size_; }
  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
  [[nodiscard]] bool full() const noexcept { return size_ == N; }

  // Appends value after the newest element. Fails with Error::kFull when the
  // buffer is full, keeping every stored element: it never overwrites.
  [[nodiscard]] Status push(T value) noexcept {
    if (!check(size_ < N)) {
      return fail(Error::kFull);
    }
    elements_[(head_ + size_) % N] = std::move(value);
    ++size_;
    return {};
  }

  // Removes and returns the oldest element; its slot goes back to T{}. Fails
  // with Error::kEmpty when the buffer is empty, which is an ordinary result
  // for a reader that polls, so it is not a check.
  [[nodiscard]] Result<T> pop() noexcept {
    if (size_ == 0) {
      return fail(Error::kEmpty);
    }
    T oldest = std::exchange(elements_[head_], T{});
    head_ = (head_ + 1) % N;
    --size_;
    return oldest;
  }

  // The oldest element, left in place. Fails with Error::kEmpty when the
  // buffer is empty.
  [[nodiscard]] Result<std::reference_wrapper<const T>> front() const noexcept {
    if (size_ == 0) {
      return fail(Error::kEmpty);
    }
    return std::cref(elements_[head_]);
  }

 private:
  std::array<T, N> elements_{};
  std::size_t head_ = 0;  // Index of the oldest element.
  std::size_t size_ = 0;
};

}  // namespace ics
