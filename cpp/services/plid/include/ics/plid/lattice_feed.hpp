#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ics/common/error.hpp"
#include "ics/lattice/adapter.hpp"
#include "ics/lattice/stream_client.hpp"
#include "ics/logging/logger.hpp"
#include "ics/v1/pli.pb.h"

namespace ics::plid {

// The Lattice entity stream (ICS-023), read in a thread of its own because
// libcurl blocks (ICS-030). Each entity's record waits in the feed until the
// service takes it. When Lattice refuses the request, the thread logs
// "lattice_refused" with the HTTP status and ends; the other feeds go on.
class LatticeFeed {
 public:
  // Starts the thread. The logger must outlive the feed.
  LatticeFeed(lattice::StreamClient client, lattice::Adapter adapter, const logging::Logger& logger);

  // Stops the stream and the thread.
  ~LatticeFeed();
  LatticeFeed(const LatticeFeed&) = delete;
  LatticeFeed& operator=(const LatticeFeed&) = delete;
  LatticeFeed(LatticeFeed&&) = delete;
  LatticeFeed& operator=(LatticeFeed&&) = delete;

  // Moves the records waiting into out.
  void take(std::vector<v1::PliRecord>& out);

  // Whether the thread has ended: Lattice refused the request.
  [[nodiscard]] bool ended() const noexcept { return ended_.load(); }

 private:
  // Takes each event from the client thread.
  class Sink final : public lattice::EventSink {
   public:
    explicit Sink(LatticeFeed& feed) noexcept : feed_(feed) {}
    void accept(const lattice::Event& event, UtcTime received) override;

   private:
    LatticeFeed& feed_;
  };

  void run();

  lattice::StreamClient client_;
  lattice::Adapter adapter_;
  const logging::Logger& logger_;
  std::mutex mutex_;
  std::vector<v1::PliRecord> waiting_;
  std::atomic<bool> stop_{false};
  std::atomic<bool> ended_{false};
  std::thread thread_;
};

}  // namespace ics::plid
