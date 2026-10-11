#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <variant>

#include "ics/camera/segment_camera.hpp"
#include "ics/common/units.hpp"
#include "ics/config/reader.hpp"
#include "ics/v1/camera_frame_meta.pb.h"

namespace ics::camd {

// Where a camera's SDK joins ics-camd (ICS-031). A driver opens one kind of
// camera as a camera::SegmentCamera. Each camera's [camera.ID.driver] table
// names its driver and holds that driver's own settings:
//
//   [camera.north-hs.driver]
//   name = "emulated-phantom"
//   trigger_interval_ns = 120_000_000
//
// To add a camera SDK:
//   1. Write a struct of its settings, with the kind of camera it drives as
//      kKind, and add it to DriverSettings.
//   2. Name it in kDriverNames, and read its settings in read_driver
//      (drivers.cpp).
//   3. Open it in drivers.cpp's Opener. The build fails until this is done.
// Nothing else in ics-camd changes: the config, the offload and the gRPC
// service take any SegmentCamera. Opening a camera only makes its driver; a
// driver that connects to its camera does so in configure, so a camera that
// cannot be reached fails its own offload, not the others'.

// The Phantom emulator (ICS-027). Its triggers come trigger_interval apart,
// from the first whole second after it is opened.
struct EmulatedPhantomDriver {
  static constexpr v1::CameraFrameMeta::CameraKind kKind = v1::CameraFrameMeta::CAMERA_KIND_HIGH_SPEED_VISIBLE;
  Duration trigger_interval{};
};

// The FLIR X6980-HS SDK emulator (ICS-028), driven by camera::FlirCamera.
// Its triggers come as the Phantom emulator's do; stamps_year is whether its
// IRIG-B decoder stamps the year, as camera::EmulatedX6980 takes it.
struct EmulatedX6980Driver {
  static constexpr v1::CameraFrameMeta::CameraKind kKind = v1::CameraFrameMeta::CAMERA_KIND_MWIR;
  Duration trigger_interval{};
  bool stamps_year = false;
};

// One camera's driver and its settings.
using DriverSettings = std::variant<EmulatedPhantomDriver, EmulatedX6980Driver>;

// The names a driver table's name setting takes.
enum class DriverName : std::uint8_t { kEmulatedPhantom, kEmulatedX6980 };
inline constexpr std::array<config::Choice<DriverName>, 2> kDriverNames{{
    {.name = "emulated-phantom", .value = DriverName::kEmulatedPhantom},
    {.name = "emulated-x6980", .value = DriverName::kEmulatedX6980},
}};

// Reads a camera's driver table: its name, then that driver's settings.
[[nodiscard]] DriverSettings read_driver(config::Reader& table);

// The kind of camera a driver drives, for its frames' CameraFrameMeta.
[[nodiscard]] v1::CameraFrameMeta::CameraKind camera_kind(const DriverSettings& driver) noexcept;

// Opens a camera with its driver at now, for one offload. quality, the
// station's time quality, must outlive the camera.
[[nodiscard]] std::unique_ptr<camera::SegmentCamera> open_camera(const DriverSettings& driver,
                                                                 camera::TimeQualitySource& quality, UtcTime now);

}  // namespace ics::camd
