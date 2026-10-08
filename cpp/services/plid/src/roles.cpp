#include "ics/plid/roles.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "ics/common/check.hpp"

namespace ics::plid {
namespace {

struct RoleName {
  std::string_view name;
  v1::EntityRole role;
};

constexpr unsigned kMaxSystem = 255;

constexpr std::array<RoleName, 4> kRoles{{
    {"target", v1::ENTITY_ROLE_TARGET},
    {"interceptor", v1::ENTITY_ROLE_INTERCEPTOR},
    {"debris", v1::ENTITY_ROLE_DEBRIS},
    {"other", v1::ENTITY_ROLE_OTHER},
}};

}  // namespace

std::optional<v1::EntityRole> role_named(const std::string_view name) noexcept {
  const auto found = std::ranges::find_if(kRoles, [name](const RoleName& known) { return known.name == name; });
  return found == kRoles.end() ? std::nullopt : std::optional(found->role);
}

Result<std::vector<NamedRole>> parse_roles(const std::vector<std::string>& texts, std::string& reason) {
  std::vector<NamedRole> out;
  for (const std::string& text : texts) {
    const std::size_t equals = text.rfind('=');
    const std::optional<v1::EntityRole> role =
        equals == std::string::npos || equals == 0 ? std::nullopt : role_named(std::string_view(text).substr(equals + 1));
    if (!role) {
      reason = "a role must be ID=ROLE, with ROLE one of target, interceptor, debris and other: " + text;
      return fail(Error::kInvalidArgument);
    }
    static_cast<void>(ics::check(equals < text.size()));
    out.push_back({.id = text.substr(0, equals), .role = *role});
  }
  return out;
}

Result<std::vector<mavlink::RoleAssignment>> mavlink_roles(const std::vector<std::string>& texts,
                                                                       std::string& reason) {
  return parse_roles(texts, reason).and_then([&reason](const std::vector<NamedRole>& roles) {
    std::vector<mavlink::RoleAssignment> out;
    for (const NamedRole& named : roles) {
      unsigned system = 0;
      const std::string_view id = named.id;
      const std::from_chars_result read = std::from_chars(id.begin(), id.end(), system);
      if (read.ec != std::errc() || read.ptr != id.end() || system > kMaxSystem) {
        reason = "a MAVLink role's ID must be a system number from 0 to 255: " + named.id;
        return Result<std::vector<mavlink::RoleAssignment>>(fail(Error::kInvalidArgument));
      }
      out.push_back({.system = static_cast<std::uint8_t>(system), .role = named.role});
    }
    return Result<std::vector<mavlink::RoleAssignment>>(std::move(out));
  });
}

}  // namespace ics::plid
