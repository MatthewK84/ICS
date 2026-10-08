#include "ics/plid/import.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "ics/plid/run.hpp"
#include "ics/store/archive.hpp"
#include "ics/store/segment_reader.hpp"
#include "ics/store/segment_writer.hpp"
#include "plid_support.hpp"
#include "support.hpp"

namespace {

using ics::plid::import_sortie;
using ics::plid::ImportConfig;
using ics::plid::ImportCounts;
using ics::plid::testing::geoid;
using ics::plid::testing::kStart;
using ics::plid::testing::Logged;
using ics::timing::testing::TempDir;

ImportConfig config(const std::filesystem::path& store) {
  return ImportConfig{.log = {ics::logging::Level::kDebug, "ics-pli-import"},
                      .store_folder = store,
                      .roles = {"1=target", "2=interceptor"},
                      .range = {.latitude = ics::Degrees(-35.363), .longitude = ics::Degrees(149.165),
                                .height = ics::Meters(600.0)}};
}

// A sortie folder holding the SITL crossing capture and both onboard logs.
void sortie_folder(const std::filesystem::path& folder) {
  std::filesystem::create_directories(folder);
  std::filesystem::copy_file(ICS_MAVLINK_FIXTURES "/crossing.pcap", folder / "tap.pcap");
  std::filesystem::copy_file(ICS_FLIGHTLOG_LOGS "/px4-crossing.ulg", folder / "px4.ulg");
  std::filesystem::copy_file(ICS_FLIGHTLOG_LOGS "/ardupilot-crossing.bin", folder / "ardupilot.bin");
  std::ofstream(folder / "notes.txt") << "not a capture or a log";
}

std::vector<ics::v1::PliRecord> stored_records(const std::filesystem::path& segment) {
  std::vector<ics::v1::PliRecord> out;
  static_cast<void>(ics::store::read_segment(segment, [&out](const ics::store::Entry& entry) {
    ics::v1::PliRecord record;
    if (entry.kind == ics::store::EntryKind::kRecord &&
        record.ParseFromArray(entry.payload.data(), static_cast<int>(entry.payload.size()))) {
      out.push_back(record);
    }
  }));
  return out;
}

TEST(ImportSortie, StoresAlignedPositionsAndTimedLogs) {
  const TempDir folder;
  sortie_folder(folder / "sortie");
  std::filesystem::create_directory(folder / "store");
  std::string reason;
  const ImportCounts counts = import_sortie(config(folder / "store"), folder / "sortie", geoid(), kStart, reason).value();
  EXPECT_EQ(counts.captures, 1U);
  EXPECT_EQ(counts.logs, 2U);
  EXPECT_GT(counts.aligned_records, 0U);
  EXPECT_GT(counts.log_records, 0U);
  EXPECT_GT(counts.log_events, 0U);
  EXPECT_EQ(counts.segment, folder / "store" / ics::store::segment_name(kStart));
  EXPECT_TRUE(ics::store::is_archived(counts.segment));
  const std::vector<ics::v1::PliRecord> records = stored_records(counts.segment);
  ASSERT_EQ(records.size(), counts.aligned_records + counts.log_records);
  std::uint64_t aligned = 0;
  for (const ics::v1::PliRecord& record : records) {
    if (record.source() == ics::v1::PLI_SOURCE_MAVLINK) {
      ++aligned;
      EXPECT_EQ(record.time_basis(), ics::v1::PLI_TIME_BASIS_VEHICLE_ALIGNED);
    }
  }
  EXPECT_EQ(aligned, counts.aligned_records);
  // ArduCopter (system 2) is the interceptor in its log too.
  EXPECT_TRUE(std::ranges::any_of(records, [](const ics::v1::PliRecord& record) {
    return record.source() == ics::v1::PLI_SOURCE_DATAFLASH && record.role() == ics::v1::ENTITY_ROLE_INTERCEPTOR;
  }));
}

TEST(ImportSortie, ImportsLogsWithoutACapture) {
  const TempDir folder;
  std::filesystem::create_directories(folder / "sortie");
  std::filesystem::copy_file(ICS_FLIGHTLOG_LOGS "/ardupilot-crossing.bin", folder / "sortie" / "ardupilot.bin");
  std::string reason;
  const ImportCounts counts = import_sortie(config(folder / ""), folder / "sortie", geoid(), kStart, reason).value();
  EXPECT_EQ(counts.captures, 0U);
  EXPECT_EQ(counts.aligned_records, 0U);
  EXPECT_GT(counts.log_records, 0U);
}

// The reason import_sortie gives, checking its error.
std::string refusal(const ImportConfig& settings, const std::filesystem::path& folder, const ics::Error expected) {
  std::string reason;
  EXPECT_EQ(import_sortie(settings, folder, geoid(), kStart, reason).error(), expected) << reason;
  return reason;
}

TEST(ImportSortie, ExplainsWhatItCannotImport) {
  const TempDir folder;
  std::filesystem::create_directories(folder / "empty");
  EXPECT_NE(refusal(config(folder / ""), folder / "empty", ics::Error::kEmpty).find("no .pcap"), std::string::npos);
  sortie_folder(folder / "sortie");
  ImportConfig bad_role = config(folder / "");
  bad_role.roles = {"1=friend"};
  EXPECT_NE(refusal(bad_role, folder / "sortie", ics::Error::kInvalidArgument).find("friend"), std::string::npos);
  EXPECT_NE(refusal(config(folder / "missing"), folder / "sortie", ics::Error::kUnwritable).find("cannot store"),
            std::string::npos);

  std::filesystem::create_directories(folder / "bad-capture");
  std::ofstream(folder / "bad-capture" / "tap.pcap") << "not a pcap file";
  EXPECT_FALSE(refusal(config(folder / ""), folder / "bad-capture", ics::Error::kUnreadable).empty());

  // A ULog header cut off after its magic.
  std::filesystem::create_directories(folder / "bad-log");
  std::ofstream(folder / "bad-log" / "x.ulg", std::ios::binary) << "ULog\x01\x12\x35\x01";
  EXPECT_NE(refusal(config(folder / ""), folder / "bad-log", ics::Error::kMalformed).find("cannot import"),
            std::string::npos);

  std::filesystem::create_directories(folder / "unreadable-log" / "x.ulg");
  EXPECT_NE(refusal(config(folder / ""), folder / "unreadable-log", ics::Error::kUnreadable).find("cannot read"),
            std::string::npos);
}

TEST(ImportSortie, ExplainsAnArchiveItCannotWrite) {
  const TempDir folder;
  sortie_folder(folder / "sortie");
  std::filesystem::create_directories(folder / "store");
  const std::filesystem::path segment = folder / "store" / ics::store::segment_name(kStart);
  std::filesystem::create_directories(ics::store::archive_paths(segment).records.string() + ".partial/inside");
  EXPECT_NE(refusal(config(folder / "store"), folder / "sortie", ics::Error::kUnwritable).find("cannot store"),
            std::string::npos);
}

TEST(ImportWith, LogsWhatItImported) {
  const TempDir folder;
  sortie_folder(folder / "sortie");
  Logged logged;
  EXPECT_EQ(ics::plid::import_with(config(folder / ""), ICS_EGM96_PATH, folder / "sortie", logged.logger),
            ics::plid::kExitStopped);
  EXPECT_TRUE(logged.has(R"("event":"imported")"));
  EXPECT_TRUE(logged.has(R"("captures":1,"logs":2)"));
  Logged failed;
  EXPECT_EQ(ics::plid::import_with(config(folder / ""), folder / "missing.pgm", folder / "sortie", failed.logger),
            ics::plid::kExitFailed);
  EXPECT_TRUE(failed.has(R"("event":"import_failed")"));
  EXPECT_TRUE(failed.has("cannot read the geoid grid"));
}

TEST(RunImport, ReadsItsConfigFile) {
  const TempDir folder;
  const std::vector<const char*> none;
  EXPECT_EQ(ics::plid::run_import(none, ICS_IMPORT_EXAMPLE, folder / ""), ics::plid::kExitUsage);
  const std::vector<const char*> args{"ics-pli-import"};
  EXPECT_EQ(ics::plid::run_import(args, folder / "missing.toml", folder / ""), ics::plid::kExitUsage);
  // The example's store is not there, and the empty folder holds nothing.
  EXPECT_EQ(ics::plid::run_import(args, ICS_IMPORT_EXAMPLE, folder / ""), ics::plid::kExitFailed);
}

}  // namespace
