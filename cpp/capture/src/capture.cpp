#include "ics/capture/capture.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <string>
#include <utility>

#include <pcap/pcap.h>

#include "status.hpp"

namespace ics::capture {

using detail::status_of;

namespace {

// How long the kernel holds a part-filled block of packets before handing it
// over, so a quiet interface's packets are not held back longer.
constexpr int kBlockTimeoutMs = 100;

// What dispatch() passes through libpcap to deliver().
struct Delivery {
  PacketSink* sink;
  pcap_t* handle;
  Duration stamp_minus_utc;
  Status status;
};

// libpcap's callback for each packet.
void deliver(u_char* const user, const pcap_pkthdr* const header, const u_char* const bytes) {
  // user is the Delivery that dispatch() passed, and the bytes are octets.
  Delivery& delivery = *reinterpret_cast<Delivery*>(user);
  const UtcTime stamp(std::chrono::seconds(header->ts.tv_sec) + std::chrono::nanoseconds(header->ts.tv_usec));
  const Packet packet{stamp - delivery.stamp_minus_utc, header->len,
                      std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes), header->caplen)};
  delivery.status = delivery.sink->accept(packet);
  if (!delivery.status) {
    ::pcap_breakloop(delivery.handle);
  }
}

// Sets up handle before it is activated: zero, or a nonzero libpcap status.
// The setters can only fail on an activated handle, except that asking for a
// time stamp type the interface lacks returns a warning, which counts.
[[nodiscard]] int configure(pcap_t* const handle, const CaptureOptions& options) noexcept {
  const int type = options.timestamps == TimestampSource::kAdapter
                       ? ::pcap_set_tstamp_type(handle, PCAP_TSTAMP_ADAPTER_UNSYNCED)
                       : 0;
  // OR keeps any nonzero status without a branch for each setter.
  return type | ::pcap_set_snaplen(handle, static_cast<int>(options.snaplen)) | ::pcap_set_promisc(handle, 1) |
         ::pcap_set_timeout(handle, kBlockTimeoutMs) |
         ::pcap_set_buffer_size(handle, static_cast<int>(options.buffer_bytes)) |
         ::pcap_set_tstamp_precision(handle, PCAP_TSTAMP_PRECISION_NANO);
}

}  // namespace

void Capture::Close::operator()(pcap* const handle) const noexcept { ::pcap_close(handle); }

Capture::Capture(Handle handle, const Duration stamp_minus_utc) noexcept
    : handle_(std::move(handle)), stamp_minus_utc_(stamp_minus_utc) {}

Result<Capture> Capture::open_live(const CaptureOptions& options, std::string& reason) {
  std::array<char, PCAP_ERRBUF_SIZE> errors{};
  Handle handle(::pcap_create(options.interface.c_str(), errors.data()));
  const char* detail = errors.data();
  int status = 0;
  return status_of(handle != nullptr, Error::kUnavailable)
      .and_then([&] {
        detail = ::pcap_geterr(handle.get());
        status = configure(handle.get(), options);
        return status_of(status == 0, Error::kUnavailable);
      })
      .and_then([&] {
        status = ::pcap_activate(handle.get());
        // A warning, such as promiscuous mode being refused, fails too.
        return status_of(status == 0, Error::kUnavailable);
      })
      .and_then([&] { return status_of(::pcap_setnonblock(handle.get(), 1, errors.data()) == 0, Error::kUnavailable); })
      .map([&] { return Capture(std::move(handle), options.stamp_minus_utc); })
      .map_error([&](const Error error) {
        reason = options.interface + ": " + ::pcap_statustostr(status) + " " + detail;
        return error;
      });
}

Result<Capture> Capture::open_file(const std::filesystem::path& path, std::string& reason) {
  std::array<char, PCAP_ERRBUF_SIZE> errors{};
  Handle handle(::pcap_open_offline_with_tstamp_precision(path.c_str(), PCAP_TSTAMP_PRECISION_NANO, errors.data()));
  return status_of(handle != nullptr, Error::kUnreadable)
      .map([&] { return Capture(std::move(handle), Duration::zero()); })
      .map_error([&](const Error error) {
        reason = errors.data();
        return error;
      });
}

int Capture::fd() const noexcept { return ::pcap_get_selectable_fd(handle_.get()); }

FileFormat Capture::format() const noexcept {
  return FileFormat{static_cast<std::uint32_t>(::pcap_snapshot(handle_.get())),
                    static_cast<std::uint32_t>(::pcap_datalink(handle_.get()))};
}

Result<std::size_t> Capture::dispatch(PacketSink& sink, const int max) noexcept {
  Delivery delivery{&sink, handle_.get(), stamp_minus_utc_, {}};
  // libpcap hands user back to deliver() as it was given.
  const int count = ::pcap_dispatch(handle_.get(), max, &deliver, reinterpret_cast<u_char*>(&delivery));
  return delivery.status.and_then([count] { return status_of(count >= 0, Error::kUnreadable); }).map([count] {
    return static_cast<std::size_t>(count);
  });
}

Result<Counters> Capture::counters() noexcept {
  pcap_stat stats{};
  return status_of(::pcap_stats(handle_.get(), &stats) == 0, Error::kUnavailable).map([&stats] {
    return Counters{stats.ps_recv, stats.ps_drop, stats.ps_ifdrop};
  });
}

}  // namespace ics::capture
