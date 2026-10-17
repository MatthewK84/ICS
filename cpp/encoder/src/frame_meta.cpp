#include "ics/encoder/frame_meta.hpp"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <istream>
#include <system_error>
#include <vector>

#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/util/delimited_message_util.h>

#include "ics/common/error.hpp"
#include "ics/v1/camera_frame_meta.pb.h"

namespace ics::encoder {

Result<std::vector<v1::CameraFrameMeta>> read_frame_meta(std::istream& in, const std::size_t max_frames) {
  google::protobuf::io::IstreamInputStream stream(&in);
  std::vector<v1::CameraFrameMeta> frames;
  v1::CameraFrameMeta frame;
  bool clean_eof = false;
  while (google::protobuf::util::ParseDelimitedFromZeroCopyStream(&frame, &stream, &clean_eof)) {
    if (frames.size() >= max_frames) {
      return fail(Error::kMalformed);
    }
    frames.push_back(frame);
  }
  if (!clean_eof) {
    return fail(Error::kMalformed);
  }
  return frames;
}

Result<std::vector<v1::CameraFrameMeta>> read_frame_meta_file(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  std::error_code error;
  if (!file.is_open() || !std::filesystem::is_regular_file(path, error)) {
    return fail(Error::kUnreadable);
  }
  return read_frame_meta(file);
}

}  // namespace ics::encoder
