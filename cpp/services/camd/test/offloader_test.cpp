#include "ics/camd/offloader.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "camd_support.hpp"
#include "support.hpp"

namespace {

using ics::camd::CameraOutcome;
using ics::camd::testing::FixedQuality;
using ics::camd::testing::read_meta;
using ics::timing::testing::TempDir;

std::vector<ics::v1::CameraFrameMeta> frames(const std::size_t count) {
  std::vector<ics::v1::CameraFrameMeta> out(count);
  for (std::size_t i = 0; i < count; ++i) {
    out[i].set_station_id("station-1");
    out[i].set_frame_index(i);
  }
  return out;
}

// Each camera's cine and metadata files in folder, as offload_all lists them.
std::vector<std::filesystem::path> segment_files(const std::filesystem::path& folder) {
  return {folder / "cine-000.cine", folder / "cine-000.meta", folder / "cine-001.cine", folder / "cine-001.meta"};
}

std::vector<CameraOutcome> offload(const ics::camd::Config& config, const std::filesystem::path& run,
                                   const ics::v1::TimeQuality& quality) {
  FixedQuality fixed(quality);
  return ics::camd::offload_all(config, run, fixed, ics::camd::testing::station_now());
}

TEST(CamdOffloader, NamesEachSegmentsMetadataFile) {
  EXPECT_EQ(ics::camd::meta_path("run/a/cine-001.cine"), "run/a/cine-001.meta");
}

TEST(CamdOffloader, WritesMetadataItReadsBack) {
  const TempDir dir;
  ASSERT_TRUE(ics::camd::write_meta(frames(3), dir / "a.meta").has_value());
  const std::vector<ics::v1::CameraFrameMeta> read = read_meta(dir / "a.meta");
  ASSERT_EQ(read.size(), 3U);
  EXPECT_EQ(read[2].frame_index(), 2U);
  EXPECT_EQ(read[2].station_id(), "station-1");
  ASSERT_TRUE(ics::camd::write_meta({}, dir / "a.meta").has_value());
  EXPECT_TRUE(read_meta(dir / "a.meta").empty());
}

TEST(CamdOffloader, ReportsMetadataItCannotWrite) {
  const TempDir dir;
  // A folder where the file would go: it cannot be opened.
  EXPECT_EQ(ics::camd::write_meta(frames(1), dir / "").error(), ics::Error::kUnwritable);
  EXPECT_EQ(ics::camd::write_meta({}, dir / "").error(), ics::Error::kUnwritable);
  // A full device: many frames fail as they are written, one as it is
  // flushed.
  EXPECT_EQ(ics::camd::write_meta(frames(1000), "/dev/full").error(), ics::Error::kUnwritable);
  EXPECT_EQ(ics::camd::write_meta(frames(1), "/dev/full").error(), ics::Error::kUnwritable);
}

TEST(CamdOffloader, OffloadsEveryCameraAtOnce) {
  const TempDir dir;
  const ics::camd::Config config = ics::camd::testing::test_config(dir / "");
  const std::vector<CameraOutcome> outcomes = offload(config, dir / "", ics::camd::testing::locked_report());
  ASSERT_EQ(outcomes.size(), 2U);
  for (std::size_t i = 0; i < outcomes.size(); ++i) {
    const CameraOutcome& outcome = outcomes[i];
    EXPECT_EQ(outcome.camera_id, config.cameras[i].id);
    EXPECT_FALSE(outcome.error.has_value());
    EXPECT_TRUE(outcome.report.verified);
    EXPECT_EQ(outcome.files, segment_files(dir / outcome.camera_id));
    const std::vector<ics::v1::CameraFrameMeta> meta = read_meta(dir / outcome.camera_id / "cine-001.meta");
    ASSERT_EQ(meta.size(), 300U);
    EXPECT_EQ(meta.front().station_id(), "station-1");
    EXPECT_EQ(meta.front().camera_id(), outcome.camera_id);
    EXPECT_EQ(meta.front().segment_id(), "cine-001");
    EXPECT_EQ(meta.back().frame_index(), 299U);
    EXPECT_EQ(meta.front().time_source(), ics::v1::CameraFrameMeta::TIME_SOURCE_IRIG);
  }
  EXPECT_EQ(read_meta(dir / "north-mwir" / "cine-000.meta").front().camera_kind(),
            ics::v1::CameraFrameMeta::CAMERA_KIND_MWIR);
}

TEST(CamdOffloader, RecordsWithoutTimeQualityButDoesNotVerify) {
  const TempDir dir;
  const std::vector<CameraOutcome> outcomes =
      offload(ics::camd::testing::test_config(dir / ""), dir / "", ics::v1::TimeQuality());
  ASSERT_EQ(outcomes.size(), 2U);
  // The Phantom records, but its frames are not IRIG-timed.
  EXPECT_FALSE(outcomes[0].error.has_value());
  EXPECT_FALSE(outcomes[0].report.verified);
  EXPECT_EQ(outcomes[0].files.size(), 4U);
  // The X6980 stamps no year, and the station's UTC is not known.
  EXPECT_EQ(outcomes[1].error, ics::Error::kUnavailable);
  EXPECT_TRUE(outcomes[1].files.empty());
}

TEST(CamdOffloader, ReportsAFolderItCannotMake) {
  const TempDir dir;
  std::ofstream(dir / "run") << "a file, not a folder";
  const std::vector<CameraOutcome> outcomes =
      offload(ics::camd::testing::test_config(dir / ""), dir / "run", ics::camd::testing::locked_report());
  ASSERT_EQ(outcomes.size(), 2U);
  for (const CameraOutcome& outcome : outcomes) {
    EXPECT_EQ(outcome.error, ics::Error::kUnwritable);
    EXPECT_TRUE(outcome.files.empty());
  }
}

TEST(CamdOffloader, ReportsAMetadataFileItCannotWrite) {
  const TempDir dir;
  std::filesystem::create_directories(dir / "north-hs" / "cine-000.meta");
  const std::vector<CameraOutcome> outcomes =
      offload(ics::camd::testing::test_config(dir / ""), dir / "", ics::camd::testing::locked_report());
  ASSERT_EQ(outcomes.size(), 2U);
  EXPECT_EQ(outcomes[0].error, ics::Error::kUnwritable);
  EXPECT_EQ(outcomes[0].files, std::vector<std::filesystem::path>{dir / "north-hs" / "cine-000.cine"});
  EXPECT_FALSE(outcomes[1].error.has_value());
}

}  // namespace
