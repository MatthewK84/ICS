#include "ics/camera/flir_camera.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

#include <gtest/gtest.h>

#include "ics/camera/cine.hpp"
#include "ics/camera/emulated_x6980.hpp"
#include "ics/camera/flir_sdk.hpp"
#include "ics/camera/mapped_file.hpp"
#include "ics/camera/segment_camera.hpp"
#include "ics/camera/trigger_schedule.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/v1/time_quality.pb.h"
#include "scratch_dir.hpp"

namespace ics::camera {
namespace {

using std::chrono::microseconds;
using std::chrono::milliseconds;
using std::chrono::seconds;
using testing_support::ScratchDir;

constexpr std::int64_t kEpochNs = 1'791'331'200'000'000'000;
// 2027-01-01T00:00:00Z.
constexpr std::int64_t k2027Ns = 1'798'761'600'000'000'000;

// A 16 x 8 window at (100, 50), 1,004 frames/s, 100 frames a segment, 60 of
// them after the trigger.
CameraSettings settings() {
  return CameraSettings{.width = 16,
                        .height = 8,
                        .window_x = 100,
                        .window_y = 50,
                        .frame_rate = 1004,
                        .exposure = microseconds(500),
                        .segment_frames = 100,
                        .post_trigger_frames = 60,
                        .irig = true};
}

// The station's time quality, sampled at a fixed time.
class StationTime final : public TimeQualitySource {
 public:
  explicit StationTime(const UtcTime now) { quality_.set_time_utc_ns(to_utc_ns(now)); }
  v1::TimeQuality current() override { return quality_; }

 private:
  v1::TimeQuality quality_;
};

Cine read_back(const std::filesystem::path& path) {
  const Result<MappedFile> file = MappedFile::open(path);
  return read_cine(file.value().bytes()).value();
}

TEST(FlirCamera, SavesASegmentAsACineOfItsFramesInUtc) {
  const ScratchDir dir;
  EmulatedX6980 sdk(TriggerSchedule{.first = utc_from_ns(kEpochNs + 300), .interval = milliseconds(150)}, true);
  StationTime station(utc_from_ns(kEpochNs));
  FlirCamera camera(sdk, station);
  EXPECT_EQ(camera.save(0, FrameRange{.first = 0, .count = 1}, dir.path() / "early.cine").error(), Error::kOutOfRange);
  ASSERT_TRUE(camera.configure(settings()).has_value());
  ASSERT_TRUE(camera.arm(1).has_value());
  const Result<SegmentStatus> status = camera.trigger();
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(status->trigger_time, utc_from_ns(kEpochNs));
  EXPECT_EQ(status->recorded.first, -40);
  const std::filesystem::path path = dir.path() / "segment.cine";
  ASSERT_TRUE(camera.save(0, FrameRange{.first = -2, .count = 5}, path).has_value());
  const Cine cine = read_back(path);
  EXPECT_EQ(cine.first_image_no, -2);
  EXPECT_EQ(cine.trigger_time, utc_from_ns(kEpochNs));
  EXPECT_EQ(cine.width, 16U);
  EXPECT_EQ(cine.bit_count, 16U);
  EXPECT_EQ(cine.real_bpp, 14U);
  EXPECT_EQ(cine.frame_rate, 1004U);
  ASSERT_EQ(cine.frame_times.size(), 5U);
  // Frame 2 is 1,992,032 ns after the trigger, at 1,992,332 ns past the
  // second: stamped 1,992 µs.
  EXPECT_EQ(cine.frame_times[4], utc_from_ns(kEpochNs) + microseconds(1992));
  EXPECT_EQ(cine.exposures[4], microseconds(500));
  // Each frame's pixels follow its 8-byte annotation; frame 2's hold 2.
  const Result<MappedFile> file = MappedFile::open(path);
  EXPECT_EQ(file.value().bytes()[cine.image_offsets[4] + 8], std::byte{2});
  EXPECT_EQ(file.value().bytes()[cine.image_offsets[0] + 8], std::byte{0xFE});
}

TEST(FlirCamera, TakesAMissingYearFromTheTriggerAcrossNewYear) {
  const ScratchDir dir;
  const UtcTime first = utc_from_ns(k2027Ns) - milliseconds(50);
  EmulatedX6980 sdk(TriggerSchedule{.first = first, .interval = milliseconds(150)}, false);
  // Read 5 s into 2027, the trigger's stamp is still 2026's.
  StationTime station(utc_from_ns(k2027Ns) + seconds(5));
  FlirCamera camera(sdk, station);
  ASSERT_TRUE(camera.configure(settings()).has_value());
  ASSERT_TRUE(camera.arm(1).has_value());
  EXPECT_EQ(camera.trigger().value().trigger_time, first);
  const std::filesystem::path path = dir.path() / "new-year.cine";
  ASSERT_TRUE(camera.save(0, FrameRange{.first = -40, .count = 100}, path).has_value());
  const Cine cine = read_back(path);
  EXPECT_LT(cine.frame_times.front(), utc_from_ns(k2027Ns));
  EXPECT_GT(cine.frame_times.back(), utc_from_ns(k2027Ns));
  EXPECT_LT(cine.frame_times.back() - cine.frame_times.front(), milliseconds(100));
  // With no station UTC sampled, a stamp without its year cannot be timed.
  EmulatedX6980 unsampled_sdk(TriggerSchedule{.first = first, .interval = milliseconds(150)}, false);
  StationTime unsampled(utc_from_ns(0));
  FlirCamera unsampled_camera(unsampled_sdk, unsampled);
  ASSERT_TRUE(unsampled_camera.configure(settings()).has_value());
  ASSERT_TRUE(unsampled_camera.arm(1).has_value());
  EXPECT_EQ(unsampled_camera.trigger().error(), Error::kUnavailable);
}

// The emulated SDK, made to fail or to hand over what it should not.
class FaultySdk final : public FlirSdk {
 public:
  enum class Fault {
    kConfigure, kArm, kTrigger, kTriggerStamp, kTriggerOutOfTurn, kRead,
    kExtraFrame, kMissingFrame, kWrongNumber, kWrongPixels, kBadStamp, kNoFrameRate
  };
  explicit FaultySdk(const Fault fault)
      : fault_(fault), sdk_(TriggerSchedule{.first = utc_from_ns(kEpochNs), .interval = milliseconds(150)}, true) {}

  Result<CameraSettings> configure(const CameraSettings& s) override {
    Result<CameraSettings> applied = fault_ == Fault::kConfigure ? fail(Error::kUnavailable) : sdk_.configure(s);
    if (applied && fault_ == Fault::kNoFrameRate) {
      applied->frame_rate = 0;
    }
    return applied;
  }
  Status arm(const std::uint32_t segments) override {
    return fault_ == Fault::kArm ? fail(Error::kUnavailable) : sdk_.arm(segments);
  }
  Result<FlirTrigger> trigger() override {
    Result<FlirTrigger> fired = fault_ == Fault::kTrigger ? fail(Error::kUnavailable) : sdk_.trigger();
    if (!fired) {
      return fired;
    }
    fired->stamp.hours = fault_ == Fault::kTriggerStamp ? 24 : fired->stamp.hours;
    fired->segment += fault_ == Fault::kTriggerOutOfTurn ? 1 : 0;
    return fired;
  }
  Result<std::vector<FlirFrame>> read(const std::uint32_t segment, const FrameRange& frames) override;

 private:
  Fault fault_;
  EmulatedX6980 sdk_;
};

Result<std::vector<FlirFrame>> FaultySdk::read(const std::uint32_t segment, const FrameRange& frames) {
  Result<std::vector<FlirFrame>> out = fault_ == Fault::kRead ? fail(Error::kUnavailable) : sdk_.read(segment, frames);
  if (!out) {
    return out;
  }
  if (fault_ == Fault::kExtraFrame) {
    FlirFrame extra = out->back();
    extra.number += 1;
    out->push_back(extra);
  }
  if (fault_ == Fault::kMissingFrame) {
    out->pop_back();
  }
  out->back().number += fault_ == Fault::kWrongNumber ? 1 : 0;
  out->back().pixels.resize(out->back().pixels.size() + (fault_ == Fault::kWrongPixels ? 1 : 0));
  out->back().stamp.day_of_year = fault_ == Fault::kBadStamp ? 0 : out->back().stamp.day_of_year;
  return out;
}

TEST(FlirCamera, FailsWhereTheSdkFailsOrHandsOverWhatItShouldNot) {
  using Fault = FaultySdk::Fault;
  const ScratchDir dir;
  StationTime station(utc_from_ns(kEpochNs));
  const std::pair<Fault, Error> cases[] = {
      {Fault::kConfigure, Error::kUnavailable},    {Fault::kArm, Error::kUnavailable},
      {Fault::kTrigger, Error::kUnavailable},      {Fault::kTriggerStamp, Error::kMalformed},
      {Fault::kTriggerOutOfTurn, Error::kMalformed}, {Fault::kRead, Error::kUnavailable},
      {Fault::kExtraFrame, Error::kMalformed},     {Fault::kMissingFrame, Error::kMalformed},
      {Fault::kWrongNumber, Error::kMalformed},    {Fault::kWrongPixels, Error::kMalformed},
      {Fault::kBadStamp, Error::kMalformed},       {Fault::kNoFrameRate, Error::kInvalidArgument}};
  for (const auto& [fault, error] : cases) {
    FaultySdk sdk(fault);
    FlirCamera camera(sdk, station);
    const Result<CameraSettings> applied = camera.configure(settings());
    const Status armed = applied ? camera.arm(1) : fail(applied.error());
    const Result<SegmentStatus> fired = armed ? camera.trigger() : fail(armed.error());
    const Status saved =
        fired ? camera.save(0, FrameRange{.first = -1, .count = 3}, dir.path() / "f.cine") : fail(fired.error());
    EXPECT_EQ(saved.error(), error) << static_cast<int>(fault);
  }
  EmulatedX6980 sdk(TriggerSchedule{.first = utc_from_ns(kEpochNs), .interval = milliseconds(150)}, true);
  FlirCamera camera(sdk, station);
  ASSERT_TRUE(camera.configure(settings()).has_value());
  ASSERT_TRUE(camera.arm(1).has_value());
  ASSERT_TRUE(camera.trigger().has_value());
  const FrameRange frames{.first = 0, .count = 1};
  EXPECT_EQ(camera.save(0, frames, dir.path() / "missing" / "f.cine").error(), Error::kUnwritable);
  // Arming again forgets the triggers.
  ASSERT_TRUE(camera.arm(1).has_value());
  EXPECT_EQ(camera.save(0, frames, dir.path() / "f.cine").error(), Error::kOutOfRange);
}

}  // namespace
}  // namespace ics::camera
