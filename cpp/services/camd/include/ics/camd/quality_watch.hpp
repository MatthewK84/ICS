#pragma once

#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>

#include "ics/camera/segment_camera.hpp"
#include "ics/common/units.hpp"
#include "ics/v1/time_quality.pb.h"

namespace grpc {
class ClientContext;
}  // namespace grpc

namespace ics::camd {

// The station's time quality during one command (ICS-031), as ics-timingd
// streams it over gRPC on its Unix-domain socket (#150). It watches from the
// moment it is made until it is destroyed, on a thread of its own, and
// current() is safe from any thread. Until the first report comes, and when
// ics-timingd cannot be reached, current() is an empty TimeQuality, under
// which no frame is IRIG-timed: the cameras still record, and their segments
// do not verify.
class QualityWatch final : public camera::TimeQualitySource {
 public:
  explicit QualityWatch(const std::filesystem::path& socket);
  // Ends the stream and waits for the thread.
  ~QualityWatch() override;
  QualityWatch(const QualityWatch&) = delete;
  QualityWatch& operator=(const QualityWatch&) = delete;
  QualityWatch(QualityWatch&&) = delete;
  QualityWatch& operator=(QualityWatch&&) = delete;

  // Waits up to timeout for the first report: whether one came. It returns
  // at once when the stream has ended without one.
  [[nodiscard]] bool wait_for_report(Duration timeout);

  // The latest report.
  [[nodiscard]] v1::TimeQuality current() override;

 private:
  // The thread: reads each report until the stream ends.
  void watch(const std::filesystem::path& socket);

  std::unique_ptr<grpc::ClientContext> context_;
  std::mutex mutex_;
  std::condition_variable changed_;
  v1::TimeQuality latest_;
  bool reported_ = false;
  bool ended_ = false;
  // Last, so that it starts after the members it uses.
  std::thread watcher_;
};

}  // namespace ics::camd
