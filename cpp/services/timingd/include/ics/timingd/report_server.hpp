#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>

#include "ics/common/error.hpp"

namespace grpc {
class Server;
}  // namespace grpc

namespace ics::timingd {

class ReportService;

// Serves TimeQualityService (ics/v1/time_quality_service.proto) over gRPC on
// a Unix-domain socket (#150). Each subscriber gets the latest report at once,
// then each new one, from its own gRPC thread, so the poll loop that posts
// the reports never waits for a subscriber; a subscriber that reads more
// slowly than the loop posts skips to the latest report. At most
// kMaxSubscribers watch at once, which bounds the server's memory; the next
// is refused with RESOURCE_EXHAUSTED until one leaves.
class ReportServer {
  // Only open() makes a server.
  struct Key {
    explicit Key() = default;
  };

 public:
  static constexpr std::size_t kMaxSubscribers = 16;
  // How often a subscriber's thread wakes, without a new report, to notice
  // that its client has gone.
  static constexpr std::chrono::milliseconds kWakeInterval{100};
  // How long stopping waits for the subscribers' streams to end.
  static constexpr std::chrono::milliseconds kShutdownGrace{1000};

  // Listens at socket, removing a file left there first. Reserve room for
  // the reports before posting the first. kInvalidArgument for a path too
  // long for a socket address; kUnavailable when the socket cannot be made,
  // for example in a folder that does not exist.
  [[nodiscard]] static Result<std::unique_ptr<ReportServer>> open(const std::filesystem::path& socket);

  ReportServer(Key key, std::filesystem::path socket, std::unique_ptr<ReportService> service,
               std::unique_ptr<grpc::Server> server);

  // Ends every subscriber's stream, stops the server and removes the socket.
  ~ReportServer();
  ReportServer(const ReportServer&) = delete;
  ReportServer& operator=(const ReportServer&) = delete;
  ReportServer(ReportServer&&) = delete;
  ReportServer& operator=(ReportServer&&) = delete;

  // Makes room for reports of up to bytes, so that post allocates nothing.
  void reserve(std::size_t bytes);

  // Hands a serialized ics.v1.TimeQuality, which must fit the room reserved,
  // to the subscribers. Allocates nothing, and does not wait for them.
  void post(std::span<const std::byte> report) noexcept;

  // The subscribers watching now.
  [[nodiscard]] std::size_t subscribers() const noexcept;

 private:
  std::filesystem::path socket_;
  // Declared before server_, so it outlives the server's threads.
  std::unique_ptr<ReportService> service_;
  std::unique_ptr<grpc::Server> server_;
};

}  // namespace ics::timingd
