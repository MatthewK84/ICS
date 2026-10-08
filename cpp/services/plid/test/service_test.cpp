#include "ics/plid/service.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <sys/stat.h>

#include "capture_support.hpp"
#include "framing.hpp"
#include "ics/capture/capture.hpp"
#include "ics/capture/datagram.hpp"
#include "ics/capture/pcap_format.hpp"
#include "ics/frames/enu.hpp"
#include "ics/frames/geodetic.hpp"
#include "ics/mavlink/adapter.hpp"
#include "ics/mavlink/frame.hpp"
#include "ics/store/archive.hpp"
#include "ics/store/segment_writer.hpp"
#include "plid_support.hpp"
#include "support.hpp"
#include "udp_support.hpp"

namespace {

using ics::plid::Config;
using ics::plid::Service;
using ics::plid::testing::everything;
using ics::plid::testing::geoid;
using ics::plid::testing::kStart;
using ics::plid::testing::Logged;
using ics::plid::testing::replay_config;
using ics::timing::testing::TempDir;
using ics::v1::QueryPliRequest;
using std::chrono::minutes;

// What ics-mavlink-replay makes of the crossing capture: the MAVLink
// adapter's records and events, in the order it made them.
struct Replayed {
  std::vector<ics::v1::PliRecord> records;
  std::vector<ics::v1::PliEvent> events;
};

class Replayer final : public ics::capture::PacketSink {
 public:
  explicit Replayer(ics::mavlink::Adapter& adapter) : adapter_(adapter) {}

  ics::Status accept(const ics::capture::Packet& packet) override {
    adapter_.tick(packet.time, out_);
    const std::optional<ics::capture::Datagram> datagram = ics::capture::udp_datagram(packet.bytes);
    ics::mavlink::FrameReader reader(datagram ? datagram->payload : std::span<const std::byte>{});
    for (std::optional<ics::mavlink::Frame> frame = reader.next(); frame; frame = reader.next()) {
      adapter_.receive(*frame, packet.time, out_);
    }
    for (ics::mavlink::Position& position : out_.positions) {
      replayed.records.push_back(position.record);
    }
    replayed.events.insert(replayed.events.end(), out_.events.begin(), out_.events.end());
    out_ = {};
    return {};
  }

  Replayed replayed;

 private:
  ics::mavlink::Adapter& adapter_;
  ics::mavlink::Output out_;
};

Replayed replay(const Config& config) {
  const ics::frames::EnuFrame range(
      ics::frames::Geodetic::make(config.range.latitude, config.range.longitude, config.range.height).value());
  ics::mavlink::Adapter adapter({.roles = {{.system = 1, .role = ics::v1::ENTITY_ROLE_TARGET},
                                           {.system = 2, .role = ics::v1::ENTITY_ROLE_INTERCEPTOR}},
                                 .link_timeout = config.mavlink->link_timeout,
                                 .max_age = config.mavlink->max_age},
                                geoid(), range);
  std::string reason;
  ics::capture::Capture capture = ics::capture::Capture::open_file(config.files.at(0), reason).value();
  Replayer replayer(adapter);
  for (ics::Result<std::size_t> count = std::size_t{1}; count && *count > 0; count = capture.dispatch(replayer, 256)) {
  }
  return std::move(replayer.replayed);
}

// Sorted as a query returns them: by time, then in the order stored.
template <typename Message, typename Time>
std::vector<std::string> by_time(std::vector<Message> messages, const Time time) {
  std::ranges::stable_sort(messages, [time](const Message& a, const Message& b) { return time(a) < time(b); });
  std::vector<std::string> out;
  for (const Message& message : messages) {
    out.push_back(message.SerializeAsString());
  }
  return out;
}

template <typename Message>
std::vector<std::string> serialized(const google::protobuf::RepeatedPtrField<Message>& messages) {
  std::vector<std::string> out;
  for (const Message& message : messages) {
    out.push_back(message.SerializeAsString());
  }
  return out;
}

Service open(const Config& config, const ics::logging::Logger& logger) {
  std::string reason;
  ics::Result<Service> service = Service::open(config, geoid(), kStart, logger, reason);
  EXPECT_TRUE(service.has_value()) << reason;
  return std::move(*service);
}

// Steps service until every file is replayed, at most 1000 times.
void replay_all(Service& service, const ics::logging::Logger& logger) {
  for (int step = 0; step < 1000 && !service.replayed(); ++step) {
    ASSERT_TRUE(service.step(kStart, logger).has_value());
  }
  ASSERT_TRUE(service.replayed());
  ASSERT_TRUE(service.step(kStart, logger).has_value());
}

TEST(Service, StoresTheSitlCaptureAsTheMavlinkAdapterReadsIt) {
  const TempDir folder;
  const Config config = replay_config(folder / "");
  const Replayed expected = replay(config);
  ASSERT_GT(expected.records.size(), 10U);
  ASSERT_GT(expected.events.size(), 5U);
  Logged logged;
  std::filesystem::path segment;
  {
    Service service = open(config, logged.logger);
    replay_all(service, logged.logger);
    EXPECT_TRUE(service.descriptors().empty());
    segment = service.ingest().path();
    const ics::v1::QueryPliResponse records = everything(config.query_socket, QueryPliRequest::KIND_RECORDS);
    EXPECT_EQ(serialized(records.records()),
              by_time(expected.records, [](const ics::v1::PliRecord& record) { return record.valid_utc_ns(); }));
    const ics::v1::QueryPliResponse events = everything(config.query_socket, QueryPliRequest::KIND_EVENTS);
    EXPECT_EQ(serialized(events.events()),
              by_time(expected.events, [](const ics::v1::PliEvent& event) { return event.time_utc_ns(); }));
    ASSERT_TRUE(service.close(logged.logger).has_value());
    EXPECT_EQ(service.ingest().stored(), expected.records.size() + expected.events.size());
  }
  EXPECT_TRUE(ics::store::is_archived(segment));
  EXPECT_TRUE(logged.has(R"("event":"segment_closed")"));
  EXPECT_TRUE(logged.has(R"("event":"archived")"));
  EXPECT_TRUE(logged.has(R"("event":"replayed","stored":)" + std::to_string(expected.records.size() + expected.events.size())));
}

TEST(Service, RotatesAndArchivesAsItGoes) {
  const TempDir folder;
  Config config = replay_config(folder / "");
  config.rotate_interval = minutes(1);
  Logged logged;
  std::filesystem::path first;
  {
    Service service = open(config, logged.logger);
    first = service.ingest().path();
    replay_all(service, logged.logger);
    ASSERT_TRUE(service.step(kStart + minutes(1), logged.logger).has_value());
    EXPECT_NE(service.ingest().path(), first);
    ASSERT_TRUE(service.close(logged.logger).has_value());
  }
  EXPECT_TRUE(ics::store::is_archived(first));
}

TEST(Service, ArchivesWhatAnEarlierRunLeft) {
  const TempDir folder;
  const std::filesystem::path left = folder / ics::store::segment_name(kStart - minutes(5));
  {
    ics::store::SegmentWriter writer = ics::store::SegmentWriter::open(folder / "", kStart - minutes(5)).value();
    ASSERT_TRUE(writer.append(ics::v1::PliRecord{}).has_value());
    ASSERT_TRUE(writer.close().has_value());
    std::ofstream(left, std::ios::app) << "torn";
  }
  const std::filesystem::path unusable = folder / ics::store::segment_name(kStart - minutes(4));
  std::ofstream(unusable) << "not a segment";
  const std::filesystem::path archived = folder / ics::store::segment_name(kStart - minutes(6));
  {
    ics::store::SegmentWriter writer = ics::store::SegmentWriter::open(folder / "", kStart - minutes(6)).value();
    ASSERT_TRUE(writer.close().has_value());
    ASSERT_TRUE(ics::store::archive_segment(archived).has_value());
  }
  Logged logged;
  {
    Service service = open(replay_config(folder / ""), logged.logger);
    ASSERT_TRUE(service.close(logged.logger).has_value());
  }
  EXPECT_TRUE(ics::store::is_archived(left));
  EXPECT_FALSE(ics::store::is_archived(unusable));
  EXPECT_TRUE(logged.has(R"("event":"segment_recovered")"));
  EXPECT_TRUE(logged.has(R"("event":"segment_unusable")"));
  EXPECT_FALSE(logged.has(archived.native()));
}

TEST(Service, ReadsAFileInBoundedBatches) {
  // More packets than one step reads: none of them MAVLink.
  const TempDir folder;
  const std::filesystem::path pcap = folder / "many.pcap";
  {
    std::ofstream out(pcap, std::ios::binary);
    const auto header = ics::capture::encode_file_header({.snaplen = 65535, .linktype = 1});
    out.write(reinterpret_cast<const char*>(header.data()), header.size());
    const ics::capture::testing::Bytes frame =
        ics::capture::testing::ethernet(ics::capture::testing::ipv4_udp({std::byte{0}}));
    for (int index = 0; index < (ics::plid::kBatch * ics::plid::kMaxBatches) + 10; ++index) {
      const auto record = ics::capture::encode_record_header(kStart, static_cast<std::uint32_t>(frame.size()),
                                                             static_cast<std::uint32_t>(frame.size()))
                              .value();
      out.write(reinterpret_cast<const char*>(record.data()), record.size());
      out.write(reinterpret_cast<const char*>(frame.data()), static_cast<std::streamsize>(frame.size()));
    }
  }
  Config config = replay_config(folder / "");
  config.files = {pcap};
  Logged logged;
  Service service = open(config, logged.logger);
  ASSERT_TRUE(service.step(kStart, logged.logger).has_value());
  EXPECT_FALSE(service.replayed());
  EXPECT_EQ(service.feeds().router.counts().packets,
            static_cast<std::uint64_t>(ics::plid::kBatch * ics::plid::kMaxBatches));
  replay_all(service, logged.logger);
  ASSERT_TRUE(service.close(logged.logger).has_value());
}

TEST(Service, StoresWhatALiveTapPortCarries) {
  // Live capture needs CAP_NET_RAW, which the tests have as root in the
  // ics-cpp container.
  const TempDir folder;
  Config config = replay_config(folder / "");
  config.files.clear();
  config.interfaces = {"lo"};
  config.feeds = {"mavlink", "cot"};
  config.cot = ics::plid::CotConfig{
      .port = 46969, .roles = {}, .ce_probability = 0.9, .le_probability = 0.9, .max_skew = std::chrono::hours(1)};
  Logged logged;
  Service service = open(config, logged.logger);
  EXPECT_EQ(service.descriptors().size(), 1U);
  const std::string atak = R"(<event version="2.0" uid="ANDROID-1" type="a-f-G" how="m-g" time="2026-09-21T14:13:20Z" )"
                           R"(start="2026-09-21T14:13:20Z" stale="2026-09-21T14:19:20Z">)"
                           R"(<point lat="-35.36" lon="149.16" hae="600" ce="5" le="5"/></event>)";
  const ics::timing::Fd sender(::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(46969);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  for (int step = 0; step < 200 && service.ingest().stored() == 0; ++step) {
    ::sendto(sender.get(), atak.data(), atak.size(), 0, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    ASSERT_TRUE(service.step(ics::logging::system_now(), logged.logger).has_value());
  }
  EXPECT_GT(service.ingest().stored(), 0U);
  EXPECT_FALSE(service.replayed());
  ASSERT_TRUE(service.close(logged.logger).has_value());
}

TEST(Service, StoresSapientDetections) {
  const TempDir folder;
  const ics::plid::testing::TcpListener listener;
  Config config = replay_config(folder / "");
  config.files.clear();
  config.mavlink.reset();
  config.feeds = {"sapient"};
  config.sapient = ics::plid::SapientConfig{
      .address = "127.0.0.1", .port = listener.port(), .roles = {"A=target"}, .max_skew = std::chrono::hours(1)};
  Logged logged;
  Service service = open(config, logged.logger);
  ASSERT_TRUE(service.step(kStart, logged.logger).has_value());
  ASSERT_EQ(service.descriptors().size(), 1U);
  const ics::timing::Fd middleware = listener.accept();
  ics::sapient::Message detection;
  detection.set_node_id("node");
  detection.mutable_detection_report()->set_object_id("A");
  sapient_msg::bsi_flex_335_v2_0::DetectionReport* report = detection.mutable_detection_report();
  report->mutable_location()->set_x(149.16);
  report->mutable_location()->set_y(-35.36);
  report->mutable_location()->set_coordinate_system(
      sapient_msg::bsi_flex_335_v2_0::LOCATION_COORDINATE_SYSTEM_LAT_LNG_DEG_M);
  report->mutable_location()->set_datum(sapient_msg::bsi_flex_335_v2_0::LOCATION_DATUM_WGS84_E);
  const std::vector<std::byte> bytes = ics::sapient::testing::frame(detection);
  ASSERT_EQ(::send(middleware.get(), bytes.data(), bytes.size(), MSG_NOSIGNAL), static_cast<ssize_t>(bytes.size()));
  for (int step = 0; step < 200 && service.ingest().stored() == 0; ++step) {
    ASSERT_TRUE(service.step(kStart, logged.logger).has_value());
  }
  EXPECT_EQ(service.ingest().stored(), 1U);
  ASSERT_TRUE(service.close(logged.logger).has_value());
}

TEST(Service, StartsALatticeFeed) {
  const TempDir folder;
  const std::filesystem::path token = folder / "token";
  std::ofstream(token) << "environment-token\n";
  ::chmod(token.c_str(), S_IRUSR | S_IWUSR);
  Config config = replay_config(folder / "");
  config.feeds = {"mavlink", "lattice"};
  // Nothing listens on port 1, so the feed keeps trying until the service ends.
  config.lattice = ics::plid::LatticeConfig{.url = "http://127.0.0.1:1",
                                            .token_file = token,
                                            .sandbox_token_file = token,
                                            .roles = {"E-1=target"},
                                            .max_skew = std::chrono::seconds(30)};
  Logged logged;
  Service service = open(config, logged.logger);
  ASSERT_NE(service.feeds().lattice, nullptr);
  replay_all(service, logged.logger);
  ASSERT_TRUE(service.close(logged.logger).has_value());
}

TEST(Service, StopsWhenItCannotStore) {
  const TempDir folder;
  const Config config = replay_config(folder / "");
  Logged logged;
  Service service = open(config, logged.logger);
  {
    // The segment holds its magic; nothing more fits.
    const ics::capture::testing::FileSizeLimit limit(8);
    EXPECT_EQ(service.step(kStart, logged.logger).error(), ics::Error::kUnwritable);
    EXPECT_EQ(service.close(logged.logger).error(), ics::Error::kUnwritable);
  }
  EXPECT_TRUE(logged.has(R"("event":"store_failed","error":"unwritable")"));
}

// The reason Service::open gives for config.
std::string refusal(const Config& config, const ics::Error expected) {
  Logged logged;
  std::string reason;
  const ics::Result<Service> service = Service::open(config, geoid(), kStart, logged.logger, reason);
  EXPECT_FALSE(service.has_value());
  EXPECT_EQ(service.error(), expected) << reason;
  return reason;
}

TEST(Service, ExplainsWhyItCannotStart) {
  const TempDir folder;
  const Config good = replay_config(folder / "");
  Config both = good;
  both.interfaces = {"lo"};
  EXPECT_NE(refusal(both, ics::Error::kInvalidArgument).find("not both"), std::string::npos);
  Config bad_role = good;
  bad_role.mavlink->roles = {"256=target"};
  EXPECT_NE(refusal(bad_role, ics::Error::kInvalidArgument).find("256"), std::string::npos);
  for (const char* id : {"x=target", "1x=target", "=target"}) {
    Config role = good;
    role.mavlink->roles = {id};
    EXPECT_FALSE(refusal(role, ics::Error::kInvalidArgument).empty()) << id;
  }
  Config missing_file = good;
  missing_file.files = {folder / "missing.pcap"};
  EXPECT_FALSE(refusal(missing_file, ics::Error::kUnreadable).empty());
  Config missing_interface = good;
  missing_interface.files.clear();
  missing_interface.interfaces = {"ics-missing0"};
  EXPECT_FALSE(refusal(missing_interface, ics::Error::kUnavailable).empty());
  Config no_store = good;
  no_store.store_folder = folder / "missing";
  EXPECT_NE(refusal(no_store, ics::Error::kUnwritable).find("cannot open a segment"), std::string::npos);
  Config no_socket = good;
  no_socket.query_socket = folder / "missing" / "query";
  EXPECT_NE(refusal(no_socket, ics::Error::kUnavailable).find("cannot listen"), std::string::npos);
}

TEST(Service, ExplainsAFeedItCannotOpen) {
  const TempDir folder;
  Config good = replay_config(folder / "");
  Config cot = good;
  cot.feeds = {"mavlink", "cot"};
  cot.cot = ics::plid::CotConfig{
      .port = 6969, .roles = {"uid"}, .ce_probability = 0.9, .le_probability = 0.9, .max_skew = std::chrono::seconds(30)};
  EXPECT_NE(refusal(cot, ics::Error::kInvalidArgument).find("uid"), std::string::npos);
  Config sapient = good;
  sapient.feeds = {"mavlink", "sapient"};
  sapient.sapient = ics::plid::SapientConfig{
      .address = "localhost", .port = 5020, .roles = {}, .max_skew = std::chrono::seconds(30)};
  EXPECT_NE(refusal(sapient, ics::Error::kInvalidArgument).find("localhost"), std::string::npos);
  Config sapient_role = sapient;
  sapient_role.sapient->roles = {"bad"};
  EXPECT_NE(refusal(sapient_role, ics::Error::kInvalidArgument).find("bad"), std::string::npos);
  const std::filesystem::path token = folder / "token";
  std::ofstream(token) << "environment-token\n";
  ::chmod(token.c_str(), S_IRUSR | S_IWUSR);
  Config lattice = good;
  lattice.feeds = {"mavlink", "lattice"};
  lattice.lattice = ics::plid::LatticeConfig{.url = "http://127.0.0.1:1",
                                             .token_file = folder / "missing",
                                             .sandbox_token_file = std::nullopt,
                                             .roles = {},
                                             .max_skew = std::chrono::seconds(30)};
  EXPECT_NE(refusal(lattice, ics::Error::kUnreadable).find("token file"), std::string::npos);
  lattice.lattice->sandbox_token_file = folder / "missing";
  EXPECT_NE(refusal(lattice, ics::Error::kUnreadable).find("token file"), std::string::npos);
  lattice.lattice->sandbox_token_file.reset();
  lattice.lattice->token_file = token;
  lattice.lattice->url = "ftp://lattice.example.com";
  EXPECT_NE(refusal(lattice, ics::Error::kInvalidArgument).find("url"), std::string::npos);
  lattice.lattice->roles = {"bad"};
  EXPECT_NE(refusal(lattice, ics::Error::kInvalidArgument).find("bad"), std::string::npos);
}

}  // namespace
