// Fuzzes the time-tagger's datagram decoder (ICS-032) with any bytes, as the
// station network could deliver them. It may not overflow or crash, and a
// datagram it reads must encode back to the same bytes.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>

#include "ics/tagger/datagram.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const std::span<const std::byte> bytes = std::as_bytes(std::span(data, size));
  const auto batch = ics::tagger::decode(bytes);
  if (!batch) {
    return 0;
  }
  std::array<std::byte, ics::tagger::kDatagramBytes> again{};
  if (ics::tagger::encode(*batch, again) != size || !std::ranges::equal(again, bytes)) {
    std::abort();
  }
  return 0;
}
