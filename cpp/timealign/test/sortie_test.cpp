#include "ics/timealign/sortie.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "ics/timealign/clock_fit.hpp"

namespace ics::timealign {
namespace {

TEST(SortieCounter, StartsASortieWhenTheBootTimeGoesBackMoreThanTheGap) {
  SortieCounter counter;
  std::vector<std::size_t> sorties;
  // Out of order by under a second, then a reboot, then another.
  for (const std::int64_t boot_us : {5'000'000, 6'000'000, 5'500'000, 7'000'000, 200'000, 900'000, 100}) {
    sorties.push_back(counter.sortie(boot_us));
  }
  EXPECT_EQ(sorties, (std::vector<std::size_t>{0, 0, 0, 0, 1, 1, 1}));
  EXPECT_EQ(counter.sortie(-5'000'000), 1U);
}

TEST(SortieCounter, TakesBootTimesOutsideThoseIcsTakesAsTheNearest) {
  SortieCounter counter(std::chrono::milliseconds(10));
  EXPECT_EQ(counter.sortie(kMaxBootUs + 1'000'000), 0U);
  EXPECT_EQ(counter.sortie(kMaxBootUs), 0U);
  EXPECT_EQ(counter.sortie(-1), 1U);
  EXPECT_EQ(counter.sortie(0), 1U);
}

}  // namespace
}  // namespace ics::timealign
