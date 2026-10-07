#include "ics/timing/camera_offsets.hpp"

#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <sys/resource.h>

#include <gtest/gtest.h>

#include "ics/common/error.hpp"
#include "ics/testing/no_allocation_scope.hpp"
#include "ics/v1/time_quality.pb.h"
#include "support.hpp"

namespace ics::timing {
namespace {

using testing::TempDir;

constexpr std::int64_t kMeasuredNs = 1'791'331'240'000'000'000;

v1::TimeQuality::CameraOffset offset(const std::string& camera, const std::int64_t ns) {
  v1::TimeQuality::CameraOffset out;
  out.set_camera_id(camera);
  out.set_offset_ns(ns);
  out.set_offset_sigma_ns(40);
  out.set_measured_utc_ns(kMeasuredNs);
  return out;
}

CameraOffsets two() { return CameraOffsets{.station_id = "north", .offsets = {offset("phantom-1", 37'400), offset("x6980-1", -112'600)}}; }

std::vector<std::byte> bytes_of(const v1::TimeQuality& message) {
  std::vector<std::byte> out(message.ByteSizeLong());
  EXPECT_TRUE(message.SerializeToArray(out.data(), static_cast<int>(out.size())));
  return out;
}

void put(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
  std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()),
                                              static_cast<std::streamsize>(bytes.size()));
}

TEST(CameraOffsets, ReadsWhatItWrites) {
  const TempDir dir;
  const std::filesystem::path file = dir / "offsets.binpb";
  ASSERT_TRUE(write_camera_offsets(file, two()).has_value());
  EXPECT_FALSE(std::filesystem::exists(dir / "offsets.binpb.tmp"));
  const Result<CameraOffsets> read = read_camera_offsets(file);
  ASSERT_TRUE(read.has_value());
  EXPECT_EQ(read->station_id, "north");
  ASSERT_EQ(read->offsets.size(), 2U);
  EXPECT_EQ(read->offsets[1].camera_id(), "x6980-1");
  EXPECT_EQ(read->offsets[1].offset_ns(), -112'600);
  EXPECT_EQ(read->offsets[1].offset_sigma_ns(), 40);
  EXPECT_EQ(read->offsets[1].measured_utc_ns(), kMeasuredNs);
}

TEST(CameraOffsets, RefusesWhatIsNotAnOffsetsFile) {
  v1::TimeQuality good;
  good.set_station_id("north");
  *good.add_camera_offsets() = offset("phantom-1", 1);
  EXPECT_TRUE(parse_camera_offsets(bytes_of(good)).has_value());
  const std::vector<std::pair<std::string, std::function<void(v1::TimeQuality&)>>> refused{
      {"no station", [](v1::TimeQuality& q) { q.clear_station_id(); }},
      {"a report's field", [](v1::TimeQuality& q) { q.set_time_utc_ns(1); }},
      {"no camera", [](v1::TimeQuality& q) { q.mutable_camera_offsets(0)->clear_camera_id(); }},
      {"negative sigma", [](v1::TimeQuality& q) { q.mutable_camera_offsets(0)->set_offset_sigma_ns(-1); }},
      {"not measured", [](v1::TimeQuality& q) { q.mutable_camera_offsets(0)->set_measured_utc_ns(0); }},
  };
  for (const auto& [name, change] : refused) {
    v1::TimeQuality bad = good;
    change(bad);
    EXPECT_EQ(parse_camera_offsets(bytes_of(bad)).error(), Error::kMalformed) << name;
  }
  // An unknown field 99, varint 1.
  std::vector<std::byte> unknown = bytes_of(good);
  unknown.insert(unknown.end(), {std::byte{0x98}, std::byte{0x06}, std::byte{0x01}});
  EXPECT_EQ(parse_camera_offsets(unknown).error(), Error::kMalformed);
  EXPECT_EQ(parse_camera_offsets(std::vector<std::byte>{std::byte{0xFF}}).error(), Error::kMalformed);
  EXPECT_EQ(parse_camera_offsets(std::vector<std::byte>(kMaxCameraOffsetsBytes + 1)).error(), Error::kMalformed);
}

TEST(CameraOffsets, TellsAMissingFileFromOneItCannotRead) {
  const TempDir dir;
  EXPECT_EQ(read_camera_offsets(dir / "missing").error(), Error::kEmpty);
  // A folder opens, and cannot be read.
  std::filesystem::create_directory(dir / "folder");
  EXPECT_EQ(read_camera_offsets(dir / "folder").error(), Error::kUnreadable);
  // A missing folder is no file too; a path through a file is not.
  EXPECT_EQ(read_camera_offsets(dir / "missing" / "offsets").error(), Error::kEmpty);
  put(dir / "big", std::vector<std::byte>(kMaxCameraOffsetsBytes + 1));
  EXPECT_EQ(read_camera_offsets(dir / "big").error(), Error::kMalformed);
  EXPECT_EQ(read_camera_offsets(dir / "big" / "offsets").error(), Error::kUnreadable);
}

TEST(CameraOffsets, WritesWholeOrNotAtAll) {
  const TempDir dir;
  EXPECT_EQ(write_camera_offsets(dir / "missing" / "offsets", two()).error(), Error::kUnwritable);
  // A folder in the way of the rename.
  std::filesystem::create_directories(dir / "folder" / "inside");
  EXPECT_EQ(write_camera_offsets(dir / "folder", two()).error(), Error::kUnwritable);
  EXPECT_FALSE(std::filesystem::exists(dir / "folder.tmp"));
  // A file size limit of one byte stops the write part way.
  rlimit saved{};
  ASSERT_EQ(::getrlimit(RLIMIT_FSIZE, &saved), 0);
  const auto previous = std::signal(SIGXFSZ, SIG_IGN);
  rlimit tight = saved;
  tight.rlim_cur = 1;
  ASSERT_EQ(::setrlimit(RLIMIT_FSIZE, &tight), 0);
  const Status limited = write_camera_offsets(dir / "limited", two());
  ASSERT_EQ(::setrlimit(RLIMIT_FSIZE, &saved), 0);
  std::signal(SIGXFSZ, previous);
  EXPECT_EQ(limited.error(), Error::kUnwritable);
  EXPECT_FALSE(std::filesystem::exists(dir / "limited"));
}

TEST(CameraOffsets, ReplacesACamerasOffsetOrAddsIt) {
  const CameraOffsets replaced = with_offset(two(), offset("phantom-1", 5));
  ASSERT_EQ(replaced.offsets.size(), 2U);
  EXPECT_EQ(replaced.offsets[0].offset_ns(), 5);
  const CameraOffsets added = with_offset(two(), offset("phantom-2", 6));
  ASSERT_EQ(added.offsets.size(), 3U);
  EXPECT_EQ(added.offsets[2].camera_id(), "phantom-2");
}

TEST(CameraOffsetsWatcher, AnswersWhenTheFileChanges) {
  const TempDir dir;
  const std::filesystem::path file = dir / "offsets.binpb";
  CameraOffsetsWatcher watcher(file);
  const std::optional<Result<CameraOffsets>> first = watcher.poll();
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(first->error(), Error::kEmpty);
  EXPECT_FALSE(watcher.poll().has_value());
  ASSERT_TRUE(write_camera_offsets(file, two()).has_value());
  const std::optional<Result<CameraOffsets>> written = watcher.poll();
  ASSERT_TRUE(written.has_value() && written->has_value());
  EXPECT_EQ((*written)->offsets.size(), 2U);
  {
    const ics::testing::NoAllocationScope no_allocation;
    EXPECT_FALSE(watcher.poll().has_value());
  }
  ASSERT_TRUE(write_camera_offsets(file, with_offset(two(), offset("phantom-2", 6))).has_value());
  std::filesystem::last_write_time(file, std::filesystem::last_write_time(file) + std::chrono::seconds(1));
  const std::optional<Result<CameraOffsets>> rewritten = watcher.poll();
  ASSERT_TRUE(rewritten.has_value() && rewritten->has_value());
  EXPECT_EQ((*rewritten)->offsets.size(), 3U);
  std::filesystem::remove(file);
  const std::optional<Result<CameraOffsets>> removed = watcher.poll();
  ASSERT_TRUE(removed.has_value());
  EXPECT_EQ(removed->error(), Error::kEmpty);
}

}  // namespace
}  // namespace ics::timing
