#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ics/common/error.hpp"
#include "ics/mavlink/adapter.hpp"
#include "ics/v1/common.pb.h"

namespace ics::plid {

// One ID=ROLE setting (ICS-030): what a feed calls a vehicle, and its role.
struct NamedRole {
  std::string id;
  v1::EntityRole role = v1::ENTITY_ROLE_UNSPECIFIED;
};

// The role named target, interceptor, debris or other.
[[nodiscard]] std::optional<v1::EntityRole> role_named(std::string_view name) noexcept;

// Each ID=ROLE text, split at its last '='. kInvalidArgument, naming the text
// in reason, for one with an empty ID or a role role_named does not know.
[[nodiscard]] Result<std::vector<NamedRole>> parse_roles(const std::vector<std::string>& texts, std::string& reason);

// Each SYSTEM=ROLE text, for the MAVLink adapter. Fails as parse_roles does,
// and for an ID that is not a system number from 0 to 255.
[[nodiscard]] Result<std::vector<mavlink::RoleAssignment>> mavlink_roles(const std::vector<std::string>& texts,
                                                                       std::string& reason);

}  // namespace ics::plid
