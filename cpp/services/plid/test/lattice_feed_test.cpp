#include "ics/plid/lattice_feed.hpp"

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "http_server.hpp"
#include "ics/frames/enu.hpp"
#include "ics/frames/geodetic.hpp"
#include "plid_support.hpp"

namespace {

using ics::lattice::testing::status;
using ics::lattice::testing::stream;
using ics::lattice::testing::TestServer;
using ics::plid::LatticeFeed;
using ics::plid::testing::Logged;

ics::lattice::StreamClient client(const std::string& url) {
  return ics::lattice::StreamClient::make({.url = url,
                                           .token = "environment-token",
                                           .sandbox_token = "",
                                           .ca_file = "",
                                           .components = {},
                                           .heartbeat_interval = std::chrono::seconds(1),
                                           .connect_timeout = std::chrono::seconds(2),
                                           .first_backoff = std::chrono::milliseconds(10),
                                           .max_backoff = std::chrono::milliseconds(40)})
      .value();
}

ics::lattice::Adapter adapter() {
  return ics::lattice::Adapter::make(
             {}, ics::frames::EnuFrame(
                     ics::frames::Geodetic::make(ics::Degrees(40.0), ics::Degrees(-100.0), ics::Meters(700.0)).value()))
      .value();
}

std::string entity(const std::string& id) {
  return R"(data: {"event":"entity","eventType":"EVENT_TYPE_UPDATE","entity":{"entityId":")" + id +
         R"(","location":{"position":{"latitudeDegrees":40.001,"longitudeDegrees":-100.002,"altitudeHaeMeters":735.5}}}})"
         "\n\n";
}

TEST(LatticeFeed, HandsOnARecordForEachEntity) {
  const std::string unlocated =
      R"(data: {"event":"entity","eventType":"EVENT_TYPE_UPDATE","entity":{"entityId":"E-0"}})" "\n\n";
  const TestServer server(
      {stream({unlocated, entity("E-1"), entity("E-2"), R"(data: {"event":"heartbeat"})" "\n\n"}, true)});
  Logged logged;
  std::vector<ics::v1::PliRecord> records;
  {
    LatticeFeed feed(client(server.url()), adapter(), logged.logger);
    for (int wait = 0; wait < 500 && records.size() < 2; ++wait) {
      feed.take(records);
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_FALSE(feed.ended());
  }
  ASSERT_EQ(records.size(), 2U);
  EXPECT_EQ(records[0].entity_id(), "E-1");
  EXPECT_EQ(records[1].source(), ics::v1::PLI_SOURCE_LATTICE);
}

TEST(LatticeFeed, EndsWhenLatticeRefusesTheRequest) {
  const TestServer server({status("401 Unauthorized")});
  Logged logged;
  {
    const LatticeFeed feed(client(server.url()), adapter(), logged.logger);
    for (int wait = 0; wait < 500 && !feed.ended(); ++wait) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(feed.ended());
  }
  EXPECT_TRUE(logged.has(R"("event":"lattice_refused","http_status":401)"));
}

}  // namespace
