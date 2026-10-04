#include "ics/lattice/sse.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ics::lattice {
namespace {

// A UTF-8 byte order mark, which the standard drops from the start of a stream.
constexpr std::string_view kByteOrderMark = "\xEF\xBB\xBF";

}  // namespace

SseReader::SseReader(const std::size_t max_event_bytes) noexcept : max_event_bytes_(max_event_bytes) {}

std::vector<SseEvent> SseReader::feed(const std::string_view chunk) {
  std::vector<SseEvent> out;
  for (const char c : chunk) {
    take(c, out);
  }
  return out;
}

void SseReader::take(const char c, std::vector<SseEvent>& out) {
  // A line feed straight after a carriage return ends the same line.
  const bool lf_after_cr = after_cr_ && c == '\n';
  after_cr_ = c == '\r';
  if (lf_after_cr) {
    return;
  }
  if (c == '\r' || c == '\n') {
    end_line(out);
    return;
  }
  if (line_.size() >= max_event_bytes_) {
    too_long_ = true;
    return;
  }
  line_.push_back(c);
}

void SseReader::end_line(std::vector<SseEvent>& out) {
  std::string_view line = line_;
  if (at_start_ && line.starts_with(kByteOrderMark)) {
    line.remove_prefix(kByteOrderMark.size());
  }
  at_start_ = false;
  if (line.empty()) {
    dispatch(out);
  } else if (!too_long_) {
    take_field(line);
  }
  line_.clear();
}

void SseReader::take_field(const std::string_view line) {
  const std::size_t colon = line.find(':');
  const std::string_view name = line.substr(0, colon);
  std::string_view value = colon == std::string_view::npos ? std::string_view{} : line.substr(colon + 1);
  if (value.starts_with(' ')) {
    value.remove_prefix(1);
  }
  if (name == "event") {
    pending_.event = value;
  } else if (name == "data" && pending_.data.size() + value.size() < max_event_bytes_) {
    pending_.data.append(value).push_back('\n');
    has_data_ = true;
  } else if (name == "data") {
    too_long_ = true;
  }
}

void SseReader::dispatch(std::vector<SseEvent>& out) {
  if (too_long_) {
    ++oversized_;
  } else if (has_data_) {
    pending_.data.pop_back();
    out.push_back(std::move(pending_));
  }
  pending_ = SseEvent{};
  has_data_ = false;
  too_long_ = false;
}

}  // namespace ics::lattice
