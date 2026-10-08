#include "ics/plid/run.hpp"

#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>

#include "capture_support.hpp"
#include "ics/store/archive.hpp"
#include "plid_support.hpp"
#include "support.hpp"

namespace {

using ics::plid::testing::Logged;
using ics::plid::testing::replay_config;
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

int run(const std::vector<const char*>& args, const std::filesystem::path& config) {
  return ics::plid::run(args, config);
}

TEST(Run, RejectsABadCommandLine) {
  EXPECT_EQ(run({}, "ics-plid.toml"), ics::plid::kExitUsage);
  EXPECT_EQ(run({"ics-plid", "other.toml"}, "ics-plid.toml"), ics::plid::kExitUsage);
}

TEST(Run, RejectsABadConfigFile) {
  const TempDir dir;
  EXPECT_EQ(run({"ics-plid"}, dir / "missing.toml"), ics::plid::kExitUsage);
}

TEST(Run, ReadsTheConfigFileThenServes) {
  // The grid is where the ICS images install it, so this starts as a deployed
  // ics-plid would; a store folder that does not exist then stops it.
  const TempDir dir;
  const std::string path = dir / "ics-plid.toml";
  std::ofstream(path) << "[log]\nlevel = \"warn\"\nservice = \"ics-plid\"\n[plid]\nstore_folder = \""
                      << (dir / "missing").native() << "\"\nquery_socket = \"" << (dir / "query").native()
                      << "\"\nrotate_interval_ns = 60_000_000_000\nsync_interval_ns = 1_000_000_000\n"
                      << "feeds = [\"sapient\"]\n[range]\nlatitude_deg = 0.0\nlongitude_deg = 0.0\nheight_m = 0.0\n"
                      << "[capture]\ninterfaces = []\nfiles = []\n[sapient]\naddress = \"127.0.0.1\"\nport = 1\n"
                      << "roles = []\nmax_skew_ns = 0\n";
  EXPECT_EQ(run({"ics-plid"}, path), ics::plid::kExitFailed);
}

TEST(Serve, ReplaysUntilSigtermThenArchives) {
  const TempDir dir;
  Logged logged;
  int status = 0;
  {
    const BlockedSigterm blocked;
    ::kill(::getpid(), SIGTERM);
    status = ics::plid::serve(replay_config(dir / ""), ICS_EGM96_PATH, logged.logger);
  }
  EXPECT_EQ(status, ics::plid::kExitStopped);
  EXPECT_TRUE(logged.has(R"("event":"started","feeds":1)"));
  EXPECT_TRUE(logged.has(R"("event":"stopped","signal":15)"));
  EXPECT_TRUE(logged.has(R"("event":"archived")"));
}

TEST(Serve, StopsWhenItCannotStore) {
  const TempDir dir;
  ics::plid::Config config = replay_config(dir / "");
  config.sync_interval = std::chrono::milliseconds(10);
  Logged logged;
  int status = 0;
  {
    const BlockedSigterm blocked;
    const ics::capture::testing::FileSizeLimit limit(8);
    status = ics::plid::serve(config, ICS_EGM96_PATH, logged.logger);
  }
  EXPECT_EQ(status, ics::plid::kExitFailed);
  EXPECT_TRUE(logged.has(R"("event":"store_failed")"));
}

TEST(Serve, ReportsNoDescriptorForTheStopSignals) {
  const TempDir dir;
  Logged logged;
  int status = 0;
  {
    const ics::timing::testing::DescriptorLimit limit;
    status = ics::plid::serve(replay_config(dir / ""), ICS_EGM96_PATH, logged.logger);
  }
  EXPECT_EQ(status, ics::plid::kExitFailed);
  EXPECT_TRUE(logged.has(R"("event":"start_failed")"));
}

TEST(Serve, ReportsWhatStopsItStarting) {
  const TempDir dir;
  Logged logged;
  EXPECT_EQ(ics::plid::serve(replay_config(dir / ""), dir / "missing.pgm", logged.logger), ics::plid::kExitFailed);
  EXPECT_TRUE(logged.has(R"("event":"start_failed")"));
  ics::plid::Config missing = replay_config(dir / "");
  missing.files = {dir / "missing.pcap"};
  Logged refused;
  EXPECT_EQ(ics::plid::serve(missing, ICS_EGM96_PATH, refused.logger), ics::plid::kExitFailed);
  EXPECT_TRUE(refused.has(R"("event":"start_failed","error":"unreadable")"));
}

}  // namespace
