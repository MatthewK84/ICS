#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "sapient_msg/bsi_flex_335_v2_0/sapient_message.pb.h"

namespace ics::sapient {

// A SAPIENT message (BSI Flex 335 v2.0), from Dstl's protobuf definitions in
// proto/third_party/sapient_msg.
using Message = sapient_msg::bsi_flex_335_v2_0::SapientMessage;

// Reads the messages a SAPIENT node sends over TCP (ICS-024). Each is framed
// as Dstl's test harness frames it (ByteDataMessageBuilder): a 4-byte
// little-endian length, then that many bytes of a serialized SapientMessage.
// The bytes may arrive in chunks of any size.
//
// A length over kMaxMessageBytes, or a message that does not parse, breaks
// the stream: framing is lost, so nothing after it can be trusted. The reader
// then returns nothing more, and the caller should close the connection.
class StreamReader {
 public:
  static constexpr std::size_t kMaxMessageBytes = std::size_t{1} << 20U;

  // The messages these bytes complete, in order. A call that breaks the
  // stream still returns the messages before the fault.
  [[nodiscard]] std::vector<Message> feed(std::span<const std::byte> bytes);

  [[nodiscard]] bool broken() const noexcept { return broken_; }

 private:
  // The bytes of the message being read, its length first.
  std::vector<std::byte> pending_;
  bool broken_ = false;
};

}  // namespace ics::sapient
