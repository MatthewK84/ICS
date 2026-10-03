#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <poll.h>

#include "ics/capd/config.hpp"
#include "ics/capture/capture.hpp"
#include "ics/capture/rotating_writer.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/logging/logger.hpp"

namespace ics::capd {

// Packets read from one interface per dispatch, and dispatches per step, so
// one busy interface cannot hold up the others for long.
inline constexpr int kBatch = 256;
inline constexpr int kMaxBatches = 16;

// One captured interface and the files it is written to.
struct Port {
  std::string interface;
  capture::Capture capture;
  capture::RotatingWriter writer;
  // The interface's counters when its last file closed.
  capture::Counters reported{};
};

// ics-capd's work (ICS-020): each step writes every packet waiting on each
// interface to its current file, and rotates the files that are due.
class Service {
 public:
  // Opens every interface in config, and a first file for each at now. Fails
  // with an explanation in reason: kInvalidArgument for a bad or repeated
  // interface name, as read_tai_minus_utc fails for adapter time stamps, as
  // Capture::open_live and RotatingWriter::open fail otherwise.
  [[nodiscard]] static Result<Service> open(const Config& config, UtcTime now, std::string& reason);

  // A service over ports already open, such as captures replayed from files.
  explicit Service(std::vector<Port> ports) noexcept;

  // What to poll for packets: one descriptor per interface.
  [[nodiscard]] std::vector<pollfd> descriptors() const;

  // Writes the packets waiting on every interface, then closes the files
  // that are due at now and opens the next. Logs each file closed. On the
  // first error, logs "capture_failed" and returns it.
  [[nodiscard]] Status step(UtcTime now, const logging::Logger& logger);

  // Closes every interface's current file, logging each; the first error.
  [[nodiscard]] Status close(const logging::Logger& logger);

 private:
  [[nodiscard]] Status drain(Port& port, UtcTime now, const logging::Logger& logger);
  [[nodiscard]] Status rotate_due(Port& port, UtcTime now, const logging::Logger& logger);
  static void report(Port& port, const capture::ClosedFile& file, const logging::Logger& logger);

  std::vector<Port> ports_;
  // Files closed for their byte limit during a dispatch, waiting to be logged.
  std::vector<capture::ClosedFile> closed_;
};

}  // namespace ics::capd
