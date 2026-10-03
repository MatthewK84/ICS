#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "ics/capd/config.hpp"
#include "ics/capture/live_capture.hpp"
#include "ics/capture/rotating_writer.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/logging/logger.hpp"

namespace ics::capd {

// ics-capd's work (ICS-020): each TAP port's packets go to rotating pcap
// files. Each file closed is logged as "file_closed" with its SHA-256, its
// packets and bytes, and the drops libpcap counted while it was open; drops
// make the line an error.
class Service {
 public:
  // With "adapter" time stamps, first reads TAI-UTC from ptp4l: kUnavailable
  // when ptp4l does not answer or does not mark the offset valid. Then opens
  // each port; fails as LiveCapture::open and RotatingWriter::make do.
  [[nodiscard]] static Result<Service> open(const Config& config);

  [[nodiscard]] std::size_t ports() const noexcept { return ports_.size(); }
  // Readable when port has packets waiting.
  [[nodiscard]] int fd(std::size_t port) const noexcept;
  // TAI-UTC subtracted from adapter time stamps; zero for host ones.
  [[nodiscard]] Duration utc_shift() const noexcept { return utc_shift_; }

  // Writes the packets waiting on port.
  [[nodiscard]] Status read(std::size_t port, const logging::Logger& logger);
  // Closes files whose interval has passed at now.
  [[nodiscard]] Status rotate_due(UtcTime now, const logging::Logger& logger);
  // Closes every open file, at stop.
  [[nodiscard]] Status close(const logging::Logger& logger);

 private:
  struct Port {
    std::string interface;
    capture::LiveCapture capture;
    capture::RotatingWriter writer;
    // libpcap's counters when the port's current file was opened.
    capture::CaptureStats counted{};
  };

  Service(std::vector<Port> ports, Duration utc_shift);
  [[nodiscard]] static Result<Port> open_port(const std::string& interface, const Config& config);
  [[nodiscard]] Status finish(Port& port, Result<std::optional<capture::ClosedFile>> closed,
                              const logging::Logger& logger);
  void log_closed(Port& port, const capture::ClosedFile& file, const logging::Logger& logger);

  std::vector<Port> ports_;
  Duration utc_shift_{};
  // Files closed while reading, kept between reads.
  std::vector<capture::ClosedFile> closed_;
};

}  // namespace ics::capd
