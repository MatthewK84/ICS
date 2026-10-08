#include "ics/plid/router.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <optional>
#include <span>
#include <utility>

#include "ics/capture/datagram.hpp"
#include "ics/common/check.hpp"
#include "ics/cot/event.hpp"
#include "ics/mavlink/frame.hpp"

namespace ics::plid {

Router::Router(std::optional<mavlink::Adapter> mavlink, std::optional<CotRoute> cot) noexcept
    : mavlink_(std::move(mavlink)), cot_(std::move(cot)) {}

Status Router::accept(const capture::Packet& packet) {
  ++counts_.packets;
  tick(packet.time);
  const std::optional<capture::Datagram> datagram = capture::udp_datagram(packet.bytes);
  if (!datagram) {
    ++counts_.not_udp;
    return {};
  }
  static_cast<void>(ics::check(datagram->payload.size() <= packet.bytes.size()));
  if (cot_ && datagram->destination.port == cot_->port) {
    to_cot(datagram->payload, packet.time);
  } else {
    to_mavlink(datagram->payload, packet.time);
  }
  return {};
}

void Router::tick(const UtcTime now) {
  if (mavlink_) {
    mavlink_->tick(now, mavlink_out_);
    drain_mavlink();
  }
}

void Router::to_mavlink(const std::span<const std::byte> payload, const UtcTime received) {
  if (!mavlink_) {
    return;
  }
  mavlink::FrameReader reader(payload);
  for (std::optional<mavlink::Frame> frame = reader.next(); frame; frame = reader.next()) {
    ++counts_.mavlink_frames;
    mavlink_->receive(*frame, received, mavlink_out_);
  }
  drain_mavlink();
}

void Router::to_cot(const std::span<const std::byte> payload, const UtcTime received) {
  const cot::Read read = cot::read_events(payload);
  counts_.cot_unreadable += read.events.empty() ? 1 : 0;
  for (const cot::Event& event : read.events) {
    ++counts_.cot_events;
    std::optional<v1::PliRecord> record = cot_->adapter.record(event, received);
    if (record) {
      pli_.records.push_back(std::move(*record));
    }
  }
}

// Moves what the MAVLink adapter produced into pli_, keeping the order the
// feeds' messages arrived in.
void Router::drain_mavlink() {
  for (mavlink::Position& position : mavlink_out_.positions) {
    pli_.records.push_back(std::move(position.record));
  }
  for (v1::PliEvent& event : mavlink_out_.events) {
    pli_.events.push_back(std::move(event));
  }
  mavlink_out_ = {};
}

void Router::take(Pli& out) {
  std::ranges::move(pli_.records, std::back_inserter(out.records));
  std::ranges::move(pli_.events, std::back_inserter(out.events));
  pli_ = {};
}

}  // namespace ics::plid
