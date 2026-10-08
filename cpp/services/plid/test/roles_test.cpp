#include "ics/plid/roles.hpp"

#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

using ics::plid::NamedRole;
using ics::plid::parse_roles;

TEST(Roles, NamesTheFourRoles) {
  EXPECT_EQ(ics::plid::role_named("target"), ics::v1::ENTITY_ROLE_TARGET);
  EXPECT_EQ(ics::plid::role_named("interceptor"), ics::v1::ENTITY_ROLE_INTERCEPTOR);
  EXPECT_EQ(ics::plid::role_named("debris"), ics::v1::ENTITY_ROLE_DEBRIS);
  EXPECT_EQ(ics::plid::role_named("other"), ics::v1::ENTITY_ROLE_OTHER);
  EXPECT_FALSE(ics::plid::role_named("friend").has_value());
}

TEST(Roles, SplitsAtTheLastEquals) {
  std::string reason;
  const std::vector<NamedRole> roles = parse_roles({"1=target", "uid=with=equals=debris"}, reason).value();
  ASSERT_EQ(roles.size(), 2U);
  EXPECT_EQ(roles[0].id, "1");
  EXPECT_EQ(roles[0].role, ics::v1::ENTITY_ROLE_TARGET);
  EXPECT_EQ(roles[1].id, "uid=with=equals");
  EXPECT_EQ(roles[1].role, ics::v1::ENTITY_ROLE_DEBRIS);
}

TEST(Roles, RejectsATextThatIsNotIdEqualsRole) {
  for (const char* text : {"target", "=target", "1=friend"}) {
    std::string reason;
    EXPECT_EQ(parse_roles({text}, reason).error(), ics::Error::kInvalidArgument) << text;
    EXPECT_NE(reason.find(text), std::string::npos);
  }
}

}  // namespace
