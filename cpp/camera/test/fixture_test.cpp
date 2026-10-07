// read_cine against PIMS (BSD-3-Clause, github.com/soft-matter/pims), the
// reference ICS reads the cine layout from: fixtures/pims-sample.cine is a
// cine write_cine wrote, and these are the values PIMS 0.7 read from it. See
// fixtures/README.md.
#include <chrono>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "ics/camera/cine.hpp"
#include "ics/camera/mapped_file.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"

namespace ics::camera {
namespace {

TEST(PimsSample, ReadsAsPimsReadsIt) {
  const Result<MappedFile> file = MappedFile::open(ICS_CAMERA_FIXTURES "/pims-sample.cine");
  ASSERT_TRUE(file.has_value());
  const Result<Cine> cine = read_cine(file->bytes());
  ASSERT_TRUE(cine.has_value());
  EXPECT_EQ(cine->frame_times.size(), 7U);
  EXPECT_EQ(cine->first_image_no, -3);
  EXPECT_EQ(cine->width, 16U);
  EXPECT_EQ(cine->height, 8U);
  EXPECT_EQ(cine->bit_count, 16U);
  EXPECT_EQ(cine->frame_rate, 5000U);
  EXPECT_EQ(cine->exposure, Duration(150'000));
  EXPECT_EQ(cine->real_bpp, 12U);
  EXPECT_EQ(to_time64(cine->trigger_time), (std::uint64_t{1'791'331'200} << 32U) | 530'242'871U);
  for (std::size_t i = 0; i < 7; ++i) {
    EXPECT_EQ(to_utc_ns(cine->frame_times[i]), 1'791'331'200'122'856'789 + (static_cast<std::int64_t>(i) * 200'013)) << i;
    EXPECT_EQ(cine->exposures[i], Duration(150'000 + static_cast<std::int64_t>(i))) << i;
  }
}

}  // namespace
}  // namespace ics::camera
