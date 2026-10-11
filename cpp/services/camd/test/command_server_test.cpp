#include "ics/camd/command_server.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

#include <grpcpp/grpcpp.h>
#include <gtest/gtest.h>

#include "camd_support.hpp"
#include "ics/timingd/report_server.hpp"
#include "ics/v1/camera_service.grpc.pb.h"
#include "support.hpp"

namespace {

using ics::camd::CommandServer;
using ics::camd::Config;
using ics::camd::testing::Logged;
using ics::timing::testing::TempDir;

// Sends ArmAndOffload for run_id to the server at socket.
grpc::Status arm_and_offload(const std::filesystem::path& socket, const std::string& run_id,
                             ics::v1::ArmAndOffloadResponse& response) {
  const auto stub = ics::v1::CameraService::NewStub(
      grpc::CreateChannel("unix:" + socket.native(), grpc::InsecureChannelCredentials()));
  grpc::ClientContext context;
  context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(60));
  ics::v1::ArmAndOffloadRequest request;
  request.set_run_id(run_id);
  return stub->ArmAndOffload(&context, request, &response);
}

std::unique_ptr<CommandServer> open(const Config& config, const Logged& logged) {
  auto server = CommandServer::open(config, logged.logger);
  EXPECT_TRUE(server.has_value());
  return std::move(*server);
}

TEST(CamdCommandServer, ArmsAndOffloadsEveryCamera) {
  const TempDir dir;
  const Config config = ics::camd::testing::test_config(dir / "");
  const auto time = ics::camd::testing::serve_report(config.time_socket, ics::camd::testing::locked_report());
  Logged logged;
  const auto server = open(config, logged);
  ics::v1::ArmAndOffloadResponse response;
  const grpc::Status status = arm_and_offload(config.command_socket, "run-1", response);
  ASSERT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(response.folder(), (dir / "runs" / "run-1").native());
  ASSERT_EQ(response.cameras_size(), 2);
  for (const ics::v1::CameraOffload& camera : response.cameras()) {
    EXPECT_TRUE(camera.verified()) << camera.camera_id();
    EXPECT_EQ(camera.segments(), 2U);
    EXPECT_EQ(camera.frames(), 600U);
    EXPECT_EQ(camera.files_size(), 4);
    EXPECT_EQ(camera.error(), "");
    for (const std::string& file : camera.files()) {
      EXPECT_TRUE(std::filesystem::exists(file)) << file;
    }
  }
  EXPECT_EQ(response.cameras(0).camera_id(), "north-hs");
  EXPECT_EQ(response.cameras(1).camera_id(), "north-mwir");
  const std::string log = logged.out.str();
  EXPECT_NE(log.find(R"("event":"offload_started","run_id":"run-1","cameras":2)"), std::string::npos) << log;
  EXPECT_NE(log.find(R"("event":"offloaded","run_id":"run-1","camera":"north-mwir","segments":2,"verified":true)"),
            std::string::npos)
      << log;
}

TEST(CamdCommandServer, OffloadsWithoutTimeQualityAndSaysWhy) {
  const TempDir dir;
  Config config = ics::camd::testing::test_config(dir / "");
  config.time_wait = std::chrono::milliseconds(100);
  Logged logged;
  const auto server = open(config, logged);
  ics::v1::ArmAndOffloadResponse response;
  ASSERT_TRUE(arm_and_offload(config.command_socket, "run-1", response).ok());
  ASSERT_EQ(response.cameras_size(), 2);
  EXPECT_FALSE(response.cameras(0).verified());
  EXPECT_EQ(response.cameras(0).error(), "");
  EXPECT_EQ(response.cameras(0).files_size(), 4);
  EXPECT_FALSE(response.cameras(1).verified());
  EXPECT_EQ(response.cameras(1).error(), "unavailable");
  const std::string log = logged.out.str();
  EXPECT_NE(log.find(R"("event":"no_time_quality","run_id":"run-1")"), std::string::npos) << log;
  EXPECT_NE(log.find(R"("event":"offload_failed","run_id":"run-1","camera":"north-mwir","error":"unavailable")"),
            std::string::npos)
      << log;
}

TEST(CamdCommandServer, RefusesABadRunId) {
  const TempDir dir;
  const Config config = ics::camd::testing::test_config(dir / "");
  Logged logged;
  const auto server = open(config, logged);
  ics::v1::ArmAndOffloadResponse response;
  EXPECT_EQ(arm_and_offload(config.command_socket, "../run-1", response).error_code(),
            grpc::StatusCode::INVALID_ARGUMENT);
  EXPECT_EQ(arm_and_offload(config.command_socket, "", response).error_code(), grpc::StatusCode::INVALID_ARGUMENT);
  EXPECT_FALSE(std::filesystem::exists(config.offload_folder));
}

TEST(CamdCommandServer, RefusesARunThatExists) {
  const TempDir dir;
  const Config config = ics::camd::testing::test_config(dir / "");
  std::filesystem::create_directories(config.offload_folder / "run-1");
  Logged logged;
  const auto server = open(config, logged);
  ics::v1::ArmAndOffloadResponse response;
  EXPECT_EQ(arm_and_offload(config.command_socket, "run-1", response).error_code(),
            grpc::StatusCode::ALREADY_EXISTS);
  EXPECT_TRUE(std::filesystem::is_empty(config.offload_folder / "run-1"));
}

TEST(CamdCommandServer, ReportsAFolderItCannotMake) {
  const TempDir dir;
  Config config = ics::camd::testing::test_config(dir / "");
  std::ofstream(config.offload_folder) << "a file, not a folder";
  Logged logged;
  const auto server = open(config, logged);
  ics::v1::ArmAndOffloadResponse response;
  EXPECT_EQ(arm_and_offload(config.command_socket, "run-1", response).error_code(),
            grpc::StatusCode::UNAVAILABLE);
}

TEST(CamdCommandServer, RefusesASecondCommandWhileOneRuns) {
  const TempDir dir;
  Config config = ics::camd::testing::test_config(dir / "");
  config.time_wait = std::chrono::seconds(2);
  // ics-timingd before its first report: the first command waits time_wait.
  const auto time = ics::timingd::ReportServer::open(config.time_socket);
  ASSERT_TRUE(time.has_value());
  Logged logged;
  const auto server = open(config, logged);
  grpc::Status first;
  std::thread running([&config, &first] {
    ics::v1::ArmAndOffloadResponse response;
    first = arm_and_offload(config.command_socket, "run-1", response);
  });
  ASSERT_TRUE(ics::camd::testing::wait_for_file(config.offload_folder / "run-1"));
  ics::v1::ArmAndOffloadResponse response;
  EXPECT_EQ(arm_and_offload(config.command_socket, "run-2", response).error_code(),
            grpc::StatusCode::FAILED_PRECONDITION);
  running.join();
  EXPECT_TRUE(first.ok()) << first.error_message();
  EXPECT_FALSE(std::filesystem::exists(config.offload_folder / "run-2"));
}

TEST(CamdCommandServer, ReportsASocketItCannotOpen) {
  const TempDir dir;
  Config config = ics::camd::testing::test_config(dir / "");
  Logged logged;
  config.command_socket = dir / std::string(200, 's');
  EXPECT_EQ(CommandServer::open(config, logged.logger).error(), ics::Error::kInvalidArgument);
  config.command_socket = dir / "missing" / "command";
  EXPECT_EQ(CommandServer::open(config, logged.logger).error(), ics::Error::kUnavailable);
}

}  // namespace
