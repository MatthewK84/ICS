#include "ics/camd/quality_watch.hpp"

#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "camd_support.hpp"
#include "ics/timingd/report_server.hpp"
#include "support.hpp"

namespace {

using ics::camd::QualityWatch;
using ics::timing::testing::TempDir;
using std::chrono::milliseconds;

TEST(CamdQualityWatch, TakesTheLatestReport) {
  const TempDir dir;
  const auto server = ics::camd::testing::serve_report(dir / "time-quality", ics::camd::testing::locked_report());
  QualityWatch watch(dir / "time-quality");
  ASSERT_TRUE(watch.wait_for_report(std::chrono::seconds(5)));
  EXPECT_EQ(watch.current().station_id(), "station-1");
  EXPECT_TRUE(watch.current().irig_b_locked());
}

TEST(CamdQualityWatch, IsReadFromEveryCamerasThread) {
  const TempDir dir;
  const auto server = ics::camd::testing::serve_report(dir / "time-quality", ics::camd::testing::locked_report());
  QualityWatch watch(dir / "time-quality");
  ASSERT_TRUE(watch.wait_for_report(std::chrono::seconds(5)));
  std::vector<std::jthread> readers;
  for (int i = 0; i < 4; ++i) {
    readers.emplace_back([&watch] {
      for (int j = 0; j < 100; ++j) {
        EXPECT_EQ(watch.current().station_id(), "station-1");
      }
    });
  }
}

TEST(CamdQualityWatch, EndsAtOnceWithoutTimingd) {
  const TempDir dir;
  QualityWatch watch(dir / "missing");
  const auto start = std::chrono::steady_clock::now();
  EXPECT_FALSE(watch.wait_for_report(std::chrono::seconds(30)));
  EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(10));
  EXPECT_FALSE(watch.current().irig_b_locked());
}

TEST(CamdQualityWatch, WaitsOnlyAsLongAsAsked) {
  const TempDir dir;
  // ics-timingd, before its first report.
  const auto server = ics::timingd::ReportServer::open(dir / "time-quality");
  ASSERT_TRUE(server.has_value());
  QualityWatch watch(dir / "time-quality");
  EXPECT_FALSE(watch.wait_for_report(milliseconds(200)));
  EXPECT_EQ(watch.current().station_id(), "");
}

}  // namespace
