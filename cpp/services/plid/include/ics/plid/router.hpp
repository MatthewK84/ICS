#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "ics/capture/capture.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/cot/adapter.hpp"
#include "ics/mavlink/adapter.hpp"
#include "ics/v1/pli.pb.h"

namespace ics::plid {

// PLI from the feeds, waiting to be stored.
struct Pli {
  std::vector<v1::PliRecord> records;
  std::vector<v1::PliEvent> events;
};

// The CoT feed: its adapter, and the UDP port its datagrams go to.
struct CotRoute {
  std::uint16_t port = 0;
  cot::Adapter adapter;
};

struct RouterCounts {
  std::uint64_t packets = 0;
  // Packets that are not UDP over IPv4 in Ethernet.
  std::uint64_t not_udp = 0;
  std::uint64_t mavlink_frames = 0;
  std::uint64_t cot_events = 0;
  // CoT datagrams that hold no event ICS can read.
  std::uint64_t cot_unreadable = 0;
};

// Sends each UDP datagram captured from a TAP port to its feed's adapter
// (ICS-030): one to the CoT port to the CoT adapter, and every other to the
// MAVLink adapter, which keeps only MAVLink frames, as ics-mavlink-replay
// does. Each packet first ticks the MAVLink adapter at its time stamp, so a
// replayed capture reports lost links as a live one does.
class Router final : public capture::PacketSink {
 public:
  Router(std::optional<mavlink::Adapter> mavlink, std::optional<CotRoute> cot) noexcept;

  [[nodiscard]] Status accept(const capture::Packet& packet) override;

  // Reports the MAVLink links silent too long at now.
  void tick(UtcTime now);

  // Moves what the adapters produced into out.
  void take(Pli& out);

  [[nodiscard]] const RouterCounts& counts() const noexcept { return counts_; }

 private:
  void to_mavlink(std::span<const std::byte> payload, UtcTime received);
  void to_cot(std::span<const std::byte> payload, UtcTime received);
  void drain_mavlink();

  std::optional<mavlink::Adapter> mavlink_;
  std::optional<CotRoute> cot_;
  mavlink::Output mavlink_out_;
  Pli pli_;
  RouterCounts counts_;
};

}  // namespace ics::plid
