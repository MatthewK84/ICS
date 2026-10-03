#include "ics/capd/utc_offset.hpp"

#include <chrono>

#include "ics/timing/ptp_client.hpp"
#include "ics/timing/quality.hpp"

namespace ics::capd {

Result<Duration> read_tai_minus_utc(const PtpConfig& config) noexcept {
  Result<timing::PtpClient> client =
      timing::PtpClient::open(config.ptp4l_socket, config.client_socket, config.ptp_domain);
  if (!client) {
    return fail(client.error());
  }
  const Result<timing::Snapshot> snapshot = client->poll(kPtpTimeout);
  if (!snapshot) {
    return fail(snapshot.error());
  }
  if (!snapshot->time.utc_offset_valid()) {
    return fail(Error::kUnavailable);
  }
  return std::chrono::seconds(snapshot->time.current_utc_offset);
}

}  // namespace ics::capd
