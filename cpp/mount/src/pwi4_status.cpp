#include "ics/mount/pwi4_status.hpp"

#include <charconv>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace ics::mount {
namespace {

using Pairs = std::vector<std::pair<std::string_view, std::string_view>>;

// The keyword=value pairs of a status text, read by keyword. A keyword that
// is missing or whose value does not read marks the text bad.
class Fields {
 public:
  explicit Fields(Pairs pairs) noexcept : pairs_(std::move(pairs)) {}

  [[nodiscard]] bool flag(const std::string_view key) {
    const std::string_view value = find(key);
    if (value == "true") {
      return true;
    }
    ok_ = ok_ && value == "false";
    return false;
  }

  [[nodiscard]] double number(const std::string_view key) {
    const std::string_view value = find(key);
    double out = 0.0;
    const std::from_chars_result read = std::from_chars(value.begin(), value.end(), out);
    ok_ = ok_ && read.ec == std::errc() && read.ptr == value.end() && std::isfinite(out);
    return out;
  }

  [[nodiscard]] Radians angle(const std::string_view key) { return to_radians(Degrees(number(key))); }

  [[nodiscard]] bool ok() const noexcept { return ok_; }

 private:
  // The first value of key, or nothing, which no value reads as.
  [[nodiscard]] std::string_view find(const std::string_view key) const noexcept {
    for (const auto& [name, value] : pairs_) {
      if (name == key) {
        return value;
      }
    }
    return {};
  }

  Pairs pairs_;
  bool ok_ = true;
};

// Splits the text into its keyword=value lines; nothing for a line, other
// than an empty one, without '='.
[[nodiscard]] Result<Pairs> split(const std::string_view text) {
  Pairs out;
  std::size_t start = 0;
  while (start < text.size()) {
    std::size_t end = text.find('\n', start);
    end = end == std::string_view::npos ? text.size() : end;
    std::string_view line = text.substr(start, end - start);
    if (line.ends_with('\r')) {
      line.remove_suffix(1);
    }
    start = end + 1;
    const std::size_t equals = line.find('=');
    if (equals == std::string_view::npos && !line.empty()) {
      return fail(Error::kMalformed);
    }
    if (!line.empty()) {
      out.emplace_back(line.substr(0, equals), line.substr(equals + 1));
    }
  }
  return out;
}

[[nodiscard]] AxisStatus axis(Fields& fields, const std::string& prefix) {
  return AxisStatus{.enabled = fields.flag(prefix + "is_enabled"),
                    .position = fields.angle(prefix + "position_degs"),
                    .velocity_rad_per_s = to_radians(Degrees(fields.number(prefix + "measured_velocity_degs_per_sec")))
                                              .value(),
                    .min_position = fields.angle(prefix + "min_mech_position_degs"),
                    .max_position = fields.angle(prefix + "max_mech_position_degs")};
}

}  // namespace

Result<MountStatus> parse_status(const std::string_view text) {
  if (text.size() > kMaxStatusBytes) {
    return fail(Error::kMalformed);
  }
  Result<Pairs> pairs = split(text);
  if (!pairs) {
    return fail(pairs.error());
  }
  Fields fields(std::move(*pairs));
  MountStatus out{.connected = fields.flag("mount.is_connected"),
                  .slewing = fields.flag("mount.is_slewing"),
                  .tracking = fields.flag("mount.is_tracking"),
                  .distance_to_sun = fields.angle("mount.distance_to_sun_degs"),
                  .axes = {}};
  out.axes[static_cast<std::size_t>(Axis::kAzimuth)] = axis(fields, "mount.axis0.");
  out.axes[static_cast<std::size_t>(Axis::kAltitude)] = axis(fields, "mount.axis1.");
  if (!fields.ok()) {
    return fail(Error::kMalformed);
  }
  return out;
}

}  // namespace ics::mount
