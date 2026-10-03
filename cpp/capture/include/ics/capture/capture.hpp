#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>

#include "ics/capture/pcap_format.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"

struct pcap;

namespace ics::capture {

// Where a packet's time stamp comes from (ICS-020).
enum class TimestampSource : std::uint8_t {
  kHost = 0,     // The kernel's clock when the packet arrived: UTC, as the system clock keeps it.
  kAdapter = 1,  // The NIC's own clock (PHC), which ptp4l keeps on TAI.
};

// How to capture from one interface.
struct CaptureOptions {
  std::string interface;
  // The most bytes of each packet kept.
  std::uint32_t snaplen = 0;
  // The kernel ring the interface's packets wait in until read.
  std::uint32_t buffer_bytes = 0;
  TimestampSource timestamps = TimestampSource::kHost;
  // Subtracted from each time stamp to give UTC: TAI - UTC, as ptp4l
  // reports it, for adapter stamps; zero for host stamps.
  Duration stamp_minus_utc{};
};

// One captured packet. Its bytes are libpcap's, valid only during accept().
struct Packet {
  UtcTime time;
  std::uint32_t original_length = 0;
  std::span<const std::byte> bytes;
};

// libpcap's counters since the capture opened: packets the kernel received,
// dropped because the ring was full, and dropped by the interface itself.
struct Counters {
  std::uint64_t received = 0;
  std::uint64_t dropped = 0;
  std::uint64_t interface_dropped = 0;
};

// What dispatch() hands each packet to.
class PacketSink {
 public:
  // Takes one packet. An error stops dispatch(), which returns it.
  [[nodiscard]] virtual Status accept(const Packet& packet) = 0;

 protected:
  PacketSink() = default;
  ~PacketSink() = default;
  PacketSink(const PacketSink&) = default;
  PacketSink& operator=(const PacketSink&) = default;
  PacketSink(PacketSink&&) = default;
  PacketSink& operator=(PacketSink&&) = default;
};

// A libpcap capture (ICS-020): live from an interface, or replayed from a
// pcap file. Closed when destroyed.
class Capture {
 public:
  // Opens the interface in promiscuous mode, with nanosecond time stamps, for
  // non-blocking reads. A TAP port's packets are addressed to other hosts, so
  // promiscuous mode is needed to see them. Adapter stamps are the PHC's raw
  // time (libpcap's "adapter_unsynced"; Linux no longer converts them to
  // system time); an interface that cannot give them fails, rather than
  // silently falling back to host stamps. kUnavailable on failure, with
  // libpcap's explanation in reason; capturing needs CAP_NET_RAW, and
  // adapter stamps also CAP_NET_ADMIN.
  [[nodiscard]] static Result<Capture> open_live(const CaptureOptions& options, std::string& reason);

  // Opens a pcap file to replay, reading its time stamps to the nanosecond.
  // kUnreadable on failure, with libpcap's explanation in reason.
  [[nodiscard]] static Result<Capture> open_file(const std::filesystem::path& path, std::string& reason);

  // A descriptor to poll: readable when packets wait.
  [[nodiscard]] int fd() const noexcept;

  // The snaplen and link-layer type of the packets. The link-layer type is
  // libpcap's DLT, which is the file's LINKTYPE for Ethernet.
  [[nodiscard]] FileFormat format() const noexcept;

  // Hands up to max waiting packets to sink, without blocking: how many,
  // zero when none wait or a file is at its end. Fails with the sink's
  // error, or kUnreadable when libpcap fails, such as on a truncated file.
  [[nodiscard]] Result<std::size_t> dispatch(PacketSink& sink, int max) noexcept;

  // The counters so far. kUnavailable for a file, which has none.
  [[nodiscard]] Result<Counters> counters() noexcept;

 private:
  struct Close {
    void operator()(pcap* handle) const noexcept;
  };
  using Handle = std::unique_ptr<pcap, Close>;

  Capture(Handle handle, Duration stamp_minus_utc) noexcept;

  Handle handle_;
  Duration stamp_minus_utc_;
};

}  // namespace ics::capture
