#include "ics/timingd/report_board.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <vector>

#include "ics/common/check.hpp"

namespace ics::timingd {

void ReportBoard::reserve(const std::size_t bytes) {
  const std::scoped_lock lock(mutex_);
  report_.reserve(bytes);
}

void ReportBoard::post(const std::span<const std::byte> report) noexcept {
  {
    const std::scoped_lock lock(mutex_);
    // Within the capacity, assign copies without allocating.
    static_cast<void>(ics::check(report.size() <= report_.capacity()));
    report_.assign(report.begin(), report.end());
    ++number_;
  }
  posted_.notify_all();
}

ReportBoard::Taken ReportBoard::take_newer(const std::uint64_t seen, std::vector<std::byte>& out,
                                           const std::chrono::milliseconds timeout) {
  std::unique_lock lock(mutex_);
  static_cast<void>(posted_.wait_for(lock, timeout, [this, seen] { return closed_ || number_ > seen; }));
  if (closed_) {
    return {.wait = Wait::kClosed, .number = 0};
  }
  if (number_ <= seen) {
    return {.wait = Wait::kTimeout, .number = 0};
  }
  out.assign(report_.begin(), report_.end());
  return {.wait = Wait::kReport, .number = number_};
}

void ReportBoard::close() noexcept {
  {
    const std::scoped_lock lock(mutex_);
    closed_ = true;
  }
  posted_.notify_all();
}

}  // namespace ics::timingd
