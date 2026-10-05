#include "ics/flightlog/dataflash.hpp"

#include <sys/mman.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "dataflash_builder.hpp"
#include "ics/common/error.hpp"

namespace ics::flightlog {
namespace {

using testing::body;
using testing::DataFlashBuilder;

constexpr std::uint8_t kNumbers = 200;
constexpr std::uint8_t kText = 201;
constexpr std::uint8_t kHalf = 202;

DataFlashBuilder numbers() {
  DataFlashBuilder builder;
  builder.format(kNumbers, 62, "NUM", "bBhHiIfdcCeELMqQ", "b,B,h,H,i,I,f,d,c,C,e,E,L,M,q,Q");
  builder.message(kNumbers, body(std::int8_t{-5}, std::uint8_t{250}, std::int16_t{-300}, std::uint16_t{60000},
                                 std::int32_t{-70000}, std::uint32_t{4'000'000'000}, 1.5F, 2.25, std::int16_t{-1234},
                                 std::uint16_t{5678}, std::int32_t{-123456}, std::uint32_t{654321},
                                 std::int32_t{399'980'185}, std::uint8_t{3}, std::int64_t{-9'000'000'000'000},
                                 std::uint64_t{9'000'000'000'000}));
  return builder;
}

DataFlash parse(const DataFlashBuilder& builder) {
  Result<DataFlash> log = DataFlash::parse(builder.bytes());
  EXPECT_TRUE(log.has_value());
  return std::move(log).value();
}

double value(const DataFlash& log, const std::string& type, const std::string& column, const std::size_t i = 0) {
  const DataFlashFormat& format = log.format(type)->get();
  return read(find_column(format, column).value(), log.messages(type).at(i)).value();
}

TEST(DataFlash, ReadsEveryNumericColumnScaledAsItsTypeSays) {
  const DataFlashBuilder builder = numbers();
  const DataFlash log = parse(builder);
  EXPECT_EQ(log.format("NUM")->get().length, 62U);
  EXPECT_EQ(value(log, "NUM", "b"), -5.0);
  EXPECT_EQ(value(log, "NUM", "B"), 250.0);
  EXPECT_EQ(value(log, "NUM", "h"), -300.0);
  EXPECT_EQ(value(log, "NUM", "H"), 60000.0);
  EXPECT_EQ(value(log, "NUM", "i"), -70000.0);
  EXPECT_EQ(value(log, "NUM", "I"), 4'000'000'000.0);
  EXPECT_EQ(value(log, "NUM", "f"), 1.5);
  EXPECT_EQ(value(log, "NUM", "d"), 2.25);
  EXPECT_EQ(value(log, "NUM", "c"), -1234.0 * 0.01);
  EXPECT_EQ(value(log, "NUM", "C"), 5678.0 * 0.01);
  EXPECT_EQ(value(log, "NUM", "e"), -123456.0 * 0.01);
  EXPECT_EQ(value(log, "NUM", "E"), 654321.0 * 0.01);
  EXPECT_EQ(value(log, "NUM", "L"), 399'980'185.0 * 1e-7);
  EXPECT_EQ(value(log, "NUM", "M"), 3.0);
  EXPECT_EQ(value(log, "NUM", "q"), -9e12);
  EXPECT_EQ(value(log, "NUM", "Q"), 9e12);
  EXPECT_EQ(find_column(log.format("NUM")->get(), "absent"), std::nullopt);
  EXPECT_EQ(log.format("ABSENT"), std::nullopt);
  EXPECT_TRUE(log.messages("ABSENT").empty());
}

TEST(DataFlash, ReadsTextAndHalfFloats) {
  DataFlashBuilder builder;
  builder.format(kText, 155, "TXT", "nNZagg", "n,N,Z,a,g,h");
  std::vector<std::byte> text;
  DataFlashBuilder::put_text(text, "MODE", 4);
  DataFlashBuilder::put_text(text, "MAV_SYSID", 16);
  DataFlashBuilder::put_text(text, "ArduCopter V4.7.1", 64);
  text.resize(text.size() + 64);
  DataFlashBuilder::put(text, std::uint16_t{0xC000});
  DataFlashBuilder::put(text, std::uint16_t{0x0001});
  builder.message(kText, text);
  const DataFlash log = parse(builder);
  const DataFlashFormat& format = log.format("TXT")->get();
  const std::span<const std::byte> message = log.messages("TXT").at(0);
  EXPECT_EQ(read_text(find_column(format, "n").value(), message), "MODE");
  EXPECT_EQ(read_text(find_column(format, "N").value(), message), "MAV_SYSID");
  EXPECT_EQ(read_text(find_column(format, "Z").value(), message), "ArduCopter V4.7.1");
  EXPECT_EQ(read(find_column(format, "Z").value(), message), std::nullopt);
  EXPECT_EQ(read(find_column(format, "a").value(), message), std::nullopt);
  EXPECT_EQ(read_text(find_column(format, "g").value(), message), std::nullopt);
  EXPECT_EQ(value(log, "TXT", "g"), -2.0);
  EXPECT_EQ(value(log, "TXT", "h"), std::ldexp(1.0, -24));
}

TEST(DataFlash, ConvertsHalfFloatsOfEveryKind) {
  constexpr double kInfinity = std::numeric_limits<double>::infinity();
  const std::array<std::pair<std::uint16_t, double>, 5> cases{
      {{0x3C00, 1.0}, {0x0000, 0.0}, {0x7BFF, 65504.0}, {0x7C00, kInfinity}, {0xFC00, -kInfinity}}};
  for (const auto& [bits, expected] : cases) {
    DataFlashBuilder builder;
    builder.format(kText, 5, "HLF", "g", "g").message(kText, body(bits));
    EXPECT_EQ(value(parse(builder), "HLF", "g"), expected) << bits;
  }
  DataFlashBuilder builder;
  builder.format(kText, 5, "HLF", "g", "g").message(kText, body(std::uint16_t{0x7E00}));
  EXPECT_TRUE(std::isnan(value(parse(builder), "HLF", "g")));
}

TEST(DataFlash, SkipsBytesThatStartNoKnownMessage) {
  DataFlashBuilder builder = numbers();
  builder.raw(body(std::uint8_t{0xA3}, std::uint8_t{0x00})).message(77, body(std::uint32_t{1}));
  builder.raw(numbers().bytes()).raw(body(std::uint8_t{0xA3}, std::uint8_t{0x95}));
  const DataFlash log = parse(builder);
  EXPECT_EQ(log.messages("NUM").size(), 2U);
  EXPECT_EQ(log.messages("FMT").size(), 2U);
  EXPECT_EQ(log.counts().skipped_bytes, 2U + 7U + 2U);
  EXPECT_FALSE(log.counts().truncated);
}

TEST(DataFlash, StopsAtAMessageCutShort) {
  DataFlashBuilder builder = numbers();
  builder.raw(numbers().bytes());
  builder.bytes().resize(builder.bytes().size() - 1);
  const DataFlash log = parse(builder);
  EXPECT_EQ(log.messages("NUM").size(), 1U);
  EXPECT_TRUE(log.counts().truncated);
}

TEST(DataFlash, IgnoresFormatsThatDoNotFitTheirLength) {
  DataFlashBuilder builder;
  builder.format(210, 7, "BADC", "Ix", "A,B").format(211, 8, "LONG", "I", "A").format(212, 7, "FEW", "II", "A");
  builder.format(213, 7, "MANY", "I", "A,B").format(214, 7, "", "I", "A");
  const DataFlash log = parse(builder);
  EXPECT_EQ(log.counts().bad_formats, 5U);
  for (const std::string name : {"BADC", "LONG", "FEW", "MANY"}) {
    EXPECT_EQ(log.format(name), std::nullopt) << name;
  }
}

TEST(DataFlash, TakesFmtDescribingItselfOnlyAtTheLengthItReads) {
  DataFlashBuilder builder;
  builder.format(128, 89, "FMT", "BBnNZ", "Type,Length,Name,Format,Columns");
  builder.format(128, 5, "FMT", "BB", "Type,Length");
  builder.format(kNumbers, 7, "ONE", "I", "A").message(kNumbers, body(std::uint32_t{1}));
  const DataFlash log = parse(builder);
  EXPECT_EQ(log.counts().bad_formats, 1U);
  EXPECT_EQ(log.format("FMT")->get().length, 89U);
  EXPECT_EQ(value(log, "ONE", "A"), 1.0);
}

TEST(DataFlash, TakesARedefinedFormat) {
  DataFlashBuilder builder;
  builder.format(kNumbers, 7, "ONE", "I", "A").message(kNumbers, body(std::uint32_t{1}));
  builder.format(kNumbers, 5, "TWO", "H", "B").message(kNumbers, body(std::uint16_t{2}));
  const DataFlash log = parse(builder);
  EXPECT_EQ(log.format("ONE"), std::nullopt);
  ASSERT_EQ(log.messages("TWO").size(), 1U);
  EXPECT_EQ(value(log, "TWO", "B"), 2.0);
}

TEST(DataFlash, ReadsNothingPastTheEndOfAMessage) {
  const DataFlashBuilder builder = numbers();
  const DataFlash log = parse(builder);
  const DataFlashFormat& format = log.format("NUM")->get();
  const std::span<const std::byte> cut = log.messages("NUM").at(0).first(10);
  EXPECT_EQ(read(find_column(format, "Q").value(), cut), std::nullopt);
  const DataFlashColumn text{.name = "N", .type = 'N', .offset = 8, .size = 16};
  EXPECT_EQ(read_text(text, cut), std::nullopt);
  EXPECT_EQ(read_text(DataFlashColumn{.name = "n", .type = 'n', .offset = 11, .size = 4}, cut), std::nullopt);
}

TEST(DataFlash, ReadsNoColumnOfAMessageCutBeforeIt) {
  DataFlashBuilder builder = numbers();
  builder.format(kHalf, 5, "HLF", "g", "g").message(kHalf, body(std::uint16_t{0x3C00}));
  const DataFlash log = parse(builder);
  for (const std::string type : {"NUM", "HLF"}) {
    const std::span<const std::byte> message = log.messages(type).at(0);
    for (const DataFlashColumn& column : log.format(type)->get().columns) {
      EXPECT_TRUE(read(column, message).has_value()) << column.name;
      EXPECT_EQ(read(column, message.first(column.offset)), std::nullopt) << column.name;
    }
  }
}

TEST(DataFlash, RefusesALogLargerThanItsLimit) {
  const std::size_t size = DataFlash::kMaxBytes + 1;
  void* const mapping = mmap(nullptr, size, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  ASSERT_NE(mapping, MAP_FAILED);
  EXPECT_EQ(DataFlash::parse(std::span<const std::byte>(static_cast<const std::byte*>(mapping), size)).error(),
            Error::kInvalidArgument);
  munmap(mapping, size);
}

}  // namespace
}  // namespace ics::flightlog
