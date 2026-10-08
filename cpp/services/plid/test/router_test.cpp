#include "ics/plid/router.hpp"

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "ics/capture/capture.hpp"
#include "ics/frames/enu.hpp"
#include "ics/frames/geodetic.hpp"
#include "mavlink_support.hpp"
#include "plid_support.hpp"
#include "udp_support.hpp"

namespace {

using ics::capture::testing::Bytes;
using ics::capture::testing::ethernet;
using ics::capture::testing::ipv4_udp;
using ics::capture::testing::UdpAddressing;
using ics::plid::Pli;
using ics::plid::Router;
using ics::plid::testing::geoid;
using ics::plid::testing::kStart;

const ics::frames::EnuFrame kRange(
    ics::frames::Geodetic::make(ics::Degrees(40.0), ics::Degrees(-100.0), ics::Meters(700.0)).value());

constexpr std::string_view kAtak =
    R"(<event version="2.0" uid="ANDROID-1" type="a-f-G-U-C" how="m-g" time="2026-09-21T14:13:20.000Z" )"
    R"(start="2026-09-21T14:13:20.000Z" stale="2026-09-21T14:19:20.000Z">)"
    R"(<point lat="40.001" lon="-100.002" hae="681.5" ce="4.9" le="9.9"/></event>)";

std::optional<ics::mavlink::Adapter> mavlink() {
  return std::optional<ics::mavlink::Adapter>(std::in_place, ics::mavlink::AdapterSettings{}, geoid(), kRange);
}

std::optional<ics::plid::CotRoute> cot(const std::uint16_t port) {
  return ics::plid::CotRoute{.port = port, .adapter = ics::cot::Adapter::make({}, kRange).value()};
}

// A captured packet: payload in UDP to port, received at time.
struct Captured {
  Bytes frame;
  ics::capture::Packet packet;
};

Captured captured(const Bytes& payload, const std::uint16_t port, const ics::UtcTime time) {
  Captured out;
  UdpAddressing to;
  to.destination_port = port;
  out.frame = ethernet(ipv4_udp(payload, to));
  out.packet = {.time = time, .original_length = static_cast<std::uint32_t>(out.frame.size()), .bytes = out.frame};
  return out;
}

Bytes text(const std::string_view value) {
  const auto bytes = std::as_bytes(std::span(value));
  return {bytes.begin(), bytes.end()};
}

TEST(Router, SendsMavlinkFramesToTheMavlinkAdapter) {
  Router router(mavlink(), cot(6969));
  const Captured position = captured(ics::capture::testing::from_hex(ics::mavlink::testing::kGlobalPositionInt), 14550, kStart);
  const Captured heartbeat = captured(ics::capture::testing::from_hex(ics::mavlink::testing::kHeartbeat), 14550, kStart);
  ASSERT_TRUE(router.accept(position.packet).has_value());
  ASSERT_TRUE(router.accept(heartbeat.packet).has_value());
  Pli pli;
  router.take(pli);
  EXPECT_EQ(pli.records.size(), 1U);
  // The first HEARTBEAT gives its mode, and that it armed.
  EXPECT_EQ(pli.events.size(), 2U);
  EXPECT_EQ(router.counts().mavlink_frames, 2U);
  Pli again;
  router.take(again);
  EXPECT_TRUE(again.records.empty());
  EXPECT_TRUE(again.events.empty());
}

TEST(Router, SendsTheCotPortToTheCotAdapter) {
  Router router(mavlink(), cot(6969));
  ASSERT_TRUE(router.accept(captured(text(kAtak), 6969, kStart).packet).has_value());
  ASSERT_TRUE(router.accept(captured(text("not xml"), 6969, kStart).packet).has_value());
  std::string chat(kAtak);
  chat.replace(chat.find("a-f-G-U-C"), 9, "b-t-f");
  ASSERT_TRUE(router.accept(captured(text(chat), 6969, kStart).packet).has_value());
  Pli pli;
  router.take(pli);
  ASSERT_EQ(pli.records.size(), 1U);
  EXPECT_EQ(pli.records[0].entity_id(), "ANDROID-1");
  EXPECT_EQ(router.counts().cot_events, 2U);
  EXPECT_EQ(router.counts().cot_unreadable, 1U);
  EXPECT_EQ(router.counts().mavlink_frames, 0U);
}

TEST(Router, KeepsCotOffTheMavlinkAdapterAndIgnoresWhatIsNotUdp) {
  Router cot_only(std::nullopt, cot(6969));
  ASSERT_TRUE(cot_only.accept(captured(ics::capture::testing::from_hex(ics::mavlink::testing::kGlobalPositionInt),
                                       14550, kStart)
                                  .packet)
                  .has_value());
  const Bytes runt{std::byte{1}, std::byte{2}};
  ASSERT_TRUE(cot_only.accept({.time = kStart, .original_length = 2, .bytes = runt}).has_value());
  Pli pli;
  cot_only.take(pli);
  EXPECT_TRUE(pli.records.empty());
  EXPECT_EQ(cot_only.counts().packets, 2U);
  EXPECT_EQ(cot_only.counts().not_udp, 1U);
  // Without a CoT feed, the CoT port's datagrams go to MAVLink.
  Router mavlink_only(mavlink(), std::nullopt);
  ASSERT_TRUE(mavlink_only.accept(captured(text(kAtak), 6969, kStart).packet).has_value());
  EXPECT_EQ(mavlink_only.counts().cot_events, 0U);
}

TEST(Router, TicksTheMavlinkLinkTimeout) {
  Router router(mavlink(), std::nullopt);
  ASSERT_TRUE(router.accept(captured(ics::capture::testing::from_hex(ics::mavlink::testing::kHeartbeat), 14550, kStart)
                                .packet)
                  .has_value());
  router.tick(kStart + std::chrono::seconds(10));
  Pli pli;
  router.take(pli);
  ASSERT_FALSE(pli.events.empty());
  EXPECT_EQ(pli.events.back().kind(), ics::v1::PliEvent::KIND_LINK_LOST);
  Router without(std::nullopt, std::nullopt);
  without.tick(kStart);
}

}  // namespace
