// tracker_test.cpp: the tracker's modes, demands and refusals (ICS-038).
#include <array>
#include <chrono>
#include <cstddef>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <utility>

#include <gtest/gtest.h>

#include <frame_maker.h>
#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/encoder/pointing_model.hpp>
#include <ics/encoder/track.hpp>
#include <ics/mount/pwi4_status.hpp>
#include <ics/mount/rate_loop.hpp>
#include <ics/testing/no_allocation_scope.hpp>
#include <ics/tracker/camera.h>
#include <ics/tracker/centroid.h>
#include <ics/tracker/estimator.h>
#include <ics/tracker/geometry.h>
#include <ics/tracker/tracker.h>

namespace
{

using ics::float64;
using ics::int32;
using ics::encoder::Direction;
using ics::tracker::Angle_state;
using ics::tracker::Mode;
using ics::tracker::Take;
using ics::tracker::Tracker;
using ics::tracker::Tracker_settings;
using ics::tracker_test::Frame_maker;

constexpr int32 width = 64;
constexpr int32 height = 48;
constexpr float64 boresight_x = 32.0;
constexpr float64 boresight_y = 24.0;
constexpr float64 plate_scale = 1.0E-4;
constexpr float64 sky = 1000.0;
constexpr float64 spot_sigma = 1.2;
constexpr float64 spot_amplitude = 2000.0;
constexpr float64 k_sigma = 5.0;
constexpr float64 min_contrast = 100.0;
constexpr int32 max_area = 200;
constexpr int32 acquire_side = 40;
constexpr int32 track_side = 16;
constexpr float64 gate_px = 3.0;
constexpr int32 confirm_frames = 3;
constexpr float64 alpha = 0.5;
constexpr float64 beta = 0.1;
constexpr ics::Duration coast = std::chrono::milliseconds(50);
constexpr ics::Duration latency = std::chrono::milliseconds(20);
constexpr ics::Duration step = std::chrono::milliseconds(10);
constexpr ics::Duration processing = std::chrono::milliseconds(1);
constexpr float64 gain = 2.0;
constexpr float64 mount_azimuth = 1.0;
constexpr float64 mount_elevation = 0.5;
constexpr float64 target_offset = 0.0008;
constexpr float64 cue_error = 0.0002;
constexpr float64 target_rate = 0.01;
constexpr float64 rate_tolerance = 0.02;
constexpr float64 jump_px = 6.0;
constexpr float64 far_away = 0.2;
constexpr int32 coasting_frames = 10;
constexpr float64 bad = std::numeric_limits<float64>::quiet_NaN();
constexpr std::size_t azimuth_axis = static_cast<std::size_t>(ics::mount::Axis::kAzimuth);

const ics::UtcTime start = ics::UtcTime{} + std::chrono::seconds(100);

Tracker_settings settings()
{
  return Tracker_settings{{boresight_x, boresight_y, plate_scale, 0.0, false},
                          {ics::tracker::Polarity::bright, k_sigma, min_contrast, 1, max_area},
                          {alpha, beta},
                          {acquire_side, acquire_side},
                          {track_side, track_side},
                          gate_px,
                          confirm_frames,
                          coast,
                          latency,
                          gain};
}

// The mount, pointed still at one direction unless told it is lost.
class Still_mount final : public ics::tracker::Axis_source
{
public:
  ics::Result<ics::encoder::Interpolated> axes_at(const ics::UtcTime when) override
  {
    ics::Result<ics::encoder::Interpolated> axes = ics::fail(ics::Error::kOutOfRange);
    if (known)
    {
      ics::encoder::Interpolated here{};
      here.utc = when;
      here.encoder = Direction{mount_azimuth, mount_elevation};
      axes = here;
    }
    return axes;
  }

  bool known = true;
};

// A cue, when there is one.
class Scripted_cue final : public ics::tracker::Cue_source
{
public:
  ics::Result<Angle_state> cue_at(const ics::UtcTime when) override
  {
    ics::Result<Angle_state> cue = ics::fail(ics::Error::kUnconstrained);
    if (script.has_value())
    {
      cue = ics::tracker::carried(*script, when);
    }
    return cue;
  }

  std::optional<Angle_state> script;
};

class Manual_time final : public ics::tracker::Time_source
{
public:
  ics::UtcTime now() override
  {
    return utc;
  }

  ics::UtcTime utc{};
};

// The target, moving east at target_rate from target_offset east of the mount.
Direction target_at(const ics::UtcTime when)
{
  const float64 seconds = std::chrono::duration<float64>(when - start).count();
  return {mount_azimuth + target_offset + (target_rate * seconds), mount_elevation};
}

// A tracker's world: the mount, the cue, the time and the frames.
class World
{
public:
  World()
  {
    const std::array<ics::encoder::Coefficient, 0> none{};
    model_ = *ics::encoder::PointingModel::make(none);
    tracker_ = std::move(*Tracker::make(settings(), *model_, mount, cue, clock));
  }

  // A frame at when, with the target painted where it is unless hidden, and
  // the time set to just after it.
  ics::tracker::Frame frame_of(const ics::UtcTime when, const bool shown)
  {
    maker_ = Frame_maker(width, height, sky);
    const ics::Result<ics::tracker::Pixel_point> pixel =
      ics::tracker::pixel_of_sky(target_at(when), {mount_azimuth, mount_elevation}, settings().camera);
    if (shown && pixel.has_value())
    {
      maker_.spot(pixel->x + offset_px, pixel->y, spot_sigma, spot_amplitude);
    }
    clock.utc = when + processing;
    return maker_.frame(when);
  }

  // Takes such a frame.
  Take frame_at(const ics::UtcTime when, const bool shown)
  {
    return tracker_->take(frame_of(when, shown));
  }

  void cue_target()
  {
    const Direction aim = target_at(start);
    cue.script = Angle_state{start, aim.azimuth_rad + cue_error, aim.elevation_rad, target_rate, 0.0};
  }

  Tracker& tracker()
  {
    return *tracker_;
  }

  Still_mount mount;
  Scripted_cue cue;
  Manual_time clock;
  float64 offset_px = 0.0;

private:
  std::optional<ics::encoder::PointingModel> model_;
  std::unique_ptr<Tracker> tracker_;
  Frame_maker maker_{width, height, sky};
};

// Frames from start, count of them, shown or hidden; the last frame's take.
Take run(World& world,
         const int32 first,
         const int32 count,
         const bool shown)
{
  Take taken = Take::unusable;
  for (int32 index = first; index < (first + count); ++index)
  {
    taken = world.frame_at(start + (index * step), shown);
  }
  return taken;
}

}  // namespace

// JSF deviation D-5: GoogleTest's TEST and EXPECT macros fix the form of the
// test names and expand to code of GoogleTest's own (AV 50, 51 and 209).
TEST(Tracker, AcquiresTheCuedTargetAndFollowsIt)
{
  World world;
  world.cue_target();
  EXPECT_EQ(world.tracker().latest(), std::nullopt);
  EXPECT_EQ(world.frame_at(start, true), Take::detected);
  EXPECT_EQ(world.tracker().mode(), Mode::acquiring);
  EXPECT_EQ(run(world, 1, confirm_frames - 1, true), Take::detected);
  EXPECT_EQ(world.tracker().mode(), Mode::tracking);
  const ics::tracker::Frame next = world.frame_of(start + (confirm_frames * step), true);
  ics::Result<Take> taken = ics::fail(ics::Error::kEmpty);
  {
    const ics::testing::NoAllocationScope no_allocation;
    taken = world.tracker().take(next);
  }
  EXPECT_EQ(taken, Take::detected);
  const std::optional<ics::mount::RateDemand> demand = world.tracker().latest();
  ASSERT_TRUE(demand.has_value());
  EXPECT_NEAR(demand->rates.rad_per_s[azimuth_axis], target_rate + (gain * target_offset), rate_tolerance);
  EXPECT_EQ(world.tracker().counts().acquisitions, 1U);
}

TEST(Tracker, CoastsThroughDropoutsAndReturnsToTheCue)
{
  World world;
  world.cue_target();
  static_cast<void>(run(world, 0, confirm_frames, true));
  EXPECT_EQ(world.frame_at(start + (confirm_frames * step), false), Take::missed);
  EXPECT_EQ(world.tracker().mode(), Mode::coasting);
  EXPECT_EQ(world.frame_at(start + ((confirm_frames + 1) * step), true), Take::detected);
  EXPECT_EQ(world.tracker().mode(), Mode::tracking);
  static_cast<void>(run(world, confirm_frames + 2, coasting_frames, false));
  EXPECT_EQ(world.tracker().mode(), Mode::cueing);
  EXPECT_EQ(world.tracker().counts().losses, 1U);
  EXPECT_GE(world.tracker().counts().misses, static_cast<ics::uint64>(coasting_frames));
}

TEST(Tracker, ReturnsToTheCueWhenAnAcquisitionIsNotConfirmed)
{
  World world;
  world.cue_target();
  EXPECT_EQ(world.frame_at(start, true), Take::detected);
  EXPECT_EQ(world.frame_at(start + step, false), Take::missed);
  EXPECT_EQ(world.tracker().mode(), Mode::cueing);
  EXPECT_EQ(world.frame_at(start + (2 * step), false), Take::missed);
}

TEST(Tracker, SearchesAroundTheBoresightWithoutACueAndThenFollowsTheTrack)
{
  World world;
  EXPECT_EQ(world.frame_at(start, false), Take::missed);
  EXPECT_EQ(world.tracker().latest(), std::nullopt);
  EXPECT_EQ(world.frame_at(start + step, true), Take::detected);
  static_cast<void>(run(world, 2, confirm_frames, true));
  EXPECT_EQ(world.tracker().mode(), Mode::tracking);
  EXPECT_TRUE(world.tracker().latest().has_value());
}

TEST(Tracker, LeavesOutDetectionsOutsideTheGate)
{
  World world;
  world.cue_target();
  static_cast<void>(run(world, 0, confirm_frames, true));
  world.offset_px = jump_px;
  EXPECT_EQ(world.frame_at(start + (confirm_frames * step), true), Take::missed);
  EXPECT_EQ(world.tracker().mode(), Mode::coasting);
}

TEST(Tracker, CannotUseFramesWithoutTheMountOrOutOfOrder)
{
  World world;
  world.cue_target();
  world.mount.known = false;
  EXPECT_EQ(world.frame_at(start, true), Take::unusable);
  world.mount.known = true;
  EXPECT_EQ(world.frame_at(start, true), Take::detected);
  EXPECT_EQ(world.frame_at(start, true), Take::unusable);
  EXPECT_EQ(world.tracker().counts().unusable, 2U);
  EXPECT_EQ(world.tracker().counts().frames, 3U);
}

TEST(Tracker, FindsNothingWhereTheCuePointsOffTheFrameOrBehind)
{
  World world;
  world.cue.script = Angle_state{start, mount_azimuth + far_away, mount_elevation, 0.0, 0.0};
  EXPECT_EQ(world.frame_at(start, true), Take::missed);
  world.cue.script = Angle_state{start, mount_azimuth + std::numbers::pi, mount_elevation, 0.0, 0.0};
  EXPECT_EQ(world.frame_at(start + step, true), Take::missed);
}

TEST(Tracker, RefusesSettingsThatCannotRun)
{
  EXPECT_TRUE(ics::tracker::valid(settings()).has_value());
  const auto refused = [](const Tracker_settings& changed)
  {
    return !ics::tracker::valid(changed).has_value();
  };
  Tracker_settings changed = settings();
  changed.camera.boresight_x = bad;
  EXPECT_TRUE(refused(changed));
  changed = settings();
  changed.camera.boresight_y = bad;
  EXPECT_TRUE(refused(changed));
  changed = settings();
  changed.camera.plate_scale_rad = 0.0;
  EXPECT_TRUE(refused(changed));
  changed = settings();
  changed.camera.roll_rad = bad;
  EXPECT_TRUE(refused(changed));
  changed = settings();
  changed.centroid.min_area = 0;
  EXPECT_TRUE(refused(changed));
  changed = settings();
  changed.centroid.min_area = max_area + 1;
  EXPECT_TRUE(refused(changed));
  changed = settings();
  changed.centroid.k_sigma = -1.0;
  EXPECT_TRUE(refused(changed));
  changed = settings();
  changed.centroid.min_contrast = -1.0;
  EXPECT_TRUE(refused(changed));
  changed = settings();
  changed.acquire_window = {2, 2};
  EXPECT_TRUE(refused(changed));
  changed = settings();
  changed.track_window = {track_side, 2};
  EXPECT_TRUE(refused(changed));
  changed = settings();
  changed.acquire_window = {ics::tracker::max_window_side + 1, track_side};
  EXPECT_TRUE(refused(changed));
  changed = settings();
  changed.acquire_window = {track_side, ics::tracker::max_window_side + 1};
  EXPECT_TRUE(refused(changed));
  changed = settings();
  changed.gate_px = bad;
  EXPECT_TRUE(refused(changed));
  changed = settings();
  changed.gate_px = 0.0;
  EXPECT_TRUE(refused(changed));
  changed = settings();
  changed.confirm_frames = 0;
  EXPECT_TRUE(refused(changed));
  changed = settings();
  changed.coast_limit = -coast;
  EXPECT_TRUE(refused(changed));
  changed = settings();
  changed.command_latency = -latency;
  EXPECT_TRUE(refused(changed));
  changed = settings();
  changed.gain = bad;
  EXPECT_TRUE(refused(changed));
  changed = settings();
  changed.gain = -gain;
  EXPECT_TRUE(refused(changed));
  changed = settings();
  changed.estimator.alpha = 0.0;
  EXPECT_TRUE(refused(changed));
  Still_mount mount;
  Scripted_cue cue;
  Manual_time time;
  const std::array<ics::encoder::Coefficient, 0> none{};
  const ics::encoder::PointingModel model = *ics::encoder::PointingModel::make(none);
  EXPECT_EQ(Tracker::make(changed, model, mount, cue, time).error(), ics::Error::kInvalidArgument);
}

TEST(Tracker, TellsTheTimeByTheHostsClock)
{
  ics::tracker::System_time clock;
  const ics::UtcTime now = clock.now();
  EXPECT_LT(std::chrono::abs(now - std::chrono::time_point_cast<ics::Duration>(std::chrono::system_clock::now())),
            std::chrono::seconds(1));
}
