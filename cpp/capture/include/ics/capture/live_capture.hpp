#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ics/capture/rotating_writer.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"

// libpcap's capture handle, pcap_t.
struct pcap;

namespace ics::capture {

// Where packet time stamps come from.
enum class TimestampSource : std::uint8_t {
  kHost = 1,     // The kernel's clock when the packet arrived.
  kAdapter = 2,  // The NIC's hardware clock (PCAP_TSTAMP_ADAPTER_UNSYNCED).
};

struct CaptureSettings {
  std::string interface;
  std::uint32_t snaplen = 0;
  // The size of the kernel ring the packets wait in.
  std::uint32_t buffer_bytes = 0;
  TimestampSource timestamps = TimestampSource::kHost;
};

// libpcap's counters since the capture opened. They are 32-bit and wrap, so
// take differences of them as 32-bit.
struct CaptureStats {
  // Packets the capture saw.
  std::uint32_t received = 0;
  // Packets dropped because the ring was full.
  std::uint32_t dropped = 0;
  // Packets the interface dropped.
  std::uint32_t interface_dropped = 0;
};

// One interface captured with libpcap (ICS-020): promiscuous, non-blocking,
// with nanosecond time stamps. Capturing needs CAP_NET_RAW, and hardware
// time stamps CAP_NET_ADMIN.
class LiveCapture {
 public:
  // kInvalidArgument when the interface cannot give the time stamps asked
  // for; kUnavailable when libpcap cannot open it (no such interface, no
  // permission, or no descriptor to spare).
  [[nodiscard]] static Result<LiveCapture> open(const CaptureSettings& settings);

  // Readable when packets are waiting.
  [[nodiscard]] int fd() const noexcept;
  [[nodiscard]] std::uint32_t link_type() const noexcept;

  // Writes up to max_packets waiting packets to writer, with utc_shift
  // subtracted from each time stamp (TAI-UTC for a NIC clock that keeps
  // TAI). Files the writer closes are added to closed. The packets read; the
  // writer's error, or kUnavailable when the capture fails.
  [[nodiscard]] Result<std::size_t> dispatch(std::size_t max_packets, Duration utc_shift, RotatingWriter& writer,
                                             std::vector<ClosedFile>& closed);

  [[nodiscard]] CaptureStats stats() noexcept;

 private:
  struct HandleClose {
    void operator()(pcap* handle) const noexcept;
  };

  explicit LiveCapture(pcap* handle) noexcept;

  std::unique_ptr<pcap, HandleClose> handle_;
};

}  // namespace ics::capture
