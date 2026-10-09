#include "ics/plid/query_server.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

#include <grpcpp/grpcpp.h>
#include <gtest/gtest.h>

#include "ics/store/segment_writer.hpp"
#include "plid_support.hpp"
#include "support.hpp"

namespace {

using ics::plid::QueryServer;
using ics::plid::testing::ask;
using ics::plid::testing::everything;
using ics::plid::testing::kStart;
using ics::plid::testing::kStartNs;
using ics::plid::testing::Logged;
using ics::plid::testing::query;
using ics::timing::testing::TempDir;
using ics::v1::QueryPliRequest;
using ics::v1::QueryPliResponse;

void write_records(const std::filesystem::path& folder, const int count) {
  ics::store::SegmentWriter writer = ics::store::SegmentWriter::open(folder, kStart).value();
  for (int index = 0; index < count; ++index) {
    ics::v1::PliRecord record;
    record.set_entity_id("uav-" + std::to_string(index % 2));
    record.set_valid_utc_ns(kStartNs + index);
    ASSERT_TRUE(writer.append(record).has_value());
  }
  ASSERT_TRUE(writer.close().has_value());
}

std::unique_ptr<QueryServer> server(const TempDir& folder, const ics::logging::Logger& logger,
                                    const ics::store::QueryLimits limits = {}) {
  return QueryServer::open(folder / "query", folder / "", logger, limits).value();
}

QueryPliRequest all_records() {
  QueryPliRequest request;
  request.set_kind(QueryPliRequest::KIND_RECORDS);
  request.set_start_utc_ns(INT64_MIN);
  request.set_end_utc_ns(INT64_MAX);
  return request;
}

void wait_until_served(const QueryServer& answering, const std::uint64_t count) {
  for (int wait = 0; wait < 500 && answering.served() < count; ++wait) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

TEST(QueryServer, AnswersAQueryInBatches) {
  const TempDir folder;
  write_records(folder / "", 50);
  Logged logged;
  {
    const std::unique_ptr<QueryServer> answering = server(folder, logged.logger, {.max_response_bytes = 200});
    QueryPliRequest request;
    request.set_kind(QueryPliRequest::KIND_RECORDS);
    request.set_start_utc_ns(kStartNs);
    request.set_end_utc_ns(kStartNs + 50);
    request.set_entity_id("uav-1");
    const std::vector<QueryPliResponse> responses = query(folder / "query", request);
    ASSERT_GT(responses.size(), 2U);
    const int records = std::accumulate(responses.begin(), responses.end(), 0, [](const int sum, const QueryPliResponse& r) {
      return sum + r.records_size();
    });
    EXPECT_EQ(records, 25);
    EXPECT_TRUE(responses.back().done());
    EXPECT_EQ(everything(folder / "query", QueryPliRequest::KIND_RECORDS).records_size(), 50);
    EXPECT_EQ(answering->served(), 2U);
  }
  EXPECT_TRUE(logged.has(R"("event":"query_answered","kind":1,"sent":25)"));
  EXPECT_FALSE(std::filesystem::exists(folder / "query"));
}

TEST(QueryServer, RefusesABadRequestWithItsStatus) {
  const TempDir folder;
  Logged logged;
  {
    const std::unique_ptr<QueryServer> answering = server(folder, logged.logger);
    const ics::plid::testing::Queried refused = ask(folder / "query", QueryPliRequest{});
    EXPECT_EQ(refused.status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
    EXPECT_EQ(refused.status.error_message(), "kind must be KIND_RECORDS or KIND_EVENTS");
    EXPECT_TRUE(refused.responses.empty());
    EXPECT_EQ(answering->served(), 1U);
  }
  EXPECT_TRUE(logged.has(R"("event":"query_refused","error":"kind must be KIND_RECORDS or KIND_EVENTS")"));
}

TEST(QueryServer, StopsAQueryWhoseClientHasGone) {
  const TempDir folder;
  // One record per batch, more than the stream's flow control lets through
  // unread, so the server is still writing when the client goes.
  constexpr int kRecords = 4000;
  write_records(folder / "", kRecords);
  Logged logged;
  const std::unique_ptr<QueryServer> answering = server(folder, logged.logger, {.max_response_bytes = 1});
  {
    const std::unique_ptr<ics::v1::PliQueryService::Stub> stub = ics::plid::testing::stub(folder / "query");
    grpc::ClientContext context;
    const std::unique_ptr<grpc::ClientReader<QueryPliResponse>> reader = stub->QueryPli(&context, all_records());
    QueryPliResponse first;
    ASSERT_TRUE(reader->Read(&first));
    EXPECT_EQ(first.records_size(), 1);
    context.TryCancel();
    EXPECT_EQ(reader->Finish().error_code(), grpc::StatusCode::CANCELLED);
  }
  wait_until_served(*answering, 1);
  EXPECT_EQ(answering->served(), 1U);
  EXPECT_FALSE(logged.has(R"("sent":4000)"));
}

TEST(QueryServer, AnswersQueriesSideBySide) {
  const TempDir folder;
  write_records(folder / "", 200);
  Logged logged;
  const std::unique_ptr<QueryServer> answering = server(folder, logged.logger, {.max_response_bytes = 512});
  std::array<int, 4> counts{};
  std::vector<std::thread> clients;
  for (std::size_t index = 0; index < counts.size(); ++index) {
    clients.emplace_back([&folder, &counts, index] {
      counts.at(index) = everything(folder / "query", QueryPliRequest::KIND_RECORDS).records_size();
    });
  }
  for (std::thread& client : clients) {
    client.join();
  }
  for (const int count : counts) {
    EXPECT_EQ(count, 200);
  }
  EXPECT_EQ(answering->served(), counts.size());
}

TEST(QueryServer, ReplacesAFileLeftAtTheSocket) {
  const TempDir folder;
  std::ofstream(folder / "query") << "left by an earlier run";
  Logged logged;
  const std::unique_ptr<QueryServer> answering = server(folder, logged.logger);
  EXPECT_EQ(everything(folder / "query", QueryPliRequest::KIND_EVENTS).events_size(), 0);
  EXPECT_EQ(answering->served(), 1U);
}

TEST(QueryServer, ReportsASocketItCannotOpen) {
  const TempDir folder;
  Logged logged;
  EXPECT_EQ(QueryServer::open(folder / "missing" / "query", folder / "", logged.logger, {}).error(),
            ics::Error::kUnavailable);
  EXPECT_EQ(QueryServer::open(std::string(200, 'x'), folder / "", logged.logger, {}).error(),
            ics::Error::kInvalidArgument);
}

}  // namespace
