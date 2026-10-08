#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <string>
#include <vector>

#include "ics/v1/pli.pb.h"

namespace ics::store::testing {

inline constexpr std::int64_t kStartNs = 1'790'000'000'000'000'000;

// A record for vehicle "uav-<index % 3>", valid index milliseconds after
// kStartNs.
inline v1::PliRecord record(const int index) {
  constexpr std::int64_t kNsPerMs = 1'000'000;
  constexpr int kVehicles = 3;
  v1::PliRecord out;
  out.set_entity_id("uav-" + std::to_string(index % kVehicles));
  out.set_role(v1::ENTITY_ROLE_INTERCEPTOR);
  out.set_source(v1::PLI_SOURCE_MAVLINK);
  out.set_valid_utc_ns(kStartNs + (index * kNsPerMs));
  out.set_time_basis(v1::PLI_TIME_BASIS_VEHICLE_GNSS);
  out.mutable_position()->set_latitude_deg(35.0 + (index * 1e-6));
  out.mutable_position()->set_longitude_deg(-106.5);
  out.mutable_position()->set_height_ellipsoid_m(1500.0 + index);
  out.set_horizontal_sigma_m(1.5);
  out.set_fix_type(v1::PliRecord::FIX_TYPE_RTK_FIXED);
  return out;
}

// An event for vehicle "uav-<index % 3>", index milliseconds after kStartNs.
inline v1::PliEvent event(const int index) {
  constexpr std::int64_t kNsPerMs = 1'000'000;
  constexpr int kVehicles = 3;
  v1::PliEvent out;
  out.set_entity_id("uav-" + std::to_string(index % kVehicles));
  out.set_source(v1::PLI_SOURCE_MAVLINK);
  out.set_time_utc_ns(kStartNs + (index * kNsPerMs));
  out.set_time_basis(v1::PLI_TIME_BASIS_VEHICLE_GNSS);
  out.set_kind(v1::PliEvent::KIND_STATUS_TEXT);
  out.set_detail("status " + std::to_string(index));
  return out;
}

// CRC-32C bit by bit: an oracle independent of ics::store::crc32c.
inline std::uint32_t bitwise_crc32c(const std::vector<std::byte>& bytes) {
  constexpr std::uint32_t kPolynomial = 0x82F63B78U;
  constexpr int kBitsPerByte = 8;
  std::uint32_t crc = ~std::uint32_t{0};
  for (const std::byte byte : bytes) {
    crc ^= std::to_integer<std::uint32_t>(byte);
    for (int bit = 0; bit < kBitsPerByte; ++bit) {
      crc = (crc & 1U) != 0U ? (crc >> 1U) ^ kPolynomial : crc >> 1U;
    }
  }
  return ~crc;
}

// An entry of kind holding payload, framed as the segment format says, with
// a CRC that passes.
inline std::vector<std::byte> raw_entry(const std::uint8_t kind, const std::vector<std::byte>& payload) {
  std::vector<std::byte> body{std::byte{kind}};
  body.insert(body.end(), payload.begin(), payload.end());
  const std::uint32_t crc = bitwise_crc32c(body);
  std::vector<std::byte> out;
  for (const std::uint32_t value : {static_cast<std::uint32_t>(payload.size()), crc}) {
    for (unsigned shift = 0; shift < 32U; shift += 8U) {
      out.push_back(static_cast<std::byte>(value >> shift));
    }
  }
  out.insert(out.end(), body.begin(), body.end());
  return out;
}

inline std::vector<std::byte> file_bytes(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
  std::vector<std::byte> out(text.size());
  std::ranges::transform(text, out.begin(), [](const char c) { return static_cast<std::byte>(c); });
  return out;
}

inline void write_file(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

}  // namespace ics::store::testing
