// Fuzzes the PTP management decoder (ICS-019). For any bytes, decode_response
// either fails with kMalformed or kUnavailable, or gives a response whose
// port state, if it holds a port data set, is one IEEE 1588 defines. The
// corpus holds responses captured from ptp4l 4.0.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <variant>

#include "ics/common/error.hpp"
#include "ics/timing/ptp_management.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const std::span<const std::uint8_t> input{data, size};
  const ics::Result<ics::timing::Response> response = ics::timing::decode_response(std::as_bytes(input));
  if (!response) {
    if (response.error() != ics::Error::kMalformed && response.error() != ics::Error::kUnavailable) {
      std::abort();
    }
    return 0;
  }
  const auto* port = std::get_if<ics::timing::PortDataSet>(&response->data);
  if (port != nullptr && (port->port_state < ics::timing::PortState::kInitializing ||
                          port->port_state > ics::timing::PortState::kSlave)) {
    std::abort();
  }
  return 0;
}
