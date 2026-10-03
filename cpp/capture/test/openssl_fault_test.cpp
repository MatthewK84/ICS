// OpenSSL that cannot allocate (ICS-020). CRYPTO_set_mem_functions works only
// before OpenSSL's first allocation, so this is the only test in its process.

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <new>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>
#include <openssl/crypto.h>

#include "ics/capture/rotating_writer.hpp"
#include "ics/capture/sha256.hpp"
#include "ics/common/error.hpp"
#include "support.hpp"

namespace {

// OpenSSL's allocations, made with operator new and sized so realloc can
// copy them, until the budget runs out; a negative budget never does.
struct Allocations {
  std::unordered_map<void*, std::size_t> sizes;
  long budget = -1;
};

Allocations& allocations() {
  static Allocations state;
  return state;
}

void* allocate(const std::size_t size, const char* /*file*/, const int /*line*/) {
  Allocations& state = allocations();
  if (state.budget == 0) {
    return nullptr;
  }
  state.budget -= state.budget > 0 ? 1 : 0;
  void* const block = ::operator new(std::max<std::size_t>(size, 1), std::nothrow);
  state.sizes[block] = size;
  return block;
}

void release(void* const block, const char* /*file*/, const int /*line*/) {
  allocations().sizes.erase(block);
  ::operator delete(block);
}

void* reallocate(void* const block, const std::size_t size, const char* const file, const int line) {
  void* const moved = allocate(size, file, line);
  if (moved != nullptr && block != nullptr) {
    std::memcpy(moved, block, std::min(size, allocations().sizes[block]));
    release(block, file, line);
  }
  return moved;
}

TEST(OpenSslFaults, ReportsWhatOpenSslCannotAllocate) {
  ASSERT_EQ(CRYPTO_set_mem_functions(&allocate, &reallocate, &release), 1);
  // OpenSSL sets itself up on first use.
  ics::capture::Sha256 warm = ics::capture::Sha256::make().value();
  ASSERT_TRUE(warm.finish().has_value());
  const ics::timing::testing::TempDir dir;
  const std::vector<std::byte> data(3);

  // Nothing at all: no digest context, so no hash and no writer.
  allocations().budget = 0;
  const auto none = ics::capture::Sha256::make();
  const auto writer = ics::capture::RotatingWriter::make({dir / "", "tap0", 64, 1, {std::chrono::seconds(1), 4096}, 4096});
  // One allocation: the context, whose digest then cannot start.
  allocations().budget = 1;
  auto started = ics::capture::Sha256::make();
  allocations().budget = -1;

  ASSERT_FALSE(none.has_value());
  EXPECT_EQ(none.error(), ics::Error::kUnavailable);
  ASSERT_FALSE(writer.has_value());
  EXPECT_EQ(writer.error(), ics::Error::kUnavailable);
  ASSERT_TRUE(started.has_value());
  started->update(data);
  const auto digest = started->finish();
  ASSERT_FALSE(digest.has_value());
  EXPECT_EQ(digest.error(), ics::Error::kUnavailable);
  // After an unhindered restart, it hashes again.
  started->update(data);
  EXPECT_TRUE(started->finish().has_value());
}

}  // namespace
