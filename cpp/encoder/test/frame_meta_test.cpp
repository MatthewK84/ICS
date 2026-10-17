// Reading the .meta files ics-camd writes beside each segment.
#include "ics/encoder/frame_meta.hpp"

#include <cstdint>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include <google/protobuf/util/delimited_message_util.h>
#include <gtest/gtest.h>

#include "encoder_support.hpp"
#include "ics/common/error.hpp"
#include "ics/v1/camera_frame_meta.pb.h"

namespace {

using ics::v1::CameraFrameMeta;

[[nodiscard]] std::vector<CameraFrameMeta> frames(const std::uint64_t count) {
  std::vector<CameraFrameMeta> out;
  for (std::uint64_t i = 0; i < count; ++i) {
    CameraFrameMeta frame;
    frame.set_camera_id("north-hs");
    frame.set_frame_index(i);
    frame.set_exposure_start_utc_ns(ics::encoder::testing::kStartNs + static_cast<std::int64_t>(i * 1'000'000));
    frame.set_time_source(CameraFrameMeta::TIME_SOURCE_IRIG);
    out.push_back(frame);
  }
  return out;
}

// The frames as ics-camd's offloader writes them.
[[nodiscard]] std::string written(const std::vector<CameraFrameMeta>& frames) {
  std::ostringstream out;
  for (const CameraFrameMeta& frame : frames) {
    EXPECT_TRUE(google::protobuf::util::SerializeDelimitedToOstream(frame, &out));
  }
  return out.str();
}

TEST(FrameMeta, ReadsWhatTheOffloaderWrites) {
  std::istringstream in(written(frames(3)));
  const auto read = ics::encoder::read_frame_meta(in);
  ASSERT_TRUE(read.has_value());
  ASSERT_EQ(read->size(), 3U);
  EXPECT_EQ(read->at(2).frame_index(), 2U);
  EXPECT_EQ(read->at(2).camera_id(), "north-hs");
  std::istringstream empty;
  EXPECT_TRUE(ics::encoder::read_frame_meta(empty)->empty());
}

TEST(FrameMeta, RefusesAFileThatEndsInsideAFrameOrIsNotFrames) {
  const std::string whole = written(frames(2));
  std::istringstream cut(whole.substr(0, whole.size() - 3));
  EXPECT_EQ(ics::encoder::read_frame_meta(cut).error(), ics::Error::kMalformed);
  // A length of 5, then a field tag with no field.
  std::istringstream garbage(std::string("\x05\x08\xff\xff\xff\xff", 6));
  EXPECT_EQ(ics::encoder::read_frame_meta(garbage).error(), ics::Error::kMalformed);
}

TEST(FrameMeta, RefusesMoreFramesThanAsked) {
  std::istringstream three(written(frames(3)));
  EXPECT_EQ(ics::encoder::read_frame_meta(three, 2).error(), ics::Error::kMalformed);
  std::istringstream two(written(frames(2)));
  EXPECT_EQ(ics::encoder::read_frame_meta(two, 2)->size(), 2U);
}

TEST(FrameMeta, ReadsAFileAndRefusesOneItCannotOpen) {
  const std::filesystem::path path = ics::encoder::testing::write_temp("cine-000.meta", written(frames(4)));
  EXPECT_EQ(ics::encoder::read_frame_meta_file(path)->size(), 4U);
  EXPECT_EQ(ics::encoder::read_frame_meta_file(path.parent_path() / "missing.meta").error(),
            ics::Error::kUnreadable);
  EXPECT_EQ(ics::encoder::read_frame_meta_file(path.parent_path()).error(), ics::Error::kUnreadable);
}

}  // namespace
