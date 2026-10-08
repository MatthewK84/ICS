#include "ics/plid/feeds.hpp"

#include <charconv>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "ics/lattice/token.hpp"
#include "ics/plid/roles.hpp"

namespace ics::plid {
namespace {

constexpr unsigned kMaxSystem = 255;

[[nodiscard]] Result<std::vector<mavlink::RoleAssignment>> mavlink_roles(const std::vector<std::string>& texts,
                                                                       std::string& reason) {
  return parse_roles(texts, reason).and_then([&reason](const std::vector<NamedRole>& roles) {
    std::vector<mavlink::RoleAssignment> out;
    for (const NamedRole& named : roles) {
      unsigned system = 0;
      const std::from_chars_result read = std::from_chars(named.id.data(), named.id.data() + named.id.size(), system);
      if (read.ec != std::errc() || read.ptr != named.id.data() + named.id.size() || system > kMaxSystem) {
        reason = "a MAVLink role's ID must be a system number from 0 to 255: " + named.id;
        return Result<std::vector<mavlink::RoleAssignment>>(fail(Error::kInvalidArgument));
      }
      out.push_back({.system = static_cast<std::uint8_t>(system), .role = named.role});
    }
    return Result<std::vector<mavlink::RoleAssignment>>(std::move(out));
  });
}

// The roles in the form each text-keyed adapter takes.
template <typename Assignment>
[[nodiscard]] Result<std::vector<Assignment>> named_roles(const std::vector<std::string>& texts, std::string& reason) {
  return parse_roles(texts, reason).map([](const std::vector<NamedRole>& roles) {
    std::vector<Assignment> out;
    for (const NamedRole& named : roles) {
      out.push_back({named.id, named.role});
    }
    return out;
  });
}

[[nodiscard]] Result<std::optional<mavlink::Adapter>> open_mavlink(const std::optional<MavlinkConfig>& config,
                                                                 const frames::Egm96& geoid,
                                                                 const frames::EnuFrame& range, std::string& reason) {
  if (!config) {
    return std::optional<mavlink::Adapter>{};
  }
  return mavlink_roles(config->roles, reason).map([&](std::vector<mavlink::RoleAssignment> roles) {
    mavlink::AdapterSettings settings{
        .roles = std::move(roles), .link_timeout = config->link_timeout, .max_age = config->max_age};
    return std::optional<mavlink::Adapter>(std::in_place, std::move(settings), geoid, range);
  });
}

[[nodiscard]] Result<std::optional<CotRoute>> open_cot(const std::optional<CotConfig>& config,
                                                      const frames::EnuFrame& range, std::string& reason) {
  if (!config) {
    return std::optional<CotRoute>{};
  }
  return named_roles<cot::RoleAssignment>(config->roles, reason).and_then([&](std::vector<cot::RoleAssignment> roles) {
    cot::AdapterSettings settings{.roles = std::move(roles),
                                  .ce_probability = config->ce_probability,
                                  .le_probability = config->le_probability,
                                  .max_skew = config->max_skew};
    return cot::Adapter::make(std::move(settings), range).map([&config](cot::Adapter adapter) {
      return std::optional<CotRoute>(CotRoute{.port = config->port, .adapter = std::move(adapter)});
    });
  });
}

[[nodiscard]] Result<std::optional<SapientLink>> open_sapient(const std::optional<SapientConfig>& config,
                                                             const frames::Egm96& geoid,
                                                             const frames::EnuFrame& range, std::string& reason) {
  if (!config) {
    return std::optional<SapientLink>{};
  }
  return named_roles<sapient::RoleAssignment>(config->roles, reason)
      .and_then([&](std::vector<sapient::RoleAssignment> roles) {
        sapient::AdapterSettings settings{.roles = std::move(roles), .max_skew = config->max_skew};
        return sapient::Adapter::make(std::move(settings), geoid, range);
      })
      .and_then([&](sapient::Adapter adapter) {
        return SapientLink::make(config->address, config->port, std::move(adapter), reason);
      })
      .map([](SapientLink link) { return std::optional<SapientLink>(std::move(link)); });
}

// A token from its file; reason names the file it could not use.
[[nodiscard]] Result<std::string> token(const std::filesystem::path& path, std::string& reason) {
  return lattice::read_token(path).map_error([&](const Error error) {
    reason = "cannot use the Lattice token file " + path.native();
    return error;
  });
}

[[nodiscard]] Result<lattice::ClientSettings> lattice_settings(const LatticeConfig& config, std::string& reason) {
  lattice::ClientSettings settings;
  settings.url = config.url;
  const Result<std::string> sandbox =
      config.sandbox_token_file ? token(*config.sandbox_token_file, reason) : Result<std::string>(std::string());
  return sandbox.and_then([&](std::string sandbox_token) {
    settings.sandbox_token = std::move(sandbox_token);
    return token(config.token_file, reason);
  }).map([&settings](std::string main_token) {
    settings.token = std::move(main_token);
    return std::move(settings);
  });
}

[[nodiscard]] Result<lattice::StreamClient> lattice_client(const LatticeConfig& config, std::string& reason) {
  return lattice_settings(config, reason).and_then([&reason](lattice::ClientSettings settings) {
    return lattice::StreamClient::make(std::move(settings)).map_error([&reason](const Error error) {
      reason = "the Lattice url is not valid";
      return error;
    });
  });
}

[[nodiscard]] Result<std::unique_ptr<LatticeFeed>> open_lattice(const std::optional<LatticeConfig>& config,
                                                               const frames::EnuFrame& range,
                                                               const logging::Logger& logger, std::string& reason) {
  if (!config) {
    return std::unique_ptr<LatticeFeed>();
  }
  Result<lattice::Adapter> adapter =
      named_roles<lattice::RoleAssignment>(config->roles, reason).and_then([&](std::vector<lattice::RoleAssignment> roles) {
        return lattice::Adapter::make({.roles = std::move(roles), .max_skew = config->max_skew}, range);
      });
  Result<lattice::StreamClient> client = adapter.and_then([&](auto&) { return lattice_client(*config, reason); });
  return client.map([&](lattice::StreamClient& made) {
    return std::make_unique<LatticeFeed>(std::move(made), std::move(*adapter), logger);
  });
}

[[nodiscard]] Result<std::vector<Source>> open_sources(const Config& config, std::string& reason) {
  std::vector<Source> out;
  for (const std::string& interface : config.interfaces) {
    const capture::CaptureOptions options{.interface = interface,
                                          .snaplen = kSnaplen,
                                          .buffer_bytes = kCaptureBufferBytes,
                                          .timestamps = capture::TimestampSource::kHost,
                                          .stamp_minus_utc = {}};
    Result<capture::Capture> opened = capture::Capture::open_live(options, reason);
    if (!opened) {
      return fail(opened.error());
    }
    out.push_back({.capture = std::move(*opened), .file = false, .done = false});
  }
  for (const std::filesystem::path& file : config.files) {
    Result<capture::Capture> opened = capture::Capture::open_file(file, reason);
    if (!opened) {
      return fail(opened.error());
    }
    out.push_back({.capture = std::move(*opened), .file = true, .done = false});
  }
  return out;
}

}  // namespace

Result<Feeds> open_feeds(const Config& config, const frames::Egm96& geoid, const frames::EnuFrame& range,
                         const logging::Logger& logger, std::string& reason) {
  Result<std::optional<mavlink::Adapter>> mavlink = open_mavlink(config.mavlink, geoid, range, reason);
  Result<std::optional<CotRoute>> cot = mavlink.and_then([&](auto&) { return open_cot(config.cot, range, reason); });
  Result<std::optional<SapientLink>> sapient =
      cot.and_then([&](auto&) { return open_sapient(config.sapient, geoid, range, reason); });
  Result<std::vector<Source>> sources = sapient.and_then([&](auto&) { return open_sources(config, reason); });
  Result<std::unique_ptr<LatticeFeed>> lattice =
      sources.and_then([&](auto&) { return open_lattice(config.lattice, range, logger, reason); });
  return lattice.map([&](std::unique_ptr<LatticeFeed>& feed) {
    return Feeds{.router = Router(std::move(*mavlink), std::move(*cot)),
                 .sources = std::move(*sources),
                 .live = !config.interfaces.empty(),
                 .sapient = std::move(*sapient),
                 .lattice = std::move(feed)};
  });
}

}  // namespace ics::plid
