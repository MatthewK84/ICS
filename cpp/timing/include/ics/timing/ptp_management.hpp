#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"

namespace ics::timing {

// PTP management messages (IEEE 1588-2008 clause 15) as ptp4l's management
// socket speaks them (ICS-019): GET requests for the four data sets
// ics-timingd reads, and the responses to them. Multi-byte fields are big-endian.

// The data sets ics-timingd asks for, by managementId.
enum class DataSetId : std::uint16_t {
  kCurrent = 0x2001,
  kParent = 0x2002,
  kTimeProperties = 0x2003,
  kPort = 0x2004,
};

// A PTP port's state (IEEE 1588-2008 Table 8).
enum class PortState : std::uint8_t {
  kInitializing = 1,
  kFaulty = 2,
  kDisabled = 3,
  kListening = 4,
  kPreMaster = 5,
  kMaster = 6,
  kPassive = 7,
  kUncalibrated = 8,
  kSlave = 9,
};

// A clock's quality as the grandmaster announces it (IEEE 1588-2008 5.3.7).
struct ClockQuality {
  std::uint8_t clock_class = 0;
  std::uint8_t clock_accuracy = 0;
  std::uint16_t offset_scaled_log_variance = 0;
};

// currentDS: this clock against its grandmaster.
struct CurrentDataSet {
  std::uint16_t steps_removed = 0;
  Duration offset_from_master{};
  Duration mean_path_delay{};
};

// parentDS: the grandmaster this clock follows.
struct ParentDataSet {
  std::uint64_t grandmaster_identity = 0;
  std::uint8_t grandmaster_priority1 = 0;
  ClockQuality grandmaster_quality;
  std::uint8_t grandmaster_priority2 = 0;
};

// timePropertiesDS: the grandmaster's time scale and traceability.
struct TimePropertiesDataSet {
  std::int16_t current_utc_offset = 0;
  std::uint8_t flags = 0;
  std::uint8_t time_source = 0;

  [[nodiscard]] bool utc_offset_valid() const noexcept;
  [[nodiscard]] bool time_traceable() const noexcept;
  [[nodiscard]] bool frequency_traceable() const noexcept;
};

// portDS: the state of one port of this clock.
struct PortDataSet {
  std::uint16_t port_number = 0;
  PortState port_state = PortState::kInitializing;
  std::int8_t log_announce_interval = 0;
};

using DataSet = std::variant<CurrentDataSet, ParentDataSet, TimePropertiesDataSet, PortDataSet>;

// A response: the sequence number of the request it answers, and its data set.
struct Response {
  std::uint16_t sequence = 0;
  DataSet data;
};

// A GET request is a 34-byte header, a 14-byte management body and a 6-byte
// TLV with no data.
inline constexpr std::size_t kGetRequestSize = 54;
using GetRequest = std::array<std::byte, kGetRequestSize>;

// A GET for one data set of the clock on the other end of the socket, in
// PTP domain domain. Its boundary hops are 0, so ptp4l answers itself and
// forwards nothing to the network.
[[nodiscard]] GetRequest encode_get(DataSetId id, std::uint16_t sequence, std::uint8_t domain) noexcept;

// The data set in a response. kUnavailable when the clock answered with a
// management error; kMalformed when the bytes are not a response to a GET for
// one of the four data sets above.
[[nodiscard]] Result<Response> decode_response(std::span<const std::byte> message) noexcept;

}  // namespace ics::timing
