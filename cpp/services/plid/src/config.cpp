#include "ics/plid/config.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace ics::plid {
namespace {

constexpr Duration kMinRotateInterval = std::chrono::minutes(1);
constexpr Duration kMaxRotateInterval = std::chrono::hours(24);
constexpr Duration kMinSyncInterval = std::chrono::milliseconds(10);
constexpr Duration kMaxSyncInterval = std::chrono::seconds(10);
constexpr Duration kMaxTimeout = std::chrono::minutes(10);
constexpr Duration kMaxSkew = std::chrono::hours(1);
constexpr double kMaxLatitude = 90.0;
constexpr double kMaxLongitude = 180.0;
constexpr double kMinHeight = -1'000.0;
constexpr double kMaxHeight = 10'000.0;
constexpr double kMinProbability = 0.01;
constexpr double kMaxProbability = 0.99;
constexpr std::int64_t kMaxPort = std::numeric_limits<std::uint16_t>::max();
constexpr std::array<std::string_view, 4> kFeeds{"mavlink", "cot", "sapient", "lattice"};
constexpr std::size_t kMaxFeeds = kFeeds.size();

[[nodiscard]] std::uint16_t port(config::Reader& table) {
  return static_cast<std::uint16_t>(table.integer("port", 1, kMaxPort));
}

[[nodiscard]] MavlinkConfig read_mavlink(config::Reader& root) {
  config::Reader table = root.section("mavlink");
  return {.roles = table.texts("roles", 0, kMaxRoles),
          .link_timeout = table.duration("link_timeout_ns", std::chrono::milliseconds(100), kMaxTimeout),
          .max_age = table.duration("max_age_ns", std::chrono::milliseconds(1), kMaxTimeout)};
}

[[nodiscard]] CotConfig read_cot(config::Reader& root) {
  config::Reader table = root.section("cot");
  CotConfig out;
  out.port = port(table);
  out.roles = table.texts("roles", 0, kMaxRoles);
  out.ce_probability = table.number("ce_probability", kMinProbability, kMaxProbability);
  out.le_probability = table.number("le_probability", kMinProbability, kMaxProbability);
  out.max_skew = table.duration("max_skew_ns", Duration(0), kMaxSkew);
  return out;
}

[[nodiscard]] SapientConfig read_sapient(config::Reader& root) {
  config::Reader table = root.section("sapient");
  SapientConfig out;
  out.address = table.text("address");
  out.port = port(table);
  out.roles = table.texts("roles", 0, kMaxRoles);
  out.max_skew = table.duration("max_skew_ns", Duration(0), kMaxSkew);
  return out;
}

[[nodiscard]] LatticeConfig read_lattice(config::Reader& root) {
  config::Reader table = root.section("lattice");
  LatticeConfig out;
  out.url = table.text("url");
  out.token_file = table.text("token_file");
  if (table.boolean("sandbox")) {
    out.sandbox_token_file = table.text("sandbox_token_file");
  }
  out.roles = table.texts("roles", 0, kMaxRoles);
  out.max_skew = table.duration("max_skew_ns", Duration(0), kMaxSkew);
  return out;
}

[[nodiscard]] RangeOrigin read_range(config::Reader& root) {
  config::Reader table = root.section("range");
  return {.latitude = table.degrees("latitude_deg", Degrees(-kMaxLatitude), Degrees(kMaxLatitude)),
          .longitude = table.degrees("longitude_deg", Degrees(-kMaxLongitude), Degrees(kMaxLongitude)),
          .height = table.meters("height_m", Meters(kMinHeight), Meters(kMaxHeight))};
}

void read_capture(config::Reader& root, Config& out) {
  config::Reader table = root.section("capture");
  out.interfaces = table.texts("interfaces", 0, kMaxCaptures);
  const std::vector<std::string> files = table.texts("files", 0, kMaxCaptures);
  out.files.assign(files.begin(), files.end());
}

[[nodiscard]] bool listed(const std::vector<std::string>& feeds, const std::string_view feed) {
  return std::ranges::find(feeds, feed) != feeds.end();
}

}  // namespace

Config read_config(config::Reader& root) {
  Config out;
  out.log = config::read_logging(root);
  config::Reader plid = root.section("plid");
  out.store_folder = plid.text("store_folder");
  out.query_socket = plid.text("query_socket");
  out.rotate_interval = plid.duration("rotate_interval_ns", kMinRotateInterval, kMaxRotateInterval);
  out.sync_interval = plid.duration("sync_interval_ns", kMinSyncInterval, kMaxSyncInterval);
  out.feeds = plid.texts("feeds", 1, kMaxFeeds);
  out.range = read_range(root);
  read_capture(root, out);
  if (listed(out.feeds, "mavlink")) {
    out.mavlink = read_mavlink(root);
  }
  if (listed(out.feeds, "cot")) {
    out.cot = read_cot(root);
  }
  if (listed(out.feeds, "sapient")) {
    out.sapient = read_sapient(root);
  }
  if (listed(out.feeds, "lattice")) {
    out.lattice = read_lattice(root);
  }
  return out;
}

Status validate(const Config& config, std::string& reason) {
  const std::set<std::string> distinct(config.feeds.begin(), config.feeds.end());
  const bool known = std::ranges::all_of(config.feeds, [](const std::string& feed) {
    return std::ranges::find(kFeeds, feed) != kFeeds.end();
  });
  if (!known || distinct.size() != config.feeds.size()) {
    reason = "feeds may only list mavlink, cot, sapient and lattice, each once";
    return fail(Error::kInvalidArgument);
  }
  const bool udp = config.mavlink.has_value() || config.cot.has_value();
  const std::size_t sources = static_cast<std::size_t>(!config.interfaces.empty()) +
                              static_cast<std::size_t>(!config.files.empty());
  if (sources > 1 || (udp && sources == 0)) {
    reason = "the mavlink and cot feeds need capture interfaces or files, and not both";
    return fail(Error::kInvalidArgument);
  }
  return {};
}

}  // namespace ics::plid
