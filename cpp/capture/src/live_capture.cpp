#include "ics/capture/live_capture.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <pcap/pcap.h>

#include "ics/common/check.hpp"

namespace ics::capture {
namespace {

// How long the kernel holds a part-filled ring block before handing it over.
constexpr int kBlockTimeoutMs = 100;
constexpr std::int64_t kNanosPerSecond = 1'000'000'000;

// What dispatch hands the callback: where packets go, and what went wrong.
struct Delivery {
  RotatingWriter* writer = nullptr;
  Duration utc_shift{};
  std::vector<ClosedFile>* closed = nullptr;
  pcap* handle = nullptr;
  // The error dispatch reports when the loop ends early: the writer's, if
  // it failed, else the capture's.
  Error error = Error::kUnavailable;
};

void deliver(u_char* const user, const pcap_pkthdr* const header, const u_char* const bytes) {
  Delivery& delivery = *reinterpret_cast<Delivery*>(user);
  // With nanosecond precision, tv_usec holds nanoseconds.
  const std::int64_t stamp_ns = static_cast<std::int64_t>(header->ts.tv_sec) * kNanosPerSecond + header->ts.tv_usec;
  const Packet packet{utc_from_ns(stamp_ns) - delivery.utc_shift, header->len,
                      std::span(reinterpret_cast<const std::byte*>(bytes), header->caplen)};
  Result<std::optional<ClosedFile>> written = delivery.writer->write(packet);
  if (!written) {
    delivery.error = written.error();
    pcap_breakloop(delivery.handle);
    return;
  }
  if (written->has_value()) {
    delivery.closed->push_back(std::move(**written));
  }
}

// Settings that fail only on an activated handle, which this is not.
void configure(pcap* const handle, const CaptureSettings& settings) noexcept {
  static_cast<void>(ics::check(pcap_set_snaplen(handle, static_cast<int>(settings.snaplen)) == 0));
  static_cast<void>(ics::check(pcap_set_promisc(handle, 1) == 0));
  static_cast<void>(ics::check(pcap_set_timeout(handle, kBlockTimeoutMs) == 0));
  static_cast<void>(ics::check(pcap_set_buffer_size(handle, static_cast<int>(settings.buffer_bytes)) == 0));
  static_cast<void>(ics::check(pcap_set_tstamp_precision(handle, PCAP_TSTAMP_PRECISION_NANO) == 0));
}

}  // namespace

void LiveCapture::HandleClose::operator()(pcap* const handle) const noexcept { pcap_close(handle); }

LiveCapture::LiveCapture(pcap* const handle) noexcept : handle_(handle) {}

Result<LiveCapture> LiveCapture::open(const CaptureSettings& settings) {
  std::array<char, PCAP_ERRBUF_SIZE> error{};
  // pcap_create fails when it cannot open a socket to ask about time stamps.
  LiveCapture capture(pcap_create(settings.interface.c_str(), error.data()));
  if (!capture.handle_) {
    return fail(Error::kUnavailable);
  }
  pcap* const handle = capture.handle_.get();
  configure(handle, settings);
  // Anything but 0 means the interface lacks hardware time stamps.
  const int stamps = settings.timestamps == TimestampSource::kAdapter
                         ? pcap_set_tstamp_type(handle, PCAP_TSTAMP_ADAPTER_UNSYNCED)
                         : 0;
  if (stamps != 0) {
    return fail(Error::kInvalidArgument);
  }
  // A warning counts as failure too, such as promiscuous mode refused.
  if (pcap_activate(handle) != 0) {
    return fail(Error::kUnavailable);
  }
  static_cast<void>(ics::check(pcap_setnonblock(handle, 1, error.data()) == 0));
  return capture;
}

int LiveCapture::fd() const noexcept { return pcap_get_selectable_fd(handle_.get()); }

std::uint32_t LiveCapture::link_type() const noexcept {
  return static_cast<std::uint32_t>(pcap_datalink(handle_.get()));
}

Result<std::size_t> LiveCapture::dispatch(const std::size_t max_packets, const Duration utc_shift,
                                          RotatingWriter& writer, std::vector<ClosedFile>& closed) {
  Delivery delivery{&writer, utc_shift, &closed, handle_.get()};
  const int count = pcap_dispatch(handle_.get(), static_cast<int>(max_packets), &deliver,
                                  reinterpret_cast<u_char*>(&delivery));
  // -2 after the callback broke the loop on a writer error, -1 on a capture
  // error.
  if (count < 0) {
    return fail(delivery.error);
  }
  return static_cast<std::size_t>(count);
}

CaptureStats LiveCapture::stats() noexcept {
  pcap_stat counters{};
  static_cast<void>(ics::check(pcap_stats(handle_.get(), &counters) == 0));
  return CaptureStats{counters.ps_recv, counters.ps_drop, counters.ps_ifdrop};
}

}  // namespace ics::capture
