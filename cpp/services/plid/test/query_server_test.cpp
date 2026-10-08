#include "ics/plid/query_server.hpp"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <sys/socket.h>

#include "ics/store/segment_writer.hpp"
#include "plid_support.hpp"
#include "support.hpp"

namespace {

using ics::plid::QueryServer;
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
  return QueryServer::open(folder / "query", folder / "", logger, limits, std::chrono::milliseconds(200)).value();
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
    int records = 0;
    for (const QueryPliResponse& response : responses) {
      records += response.records_size();
    }
    EXPECT_EQ(records, 25);
    EXPECT_TRUE(responses.back().done());
    EXPECT_EQ(everything(folder / "query", QueryPliRequest::KIND_RECORDS).records_size(), 50);
    EXPECT_EQ(answering->served(), 2U);
  }
  EXPECT_TRUE(logged.has(R"("event":"query_answered","kind":1,"sent":25)"));
  EXPECT_FALSE(std::filesystem::exists(folder / "query"));
}

TEST(QueryServer, RefusesARequestThatDoesNotParse) {
  const TempDir folder;
  Logged logged;
  {
    const std::unique_ptr<QueryServer> answering = server(folder, logged.logger);
    const ics::timing::Fd socket(::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0));
    const sockaddr_un address = ics::timing::unix_address(folder / "query").value();
    ASSERT_EQ(::connect(socket.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)), 0);
    const char garbage[] = {'\xFF', '\xFF'};
    ASSERT_EQ(::send(socket.get(), garbage, sizeof(garbage), MSG_NOSIGNAL), 2);
    std::vector<char> buffer(1024);
    const ssize_t got = ::recv(socket.get(), buffer.data(), buffer.size(), 0);
    QueryPliResponse response;
    ASSERT_TRUE(response.ParseFromArray(buffer.data(), static_cast<int>(got)));
    EXPECT_TRUE(response.done());
    EXPECT_EQ(response.error(), "the request is not a QueryPliRequest");
  }
  EXPECT_TRUE(logged.has(R"("event":"query_refused","bytes":2)"));
}

TEST(QueryServer, DropsAClientThatSendsNothing) {
  const TempDir folder;
  Logged logged;
  {
    const std::unique_ptr<QueryServer> answering = server(folder, logged.logger);
    const ics::timing::Fd socket(::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0));
    const sockaddr_un address = ics::timing::unix_address(folder / "query").value();
    ASSERT_EQ(::connect(socket.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)), 0);
    for (int wait = 0; wait < 300 && answering->served() == 0; ++wait) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_EQ(answering->served(), 1U);
  }
  EXPECT_TRUE(logged.has(R"("event":"query_refused","bytes":-1)"));
}

TEST(QueryServer, ReportsASocketItCannotBind) {
  const TempDir folder;
  Logged logged;
  EXPECT_EQ(QueryServer::open(folder / "missing" / "query", folder / "", logged.logger, {}).error(),
            ics::Error::kUnavailable);
  EXPECT_EQ(QueryServer::open(std::string(200, 'x'), folder / "", logged.logger, {}).error(),
            ics::Error::kInvalidArgument);
}

}  // namespace
