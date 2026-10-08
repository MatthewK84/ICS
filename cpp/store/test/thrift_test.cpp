#include "ics/store/thrift.hpp"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <vector>

#include <gtest/gtest.h>

namespace {

using ics::store::ThriftType;
using ics::store::ThriftWriter;

std::vector<std::byte> bytes(const std::initializer_list<int> values) {
  std::vector<std::byte> out;
  for (const int value : values) {
    out.push_back(static_cast<std::byte>(value));
  }
  return out;
}

TEST(Varint, TakesSevenBitsPerByte) {
  std::vector<std::byte> out;
  ics::store::append_varint(0, out);
  ics::store::append_varint(127, out);
  ics::store::append_varint(300, out);
  ics::store::append_varint(UINT64_MAX, out);
  EXPECT_EQ(out, bytes({0x00, 0x7F, 0xAC, 0x02, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01}));
}

TEST(ThriftWriter, WritesShortFieldHeadersAndZigzagNumbers) {
  std::vector<std::byte> out;
  ThriftWriter thrift(out);
  thrift.begin_struct();
  thrift.field_i32(1, 3);
  thrift.field_i64(2, -1);
  thrift.field_binary(4, "ab");
  thrift.end_struct();
  EXPECT_EQ(thrift.depth(), 0U);
  EXPECT_EQ(out, bytes({0x15, 0x06, 0x16, 0x01, 0x28, 0x02, 'a', 'b', 0x00}));
}

TEST(ThriftWriter, WritesLongFieldHeadersForLargeOrBackwardSteps) {
  std::vector<std::byte> out;
  ThriftWriter thrift(out);
  thrift.begin_struct();
  thrift.field_i32(20, 1);
  thrift.field_i32(2, 1);
  thrift.end_struct();
  EXPECT_EQ(out, bytes({0x05, 0x28, 0x02, 0x05, 0x04, 0x02, 0x00}));
}

TEST(ThriftWriter, NestsStructsWithTheirOwnFieldIds) {
  std::vector<std::byte> out;
  ThriftWriter thrift(out);
  thrift.begin_struct();
  thrift.field_i32(3, 0);
  thrift.begin_struct_field(5);
  EXPECT_EQ(thrift.depth(), 2U);
  thrift.field_i32(1, 0);
  thrift.end_struct();
  thrift.field_i32(6, 0);
  thrift.end_struct();
  EXPECT_EQ(out, bytes({0x35, 0x00, 0x2C, 0x15, 0x00, 0x00, 0x15, 0x00, 0x00}));
}

TEST(ThriftWriter, WritesShortAndLongLists) {
  std::vector<std::byte> out;
  ThriftWriter thrift(out);
  thrift.begin_struct();
  thrift.begin_list_field(1, ThriftType::kI32, 2);
  thrift.i32(0);
  thrift.i32(3);
  thrift.begin_list_field(2, ThriftType::kBinary, 15);
  for (int index = 0; index < 15; ++index) {
    thrift.binary("");
  }
  thrift.begin_list_field(3, ThriftType::kStruct, 1);
  thrift.begin_struct();
  thrift.end_struct();
  thrift.end_struct();
  std::vector<std::byte> expected = bytes({0x19, 0x25, 0x00, 0x06, 0x19, 0xF8, 0x0F});
  expected.insert(expected.end(), 15, std::byte{0});
  const std::vector<std::byte> tail = bytes({0x19, 0x1C, 0x00, 0x00});
  expected.insert(expected.end(), tail.begin(), tail.end());
  EXPECT_EQ(out, expected);
}

}  // namespace
