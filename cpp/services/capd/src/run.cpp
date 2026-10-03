#include "ics/capd/run.hpp"

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

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

// Steps service each time packets wait, or kPollTimeoutMs passes, until stop
// becomes readable or a step fails.
[[nodiscard]] Status loop(Service& service, const logging::Logger& logger, const int stop) {
  std::vector<pollfd> ready = service.descriptors();
  ready.push_back(pollfd{stop, POLLIN, 0});
  Status stepped;
  for (bool running = true; running && stepped;) {
    static_cast<void>(::poll(ready.data(), ready.size(), kPollTimeoutMs));
    stepped = service.step(logging::system_now(), logger);
    running = (ready.back().revents & POLLIN) == 0;
  }
  return stepped;
}

}  // namespace

int serve(const Config& config, const logging::Logger& logger) {
  const timing::Fd stop = stop_signals();
  if (!stop.valid()) {
    logger.error("start_failed", {{"error", "no descriptor for SIGINT and SIGTERM"}});
    return kExitFailed;
  }
  std::string reason;
  Result<Service> service = Service::open(config, logging::system_now(), reason);
  if (!service) {
    logger.error("start_failed", {{"error", to_string(service.error())}, {"reason", reason}});
    return kExitFailed;
  }
  logger.info("started", {{"interfaces", static_cast<std::int64_t>(config.interfaces.size())},
                          {"folder", config.folder.native()},
                          {"rotate_interval_ns", config.rotation.max_age.count()}});
  const Status ran = loop(*service, logger, stop.get());
  const Status closed = service->close(logger);
  if (!ran || !closed) {
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
