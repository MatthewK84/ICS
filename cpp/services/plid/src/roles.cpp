#include "ics/plid/roles.hpp"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ics/common/check.hpp"

namespace ics::plid {
namespace {

struct RoleName {
  std::string_view name;
  v1::EntityRole role;
};

constexpr std::array<RoleName, 4> kRoles{{
    {"target", v1::ENTITY_ROLE_TARGET},
    {"interceptor", v1::ENTITY_ROLE_INTERCEPTOR},
    {"debris", v1::ENTITY_ROLE_DEBRIS},
    {"other", v1::ENTITY_ROLE_OTHER},
}};

}  // namespace

std::optional<v1::EntityRole> role_named(const std::string_view name) noexcept {
  for (const RoleName& known : kRoles) {
    if (known.name == name) {
      return known.role;
    }
  }
  return std::nullopt;
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

}  // namespace ics::plid
