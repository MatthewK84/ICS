// The tagger's datagrams over UDP.
#include "ics/encoder/receiver.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <vector>

#include <gtest/gtest.h>

#include "encoder_support.hpp"
#include "ics/common/error.hpp"
#include "ics/tagger/datagram.hpp"
#include "udp_sender.hpp"

namespace {

using ics::encoder::DatagramReceiver;
using ics::encoder::testing::batch;
using ics::encoder::testing::UdpSender;
using std::chrono::milliseconds;

// Long enough for a loopback datagram to arrive on a loaded machine.
constexpr milliseconds kArrival{2000};

[[nodiscard]] DatagramReceiver loopback() { return DatagramReceiver::make("127.0.0.1", 0).value(); }

TEST(DatagramReceiver, RefusesAnAddressThatIsNotIpv4) {
  for (const char* address : {"localhost", "::1", "300.1.1.1", ""}) {
    EXPECT_EQ(DatagramReceiver::make(address, 0).error(), ics::Error::kInvalidArgument) << address;
  }
}

TEST(DatagramReceiver, CannotBindAPortTakenOrAnAddressNotItsOwn) {
  const DatagramReceiver first = loopback();
  EXPECT_NE(first.port(), 0);
  EXPECT_EQ(DatagramReceiver::make("127.0.0.1", first.port()).error(), ics::Error::kUnavailable);
  // TEST-NET-3, which no host here holds.
  EXPECT_EQ(DatagramReceiver::make("203.0.113.7", 0).error(), ics::Error::kUnavailable);
}

TEST(DatagramReceiver, SaysWhenNothingCame) {
  DatagramReceiver receiver = loopback();
  EXPECT_EQ(receiver.receive(milliseconds(0)).error(), ics::Error::kEmpty);
  EXPECT_EQ(receiver.receive(milliseconds(-5)).error(), ics::Error::kEmpty);
  EXPECT_EQ(receiver.receive(milliseconds(10)).error(), ics::Error::kEmpty);
  EXPECT_EQ(receiver.stats().datagrams, 0U);
}

TEST(DatagramReceiver, DecodesABatch) {
  DatagramReceiver receiver = loopback();
  const UdpSender sender(receiver.port());
  ASSERT_TRUE(sender.send(batch({.sequence = 12, .azimuth = 5, .azimuth_step = 3})));
  const auto got = receiver.receive(kArrival);
  ASSERT_TRUE(got.has_value());
  EXPECT_EQ(got->sequence, 12U);
  EXPECT_EQ(got->samples.at(9).azimuth_counts, 32);
  EXPECT_EQ(receiver.stats().datagrams, 1U);
  EXPECT_EQ(receiver.stats().malformed, 0U);
}

TEST(DatagramReceiver, CountsWhatIsNotABatch) {
  DatagramReceiver receiver = loopback();
  const UdpSender sender(receiver.port());
  std::array<std::byte, ics::tagger::kDatagramBytes> good{};
  ASSERT_EQ(ics::tagger::encode(batch({}), good), good.size());
  std::array<std::byte, ics::tagger::kDatagramBytes> corrupt = good;
  corrupt.at(100) ^= std::byte{1};
  const std::vector<std::byte> longer(ics::tagger::kDatagramBytes + 44, std::byte{0});
  ASSERT_TRUE(sender.send(std::span(good).first(ics::tagger::kDatagramBytes - 1)));
  ASSERT_TRUE(sender.send(longer));
  ASSERT_TRUE(sender.send(corrupt));
  ASSERT_TRUE(sender.send(good));
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(receiver.receive(kArrival).error(), ics::Error::kMalformed);
  }
  EXPECT_TRUE(receiver.receive(kArrival).has_value());
  EXPECT_EQ(receiver.stats().datagrams, 4U);
  EXPECT_EQ(receiver.stats().malformed, 3U);
}

}  // namespace
