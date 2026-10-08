#include "ics/plid/lattice_feed.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace ics::plid {

LatticeFeed::LatticeFeed(lattice::StreamClient client, lattice::Adapter adapter, const logging::Logger& logger)
    : client_(std::move(client)), adapter_(std::move(adapter)), logger_(logger), thread_([this] { run(); }) {}

LatticeFeed::~LatticeFeed() {
  stop_.store(true);
  thread_.join();
}

void LatticeFeed::Sink::accept(const lattice::Event& event, const UtcTime received) {
  std::optional<v1::PliRecord> record = feed_.adapter_.record(event, received);
  if (record) {
    const std::scoped_lock lock(feed_.mutex_);
    feed_.waiting_.push_back(std::move(*record));
  }
}

void LatticeFeed::run() {
  Sink sink(*this);
  const Status ran = client_.run(sink, stop_);
  if (!ran) {
    logger_.error("lattice_refused", {{"http_status", client_.stats().last_http_status},
                                      {"error", client_.stats().last_error}});
  }
  ended_.store(true);
}

void LatticeFeed::take(std::vector<v1::PliRecord>& out) {
  const std::scoped_lock lock(mutex_);
  std::ranges::move(waiting_, std::back_inserter(out));
  waiting_.clear();
}

}  // namespace ics::plid
