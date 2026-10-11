#include "ics/camd/command_client.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "camd_support.hpp"
#include "ics/camd/command_server.hpp"
#include "ics/camd/run.hpp"
#include "support.hpp"

namespace {

using ics::camd::Config;
using ics::camd::testing::Logged;
using ics::timing::testing::TempDir;

// What ics-cam-offload exits with, writes to stdout and writes to stderr.
struct Ran {
  int status = 0;
  std::string out;
  std::string err;
};

Ran offload(const std::vector<const char*>& args) {
  testing::internal::CaptureStdout();
  testing::internal::CaptureStderr();
  const int status = ics::camd::run_offload_client(args, stdout);
  std::fflush(stdout);
  return Ran{.status = status,
             .out = testing::internal::GetCapturedStdout(),
             .err = testing::internal::GetCapturedStderr()};
}

std::unique_ptr<ics::camd::CommandServer> open(const Config& config, const Logged& logged) {
  auto server = ics::camd::CommandServer::open(config, logged.logger);
  EXPECT_TRUE(server.has_value());
  return std::move(*server);
}

TEST(CamOffload, RejectsBadArguments) {
  EXPECT_EQ(offload({"ics-cam-offload"}).status, ics::camd::kExitUsage);
  const Ran ran = offload({"ics-cam-offload", "run-1", "socket", "extra"});
  EXPECT_EQ(ran.status, ics::camd::kExitUsage);
  EXPECT_EQ(ran.err, "usage: ics-cam-offload RUN_ID [SOCKET]\n");
}

TEST(CamOffload, PrintsEachCameraAndSucceedsWhenEveryOneVerifies) {
  const TempDir dir;
  const Config config = ics::camd::testing::test_config(dir / "");
  const auto time = ics::camd::testing::serve_report(config.time_socket, ics::camd::testing::locked_report());
  Logged logged;
  const auto server = open(config, logged);
  const Ran ran = offload({"ics-cam-offload", "run-1", config.command_socket.c_str()});
  EXPECT_EQ(ran.status, ics::camd::kExitStopped) << ran.err;
  EXPECT_EQ(ran.out.find(R"({"camera_id":"north-hs","verified":true,"segments":2,"frames":"600","files":[)"), 0U)
      << ran.out;
  EXPECT_NE(ran.out.find(R"(
{"camera_id":"north-mwir","verified":true,)"),
            std::string::npos)
      << ran.out;
  EXPECT_EQ(ran.err, "");
}

TEST(CamOffload, FailsWhenACameraDoesNotVerify) {
  const TempDir dir;
  Config config = ics::camd::testing::test_config(dir / "");
  config.time_wait = std::chrono::milliseconds(100);
  Logged logged;
  const auto server = open(config, logged);
  const Ran ran = offload({"ics-cam-offload", "run-1", config.command_socket.c_str()});
  EXPECT_EQ(ran.status, ics::camd::kExitUnavailable);
  EXPECT_EQ(ran.err,
            "ics-cam-offload: north-hs did not verify\n"
            "ics-cam-offload: north-mwir did not verify: unavailable\n");
}

TEST(CamOffload, FailsWhenCamdRefuses) {
  const TempDir dir;
  const Config config = ics::camd::testing::test_config(dir / "");
  Logged logged;
  const auto server = open(config, logged);
  const Ran ran = offload({"ics-cam-offload", "..", config.command_socket.c_str()});
  EXPECT_EQ(ran.status, ics::camd::kExitUnavailable);
  EXPECT_NE(ran.err.find("run_id must be"), std::string::npos) << ran.err;
}

TEST(CamOffload, FailsWhenCamdCannotBeReached) {
  const TempDir dir;
  const std::string socket = (dir / "missing").native();
  EXPECT_EQ(offload({"ics-cam-offload", "run-1", socket.c_str()}).status, ics::camd::kExitUnavailable);
  // The default socket, where no ics-camd runs during the tests.
  EXPECT_EQ(offload({"ics-cam-offload", "run-1"}).status, ics::camd::kExitUnavailable);
}

}  // namespace
