#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <vector>

namespace ics::timingd {

// The latest report, handed from ics-timingd's poll loop to the threads that
// send it to subscribers (#150). The loop posts each serialized report
// without allocating and without waiting for any subscriber, only for a
// subscriber's thread that is copying the previous report out, which takes
// microseconds. A subscriber's thread waits for a report newer than the last
// it took. One slower than the loop skips to the latest report: subscribers
// want the station's current state, not its history.
class ReportBoard {
 public:
  // What take_newer found.
  enum class Wait : std::uint8_t {
    kReport,   // A newer report, copied out.
    kTimeout,  // No newer report in time.
    kClosed,   // The board is closed: no more reports will come.
  };

  // The outcome of take_newer, and the report's number when it took one.
  struct Taken {
    Wait wait = Wait::kTimeout;
    std::uint64_t number = 0;
  };

  // Makes room for a report of up to bytes, so that post allocates nothing.
  void reserve(std::size_t bytes);

  // Replaces the latest report with report, which must fit the room
  // reserved, numbers it one more than the last, and wakes the waiting
  // threads. Allocates nothing.
  void post(std::span<const std::byte> report) noexcept;

  // Waits up to timeout for a report numbered after seen (0 before the first),
  // and copies it to out. Reports are numbered from 1.
  [[nodiscard]] Taken take_newer(std::uint64_t seen, std::vector<std::byte>& out, std::chrono::milliseconds timeout);

  // Wakes every waiting thread; from now on take_newer returns kClosed.
  void close() noexcept;

 private:
  std::mutex mutex_;
  std::condition_variable posted_;
  // The latest report; its capacity is the room reserved.
  std::vector<std::byte> report_;
  std::uint64_t number_ = 0;
  bool closed_ = false;
};

}  // namespace ics::timingd
