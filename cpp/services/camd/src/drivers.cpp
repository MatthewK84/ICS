#include "ics/camd/drivers.hpp"

#include <chrono>
#include <filesystem>
#include <memory>
#include <variant>

#include "ics/camera/emulated_phantom.hpp"
#include "ics/camera/emulated_x6980.hpp"
#include "ics/camera/flir_camera.hpp"
#include "ics/camera/trigger_schedule.hpp"
#include "ics/common/error.hpp"

namespace ics::camd {
namespace {

using camera::CameraSettings;
using camera::FrameRange;
using camera::SegmentStatus;

constexpr Duration kMinTriggerInterval = std::chrono::milliseconds(1);
constexpr Duration kMaxTriggerInterval = std::chrono::minutes(1);

// The X6980 emulator and the FlirCamera that drives it, as one camera.
class EmulatedFlir final : public camera::SegmentCamera {
 public:
  EmulatedFlir(const camera::TriggerSchedule& schedule, const bool stamps_year,
               camera::TimeQualitySource& quality) noexcept
      : sdk_(schedule, stamps_year), camera_(sdk_, quality) {}

  [[nodiscard]] Result<CameraSettings> configure(const CameraSettings& settings) override {
    return camera_.configure(settings);
  }
  [[nodiscard]] Status arm(const std::uint32_t segments) override { return camera_.arm(segments); }
  [[nodiscard]] Result<SegmentStatus> trigger() override { return camera_.trigger(); }
  [[nodiscard]] Status save(const std::uint32_t segment, const FrameRange& frames,
                            const std::filesystem::path& path) override {
    return camera_.save(segment, frames, path);
  }

 private:
  camera::EmulatedX6980 sdk_;
  // Declared after sdk_, which it drives.
  camera::FlirCamera camera_;
};

// The emulators' triggers: trigger_interval apart, from the first whole
// second after now.
[[nodiscard]] camera::TriggerSchedule schedule(const Duration interval, const UtcTime now) noexcept {
  return {.first = std::chrono::ceil<std::chrono::seconds>(now), .interval = interval};
}

// Opens each driver's camera.
struct Opener {
  camera::TimeQualitySource& quality;
  UtcTime now;

  [[nodiscard]] std::unique_ptr<camera::SegmentCamera> operator()(const EmulatedPhantomDriver& driver) const {
    return std::make_unique<camera::EmulatedPhantom>(schedule(driver.trigger_interval, now));
  }
  [[nodiscard]] std::unique_ptr<camera::SegmentCamera> operator()(const EmulatedX6980Driver& driver) const {
    return std::make_unique<EmulatedFlir>(schedule(driver.trigger_interval, now), driver.stamps_year, quality);
  }
};

[[nodiscard]] Duration trigger_interval(config::Reader& table) {
  return table.duration("trigger_interval_ns", kMinTriggerInterval, kMaxTriggerInterval);
}

}  // namespace

DriverSettings read_driver(config::Reader& table) {
  const DriverName name = table.choice("name", kDriverNames);
  if (name == DriverName::kEmulatedX6980) {
    return EmulatedX6980Driver{.trigger_interval = trigger_interval(table),
                               .stamps_year = table.boolean("stamps_year")};
  }
  return EmulatedPhantomDriver{.trigger_interval = trigger_interval(table)};
}

v1::CameraFrameMeta::CameraKind camera_kind(const DriverSettings& driver) noexcept {
  return std::visit([](const auto& settings) { return settings.kKind; }, driver);
}

std::unique_ptr<camera::SegmentCamera> open_camera(const DriverSettings& driver, camera::TimeQualitySource& quality,
                                                   const UtcTime now) {
  return std::visit(Opener{.quality = quality, .now = now}, driver);
}

}  // namespace ics::camd
