#include "ics/capd/run.hpp"

#include <array>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <span>

#include <poll.h>
#include <sys/signalfd.h>
#include <unistd.h>

#include "ics/capd/service.hpp"
#include "ics/common/check.hpp"
#include "ics/common/error.hpp"
#include "ics/config/reader.hpp"
#include "ics/timing/unix_socket.hpp"

namespace ics::capd {
namespace {

// How long a quiet loop waits before checking for files due to close.
constexpr int kTickMs = 1000;
// The stop descriptor and one per port.
constexpr std::size_t kMaxDescriptors = 1 + kMaxInterfaces;

// Blocks SIGINT and SIGTERM, and returns a descriptor that becomes readable
// when either arrives; invalid if none can be made.
[[nodiscard]] timing::Fd stop_signals() noexcept {
  sigset_t signals{};
  sigemptyset(&signals);
  sigaddset(&signals, SIGINT);
  sigaddset(&signals, SIGTERM);
  static_cast<void>(ics::check(::pthread_sigmask(SIG_BLOCK, &signals, nullptr) == 0));
  return timing::Fd(::signalfd(-1, &signals, SFD_CLOEXEC | SFD_NONBLOCK));
}

// The signal that made stop readable.
[[nodiscard]] std::int64_t stop_signal(const int stop) noexcept {
  signalfd_siginfo info{};
  static_cast<void>(ics::check(::read(stop, &info, sizeof(info)) == static_cast<ssize_t>(sizeof(info))));
  return info.ssi_signo;
}

// Reads each port poll marked: readable, or in error, which the read reports.
[[nodiscard]] Status read_ready(Service& service, const std::span<const pollfd> ports, const logging::Logger& logger) {
  for (std::size_t port = 0; port < service.ports(); ++port) {
    if (ports[port].revents != 0) {
      const Status read = service.read(port, logger);
      if (!read) {
        return read;
      }
    }
  }
  return {};
}

// Captures until stop becomes readable or a port or file fails.
[[nodiscard]] Status loop(Service& service, const logging::Logger& logger, const int stop) {
  std::array<pollfd, kMaxDescriptors> watched{};
  watched[0] = {stop, POLLIN, 0};
  for (std::size_t port = 0; port < service.ports(); ++port) {
    watched[port + 1] = {service.fd(port), POLLIN, 0};
  }
  const std::span<pollfd> used = std::span(watched).first(service.ports() + 1);
  for (bool running = true; running;) {
    const int ready = ::poll(used.data(), used.size(), kTickMs);
    static_cast<void>(ready);
    const Status read = read_ready(service, used.subspan(1), logger);
    if (!read) {
      return read;
    }
    const Status rotated = service.rotate_due(logging::system_now(), logger);
    if (!rotated) {
      return rotated;
    }
    running = (used[0].revents & POLLIN) == 0;
  }
  return {};
}

}  // namespace

int serve(const Config& config, const logging::Logger& logger) {
  const timing::Fd stop = stop_signals();
  if (!stop.valid()) {
    logger.error("start_failed", {{"error", "no descriptor for SIGINT and SIGTERM"}});
    return kExitFailed;
  }
  Result<Service> service = Service::open(config);
  if (!service) {
    logger.error("start_failed", {{"error", to_string(service.error())}});
    return kExitFailed;
  }
  logger.info("started", {{"ports", static_cast<std::int64_t>(service->ports())},
                          {"output_dir", config.output_dir.native()},
                          {"utc_shift_ns", service->utc_shift().count()}});
  Status result = loop(*service, logger, stop.get());
  const Status closed = service->close(logger);
  result = result.has_value() ? closed : result;
  if (!result) {
    logger.error("capture_failed", {{"error", to_string(result.error())}});
    return kExitFailed;
  }
  logger.info("stopped", {{"signal", stop_signal(stop.get())}});
  return kExitStopped;
}

int run(const std::span<const char* const> args, const std::filesystem::path& path) {
  if (args.size() != 1) {
    std::fprintf(stderr, "usage: ics-capd\nThe config file is %s.\n", path.c_str());
    return kExitUsage;
  }
  const auto config = config::read_file(path, &read_config);
  if (!config) {
    std::fputs(config::format_errors(path.native(), config.error()).c_str(), stderr);
    return kExitUsage;
  }
  const logging::Logger logger = logging::Logger::to_stderr(config->log.service, config->log.level);
  return serve(*config, logger);
}

}  // namespace ics::capd
