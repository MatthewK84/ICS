#include "ics/lattice/event.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"

namespace ics::lattice {
namespace {

// 2026-10-04T12:00:00Z.
constexpr std::int64_t kNoon = 1'791'115'200'000'000'000;

// The event decoded from json, which must be an entity event.
Event decoded(const std::string_view json) {
  const Result<std::optional<Event>> got = decode_event(json);
  EXPECT_TRUE(got.has_value() && got->has_value()) << json;
  return got.value_or(std::nullopt).value_or(Event{});
}

// An UPDATE event for entity E-1 with these entity members after its ID.
std::string update(const std::string_view members) {
  return std::string(R"({"event":"entity","eventType":"EVENT_TYPE_UPDATE","time":"2026-10-04T12:00:01Z",)") +
         R"("entity":{"entityId":"E-1")" + std::string(members) + "}}";
}

TEST(DecodeEvent, ReadsAnEntityEvent) {
  const Event event = decoded(update(
      R"(,"isLive":true,"location":{"position":{"latitudeDegrees":40.5,"longitudeDegrees":-100.25,)"
      R"("altitudeHaeMeters":712.5},"velocityEnu":{"e":1.5,"n":-2,"u":0.25}},)"
      R"("locationUncertainty":{"positionEnuCov":{"mxx":9,"mxy":1,"mxz":2,"myy":4,"myz":3,"mzz":16}},)"
      R"("provenance":{"integrationName":"example","sourceUpdateTime":"2026-10-04T12:00:00.250Z"},)"
      R"("aliases":{"name":"Example"},"ontology":{"template":"TEMPLATE_ASSET"})"));
  EXPECT_EQ(event.type, EventType::kUpdate);
  ASSERT_TRUE(event.time.has_value());
  EXPECT_EQ(to_utc_ns(*event.time), kNoon + 1'000'000'000);
  EXPECT_EQ(event.entity.entity_id, "E-1");
  EXPECT_TRUE(event.entity.live);
  ASSERT_TRUE(event.entity.position.has_value());
  EXPECT_EQ(event.entity.position->latitude_deg, 40.5);
  EXPECT_EQ(event.entity.position->longitude_deg, -100.25);
  EXPECT_EQ(event.entity.position->hae_m, 712.5);
  ASSERT_TRUE(event.entity.velocity_enu_mps.has_value());
  EXPECT_EQ(event.entity.velocity_enu_mps->east, 1.5);
  EXPECT_EQ(event.entity.velocity_enu_mps->north, -2.0);
  EXPECT_EQ(event.entity.velocity_enu_mps->up, 0.25);
  ASSERT_TRUE(event.entity.position_covariance_m2.has_value());
  const Covariance& covariance = *event.entity.position_covariance_m2;
  EXPECT_EQ(covariance.xx, 9.0);
  EXPECT_EQ(covariance.xy, 1.0);
  EXPECT_EQ(covariance.xz, 2.0);
  EXPECT_EQ(covariance.yy, 4.0);
  EXPECT_EQ(covariance.yz, 3.0);
  EXPECT_EQ(covariance.zz, 16.0);
  ASSERT_TRUE(event.entity.source_update_time.has_value());
  EXPECT_EQ(to_utc_ns(*event.entity.source_update_time), kNoon + 250'000'000);
}

TEST(DecodeEvent, GivesNothingForAPayloadWithoutAnEntity) {
  for (const std::string_view json : {R"({"event":"heartbeat","timestamp":"2026-10-04T12:00:00Z"})", R"({})",
                                      R"({"entity":"E-1"})"}) {
    const Result<std::optional<Event>> got = decode_event(json);
    ASSERT_TRUE(got.has_value()) << json;
    EXPECT_FALSE(got->has_value()) << json;
  }
}

TEST(DecodeEvent, RefusesWhatIsNotAJsonObjectOrAnEntityWithoutAnId) {
  for (const std::string_view json :
       {"", "not json", "[1,2]", R"("text")", R"({"entity":{"entityId":"E-1"})", R"({"entity":{}})",
        R"({"entity":{"entityId":""}})", R"({"entity":{"entityId":7}})"}) {
    EXPECT_EQ(decode_event(json).error(), Error::kMalformed) << json;
  }
}

TEST(DecodeEvent, ReadsEveryEventType) {
  const auto type_of = [](const std::string_view member) {
    return decoded(std::string("{") + std::string(member) + R"("entity":{"entityId":"E-1"}})").type;
  };
  EXPECT_EQ(type_of(R"("eventType":"EVENT_TYPE_PREEXISTING",)"), EventType::kPreexisting);
  EXPECT_EQ(type_of(R"("eventType":"EVENT_TYPE_CREATED",)"), EventType::kCreated);
  EXPECT_EQ(type_of(R"("eventType":"EVENT_TYPE_UPDATE",)"), EventType::kUpdate);
  EXPECT_EQ(type_of(R"("eventType":"EVENT_TYPE_DELETED",)"), EventType::kDeleted);
  EXPECT_EQ(type_of(R"("eventType":"EVENT_TYPE_POST_EXPIRY_OVERRIDE",)"), EventType::kOther);
  EXPECT_EQ(type_of(R"("eventType":3,)"), EventType::kOther);
  EXPECT_EQ(type_of(""), EventType::kOther);
}

TEST(DecodeEvent, TakesAMissingNumberAsTheZeroProtobufLeftOut) {
  const Event event = decoded(update(R"(,"location":{"position":{"latitudeDegrees":12.5},"velocityEnu":{"e":3}},)"
                                     R"("locationUncertainty":{"positionEnuCov":{"mxx":4,"mzz":9}})"));
  ASSERT_TRUE(event.entity.position.has_value());
  EXPECT_EQ(event.entity.position->latitude_deg, 12.5);
  EXPECT_EQ(event.entity.position->longitude_deg, 0.0);
  EXPECT_FALSE(event.entity.position->hae_m.has_value());
  ASSERT_TRUE(event.entity.velocity_enu_mps.has_value());
  EXPECT_EQ(event.entity.velocity_enu_mps->north, 0.0);
  EXPECT_EQ(event.entity.velocity_enu_mps->up, 0.0);
  ASSERT_TRUE(event.entity.position_covariance_m2.has_value());
  EXPECT_EQ(event.entity.position_covariance_m2->yy, 0.0);
  EXPECT_EQ(event.entity.position_covariance_m2->zz, 9.0);
  EXPECT_EQ(decoded(update(R"(,"location":{"position":{"longitudeDegrees":-1}})")).entity.position->latitude_deg, 0.0);
}

TEST(DecodeEvent, LeavesOutAPartThatIsMissingOrNotNumbers) {
  for (const std::string_view members : {
           R"()",
           R"(,"location":"here")",
           R"(,"location":{})",
           R"(,"location":{"position":{}})",
           R"(,"location":{"position":{"altitudeHaeMeters":5}})",
           R"(,"location":{"position":{"latitudeDegrees":"north","longitudeDegrees":1}})",
           R"(,"location":{"position":{"latitudeDegrees":1,"longitudeDegrees":"NaN"}})",
           R"(,"location":{"position":{"latitudeDegrees":1,"longitudeDegrees":null}})",
       }) {
    EXPECT_FALSE(decoded(update(members)).entity.position.has_value()) << members;
  }
  for (const std::string_view velocity : {R"({"e":"1"})", R"({"n":true})", R"({"u":[]})", R"("fast")"}) {
    const std::string members = std::string(R"(,"location":{"velocityEnu":)") + std::string(velocity) + "}";
    EXPECT_FALSE(decoded(update(members)).entity.velocity_enu_mps.has_value()) << velocity;
  }
  for (const std::string_view cell : {"mxx", "mxy", "mxz", "myy", "myz", "mzz"}) {
    const std::string members =
        std::string(R"(,"locationUncertainty":{"positionEnuCov":{")") + std::string(cell) + R"(":"Infinity"}})";
    EXPECT_FALSE(decoded(update(members)).entity.position_covariance_m2.has_value()) << cell;
  }
  EXPECT_FALSE(decoded(update(R"(,"locationUncertainty":{})")).entity.position_covariance_m2.has_value());
  const Event odd_height = decoded(update(R"(,"location":{"position":{"latitudeDegrees":1,"altitudeHaeMeters":"high"}})"));
  ASSERT_TRUE(odd_height.entity.position.has_value());
  EXPECT_FALSE(odd_height.entity.position->hae_m.has_value());
}

TEST(DecodeEvent, ReadsLiveOnlyFromAnExplicitFalse) {
  EXPECT_FALSE(decoded(update(R"(,"isLive":false)")).entity.live);
  EXPECT_TRUE(decoded(update(R"(,"isLive":true)")).entity.live);
  EXPECT_TRUE(decoded(update(R"(,"isLive":"false")")).entity.live);
  EXPECT_TRUE(decoded(update("")).entity.live);
}

TEST(DecodeEvent, ReadsTimesFrom1970To2200) {
  const auto source_time = [](const std::string_view value) {
    return decoded(update(std::string(R"(,"provenance":{"sourceUpdateTime":)") + std::string(value) + "}"))
        .entity.source_update_time;
  };
  EXPECT_EQ(to_utc_ns(source_time(R"("2026-10-04T13:00:00.000000001+01:00")").value_or(UtcTime{})), kNoon + 1);
  EXPECT_EQ(to_utc_ns(source_time(R"("1970-01-01T00:00:00Z")").value_or(UtcTime{} + std::chrono::seconds(1))), 0);
  EXPECT_TRUE(source_time(R"("2200-12-31T23:59:59.999999999Z")").has_value());
  for (const std::string_view bad : {R"("1969-12-31T23:59:59Z")", R"("2201-01-01T00:00:00Z")", R"("yesterday")",
                                     R"(1791115200)", R"(null)"}) {
    EXPECT_FALSE(source_time(bad).has_value()) << bad;
  }
  EXPECT_FALSE(decoded(update(R"(,"provenance":{})")).entity.source_update_time.has_value());
  EXPECT_FALSE(decoded(R"({"time":"never","entity":{"entityId":"E-1"}})").time.has_value());
}

}  // namespace
}  // namespace ics::lattice
