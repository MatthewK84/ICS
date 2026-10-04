#include "ics/cot/event.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "ics/common/units.hpp"

namespace ics::cot {
namespace {

Read read(const std::string_view xml) { return read_events(std::as_bytes(std::span(xml.data(), xml.size()))); }

constexpr std::string_view kPoint = R"(lat="40.0" lon="-100.0" hae="700.0" ce="3.0" le="5.0")";
constexpr std::string_view kAttributes = R"(uid="U-1" type="a-f-G" how="m-g")";

// An event with these point attributes, children after the point and event attributes.
std::string event(const std::string_view point = kPoint, const std::string_view detail = "",
                  const std::string_view attributes = kAttributes) {
  return std::string("<event version=\"2.0\" ") + std::string(attributes) +
         R"( time="2026-10-04T12:00:00Z"><point )" + std::string(point) + "/>" + std::string(detail) + "</event>";
}

TEST(ReadEvents, ReadsAnEvent) {
  const Read got = read(event(kPoint, R"(<detail><track course="90.5" speed="12.25"/></detail>)"));
  ASSERT_EQ(got.events.size(), 1U);
  const Event& only = got.events[0];
  EXPECT_EQ(only.uid, "U-1");
  EXPECT_EQ(only.type, "a-f-G");
  EXPECT_EQ(only.how, "m-g");
  ASSERT_TRUE(only.time.has_value());
  EXPECT_EQ(to_utc_ns(*only.time), 1'791'115'200'000'000'000);
  EXPECT_EQ(only.point.latitude_deg, 40.0);
  EXPECT_EQ(only.point.longitude_deg, -100.0);
  EXPECT_EQ(only.point.hae_m, 700.0);
  EXPECT_EQ(only.point.ce_m, 3.0);
  EXPECT_EQ(only.point.le_m, 5.0);
  ASSERT_TRUE(only.track.has_value());
  EXPECT_EQ(only.track->course_deg, 90.5);
  EXPECT_EQ(only.track->speed_mps, 12.25);
  EXPECT_EQ(got.counts.malformed + got.counts.not_events + got.counts.invalid, 0U);
}

TEST(ReadEvents, ReadsSeveralEventsAndSkipsTopLevelText) {
  const Read got = read("stray text " + event() + "\n" + event() + "<!-- a comment -->");
  EXPECT_EQ(got.events.size(), 2U);
  EXPECT_EQ(got.counts.not_events, 0U);
  EXPECT_TRUE(read("").events.empty());
}

TEST(ReadEvents, CountsWhatIsNotAnEvent) {
  const Read got = read(R"(<message/><auth><cot/></auth>)" + event());
  EXPECT_EQ(got.events.size(), 1U);
  EXPECT_EQ(got.counts.not_events, 2U);
}

TEST(ReadEvents, CountsMalformedXmlAndReadsNoneOfIt) {
  const Read got = read(event() + "<event uid=\"U-2\"");
  EXPECT_TRUE(got.events.empty());
  EXPECT_EQ(got.counts.malformed, 1U);
}

TEST(ReadEvents, NeedsAUidATypeAndAPointOfFiveNumbersInRange) {
  for (const std::string& bad : {
           event(kPoint, "", R"(uid="" type="a-f-G")"),
           event(kPoint, "", R"(uid="U-1")"),
           std::string(R"(<event uid="U-1" type="a-f-G"/>)"),
           event(R"(lat="40.0" lon="-100.0" hae="700.0" ce="3.0")"),
           event(R"(lat="40.0" lon="-100.0" hae="700.0" le="5.0")"),
           event(R"(lat="40.0" lon="-100.0" ce="3.0" le="5.0")"),
           event(R"(lat="40.0" hae="700.0" ce="3.0" le="5.0")"),
           event(R"(lon="-100.0" hae="700.0" ce="3.0" le="5.0")"),
           event(R"(lat="90.5" lon="-100.0" hae="700.0" ce="3.0" le="5.0")"),
           event(R"(lat="-90.5" lon="-100.0" hae="700.0" ce="3.0" le="5.0")"),
           event(R"(lat="40.0" lon="180.5" hae="700.0" ce="3.0" le="5.0")"),
           event(R"(lat="40.0" lon="-180.5" hae="700.0" ce="3.0" le="5.0")"),
           event(R"(lat="north" lon="-100.0" hae="700.0" ce="3.0" le="5.0")"),
           event(R"(lat="40.0x" lon="-100.0" hae="700.0" ce="3.0" le="5.0")"),
           event(R"(lat="   " lon="-100.0" hae="700.0" ce="3.0" le="5.0")"),
           event(R"(lat="inf" lon="-100.0" hae="700.0" ce="3.0" le="5.0")"),
           event(R"(lat="40.0" lon="nan" hae="700.0" ce="3.0" le="5.0")"),
       }) {
    const Read got = read(bad);
    EXPECT_TRUE(got.events.empty()) << bad;
    EXPECT_EQ(got.counts.invalid, 1U) << bad;
  }
}

TEST(ReadEvents, AcceptsTheEdgesOfRangeAndPaddedNumbers) {
  const Read got = read(event(R"(lat=" -90 " lon="180" hae="-12.5e0" ce="9999999.0" le="0")"));
  ASSERT_EQ(got.events.size(), 1U);
  EXPECT_EQ(got.events[0].point.latitude_deg, -90.0);
  EXPECT_EQ(got.events[0].point.longitude_deg, 180.0);
  EXPECT_EQ(got.events[0].point.hae_m, -12.5);
}

TEST(ReadEvents, LeavesOutATimeItCannotRead) {
  const std::string untimed = R"(<event uid="U-1" type="a-f-G"><point lat="1" lon="2" hae="3" ce="4" le="5"/></event>)";
  const std::string garbled =
      R"(<event uid="U-1" type="a-f-G" time="yesterday"><point lat="1" lon="2" hae="3" ce="4" le="5"/></event>)";
  for (const std::string& xml : {untimed, garbled}) {
    const Read got = read(xml);
    ASSERT_EQ(got.events.size(), 1U);
    EXPECT_FALSE(got.events[0].time.has_value());
    EXPECT_EQ(got.events[0].how, "");
  }
}

TEST(ReadEvents, LeavesOutATrackItCannotUse) {
  for (const std::string_view track : {
           "",
           R"(<detail/>)",
           R"(<detail><track speed="3"/></detail>)",
           R"(<detail><track course="10"/></detail>)",
           R"(<detail><track course="-1" speed="3"/></detail>)",
           R"(<detail><track course="360" speed="3"/></detail>)",
           R"(<detail><track course="10" speed="-3"/></detail>)",
           R"(<detail><track course="10" speed="9999999.0"/></detail>)",
       }) {
    const Read got = read(event(kPoint, track));
    ASSERT_EQ(got.events.size(), 1U) << track;
    EXPECT_FALSE(got.events[0].track.has_value()) << track;
  }
}

}  // namespace
}  // namespace ics::cot
