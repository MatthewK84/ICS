#include "ics/lattice/stream_client.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "http_server.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/lattice/event.hpp"

namespace ics::lattice {
namespace {

using std::chrono::milliseconds;
using std::chrono::seconds;
using testing::status;
using testing::stream;
using testing::TestServer;

// Keeps every event, and sets stop once it has the number wanted.
class Collector final : public EventSink {
 public:
  Collector(std::atomic<bool>& stop, const std::size_t wanted) : stop_(stop), wanted_(wanted) {}

  void accept(const Event& event, const UtcTime received) override {
    events_.push_back(event);
    received_.push_back(received);
    if (events_.size() >= wanted_) {
      stop_.store(true);
    }
  }

  std::vector<Event> events_;
  std::vector<UtcTime> received_;

 private:
  std::atomic<bool>& stop_;
  std::size_t wanted_;
};

ClientSettings settings(const std::string& url) {
  return ClientSettings{.url = url,
                        .token = "environment-token",
                        .sandbox_token = "",
                        .ca_file = "",
                        .components = {},
                        .heartbeat_interval = seconds(1),
                        .connect_timeout = seconds(2),
                        .first_backoff = milliseconds(10),
                        .max_backoff = milliseconds(40)};
}

std::string entity(const std::string& id) {
  return R"(data: {"event":"entity","eventType":"EVENT_TYPE_UPDATE","entity":{"entityId":")" + id + "\"}}\n\n";
}

TEST(StreamClient, SendsTheRequestAndHandsOnEachEvent) {
  const TestServer server({stream({entity("E-1").substr(0, 30), entity("E-1").substr(30) + entity("E-2")}, true)});
  ClientSettings with_sandbox = settings(server.url() + "/");
  with_sandbox.sandbox_token = "sandbox-token";
  with_sandbox.components = {"location", "location_uncertainty"};
  StreamClient client = StreamClient::make(with_sandbox).value();
  std::atomic<bool> stop{false};
  Collector sink(stop, 2);
  ASSERT_TRUE(client.run(sink, stop).has_value());
  ASSERT_EQ(sink.events_.size(), 2U);
  EXPECT_EQ(sink.events_[0].entity.entity_id, "E-1");
  EXPECT_EQ(sink.events_[1].entity.entity_id, "E-2");
  EXPECT_LE(sink.received_[0], sink.received_[1]);
  EXPECT_EQ(client.stats().connections, 1U);
  EXPECT_EQ(client.stats().events, 2U);
  EXPECT_EQ(client.stats().last_http_status, 200);
  const std::vector<std::string> requests = server.requests();
  ASSERT_EQ(requests.size(), 1U);
  const std::string& request = requests[0];
  EXPECT_TRUE(request.starts_with("POST /api/v1/entities/stream HTTP/1.1\r\n")) << request;
  for (const std::string expected :
       {"Authorization: Bearer environment-token\r\n", "Anduril-Sandbox-Authorization: Bearer sandbox-token\r\n",
        "Content-Type: application/json\r\n", "Accept: text/event-stream\r\n", R"("heartbeatIntervalMS":1000)",
        R"("preExistingOnly":false)", R"("componentsToInclude":["location","location_uncertainty"])"}) {
    EXPECT_NE(request.find(expected), std::string::npos) << expected << " in " << request;
  }
}

TEST(StreamClient, ConnectsAgainAfterErrorsAndDroppedStreams) {
  const TestServer server({status("503 Service Unavailable"), status("408 Request Timeout"),
                           status("429 Too Many Requests"),
                           stream({": heartbeat\n\n", "data: not json\n\n", R"(data: {"event":"heartbeat"})" "\n\n",
                                   entity("E-1")}),
                           stream({entity("E-2")}, true)});
  StreamClient client = StreamClient::make(settings(server.url())).value();
  std::atomic<bool> stop{false};
  Collector sink(stop, 2);
  ASSERT_TRUE(client.run(sink, stop).has_value());
  ASSERT_EQ(sink.events_.size(), 2U);
  EXPECT_EQ(sink.events_[1].entity.entity_id, "E-2");
  EXPECT_EQ(client.stats().connections, 5U);
  EXPECT_EQ(client.stats().malformed, 1U);
  EXPECT_EQ(server.requests().size(), 5U);
}

TEST(StreamClient, ConnectsAgainWhenTheStreamFallsSilent) {
  // Silent for three one-second heartbeat intervals.
  const TestServer server({stream({}, true), stream({entity("E-1")}, true)});
  StreamClient client = StreamClient::make(settings(server.url())).value();
  std::atomic<bool> stop{false};
  Collector sink(stop, 1);
  ASSERT_TRUE(client.run(sink, stop).has_value());
  EXPECT_EQ(sink.events_.size(), 1U);
  EXPECT_EQ(client.stats().connections, 2U);
}

TEST(StreamClient, GivesUpWhenTheServerRefusesTheRequest) {
  for (const std::string line : {"401 Unauthorized", "403 Forbidden", "404 Not Found", "499 Other"}) {
    const TestServer server({status(line)});
    StreamClient client = StreamClient::make(settings(server.url())).value();
    std::atomic<bool> stop{false};
    Collector sink(stop, 1);
    EXPECT_EQ(client.run(sink, stop).error(), Error::kUnavailable) << line;
    EXPECT_EQ(std::to_string(client.stats().last_http_status), line.substr(0, 3));
    EXPECT_NE(client.stats().last_error.find("HTTP"), std::string::npos) << client.stats().last_error;
  }
}

TEST(StreamClient, StopsWhileItWaitsToConnectAgain) {
  // Nothing listens on the port once this server has gone.
  std::string url;
  {
    const TestServer gone({});
    url = gone.url();
  }
  ClientSettings slow = settings(url);
  slow.first_backoff = seconds(30);
  slow.max_backoff = seconds(30);
  StreamClient client = StreamClient::make(slow).value();
  std::atomic<bool> stop{false};
  Collector sink(stop, 1);
  std::thread stopper([&stop] {
    std::this_thread::sleep_for(milliseconds(200));
    stop.store(true);
  });
  const auto started = std::chrono::steady_clock::now();
  EXPECT_TRUE(client.run(sink, stop).has_value());
  stopper.join();
  EXPECT_LT(std::chrono::steady_clock::now() - started, seconds(5));
  EXPECT_EQ(client.stats().connections, 1U);
  EXPECT_EQ(client.stats().last_http_status, 0);
  // Already stopped: no connection at all.
  StreamClient idle = StreamClient::make(settings(url)).value();
  EXPECT_TRUE(idle.run(sink, stop).has_value());
  EXPECT_EQ(idle.stats().connections, 0U);
}

TEST(StreamClient, RefusesSettingsItCannotUseSafely) {
  const ClientSettings good = settings("https://lattice.example.com");
  ASSERT_TRUE(StreamClient::make(good).has_value());
  ClientSettings with_ca = good;
  with_ca.ca_file = "/etc/ssl/certs/ca-certificates.crt";
  StreamClient replaced = StreamClient::make(good).value();
  replaced = StreamClient::make(with_ca).value();
  EXPECT_EQ(replaced.stats().connections, 0U);
  std::vector<ClientSettings> bad(16, good);
  // The same URL without TLS: plain HTTP to another host.
  bad[0].url = std::string(good.url).erase(4, 1);
  bad[1].url = "ftp://127.0.0.1:21";
  bad[2].url = "https://lattice.example.com/a b";
  bad[3].url = "";
  bad[4].token = "";
  bad[5].token = "two words";
  bad[6].token = "line\r\nX-Injected: yes";
  bad[7].sandbox_token = "bad\ttoken";
  bad[8].components = {"location", ""};
  bad[9].components = {"location-uncertainty"};
  bad[10].heartbeat_interval = seconds(0);
  bad[11].connect_timeout = seconds(0);
  bad[12].first_backoff = Duration::zero();
  bad[13].max_backoff = milliseconds(5);
  bad[14].components = {"Location_2"};
  bad[15].components = {"loc~ation"};
  ASSERT_EQ(bad[0].url, "http" + good.url.substr(5));
  for (std::size_t i = 0; i < bad.size(); ++i) {
    EXPECT_EQ(StreamClient::make(bad[i]).error(), Error::kInvalidArgument) << i;
  }
  for (const std::string url : {"http://localhost:8080", "http://[::1]:8080", "http://127.0.0.1:8080"}) {
    EXPECT_TRUE(StreamClient::make(settings(url)).has_value()) << url;
  }
}

}  // namespace
}  // namespace ics::lattice
