#include "ics/plid/sapient_link.hpp"

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <sys/socket.h>

#include "framing.hpp"
#include "ics/frames/enu.hpp"
#include "ics/frames/geodetic.hpp"
#include "plid_support.hpp"
#include "support.hpp"

namespace {

namespace bsi = sapient_msg::bsi_flex_335_v2_0;

using ics::plid::SapientLink;
using ics::plid::testing::geoid;
using ics::plid::testing::kStart;
using ics::plid::testing::kStartNs;
using ics::plid::testing::Logged;
using ics::plid::testing::TcpListener;
using std::chrono::milliseconds;
using std::chrono::seconds;

ics::sapient::Adapter adapter() {
  return ics::sapient::Adapter::make(
             {}, geoid(),
             ics::frames::EnuFrame(
                 ics::frames::Geodetic::make(ics::Degrees(40.0), ics::Degrees(-100.0), ics::Meters(700.0)).value()))
      .value();
}

SapientLink link(const std::uint16_t port) {
  std::string reason;
  return SapientLink::make("127.0.0.1", port, adapter(), reason).value();
}

// A detection at a latitude and longitude, which needs no registration.
ics::sapient::Message detection(const std::string& object_id) {
  ics::sapient::Message message;
  message.set_node_id("3f2a9c1e-5b7d-4e8f-9a01-2c3d4e5f6a7b");
  message.mutable_timestamp()->set_seconds(kStartNs / 1'000'000'000);
  bsi::DetectionReport& report = *message.mutable_detection_report();
  report.set_object_id(object_id);
  bsi::Location& location = *report.mutable_location();
  location.set_x(-100.001);
  location.set_y(40.001);
  location.set_z(735.0);
  location.set_coordinate_system(bsi::LOCATION_COORDINATE_SYSTEM_LAT_LNG_DEG_M);
  location.set_datum(bsi::LOCATION_DATUM_WGS84_E);
  return message;
}

void send_all(const int socket, const std::vector<std::byte>& bytes) {
  std::size_t sent = 0;
  while (sent < bytes.size()) {
    const ssize_t count = ::send(socket, std::span(bytes).subspan(sent).data(), bytes.size() - sent, MSG_NOSIGNAL);
    ASSERT_GT(count, 0);
    sent += static_cast<std::size_t>(count);
  }
}

// Steps link until it has connected, at most 100 times.
void connect(SapientLink& sapient, const ics::UtcTime now, const ics::logging::Logger& logger) {
  std::vector<ics::v1::PliRecord> ignored;
  for (int step = 0; step < 100 && !sapient.connected(); ++step) {
    sapient.step(now, ignored, logger);
  }
  ASSERT_TRUE(sapient.connected());
}

// Steps link until it has read count records, at most 1000 times.
std::vector<ics::v1::PliRecord> read(SapientLink& sapient, const std::size_t count, const ics::logging::Logger& logger) {
  std::vector<ics::v1::PliRecord> out;
  for (int step = 0; step < 1000 && out.size() < count; ++step) {
    sapient.step(kStart, out, logger);
  }
  return out;
}

TEST(SapientLink, ReadsDetectionsAndConnectsAgainWhenClosed) {
  const TcpListener listener;
  SapientLink sapient = link(listener.port());
  EXPECT_EQ(sapient.descriptor().fd, -1);
  Logged logged;
  connect(sapient, kStart, logged.logger);
  EXPECT_EQ(sapient.descriptor().events, POLLIN);
  {
    const ics::timing::Fd middleware = listener.accept();
    ics::sapient::Message status;
    status.set_node_id("3f2a9c1e-5b7d-4e8f-9a01-2c3d4e5f6a7b");
    status.mutable_status_report();
    std::vector<std::byte> bytes = ics::sapient::testing::frame(detection("A"));
    const std::vector<std::byte> more = ics::sapient::testing::frame(status);
    bytes.insert(bytes.end(), more.begin(), more.end());
    send_all(middleware.get(), bytes);
    const std::vector<ics::v1::PliRecord> records = read(sapient, 1, logged.logger);
    ASSERT_EQ(records.size(), 1U);
    EXPECT_EQ(records[0].entity_id(), "A");
    EXPECT_EQ(records[0].source(), ics::v1::PLI_SOURCE_SAPIENT);
  }
  read(sapient, 1, logged.logger);
  EXPECT_FALSE(sapient.connected());
  EXPECT_TRUE(logged.has(R"("reason":"closed by the middleware","retry_in_ns":1000000000)"));
  // It waits a second, then connects again.
  std::vector<ics::v1::PliRecord> none;
  sapient.step(kStart + milliseconds(999), none, logged.logger);
  EXPECT_EQ(sapient.counts().connections, 1U);
  connect(sapient, kStart + seconds(1), logged.logger);
  EXPECT_EQ(sapient.counts().connections, 2U);
  EXPECT_EQ(sapient.counts().messages, 2U);
  EXPECT_EQ(sapient.counts().records, 1U);
  EXPECT_TRUE(logged.has(R"("event":"sapient_connected","connections":2)"));
}

TEST(SapientLink, DoublesItsPauseWhileNothingListens) {
  std::uint16_t port = 0;
  {
    const TcpListener gone;
    port = gone.port();
  }
  SapientLink sapient = link(port);
  Logged logged;
  std::vector<ics::v1::PliRecord> none;
  ics::UtcTime now = kStart;
  for (const char* const pause : {"1000000000", "2000000000", "4000000000"}) {
    for (int step = 0; step < 100 && sapient.descriptor().fd < 0; ++step) {
      sapient.step(now, none, logged.logger);
    }
    for (int step = 0; step < 100 && sapient.descriptor().fd >= 0; ++step) {
      sapient.step(now, none, logged.logger);
    }
    EXPECT_TRUE(logged.has(std::string(R"("reason":"connect failed","retry_in_ns":)") + pause)) << pause;
    now += seconds(4);
  }
  EXPECT_FALSE(sapient.connected());
}

TEST(SapientLink, WaitsForAConnectionStillInProgress) {
  // A listener whose queue is full drops the next SYN, so that connection
  // stays in progress.
  const TcpListener full(0);
  const ics::timing::Fd first(::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(full.port());
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ASSERT_EQ(::connect(first.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)), 0);
  SapientLink sapient = link(full.port());
  Logged logged;
  std::vector<ics::v1::PliRecord> none;
  sapient.step(kStart, none, logged.logger);
  sapient.step(kStart, none, logged.logger);
  EXPECT_FALSE(sapient.connected());
  EXPECT_EQ(sapient.descriptor().events, POLLOUT);
  EXPECT_GE(sapient.descriptor().fd, 0);
}

TEST(SapientLink, DropsAStreamThatBreaksItsFraming) {
  const TcpListener listener;
  SapientLink sapient = link(listener.port());
  Logged logged;
  connect(sapient, kStart, logged.logger);
  const ics::timing::Fd middleware = listener.accept();
  // A length of 4 GiB - 1.
  send_all(middleware.get(), {std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}});
  read(sapient, 1, logged.logger);
  EXPECT_FALSE(sapient.connected());
  EXPECT_TRUE(logged.has(R"("reason":"a message broke the framing")"));
}

TEST(SapientLink, DropsAConnectionTheMiddlewareResets) {
  const TcpListener listener;
  SapientLink sapient = link(listener.port());
  Logged logged;
  connect(sapient, kStart, logged.logger);
  {
    const ics::timing::Fd middleware = listener.accept();
    const linger reset{.l_onoff = 1, .l_linger = 0};
    ASSERT_EQ(::setsockopt(middleware.get(), SOL_SOCKET, SO_LINGER, &reset, sizeof(reset)), 0);
  }
  read(sapient, 1, logged.logger);
  EXPECT_FALSE(sapient.connected());
  EXPECT_TRUE(logged.has(R"("reason":"read failed")"));
}

TEST(SapientLink, ReadsABoundedAmountPerStep) {
  const TcpListener listener;
  SapientLink sapient = link(listener.port());
  Logged logged;
  connect(sapient, kStart, logged.logger);
  const ics::timing::Fd middleware = listener.accept();
  std::vector<std::byte> bytes;
  const std::vector<std::byte> one = ics::sapient::testing::frame(detection("A"));
  while (bytes.size() < 2 * SapientLink::kReadBytes * SapientLink::kMaxReads) {
    bytes.insert(bytes.end(), one.begin(), one.end());
  }
  const std::size_t count = bytes.size() / one.size();
  std::thread sender([&middleware, &bytes] { send_all(middleware.get(), bytes); });
  std::vector<ics::v1::PliRecord> first;
  for (int step = 0; step < 1000 && first.empty(); ++step) {
    sapient.step(kStart, first, logged.logger);
  }
  EXPECT_LT(first.size(), count);
  const std::vector<ics::v1::PliRecord> rest = read(sapient, count - first.size(), logged.logger);
  sender.join();
  EXPECT_EQ(first.size() + rest.size(), count);
  EXPECT_TRUE(sapient.connected());
}

TEST(SapientLink, ReportsASocketItCannotMake) {
  const TcpListener listener;
  SapientLink sapient = link(listener.port());
  Logged logged;
  std::vector<ics::v1::PliRecord> none;
  {
    const ics::timing::testing::DescriptorLimit limit;
    sapient.step(kStart, none, logged.logger);
  }
  EXPECT_FALSE(sapient.connected());
  EXPECT_TRUE(logged.has(R"("reason":"no socket")"));
}

TEST(SapientLink, RefusesAnAddressThatIsNotIpv4) {
  for (const char* address : {"localhost", "::1", "10.0.0"}) {
    std::string reason;
    EXPECT_EQ(SapientLink::make(address, 5020, adapter(), reason).error(), ics::Error::kInvalidArgument);
    EXPECT_NE(reason.find(address), std::string::npos);
  }
}

}  // namespace
