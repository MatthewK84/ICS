#include "ics/flightlog/ulog.hpp"

#include <sys/mman.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "ics/common/error.hpp"
#include "ulog_builder.hpp"

namespace ics::flightlog {
namespace {

using testing::sample;
using testing::ULogBuilder;

constexpr std::string_view kPosition =
    "pos:uint64_t timestamp;double lat;float[3] v;int8_t i8;uint16_t u16;int32_t i32;uint32_t u32;int64_t i64;"
    "bool ok;char[4] name;uint8_t[2] _padding0;";

std::vector<std::byte> position_sample() {
  return sample(std::uint64_t{1'000'000}, 40.5, 1.0F, 2.0F, 3.0F, std::int8_t{-8}, std::uint16_t{16}, std::int32_t{-32},
                std::uint32_t{32}, std::int64_t{-64}, std::uint8_t{1}, std::array<char, 4>{'a', 'b', 'c', 'd'});
}

ULog parse(const ULogBuilder& builder) {
  Result<ULog> log = ULog::parse(builder.bytes());
  EXPECT_TRUE(log.has_value());
  return std::move(log).value();
}

ULogField field(const ULog& log, const std::string& topic, const std::string& name) {
  return find_field(log.format(topic)->get(), name).value();
}

TEST(ULog, ReadsFieldsOfEveryType) {
  ULogBuilder builder;
  builder.format(kPosition).add_logged(0, 3, "pos").data(3, position_sample());
  const ULog log = parse(builder);
  const std::vector<std::span<const std::byte>> samples = log.samples("pos", 0);
  ASSERT_EQ(samples.size(), 1U);
  const ULogFormat& format = log.format("pos")->get();
  EXPECT_EQ(format.size, 8U + 8U + 12U + 1U + 2U + 4U + 4U + 8U + 1U + 4U + 2U);
  EXPECT_EQ(read_integer(field(log, "pos", "timestamp"), samples[0]), 1'000'000);
  EXPECT_EQ(read(field(log, "pos", "lat"), samples[0]), 40.5);
  EXPECT_EQ(read(field(log, "pos", "v"), samples[0], 2), 3.0);
  EXPECT_EQ(read(field(log, "pos", "v"), samples[0], 3), std::nullopt);
  EXPECT_EQ(read(field(log, "pos", "i8"), samples[0]), -8.0);
  EXPECT_EQ(read(field(log, "pos", "u16"), samples[0]), 16.0);
  EXPECT_EQ(read(field(log, "pos", "i32"), samples[0]), -32.0);
  EXPECT_EQ(read(field(log, "pos", "u32"), samples[0]), 32.0);
  EXPECT_EQ(read(field(log, "pos", "i64"), samples[0]), -64.0);
  EXPECT_EQ(read(field(log, "pos", "ok"), samples[0]), 1.0);
  EXPECT_EQ(read(field(log, "pos", "name"), samples[0]), std::nullopt);
  EXPECT_EQ(read_integer(field(log, "pos", "lat"), samples[0]), std::nullopt);
  EXPECT_EQ(find_field(format, "absent"), std::nullopt);
}

TEST(ULog, LeavesOutTrailingPaddingAndReadsBooleansAsZeroOrOne) {
  ULogBuilder builder;
  std::vector<std::byte> short_sample = position_sample();
  short_sample[8 + 8 + 12 + 1 + 2 + 4 + 4 + 8] = std::byte{7};
  builder.format(kPosition).add_logged(0, 3, "pos").data(3, short_sample);
  const ULog log = parse(builder);
  const std::span<const std::byte> logged = log.samples("pos", 0).at(0);
  EXPECT_EQ(logged.size(), log.format("pos")->get().size - 2);
  EXPECT_EQ(read(field(log, "pos", "ok"), logged), 1.0);
  EXPECT_EQ(read(field(log, "pos", "_padding0"), logged), std::nullopt);
}

TEST(ULog, ReadsTheLargestIntegersExactlyOrNotAtAll) {
  ULogBuilder builder;
  builder.format("big:uint64_t a;uint64_t b;").add_logged(0, 0, "big");
  builder.data(0, sample(std::uint64_t{(std::uint64_t{1} << 63U) - 1}, std::uint64_t{1} << 63U));
  const ULog log = parse(builder);
  const std::span<const std::byte> logged = log.samples("big", 0).at(0);
  EXPECT_EQ(read_integer(field(log, "big", "a"), logged), std::numeric_limits<std::int64_t>::max());
  EXPECT_EQ(read_integer(field(log, "big", "b"), logged), std::nullopt);
  EXPECT_EQ(read(field(log, "big", "b"), logged), std::nullopt);
}

TEST(ULog, LaysOutNestedFormatsWhateverTheirOrder) {
  ULogBuilder builder;
  builder.format("outer:uint64_t timestamp;inner[2] parts;float after;").format("inner:float a;uint8_t b;");
  builder.format("deepest:outer o;");
  const ULog log = parse(builder);
  const ULogFormat& outer = log.format("outer")->get();
  EXPECT_EQ(outer.size, 8U + (2U * 5U) + 4U);
  const ULogField parts = find_field(outer, "parts").value();
  EXPECT_EQ(parts.type, ULogType::kNested);
  EXPECT_EQ(parts.nested, "inner");
  EXPECT_EQ(parts.offset, 8U);
  EXPECT_EQ(find_field(outer, "after")->offset, 18U);
  EXPECT_EQ(read(parts, std::span<const std::byte>()), std::nullopt);
  EXPECT_EQ(log.format("deepest")->get().size, outer.size);
  EXPECT_EQ(log.counts().unresolved_formats, 0U);
}

TEST(ULog, CannotLayOutUndefinedCircularOrHugeFormats) {
  ULogBuilder builder;
  builder.format("missing:absent a;").format("loop:loop again;").format("a:b x;").format("b:a y;");
  builder.format("wide:uint8_t[65535] w;").format("wider:wide[17] x;").format("user:missing m;float f;");
  const ULog log = parse(builder);
  for (const std::string name : {"missing", "loop", "a", "b", "wider", "user"}) {
    EXPECT_EQ(log.format(name), std::nullopt) << name;
  }
  EXPECT_TRUE(log.format("wide").has_value());
  EXPECT_EQ(log.counts().unresolved_formats, 6U);
}

TEST(ULog, KeepsTheFirstValueOfEachParameterAndTextInformation) {
  ULogBuilder builder;
  builder.key_value('P', "int32_t MAV_SYS_ID", sample(std::int32_t{7}));
  builder.key_value('P', "int32_t MAV_SYS_ID", sample(std::int32_t{8}));
  builder.key_value('P', "float GAIN", sample(0.5F));
  builder.key_value('P', "double WIDE", sample(1.0));
  builder.key_value('P', "float[2] PAIR", sample(1.0F, 2.0F));
  builder.key_value('I', "char[3] sys_name", sample(std::array<char, 3>{'P', 'X', '4'}));
  builder.key_value('I', "uint32_t ver_hw", sample(std::uint32_t{1}));
  const ULog log = parse(builder);
  EXPECT_EQ(log.parameter("MAV_SYS_ID"), 7.0);
  EXPECT_EQ(log.parameter("GAIN"), 0.5);
  EXPECT_EQ(log.parameter("WIDE"), std::nullopt);
  EXPECT_EQ(log.parameter("PAIR"), std::nullopt);
  EXPECT_EQ(log.info("sys_name"), "PX4");
  EXPECT_EQ(log.info("ver_hw"), std::nullopt);
  EXPECT_FALSE(log.counts().corrupt);
}

TEST(ULog, SeparatesInstancesAndFollowsResubscriptions) {
  ULogBuilder builder;
  builder.format("gps:uint64_t timestamp;").add_logged(0, 1, "gps").add_logged(1, 2, "gps");
  builder.data(1, sample(std::uint64_t{1})).data(2, sample(std::uint64_t{2})).data(9, sample(std::uint64_t{9}));
  builder.add_logged(0, 4, "gps").data(4, sample(std::uint64_t{4}));
  const ULog log = parse(builder);
  const ULogField timestamp = field(log, "gps", "timestamp");
  const std::vector<std::span<const std::byte>> first = log.samples("gps", 0);
  ASSERT_EQ(first.size(), 2U);
  EXPECT_EQ(read_integer(timestamp, first[1]), 4);
  EXPECT_EQ(log.samples("gps", 1).size(), 1U);
  EXPECT_TRUE(log.samples("absent", 0).empty());
  EXPECT_EQ(log.counts().orphan_samples, 1U);
}

TEST(ULog, ReadsLoggedStringsAndSkipsOtherMessages) {
  ULogBuilder builder;
  builder.logging(5, "[commander] Armed").tagged(6, "[navigator] Mission").message('O', sample(std::uint16_t{50}));
  for (const char type : {'M', 'Q', 'R', 'S', 'B', 'X'}) {
    builder.message(type, sample(std::uint8_t{0}));
  }
  const ULog log = parse(builder);
  ASSERT_EQ(log.strings().size(), 2U);
  EXPECT_EQ(log.strings()[0].text, "[commander] Armed");
  EXPECT_EQ(log.strings()[0].level, 54U);
  EXPECT_EQ(log.strings()[1].timestamp_us, 6U);
  EXPECT_EQ(log.strings()[1].text, "[navigator] Mission");
  EXPECT_EQ(log.counts().dropouts, 1U);
  EXPECT_EQ(log.counts().unknown_messages, 1U);
}

TEST(ULog, StopsAtAMessageCutShort) {
  for (const std::size_t cut : {1U, 2U, 5U}) {
    ULogBuilder builder;
    builder.logging(5, "kept").logging(6, "cut");
    builder.bytes().resize(builder.size() - cut);
    const ULog log = parse(builder);
    EXPECT_EQ(log.strings().size(), 1U) << cut;
    EXPECT_TRUE(log.counts().truncated);
  }
}

TEST(ULog, StopsAtAMalformedMessage) {
  const std::vector<std::pair<char, std::vector<std::byte>>> malformed{
      {'F', sample(std::array<char, 3>{'n', 'o', ';'})},
      {'F', sample(std::array<char, 4>{':', 'a', ' ', 'b'})},
      {'F', sample(std::array<char, 8>{'p', ':', 'f', 'l', 'o', 'a', 't', ';'})},
      {'A', sample(std::uint8_t{0}, std::uint16_t{1})},
      {'D', sample(std::uint8_t{0})},
      {'L', sample(std::uint8_t{0}, std::uint32_t{1})},
      {'C', sample(std::uint8_t{0}, std::uint16_t{1}, std::uint32_t{1})},
      {'I', {}},
      {'I', sample(std::uint8_t{9}, std::uint8_t{1})},
      {'P', sample(std::uint8_t{3}, std::array<char, 3>{'x', 'y', 'z'})},
  };
  for (const auto& [type, body] : malformed) {
    ULogBuilder builder;
    builder.logging(5, "kept").message(type, body).logging(6, "after");
    const ULog log = parse(builder);
    EXPECT_EQ(log.strings().size(), 1U) << type;
    EXPECT_TRUE(log.counts().corrupt) << type;
  }
}

TEST(ULog, RejectsBadArrayCounts) {
  for (const std::string_view text : {"p:float[] a;", "p:float[x] a;", "p:float[65536] a;",
                                      "p:[2] a;", "p:float[2 a;", "p:float[99999999999999999999] a;"}) {
    ULogBuilder builder;
    builder.format(text);
    EXPECT_TRUE(parse(builder).counts().corrupt) << text;
  }
  ULogBuilder builder;
  builder.format("p:float[65535] a;char[0] none;");
  const ULog log = parse(builder);
  EXPECT_EQ(log.format("p")->get().size, 4U * 65535U);
  EXPECT_EQ(read(find_field(log.format("p")->get(), "none").value(), std::span<const std::byte>()), std::nullopt);
}

TEST(ULog, ReadsAppendedDataAfterACutSection) {
  ULogBuilder builder;
  builder.flag_bits(1, {0, 0, 0}).logging(1, "before");
  const std::size_t appended = builder.size() + 6;
  builder.bytes()[16 + 3 + 16] = static_cast<std::byte>(appended);
  builder.logging(2, "cut by the crash");
  builder.bytes().resize(appended);
  builder.logging(3, "appended");
  const ULog log = parse(builder);
  ASSERT_EQ(log.strings().size(), 2U);
  EXPECT_EQ(log.strings()[1].text, "appended");
  EXPECT_TRUE(log.counts().truncated);
}

TEST(ULog, IgnoresAppendedOffsetsThatCannotBeUsed) {
  ULogBuilder builder;
  builder.flag_bits(1, {4, 1'000'000, 0}).logging(1, "only");
  EXPECT_EQ(parse(builder).strings().size(), 1U);
  ULogBuilder unflagged;
  unflagged.flag_bits(0, {20, 0, 0}).logging(1, "only");
  EXPECT_EQ(parse(unflagged).strings().size(), 1U);
}

TEST(ULog, RefusesLogsItCannotRead) {
  ULogBuilder unknown_flag;
  unknown_flag.flag_bits(2, {0, 0, 0});
  ULogBuilder unknown_second_flag;
  unknown_second_flag.flag_bits(0, {0, 0, 0}, 1);
  std::vector<std::byte> no_magic = ULogBuilder().bytes();
  no_magic[0] = std::byte{0};
  std::vector<std::byte> short_header = ULogBuilder().bytes();
  short_header.pop_back();
  for (const std::vector<std::byte>& bytes : {unknown_flag.bytes(), unknown_second_flag.bytes(), no_magic, short_header}) {
    EXPECT_EQ(ULog::parse(bytes).error(), Error::kMalformed);
  }
}

TEST(ULog, RefusesALogLargerThanItsLimit) {
  const std::size_t size = ULog::kMaxBytes + 1;
  void* const mapping = mmap(nullptr, size, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  ASSERT_NE(mapping, MAP_FAILED);
  const std::span<const std::byte> huge(static_cast<const std::byte*>(mapping), size);
  EXPECT_EQ(ULog::parse(huge).error(), Error::kInvalidArgument);
  munmap(mapping, size);
}

}  // namespace
}  // namespace ics::flightlog
