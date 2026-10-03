#include "ics/capture/output_file.hpp"

#include <cstddef>
#include <fstream>
#include <vector>

#include <gtest/gtest.h>

#include "capture_support.hpp"
#include "support.hpp"

namespace {

using ics::capture::OutputFile;
using ics::capture::testing::FileSizeLimit;
using ics::capture::testing::read_bytes;
using ics::timing::testing::TempDir;

TEST(OutputFile, WritesAndSyncsANewFile) {
  const TempDir folder;
  OutputFile file = OutputFile::create(folder / "a.pcap").value();
  const std::vector<std::byte> bytes{std::byte{1}, std::byte{2}, std::byte{3}};
  ASSERT_TRUE(file.write(bytes).has_value());
  ASSERT_TRUE(file.sync().has_value());
  EXPECT_EQ(read_bytes(folder / "a.pcap"), bytes);
}

TEST(OutputFile, NeverOverwritesAFile) {
  const TempDir folder;
  std::ofstream(folder / "a.pcap") << "kept";
  EXPECT_EQ(OutputFile::create(folder / "a.pcap").error(), ics::Error::kUnwritable);
  EXPECT_EQ(OutputFile::create(folder / "missing" / "a.pcap").error(), ics::Error::kUnwritable);
}

TEST(OutputFile, ReportsAWriteThatStopsPartWay) {
  const TempDir folder;
  OutputFile file = OutputFile::create(folder / "a.pcap").value();
  const std::vector<std::byte> bytes(100, std::byte{7});
  {
    // The first write stops at 60 bytes; the second, for the rest, fails.
    const FileSizeLimit limit(60);
    EXPECT_EQ(file.write(bytes).error(), ics::Error::kUnwritable);
  }
  EXPECT_EQ(read_bytes(folder / "a.pcap").size(), 60U);
}

}  // namespace
