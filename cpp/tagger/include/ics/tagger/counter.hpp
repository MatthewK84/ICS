#pragma once

#include <cstdint>

namespace ics::tagger {

// Extends a free-running 32-bit hardware counter to 64 bits (ICS-032). Each
// reading is taken as the nearer of the counts on either side of the last, so
// a wrap counts, and so does a reading a little earlier than the last: a
// sample latched just before a PPS edge but handled after it. Consecutive
// readings must be less than 2^31 ticks apart, 10.7 s at 200 MHz; the tagger
// reads the counter 1,000 times a second. The first reading counts from
// 2^32, so a reading before it stays positive.
class CounterExtender {
 public:
  [[nodiscard]] std::uint64_t extend(const std::uint32_t raw) noexcept {
    if (!started_) {
      started_ = true;
      count_ = kEpoch + raw;
    } else {
      // The step from the last reading, modulo 2^32, as a signed count.
      const auto step = static_cast<std::int32_t>(raw - last_);
      count_ += static_cast<std::uint64_t>(static_cast<std::int64_t>(step));
    }
    last_ = raw;
    return count_;
  }

 private:
  static constexpr std::uint64_t kEpoch = std::uint64_t{1} << 32U;

  bool started_ = false;
  std::uint32_t last_ = 0;
  std::uint64_t count_ = 0;
};

}  // namespace ics::tagger
