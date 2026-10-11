#include "ics/camd/run.hpp"

#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <pthread.h>
#include <signal.h>

#include "camd_support.hpp"
#include "ics/camd/command_client.hpp"
#include "support.hpp"

namespace {

using ics::camd::Config;
using ics::camd::testing::BlockedSigterm;
using ics::camd::testing::Logged;
using ics::timing::testing::TempDir;

int run(const std::vector<const char*>& args, const std::filesystem::path& config) {
  return ics::camd::run(args, config);
}

// The example config, with its sockets and offload folder in dir.
std::filesystem::path write_config(const TempDir& dir) {
  std::ifstream example(ICS_CAMD_EXAMPLE);
  std::stringstream text;
  text << example.rdbuf();
  std::string config = text.str();
  const auto move_to_dir = [&config, &dir](const std::string& from, const std::string& name) {
    config.replace(config.find(from), from.size(), (dir / name).native());
  };
  move_to_dir("/run/ics-camd/command", "command");
  move_to_dir("/run/ics-timingd/time-quality", "time-quality");
  move_to_dir("/var/lib/ics/cameras", "runs");
  const std::filesystem::path path = dir / "ics-camd.toml";
  std::ofstream(path) << config;
  return path;
}

TEST(CamdRun, RejectsABadCommandLine) {
  EXPECT_EQ(run({}, "ics-camd.toml"), ics::camd::kExitUsage);
  EXPECT_EQ(run({"ics-camd", "other.toml"}, "ics-camd.toml"), ics::camd::kExitUsage);
}

TEST(CamdRun, RejectsABadConfigFile) {
  const TempDir dir;
  EXPECT_EQ(run({"ics-camd"}, dir / "missing.toml"), ics::camd::kExitUsage);
}

TEST(CamdRun, RejectsAConfigThatDoesNotValidate) {
  const TempDir dir;
  const std::filesystem::path path = write_config(dir);
  std::ifstream in(path);
  std::stringstream text;
  text << in.rdbuf();
  std::string config = text.str();
  // Every camera table is still read, so the file reads, and then does not
  // validate.
  config.replace(config.find(R"(cameras = ["north-hs", "north-mwir"])"), 36,
                 R"(cameras = ["north-hs", "north-mwir", "north-hs"])");
  std::ofstream(path) << config;
  testing::internal::CaptureStderr();
  EXPECT_EQ(run({"ics-camd"}, path), ics::camd::kExitUsage);
  EXPECT_NE(testing::internal::GetCapturedStderr().find("camera ID \"north-hs\" is listed twice"), std::string::npos);
}

TEST(CamdRun, ServesTheConfigFileUntilSigterm) {
  const TempDir dir;
  const std::filesystem::path path = write_config(dir);
  const BlockedSigterm blocked;
  ::pthread_kill(::pthread_self(), SIGTERM);
  EXPECT_EQ(run({"ics-camd"}, path), ics::camd::kExitStopped);
}

// ICS-031's "Done when": one command arms and offloads every camera.
TEST(CamdRun, OneCommandArmsAndOffloadsEveryCamera) {
  const TempDir dir;
  const Config config = ics::camd::testing::test_config(dir / "");
  const auto time = ics::camd::testing::serve_report(config.time_socket, ics::camd::testing::locked_report());
  const BlockedSigterm blocked;
  Logged logged;
  const pthread_t serving_thread = ::pthread_self();
  int command_status = -1;
  std::thread command([&config, &command_status, serving_thread] {
    EXPECT_TRUE(ics::camd::testing::wait_for_file(config.command_socket));
    testing::internal::CaptureStdout();
    const std::vector<const char*> args{"ics-cam-offload", "run-1", config.command_socket.c_str()};
    command_status = ics::camd::run_offload_client(args, stdout);
    static_cast<void>(testing::internal::GetCapturedStdout());
    ::pthread_kill(serving_thread, SIGTERM);
  });
  const int status = ics::camd::serve(config, logged.logger);
  command.join();
  EXPECT_EQ(status, ics::camd::kExitStopped);
  EXPECT_EQ(command_status, ics::camd::kExitStopped);
  for (const char* camera : {"north-hs", "north-mwir"}) {
    for (const char* segment : {"cine-000", "cine-001"}) {
      const std::filesystem::path base = config.offload_folder / "run-1" / camera / segment;
      EXPECT_TRUE(std::filesystem::exists(base.native() + ".cine")) << base;
      EXPECT_EQ(ics::camd::testing::read_meta(base.native() + ".meta").size(), 300U) << base;
    }
  }
  const std::string log = logged.out.str();
  EXPECT_NE(log.find(R"("event":"started","station_id":"station-1",)"), std::string::npos) << log;
  EXPECT_NE(log.find(R"("event":"stopped","signal":15})"), std::string::npos) << log;
}

TEST(CamdRun, ReportsASocketItCannotOpen) {
  const TempDir dir;
  Config config = ics::camd::testing::test_config(dir / "");
  config.command_socket = dir / "missing" / "command";
  const BlockedSigterm blocked;
  Logged logged;
  EXPECT_EQ(ics::camd::serve(config, logged.logger), ics::camd::kExitUnavailable);
  EXPECT_NE(logged.out.str().find(R"("event":"start_failed","error":"unavailable")"), std::string::npos);
}

TEST(CamdRun, ReportsNoDescriptorForSignals) {
  const TempDir dir;
  const BlockedSigterm blocked;
  Logged logged;
  int status = 0;
  {
    const ics::timing::testing::DescriptorLimit limit;
    status = ics::camd::serve(ics::camd::testing::test_config(dir / ""), logged.logger);
  }
  EXPECT_EQ(status, ics::camd::kExitUnavailable);
  EXPECT_NE(logged.out.str().find(R"("event":"start_failed")"), std::string::npos);
}

}  // namespace
