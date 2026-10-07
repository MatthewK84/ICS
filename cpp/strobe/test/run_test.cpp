#include "ics/strobe/run.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "ics/common/error.hpp"
#include "ics/logging/logger.hpp"
#include "ics/strobe/config.hpp"
#include "ics/timing/camera_offsets.hpp"
#include "scratch_dir.hpp"
#include "strobe_rig.hpp"

namespace ics::strobe {
namespace {

using camera::testing_support::ScratchDir;

// The example config, with the offsets file and the schedule given.
void write_config(const std::filesystem::path& path, const std::filesystem::path& offsets,
                  const std::string& latency_ns) {
  std::ofstream(path) << "[log]\nlevel = \"warn\"\nservice = \"ics-strobe-analyzer\"\n[strobe]\n"
                      << "station_id = \"station-1\"\ncamera_id = \"phantom-1\"\n"
                      << "offsets_file = \"" << offsets.native() << "\"\n"
                      << "roi_x_px = 8\nroi_y_px = 8\nroi_width_px = 8\nroi_height_px = 8\n"
                      << "pulse_width_ns = 20_000\nlatency_ns = " << latency_ns << "\n"
                      << "delay_step_ns = 5_000\nsweep_steps = 40\nmax_offset_ns = 500_000\n";
}

const char* const kCommand[] = {"ics-strobe-analyzer"};
const char* const kWithArgument[] = {"ics-strobe-analyzer", "--help"};

TEST(Run, RefusesArgumentsAndBadConfigFiles) {
  const ScratchDir dir;
  EXPECT_EQ(run(kWithArgument, dir.path() / "strobe.toml", dir.path()), kExitUsage);
  EXPECT_EQ(run(kCommand, dir.path() / "missing.toml", dir.path()), kExitUsage);
  // Pulses that would run past their second.
  write_config(dir.path() / "late.toml", dir.path() / "offsets.binpb", "999_900_000");
  EXPECT_EQ(run(kCommand, dir.path() / "late.toml", dir.path()), kExitUsage);
}

TEST(Run, MeasuresAndPublishesTheOffset) {
  const ScratchDir dir;
  const ScratchDir cines;
  ASSERT_TRUE(testing::record_phantom(cines.path(), testing::phantom()).has_value());
  write_config(dir.path() / "strobe.toml", dir.path() / "offsets.binpb", "1_500");
  EXPECT_EQ(run(kCommand, dir.path() / "strobe.toml", cines.path()), kExitPublished);
  const Result<timing::CameraOffsets> offsets = timing::read_camera_offsets(dir.path() / "offsets.binpb");
  ASSERT_TRUE(offsets.has_value());
  ASSERT_EQ(offsets->offsets.size(), 1U);
  EXPECT_EQ(offsets->offsets[0].camera_id(), "phantom-1");
  EXPECT_NEAR(static_cast<double>(offsets->offsets[0].offset_ns()), 37'400.0, 5'000.0);
}

TEST(Run, LogsWhyItCouldNotMeasureOrPublish) {
  const ScratchDir dir;
  std::ostringstream log;
  const logging::Logger logger = logging::Logger::to_stream("ics-strobe-analyzer", logging::Level::kInfo, log,
                                                            []() noexcept { return ics::UtcTime{}; });
  AnalyzerConfig config;
  config.station_id = "station-1";
  config.strobe = testing::settings_for(testing::phantom());
  config.offsets_file = dir.path() / "missing" / "offsets.binpb";
  EXPECT_EQ(analyze_and_publish(config, dir.path(), logger), kExitFailed);
  EXPECT_NE(log.str().find(R"("event":"analysis_failed","error":"empty")"), std::string::npos);
  ASSERT_TRUE(testing::record_phantom(dir.path(), testing::phantom()).has_value());
  EXPECT_EQ(analyze_and_publish(config, dir.path(), logger), kExitFailed);
  EXPECT_NE(log.str().find(R"("event":"publish_failed","error":"unwritable")"), std::string::npos);
  config.offsets_file = dir.path() / "offsets.binpb";
  EXPECT_EQ(analyze_and_publish(config, dir.path(), logger), kExitPublished);
  EXPECT_NE(log.str().find(R"("event":"camera_offset","camera_id":"camera-1")"), std::string::npos);
}

}  // namespace
}  // namespace ics::strobe
