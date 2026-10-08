#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/config/logging_config.hpp"
#include "ics/config/reader.hpp"

namespace ics::plid {

// The range ENU frame's origin: the defended asset, as RunRecord.range_origin.
struct RangeOrigin {
  Degrees latitude{0.0};
  Degrees longitude{0.0};
  // Above the WGS84 ellipsoid.
  Meters height{0.0};
};

// The MAVLink feed (ICS-021): every UDP datagram that no other feed claims.
struct MavlinkConfig {
  // SYSTEM=ROLE, such as "1=target"; a system not listed is ENTITY_ROLE_OTHER.
  std::vector<std::string> roles;
  Duration link_timeout{};
  Duration max_age{};
};

// The CoT feed (ICS-022): UDP datagrams to one port.
struct CotConfig {
  std::uint16_t port = 0;
  // UID=ROLE, split at the last '='.
  std::vector<std::string> roles;
  double ce_probability = 0.0;
  double le_probability = 0.0;
  Duration max_skew{};
};

// The SAPIENT feed (ICS-024): ics-plid connects to a SAPIENT middleware over
// TCP and reads its detection reports.
struct SapientConfig {
  // An IPv4 address, such as "10.0.0.5".
  std::string address;
  std::uint16_t port = 0;
  // ID=ROLE: a detection's object_id or id.
  std::vector<std::string> roles;
  Duration max_skew{};
};

// The Lattice feed (ICS-023): the entity stream, read in a thread of its own.
struct LatticeConfig {
  std::string url;
  std::filesystem::path token_file;
  // Only for a Lattice Sandbox.
  std::optional<std::filesystem::path> sandbox_token_file;
  // ENTITY_ID=ROLE.
  std::vector<std::string> roles;
  Duration max_skew{};
};

// The settings of ics-plid (ICS-030), from its TOML file:
//
//   [log]
//   level = "info"
//   service = "ics-plid"
//
//   [plid]
//   store_folder = "/var/lib/ics/pli"         # the segment log and its archive
//   query_socket = "/run/ics-plid/query"      # the PliQueryService socket
//   rotate_interval_ns = 3_600_000_000_000    # 1 min to 1 day
//   sync_interval_ns = 1_000_000_000          # 10 ms to 10 s
//   feeds = ["mavlink", "cot"]                # any of mavlink, cot, sapient, lattice
//
//   [range]                                   # the range ENU frame's origin
//   latitude_deg = 32.9
//   longitude_deg = -106.4
//   height_m = 1200.0                         # above the WGS84 ellipsoid
//
//   [capture]                                 # where the UDP feeds come from
//   interfaces = ["ics-tap0"]                 # TAP ports, captured live, or
//   files = []                                # pcap files replayed, for tests
//
//   [mavlink]                                 # only with "mavlink" in feeds
//   roles = ["1=target", "2=interceptor"]
//   link_timeout_ns = 3_000_000_000
//   max_age_ns = 1_000_000_000
//
//   [cot]                                     # only with "cot"
//   port = 6969
//   roles = ["ANDROID-1234=interceptor"]
//   ce_probability = 0.9
//   le_probability = 0.9
//   max_skew_ns = 30_000_000_000
//
//   [sapient]                                 # only with "sapient"
//   address = "10.0.0.5"
//   port = 5020
//   roles = []
//   max_skew_ns = 30_000_000_000
//
//   [lattice]                                 # only with "lattice"
//   url = "https://lattice.example.com"
//   token_file = "/etc/ics/lattice-token"
//   sandbox = false                           # true adds sandbox_token_file
//   roles = []
//   max_skew_ns = 30_000_000_000
struct Config {
  config::LoggingConfig log;
  std::filesystem::path store_folder;
  std::filesystem::path query_socket;
  Duration rotate_interval{};
  Duration sync_interval{};
  std::vector<std::string> feeds;
  RangeOrigin range;
  std::vector<std::string> interfaces;
  std::vector<std::filesystem::path> files;
  std::optional<MavlinkConfig> mavlink;
  std::optional<CotConfig> cot;
  std::optional<SapientConfig> sapient;
  std::optional<LatticeConfig> lattice;
};

// The most TAP ports or files one ics-plid captures, and roles per feed.
inline constexpr std::size_t kMaxCaptures = 4;
inline constexpr std::size_t kMaxRoles = 256;

// Reads the [log], [plid], [range] and [capture] tables under root, and the
// table of each feed listed.
[[nodiscard]] Config read_config(config::Reader& root);

// Checks what one table cannot: each feed is known and listed once; and the
// UDP feeds, MAVLink and CoT, have TAP ports or files to read, but not both.
// kInvalidArgument with an explanation in reason.
[[nodiscard]] Status validate(const Config& config, std::string& reason);

}  // namespace ics::plid
