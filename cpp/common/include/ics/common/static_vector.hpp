#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <span>
#include <utility>

#include "ics/common/check.hpp"
#include "ics/common/error.hpp"
#include "ics/common/storable.hpp"

namespace ics {

// A vector with a fixed capacity N that never allocates (ICS-015). All N
// elements live inside the object and are default-constructed with it; the
// first size() of them are in use.
template <Storable T, std::size_t N>
  requires(N > 0)
class StaticVector {
 public:
  [[nodiscard]] static constexpr std::size_t capacity() noexcept { return N; }
  [[nodiscard]] std::size_t size() const noexcept { return size_; }
  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
  [[nodiscard]] bool full() const noexcept { return size_ == N; }

  // The elements in use.
  [[nodiscard]] std::span<T> span() noexcept { return std::span<T>(elements_).first(size_); }
  [[nodiscard]] std::span<const T> span() const noexcept { return std::span<const T>(elements_).first(size_); }

  // Appends value. Fails with Error::kFull when the vector is full.
  [[nodiscard]] Status push_back(T value) noexcept {
    if (!check(size_ < N)) {
      return fail(Error::kFull);
    }
    elements_[size_] = std::move(value);
    ++size_;
    return {};
  }

  // Removes and returns the last element; its slot goes back to T{}. Fails
  // with Error::kEmpty when the vector is empty.
  [[nodiscard]] Result<T> pop_back() noexcept {
    if (!check(size_ > 0)) {
      return fail(Error::kEmpty);
    }
    --size_;
    return std::exchange(elements_[size_], T{});
  }

  // The element at index. Fails with Error::kOutOfRange at or past size().
  [[nodiscard]] Result<std::reference_wrapper<T>> at(const std::size_t index) noexcept {
    if (!check(index < size_)) {
      return fail(Error::kOutOfRange);
    }
    return std::ref(elements_[index]);
  }

  [[nodiscard]] Result<std::reference_wrapper<const T>> at(const std::size_t index) const noexcept {
    if (!check(index < size_)) {
      return fail(Error::kOutOfRange);
    }
    return std::cref(elements_[index]);
  }

 private:
  std::array<T, N> elements_{};
  std::size_t size_ = 0;
};

}  // namespace ics
