#include "ics/plid/archiver.hpp"

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <utility>

#include "ics/common/check.hpp"
#include "ics/common/error.hpp"

namespace ics::plid {

Archiver::Archiver(const logging::Logger& logger, const store::RowGroupLimits limits)
    : logger_(logger), limits_(limits), thread_([this] { work(); }) {}

Archiver::~Archiver() {
  {
    const std::scoped_lock lock(mutex_);
    stopping_ = true;
  }
  changed_.notify_all();
  thread_.join();
}

void Archiver::add(std::filesystem::path segment) {
  {
    const std::scoped_lock lock(mutex_);
    queue_.push_back(std::move(segment));
  }
  changed_.notify_all();
}

void Archiver::wait_idle() {
  std::unique_lock lock(mutex_);
  changed_.wait(lock, [this] { return queue_.empty() && !busy_; });
}

std::uint64_t Archiver::archived() const {
  const std::scoped_lock lock(mutex_);
  return archived_;
}

std::uint64_t Archiver::failed() const {
  const std::scoped_lock lock(mutex_);
  return failed_;
}

// Takes each queued segment in turn until stopped with none left.
void Archiver::work() {
  std::unique_lock lock(mutex_);
  changed_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
  while (!queue_.empty()) {
    const std::filesystem::path segment = std::move(queue_.front());
    queue_.pop_front();
    static_cast<void>(ics::check(!busy_));
    busy_ = true;
    lock.unlock();
    archive(segment);
    lock.lock();
    busy_ = false;
    changed_.notify_all();
    changed_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
  }
}

void Archiver::archive(const std::filesystem::path& segment) {
  const Result<store::ArchiveCounts> counts = store::archive_segment(segment, limits_);
  const std::scoped_lock lock(mutex_);
  if (!counts) {
    ++failed_;
    logger_.warn("archive_failed", {{"segment", segment.native()}, {"error", to_string(counts.error())}});
    return;
  }
  ++archived_;
  logger_.info("archived", {{"segment", segment.native()},
                            {"records", static_cast<std::int64_t>(counts->records)},
                            {"events", static_cast<std::int64_t>(counts->events)}});
}

}  // namespace ics::plid
