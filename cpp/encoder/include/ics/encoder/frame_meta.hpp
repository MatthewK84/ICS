#pragma once

#include <cstddef>
#include <filesystem>
#include <istream>
#include <vector>

#include "ics/common/error.hpp"
#include "ics/v1/camera_frame_meta.pb.h"

namespace ics::encoder {

// The most frames read from one .meta file.
inline constexpr std::size_t kMaxMetaFrames = 10'000'000;

// A camera's .meta file, as ics-camd writes one beside each segment it
// offloads (ICS-031): its frames' CameraFrameMeta, each preceded by its
// length as a varint. kMalformed for a message that does not parse, a file
// that ends inside one, or more than max_frames frames.
[[nodiscard]] Result<std::vector<v1::CameraFrameMeta>> read_frame_meta(std::istream& in,
                                                                       std::size_t max_frames = kMaxMetaFrames);

// The .meta file at path; kUnreadable when it cannot be opened.
[[nodiscard]] Result<std::vector<v1::CameraFrameMeta>> read_frame_meta_file(const std::filesystem::path& path);

}  // namespace ics::encoder
