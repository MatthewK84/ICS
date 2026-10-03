#include "ics/capd/report.hpp"

#include <gtest/gtest.h>

#include "capd_support.hpp"
#include "ics/capture/capture.hpp"
#include "ics/capture/rotating_writer.hpp"

namespace {

using ics::capd::log_closed;
using ics::capd::testing::Logged;
using ics::capture::ClosedFile;
using ics::capture::Counters;

const ClosedFile kFile{"/var/lib/ics/capture/tap0-20261003T171500.000000000Z.pcap",
                       "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", 3, 336};

TEST(Report, CountsFromOneFileToTheNext) {
  const Counters counted = ics::capd::since(Counters{100, 5, 2}, Counters{40, 1, 2});
  EXPECT_EQ(counted.received, 60U);
  EXPECT_EQ(counted.dropped, 4U);
  EXPECT_EQ(counted.interface_dropped, 0U);
}

TEST(Report, LogsAClosedFileWithItsHashAndCounters) {
  Logged logged;
  log_closed(logged.logger, "tap0", kFile, Counters{3, 0, 0});
  EXPECT_TRUE(logged.has(
      R"("event":"file_closed","interface":"tap0","path":"/var/lib/ics/capture/tap0-20261003T171500.000000000Z.pcap",)"
      R"("sha256":"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","packets":3,"bytes":336})"));
  EXPECT_TRUE(logged.has(R"("event":"counters","interface":"tap0","received":3,"dropped":0,"interface_dropped":0})"));
  EXPECT_FALSE(logged.has(R"("event":"drops")"));
}

TEST(Report, LogsDropsAsAnError) {
  Logged kernel;
  log_closed(kernel.logger, "tap0", kFile, Counters{3, 2, 0});
  EXPECT_TRUE(kernel.has(R"("level":"error","service":"ics-capd","event":"drops","interface":"tap0","dropped":2,)"
                         R"("interface_dropped":0})"));
  Logged interface;
  log_closed(interface.logger, "tap0", kFile, Counters{3, 0, 1});
  EXPECT_TRUE(interface.has(R"("event":"drops","interface":"tap0","dropped":0,"interface_dropped":1})"));
}

TEST(Report, SaysWhenTheCountersAreUnavailable) {
  Logged logged;
  log_closed(logged.logger, "tap0", kFile, ics::fail(ics::Error::kUnavailable));
  EXPECT_TRUE(logged.has(R"("event":"file_closed")"));
  EXPECT_TRUE(logged.has(R"("event":"counters_unavailable","interface":"tap0","error":"unavailable"})"));
}

}  // namespace
