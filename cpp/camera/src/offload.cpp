#include "ics/camera/offload.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ics/camera/cine.hpp"
#include "ics/camera/frame_meta.hpp"
#include "ics/camera/mapped_file.hpp"
#include "ics/camera/phantom.hpp"
#include "ics/common/check.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"

namespace ics::camera {
namespace {

constexpr double kNsPerSecond = 1e9;

// A segment recorded, with what ICS knew of its timing when it was.
struct Recorded {
  SegmentStatus status;
  TimeAuthority authority;
};

[[nodiscard]] Duration magnitude(const double ns) noexcept { return Duration(std::llround(std::abs(ns))); }

// The largest difference between consecutive frames' spacing and a period.
[[nodiscard]] Duration spacing_error(const std::vector<UtcTime>& times, const double period_ns) {
  Duration worst{};
  for (std::size_t i = 1; i < times.size(); ++i) {
    const auto gap = static_cast<double>((times[i] - times[i - 1]).count());
    worst = std::max(worst, magnitude(gap - period_ns));
  }
  return worst;
}

[[nodiscard]] std::string segment_name(const std::uint32_t segment) {
  std::array<char, 16> text{};
  const int written = std::snprintf(text.data(), text.size(), "cine-%03u", segment);
  return {text.data(), static_cast<std::size_t>(std::max(written, 0))};
}

// Saves a recorded segment's planned frames, reads the cine back and
// verifies it.
[[nodiscard]] Result<SegmentCheck> offload(PhantomCamera& camera, const OffloadPlan& plan, const Recorded& segment,
                                           const std::optional<UtcTime> previous_last) {
  const std::string name = segment_name(segment.status.segment);
  const std::filesystem::path path = plan.directory / (name + ".cine");
  const Status saved = camera.save(segment.status.segment, plan.frames, path);
  const Result<MappedFile> file = saved ? MappedFile::open(path) : fail(saved.error());
  const Result<Cine> cine = file ? read_cine(file->bytes()) : fail(file.error());
  if (!cine) {
    return fail(cine.error());
  }
  SegmentCheck outcome = verify_segment(*cine, SegmentExpectation{.frames = plan.frames,
                                                                  .trigger_time = segment.status.trigger_time,
                                                                  .previous_last = previous_last,
                                                                  .authority = segment.authority});
  outcome.segment = segment.status.segment;
  outcome.path = path;
  outcome.meta = frame_meta(
      *cine, SegmentName{.station_id = plan.station_id, .camera_id = plan.camera_id, .segment_id = name},
      segment.authority);
  return outcome;
}

// Arms the camera and records the plan's segments.
[[nodiscard]] Result<std::vector<Recorded>> record(PhantomCamera& camera, const CameraSettings& settings,
                                                   const OffloadPlan& plan, TimeQualitySource& quality) {
  const Result<CameraSettings> applied = camera.configure(settings);
  const Status armed = applied ? camera.arm(plan.segments) : fail(applied.error());
  if (!armed) {
    return fail(armed.error());
  }
  std::vector<Recorded> out;
  out.reserve(plan.segments);
  for (std::uint32_t i = 0; i < plan.segments; ++i) {
    const Result<SegmentStatus> status = camera.trigger();
    if (!status) {
      return fail(status.error());
    }
    out.push_back(Recorded{.status = *status, .authority = time_authority(applied->irig, quality.current(), plan.camera_id)});
  }
  return out;
}

}  // namespace

SegmentCheck verify_segment(const Cine& cine, const SegmentExpectation& expected) {
  SegmentCheck out;
  // read_cine refuses a cine with no frames or no frame rate.
  if (!check(!cine.frame_times.empty() && cine.frame_rate > 0)) {
    return out;
  }
  const double period_ns = kNsPerSecond / cine.frame_rate;
  out.frames = cine.frame_times.size();
  out.first = cine.frame_times.front();
  out.last = cine.frame_times.back();
  out.spacing_error = spacing_error(cine.frame_times, period_ns);
  const auto from_trigger = static_cast<double>((out.first - expected.trigger_time).count());
  const Duration reported = magnitude(static_cast<double>((cine.trigger_time - expected.trigger_time).count()));
  out.trigger_error = std::max(reported, magnitude(from_trigger - (cine.first_image_no * period_ns)));
  out.irig = expected.authority.irig();
  out.ordered = !expected.previous_last.has_value() || out.first > *expected.previous_last;
  out.verified = out.frames == expected.frames.count && cine.first_image_no == expected.frames.first && out.irig &&
                 out.ordered && out.spacing_error <= kSpacingTolerance &&
                 static_cast<double>(out.trigger_error.count()) <= period_ns;
  return out;
}

Result<OffloadReport> record_and_offload(PhantomCamera& camera, const CameraSettings& settings,
                                         const OffloadPlan& plan, TimeQualitySource& quality) {
  const Result<std::vector<Recorded>> recorded = record(camera, settings, plan, quality);
  if (!recorded) {
    return fail(recorded.error());
  }
  OffloadReport report{.segments = {}, .verified = true};
  std::optional<UtcTime> previous_last;
  for (const Recorded& segment : *recorded) {
    Result<SegmentCheck> checked = offload(camera, plan, segment, previous_last);
    if (!checked) {
      return fail(checked.error());
    }
    previous_last = checked->last;
    report.verified = report.verified && checked->verified;
    report.segments.push_back(std::move(*checked));
  }
  return report;
}

}  // namespace ics::camera
