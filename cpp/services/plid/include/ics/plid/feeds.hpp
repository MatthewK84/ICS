#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ics/capture/capture.hpp"
#include "ics/common/error.hpp"
#include "ics/frames/egm96.hpp"
#include "ics/frames/enu.hpp"
#include "ics/logging/logger.hpp"
#include "ics/plid/config.hpp"
#include "ics/plid/lattice_feed.hpp"
#include "ics/plid/router.hpp"
#include "ics/plid/sapient_link.hpp"

namespace ics::plid {

// What ics-plid captures from: a TAP port, or a pcap file it replays to the
// end.
struct Source {
  capture::Capture capture;
  bool file = false;
  bool done = false;
};

// The parts of ics-plid that read the feeds (ICS-030).
struct Feeds {
  Router router;
  std::vector<Source> sources;
  // Whether the sources are TAP ports, so the clock moves on between packets.
  bool live = false;
  std::optional<SapientLink> sapient;
  std::unique_ptr<LatticeFeed> lattice;
};

// The kernel ring each TAP port gets, and the bytes kept per packet.
inline constexpr std::uint32_t kCaptureBufferBytes = std::uint32_t{1} << 24U;
inline constexpr std::uint32_t kSnaplen = 65'535;

// Opens the feeds config lists, with their roles, in the range ENU frame
// around range, and the captures that carry the UDP ones, with host time
// stamps. The geoid and logger must outlive the feeds. Fails, with an
// explanation in reason: kInvalidArgument for a role that does not parse, a
// SAPIENT address that is not IPv4, or Lattice settings StreamClient::make
// refuses; as read_token fails for a Lattice token file; and as
// Capture::open_live and open_file fail.
[[nodiscard]] Result<Feeds> open_feeds(const Config& config, const frames::Egm96& geoid,
                                       const frames::EnuFrame& range, const logging::Logger& logger,
                                       std::string& reason);

}  // namespace ics::plid
