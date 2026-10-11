#include "ics/camd/run.hpp"

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

#include <poll.h>
#include <sys/signalfd.h>
#include <unistd.h>

#include "ics/camd/command_server.hpp"
#include "ics/common/check.hpp"
#include "ics/common/error.hpp"
#include "ics/config/reader.hpp"
#include "ics/timing/unix_socket.hpp"

namespace ics::camd {
namespace {

// Blocks SIGINT and SIGTERM, and returns a descriptor that becomes readable
// when either arrives; invalid if none can be made.
[[nodiscard]] timing::Fd stop_signals() noexcept {
  sigset_t signals{};
  sigemptyset(&signals);
  sigaddset(&signals, SIGINT);
  sigaddset(&signals, SIGTERM);
  static_cast<void>(ics::check(::pthread_sigmask(SIG_BLOCK, &signals, nullptr) == 0));
  return timing::Fd(::signalfd(-1, &signals, SFD_CLOEXEC));
}

// Waits for SIGINT or SIGTERM, and returns which came.
[[nodiscard]] std::int64_t wait_for_stop(const int stop) noexcept {
  pollfd ready{stop, POLLIN, 0};
  static_cast<void>(ics::check(::poll(&ready, 1, -1) == 1));
  signalfd_siginfo info{};
  static_cast<void>(ics::check(::read(stop, &info, sizeof(info)) == static_cast<ssize_t>(sizeof(info))));
  return info.ssi_signo;
}

}  // namespace

int serve(const Config& config, const logging::Logger& logger) {
  const timing::Fd stop = stop_signals();
  if (!stop.valid()) {
    logger.error("start_failed", {{"error", "no descriptor for SIGINT and SIGTERM"}});
    return kExitUnavailable;
  }
  Result<std::unique_ptr<CommandServer>> server = CommandServer::open(config, logger);
  if (!server) {
    logger.error("start_failed", {{"error", to_string(server.error())}});
    return kExitUnavailable;
  }
  logger.info("started", {{"station_id", config.station_id},
                          {"command_socket", config.command_socket.native()},
                          {"cameras", static_cast<std::int64_t>(config.cameras.size())}});
  const std::int64_t signal = wait_for_stop(stop.get());
  server->reset();
  logger.info("stopped", {{"signal", signal}});
  return kExitStopped;
}

int run(const std::span<const char* const> args, const std::filesystem::path& path) {
  if (args.size() != 1) {
    std::fprintf(stderr, "usage: ics-camd\nThe config file is %s.\n", path.c_str());
    return kExitUsage;
  }
  const auto config = config::read_file(path, &read_config);
  if (!config) {
    std::fputs(config::format_errors(path.native(), config.error()).c_str(), stderr);
    return kExitUsage;
  }
  std::string reason;
  if (!validate(*config, reason)) {
    std::fprintf(stderr, "%s: %s\n", path.c_str(), reason.c_str());
    return kExitUsage;
  }
  const logging::Logger logger = logging::Logger::to_stderr(config->log.service, config->log.level);
  return serve(*config, logger);
}

}  // namespace ics::camd
