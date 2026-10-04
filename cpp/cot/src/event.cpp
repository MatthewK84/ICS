#include "ics/cot/event.hpp"

#include <charconv>
#include <cmath>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <pugixml.hpp>

#include "ics/common/check.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/cot/time.hpp"
#include "ics/cot/uncertainty.hpp"

namespace ics::cot {
namespace {

// The default parse, which keeps CDATA, character and entity references and
// line ends, with several top-level elements allowed.
constexpr unsigned kParseOptions = pugi::parse_default | pugi::parse_fragment;
constexpr double kMaxLatitude = 90.0;
constexpr double kMaxLongitude = 180.0;
constexpr double kFullCircle = 360.0;

// A finite number, if that is all text holds besides spaces at either end.
[[nodiscard]] std::optional<double> number(const char* const text) noexcept {
  std::string_view view(text);
  const std::size_t first = view.find_first_not_of(' ');
  if (first == std::string_view::npos) {
    return std::nullopt;
  }
  view = view.substr(first, view.find_last_not_of(' ') - first + 1);
  double out = 0.0;
  const std::from_chars_result read = std::from_chars(view.begin(), view.end(), out);
  if (read.ec != std::errc() || read.ptr != view.end() || !std::isfinite(out)) {
    return std::nullopt;
  }
  return out;
}

[[nodiscard]] std::optional<double> attribute(const pugi::xml_node node, const char* const name) noexcept {
  return number(node.attribute(name).value());
}

[[nodiscard]] std::optional<Point> point(const pugi::xml_node event) noexcept {
  const pugi::xml_node node = event.child("point");
  const std::optional<double> latitude = attribute(node, "lat");
  const std::optional<double> longitude = attribute(node, "lon");
  const std::optional<double> hae = attribute(node, "hae");
  const std::optional<double> ce = attribute(node, "ce");
  const std::optional<double> le = attribute(node, "le");
  if (!latitude || !longitude || !hae || !ce || !le) {
    return std::nullopt;
  }
  if (std::abs(*latitude) > kMaxLatitude || std::abs(*longitude) > kMaxLongitude) {
    return std::nullopt;
  }
  return Point{.latitude_deg = *latitude, .longitude_deg = *longitude, .hae_m = *hae, .ce_m = *ce, .le_m = *le};
}

[[nodiscard]] std::optional<Track> track(const pugi::xml_node event) noexcept {
  const pugi::xml_node node = event.child("detail").child("track");
  const std::optional<double> course = attribute(node, "course");
  const std::optional<double> speed = attribute(node, "speed");
  if (!course || !speed || *course < 0.0 || *course >= kFullCircle || *speed < 0.0 || *speed >= kUnknown) {
    return std::nullopt;
  }
  return Track{.course_deg = *course, .speed_mps = *speed};
}

[[nodiscard]] std::optional<Event> read_event(const pugi::xml_node node) {
  std::string uid = node.attribute("uid").value();
  std::string type = node.attribute("type").value();
  const std::optional<Point> where = point(node);
  if (uid.empty() || type.empty() || !where) {
    return std::nullopt;
  }
  const Result<UtcTime> time = parse_cot_time(node.attribute("time").value());
  return Event{.uid = std::move(uid),
               .type = std::move(type),
               .how = node.attribute("how").value(),
               .time = time ? std::optional<UtcTime>(*time) : std::nullopt,
               .point = *where,
               .track = track(node)};
}

// Adds a top-level element to out: an event, or a count of what it was.
void take(const pugi::xml_node node, Read& out) {
  if (std::string_view(node.name()) != "event") {
    ++out.counts.not_events;
    return;
  }
  std::optional<Event> event = read_event(node);
  if (!event) {
    ++out.counts.invalid;
    return;
  }
  out.events.push_back(std::move(*event));
}

}  // namespace

Read read_events(const std::span<const std::byte> payload) {
  Read out;
  pugi::xml_document document;
  const pugi::xml_parse_result parsed =
      document.load_buffer(payload.data(), payload.size(), kParseOptions, pugi::encoding_auto);
  if (!parsed) {
    ++out.counts.malformed;
    return out;
  }
  // Top-level text, which a fragment may hold, is not an element and is skipped.
  for (const pugi::xml_node node : document.children()) {
    if (node.type() == pugi::node_element) {
      take(node, out);
    }
  }
  static_cast<void>(check(out.counts.malformed == 0));
  return out;
}

}  // namespace ics::cot
