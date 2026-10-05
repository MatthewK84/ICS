// ics-mavlink-replay (ICS-021, deploy/sitl): replays a pcap file of a TAP
// port through the MAVLink adapter and writes what it produced on stdout, one
// JSON object per line:
//
//   {"kind":"position","system":1,"time_boot_ms":654421,"msl_m":735.25,"record":{...}}
//   {"kind":"event","event":{...}}
//
// "record" and "event" are the ics.v1.PliRecord and PliEvent in protobuf's
// JSON form, with proto field names and zeros written out. time_boot_ms and msl_m are what the
// vehicle's GLOBAL_POSITION_INT gave, for matching against its own log; msl_m
// comes back from the record's ellipsoid height through EGM96.
//
// Usage: ics-mavlink-replay PCAP LATITUDE LONGITUDE HEIGHT [SYSTEM=ROLE]...
//
// LATITUDE, LONGITUDE and HEIGHT (metres above the ellipsoid) are the range
// ENU frame's origin. Each SYSTEM=ROLE gives a MAVLink system's role: target,
// interceptor, debris or other. The geoid grid is read from where the ICS
// images install it. Counts go to stderr. Exits 1 when the file cannot be
// read or does not hold Ethernet frames, and 2 for bad arguments.

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <google/protobuf/util/json_util.h>

#include "ics/capture/capture.hpp"
#include "ics/capture/datagram.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/frames/egm96.hpp"
#include "ics/frames/enu.hpp"
#include "ics/frames/geodetic.hpp"
#include "ics/mavlink/adapter.hpp"
#include "ics/mavlink/frame.hpp"

namespace {

using ics::mavlink::Adapter;
using ics::mavlink::Output;

constexpr int kBatch = 4096;
constexpr std::uint32_t kLinkTypeEthernet = 1;
constexpr std::size_t kFirstRole = 5;
constexpr std::uint64_t kMaxSystem = 255;

struct Plan {
  std::string_view path;
  ics::frames::Geodetic origin;
  ics::mavlink::AdapterSettings settings;
};

struct Counts {
  std::size_t packets = 0;
  std::size_t datagrams = 0;
  std::size_t frames = 0;
  std::size_t unknown = 0;
  std::size_t rejected_bytes = 0;
  std::size_t positions = 0;
  std::size_t events = 0;
};

template <typename T>
[[nodiscard]] std::optional<T> number(const std::string_view text) noexcept {
  T out{};
  const std::from_chars_result read = std::from_chars(text.begin(), text.end(), out);
  if (read.ec != std::errc() || read.ptr != text.end() || text.empty()) {
    return std::nullopt;
  }
  return out;
}

[[nodiscard]] std::optional<ics::v1::EntityRole> role_named(const std::string_view name) noexcept {
  if (name == "target") {
    return ics::v1::ENTITY_ROLE_TARGET;
  }
  if (name == "interceptor") {
    return ics::v1::ENTITY_ROLE_INTERCEPTOR;
  }
  if (name == "debris") {
    return ics::v1::ENTITY_ROLE_DEBRIS;
  }
  if (name == "other") {
    return ics::v1::ENTITY_ROLE_OTHER;
  }
  return std::nullopt;
}

// SYSTEM=ROLE, or nothing.
[[nodiscard]] std::optional<ics::mavlink::RoleAssignment> assignment(const std::string_view text) {
  const std::size_t equals = text.find('=');
  const std::optional<std::uint64_t> system =
      equals == std::string_view::npos ? std::nullopt : number<std::uint64_t>(text.substr(0, equals));
  const std::optional<ics::v1::EntityRole> role =
      system && *system <= kMaxSystem ? role_named(text.substr(equals + 1)) : std::nullopt;
  if (!role) {
    return std::nullopt;
  }
  return ics::mavlink::RoleAssignment{.system = static_cast<std::uint8_t>(*system), .role = *role};
}

[[nodiscard]] std::optional<Plan> parse(const std::span<const char* const> args) {
  if (args.size() < kFirstRole) {
    return std::nullopt;
  }
  const std::optional<double> latitude = number<double>(args[2]);
  const std::optional<double> longitude = number<double>(args[3]);
  const std::optional<double> height = number<double>(args[4]);
  const ics::Result<ics::frames::Geodetic> origin =
      latitude && longitude && height
          ? ics::frames::Geodetic::make(ics::Degrees(*latitude), ics::Degrees(*longitude), ics::Meters(*height))
          : ics::fail(ics::Error::kInvalidArgument);
  if (!origin) {
    return std::nullopt;
  }
  Plan plan{.path = args[1], .origin = *origin, .settings = {}};
  for (const char* const text : args.subspan(kFirstRole)) {
    const std::optional<ics::mavlink::RoleAssignment> assigned = assignment(text);
    if (!assigned) {
      return std::nullopt;
    }
    plan.settings.roles.push_back(*assigned);
  }
  return plan;
}

// The JSON of a message, or nothing if protobuf cannot write it.
[[nodiscard]] std::optional<std::string> json(const google::protobuf::Message& message) {
  google::protobuf::util::JsonPrintOptions options;
  options.preserve_proto_field_names = true;
  options.always_print_fields_with_no_presence = true;
  std::string out;
  if (!google::protobuf::util::MessageToJsonString(message, &out, options).ok()) {
    return std::nullopt;
  }
  return out;
}

// Feeds each packet's frames to the adapter and prints what comes out.
class Replayer final : public ics::capture::PacketSink {
 public:
  Replayer(Adapter& adapter, const ics::frames::Egm96& geoid) : adapter_(adapter), geoid_(geoid) {}

  ics::Status accept(const ics::capture::Packet& packet) override {
    ++counts_.packets;
    adapter_.tick(packet.time, out_);
    const std::optional<ics::capture::Datagram> datagram = ics::capture::udp_datagram(packet.bytes);
    if (datagram) {
      ++counts_.datagrams;
      read_frames(datagram->payload, packet.time);
    }
    return print();
  }

  [[nodiscard]] const Counts& counts() const noexcept { return counts_; }

 private:
  void read_frames(const std::span<const std::byte> payload, const ics::UtcTime received) {
    ics::mavlink::FrameReader reader(payload);
    for (std::optional<ics::mavlink::Frame> frame = reader.next(); frame; frame = reader.next()) {
      ++counts_.frames;
      adapter_.receive(*frame, received, out_);
    }
    counts_.unknown += reader.counts().unknown;
    counts_.rejected_bytes += reader.counts().rejected_bytes;
  }

  [[nodiscard]] ics::Status print() {
    for (const ics::mavlink::Position& position : out_.positions) {
      const std::optional<std::string> record = json(position.record);
      const ics::v1::GeodeticPoint& point = position.record.position();
      const ics::Result<ics::frames::Geodetic> where = ics::frames::Geodetic::make(
          ics::Degrees(point.latitude_deg()), ics::Degrees(point.longitude_deg()), ics::Meters(point.height_ellipsoid_m()));
      if (!record || !where) {
        return ics::fail(ics::Error::kMalformed);
      }
      std::printf("{\"kind\":\"position\",\"system\":%s,\"time_boot_ms\":%u,\"msl_m\":%.4f,\"record\":%s}\n",
                  position.record.entity_id().c_str(), position.time_boot_ms, geoid_.msl_height(*where).value(),
                  record->c_str());
    }
    for (const ics::v1::PliEvent& event : out_.events) {
      const std::optional<std::string> text = json(event);
      if (!text) {
        return ics::fail(ics::Error::kMalformed);
      }
      std::printf("{\"kind\":\"event\",\"event\":%s}\n", text->c_str());
    }
    counts_.positions += out_.positions.size();
    counts_.events += out_.events.size();
    out_.positions.clear();
    out_.events.clear();
    out_.clocks.clear();
    return {};
  }

  Adapter& adapter_;
  const ics::frames::Egm96& geoid_;
  Output out_;
  Counts counts_;
};

// Replays every packet: true unless libpcap or the output failed.
bool replay_all(ics::capture::Capture& capture, Replayer& replayer) {
  for (ics::Result<std::size_t> count = std::size_t{1}; count; count = capture.dispatch(replayer, kBatch)) {
    if (*count == 0) {
      return true;
    }
  }
  return false;
}

void report(const Counts& counts, const ics::mavlink::AdapterCounts& adapter) {
  std::fprintf(stderr,
               "packets %zu, UDP datagrams %zu, frames read %zu, frames of other messages %zu, bytes rejected %zu, "
               "positions %zu (%zu out of range), events %zu\n",
               counts.packets, counts.datagrams, counts.frames, counts.unknown, counts.rejected_bytes, counts.positions,
               adapter.bad_positions, counts.events);
}

int replay(Plan plan) {
  const ics::Result<ics::frames::Egm96> geoid = ics::frames::Egm96::load(ics::frames::Egm96::kDefaultPath);
  std::string reason;
  ics::Result<ics::capture::Capture> capture = ics::capture::Capture::open_file(plan.path, reason);
  if (!geoid || !capture) {
    std::fprintf(stderr, "%s\n", geoid ? reason.c_str() : "cannot read the EGM96 grid");
    return 1;
  }
  if (capture->format().linktype != kLinkTypeEthernet) {
    std::fprintf(stderr, "%s does not hold Ethernet frames\n", std::string(plan.path).c_str());
    return 1;
  }
  Adapter adapter(std::move(plan.settings), *geoid, ics::frames::EnuFrame(plan.origin));
  Replayer replayer(adapter, *geoid);
  const bool replayed = replay_all(*capture, replayer);
  report(replayer.counts(), adapter.counts());
  if (!replayed) {
    std::fprintf(stderr, "%s: replay failed partway\n", std::string(plan.path).c_str());
  }
  return replayed ? 0 : 1;
}

}  // namespace

int main(const int argc, char* argv[]) {
  std::optional<Plan> plan = parse(std::span<const char* const>(argv, static_cast<std::size_t>(argc)));
  if (!plan) {
    std::fputs("usage: ics-mavlink-replay PCAP LATITUDE LONGITUDE HEIGHT [SYSTEM=ROLE]...\n", stderr);
    return 2;
  }
  return replay(std::move(*plan));
}
