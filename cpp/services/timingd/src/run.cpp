#include "ics/timingd/run.hpp"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string_view>

#include <poll.h>
#include <sys/signalfd.h>
#include <unistd.h>

#include "ics/common/check.hpp"
#include "ics/common/error.hpp"
#include "ics/config/reader.hpp"
#include "ics/timing/quality.hpp"
#include "ics/timing/unix_socket.hpp"
#include "ics/timingd/service.hpp"

namespace ics::timingd {
namespace {

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

// Waits until deadline: true when stop became readable first.
[[nodiscard]] bool stopped(const int stop, const timing::SteadyTime deadline) noexcept {
  const auto wait = std::chrono::ceil<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
  pollfd ready{stop, POLLIN, 0};
  return ::poll(&ready, 1, static_cast<int>(std::max<std::int64_t>(wait.count(), 0))) > 0;
}

// The signal that made stop readable.
[[nodiscard]] std::int64_t stop_signal(const int stop) noexcept {
  signalfd_siginfo info{};
  static_cast<void>(ics::check(::read(stop, &info, sizeof(info)) == static_cast<ssize_t>(sizeof(info))));
  return info.ssi_signo;
}

// Steps service every interval until stop becomes readable. A step that
// overruns the interval is followed at once by the next.
void loop(Service& service, const logging::Logger& logger, const int stop, const Duration interval) {
  timing::SteadyTime next = std::chrono::steady_clock::now();
  for (bool running = true; running;) {
    service.step(logger);
    next = std::max(next + interval, std::chrono::steady_clock::now());
    running = !stopped(stop, next);
  }
}

}  // namespace

int serve(const Config& config, const logging::Logger& logger) {
  const timing::Fd stop = stop_signals();
  if (!stop.valid()) {
    logger.error("start_failed", {{"error", "no descriptor for SIGINT and SIGTERM"}});
    return kExitUnavailable;
  }
  Result<Service> service = Service::open(config);
  if (!service) {
    logger.error("start_failed", {{"error", to_string(service.error())}});
    return kExitUnavailable;
  }
  logger.info("started", {{"station_id", config.station_id},
                          {"ptp4l_socket", config.ptp4l_socket.native()},
                          {"publish_socket", config.publish_socket.native()},
                          {"poll_interval_ns", config.poll_interval.count()}});
  loop(*service, logger, stop.get(), config.poll_interval);
  logger.info("stopped", {{"signal", stop_signal(stop.get())}});
  return kExitStopped;
}

int run(const std::span<const char* const> args) {
  if (args.size() != 2) {
    std::fputs("usage: ics-timingd CONFIG\n", stderr);
    return kExitUsage;
  }
  const std::filesystem::path path(args[1]);
  const auto config = config::read_file(path, &read_config);
  if (!config) {
    std::fputs(config::format_errors(path.native(), config.error()).c_str(), stderr);
    return kExitUsage;
  }
  const logging::Logger logger = logging::Logger::to_stderr(config->log.service, config->log.level);
  return serve(*config, logger);
}

}  // namespace ics::timingd
