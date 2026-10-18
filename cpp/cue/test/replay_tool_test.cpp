// replay_tool_test.cpp: ics-cue-replay's work on segment files as ics-plid writes them (ICS-037).
#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/config/config.hpp>
#include <ics/cue/config.h>
#include <ics/cue/extrapolator.h>
#include <ics/cue/replay.h>
#include <ics/store/crc32c.hpp>
#include <ics/store/segment_format.hpp>
#include <ics/store/segment_writer.hpp>
#include <ics/v1/common.pb.h>
#include <ics/v1/pli.pb.h>
#include <temp_folder.h>

namespace
{

using ics::float64;
using ics::int32;
using ics::int64;
using ics::cue_test::Temp_folder;
using ics::v1::PliRecord;

constexpr int64 start_ns = 1760000000000000000;
constexpr int64 period_ns = 100000000;
constexpr int64 receipt_lag_ns = 100000000;
constexpr int64 tick_ns = 20000000;
constexpr int32 reports = 30;
constexpr float64 metres_per_degree_east = 85000.0;
constexpr std::array<const char*, 1> bare{"ics-cue-replay"};

ics::UtcTime at_ns(const int64 nanoseconds)
{
  return ics::UtcTime{} + ics::Duration(nanoseconds);
}

PliRecord report(const int32 number, const ics::v1::EntityRole role)
{
  PliRecord made;
  made.set_role(role);
  made.set_valid_utc_ns(start_ns + (number * period_ns));
  made.set_received_utc_ns(start_ns + (number * period_ns) + receipt_lag_ns);
  made.set_fix_type(PliRecord::FIX_TYPE_THREE_DIMENSIONAL);
  made.mutable_position()->set_latitude_deg(40.0);
  made.mutable_position()->set_longitude_deg(-100.0 + ((-60.0 + (0.5 * number)) / metres_per_degree_east));
  made.mutable_position()->set_height_ellipsoid_m(735.0);
  made.mutable_velocity_enu_mps()->set_east(5.0);
  return made;
}

// A segment of the target's flight, after one report of another vehicle and one event.
void write_flight(const std::filesystem::path& folder)
{
  ics::Result<ics::store::SegmentWriter> writer = ics::store::SegmentWriter::open(folder, at_ns(start_ns));
  ASSERT_TRUE(writer.has_value());
  PliRecord other = report(0, ics::v1::ENTITY_ROLE_INTERCEPTOR);
  other.clear_received_utc_ns();
  EXPECT_TRUE(writer->append(other).has_value());
  ics::v1::PliEvent armed;
  armed.set_kind(ics::v1::PliEvent::KIND_ARMED);
  EXPECT_TRUE(writer->append(armed).has_value());
  for (int32 number = 0; number < reports; ++number)
  {
    EXPECT_TRUE(writer->append(report(number, ics::v1::ENTITY_ROLE_TARGET)).has_value());
  }
  EXPECT_TRUE(writer->close().has_value());
}

// A segment whose one entry passes its checks but holds no record: a forged CRC over a bad payload.
void write_forged(const std::filesystem::path& folder)
{
  const std::array<std::byte, 2> checked{std::byte{1}, std::byte{8}};
  const ics::uint32 crc = ics::store::crc32c(checked);
  std::ofstream file(folder / ics::store::segment_name(at_ns(start_ns - period_ns)), std::ios::binary);
  for (const std::byte magic : ics::store::kSegmentMagic)
  {
    file.put(std::to_integer<char>(magic));
  }
  const std::array<ics::uint32, 2> words{1U, crc};
  for (const ics::uint32 word : words)
  {
    for (const ics::uint32 shift : std::array<ics::uint32, 4>{0U, 8U, 16U, 24U})
    {
      file.put(static_cast<char>((word >> shift) & 0xFFU));
    }
  }
  file.put(static_cast<char>(1));
  file.put(static_cast<char>(8));
}

std::vector<std::string> lines_of(const std::string& text)
{
  std::vector<std::string> lines;
  std::istringstream stream(text);
  std::string line;
  while (std::getline(stream, line))
  {
    lines.push_back(line);
  }
  return lines;
}

ics::cue::Cue_settings sample()
{
  const auto read = ics::config::read_file(ICS_CUE_EXAMPLE, &ics::cue::read_replay_config);
  EXPECT_TRUE(read.has_value());
  return read.value_or(ics::cue::Cue_settings{});
}

}  // namespace

// JSF deviation D-5: GoogleTest's TEST and EXPECT macros fix the form of the
// test names and expand to code of GoogleTest's own (AV 50, 51 and 209).
TEST(CueReplayTool, WritesACueEvery20Milliseconds)
{
  const Temp_folder folder("writes");
  write_flight(folder.path());
  write_forged(folder.path());
  std::ofstream(folder.path() / "notes.txt") << "not a segment\n";
  std::ostringstream out;
  std::ostringstream errors;
  EXPECT_EQ(ics::cue::run_replay(bare, ICS_CUE_EXAMPLE, folder.path(), out, errors), ics::cue::exit_replayed);
  EXPECT_EQ(errors.str(), "");
  const std::vector<std::string> lines = lines_of(out.str());
  // From the first record's time to the last one's receipt, both ends included.
  ASSERT_EQ(static_cast<int64>(lines.size()), ((((reports - 1) * period_ns) + receipt_lag_ns) / tick_ns) + 1);
  EXPECT_NE(lines.front().find("\"ok\":false,\"error\":\"empty\""), std::string::npos) << lines.front();
  EXPECT_NE(lines.back().find("\"ok\":true"), std::string::npos) << lines.back();
  EXPECT_NE(lines.back().find("\"taken\":30,\"late\":0,\"gated\":0"), std::string::npos) << lines.back();
  EXPECT_NE(lines.back().find("\"not_target\":1,\"unusable\":0}"), std::string::npos) << lines.back();
}

TEST(CueReplayTool, FailsWithNothingToReplay)
{
  const Temp_folder folder("nothing");
  std::ostringstream out;
  std::ostringstream errors;
  EXPECT_EQ(ics::cue::run_replay(bare, ICS_CUE_EXAMPLE, folder.path(), out, errors), ics::cue::exit_failed);
  EXPECT_NE(errors.str().find("no PLI records to replay: empty"), std::string::npos) << errors.str();
}

TEST(CueReplayTool, FailsOnAFolderThatIsNotThere)
{
  const Temp_folder folder("missing");
  std::ostringstream out;
  std::ostringstream errors;
  EXPECT_EQ(ics::cue::replay_folder(sample(), folder.path() / "absent", out, errors), ics::cue::exit_failed);
  EXPECT_NE(errors.str().find("no PLI records to replay: empty"), std::string::npos) << errors.str();
}

TEST(CueReplayTool, FailsOnASegmentItCannotRead)
{
  const Temp_folder folder("unreadable");
  write_flight(folder.path());
  std::filesystem::create_directory(folder.path() / ics::store::segment_name(at_ns(start_ns + period_ns)));
  std::ostringstream out;
  std::ostringstream errors;
  EXPECT_EQ(ics::cue::replay_folder(sample(), folder.path(), out, errors), ics::cue::exit_failed);
  EXPECT_NE(errors.str().find("no PLI records to replay"), std::string::npos) << errors.str();
}

TEST(CueReplayTool, FailsOnSitesThatAreNoPoints)
{
  const Temp_folder folder("sites");
  write_flight(folder.path());
  ics::cue::Cue_settings settings = sample();
  settings.station.latitude_deg = std::numeric_limits<float64>::quiet_NaN();
  std::ostringstream out;
  std::ostringstream errors;
  EXPECT_EQ(ics::cue::replay_folder(settings, folder.path(), out, errors), ics::cue::exit_failed);
  EXPECT_NE(errors.str().find("no point"), std::string::npos) << errors.str();
}

TEST(CueReplayTool, FailsOnAConfigItCannotRead)
{
  const Temp_folder folder("config");
  std::ostringstream out;
  std::ostringstream errors;
  EXPECT_EQ(ics::cue::run_replay(bare, folder.path() / "missing.toml", folder.path(), out, errors),
            ics::cue::exit_failed);
  EXPECT_NE(errors.str().find("missing.toml"), std::string::npos) << errors.str();
}

TEST(CueReplayTool, ExplainsItsUsage)
{
  std::ostringstream out;
  std::ostringstream errors;
  const std::array<const char*, 2> help{"ics-cue-replay", "--help"};
  EXPECT_EQ(ics::cue::run_replay(help, "unused", "unused", out, errors), ics::cue::exit_replayed);
  EXPECT_NE(out.str().find("usage: ics-cue-replay"), std::string::npos);
  const std::array<const char*, 2> extra{"ics-cue-replay", "folder"};
  EXPECT_EQ(ics::cue::run_replay(extra, "unused", "unused", out, errors), ics::cue::exit_usage);
  const std::array<const char*, 3> more{"ics-cue-replay", "--help", "now"};
  EXPECT_EQ(ics::cue::run_replay(more, "unused", "unused", out, errors), ics::cue::exit_usage);
  EXPECT_NE(errors.str().find("usage: ics-cue-replay"), std::string::npos);
}
