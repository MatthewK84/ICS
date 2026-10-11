#include "ics/camd/config.hpp"

#include <chrono>
#include <string>
#include <string_view>
#include <variant>

#include <gtest/gtest.h>

#include "camd_support.hpp"
#include "ics/config/config.hpp"
#include "ics/config/reader.hpp"

namespace {

using ics::camd::Config;
using ics::camd::EmulatedPhantomDriver;
using ics::camd::EmulatedX6980Driver;
using ics::camd::folder_name;

// The settings of one camera table, without its driver.
constexpr std::string_view kCamera = R"(
width_px = 64
height_px = 32
window_x_px = 0
window_y_px = 0
frame_rate_hz = 5_000
exposure_ns = 150_000
segment_frames = 500
post_trigger_frames = 400
irig = true
segments = 2
first_frame = -50
frame_count = 300
)";

// The errors in reading text, formatted; empty when it reads.
std::string errors_in(const std::string& text) {
  const auto table = ics::config::parse(text);
  EXPECT_TRUE(table.has_value());
  const auto config = ics::config::read(*table, &ics::camd::read_config);
  return config ? std::string() : ics::config::format_errors("ics-camd.toml", config.error());
}

// A whole config with one camera, a, whose driver table is driver.
std::string one_camera(const std::string_view driver) {
  return std::string(R"([log]
level = "info"
service = "ics-camd"
[camd]
station_id = "station-1"
command_socket = "/run/ics-camd/command"
time_socket = "/run/ics-timingd/time-quality"
offload_folder = "/var/lib/ics/cameras"
time_wait_ns = 0
cameras = ["a"]
[camera.a])") + std::string(kCamera) + "[camera.a.driver]\n" + std::string(driver);
}

TEST(CamdConfig, ReadsTheExample) {
  const auto config = ics::config::read_file(ICS_CAMD_EXAMPLE, &ics::camd::read_config);
  ASSERT_TRUE(config.has_value()) << ics::config::format_errors("example", config.error());
  std::string reason;
  EXPECT_TRUE(ics::camd::validate(*config, reason).has_value()) << reason;
  EXPECT_EQ(config->station_id, "station-1");
  EXPECT_EQ(config->command_socket, "/run/ics-camd/command");
  EXPECT_EQ(config->time_socket, "/run/ics-timingd/time-quality");
  EXPECT_EQ(config->offload_folder, "/var/lib/ics/cameras");
  EXPECT_EQ(config->time_wait, std::chrono::seconds(2));
  ASSERT_EQ(config->cameras.size(), 2U);
  const ics::camd::CameraConfig& hs = config->cameras[0];
  EXPECT_EQ(hs.id, "north-hs");
  EXPECT_EQ(hs.settings.frame_rate, 5000U);
  EXPECT_EQ(hs.settings.exposure, std::chrono::microseconds(150));
  EXPECT_TRUE(hs.settings.irig);
  EXPECT_EQ(hs.segments, 2U);
  EXPECT_EQ(hs.frames.first, -50);
  EXPECT_EQ(hs.frames.count, 300U);
  ASSERT_TRUE(std::holds_alternative<EmulatedPhantomDriver>(hs.driver));
  EXPECT_EQ(std::get<EmulatedPhantomDriver>(hs.driver).trigger_interval, std::chrono::milliseconds(120));
  const ics::camd::CameraConfig& mwir = config->cameras[1];
  EXPECT_EQ(mwir.id, "north-mwir");
  EXPECT_EQ(mwir.settings.window_x, 288U);
  EXPECT_EQ(mwir.settings.window_y, 240U);
  ASSERT_TRUE(std::holds_alternative<EmulatedX6980Driver>(mwir.driver));
  EXPECT_EQ(std::get<EmulatedX6980Driver>(mwir.driver).trigger_interval, std::chrono::milliseconds(600));
  EXPECT_FALSE(std::get<EmulatedX6980Driver>(mwir.driver).stamps_year);
}

TEST(CamdConfig, ReadsEachDriver) {
  EXPECT_EQ(errors_in(one_camera("name = \"emulated-phantom\"\ntrigger_interval_ns = 1_000_000\n")), "");
  EXPECT_EQ(errors_in(one_camera("name = \"emulated-x6980\"\ntrigger_interval_ns = 60_000_000_000\n"
                                 "stamps_year = true\n")),
            "");
}

TEST(CamdConfig, NamesAnUnknownDriver) {
  const std::string errors = errors_in(one_camera("name = \"phantom-sdk\"\ntrigger_interval_ns = 1_000_000\n"));
  EXPECT_NE(errors.find("camera.a.driver.name"), std::string::npos) << errors;
}

TEST(CamdConfig, NamesAnotherDriversSetting) {
  const std::string errors =
      errors_in(one_camera("name = \"emulated-phantom\"\ntrigger_interval_ns = 1_000_000\nstamps_year = true\n"));
  EXPECT_NE(errors.find("camera.a.driver.stamps_year"), std::string::npos) << errors;
}

TEST(CamdConfig, NamesEachBadCameraSetting) {
  std::string text = one_camera("name = \"emulated-phantom\"\ntrigger_interval_ns = 0\n");
  text.replace(text.find("frame_rate_hz = 5_000"), 21, "frame_rate_hz = 0");
  const std::string errors = errors_in(text);
  EXPECT_NE(errors.find("camera.a.frame_rate_hz"), std::string::npos) << errors;
  EXPECT_NE(errors.find("camera.a.driver.trigger_interval_ns"), std::string::npos) << errors;
}

TEST(CamdConfig, NamesACameraWithoutATable) {
  std::string text = one_camera("name = \"emulated-phantom\"\ntrigger_interval_ns = 1_000_000\n");
  text.replace(text.find("cameras = [\"a\"]"), 15, "cameras = [\"a\", \"b\"]");
  const std::string errors = errors_in(text);
  EXPECT_NE(errors.find("camera.b"), std::string::npos) << errors;
}

TEST(CamdConfig, NamesAFolderOfItsOwn) {
  EXPECT_TRUE(folder_name("run-1.a_B9"));
  EXPECT_TRUE(folder_name(std::string(64, 'r')));
  EXPECT_FALSE(folder_name(""));
  EXPECT_FALSE(folder_name(std::string(65, 'r')));
  EXPECT_FALSE(folder_name("."));
  EXPECT_FALSE(folder_name(".."));
  EXPECT_FALSE(folder_name("a/b"));
  EXPECT_FALSE(folder_name("a b"));
  EXPECT_FALSE(folder_name("a~b"));
  EXPECT_FALSE(folder_name("caf\xc3\xa9"));
  EXPECT_TRUE(folder_name("..."));
}

TEST(CamdConfig, ValidatesCameraIds) {
  Config config = ics::camd::testing::test_config("/tmp");
  std::string reason;
  EXPECT_TRUE(ics::camd::validate(config, reason).has_value());
  config.cameras[1].id = "north/mwir";
  EXPECT_EQ(ics::camd::validate(config, reason).error(), ics::Error::kInvalidArgument);
  EXPECT_EQ(reason, "camera ID \"north/mwir\" must be 1 to 64 letters, digits, '.', '_' or '-'");
  config.cameras[1].id = "north-hs";
  EXPECT_EQ(ics::camd::validate(config, reason).error(), ics::Error::kInvalidArgument);
  EXPECT_EQ(reason, "camera ID \"north-hs\" is listed twice");
}

}  // namespace
