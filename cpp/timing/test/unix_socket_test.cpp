#include "ics/timing/unix_socket.hpp"

#include <filesystem>
#include <string>
#include <utility>

#include <fcntl.h>
#include <gtest/gtest.h>
#include <sys/socket.h>
#include <unistd.h>

#include "ics/common/error.hpp"
#include "support.hpp"

namespace {

using ics::timing::Fd;
using ics::timing::SocketRole;
using ics::timing::testing::TempDir;

bool is_open(const int fd) { return ::fcntl(fd, F_GETFD) != -1; }

TEST(Fd, ClosesWhatItOwnsOnce) {
  const int raw = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
  {
    Fd first(raw);
    Fd second(std::move(first));
    EXPECT_FALSE(first.valid());
    EXPECT_EQ(second.get(), raw);
    Fd third;
    third = std::move(second);
    EXPECT_FALSE(second.valid());
    EXPECT_TRUE(is_open(raw));
    Fd& same = third;
    third = std::move(same);
    EXPECT_TRUE(is_open(raw));
  }
  EXPECT_FALSE(is_open(raw));
}

TEST(Fd, ClosesTheOldDescriptorWhenAssigned) {
  const int old_fd = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
  Fd holder(old_fd);
  holder = Fd(::open("/dev/null", O_RDONLY | O_CLOEXEC));
  EXPECT_FALSE(is_open(old_fd));
}

TEST(UnixAddress, TakesAPathThatFits) {
  const ics::Result<sockaddr_un> address = ics::timing::unix_address("/var/run/ptp4l-ro");
  ASSERT_TRUE(address.has_value());
  EXPECT_EQ(address->sun_family, AF_UNIX);
  EXPECT_EQ(std::string(&address->sun_path[0]), "/var/run/ptp4l-ro");
}

TEST(UnixAddress, RefusesAnEmptyPathOrOneTooLong) {
  for (const std::filesystem::path& path : {std::filesystem::path(), std::filesystem::path(std::string(108, 'a'))}) {
    const ics::Result<sockaddr_un> address = ics::timing::unix_address(path);
    ASSERT_FALSE(address.has_value());
    EXPECT_EQ(address.error(), ics::Error::kInvalidArgument);
  }
  EXPECT_TRUE(ics::timing::unix_address(std::string(107, 'a')).has_value());
}

TEST(BoundSocket, ReplacesAFileLeftAtThePath) {
  const TempDir dir;
  for (const SocketRole role : {SocketRole::kDatagram, SocketRole::kListener}) {
    const ics::Result<Fd> first = ics::timing::bound_socket(dir / "socket", role);
    ASSERT_TRUE(first.has_value());
    const ics::Result<Fd> second = ics::timing::bound_socket(dir / "socket", role);
    ASSERT_TRUE(second.has_value());
    EXPECT_TRUE(std::filesystem::exists(dir / "socket"));
  }
}

TEST(BoundSocket, ReportsAPathItCannotUse) {
  const TempDir dir;
  const ics::Result<Fd> missing_folder = ics::timing::bound_socket(dir / "missing" / "socket", SocketRole::kListener);
  ASSERT_FALSE(missing_folder.has_value());
  EXPECT_EQ(missing_folder.error(), ics::Error::kUnavailable);
  const ics::Result<Fd> too_long = ics::timing::bound_socket(std::string(200, 'a'), SocketRole::kDatagram);
  ASSERT_FALSE(too_long.has_value());
  EXPECT_EQ(too_long.error(), ics::Error::kInvalidArgument);
}

TEST(BoundSocket, ReportsASocketItCannotMake) {
  const TempDir dir;
  const ics::timing::testing::DescriptorLimit limit;
  const ics::Result<Fd> socket = ics::timing::bound_socket(dir / "socket", SocketRole::kDatagram);
  ASSERT_FALSE(socket.has_value());
  EXPECT_EQ(socket.error(), ics::Error::kUnavailable);
}

}  // namespace
