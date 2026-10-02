#include "ics/timing/ptp_client.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/testing/no_allocation_scope.hpp"
#include "ics/timing/ptp_management.hpp"
#include "ics/timing/quality.hpp"
#include "ics/timing/unix_socket.hpp"
#include "support.hpp"

namespace {

using ics::Duration;
using ics::timing::PtpClient;
using ics::timing::Snapshot;
using ics::timing::testing::bytes;
using ics::timing::testing::FakePtp4l;
using ics::timing::testing::TempDir;
using ics::timing::testing::with_sequence;
using std::chrono::milliseconds;

constexpr Duration kShortWait = milliseconds(30);
constexpr Duration kLongWait = milliseconds(5'000);

// Sends count one-byte datagrams to client from a blocking socket of its own,
// so each waits for room in the client's queue rather than being refused; a
// send that waits 5 s gives up.
void send_junk(const std::filesystem::path& client, const int count) {
  const ics::timing::Fd socket(::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0));
  const timeval limit{5, 0};
  ::setsockopt(socket.get(), SOL_SOCKET, SO_SNDTIMEO, &limit, sizeof(limit));
  const sockaddr_un address = ics::timing::unix_address(client).value();
  const std::array<std::byte, 1> junk{std::byte{0x0D}};
  for (int i = 0; i < count; ++i) {
    ::sendto(socket.get(), junk.data(), junk.size(), 0, reinterpret_cast<const sockaddr*>(&address),
             sizeof(address));
  }
}

struct Rig {
  TempDir dir;
  FakePtp4l ptp4l{dir / "ptp4l-ro"};
  PtpClient client = PtpClient::open(dir / "ptp4l-ro", dir / "client", 0).value();
};

TEST(PtpClient, ReadsTheFourDataSets) {
  Rig rig;
  rig.ptp4l.answer(rig.dir / "client", 0);
  const ics::Result<Snapshot> snapshot = rig.client.poll(kLongWait);
  ASSERT_TRUE(snapshot.has_value());
  EXPECT_EQ(snapshot->current.offset_from_master, Duration(-191));
  EXPECT_EQ(snapshot->parent.grandmaster_quality.clock_class, 6);
  EXPECT_TRUE(snapshot->time.time_traceable());
  EXPECT_EQ(snapshot->port.port_state, ics::timing::PortState::kSlave);
  // It asked for the four data sets in order, with sequences 0 to 3.
  const std::vector<std::vector<std::byte>> requests = rig.ptp4l.requests();
  ASSERT_EQ(requests.size(), 4U);
  for (std::size_t i = 0; i < requests.size(); ++i) {
    EXPECT_EQ(requests[i].at(31), static_cast<std::byte>(i));
    EXPECT_EQ(requests[i].at(53), static_cast<std::byte>(1 + i));
  }
}

TEST(PtpClient, IgnoresAnswersToAnEarlierPoll) {
  Rig rig;
  rig.ptp4l.answer(rig.dir / "client", 0);
  ASSERT_TRUE(rig.client.poll(kLongWait).has_value());
  rig.ptp4l.answer(rig.dir / "client", 0);
  const ics::Result<Snapshot> stale = rig.client.poll(kShortWait);
  ASSERT_FALSE(stale.has_value());
  EXPECT_EQ(stale.error(), ics::Error::kUnavailable);
  rig.ptp4l.answer(rig.dir / "client", 8);
  EXPECT_TRUE(rig.client.poll(kLongWait).has_value());
}

TEST(PtpClient, SkipsJunkErrorsAndExtraPorts) {
  Rig rig;
  rig.ptp4l.send_to(rig.dir / "client", bytes("0d"));
  rig.ptp4l.send_to(rig.dir / "client", with_sequence(bytes(ics::timing::testing::kError), 2));
  rig.ptp4l.answer(rig.dir / "client", 0);
  // A second port's answer to the same request comes after the first.
  std::vector<std::byte> listening = with_sequence(bytes(ics::timing::testing::kPort), 3);
  listening.at(64) = std::byte{4};
  rig.ptp4l.send_to(rig.dir / "client", listening);
  const ics::Result<Snapshot> snapshot = rig.client.poll(kLongWait);
  ASSERT_TRUE(snapshot.has_value());
  EXPECT_EQ(snapshot->port.port_state, ics::timing::PortState::kSlave);
}

TEST(PtpClient, StopsReadingAfterTooManyDatagrams) {
  Rig rig;
  // More than the 32 a poll reads; the 8 left over fit the client's queue,
  // so the sender finishes.
  std::thread sender(send_junk, rig.dir / "client", 40);
  const auto start = std::chrono::steady_clock::now();
  const ics::Result<Snapshot> snapshot = rig.client.poll(kLongWait);
  const auto took = std::chrono::steady_clock::now() - start;
  sender.join();
  ASSERT_FALSE(snapshot.has_value());
  EXPECT_EQ(snapshot.error(), ics::Error::kUnavailable);
  EXPECT_LT(took, kLongWait);
}

TEST(PtpClient, ReportsASilentOrMissingPtp4l) {
  Rig rig;
  const ics::Result<Snapshot> silent = rig.client.poll(kShortWait);
  ASSERT_FALSE(silent.has_value());
  EXPECT_EQ(silent.error(), ics::Error::kUnavailable);
  PtpClient orphan = PtpClient::open(rig.dir / "no-ptp4l", rig.dir / "orphan", 0).value();
  const ics::Result<Snapshot> missing = orphan.poll(kShortWait);
  ASSERT_FALSE(missing.has_value());
  EXPECT_EQ(missing.error(), ics::Error::kUnavailable);
}

TEST(PtpClient, ReportsSocketsItCannotOpen) {
  const TempDir dir;
  const ics::Result<PtpClient> long_server = PtpClient::open(std::string(200, 'a'), dir / "client", 0);
  ASSERT_FALSE(long_server.has_value());
  EXPECT_EQ(long_server.error(), ics::Error::kInvalidArgument);
  const ics::Result<PtpClient> no_folder = PtpClient::open(dir / "ptp4l", dir / "missing" / "client", 0);
  ASSERT_FALSE(no_folder.has_value());
  EXPECT_EQ(no_folder.error(), ics::Error::kUnavailable);
}

TEST(PtpClient, RemovesItsSocketFileWhenDestroyed) {
  const TempDir dir;
  {
    PtpClient client = PtpClient::open(dir / "ptp4l", dir / "client", 0).value();
    PtpClient moved(std::move(client));
    EXPECT_TRUE(std::filesystem::exists(dir / "client"));
  }
  EXPECT_FALSE(std::filesystem::exists(dir / "client"));
}

TEST(PtpClient, PollsWithoutAllocating) {
  Rig rig;
  rig.ptp4l.answer(rig.dir / "client", 0);
  const ics::testing::NoAllocationScope no_allocation;
  EXPECT_TRUE(rig.client.poll(kLongWait).has_value());
}

}  // namespace
