#include "ics/capd/service.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ics/common/check.hpp"
#include "ics/timing/ptp_client.hpp"
#include "ics/timing/quality.hpp"

namespace ics::capd {
namespace {

// At least one record at the largest snapshot length.
constexpr std::size_t kWriteBufferBytes = std::size_t{4} << 20U;
constexpr std::size_t kPacketsPerRead = 4096;
constexpr Duration kPtpTimeout = std::chrono::seconds(1);
constexpr std::size_t kClosedPerRead = 16;

// TAI-UTC for adapter time stamps: ptp4l keeps a slave NIC's clock on TAI.
// Zero for host time stamps, which are already UTC.
[[nodiscard]] Result<Duration> read_utc_shift(const Config& config) {
  if (config.timestamps == capture::TimestampSource::kHost) {
    return Duration::zero();
  }
  Result<timing::PtpClient> client =
      timing::PtpClient::open(config.ptp4l_socket, config.client_socket, config.ptp_domain);
  if (!client) {
    return fail(client.error());
  }
  const Result<timing::Snapshot> snapshot = client->poll(kPtpTimeout);
  if (!snapshot || !snapshot->time.utc_offset_valid()) {
    return fail(Error::kUnavailable);
  }
  return std::chrono::seconds(snapshot->time.current_utc_offset);
}

}  // namespace

Service::Service(std::vector<Port> ports, const Duration utc_shift) : ports_(std::move(ports)), utc_shift_(utc_shift) {
  closed_.reserve(kClosedPerRead);
}

Result<Service::Port> Service::open_port(const std::string& interface, const Config& config) {
  Result<capture::LiveCapture> capture =
      capture::LiveCapture::open({interface, config.snaplen, config.ring_bytes, config.timestamps});
  if (!capture) {
    return fail(capture.error());
  }
  Result<capture::RotatingWriter> writer = capture::RotatingWriter::make(
      {config.output_dir, interface, config.snaplen, capture->link_type(), config.rotation, kWriteBufferBytes});
  if (!writer) {
    return fail(writer.error());
  }
  const capture::CaptureStats counted = capture->stats();
  return Port{interface, std::move(*capture), std::move(*writer), counted};
}

Result<Service> Service::open(const Config& config) {
  const Result<Duration> shift = read_utc_shift(config);
  if (!shift) {
    return fail(shift.error());
  }
  std::vector<Port> opened;
  for (const std::string& interface : config.interfaces) {
    Result<Port> port = open_port(interface, config);
    if (!port) {
      return fail(port.error());
    }
    opened.push_back(std::move(*port));
  }
  return Service(std::move(opened), *shift);
}

int Service::fd(const std::size_t port) const noexcept { return ports_[port].capture.fd(); }

Status Service::read(const std::size_t port, const logging::Logger& logger) {
  if (!ics::check(port < ports_.size())) {
    return fail(Error::kOutOfRange);
  }
  Port& read_port = ports_[port];
  closed_.clear();
  const Result<std::size_t> read = read_port.capture.dispatch(kPacketsPerRead, utc_shift_, read_port.writer, closed_);
  // Files closed before a failure are complete, so they are logged too.
  for (const capture::ClosedFile& file : closed_) {
    log_closed(read_port, file, logger);
  }
  if (!read) {
    return fail(read.error());
  }
  return {};
}

Status Service::rotate_due(const UtcTime now, const logging::Logger& logger) {
  for (Port& port : ports_) {
    const Status finished = finish(port, port.writer.close_if_due(now), logger);
    if (!finished) {
      return finished;
    }
  }
  return {};
}

Status Service::close(const logging::Logger& logger) {
  for (Port& port : ports_) {
    const Status finished = finish(port, port.writer.close(), logger);
    if (!finished) {
      return finished;
    }
  }
  return {};
}

Status Service::finish(Port& port, Result<std::optional<capture::ClosedFile>> closed,
                       const logging::Logger& logger) {
  if (!closed) {
    return fail(closed.error());
  }
  if (closed->has_value()) {
    log_closed(port, **closed, logger);
  }
  return {};
}

void Service::log_closed(Port& port, const capture::ClosedFile& file, const logging::Logger& logger) {
  const capture::CaptureStats now = port.capture.stats();
  const std::uint32_t dropped = now.dropped - port.counted.dropped;
  const std::uint32_t interface_dropped = now.interface_dropped - port.counted.interface_dropped;
  port.counted = now;
  const capture::DigestHex hex = capture::to_hex(file.sha256);
  const bool lost = (dropped | interface_dropped) != 0;
  logger.log(lost ? logging::Level::kError : logging::Level::kInfo, "file_closed",
             {{"interface", port.interface},
              {"path", file.path.native()},
              {"sha256", std::string_view(hex.data(), hex.size())},
              {"packets", static_cast<std::int64_t>(file.packets)},
              {"bytes", static_cast<std::int64_t>(file.bytes)},
              {"dropped", static_cast<std::int64_t>(dropped)},
              {"interface_dropped", static_cast<std::int64_t>(interface_dropped)}});
}

}  // namespace ics::capd
