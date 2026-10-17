// Fuzzes the camera .meta file reader (ICS-036) with any bytes, as a file
// from another tool could hold. It may not overflow or crash, or read more
// frames than it is allowed.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <string>

#include "ics/encoder/frame_meta.hpp"

namespace {

// Each frame takes a byte at least, so a few kilobytes of input stay within it.
constexpr std::size_t kMaxFrames = 100'000;

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  // The file's bytes are chars to the stream.
  std::istringstream in(std::string(reinterpret_cast<const char*>(data), size));
  const auto frames = ics::encoder::read_frame_meta(in, kMaxFrames);
  if (frames && frames->size() > kMaxFrames) {
    std::abort();
  }
  return 0;
}
