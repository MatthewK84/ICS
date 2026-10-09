#include "ics/plid/query_client.hpp"

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "ics/plid/query_server.hpp"
#include "ics/plid/run.hpp"
#include "ics/store/segment_writer.hpp"
#include "plid_support.hpp"
#include "support.hpp"

namespace {

using ics::plid::run_query_client;
using ics::plid::testing::kStart;
using ics::plid::testing::kStartNs;
using ics::plid::testing::Logged;
using ics::timing::testing::TempDir;

// What a run of ics-pli-query wrote, and its exit status.
struct Ran {
  int status = 0;
  std::string out;
  std::string err;
};

Ran query(const std::vector<const char*>& args, const std::filesystem::path& socket) {
  ::testing::internal::CaptureStdout();
  ::testing::internal::CaptureStderr();
  const int status = run_query_client(args, socket, stdout);
  std::fflush(stdout);
  std::string out = ::testing::internal::GetCapturedStdout();
  return Ran{.status = status, .out = std::move(out), .err = ::testing::internal::GetCapturedStderr()};
}

// A store of three records and an event, and a server answering for it.
class Store {
 public:
  explicit Store(const ics::store::QueryLimits limits = {}) {
    ics::store::SegmentWriter writer = ics::store::SegmentWriter::open(folder_ / "", kStart).value();
    for (int index = 0; index < 3; ++index) {
      ics::v1::PliRecord record;
      record.set_entity_id(index == 0 ? "1" : "2");
      record.set_valid_utc_ns(kStartNs + index);
      EXPECT_TRUE(writer.append(record).has_value());
    }
    ics::v1::PliEvent event;
    event.set_entity_id("2");
    event.set_time_utc_ns(kStartNs);
    EXPECT_TRUE(writer.append(event).has_value());
    EXPECT_TRUE(writer.close().has_value());
    server_ = ics::plid::QueryServer::open(socket(), folder_ / "", logged_.logger, limits).value();
  }

  [[nodiscard]] std::filesystem::path socket() const { return folder_ / "query"; }

 private:
  TempDir folder_;
  Logged logged_;
  std::unique_ptr<ics::plid::QueryServer> server_;
};

TEST(QueryClient, WritesEveryRecordThenDone) {
  const Store store;
  const Ran ran = query({"ics-pli-query", "records"}, store.socket());
  EXPECT_EQ(ran.status, ics::plid::kExitStopped);
  EXPECT_EQ(ran.out.find(R"({"kind":"record","record":{"entity_id":"1")"), 0U) << ran.out;
  EXPECT_NE(ran.out.find(R"({"kind":"done","count":3,"truncated":false})"), std::string::npos) << ran.out;
}

TEST(QueryClient, ReadsEveryBatch) {
  // Batches of one record each.
  const Store store({.max_items = 100, .max_response_bytes = 1});
  const Ran ran = query({"ics-pli-query", "records"}, store.socket());
  EXPECT_EQ(ran.status, ics::plid::kExitStopped);
  EXPECT_NE(ran.out.find(R"({"kind":"done","count":3,"truncated":false})"), std::string::npos) << ran.out;
}

TEST(QueryClient, AsksForARangeAndAnEntity) {
  const Store store;
  const std::string start = std::to_string(kStartNs);
  const std::string end = std::to_string(kStartNs + 10);
  const Ran events = query({"ics-pli-query", "events", start.c_str(), end.c_str(), "2"}, store.socket());
  EXPECT_EQ(events.status, ics::plid::kExitStopped);
  EXPECT_NE(events.out.find(R"({"kind":"event","event":{"entity_id":"2")"), std::string::npos) << events.out;
  EXPECT_NE(events.out.find(R"("count":1,)"), std::string::npos);
  const Ran records = query({"ics-pli-query", "records", start.c_str(), end.c_str()}, store.socket());
  EXPECT_NE(records.out.find(R"("count":3,)"), std::string::npos);
}

TEST(QueryClient, FailsATruncatedOrFailedQuery) {
  const Store limited({.max_items = 1});
  const Ran truncated = query({"ics-pli-query", "records"}, limited.socket());
  EXPECT_EQ(truncated.status, ics::plid::kExitFailed);
  EXPECT_NE(truncated.out.find(R"({"kind":"done","count":1,"truncated":true})"), std::string::npos);
  // The server refuses the query with its status, so no batch comes.
  const Ran backwards = query({"ics-pli-query", "records", "10", "5"}, limited.socket());
  EXPECT_EQ(backwards.status, ics::plid::kExitFailed);
  EXPECT_EQ(backwards.out, "");
  EXPECT_EQ(backwards.err, "the query failed: end_utc_ns must be after start_utc_ns\n");
}

TEST(QueryClient, RefusesBadArguments) {
  for (const std::vector<const char*>& args :
       {std::vector<const char*>{"ics-pli-query"}, std::vector<const char*>{"ics-pli-query", "tracks"},
        std::vector<const char*>{"ics-pli-query", "records", "1"},
        std::vector<const char*>{"ics-pli-query", "records", "x", "2"},
        std::vector<const char*>{"ics-pli-query", "records", "1", "2x"},
        std::vector<const char*>{"ics-pli-query", "records", "1", "2", "e", "more"}}) {
    EXPECT_EQ(query(args, "/nowhere").status, ics::plid::kExitUsage) << args.size();
  }
}

TEST(QueryClient, FailsWhenNothingAnswers) {
  const TempDir folder;
  const Ran missing = query({"ics-pli-query", "events"}, folder / "missing");
  EXPECT_EQ(missing.status, ics::plid::kExitFailed);
  EXPECT_EQ(missing.out, "");
  EXPECT_EQ(missing.err.find("no answer from " + (folder / "missing").native() + ": "), 0U) << missing.err;
  EXPECT_EQ(query({"ics-pli-query", "events"}, std::string(200, 'x')).status, ics::plid::kExitFailed);
}

}  // namespace
