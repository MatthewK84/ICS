#include "ics/timing/ptp_management.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "support.hpp"

namespace {

using ics::Duration;
using ics::timing::CurrentDataSet;
using ics::timing::DataSetId;
using ics::timing::ParentDataSet;
using ics::timing::PortDataSet;
using ics::timing::PortState;
using ics::timing::Response;
using ics::timing::TimePropertiesDataSet;
using ics::timing::testing::bytes;

// The GET for the current data set, sequence 100, domain 0: the request that
// drew kCurrent from ptp4l.
constexpr std::string_view kGetCurrent =
    "0d02003600000000000000000000000000000000000000000000000000000064047fffffffffffffffffffff00000000000100022001";
constexpr std::uint16_t kFirstSequence = 100;

Response decoded(const std::string_view hex) {
  const std::vector<std::byte> message = bytes(hex);
  const ics::Result<Response> response = ics::timing::decode_response(message);
  EXPECT_TRUE(response.has_value()) << hex;
  return response.value_or(Response{});
}

void expect_error(const std::vector<std::byte>& message, const ics::Error error) {
  const ics::Result<Response> response = ics::timing::decode_response(message);
  ASSERT_FALSE(response.has_value());
  EXPECT_EQ(response.error(), error);
}

// kCurrent with byte at replaced by value.
std::vector<std::byte> changed(const std::string_view hex, const std::size_t at, const unsigned value) {
  std::vector<std::byte> message = bytes(hex);
  message.at(at) = static_cast<std::byte>(value);
  return message;
}

TEST(PtpManagement, EncodesTheGetPtp4lAnswered) {
  const ics::timing::GetRequest request = ics::timing::encode_get(DataSetId::kCurrent, kFirstSequence, 0);
  EXPECT_EQ(std::vector<std::byte>(request.begin(), request.end()), bytes(kGetCurrent));
}

TEST(PtpManagement, EncodesTheDomainSequenceAndDataSet) {
  const ics::timing::GetRequest request = ics::timing::encode_get(DataSetId::kPort, 0xABCD, 24);
  EXPECT_EQ(request[4], std::byte{24});
  EXPECT_EQ(request[30], std::byte{0xAB});
  EXPECT_EQ(request[31], std::byte{0xCD});
  EXPECT_EQ(request[52], std::byte{0x20});
  EXPECT_EQ(request[53], std::byte{0x04});
}

TEST(PtpManagement, DecodesTheCurrentDataSet) {
  const Response response = decoded(ics::timing::testing::kCurrent);
  EXPECT_EQ(response.sequence, kFirstSequence);
  const auto* current = std::get_if<CurrentDataSet>(&response.data);
  ASSERT_NE(current, nullptr);
  EXPECT_EQ(current->steps_removed, 1);
  // TimeIntervals are scaled by 2^16 and truncate toward zero.
  EXPECT_EQ(current->offset_from_master, Duration(-191));
  EXPECT_EQ(current->mean_path_delay, Duration(1455));
}

TEST(PtpManagement, DecodesTheParentDataSet) {
  const Response response = decoded(ics::timing::testing::kParent);
  const auto* parent = std::get_if<ParentDataSet>(&response.data);
  ASSERT_NE(parent, nullptr);
  EXPECT_EQ(parent->grandmaster_identity, 0xce07d8fffe0f38b1U);
  EXPECT_EQ(parent->grandmaster_priority1, 128);
  EXPECT_EQ(parent->grandmaster_priority2, 128);
  EXPECT_EQ(parent->grandmaster_quality.clock_class, 6);
  EXPECT_EQ(parent->grandmaster_quality.clock_accuracy, 0x21);
  EXPECT_EQ(parent->grandmaster_quality.offset_scaled_log_variance, 0x4e5d);
  const Response holdover = decoded(ics::timing::testing::kParentHoldover);
  EXPECT_EQ(std::get<ParentDataSet>(holdover.data).grandmaster_quality.clock_class, 7);
}

TEST(PtpManagement, DecodesTheTimePropertiesDataSet) {
  const Response locked = decoded(ics::timing::testing::kTimeProperties);
  const auto* time = std::get_if<TimePropertiesDataSet>(&locked.data);
  ASSERT_NE(time, nullptr);
  EXPECT_EQ(time->current_utc_offset, 0);
  EXPECT_TRUE(time->utc_offset_valid());
  EXPECT_TRUE(time->time_traceable());
  EXPECT_TRUE(time->frequency_traceable());
  EXPECT_EQ(time->time_source, 0x20);  // GNSS
  const auto lost = std::get<TimePropertiesDataSet>(decoded(ics::timing::testing::kTimePropertiesHoldover).data);
  EXPECT_FALSE(lost.time_traceable());
  EXPECT_EQ(lost.time_source, 0xA0);  // internal oscillator
  EXPECT_FALSE(TimePropertiesDataSet{}.utc_offset_valid());
  EXPECT_FALSE(TimePropertiesDataSet{}.frequency_traceable());
}

TEST(PtpManagement, DecodesThePortDataSet) {
  const Response response = decoded(ics::timing::testing::kPort);
  const auto* port = std::get_if<PortDataSet>(&response.data);
  ASSERT_NE(port, nullptr);
  EXPECT_EQ(port->port_number, 1);
  EXPECT_EQ(port->port_state, PortState::kSlave);
  EXPECT_EQ(port->log_announce_interval, 1);
}

TEST(PtpManagement, ReportsAManagementErrorAsUnavailable) {
  expect_error(bytes(ics::timing::testing::kError), ics::Error::kUnavailable);
}

TEST(PtpManagement, IgnoresBytesPastTheStatedLength) {
  std::vector<std::byte> message = bytes(ics::timing::testing::kCurrent);
  message.push_back(std::byte{0xEE});
  EXPECT_TRUE(ics::timing::decode_response(message).has_value());
}

TEST(PtpManagement, RejectsAMessageThatIsNotAManagementResponse) {
  const std::string_view current = ics::timing::testing::kCurrent;
  std::vector<std::byte> short_message = bytes(current);
  short_message.resize(53);
  expect_error(short_message, ics::Error::kMalformed);
  expect_error(changed(current, 0, 0x0B), ics::Error::kMalformed);   // an announce message
  expect_error(changed(current, 1, 0x11), ics::Error::kMalformed);   // PTP version 1
  expect_error(changed(current, 3, 0x30), ics::Error::kMalformed);   // a stated length under 54
  expect_error(changed(current, 3, 0x49), ics::Error::kMalformed);   // a stated length past the end
  expect_error(changed(current, 46, 0x00), ics::Error::kMalformed);  // a GET, not a response
}

TEST(PtpManagement, RejectsATlvThatDoesNotFit) {
  const std::string_view current = ics::timing::testing::kCurrent;
  expect_error(changed(current, 51, 0x15), ics::Error::kMalformed);  // longer than the message
  expect_error(changed(current, 51, 0x01), ics::Error::kMalformed);  // no room for the managementId
  expect_error(changed(current, 49, 0x03), ics::Error::kMalformed);  // not a management TLV
}

TEST(PtpManagement, RejectsADataSetItDidNotAskFor) {
  expect_error(bytes(ics::timing::testing::kDefault), ics::Error::kMalformed);
}

TEST(PtpManagement, RejectsADataSetOfTheWrongSize) {
  // Each answer with its TLV a byte short; the stated length still fits.
  for (const std::string_view hex : {ics::timing::testing::kCurrent, ics::timing::testing::kParent,
                                     ics::timing::testing::kTimeProperties, ics::timing::testing::kPort}) {
    std::vector<std::byte> message = bytes(hex);
    message.at(51) = static_cast<std::byte>(std::to_integer<unsigned>(message.at(51)) - 1);
    expect_error(message, ics::Error::kMalformed);
  }
}

TEST(PtpManagement, RejectsAPortStateIeee1588DoesNotDefine) {
  for (const unsigned state : {0U, 10U, 0xFFU}) {
    expect_error(changed(ics::timing::testing::kPort, 64, state), ics::Error::kMalformed);
  }
}

}  // namespace
