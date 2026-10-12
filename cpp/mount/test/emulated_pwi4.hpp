#pragma once

// An emulated PlaneWave Interface 4 for the mount client's tests (ICS-033): a
// loopback HTTP/1.1 server that keeps its connections open and answers the
// commands the client sends as PlaneWave documents them, each with status
// text. It moves two simulated axes: a goto or a park slews each axis at up
// to its top speed, which it reaches and sheds at its acceleration; a rate is
// reached at the axis's acceleration; an axis stops at its mechanical limits.
// It reports the distance to a sun the test places (ICS-034). It can answer
// late, answer with an error, or drop the connection, and it keeps what it
// saw.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "ics/common/units.hpp"
#include "ics/mount/keep_out.hpp"
#include "ics/mount/limits.hpp"

namespace ics::mount::testing {

// One simulated axis, in degrees.
struct EmulatedAxis {
  double position_deg = 0.0;
  double min_deg = -270.0;
  double max_deg = 270.0;
  double max_velocity_deg_s = 15.0;
  double acceleration_deg_s2 = 30.0;
};

// Azimuth from 0 within ±270°; altitude from 30° within 0° to 90°.
inline std::array<EmulatedAxis, 2> default_axes() {
  return {EmulatedAxis{}, EmulatedAxis{.position_deg = 30.0, .min_deg = 0.0, .max_deg = 90.0}};
}

// What the emulator saw, by axis where there are two.
struct Seen {
  std::uint64_t requests = 0;
  std::uint64_t statuses = 0;
  std::uint64_t rate_commands = 0;
  std::uint64_t stop_rates = 0;
  std::uint64_t stops = 0;
  std::uint64_t gotos = 0;
  std::uint64_t parks = 0;
  std::uint64_t connects = 0;
  std::uint64_t enables = 0;
  bool connected = false;
  std::array<bool, 2> enabled{};
  double goto_alt_deg = 0.0;
  double goto_az_deg = 0.0;
  std::array<double, 2> last_rate_deg_s{};
  std::array<double, 2> max_abs_rate_deg_s{};
  std::array<double, 2> min_position_deg{};
  std::array<double, 2> max_position_deg{};
  // The closest the line of sight came to the sun, in degrees, sampled at
  // least every 20 ms.
  double closest_to_sun_deg = 180.0;
  // How far each axis's speed was from its commanded rate, summed over each
  // status, and how many statuses.
  std::array<double, 2> rate_error_sum_deg_s{};
  std::uint64_t rate_error_samples = 0;
};

class EmulatedPwi4 {
 public:
  explicit EmulatedPwi4(const std::array<EmulatedAxis, 2>& axes = default_axes()) : axes_(axes) {
    for (std::size_t i = 0; i < 2; ++i) {
      seen_.min_position_deg[i] = axes_[i].position_deg;
      seen_.max_position_deg[i] = axes_[i].position_deg;
    }
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(listen_fd_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
        ::listen(listen_fd_, 8) != 0) {
      ADD_FAILURE() << "the emulated PWI4 cannot listen";
    }
    socklen_t length = sizeof(address);
    ::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&address), &length);
    port_ = ntohs(address.sin_port);
    last_ = std::chrono::steady_clock::now();
    thread_ = std::thread([this] { serve(); });
  }
  ~EmulatedPwi4() {
    stopping_ = true;
    thread_.join();
    for (const Client& client : clients_) {
      ::close(client.fd);
    }
    ::close(listen_fd_);
  }
  EmulatedPwi4(const EmulatedPwi4&) = delete;
  EmulatedPwi4& operator=(const EmulatedPwi4&) = delete;
  EmulatedPwi4(EmulatedPwi4&&) = delete;
  EmulatedPwi4& operator=(EmulatedPwi4&&) = delete;

  [[nodiscard]] std::uint16_t port() const noexcept { return port_; }

  // Answer the next count requests with HTTP 500.
  void fail_next(const std::uint32_t count) {
    const std::scoped_lock lock(mutex_);
    fail_ = count;
  }
  // Answer the next count requests for path with HTTP 500.
  void fail_path(std::string path, const std::uint32_t count) {
    const std::scoped_lock lock(mutex_);
    fail_path_ = std::move(path);
    fail_path_count_ = count;
  }
  // Close the connection instead of answering the next count requests.
  void drop_next(const std::uint32_t count) {
    const std::scoped_lock lock(mutex_);
    drop_ = count;
  }
  // Wait this long before each answer.
  void delay(const std::chrono::milliseconds wait) {
    const std::scoped_lock lock(mutex_);
    delay_ = wait;
  }
  // Where the sun is, for mount.distance_to_sun_degs; far below the horizon
  // until set.
  void set_sun(const AltAz& sun) {
    const std::scoped_lock lock(mutex_);
    sun_ = sun;
  }
  // Where /mount/park sends the axes, in degrees.
  void set_park(const double alt_deg, const double az_deg) {
    const std::scoped_lock lock(mutex_);
    park_ = {az_deg, alt_deg};
  }
  // Where the axes point now.
  [[nodiscard]] AltAz pointing() {
    const std::scoped_lock lock(mutex_);
    advance();
    return pointing_now();
  }
  // Answer /status with this text instead of the simulated status.
  void status_text(std::optional<std::string> text) {
    const std::scoped_lock lock(mutex_);
    status_override_ = std::move(text);
  }
  [[nodiscard]] Seen seen() const {
    const std::scoped_lock lock(mutex_);
    return seen_;
  }

 private:
  struct Client {
    int fd = -1;
    std::string buffer;
    bool closed = false;
  };
  struct Answer {
    int code = 200;
    std::string body;
    bool drop = false;
  };
  using Params = std::map<std::string, std::string, std::less<>>;

  void serve() {
    while (!stopping_) {
      std::vector<pollfd> fds{{listen_fd_, POLLIN, 0}};
      for (const Client& client : clients_) {
        fds.push_back({client.fd, POLLIN, 0});
      }
      if (::poll(fds.data(), fds.size(), 20) <= 0) {
        const std::scoped_lock lock(mutex_);
        advance();
        continue;
      }
      if ((fds[0].revents & POLLIN) != 0) {
        clients_.push_back(Client{.fd = ::accept(listen_fd_, nullptr, nullptr), .buffer = {}, .closed = false});
      }
      for (std::size_t i = 1; i < fds.size(); ++i) {
        if ((fds[i].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
          read_from(clients_[i - 1]);
        }
      }
      std::erase_if(clients_, [](const Client& client) {
        if (client.closed) {
          ::close(client.fd);
        }
        return client.closed;
      });
    }
  }

  void read_from(Client& client) {
    std::array<char, 4096> chunk{};
    const ssize_t got = ::recv(client.fd, chunk.data(), chunk.size(), 0);
    if (got <= 0) {
      client.closed = true;
      return;
    }
    client.buffer.append(chunk.data(), static_cast<std::size_t>(got));
    for (std::size_t end = client.buffer.find("\r\n\r\n"); end != std::string::npos && !client.closed;
         end = client.buffer.find("\r\n\r\n")) {
      const std::string head = client.buffer.substr(0, end);
      client.buffer.erase(0, end + 4);
      const std::size_t space = head.find(' ');
      const std::string target = head.substr(space + 1, head.find(' ', space + 1) - space - 1);
      respond(client, target);
    }
  }

  void respond(Client& client, const std::string& target) {
    std::chrono::milliseconds wait{};
    {
      const std::scoped_lock lock(mutex_);
      wait = delay_;
    }
    std::this_thread::sleep_for(wait);
    const Answer answer = answer_for(target);
    if (answer.drop) {
      client.closed = true;
      return;
    }
    const std::string text = "HTTP/1.1 " + std::to_string(answer.code) + (answer.code == 200 ? " OK" : " Error") +
                             "\r\nContent-Type: text/plain\r\nContent-Length: " + std::to_string(answer.body.size()) +
                             "\r\n\r\n" + answer.body;
    static_cast<void>(::send(client.fd, text.data(), text.size(), MSG_NOSIGNAL));
  }

  Answer answer_for(const std::string& target) {
    const std::scoped_lock lock(mutex_);
    ++seen_.requests;
    if (drop_ > 0) {
      --drop_;
      return {.code = 0, .body = {}, .drop = true};
    }
    if (fail_ > 0) {
      --fail_;
      return {.code = 500, .body = "error=emulated\n", .drop = false};
    }
    advance();
    const std::size_t question = target.find('?');
    const std::string path = target.substr(0, question);
    if (fail_path_count_ > 0 && path == fail_path_) {
      --fail_path_count_;
      return {.code = 500, .body = "error=emulated\n", .drop = false};
    }
    const Params params = parse(question == std::string::npos ? std::string() : target.substr(question + 1));
    if (!command(path, params)) {
      return {.code = 404, .body = "error=unknown command\n", .drop = false};
    }
    return {.code = 200, .body = path == "/status" && status_override_ ? *status_override_ : status(), .drop = false};
  }

  // Carries out a command; false for one it does not know.
  bool command(const std::string& path, const Params& params) {
    if (path == "/status") {
      sample_rate_error();
      ++seen_.statuses;
    } else if (path == "/mount/connect") {
      ++seen_.connects;
      seen_.connected = true;
    } else if (path == "/mount/enable") {
      ++seen_.enables;
      seen_.enabled[static_cast<std::size_t>(number(params, "axis")) % 2] = true;
    } else if (path == "/mount/stop") {
      ++seen_.stops;
      rates_ = {};
      slewing_ = false;
    } else if (path == "/mount/goto_alt_az") {
      goto_alt_az(number(params, "alt_degs"), number(params, "az_degs"));
    } else if (path == "/mount/park") {
      ++seen_.parks;
      targets_ = park_;
      rates_ = {};
      slewing_ = true;
    } else if (path == "/mount/offset") {
      offset(params);
    } else {
      return false;
    }
    return true;
  }

  void goto_alt_az(const double alt_deg, const double az_deg) {
    ++seen_.gotos;
    seen_.goto_alt_deg = alt_deg;
    seen_.goto_az_deg = az_deg;
    targets_ = {az_deg, alt_deg};
    rates_ = {};
    slewing_ = true;
  }

  void offset(const Params& params) {
    for (std::size_t i = 0; i < 2; ++i) {
      const std::string axis = "axis" + std::to_string(i);
      if (params.contains(axis + "_set_rate_arcsec_per_sec")) {
        rates_[i] = number(params, axis + "_set_rate_arcsec_per_sec") / 3600.0;
        seen_.max_abs_rate_deg_s[i] = std::max(seen_.max_abs_rate_deg_s[i], std::abs(rates_[i]));
      }
      if (params.contains(axis + "_stop_rate")) {
        rates_[i] = 0.0;
      }
    }
    seen_.last_rate_deg_s = rates_;
    seen_.rate_commands += params.contains("axis0_set_rate_arcsec_per_sec") ? 1 : 0;
    seen_.stop_rates += params.contains("axis0_stop_rate") ? 1 : 0;
  }

  // Moves the axes on to now.
  void advance() {
    const auto now = std::chrono::steady_clock::now();
    const double dt = std::chrono::duration<double>(now - last_).count();
    last_ = now;
    for (std::size_t i = 0; i < 2; ++i) {
      EmulatedAxis& axis = axes_[i];
      const double left = targets_[i] - axis.position_deg;
      // A slew comes to rest on its target: no faster than it can stop in
      // what is left, nor than reaches it within this step.
      const double arrival = std::min({axis.max_velocity_deg_s, std::sqrt(2.0 * axis.acceleration_deg_s2 * std::abs(left)),
                                       std::abs(left) / std::max(dt, 1e-3)});
      const double wanted = slewing_ ? std::copysign(arrival, left) : rates_[i];
      const double step = axis.acceleration_deg_s2 * dt;
      velocity_[i] = std::clamp(wanted, velocity_[i] - step, velocity_[i] + step);
      axis.position_deg = std::clamp(axis.position_deg + (velocity_[i] * dt), axis.min_deg, axis.max_deg);
      seen_.min_position_deg[i] = std::min(seen_.min_position_deg[i], axis.position_deg);
      seen_.max_position_deg[i] = std::max(seen_.max_position_deg[i], axis.position_deg);
    }
    slewing_ = slewing_ && (std::abs(targets_[0] - axes_[0].position_deg) > 1e-6 ||
                            std::abs(targets_[1] - axes_[1].position_deg) > 1e-6);
    seen_.closest_to_sun_deg =
        std::min(seen_.closest_to_sun_deg, separation(pointing_now(), sun_).value() * 180.0 / std::numbers::pi);
  }

  void sample_rate_error() {
    for (std::size_t i = 0; i < 2; ++i) {
      seen_.rate_error_sum_deg_s[i] += std::abs(velocity_[i] - rates_[i]);
    }
    ++seen_.rate_error_samples;
  }

  [[nodiscard]] std::string status() const {
    // PWI4 reports 0 while it is not connected to the mount.
    const double to_sun = seen_.connected ? separation(pointing_now(), sun_).value() * 180.0 / std::numbers::pi : 0.0;
    std::string out = "pwi4.version=4.1.9 beta 22\nmount.is_connected=" + flag(seen_.connected) +
                      "\nmount.is_slewing=" + flag(slewing_) +
                      "\nmount.is_tracking=false\nmount.distance_to_sun_degs=" + std::to_string(to_sun) + "\n";
    for (std::size_t i = 0; i < 2; ++i) {
      const std::string prefix = "mount.axis" + std::to_string(i) + ".";
      out += prefix + "is_enabled=" + flag(seen_.enabled[i]) + "\n" + prefix +
             "position_degs=" + std::to_string(axes_[i].position_deg) + "\n" + prefix +
             "measured_velocity_degs_per_sec=" + std::to_string(velocity_[i]) + "\n" + prefix +
             "min_mech_position_degs=" + std::to_string(axes_[i].min_deg) + "\n" + prefix +
             "max_mech_position_degs=" + std::to_string(axes_[i].max_deg) + "\n";
    }
    return out;
  }

  [[nodiscard]] AltAz pointing_now() const {
    constexpr double kRad = std::numbers::pi / 180.0;
    return {.altitude = Radians(axes_[1].position_deg * kRad), .azimuth = Radians(axes_[0].position_deg * kRad)};
  }

  [[nodiscard]] static std::string flag(const bool value) { return value ? "true" : "false"; }

  [[nodiscard]] static Params parse(const std::string& query) {
    Params out;
    std::size_t start = 0;
    while (start < query.size()) {
      const std::size_t end = std::min(query.find('&', start), query.size());
      const std::string pair = query.substr(start, end - start);
      const std::size_t equals = pair.find('=');
      out[pair.substr(0, equals)] = equals == std::string::npos ? std::string() : pair.substr(equals + 1);
      start = end + 1;
    }
    return out;
  }

  [[nodiscard]] static double number(const Params& params, const std::string& key) {
    const auto found = params.find(key);
    double out = 0.0;
    if (found != params.end()) {
      const std::string_view text = found->second;
      static_cast<void>(std::from_chars(text.begin(), text.end(), out));
    }
    return out;
  }

  int listen_fd_ = -1;
  std::uint16_t port_ = 0;
  std::thread thread_;
  std::atomic<bool> stopping_{false};
  std::vector<Client> clients_;
  mutable std::mutex mutex_;
  std::array<EmulatedAxis, 2> axes_;
  std::array<double, 2> velocity_{};
  std::array<double, 2> rates_{};
  std::array<double, 2> targets_{};
  // By axis: azimuth, then altitude, in degrees.
  std::array<double, 2> park_{0.0, 10.0};
  AltAz sun_{.altitude = Radians(-1.2), .azimuth = Radians(0.0)};
  bool slewing_ = false;
  std::chrono::steady_clock::time_point last_;
  std::uint32_t fail_ = 0;
  std::uint32_t drop_ = 0;
  std::string fail_path_;
  std::uint32_t fail_path_count_ = 0;
  std::chrono::milliseconds delay_{};
  std::optional<std::string> status_override_;
  Seen seen_;
};

}  // namespace ics::mount::testing
