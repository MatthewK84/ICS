#include "ics/capd/run.hpp"

#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <signal.h>
#include <unistd.h>

#include "capd_support.hpp"
#include "capture_support.hpp"
#include "ics/capd/config.hpp"
#include "support.hpp"

namespace {

using ics::capd::Config;
using ics::capd::testing::Logged;
using ics::capd::testing::test_config;
using ics::capture::testing::Loopback;
using ics::timing::testing::BlockedSigterm;
using ics::timing::testing::TempDir;
using std::chrono::milliseconds;

int run(const std::vector<const char*>& args, const std::filesystem::path& config) {
  return ics::capd::run(args, config);
}

TEST(CapdRun, RejectsABadCommandLine) {
  EXPECT_EQ(run({}, "ics-capd.toml"), ics::capd::kExitUsage);
  EXPECT_EQ(run({"ics-capd", "other.toml"}, "ics-capd.toml"), ics::capd::kExitUsage);
}

TEST(CapdRun, RejectsABadConfigFile) {
  const TempDir dir;
  EXPECT_EQ(run({"ics-capd"}, dir / "missing.toml"), ics::capd::kExitUsage);
}

TEST(CapdRun, CapturesTheConfiguredPortsUntilSigterm) {
  const TempDir dir;
  const std::string path = dir / "ics-capd.toml";
  std::ofstream(path) << "[log]\nlevel = \"warn\"\nservice = \"ics-capd\"\n[capture]\ninterfaces = [\"lo\"]\n"
                      << "output_dir = \"" << (dir / "").native() << "\"\n"
                      << "snaplen = 256\nring_bytes = 1_048_576\ntimestamps = \"host\"\n"
                      << "rotate_interval_ns = 60_000_000_000\nrotate_bytes = 1_048_576\n"
                      << "ptp4l_socket = \"" << (dir / "ptp4l-ro").native() << "\"\n"
                      << "client_socket = \"" << (dir / "client").native() << "\"\nptp_domain = 0\n";
  const BlockedSigterm blocked;
  ::kill(::getpid(), SIGTERM);
  EXPECT_EQ(run({"ics-capd"}, path), ics::capd::kExitStopped);
}

TEST(CapdRun, WritesWhatArrivesAndClosesTheFilesAtSigterm) {
  const TempDir dir;
  const BlockedSigterm blocked;
  Logged logged;
  std::thread traffic([] {
    const Loopback loopback;
    std::this_thread::sleep_for(milliseconds(200));
    loopback.send(5);
    std::this_thread::sleep_for(milliseconds(1300));
    ::kill(::getpid(), SIGTERM);
  });
  const int status = ics::capd::serve(test_config(dir), logged.logger);
  traffic.join();
  EXPECT_EQ(status, ics::capd::kExitStopped);
  const std::string log = logged.out.str();
  EXPECT_NE(log.find(R"("event":"started","ports":1,)"), std::string::npos);
  EXPECT_NE(log.find(R"("event":"file_closed","interface":"lo",)"), std::string::npos);
  EXPECT_NE(log.find(R"("event":"stopped","signal":15})"), std::string::npos);
  EXPECT_EQ(ics::capture::testing::marked(ics::capd::testing::pcap_files(dir)), 5U);
}

TEST(CapdRun, StopsWhenAFileCannotBeWritten) {
  const TempDir dir;
  Config config = test_config(dir);
  config.output_dir = dir / "gone";
  std::filesystem::create_directory(config.output_dir);
  const BlockedSigterm blocked;
  Logged logged;
  std::thread traffic([&config] {
    const Loopback loopback;
    std::this_thread::sleep_for(milliseconds(200));
    std::filesystem::remove(config.output_dir);
    loopback.send(5);
  });
  const int status = ics::capd::serve(config, logged.logger);
  traffic.join();
  EXPECT_EQ(status, ics::capd::kExitFailed);
  EXPECT_NE(logged.out.str().find(R"("event":"capture_failed","error":"unwritable")"), std::string::npos);
}

TEST(CapdRun, StopsWhenAFileCannotBeFinished) {
  const TempDir dir;
  Config config = test_config(dir);
  config.rotation.interval = std::chrono::seconds(1);
  const BlockedSigterm blocked;
  Logged logged;
  std::thread traffic([] {
    const Loopback loopback;
    std::this_thread::sleep_for(milliseconds(200));
    loopback.send(5);
  });
  int status = 0;
  {
    // The packets wait in the write buffer; closing the file a second later
    // cannot write them.
    const ics::capture::testing::FileSizeLimit limit(10);
    status = ics::capd::serve(config, logged.logger);
  }
  traffic.join();
  EXPECT_EQ(status, ics::capd::kExitFailed);
  EXPECT_NE(logged.out.str().find(R"("event":"capture_failed","error":"unwritable")"), std::string::npos);
}

TEST(CapdRun, ReportsAPortItCannotOpen) {
  const TempDir dir;
  Config config = test_config(dir);
  config.interfaces = {"ics-none0"};
  const BlockedSigterm blocked;
  Logged logged;
  EXPECT_EQ(ics::capd::serve(config, logged.logger), ics::capd::kExitFailed);
  EXPECT_NE(logged.out.str().find(R"("event":"start_failed","error":"unavailable")"), std::string::npos);
}

TEST(CapdRun, ReportsNoDescriptorForSignals) {
  const TempDir dir;
  const BlockedSigterm blocked;
  Logged logged;
  int status = 0;
  {
    const ics::timing::testing::DescriptorLimit limit;
    status = ics::capd::serve(test_config(dir), logged.logger);
  }
  EXPECT_EQ(status, ics::capd::kExitFailed);
  EXPECT_NE(logged.out.str().find(R"("event":"start_failed")"), std::string::npos);
}

}  // namespace
