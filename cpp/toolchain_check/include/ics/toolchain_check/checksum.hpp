#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace ics::toolchain_check {

// 32-bit FNV-1a hash of a byte sequence. It exists only to give the
// toolchain check real code to compile, link and test.
[[nodiscard]] std::uint32_t fnv1a32(std::span<const std::byte> bytes) noexcept;

}  // namespace ics::toolchain_check
