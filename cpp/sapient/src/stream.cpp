#include "ics/sapient/stream.hpp"

#include <algorithm>
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

#include "ics/common/check.hpp"

namespace ics::sapient {
namespace {

constexpr std::size_t kHeaderBytes = 4;
constexpr unsigned kBitsPerByte = 8;

// The length a header gives: four bytes, least significant first.
[[nodiscard]] std::size_t length_in(const std::span<const std::byte, kHeaderBytes> header) noexcept {
  std::size_t length = 0;
  for (std::size_t i = kHeaderBytes; i > 0; --i) {
    length = (length << kBitsPerByte) | std::to_integer<std::size_t>(header[i - 1]);
  }
  return length;
}

// How many bytes the message being read needs, its header included: the
// header alone until it is complete.
[[nodiscard]] std::size_t wanted(const std::vector<std::byte>& pending) noexcept {
  if (pending.size() < kHeaderBytes) {
    return kHeaderBytes;
  }
  return kHeaderBytes + length_in(std::span(pending).first<kHeaderBytes>());
}

}  // namespace

std::vector<Message> StreamReader::feed(std::span<const std::byte> bytes) {
  std::vector<Message> out;
  while (!broken_ && !bytes.empty()) {
    const std::size_t taken = std::min(wanted(pending_) - pending_.size(), bytes.size());
    const std::span<const std::byte> chunk = bytes.first(taken);
    pending_.insert(pending_.end(), chunk.begin(), chunk.end());
    bytes = bytes.subspan(taken);
    if (pending_.size() < kHeaderBytes) {
      continue;
    }
    const std::size_t length = length_in(std::span(pending_).first<kHeaderBytes>());
    broken_ = length > kMaxMessageBytes;
    if (broken_ || pending_.size() < kHeaderBytes + length) {
      continue;
    }
    const std::span<const std::byte> body = std::span(pending_).subspan(kHeaderBytes);
    static_cast<void>(check(body.size() <= kMaxMessageBytes));
    Message message;
    broken_ = !message.ParseFromArray(body.data(), static_cast<int>(body.size()));
    if (!broken_) {
      out.push_back(std::move(message));
    }
    pending_.clear();
  }
  return out;
}

}  // namespace ics::sapient
