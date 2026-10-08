#include "ics/retime/sorties.hpp"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "ics/capture/capture.hpp"
#include "ics/capture/datagram.hpp"
#include "ics/common/check.hpp"
#include "ics/mavlink/frame.hpp"

namespace ics::retime {
namespace {

constexpr int kBatch = 4096;
constexpr std::uint32_t kLinkTypeEthernet = 1;
constexpr std::int64_t kUsPerMs = 1'000;
constexpr std::int64_t kNsPerUs = 1'000;

// The MAVLink system a record is from: the adapter names it in entity_id.
std::uint8_t system_of(const v1::PliRecord& record) {
  unsigned system = 0;
  const std::string_view id = record.entity_id();
  static_cast<void>(std::from_chars(id.begin(), id.end(), system));
  return static_cast<std::uint8_t>(system);
}

// The sortie a boot time belongs to, made if it is the first of its sortie.
Sortie& sortie_of(Vehicle& vehicle, const std::int64_t boot_us) {
  const std::size_t index = vehicle.counter.sortie(boot_us);
  if (vehicle.sorties.size() <= index) {
    vehicle.sorties.resize(index + 1);
  }
  static_cast<void>(ics::check(index < vehicle.sorties.size()));
  return vehicle.sorties[index];
}

// Feeds each packet's frames to the adapter and sorts what comes out.
class Collector final : public capture::PacketSink {
 public:
  Collector(mavlink::Adapter& adapter, Vehicles& vehicles) : adapter_(adapter), vehicles_(vehicles) {}

  Status accept(const capture::Packet& packet) override {
    const std::optional<capture::Datagram> datagram = capture::udp_datagram(packet.bytes);
    mavlink::FrameReader reader(datagram ? datagram->payload : std::span<const std::byte>());
    for (std::optional<mavlink::Frame> frame = reader.next(); frame; frame = reader.next()) {
      adapter_.receive(*frame, packet.time, out_);
    }
    sort();
    return {};
  }

 private:
  void sort() {
    for (const mavlink::SystemClock& clock : out_.clocks) {
      const std::int64_t boot_us = static_cast<std::int64_t>(clock.time_boot_ms) * kUsPerMs;
      sortie_of(vehicles_[clock.system], boot_us)
          .samples.push_back(timealign::ClockSample{
              .boot_us = boot_us, .utc_ns = static_cast<std::int64_t>(clock.time_unix_usec) * kNsPerUs});
    }
    for (mavlink::Position& position : out_.positions) {
      const std::int64_t boot_us = static_cast<std::int64_t>(position.time_boot_ms) * kUsPerMs;
      sortie_of(vehicles_[system_of(position.record)], boot_us)
          .positions.push_back(Positioned{.record = std::move(position.record), .boot_us = boot_us});
    }
    out_.clocks.clear();
    out_.positions.clear();
    out_.events.clear();
  }

  mavlink::Adapter& adapter_;
  Vehicles& vehicles_;
  mavlink::Output out_;
};

// Replays one capture into collector.
[[nodiscard]] Status replay(const std::filesystem::path& path, Collector& collector, std::string& reason) {
  Result<capture::Capture> capture = capture::Capture::open_file(path, reason);
  if (!capture) {
    return fail(capture.error());
  }
  if (capture->format().linktype != kLinkTypeEthernet) {
    reason = path.string() + " does not hold Ethernet frames";
    return fail(Error::kMalformed);
  }
  Result<std::size_t> count = std::size_t{1};
  while (count.value_or(0) > 0) {
    count = capture->dispatch(collector, kBatch);
  }
  if (!count) {
    reason = path.string() + ": the replay failed partway";
    return fail(Error::kUnreadable);
  }
  return {};
}

}  // namespace

Result<Vehicles> collect(const std::span<const std::filesystem::path> captures,
                         const mavlink::AdapterSettings& settings, const frames::Egm96& geoid,
                         const frames::EnuFrame& range, std::string& reason) {
  mavlink::Adapter adapter(settings, geoid, range);
  Vehicles vehicles;
  Collector collector(adapter, vehicles);
  Status replayed;
  for (const std::filesystem::path& path : captures) {
    replayed = replayed.and_then([&] { return replay(path, collector, reason); });
  }
  return replayed.map([&vehicles] { return std::move(vehicles); });
}

}  // namespace ics::retime
