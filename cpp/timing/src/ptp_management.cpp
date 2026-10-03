#include "ics/timing/ptp_management.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace ics::timing {
namespace {

using Bytes = std::span<const std::byte>;

constexpr unsigned kBitsPerByte = 8;
constexpr unsigned kByteMask = 0xFFU;
constexpr unsigned kLowNibble = 0x0FU;
// The header (IEEE 1588-2008 13.3): message type, version, length, domain,
// sequence, control and interval, at these offsets.
constexpr std::uint8_t kManagementMessage = 0x0D;
constexpr std::uint8_t kPtpVersion = 2;
constexpr std::uint8_t kManagementControl = 0x04;
constexpr std::uint8_t kNoInterval = 0x7F;
constexpr std::size_t kLengthAt = 2;
constexpr std::size_t kDomainAt = 4;
constexpr std::size_t kSequenceAt = 30;
constexpr std::size_t kControlAt = 32;
constexpr std::size_t kIntervalAt = 33;
// The management body (15.4.1): target port, boundary hops, then the action.
constexpr std::size_t kTargetAt = 34;
constexpr std::size_t kTargetSize = 10;
constexpr std::size_t kActionAt = 46;
constexpr unsigned kActionResponse = 2;
// The management TLV (15.5.2): type, length (counting the managementId and
// the data), managementId, data.
constexpr std::size_t kTlvAt = 48;
constexpr std::size_t kTlvLengthAt = 50;
constexpr std::size_t kTlvIdAt = 52;
constexpr std::size_t kTlvDataAt = 54;
constexpr std::size_t kTlvTypeAndLength = 4;
constexpr std::size_t kIdSize = 2;
constexpr std::uint16_t kTlvManagement = 0x0001;
constexpr std::uint16_t kTlvManagementErrorStatus = 0x0002;
// A TimeInterval is nanoseconds scaled by 2^16.
constexpr std::int64_t kTimeIntervalScale = 65536;
// The sizes of the data sets (15.5.3), and the flags in timePropertiesDS.
constexpr std::size_t kCurrentSize = 18;
constexpr std::size_t kParentSize = 32;
constexpr std::size_t kTimePropertiesSize = 4;
constexpr std::size_t kPortSize = 26;
constexpr unsigned kUtcOffsetValid = 0x04U;
constexpr unsigned kTimeTraceable = 0x10U;
constexpr unsigned kFrequencyTraceable = 0x20U;

[[nodiscard]] std::byte low_byte(const unsigned value) noexcept { return static_cast<std::byte>(value & kByteMask); }

void put16(GetRequest& out, const std::size_t at, const unsigned value) noexcept {
  out[at] = low_byte(value >> kBitsPerByte);
  out[at + 1] = low_byte(value);
}

[[nodiscard]] std::uint8_t u8(const Bytes data, const std::size_t at) noexcept {
  return std::to_integer<std::uint8_t>(data[at]);
}

[[nodiscard]] std::uint16_t u16(const Bytes data, const std::size_t at) noexcept {
  return static_cast<std::uint16_t>((static_cast<unsigned>(u8(data, at)) << kBitsPerByte) | u8(data, at + 1));
}

[[nodiscard]] std::uint64_t u64(const Bytes data, const std::size_t at) noexcept {
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < sizeof(value); ++i) {
    value = (value << kBitsPerByte) | u8(data, at + i);
  }
  return value;
}

// A TimeInterval, truncated to whole nanoseconds.
[[nodiscard]] Duration interval(const Bytes data, const std::size_t at) noexcept {
  return Duration(static_cast<std::int64_t>(u64(data, at)) / kTimeIntervalScale);
}

[[nodiscard]] Result<DataSet> current(const Bytes data) noexcept {
  if (data.size() != kCurrentSize) {
    return fail(Error::kMalformed);
  }
  return CurrentDataSet{u16(data, 0), interval(data, 2), interval(data, 10)};
}

[[nodiscard]] Result<DataSet> parent(const Bytes data) noexcept {
  if (data.size() != kParentSize) {
    return fail(Error::kMalformed);
  }
  const ClockQuality quality{u8(data, 19), u8(data, 20), u16(data, 21)};
  return ParentDataSet{u64(data, 24), u8(data, 18), quality, u8(data, 23)};
}

[[nodiscard]] Result<DataSet> time_properties(const Bytes data) noexcept {
  if (data.size() != kTimePropertiesSize) {
    return fail(Error::kMalformed);
  }
  return TimePropertiesDataSet{static_cast<std::int16_t>(u16(data, 0)), u8(data, 2), u8(data, 3)};
}

[[nodiscard]] Result<DataSet> port(const Bytes data) noexcept {
  const bool known_state = data.size() == kPortSize && u8(data, 10) >= static_cast<std::uint8_t>(PortState::kInitializing) &&
                           u8(data, 10) <= static_cast<std::uint8_t>(PortState::kSlave);
  if (!known_state) {
    return fail(Error::kMalformed);
  }
  return PortDataSet{u16(data, 8), static_cast<PortState>(u8(data, 10)), static_cast<std::int8_t>(u8(data, 20))};
}

[[nodiscard]] Result<DataSet> data_set(const std::uint16_t id, const Bytes data) noexcept {
  switch (static_cast<DataSetId>(id)) {
    case DataSetId::kCurrent:
      return current(data);
    case DataSetId::kParent:
      return parent(data);
    case DataSetId::kTimeProperties:
      return time_properties(data);
    case DataSetId::kPort:
      return port(data);
  }
  return fail(Error::kMalformed);
}

// The message up to its stated length, if it is a management response.
[[nodiscard]] Result<Bytes> response_body(const Bytes message) noexcept {
  if (message.size() < kTlvDataAt) {
    return fail(Error::kMalformed);
  }
  const std::size_t length = u16(message, kLengthAt);
  const bool response = (u8(message, 0) & kLowNibble) == kManagementMessage &&
                        (u8(message, 1) & kLowNibble) == kPtpVersion && length >= kTlvDataAt &&
                        length <= message.size() && (u8(message, kActionAt) & kLowNibble) == kActionResponse;
  if (!response) {
    return fail(Error::kMalformed);
  }
  return message.first(length);
}

}  // namespace

bool TimePropertiesDataSet::utc_offset_valid() const noexcept { return (flags & kUtcOffsetValid) != 0; }

bool TimePropertiesDataSet::time_traceable() const noexcept { return (flags & kTimeTraceable) != 0; }

bool TimePropertiesDataSet::frequency_traceable() const noexcept { return (flags & kFrequencyTraceable) != 0; }

GetRequest encode_get(const DataSetId id, const std::uint16_t sequence, const std::uint8_t domain) noexcept {
  GetRequest out{};
  out[0] = std::byte{kManagementMessage};
  out[1] = std::byte{kPtpVersion};
  put16(out, kLengthAt, kGetRequestSize);
  out[kDomainAt] = std::byte{domain};
  put16(out, kSequenceAt, sequence);
  out[kControlAt] = std::byte{kManagementControl};
  out[kIntervalAt] = std::byte{kNoInterval};
  // Every clock and every port the socket reaches; with zero boundary hops
  // and the GET action, the bytes after the target stay zero.
  for (std::size_t i = 0; i < kTargetSize; ++i) {
    out[kTargetAt + i] = std::byte{kByteMask};
  }
  put16(out, kTlvAt, kTlvManagement);
  put16(out, kTlvLengthAt, kIdSize);
  put16(out, kTlvIdAt, static_cast<unsigned>(id));
  return out;
}

Result<Response> decode_response(const std::span<const std::byte> message) noexcept {
  const Result<Bytes> body = response_body(message);
  if (!body) {
    return fail(body.error());
  }
  const std::size_t tlv_length = u16(*body, kTlvLengthAt);
  if (kTlvAt + kTlvTypeAndLength + tlv_length > body->size() || tlv_length < kIdSize) {
    return fail(Error::kMalformed);
  }
  const std::uint16_t type = u16(*body, kTlvAt);
  if (type == kTlvManagementErrorStatus) {
    return fail(Error::kUnavailable);
  }
  if (type != kTlvManagement) {
    return fail(Error::kMalformed);
  }
  const Result<DataSet> data = data_set(u16(*body, kTlvIdAt), body->subspan(kTlvDataAt, tlv_length - kIdSize));
  if (!data) {
    return fail(data.error());
  }
  return Response{u16(*body, kSequenceAt), *data};
}

}  // namespace ics::timing
