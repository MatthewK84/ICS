#pragma once

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
  std::vector<std::byte> out;
  for (unsigned shift = 0; shift < 32U; shift += 8U) {
    out.push_back(static_cast<std::byte>((length >> shift) & 0xFFU));
  }
  const std::span<const std::byte> bytes = std::as_bytes(std::span(body));
  out.insert(out.end(), bytes.begin(), bytes.end());
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
