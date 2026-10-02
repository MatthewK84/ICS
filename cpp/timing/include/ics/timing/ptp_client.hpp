#pragma once

#include <cstdint>
#include <filesystem>

#include <sys/un.h>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/timing/quality.hpp"
#include "ics/timing/unix_socket.hpp"

namespace ics::timing {

// A client of ptp4l's management socket (ICS-019). Use the read-only socket
// (uds_ro_address, /var/run/ptp4l-ro by default): it answers GET requests and
// refuses everything else, so ics-timingd cannot change the clock.
//
// The client assumes the station has one PTP port, as a station's single
// timing NIC does: it takes the first port data set ptp4l sends.
class PtpClient {
 public:
  // Opens a client of the socket at server, in PTP domain domain. Answers
  // arrive on client, a socket path this process owns: a file left there is
  // removed first, and the socket again when the client is destroyed.
  [[nodiscard]] static Result<PtpClient> open(const std::filesystem::path& server, const std::filesystem::path& client,
                                              std::uint8_t domain) noexcept;

  ~PtpClient();
  PtpClient(PtpClient&& other) noexcept = default;
  PtpClient& operator=(PtpClient&& other) noexcept = default;
  PtpClient(const PtpClient&) = delete;
  PtpClient& operator=(const PtpClient&) = delete;

  // Asks for the four data sets and waits up to timeout for all four
  // answers. kUnavailable when they do not all come: ptp4l is not running,
  // is too slow, or refused. Allocates nothing.
  [[nodiscard]] Result<Snapshot> poll(Duration timeout) noexcept;

 private:
  PtpClient(Fd socket, sockaddr_un server, std::filesystem::path client, std::uint8_t domain) noexcept;

  [[nodiscard]] Status send_requests(std::uint16_t first) const noexcept;

  Fd socket_;
  sockaddr_un server_{};
  std::filesystem::path client_;
  std::uint16_t sequence_ = 0;
  std::uint8_t domain_ = 0;
};

}  // namespace ics::timing
