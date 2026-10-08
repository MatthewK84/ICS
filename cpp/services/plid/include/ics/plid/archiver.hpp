#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>

#include "ics/logging/logger.hpp"
#include "ics/store/archive.hpp"

namespace ics::plid {

// Archives closed segments as Parquet (ICS-030) in a thread of its own, so a
// long archive never holds up the feeds. Logs "archived" with each segment's
// counts, or warns "archive_failed" with the error.
class Archiver {
 public:
  // Starts the thread. The logger must outlive the archiver.
  Archiver(const logging::Logger& logger, store::RowGroupLimits limits);

  // Archives every segment added, then stops the thread.
  ~Archiver();
  Archiver(const Archiver&) = delete;
  Archiver& operator=(const Archiver&) = delete;
  Archiver(Archiver&&) = delete;
  Archiver& operator=(Archiver&&) = delete;

  // Queues a closed segment.
  void add(std::filesystem::path segment);

  // Waits until every segment added so far is archived or has failed.
  void wait_idle();

  // Segments archived, and failed.
  [[nodiscard]] std::uint64_t archived() const;
  [[nodiscard]] std::uint64_t failed() const;

 private:
  void work();
  void archive(const std::filesystem::path& segment);

  const logging::Logger& logger_;
  store::RowGroupLimits limits_;
  mutable std::mutex mutex_;
  std::condition_variable changed_;
  std::deque<std::filesystem::path> queue_;
  bool busy_ = false;
  bool stopping_ = false;
  std::uint64_t archived_ = 0;
  std::uint64_t failed_ = 0;
  std::thread thread_;
};

}  // namespace ics::plid
