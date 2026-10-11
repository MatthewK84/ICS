#include "ics/camd/quality_watch.hpp"

#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>

#include <grpcpp/grpcpp.h>

#include "ics/common/check.hpp"
#include "ics/v1/time_quality_service.grpc.pb.h"

namespace ics::camd {

QualityWatch::QualityWatch(const std::filesystem::path& socket)
    : context_(std::make_unique<grpc::ClientContext>()), watcher_([this, socket] { watch(socket); }) {}

QualityWatch::~QualityWatch() {
  // A cancel before the call starts cancels it as it starts.
  context_->TryCancel();
  watcher_.join();
}

bool QualityWatch::wait_for_report(const Duration timeout) {
  std::unique_lock lock(mutex_);
  changed_.wait_for(lock, timeout, [this] { return reported_ || ended_; });
  return reported_;
}

v1::TimeQuality QualityWatch::current() {
  const std::scoped_lock lock(mutex_);
  return latest_;
}

void QualityWatch::watch(const std::filesystem::path& socket) {
  const std::unique_ptr<v1::TimeQualityService::Stub> stub = v1::TimeQualityService::NewStub(
      grpc::CreateChannel("unix:" + socket.native(), grpc::InsecureChannelCredentials()));
  const std::unique_ptr<grpc::ClientReader<v1::WatchTimeQualityResponse>> reader =
      stub->WatchTimeQuality(context_.get(), v1::WatchTimeQualityRequest());
  v1::WatchTimeQualityResponse response;
  while (reader->Read(&response)) {
    const std::scoped_lock lock(mutex_);
    latest_ = response.report();
    reported_ = true;
    changed_.notify_all();
  }
  // How the stream ended does not matter: current() keeps the latest report.
  static_cast<void>(reader->Finish());
  const std::scoped_lock lock(mutex_);
  // The stream ends once.
  static_cast<void>(ics::check(!ended_));
  ended_ = true;
  changed_.notify_all();
}

}  // namespace ics::camd
