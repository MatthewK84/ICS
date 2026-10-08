#include "ics/plid/run.hpp"

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <poll.h>
#include <sys/signalfd.h>
#include <unistd.h>

#include "ics/common/check.hpp"
#include "ics/common/error.hpp"
#include "ics/config/reader.hpp"
#include "ics/frames/egm96.hpp"
#include "ics/plid/service.hpp"
#include "ics/timing/unix_socket.hpp"

namespace ics::plid {
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

// Steps service each time a feed has something, or kPollTimeoutMs passes,
// until stop becomes readable or a step fails.
[[nodiscard]] Status loop(Service& service, const logging::Logger& logger, const int stop) {
  Status stepped;
  for (bool running = true; running && stepped;) {
    std::vector<pollfd> ready = service.descriptors();
    ready.push_back(pollfd{stop, POLLIN, 0});
    // Each TAP port and file, the SAPIENT socket and the stop signals.
    static_cast<void>(ics::check(ready.size() <= (2 * kMaxCaptures) + 2));
    static_cast<void>(::poll(ready.data(), ready.size(), kPollTimeoutMs));
    stepped = service.step(logging::system_now(), logger);
    running = (ready.back().revents & POLLIN) == 0;
  }
  return stepped;
}

}  // namespace

int serve(const Config& config, const std::filesystem::path& geoid, const logging::Logger& logger) {
  const timing::Fd stop = stop_signals();
  const Result<frames::Egm96> grid = frames::Egm96::load(geoid);
  if (!stop.valid() || !grid) {
    logger.error("start_failed", {{"error", "no descriptor for SIGINT and SIGTERM, or no geoid grid"},
                                  {"geoid", geoid.native()}});
    return kExitFailed;
  }
  std::string reason;
  Result<Service> service = Service::open(config, *grid, logging::system_now(), logger, reason);
  if (!service) {
    logger.error("start_failed", {{"error", to_string(service.error())}, {"reason", reason}});
    return kExitFailed;
  }
  logger.info("started", {{"feeds", static_cast<std::int64_t>(config.feeds.size())},
                          {"store_folder", config.store_folder.native()},
                          {"query_socket", config.query_socket.native()}});
  const Status ran = loop(*service, logger, stop.get());
  const Status closed = service->close(logger);
  if (!ran.and_then([&closed] { return closed; })) {
    return kExitFailed;
  }
  logger.info("stopped", {{"signal", stop_signal(stop.get())}, {"stored", static_cast<std::int64_t>(service->ingest().stored())}});
  return kExitStopped;
}

int run(const std::span<const char* const> args, const std::filesystem::path& path) {
  if (args.size() != 1) {
    std::fprintf(stderr, "usage: ics-plid\nThe config file is %s.\n", path.c_str());
    return kExitUsage;
  }
  const auto config = config::read_file(path, &read_config);
  if (!config) {
    std::fputs(config::format_errors(path.native(), config.error()).c_str(), stderr);
    return kExitUsage;
  }
  const logging::Logger logger = logging::Logger::to_stderr(config->log.service, config->log.level);
  return serve(*config, frames::Egm96::kDefaultPath, logger);
}

}  // namespace ics::plid
