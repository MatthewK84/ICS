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
#include "ics/capd/config.hpp"
#include "support.hpp"

namespace {

using ics::capd::testing::Logged;
using ics::timing::testing::TempDir;

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

// Sends SIGTERM to this process after delay, from another thread.
std::thread sigterm_after(const std::chrono::milliseconds delay) {
  return std::thread([delay] {
    std::this_thread::sleep_for(delay);
    ::kill(::getpid(), SIGTERM);
  });
}

int run(const std::vector<const char*>& args, const std::filesystem::path& config) {
  return ics::capd::run(args, config);
}

TEST(Run, RejectsABadCommandLine) {
  EXPECT_EQ(run({}, "ics-capd.toml"), ics::capd::kExitUsage);
  EXPECT_EQ(run({"ics-capd", "other.toml"}, "ics-capd.toml"), ics::capd::kExitUsage);
}

TEST(Run, RejectsABadConfigFile) {
  const TempDir dir;
  EXPECT_EQ(run({"ics-capd"}, dir / "missing.toml"), ics::capd::kExitUsage);
}

TEST(Run, ServesTheConfigFileUntilSigterm) {
  const TempDir dir;
  const std::string path = dir / "ics-capd.toml";
  std::ofstream(path) << "[log]\nlevel = \"warn\"\nservice = \"ics-capd\"\n[capture]\ninterfaces = [\"lo\"]\n"
                      << "folder = \"" << (dir / "").native() << "\"\nsnaplen = 65535\nbuffer_bytes = 1_048_576\n"
                      << "timestamps = \"host\"\nrotate_interval_ns = 1_000_000_000\nrotate_bytes = 1_048_576\n";
  const BlockedSigterm blocked;
  ::kill(::getpid(), SIGTERM);
  EXPECT_EQ(run({"ics-capd"}, path), ics::capd::kExitStopped);
}

TEST(Run, CapturesUntilSigtermThenClosesEveryFile) {
  const TempDir dir;
  const BlockedSigterm blocked;
  Logged logged;
  std::thread stopper = sigterm_after(std::chrono::milliseconds(1500));
  const int status = ics::capd::serve(ics::capd::testing::loopback_config(dir / ""), logged.logger);
  stopper.join();
  EXPECT_EQ(status, ics::capd::kExitStopped);
  EXPECT_TRUE(logged.has(R"("event":"started","interfaces":1,)"));
  // One file rotated after a second, and one closed at the stop.
  EXPECT_TRUE(logged.has(R"("event":"file_closed","interface":"lo",)"));
  EXPECT_TRUE(logged.has(R"("event":"stopped","signal":15})"));
}

TEST(Run, ExitsWhenAFileCannotBeWritten) {
  const TempDir dir;
  std::filesystem::create_directories(dir / "capture");
  const BlockedSigterm blocked;
  Logged logged;
  // The folder goes away, so the first rotation, after a second, fails.
  std::thread remover([&dir] {
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    std::filesystem::remove_all(dir / "capture");
  });
  const int status = ics::capd::serve(ics::capd::testing::loopback_config(dir / "capture"), logged.logger);
  remover.join();
  EXPECT_EQ(status, ics::capd::kExitFailed);
  EXPECT_TRUE(logged.has(R"("event":"capture_failed","interface":"lo","error":"unwritable"})"));
}

TEST(Run, ExitsWhenTheLastFilesCannotBeClosed) {
  const TempDir dir;
  std::filesystem::create_directories(dir / "capture");
  const BlockedSigterm blocked;
  Logged logged;
  std::thread remover([&dir] {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::filesystem::remove_all(dir / "capture");
    ::kill(::getpid(), SIGTERM);
  });
  const int status = ics::capd::serve(ics::capd::testing::loopback_config(dir / "capture"), logged.logger);
  remover.join();
  EXPECT_EQ(status, ics::capd::kExitFailed);
  EXPECT_TRUE(logged.has(R"("event":"close_failed","interface":"lo","error":"unwritable"})"));
}

TEST(Run, ReportsAnInterfaceItCannotOpen) {
  const TempDir dir;
  ics::capd::Config config = ics::capd::testing::loopback_config(dir / "");
  config.interfaces = {"ics-no-such"};
  const BlockedSigterm blocked;
  Logged logged;
  EXPECT_EQ(ics::capd::serve(config, logged.logger), ics::capd::kExitFailed);
  EXPECT_TRUE(logged.has(R"("event":"start_failed","error":"unavailable","reason":"ics-no-such: )"));
}

TEST(Run, ReportsNoDescriptorForSignals) {
  const TempDir dir;
  const BlockedSigterm blocked;
  Logged logged;
  int status = 0;
  {
    const ics::timing::testing::DescriptorLimit limit;
    status = ics::capd::serve(ics::capd::testing::loopback_config(dir / ""), logged.logger);
  }
  EXPECT_EQ(status, ics::capd::kExitFailed);
  EXPECT_TRUE(logged.has(R"("event":"start_failed","error":"no descriptor for SIGINT and SIGTERM"})"));
}

}  // namespace
