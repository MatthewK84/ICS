// link_test.cpp: the TCP link to a 577, against one on a local port (ICS-040).
#include <chrono>
#include <memory>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include <emulated_577.h>
#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/trigger/generator.h>
#include <ics/trigger/link.h>
#include <ics/trigger/scpi.h>
#include <scpi_server.h>

namespace
{

using ics::trigger::Link_settings;
using ics::trigger::Tcp_link;
using ics::trigger_test::Manner;
using ics::trigger_test::Scpi_server;

constexpr ics::Duration timeout = std::chrono::milliseconds(50);
constexpr ics::Duration width = std::chrono::microseconds(100);
constexpr ics::float64 level_v = 2.5;

// A link to a server on this host.
std::unique_ptr<Tcp_link> link_to(const Scpi_server& server)
{
  ics::Result<std::unique_ptr<Tcp_link>> link = Tcp_link::open(Link_settings{"127.0.0.1", server.port(), timeout});
  EXPECT_TRUE(link.has_value());
  return std::move(*link);
}

}  // namespace

// JSF deviation D-5: GoogleTest's TEST and EXPECT macros fix the form of the
// test names and expand to code of GoogleTest's own (AV 50, 51 and 209).
TEST(Link, AsksA577AndReadsItsReply)
{
  Scpi_server server;
  const std::unique_ptr<Tcp_link> link = link_to(server);
  const ics::Result<std::string> identity = link->ask("*IDN?");
  ASSERT_TRUE(identity.has_value());
  EXPECT_EQ(ics::trigger::parse_reply(*identity).text, ics::trigger_test::identity);
  const ics::Result<std::string> taken = link->ask(":PULSE0:STATE OFF");
  ASSERT_TRUE(taken.has_value());
  EXPECT_EQ(ics::trigger::parse_reply(*taken).kind, ics::trigger::Reply_kind::ok);
}

TEST(Link, CarriesTheDriversSetUp)
{
  Scpi_server server;
  const std::unique_ptr<Tcp_link> link = link_to(server);
  ics::trigger::Bnc577 generator(*link, ics::trigger::Bnc577_settings{{1}, 1, width, level_v});
  ASSERT_TRUE(generator.set_up().has_value());
  EXPECT_EQ(generator.identity(), ics::trigger_test::identity);
  EXPECT_EQ(server.commands().back(), ":PULSE1:STATE ON");
}

TEST(Link, IsNotMadeForAnAddressThatIsNotIpv4)
{
  const ics::Result<std::unique_ptr<Tcp_link>> link = Tcp_link::open(Link_settings{"the-577", 1, timeout});
  EXPECT_EQ(link.error(), ics::Error::kInvalidArgument);
}

TEST(Link, IsNotMadeWhereNothingListens)
{
  const ics::Result<std::unique_ptr<Tcp_link>> link =
    Tcp_link::open(Link_settings{"127.0.0.1", ics::trigger_test::closed_port(), timeout});
  EXPECT_EQ(link.error(), ics::Error::kUnavailable);
}

TEST(Link, GivesUpOnA577ThatSaysNothing)
{
  Scpi_server server;
  server.manner.store(Manner::silent);
  const std::unique_ptr<Tcp_link> link = link_to(server);
  EXPECT_EQ(link->ask("*IDN?").error(), ics::Error::kUnavailable);
}

TEST(Link, GivesUpOnA577ThatHangsUp)
{
  Scpi_server server;
  server.manner.store(Manner::hanging_up);
  const std::unique_ptr<Tcp_link> link = link_to(server);
  EXPECT_EQ(link->ask("*IDN?").error(), ics::Error::kUnavailable);
}

TEST(Link, GivesUpOnAReplyThatNeverEnds)
{
  Scpi_server server;
  server.manner.store(Manner::rambling);
  const std::unique_ptr<Tcp_link> link = link_to(server);
  EXPECT_EQ(link->ask("*IDN?").error(), ics::Error::kUnavailable);
}
