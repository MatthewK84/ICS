#include "ics/timing/publisher.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "ics/common/error.hpp"
#include "ics/testing/no_allocation_scope.hpp"
#include "ics/timing/unix_socket.hpp"
#include "support.hpp"

namespace {

using ics::timing::Fd;
using ics::timing::Publisher;
using ics::timing::testing::TempDir;

std::span<const std::byte> as_message(const std::string_view text) { return std::as_bytes(std::span(text)); }

Fd subscribe(const std::filesystem::path& path) {
  Fd socket(::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0));
  const sockaddr_un address = ics::timing::unix_address(path).value();
  EXPECT_EQ(::connect(socket.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)), 0);
  return socket;
}

// The next message the subscriber reads, or "" if none comes within a second.
std::string next(const Fd& subscriber) {
  pollfd ready{subscriber.get(), POLLIN, 0};
  std::array<char, 256> buffer{};
  if (::poll(&ready, 1, 1'000) <= 0) {
    return "";
  }
  const ssize_t size = ::recv(subscriber.get(), buffer.data(), buffer.size(), 0);
  return std::string(buffer.data(), static_cast<std::size_t>(std::max<ssize_t>(size, 0)));
}

TEST(Publisher, SendsEachMessageWholeToEverySubscriber) {
  const TempDir dir;
  Publisher publisher = Publisher::open(dir / "timing.sock").value();
  publisher.publish(as_message("before anyone"));
  const Fd first = subscribe(dir / "timing.sock");
  const Fd second = subscribe(dir / "timing.sock");
  publisher.publish(as_message("locked"));
  publisher.publish(as_message("holdover"));
  EXPECT_EQ(publisher.subscribers(), 2U);
  for (const Fd* subscriber : {&first, &second}) {
    EXPECT_EQ(next(*subscriber), "locked");
    EXPECT_EQ(next(*subscriber), "holdover");
  }
}

TEST(Publisher, DropsASubscriberThatLeft) {
  const TempDir dir;
  Publisher publisher = Publisher::open(dir / "timing.sock").value();
  const Fd staying = subscribe(dir / "timing.sock");
  {
    const Fd leaving = subscribe(dir / "timing.sock");
    publisher.publish(as_message("one"));
  }
  publisher.publish(as_message("two"));
  EXPECT_EQ(publisher.subscribers(), 1U);
  EXPECT_EQ(next(staying), "one");
  EXPECT_EQ(next(staying), "two");
}

TEST(Publisher, DropsASubscriberThatStopsReading) {
  const TempDir dir;
  Publisher publisher = Publisher::open(dir / "timing.sock").value();
  const Fd stalled = subscribe(dir / "timing.sock");
  const std::string big(4'096, 'x');
  for (int i = 0; i < 10'000 && (i == 0 || publisher.subscribers() > 0); ++i) {
    publisher.publish(as_message(big));
  }
  EXPECT_EQ(publisher.subscribers(), 0U);
}

TEST(Publisher, KeepsAtMostSixteenSubscribers) {
  const TempDir dir;
  Publisher publisher = Publisher::open(dir / "timing.sock").value();
  std::vector<Fd> subscribers;
  for (std::size_t i = 0; i <= Publisher::kMaxSubscribers; ++i) {
    subscribers.push_back(subscribe(dir / "timing.sock"));
  }
  publisher.publish(as_message("full"));
  EXPECT_EQ(publisher.subscribers(), Publisher::kMaxSubscribers);
  // The last waits to be accepted until one leaves.
  subscribers.front() = Fd();
  publisher.publish(as_message("room"));
  publisher.publish(as_message("again"));
  EXPECT_EQ(publisher.subscribers(), Publisher::kMaxSubscribers);
  EXPECT_EQ(next(subscribers.back()), "again");
}

TEST(Publisher, ReportsAPathItCannotListenOn) {
  const TempDir dir;
  const ics::Result<Publisher> publisher = Publisher::open(dir / "missing" / "timing.sock");
  ASSERT_FALSE(publisher.has_value());
  EXPECT_EQ(publisher.error(), ics::Error::kUnavailable);
}

TEST(Publisher, RemovesItsSocketFileWhenDestroyed) {
  const TempDir dir;
  {
    Publisher publisher = Publisher::open(dir / "timing.sock").value();
    Publisher moved(std::move(publisher));
    EXPECT_TRUE(std::filesystem::exists(dir / "timing.sock"));
  }
  EXPECT_FALSE(std::filesystem::exists(dir / "timing.sock"));
}

TEST(Publisher, PublishesWithoutAllocating) {
  const TempDir dir;
  Publisher publisher = Publisher::open(dir / "timing.sock").value();
  const Fd subscriber = subscribe(dir / "timing.sock");
  const ics::testing::NoAllocationScope no_allocation;
  publisher.publish(as_message("locked"));
  EXPECT_EQ(publisher.subscribers(), 1U);
}

}  // namespace
