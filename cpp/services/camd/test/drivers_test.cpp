#include "ics/camd/drivers.hpp"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string_view>
#include <variant>

#include <gtest/gtest.h>

#include "camd_support.hpp"
#include "ics/camera/offload.hpp"
#include "ics/config/config.hpp"
#include "ics/config/reader.hpp"
#include "support.hpp"

namespace {

using ics::camd::DriverSettings;
using ics::camd::EmulatedPhantomDriver;
using ics::camd::EmulatedX6980Driver;
using ics::camd::testing::FixedQuality;
using ics::timing::testing::TempDir;

DriverSettings read(const std::string_view text) {
  const auto table = ics::config::parse(text);
  EXPECT_TRUE(table.has_value());
  const auto driver = ics::config::read(*table, [](ics::config::Reader& root) {
    ics::config::Reader section = root.section("driver");
    return ics::camd::read_driver(section);
  });
  EXPECT_TRUE(driver.has_value());
  return driver.value_or(DriverSettings{});
}

// Records and offloads camera's segments with a camera its driver opens.
ics::Result<ics::camera::OffloadReport> offload(const ics::camd::CameraConfig& camera,
                                                const std::filesystem::path& folder) {
  FixedQuality quality(ics::camd::testing::locked_report());
  const std::unique_ptr<ics::camera::SegmentCamera> opened =
      ics::camd::open_camera(camera.driver, quality, ics::camd::testing::station_now());
  const ics::camera::OffloadPlan plan{.station_id = "station-1",
                                      .camera_id = camera.id,
                                      .camera_kind = ics::camd::camera_kind(camera.driver),
                                      .segments = camera.segments,
                                      .frames = camera.frames,
                                      .directory = folder};
  return ics::camera::record_and_offload(*opened, camera.settings, plan, quality);
}

TEST(CamdDrivers, ReadsEachDriversSettings) {
  const DriverSettings phantom = read("[driver]\nname = \"emulated-phantom\"\ntrigger_interval_ns = 120_000_000\n");
  ASSERT_TRUE(std::holds_alternative<EmulatedPhantomDriver>(phantom));
  EXPECT_EQ(std::get<EmulatedPhantomDriver>(phantom).trigger_interval, std::chrono::milliseconds(120));
  const DriverSettings x6980 =
      read("[driver]\nname = \"emulated-x6980\"\ntrigger_interval_ns = 600_000_000\nstamps_year = true\n");
  ASSERT_TRUE(std::holds_alternative<EmulatedX6980Driver>(x6980));
  EXPECT_EQ(std::get<EmulatedX6980Driver>(x6980).trigger_interval, std::chrono::milliseconds(600));
  EXPECT_TRUE(std::get<EmulatedX6980Driver>(x6980).stamps_year);
}

TEST(CamdDrivers, NamesTheKindOfCameraEachDrives) {
  EXPECT_EQ(ics::camd::camera_kind(EmulatedPhantomDriver{}), ics::v1::CameraFrameMeta::CAMERA_KIND_HIGH_SPEED_VISIBLE);
  EXPECT_EQ(ics::camd::camera_kind(EmulatedX6980Driver{}), ics::v1::CameraFrameMeta::CAMERA_KIND_MWIR);
}

TEST(CamdDrivers, OpensAPhantomThatRecordsAndOffloads) {
  const TempDir dir;
  const auto report = offload(ics::camd::testing::phantom("north-hs"), dir / "");
  ASSERT_TRUE(report.has_value()) << ics::to_string(report.error());
  EXPECT_TRUE(report->verified);
  EXPECT_EQ(report->segments.size(), 2U);
}

TEST(CamdDrivers, OpensAnX6980ThatRecordsAndOffloads) {
  const TempDir dir;
  const auto report = offload(ics::camd::testing::x6980("north-mwir"), dir / "");
  ASSERT_TRUE(report.has_value()) << ics::to_string(report.error());
  EXPECT_TRUE(report->verified);
  ASSERT_EQ(report->segments.size(), 2U);
  EXPECT_EQ(report->segments[0].meta.front().window_x_px(), 288U);
  EXPECT_EQ(report->segments[0].meta.front().camera_kind(), ics::v1::CameraFrameMeta::CAMERA_KIND_MWIR);
}

}  // namespace
