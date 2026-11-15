// scpi_test.cpp: the 577's replies, and the numbers it is sent (ICS-040).
#include <chrono>

#include <gtest/gtest.h>

#include <ics/common/units.hpp>
#include <ics/trigger/scpi.h>

namespace
{

using ics::trigger::parse_reply;
using ics::trigger::Reply;
using ics::trigger::Reply_kind;
using ics::trigger::seconds_text;

constexpr ics::Duration nine_digits = std::chrono::nanoseconds(123456789);
constexpr ics::Duration just_over = std::chrono::nanoseconds(1000000005);
constexpr ics::Duration many_seconds = std::chrono::milliseconds(12500);

}  // namespace

// JSF deviation D-5: GoogleTest's TEST and EXPECT macros fix the form of the
// test names and expand to code of GoogleTest's own (AV 50, 51 and 209).
TEST(Scpi, ACommandTakenIsAnsweredOk)
{
  EXPECT_EQ(parse_reply("ok\r\n").kind, Reply_kind::ok);
  EXPECT_EQ(parse_reply("ok\n").kind, Reply_kind::ok);
  EXPECT_EQ(parse_reply("ok\r").kind, Reply_kind::ok);
  EXPECT_EQ(parse_reply("ok").kind, Reply_kind::ok);
}

TEST(Scpi, ARefusalCarriesItsCode)
{
  const Reply first = parse_reply("?1\r\n");
  EXPECT_EQ(first.kind, Reply_kind::refused);
  EXPECT_EQ(first.code, 1);
  const Reply last = parse_reply("?9\r\n");
  EXPECT_EQ(last.kind, Reply_kind::refused);
  EXPECT_EQ(last.code, 9);
}

TEST(Scpi, AQuestionMarkWithoutACodeIsNotAReply)
{
  EXPECT_EQ(parse_reply("?\r\n").kind, Reply_kind::malformed);
  EXPECT_EQ(parse_reply("?0\r\n").kind, Reply_kind::malformed);
  EXPECT_EQ(parse_reply("?:\r\n").kind, Reply_kind::malformed);
  EXPECT_EQ(parse_reply("?12\r\n").kind, Reply_kind::malformed);
  EXPECT_EQ(parse_reply("?5\r\n").code, 5);
  EXPECT_EQ(parse_reply("?0\r\n").code, 0);
}

TEST(Scpi, AQueryIsAnsweredWithItsValue)
{
  const Reply reply = parse_reply("BNC,577-8C,31309,2.4.1-1.0.11\r\n");
  EXPECT_EQ(reply.kind, Reply_kind::value);
  EXPECT_EQ(reply.text, "BNC,577-8C,31309,2.4.1-1.0.11");
}

TEST(Scpi, AnEmptyOrUnprintableLineIsNotAReply)
{
  EXPECT_EQ(parse_reply("").kind, Reply_kind::malformed);
  EXPECT_EQ(parse_reply("\r\n").kind, Reply_kind::malformed);
  EXPECT_EQ(parse_reply("ok\x01\r\n").kind, Reply_kind::malformed);
  EXPECT_EQ(parse_reply("ok\x7F\r\n").kind, Reply_kind::malformed);
  EXPECT_EQ(parse_reply("o\x01k\r\n").kind, Reply_kind::malformed);
}

TEST(Scpi, ASpanIsSentAsSecondsToTheNanosecond)
{
  EXPECT_EQ(seconds_text(ics::Duration::zero()), "0.000000000");
  EXPECT_EQ(seconds_text(nine_digits), "0.123456789");
  EXPECT_EQ(seconds_text(just_over), "1.000000005");
  EXPECT_EQ(seconds_text(many_seconds), "12.500000000");
  EXPECT_EQ(seconds_text(-nine_digits), "0.000000000");
}
