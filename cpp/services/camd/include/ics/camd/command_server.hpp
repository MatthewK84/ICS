#pragma once

#include <chrono>
#include <filesystem>
#include <memory>

#include "ics/camd/config.hpp"
#include "ics/common/error.hpp"
#include "ics/logging/logger.hpp"

namespace grpc {
class Server;
}  // namespace grpc

namespace ics::camd {

class CommandService;

// Serves CameraService (ics/v1/camera_service.proto) over gRPC on the
// config's command socket (ICS-031). ArmAndOffload makes the run's folder
// under the offload folder, watches ics-timingd's time quality, waiting up
// to the config's time_wait for its first report, and offloads every camera
// at once (offload_all). One runs at a time: another that comes meanwhile is
// refused with FAILED_PRECONDITION, since arming a camera again would
// discard the segments it has not yet saved.
class CommandServer {
  // Only open() makes a server.
  struct Key {
    explicit Key() = default;
  };

 public:
  // How long stopping waits for a command to finish.
  static constexpr std::chrono::seconds kShutdownGrace{5};

  // Listens at the config's command socket, removing a file left there
  // first. config and logger must outlive the server. kInvalidArgument for a
  // path too long for a socket address; kUnavailable when the socket cannot
  // be made, for example in a folder that does not exist.
  [[nodiscard]] static Result<std::unique_ptr<CommandServer>> open(const Config& config,
                                                                   const logging::Logger& logger);

  CommandServer(Key key, std::filesystem::path socket, std::unique_ptr<CommandService> service,
                std::unique_ptr<grpc::Server> server);

  // Stops the server, and removes the socket.
  ~CommandServer();
  CommandServer(const CommandServer&) = delete;
  CommandServer& operator=(const CommandServer&) = delete;
  CommandServer(CommandServer&&) = delete;
  CommandServer& operator=(CommandServer&&) = delete;

 private:
  std::filesystem::path socket_;
  // Declared before server_, so it outlives the server's threads.
  std::unique_ptr<CommandService> service_;
  std::unique_ptr<grpc::Server> server_;
};

}  // namespace ics::camd
