#include "ics/timingd/run.hpp"

#include <chrono>
#include <csignal>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <signal.h>
#include <unistd.h>

#include "ics/logging/json_line.hpp"
#include "ics/logging/logger.hpp"
#include "ics/timingd/config.hpp"
#include "support.hpp"
#include "timingd_support.hpp"

namespace {

using ics::timing::testing::TempDir;
using ics::timingd::Config;

// Blocks SIGTERM in this thread, and in threads it starts, while it lives;
// then discards a SIGTERM left pending and restores the signal mask.
class BlockedSigterm {
 public:
  BlockedSigterm() {
    sigemptyset(&sigterm_);
    sigaddset(&sigterm_, SIGTERM);
    ::pthread_sigmask(SIG_BLOCK, &sigterm_, &saved_);
  }
  ~BlockedSigterm() {
    const timespec now{};
    ::sigtimedwait(&sigterm_, nullptr, &now);
    ::pthread_sigmask(SIG_SETMASK, &saved_, nullptr);
  }
  BlockedSigterm(const BlockedSigterm&) = delete;
  BlockedSigterm& operator=(const BlockedSigterm&) = delete;
  BlockedSigterm(BlockedSigterm&&) = delete;
  BlockedSigterm& operator=(BlockedSigterm&&) = delete;

 private:
  sigset_t sigterm_{};
  sigset_t saved_{};
};

int run(const std::vector<const char*>& args) { return ics::timingd::run(args); }

struct Logged {
  std::ostringstream out;
  ics::logging::Logger logger = ics::logging::Logger::to_stream("ics-timingd", ics::logging::Level::kDebug, out,
                                                                &ics::timingd::testing::fixed_clock);
};

TEST(Run, RejectsABadCommandLine) {
  EXPECT_EQ(run({"ics-timingd"}), ics::timingd::kExitUsage);
  EXPECT_EQ(run({"ics-timingd", "a.toml", "b.toml"}), ics::timingd::kExitUsage);
}

TEST(Run, RejectsABadConfigFile) {
  const TempDir dir;
  EXPECT_EQ(run({"ics-timingd", (dir / "missing.toml").c_str()}), ics::timingd::kExitUsage);
}

TEST(Run, ServesTheConfigFileUntilSigterm) {
  const TempDir dir;
  const std::string path = dir / "ics-timingd.toml";
  std::ofstream(path) << "[log]\nlevel = \"warn\"\nservice = \"ics-timingd\"\n[timing]\n"
                      << "station_id = \"station-1\"\nptp4l_socket = \"" << (dir / "ptp4l-ro").native() << "\"\n"
                      << "client_socket = \"" << (dir / "client").native() << "\"\n"
                      << "publish_socket = \"" << (dir / "time-quality").native() << "\"\n"
                      << "ptp_domain = 0\npoll_interval_ns = 10_000_000\nasymmetry_bound_ns = 1_000\n"
                      << "holdover_drift_ns_per_s = 50.0\n";
  const BlockedSigterm blocked;
  ::kill(::getpid(), SIGTERM);
  EXPECT_EQ(run({"ics-timingd", path.c_str()}), ics::timingd::kExitStopped);
}

TEST(Run, StepsEachIntervalUntilSigterm) {
  const TempDir dir;
  const BlockedSigterm blocked;
  Logged logged;
  std::thread stopper([] {
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    ::kill(::getpid(), SIGTERM);
  });
  const int status = ics::timingd::serve(ics::timingd::testing::test_config(dir), logged.logger);
  stopper.join();
  EXPECT_EQ(status, ics::timingd::kExitStopped);
  const std::string log = logged.out.str();
  EXPECT_NE(log.find(R"("event":"started","station_id":"station-1",)"), std::string::npos);
  EXPECT_NE(log.find(R"("event":"clock_state","state":"free_running")"), std::string::npos);
  EXPECT_NE(log.find(R"("event":"stopped","signal":15})"), std::string::npos);
}

TEST(Run, ReportsASocketItCannotOpen) {
  const TempDir dir;
  Config config = ics::timingd::testing::test_config(dir);
  config.publish_socket = dir / "missing" / "time-quality";
  const BlockedSigterm blocked;
  Logged logged;
  EXPECT_EQ(ics::timingd::serve(config, logged.logger), ics::timingd::kExitUnavailable);
  EXPECT_NE(logged.out.str().find(R"("event":"start_failed","error":"unavailable")"), std::string::npos);
}

TEST(Run, ReportsNoDescriptorForSignals) {
  const TempDir dir;
  const BlockedSigterm blocked;
  Logged logged;
  int status = 0;
  {
    const ics::timing::testing::DescriptorLimit limit;
    status = ics::timingd::serve(ics::timingd::testing::test_config(dir), logged.logger);
  }
  EXPECT_EQ(status, ics::timingd::kExitUnavailable);
  EXPECT_NE(logged.out.str().find(R"("event":"start_failed")"), std::string::npos);
}

}  // namespace
