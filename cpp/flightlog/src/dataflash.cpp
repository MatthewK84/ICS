#include "ics/flightlog/dataflash.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ics/common/check.hpp"
#include "ics/common/error.hpp"
#include "little_endian.hpp"

namespace ics::flightlog {
namespace {

using detail::read;

constexpr std::array<std::uint8_t, 2> kHead{0xA3, 0x95};
constexpr std::size_t kHeaderBytes = 3;
constexpr std::uint8_t kFmtType = 128;
// An FMT message's body: the type and length it defines, then its name (4
// characters), format characters (16) and comma-separated column names (64).
constexpr std::size_t kNameBytes = 4;
constexpr std::size_t kFormatBytes = 16;
constexpr std::size_t kColumnsBytes = 64;
constexpr std::size_t kFmtLength = kHeaderBytes + 2 + kNameBytes + kFormatBytes + kColumnsBytes;
constexpr double kHundredths = 0.01;
constexpr double kDegreesE7 = 1e-7;

struct TypeSize {
  char type = 0;
  std::size_t size = 0;
};

constexpr std::array<TypeSize, 21> kTypeSizes{{{'a', 64}, {'b', 1},  {'B', 1}, {'h', 2}, {'H', 2}, {'i', 4},
                                               {'I', 4},  {'f', 4},  {'d', 8}, {'n', 4}, {'N', 16}, {'Z', 64},
                                               {'c', 2},  {'C', 2},  {'e', 4}, {'E', 4}, {'L', 4}, {'M', 1},
                                               {'q', 8},  {'Q', 8},  {'g', 2}}};

[[nodiscard]] std::optional<std::size_t> type_size(const char type) noexcept {
  const auto found = std::ranges::find(kTypeSizes, type, &TypeSize::type);
  return found == kTypeSizes.end() ? std::nullopt : std::optional<std::size_t>(found->size);
}

// The layout of a type from its format characters and column names: the
// columns follow the header, and must fill the length exactly.
[[nodiscard]] std::optional<std::vector<DataFlashColumn>> lay_out(const std::string_view format,
                                                                  std::string_view names, const std::size_t length) {
  std::vector<DataFlashColumn> columns;
  std::size_t offset = kHeaderBytes;
  for (const char type : format) {
    const std::size_t comma = std::min(names.find(','), names.size());
    const std::optional<std::size_t> size = type_size(type);
    if (!size || names.empty()) {
      return std::nullopt;
    }
    columns.push_back(DataFlashColumn{.name = std::string(names.substr(0, comma)), .type = type, .offset = offset, .size = *size});
    names.remove_prefix(std::min(comma + 1, names.size()));
    offset += *size;
  }
  if (!names.empty() || offset != length) {
    return std::nullopt;
  }
  return columns;
}

[[nodiscard]] std::optional<DataFlashFormat> parse_format(const std::span<const std::byte> message) {
  static_cast<void>(check(message.size() == kFmtLength));
  const std::span<const std::byte> body = message.subspan(kHeaderBytes);
  const std::string name = detail::text(body.subspan(2, kNameBytes));
  const std::string format = detail::text(body.subspan(2 + kNameBytes, kFormatBytes));
  const std::string names = detail::text(body.subspan(2 + kNameBytes + kFormatBytes, kColumnsBytes));
  const std::size_t length = std::to_integer<std::size_t>(body[1]);
  std::optional<std::vector<DataFlashColumn>> columns = lay_out(format, names, length);
  if (!columns || name.empty()) {
    return std::nullopt;
  }
  return DataFlashFormat{.type = std::to_integer<std::uint8_t>(body[0]), .name = name, .length = length, .columns = std::move(*columns)};
}

[[nodiscard]] DataFlashFormat fmt_format() {
  return DataFlashFormat{.type = kFmtType,
                         .name = "FMT",
                         .length = kFmtLength,
                         .columns = lay_out("BBnNZ", "Type,Length,Name,Format,Columns", kFmtLength).value()};
}

// IEEE 754 half precision: 1 sign bit, 5 exponent bits, 10 fraction bits.
[[nodiscard]] double from_half(const std::uint16_t bits) noexcept {
  constexpr unsigned kFractionBits = 10;
  constexpr unsigned kExponentMask = 0x1FU;
  constexpr unsigned kFractionMask = 0x3FFU;
  constexpr int kBias = 15;
  const unsigned exponent = (static_cast<unsigned>(bits) >> kFractionBits) & kExponentMask;
  const double fraction = static_cast<double>(bits & kFractionMask) / static_cast<double>(1U << kFractionBits);
  const double sign = (bits & 0x8000U) != 0 ? -1.0 : 1.0;
  if (exponent == kExponentMask) {
    return fraction == 0.0 ? sign * std::numeric_limits<double>::infinity() : std::numeric_limits<double>::quiet_NaN();
  }
  const bool normal = exponent != 0;
  return sign * std::ldexp(normal ? 1.0 + fraction : fraction, normal ? static_cast<int>(exponent) - kBias : 1 - kBias);
}

template <typename T>
[[nodiscard]] std::optional<double> scaled(const std::span<const std::byte> message, const std::size_t at,
                                           const double scale = 1.0) noexcept {
  const std::optional<T> value = read<T>(message, at);
  return value ? std::optional<double>(static_cast<double>(*value) * scale) : std::nullopt;
}

}  // namespace

Result<DataFlash> DataFlash::parse(const std::span<const std::byte> log) {
  if (log.size() > kMaxBytes) {
    return fail(Error::kInvalidArgument);
  }
  DataFlash out;
  out.formats_.emplace(kFmtType, fmt_format());
  std::size_t offset = 0;
  while (log.size() - offset >= kHeaderBytes) {
    const bool head = std::to_integer<std::uint8_t>(log[offset]) == kHead[0] &&
                      std::to_integer<std::uint8_t>(log[offset + 1]) == kHead[1];
    const std::uint8_t type = std::to_integer<std::uint8_t>(log[offset + 2]);
    const auto found = head ? out.formats_.find(type) : out.formats_.end();
    if (found == out.formats_.end()) {
      ++out.counts_.skipped_bytes;
      ++offset;
      continue;
    }
    if (log.size() - offset < found->second.length) {
      out.counts_.truncated = true;
      return out;
    }
    const std::span<const std::byte> message = log.subspan(offset, found->second.length);
    out.messages_.push_back(Message{.type = type, .bytes = message});
    offset += found->second.length;
    if (type == kFmtType) {
      std::optional<DataFlashFormat> defined = parse_format(message);
      out.counts_.bad_formats += defined ? 0U : 1U;
      if (defined) {
        out.formats_.insert_or_assign(defined->type, std::move(*defined));
      }
    }
  }
  out.counts_.skipped_bytes += log.size() - offset;
  return out;
}

std::optional<std::reference_wrapper<const DataFlashFormat>> DataFlash::format(const std::string_view name) const {
  const auto found = std::ranges::find_if(formats_, [name](const auto& entry) { return entry.second.name == name; });
  if (found == formats_.end()) {
    return std::nullopt;
  }
  return std::cref(found->second);
}

std::vector<std::span<const std::byte>> DataFlash::messages(const std::string_view name) const {
  const std::optional<std::reference_wrapper<const DataFlashFormat>> wanted = format(name);
  std::vector<std::span<const std::byte>> out;
  for (const Message& message : messages_) {
    if (wanted && message.type == wanted->get().type && message.bytes.size() == wanted->get().length) {
      out.push_back(message.bytes);
    }
  }
  return out;
}

std::optional<DataFlashColumn> find_column(const DataFlashFormat& format, const std::string_view name) {
  const auto found = std::ranges::find(format.columns, name, &DataFlashColumn::name);
  return found == format.columns.end() ? std::nullopt : std::optional<DataFlashColumn>(*found);
}

std::optional<double> read(const DataFlashColumn& column, const std::span<const std::byte> message) noexcept {
  const std::size_t at = column.offset;
  switch (column.type) {
    case 'b':
      return scaled<std::int8_t>(message, at);
    case 'B':
    case 'M':
      return scaled<std::uint8_t>(message, at);
    case 'h':
      return scaled<std::int16_t>(message, at);
    case 'H':
      return scaled<std::uint16_t>(message, at);
    case 'i':
      return scaled<std::int32_t>(message, at);
    case 'I':
      return scaled<std::uint32_t>(message, at);
    case 'q':
      return scaled<std::int64_t>(message, at);
    case 'Q':
      return scaled<std::uint64_t>(message, at);
    case 'f':
      return scaled<float>(message, at);
    case 'd':
      return scaled<double>(message, at);
    case 'c':
      return scaled<std::int16_t>(message, at, kHundredths);
    case 'C':
      return scaled<std::uint16_t>(message, at, kHundredths);
    case 'e':
      return scaled<std::int32_t>(message, at, kHundredths);
    case 'E':
      return scaled<std::uint32_t>(message, at, kHundredths);
    case 'L':
      return scaled<std::int32_t>(message, at, kDegreesE7);
    case 'g': {
      const std::optional<std::uint16_t> bits = read<std::uint16_t>(message, at);
      return bits ? std::optional<double>(from_half(*bits)) : std::nullopt;
    }
    default:
      return std::nullopt;
  }
}

std::optional<std::string> read_text(const DataFlashColumn& column, const std::span<const std::byte> message) {
  const bool text = column.type == 'n' || column.type == 'N' || column.type == 'Z';
  if (!text || column.offset > message.size() || message.size() - column.offset < column.size) {
    return std::nullopt;
  }
  return detail::text(message.subspan(column.offset, column.size));
}

}  // namespace ics::flightlog
