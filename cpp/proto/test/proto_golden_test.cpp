// The C++ code generated from proto/ reads and writes every ICS message exactly
// as the Python reference does (ICS-012). Each golden file from
// python/ics_golden/proto.py must parse, hold no field this code does not
// know, and serialize back to the same bytes.
#include "ics/v1/camera_frame_meta.pb.h"
#include "ics/v1/footprint.pb.h"
#include "ics/v1/fragment.pb.h"
#include "ics/v1/kill_assessment.pb.h"
#include "ics/v1/mount_sample.pb.h"
#include "ics/v1/pli.pb.h"
#include "ics/v1/pli_query.pb.h"
#include "ics/v1/run_record.pb.h"
#include "ics/v1/time_quality.pb.h"
#include "ics/v1/time_quality_service.pb.h"
#include "ics/v1/track.pb.h"
#include "ics/v1/trigger_event.pb.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>

#include <gtest/gtest.h>

namespace {

constexpr std::int64_t kTimeUtcNs = 1'790'000'000'123'456'789;

// Set by cpp/proto/test/CMakeLists.txt: the repository's golden/proto folder.
std::filesystem::path golden_dir() {
  return std::filesystem::path{ICS_PROTO_GOLDEN_DIR};
}

std::string read_golden(const std::string& name) {
  std::ifstream file{golden_dir() / (name + ".binpb"), std::ios::binary};
  return std::string(std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{});
}

// Parses the golden file, drops any field this code does not know, and
// requires the same bytes back: an unknown field would change them.
template <typename Message>
void expect_same_bytes(const std::string& name) {
  const std::string golden = read_golden(name);
  ASSERT_FALSE(golden.empty()) << name << ".binpb is missing or empty";
  Message message;
  ASSERT_TRUE(message.ParseFromString(golden)) << name;
  message.DiscardUnknownFields();
  EXPECT_EQ(message.SerializeAsString(), golden) << name;
}

TEST(ProtoGolden, CameraFrameMeta) { expect_same_bytes<ics::v1::CameraFrameMeta>("camera_frame_meta"); }
TEST(ProtoGolden, Footprint) { expect_same_bytes<ics::v1::Footprint>("footprint"); }
TEST(ProtoGolden, Fragment) { expect_same_bytes<ics::v1::Fragment>("fragment"); }
TEST(ProtoGolden, KillAssessment) { expect_same_bytes<ics::v1::KillAssessment>("kill_assessment"); }
TEST(ProtoGolden, MountSample) { expect_same_bytes<ics::v1::MountSample>("mount_sample"); }
TEST(ProtoGolden, PliEvent) { expect_same_bytes<ics::v1::PliEvent>("pli_event"); }
TEST(ProtoGolden, PliRecord) { expect_same_bytes<ics::v1::PliRecord>("pli_record"); }
TEST(ProtoGolden, QueryPliRequest) { expect_same_bytes<ics::v1::QueryPliRequest>("query_pli_request"); }
TEST(ProtoGolden, QueryPliResponse) { expect_same_bytes<ics::v1::QueryPliResponse>("query_pli_response"); }
TEST(ProtoGolden, RunRecord) { expect_same_bytes<ics::v1::RunRecord>("run_record"); }
TEST(ProtoGolden, TimeQuality) { expect_same_bytes<ics::v1::TimeQuality>("time_quality"); }
TEST(ProtoGolden, Track) { expect_same_bytes<ics::v1::Track>("track"); }
TEST(ProtoGolden, TriggerEvent) { expect_same_bytes<ics::v1::TriggerEvent>("trigger_event"); }
TEST(ProtoGolden, WatchTimeQualityResponse) {
  expect_same_bytes<ics::v1::WatchTimeQualityResponse>("watch_time_quality_response");
}

TEST(ProtoGolden, HasATestForEveryGoldenFile) {
  const std::set<std::string> tested{
      "camera_frame_meta", "footprint",          "fragment",   "kill_assessment", "mount_sample",
      "pli_event",         "pli_record",         "query_pli_request", "query_pli_response", "run_record",
      "time_quality",      "track",              "trigger_event",
      "watch_time_quality_response",
  };
  std::set<std::string> found;
  for (const auto& entry : std::filesystem::directory_iterator{golden_dir()}) {
    found.insert(entry.path().stem().string());
  }
  EXPECT_EQ(found, tested);
}

TEST(ProtoMessages, KeepNanosecondTimesExact) {
  ics::v1::TriggerEvent event;
  event.set_time_utc_ns(kTimeUtcNs);
  event.add_channels("a");
  ics::v1::TriggerEvent parsed;
  ASSERT_TRUE(parsed.ParseFromString(event.SerializeAsString()));
  EXPECT_EQ(parsed.time_utc_ns(), kTimeUtcNs);
  ASSERT_EQ(parsed.channels_size(), 1);
}

TEST(ProtoMessages, KeepThePresenceOfOptionalFieldsEvenAtZero) {
  ics::v1::PliRecord record;
  record.set_horizontal_sigma_m(0.0);
  ics::v1::PliRecord parsed;
  ASSERT_TRUE(parsed.ParseFromString(record.SerializeAsString()));
  EXPECT_TRUE(parsed.has_horizontal_sigma_m());
  EXPECT_FALSE(parsed.has_vertical_sigma_m());
  EXPECT_FALSE(parsed.has_received_utc_ns());
}

TEST(ProtoMessages, ReadAnEmptyMessageAsDefaults) {
  ics::v1::RunRecord parsed;
  ASSERT_TRUE(parsed.ParseFromString(std::string{}));
  EXPECT_FALSE(parsed.has_kill_assessment());
  EXPECT_EQ(parsed.flags_size(), 0);
  EXPECT_EQ(ics::v1::KillAssessment{}.computed_class(), ics::v1::KillAssessment::KILL_CLASS_UNSPECIFIED);
}

}  // namespace
