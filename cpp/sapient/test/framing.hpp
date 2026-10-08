#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <vector>

#include <google/protobuf/util/json_util.h>
#include <gtest/gtest.h>

#include "ics/sapient/stream.hpp"

namespace ics::sapient::testing {

// A message as SAPIENT frames it on the wire: its length, 4 bytes little
// endian, then the message.
inline std::vector<std::byte> frame(const Message& message) {
  const std::string body = message.SerializeAsString();
  const auto length = static_cast<std::uint32_t>(body.size());
  // Sized once and filled in place: GCC 13 at -O3 reads a push_back followed
  // by a range insert as an overflow.
  std::vector<std::byte> out(sizeof(length) + body.size());
  for (std::size_t i = 0; i < sizeof(length); ++i) {
    out[i] = static_cast<std::byte>((length >> (8U * i)) & 0xFFU);
  }
  std::ranges::copy(std::as_bytes(std::span(body)), std::span(out).subspan(sizeof(length)).begin());
  return out;
}

inline std::string read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  EXPECT_TRUE(in.is_open()) << path;
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// A message from its JSON form, as Dstl's test harness writes them.
inline Message from_json(const std::string& json) {
  Message message;
  const auto status = google::protobuf::util::JsonStringToMessage(json, &message);
  EXPECT_TRUE(status.ok()) << status.message();
  return message;
}

}  // namespace ics::sapient::testing
