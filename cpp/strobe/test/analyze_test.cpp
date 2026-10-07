#include "ics/strobe/analyze.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include <gtest/gtest.h>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/strobe/detect.hpp"
#include "ics/strobe/fit.hpp"
#include "ics/timing/camera_offsets.hpp"
#include "ics/v1/time_quality.pb.h"
#include "scratch_dir.hpp"
#include "strobe_rig.hpp"

namespace ics::strobe {
namespace {

using camera::testing_support::ScratchDir;
using std::chrono::microseconds;

constexpr std::int64_t kNsPerSecond = 1'000'000'000;

// ICS-029's "Done when": offsets within 5 µs on the bench, here the emulated
// one.
TEST(AnalyzeFolder, FindsThePhantomsOffsetWithin5Microseconds) {
  const ScratchDir dir;
  const testing::Calibration c = testing::phantom();
  ASSERT_TRUE(testing::record_phantom(dir.path(), c).has_value());
  const Result<StrobeReport> report = analyze_folder(dir.path(), testing::settings_for(c));
  ASSERT_TRUE(report.has_value());
  EXPECT_NEAR(static_cast<double>(report->offset.offset_ns()), 37'400.0, 5'000.0);
  EXPECT_NEAR(static_cast<double>(report->offset.offset_ns()), 37'400.0, 500.0);
  EXPECT_LT(report->offset.offset_sigma_ns(), 5'000);
  EXPECT_EQ(report->offset.camera_id(), "camera-1");
  EXPECT_EQ(report->segments, 40U);
  EXPECT_EQ(report->lit + report->unlit + report->scattered, 40U);
  EXPECT_GT(report->lit, 30U);
  EXPECT_EQ(report->scattered, 0U);
  EXPECT_EQ(report->offset.measured_utc_ns(), (testing::kSweepStart + 39) * kNsPerSecond);
}

TEST(AnalyzeFolder, FindsTheX6980sOffsetWithin5Microseconds) {
  const ScratchDir dir;
  const testing::Calibration c = testing::x6980();
  ASSERT_TRUE(testing::record_x6980(dir.path(), c).has_value());
  const Result<StrobeReport> report = analyze_folder(dir.path(), testing::settings_for(c));
  ASSERT_TRUE(report.has_value());
  // Stamps truncated to the microsecond read about 0.5 µs early.
  EXPECT_NEAR(static_cast<double>(report->offset.offset_ns()), -112'600.0, 5'000.0);
  EXPECT_NEAR(static_cast<double>(report->offset.offset_ns()), -113'100.0, 1'000.0);
  EXPECT_LT(report->offset.offset_sigma_ns(), 5'000);
  EXPECT_GT(report->fit.edge_frames, 0U);
}

TEST(AnalyzeFolder, RefusesAFolderItCannotUse) {
  const ScratchDir dir;
  const StrobeSettings settings = testing::settings_for(testing::phantom());
  EXPECT_EQ(analyze_folder(dir.path(), settings).error(), Error::kEmpty);
  EXPECT_EQ(analyze_folder(dir.path() / "missing", settings).error(), Error::kUnreadable);
  std::ofstream(dir.path() / "notes.txt") << "not a cine";
  EXPECT_EQ(analyze_folder(dir.path(), settings).error(), Error::kEmpty);
  std::ofstream(dir.path() / "bad.cine") << "not a cine";
  EXPECT_EQ(analyze_folder(dir.path(), settings).error(), Error::kMalformed);
  // A folder named like a cine cannot be mapped; it sorts first.
  std::filesystem::create_directory(dir.path() / "a.cine");
  EXPECT_EQ(analyze_folder(dir.path(), settings).error(), Error::kUnreadable);
}

// Segments of four frames around one second: lit as given.
StrobeSegment segment(const std::int64_t second, const std::vector<double>& brightness, const std::size_t lit,
                      const bool contiguous) {
  StrobeSegment out{.second = second, .frames = {}, .lit = lit, .contiguous = contiguous};
  for (std::size_t i = 0; i < brightness.size(); ++i) {
    out.frames.push_back(StrobeFrame{.stamp = UtcTime(std::chrono::seconds(second)) + microseconds(200 * static_cast<std::int64_t>(i)),
                                     .exposure = microseconds(150),
                                     .brightness = brightness[i]});
  }
  return out;
}

TEST(Analyze, FitsOnlyTheSegmentsLitOnceAndContiguously) {
  const StrobeSettings settings = testing::settings_for(testing::phantom());
  const std::vector<StrobeSegment> none{segment(10, {200, 200, 200, 200}, 0, true),
                                        segment(11, {900, 200, 200, 900}, 2, false)};
  EXPECT_EQ(analyze(none, settings).error(), Error::kEmpty);
  // Lit, but in frames that pin nothing: the fit's failure passes through.
  const std::vector<StrobeSegment> flat{segment(12, {200, 200, 200, 200}, 1, true), none[0], none[1]};
  EXPECT_EQ(analyze(flat, settings).error(), Error::kUnconstrained);
}

v1::TimeQuality::CameraOffset offset(const std::string& camera, const std::int64_t ns) {
  v1::TimeQuality::CameraOffset out;
  out.set_camera_id(camera);
  out.set_offset_ns(ns);
  out.set_offset_sigma_ns(40);
  out.set_measured_utc_ns(1);
  return out;
}

TEST(Publish, PutsTheOffsetInTheStationsFile) {
  const ScratchDir dir;
  const std::filesystem::path file = dir.path() / "offsets.binpb";
  ASSERT_TRUE(publish(offset("phantom-1", 37'400), "station-1", file).has_value());
  ASSERT_TRUE(publish(offset("x6980-1", -112'600), "station-1", file).has_value());
  ASSERT_TRUE(publish(offset("phantom-1", 37'500), "station-1", file).has_value());
  const Result<timing::CameraOffsets> read = timing::read_camera_offsets(file);
  ASSERT_TRUE(read.has_value());
  EXPECT_EQ(read->station_id, "station-1");
  ASSERT_EQ(read->offsets.size(), 2U);
  EXPECT_EQ(read->offsets[0].offset_ns(), 37'500);
  EXPECT_EQ(publish(offset("phantom-1", 1), "station-2", file).error(), Error::kInvalidArgument);
  std::ofstream(dir.path() / "bad.binpb") << "not offsets";
  EXPECT_EQ(publish(offset("phantom-1", 1), "station-1", dir.path() / "bad.binpb").error(), Error::kMalformed);
  // A missing folder reads as no file yet, and cannot be written.
  EXPECT_EQ(publish(offset("phantom-1", 1), "station-1", dir.path() / "missing" / "o.binpb").error(),
            Error::kUnwritable);
  // A folder in place of the file cannot be read.
  std::filesystem::create_directory(dir.path() / "folder.binpb");
  EXPECT_EQ(publish(offset("phantom-1", 1), "station-1", dir.path() / "folder.binpb").error(), Error::kUnreadable);
}

}  // namespace
}  // namespace ics::strobe
