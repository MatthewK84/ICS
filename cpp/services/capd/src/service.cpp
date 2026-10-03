#include "ics/capd/service.hpp"

#include <algorithm>
#include <optional>
#include <set>
#include <utility>

#include "ics/capd/report.hpp"
#include "ics/capd/utc_offset.hpp"

namespace ics::capd {
namespace {

// Writes each packet dispatched from an interface, keeping the files its
// byte limit closes.
class Writing final : public capture::PacketSink {
 public:
  Writing(capture::RotatingWriter& writer, std::vector<capture::ClosedFile>& closed, const UtcTime now) noexcept
      : writer_(writer), closed_(closed), now_(now) {}

  Status accept(const capture::Packet& packet) override {
    return writer_.write(packet, now_).map([this](std::optional<capture::ClosedFile> file) {
      if (file) {
        closed_.push_back(std::move(*file));
      }
    });
  }

 private:
  capture::RotatingWriter& writer_;
  std::vector<capture::ClosedFile>& closed_;
  UtcTime now_;
};

// Fails unless every interface name is valid and none repeats.
[[nodiscard]] Status check_interfaces(const std::vector<std::string>& interfaces, std::string& reason) {
  const std::set<std::string> unique(interfaces.begin(), interfaces.end());
  const auto bad = std::ranges::find_if_not(interfaces, valid_interface_name);
  if (bad != interfaces.end() || unique.size() != interfaces.size()) {
    reason = "interface names must be valid and different";
    return fail(Error::kInvalidArgument);
  }
  return {};
}

// TAI - UTC for adapter time stamps; zero for host ones.
[[nodiscard]] Result<Duration> stamp_minus_utc(const Config& config, std::string& reason) {
  if (!config.ptp) {
    return Duration::zero();
  }
  Result<Duration> offset = read_tai_minus_utc(*config.ptp);
  if (!offset) {
    reason = "no valid TAI - UTC offset from ptp4l at " + config.ptp->ptp4l_socket.native();
  }
  return offset;
}

// Opens the capture and first file of one interface.
[[nodiscard]] Result<Port> open_port(const Config& config, const std::string& interface, const Duration offset,
                                     const UtcTime now, std::string& reason) {
  const capture::CaptureOptions options{interface, config.snaplen, config.buffer_bytes, config.timestamps, offset};
  Result<capture::Capture> capture = capture::Capture::open_live(options, reason);
  if (!capture) {
    return fail(capture.error());
  }
  Result<capture::RotatingWriter> writer =
      capture::RotatingWriter::open(config.folder, interface, capture->format(), config.rotation, now);
  if (!writer) {
    reason = "cannot create a capture file in " + config.folder.native();
    return fail(writer.error());
  }
  return Port{interface, std::move(*capture), std::move(*writer), {}};
}

}  // namespace

Service::Service(std::vector<Port> ports) noexcept : ports_(std::move(ports)) {}

Result<Service> Service::open(const Config& config, const UtcTime now, std::string& reason) {
  const Status named = check_interfaces(config.interfaces, reason);
  const Result<Duration> offset = named.and_then([&] { return stamp_minus_utc(config, reason); });
  if (!offset) {
    return fail(offset.error());
  }
  std::vector<Port> ports;
  for (const std::string& interface : config.interfaces) {
    Result<Port> port = open_port(config, interface, *offset, now, reason);
    if (!port) {
      return fail(port.error());
    }
    ports.push_back(std::move(*port));
  }
  return Service(std::move(ports));
}

std::vector<pollfd> Service::descriptors() const {
  std::vector<pollfd> out(ports_.size());
  std::ranges::transform(ports_, out.begin(), [](const Port& port) { return pollfd{port.capture.fd(), POLLIN, 0}; });
  return out;
}

Status Service::step(const UtcTime now, const logging::Logger& logger) {
  for (Port& port : ports_) {
    const Status stepped = drain(port, now, logger).and_then([&] { return rotate_due(port, now, logger); });
    if (!stepped) {
      logger.error("capture_failed", {{"interface", port.interface}, {"error", to_string(stepped.error())}});
      return stepped;
    }
  }
  return {};
}

Status Service::drain(Port& port, const UtcTime now, const logging::Logger& logger) {
  Writing sink(port.writer, closed_, now);
  Result<std::size_t> count = std::size_t{kBatch};
  for (int batch = 0; batch < kMaxBatches && count && *count == kBatch; ++batch) {
    count = port.capture.dispatch(sink, kBatch);
  }
  for (const capture::ClosedFile& file : closed_) {
    report(port, file, logger);
  }
  closed_.clear();
  return count.map([](std::size_t) {});
}

Status Service::rotate_due(Port& port, const UtcTime now, const logging::Logger& logger) {
  if (!port.writer.due(now)) {
    return {};
  }
  return port.writer.rotate(now).map([&](const capture::ClosedFile& file) { report(port, file, logger); });
}

Status Service::close(const logging::Logger& logger) {
  Status first;
  for (Port& port : ports_) {
    const Status closed = port.writer.close().map([&](const capture::ClosedFile& file) { report(port, file, logger); });
    if (!closed) {
      logger.error("close_failed", {{"interface", port.interface}, {"error", to_string(closed.error())}});
    }
    first = first ? closed : first;
  }
  return first;
}

void Service::report(Port& port, const capture::ClosedFile& file, const logging::Logger& logger) {
  const Result<capture::Counters> counters = port.capture.counters();
  const Result<capture::Counters> counted =
      counters.map([&port](const capture::Counters& now) { return since(now, std::exchange(port.reported, now)); });
  log_closed(logger, port.interface, file, counted);
}

}  // namespace ics::capd
