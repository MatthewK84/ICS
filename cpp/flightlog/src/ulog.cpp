#include "ics/flightlog/ulog.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "ics/common/check.hpp"
#include "ics/common/error.hpp"
#include "little_endian.hpp"

namespace ics::flightlog {
namespace {

using detail::read;

constexpr std::array<std::uint8_t, 7> kMagic{0x55, 0x4c, 0x6f, 0x67, 0x01, 0x12, 0x35};
constexpr std::size_t kHeaderBytes = 16;
constexpr std::size_t kMessageHeaderBytes = 3;
// The flag-bits message: compatible and incompatible flags, then the file
// offsets of up to three sections of appended data.
constexpr std::size_t kFlagBytes = 8;
constexpr std::size_t kAppendedOffsets = 3;
constexpr std::size_t kFlagBitsBytes = (2 * kFlagBytes) + (kAppendedOffsets * sizeof(std::uint64_t));
constexpr std::uint8_t kDataAppended = 0x01;
// A format whose layout would exceed this cannot be a logged topic: a
// message holds at most 65,535 bytes.
constexpr std::size_t kMaxFormatBytes = std::size_t{1} << 20U;
constexpr std::size_t kMaxCount = std::numeric_limits<std::uint16_t>::max();
constexpr std::size_t kDecimal = 10;

struct BaseType {
  std::string_view name;
  ULogType type = ULogType::kUint8;
  std::size_t size = 0;
};

constexpr std::array<BaseType, 12> kBaseTypes{{{"int8_t", ULogType::kInt8, 1},
                                               {"uint8_t", ULogType::kUint8, 1},
                                               {"int16_t", ULogType::kInt16, 2},
                                               {"uint16_t", ULogType::kUint16, 2},
                                               {"int32_t", ULogType::kInt32, 4},
                                               {"uint32_t", ULogType::kUint32, 4},
                                               {"int64_t", ULogType::kInt64, 8},
                                               {"uint64_t", ULogType::kUint64, 8},
                                               {"float", ULogType::kFloat, 4},
                                               {"double", ULogType::kDouble, 8},
                                               {"bool", ULogType::kBool, 1},
                                               {"char", ULogType::kChar, 1}}};

[[nodiscard]] std::optional<BaseType> base_type(const std::string_view name) noexcept {
  const auto found = std::ranges::find(kBaseTypes, name, &BaseType::name);
  return found == kBaseTypes.end() ? std::nullopt : std::optional<BaseType>(*found);
}

[[nodiscard]] std::string_view as_text(const std::span<const std::byte> bytes) noexcept {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

// A count in decimal, from 0 to kMaxCount: PX4 logs an empty text
// information value as char[0].
[[nodiscard]] std::optional<std::size_t> parse_count(const std::string_view digits) noexcept {
  std::size_t count = 0;
  for (const char digit : digits) {
    if (digit < '0' || digit > '9' || count > kMaxCount) {
      return std::nullopt;
    }
    count = (count * kDecimal) + static_cast<std::size_t>(digit - '0');
  }
  if (digits.empty() || count > kMaxCount) {
    return std::nullopt;
  }
  return count;
}

// One "type name" or "type[count] name" field of a format message.
struct ParsedField {
  std::string_view type;
  std::size_t count = 1;
  std::string_view name;
};

[[nodiscard]] std::optional<ParsedField> parse_field(const std::string_view text) noexcept {
  const std::size_t space = text.find(' ');
  if (space == std::string_view::npos || space == 0 || space + 1 == text.size()) {
    return std::nullopt;
  }
  std::string_view type = text.substr(0, space);
  const std::string_view name = text.substr(space + 1);
  std::size_t count = 1;
  const std::size_t bracket = type.find('[');
  if (bracket != std::string_view::npos) {
    const std::optional<std::size_t> parsed =
        type.back() == ']' ? parse_count(type.substr(bracket + 1, type.size() - bracket - 2)) : std::nullopt;
    if (!parsed || bracket == 0) {
      return std::nullopt;
    }
    count = *parsed;
    type = type.substr(0, bracket);
  }
  return ParsedField{.type = type, .count = count, .name = name};
}

// A message's body as a view, and its type.
struct Message {
  char type = 0;
  std::span<const std::byte> body;
};

}  // namespace

// Reads the messages of a log into a ULog.
struct ULog::Reader {
  ULog& out;
  std::span<const std::byte> log;

  [[nodiscard]] Result<std::vector<std::size_t>> section_ends() const;
  void read_section(std::size_t begin, std::size_t end);
  [[nodiscard]] bool take(const Message& message);
  [[nodiscard]] bool take_definition(const Message& message);
  [[nodiscard]] bool read_format(std::span<const std::byte> body);
  [[nodiscard]] bool read_key_value(std::span<const std::byte> body, bool parameter);
  [[nodiscard]] bool read_add_logged(std::span<const std::byte> body);
  [[nodiscard]] bool read_data(std::span<const std::byte> body);
  [[nodiscard]] bool read_logging(std::span<const std::byte> body, bool tagged);
  void resolve_formats();
  [[nodiscard]] bool lay_out(const std::string& name);
};

// Where each section of the log ends: the appended-data offsets that the
// flag-bits message gives, if the first message is one, then the log's end.
Result<std::vector<std::size_t>> ULog::Reader::section_ends() const {
  std::vector<std::size_t> ends;
  const std::optional<std::uint16_t> size = read<std::uint16_t>(log, kHeaderBytes);
  const bool flag_bits = size && *size >= kFlagBitsBytes && read<char>(log, kHeaderBytes + 2) == 'B' &&
                         log.size() >= kHeaderBytes + kMessageHeaderBytes + kFlagBitsBytes;
  if (flag_bits) {
    const std::span<const std::byte> body = log.subspan(kHeaderBytes + kMessageHeaderBytes, kFlagBitsBytes);
    const std::span<const std::byte> incompatible = body.subspan(kFlagBytes, kFlagBytes);
    const bool unknown = std::ranges::any_of(incompatible.subspan(1), [](const std::byte b) { return b != std::byte{}; });
    if (unknown || (std::to_integer<std::uint8_t>(incompatible[0]) & ~kDataAppended) != 0) {
      return fail(Error::kMalformed);
    }
    for (std::size_t i = 0; i < kAppendedOffsets; ++i) {
      const std::uint64_t at = read<std::uint64_t>(body, (2 * kFlagBytes) + (i * sizeof(std::uint64_t))).value_or(0);
      const bool appended = (std::to_integer<std::uint8_t>(incompatible[0]) & kDataAppended) != 0;
      if (appended && at > (ends.empty() ? kHeaderBytes : ends.back()) && at < log.size()) {
        ends.push_back(static_cast<std::size_t>(at));
      }
    }
  }
  ends.push_back(log.size());
  return ends;
}

void ULog::Reader::read_section(const std::size_t begin, const std::size_t end) {
  std::size_t offset = begin;
  while (offset < end) {
    const std::optional<std::uint16_t> size = read<std::uint16_t>(log.first(end), offset);
    const std::size_t body_end = offset + kMessageHeaderBytes + size.value_or(0);
    if (!size || body_end > end) {
      out.counts_.truncated = true;
      return;
    }
    const Message message{.type = read<char>(log, offset + 2).value_or(0),
                          .body = log.subspan(offset + kMessageHeaderBytes, *size)};
    if (!take(message)) {
      out.counts_.corrupt = true;
      return;
    }
    offset = body_end;
  }
}

bool ULog::Reader::take(const Message& message) {
  switch (message.type) {
    case 'A':
      return read_add_logged(message.body);
    case 'D':
      return read_data(message.body);
    case 'L':
      return read_logging(message.body, false);
    case 'C':
      return read_logging(message.body, true);
    case 'O':
      ++out.counts_.dropouts;
      return true;
    case 'B':
    case 'M':
    case 'Q':
    case 'R':
    case 'S':
      return true;
    default:
      return take_definition(message);
  }
}

bool ULog::Reader::take_definition(const Message& message) {
  switch (message.type) {
    case 'F':
      return read_format(message.body);
    case 'I':
      return read_key_value(message.body, false);
    case 'P':
      return read_key_value(message.body, true);
    default:
      ++out.counts_.unknown_messages;
      return true;
  }
}

bool ULog::Reader::read_format(const std::span<const std::byte> body) {
  const std::string_view text = as_text(body);
  const std::size_t colon = text.find(':');
  if (colon == std::string_view::npos || colon == 0) {
    return false;
  }
  std::vector<RawField> fields;
  std::string_view rest = text.substr(colon + 1);
  while (!rest.empty()) {
    const std::size_t end = std::min(rest.find(';'), rest.size());
    const std::string_view piece = rest.substr(0, end);
    rest.remove_prefix(std::min(end + 1, rest.size()));
    if (piece.empty()) {
      continue;
    }
    const std::optional<ParsedField> field = parse_field(piece);
    if (!field) {
      return false;
    }
    fields.push_back(RawField{.type = std::string(field->type), .count = field->count, .name = std::string(field->name)});
  }
  out.raw_formats_.insert_or_assign(std::string(text.substr(0, colon)), std::move(fields));
  return true;
}

// An information or parameter message: a key "type name", then its value.
// Text information and int32 or float parameters are kept, each first value.
bool ULog::Reader::read_key_value(const std::span<const std::byte> body, const bool parameter) {
  const std::size_t key_size = std::to_integer<std::size_t>(body.empty() ? std::byte{0} : body[0]);
  if (body.empty() || body.size() < 1 + key_size) {
    return false;
  }
  const std::optional<ParsedField> key = parse_field(as_text(body.subspan(1, key_size)));
  const std::span<const std::byte> value = body.subspan(1 + key_size);
  if (!key) {
    return false;
  }
  const std::string name(key->name);
  if (!parameter && key->type == "char") {
    out.info_.try_emplace(name, detail::text(value));
  } else if (parameter && key->type == "int32_t" && key->count == 1) {
    out.parameters_.try_emplace(name, static_cast<double>(read<std::int32_t>(value, 0).value_or(0)));
  } else if (parameter && key->type == "float" && key->count == 1) {
    out.parameters_.try_emplace(name, static_cast<double>(read<float>(value, 0).value_or(0.0F)));
  }
  return true;
}

bool ULog::Reader::read_add_logged(const std::span<const std::byte> body) {
  const std::optional<std::uint8_t> multi_id = read<std::uint8_t>(body, 0);
  const std::optional<std::uint16_t> msg_id = read<std::uint16_t>(body, 1);
  if (!multi_id || !msg_id || body.size() <= 3) {
    return false;
  }
  out.subscriptions_.insert_or_assign(*msg_id,
                                      Subscription{.topic = std::string(as_text(body.subspan(3))), .multi_id = *multi_id});
  return true;
}

bool ULog::Reader::read_data(const std::span<const std::byte> body) {
  const std::optional<std::uint16_t> msg_id = read<std::uint16_t>(body, 0);
  if (!msg_id) {
    return false;
  }
  if (!out.subscriptions_.contains(*msg_id)) {
    ++out.counts_.orphan_samples;
    return true;
  }
  out.samples_.push_back(Sample{.msg_id = *msg_id, .data = body.subspan(sizeof(std::uint16_t))});
  return true;
}

bool ULog::Reader::read_logging(const std::span<const std::byte> body, const bool tagged) {
  const std::size_t at = tagged ? 1 + sizeof(std::uint16_t) : 1;
  const std::optional<std::uint64_t> timestamp = read<std::uint64_t>(body, at);
  if (!timestamp) {
    return false;
  }
  out.strings_.push_back(ULogString{.level = std::to_integer<std::uint8_t>(body[0]),
                                    .timestamp_us = *timestamp,
                                    .text = std::string(as_text(body.subspan(at + sizeof(std::uint64_t))))});
  return true;
}

// Lays out every format whose types are all defined, each after the formats
// it nests: a work list, so a deep or circular definition cannot exhaust the
// stack.
void ULog::Reader::resolve_formats() {
  std::map<std::string_view, std::size_t> waiting;
  std::map<std::string_view, std::vector<std::string_view>> users;
  std::vector<std::string_view> ready;
  for (const auto& [name, fields] : out.raw_formats_) {
    std::size_t nested = 0;
    for (const RawField& field : fields) {
      if (!base_type(field.type)) {
        ++nested;
        users[field.type].push_back(name);
      }
    }
    waiting[name] = nested;
    if (nested == 0) {
      ready.push_back(name);
    }
  }
  while (!ready.empty()) {
    const std::string name(ready.back());
    ready.pop_back();
    if (!lay_out(name)) {
      continue;
    }
    for (const std::string_view user : users[name]) {
      if (--waiting[user] == 0) {
        ready.push_back(user);
      }
    }
  }
  out.counts_.unresolved_formats = out.raw_formats_.size() - out.formats_.size();
}

bool ULog::Reader::lay_out(const std::string& name) {
  ULogFormat format{.name = name, .fields = {}, .size = 0};
  for (const RawField& raw : out.raw_formats_.at(name)) {
    // resolve_formats() lays out a format only after every format it nests.
    const std::optional<BaseType> base = base_type(raw.type);
    static_cast<void>(check(base.has_value() || out.formats_.contains(raw.type)));
    const std::size_t element = base ? base->size : out.formats_.at(raw.type).size;
    if (element * raw.count > kMaxFormatBytes - format.size) {
      return false;
    }
    format.fields.push_back(ULogField{.name = raw.name,
                                      .type = base ? base->type : ULogType::kNested,
                                      .nested = base ? std::string() : raw.type,
                                      .count = raw.count,
                                      .offset = format.size,
                                      .size = element * raw.count});
    format.size += element * raw.count;
  }
  out.formats_.insert_or_assign(name, std::move(format));
  return true;
}

Result<ULog> ULog::parse(const std::span<const std::byte> log) {
  if (log.size() > kMaxBytes) {
    return fail(Error::kInvalidArgument);
  }
  const bool magic = log.size() >= kHeaderBytes &&
                     std::ranges::equal(log.first(kMagic.size()), kMagic,
                                        [](const std::byte b, const std::uint8_t m) { return std::to_integer<std::uint8_t>(b) == m; });
  if (!magic) {
    return fail(Error::kMalformed);
  }
  ULog out;
  Reader reader{.out = out, .log = log};
  const Result<std::vector<std::size_t>> ends = reader.section_ends();
  if (!ends) {
    return fail(ends.error());
  }
  std::size_t begin = kHeaderBytes;
  for (const std::size_t end : *ends) {
    reader.read_section(begin, end);
    begin = end;
  }
  reader.resolve_formats();
  return out;
}

std::optional<std::reference_wrapper<const ULogFormat>> ULog::format(const std::string_view name) const {
  const auto found = formats_.find(name);
  if (found == formats_.end()) {
    return std::nullopt;
  }
  return std::cref(found->second);
}

std::vector<std::span<const std::byte>> ULog::samples(const std::string_view topic, const std::uint8_t multi_id) const {
  std::vector<std::uint16_t> ids;
  for (const auto& [msg_id, subscription] : subscriptions_) {
    if (subscription.topic == topic && subscription.multi_id == multi_id) {
      ids.push_back(msg_id);
    }
  }
  std::vector<std::span<const std::byte>> out;
  for (const Sample& sample : samples_) {
    if (std::ranges::find(ids, sample.msg_id) != ids.end()) {
      out.push_back(sample.data);
    }
  }
  return out;
}

std::optional<double> ULog::parameter(const std::string_view name) const {
  const auto found = parameters_.find(name);
  return found == parameters_.end() ? std::nullopt : std::optional<double>(found->second);
}

std::optional<std::string> ULog::info(const std::string_view key) const {
  const auto found = info_.find(key);
  return found == info_.end() ? std::nullopt : std::optional<std::string>(found->second);
}

std::optional<ULogField> find_field(const ULogFormat& format, const std::string_view name) {
  const auto found = std::ranges::find(format.fields, name, &ULogField::name);
  return found == format.fields.end() ? std::nullopt : std::optional<ULogField>(*found);
}

namespace {

// Where one element of a field lies in a sample, if the field holds numbers.
[[nodiscard]] std::optional<std::size_t> element_offset(const ULogField& field, const std::size_t index) noexcept {
  if (field.type == ULogType::kNested || field.type == ULogType::kChar || index >= field.count) {
    return std::nullopt;
  }
  return field.offset + (index * (field.size / field.count));
}

template <typename T>
[[nodiscard]] std::optional<double> as_double(const std::span<const std::byte> sample, const std::size_t at) noexcept {
  const std::optional<T> value = read<T>(sample, at);
  return value ? std::optional<double>(static_cast<double>(*value)) : std::nullopt;
}

}  // namespace

std::optional<double> read(const ULogField& field, const std::span<const std::byte> sample, const std::size_t index) noexcept {
  const std::optional<std::size_t> at = element_offset(field, index);
  if (!at) {
    return std::nullopt;
  }
  switch (field.type) {
    case ULogType::kFloat:
      return as_double<float>(sample, *at);
    case ULogType::kDouble:
      return as_double<double>(sample, *at);
    case ULogType::kBool: {
      const std::optional<std::uint8_t> value = read<std::uint8_t>(sample, *at);
      return value ? std::optional<double>(*value != 0 ? 1.0 : 0.0) : std::nullopt;
    }
    default: {
      const std::optional<std::int64_t> integer = read_integer(field, sample, index);
      return integer ? std::optional<double>(static_cast<double>(*integer)) : std::nullopt;
    }
  }
}

namespace {

template <typename T>
[[nodiscard]] std::optional<std::int64_t> as_integer(const std::span<const std::byte> sample, const std::size_t at) noexcept {
  const std::optional<T> value = read<T>(sample, at);
  if (!value) {
    return std::nullopt;
  }
  if constexpr (std::is_same_v<T, std::uint64_t>) {
    if (*value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
      return std::nullopt;
    }
  }
  return static_cast<std::int64_t>(*value);
}

}  // namespace

std::optional<std::int64_t> read_integer(const ULogField& field, const std::span<const std::byte> sample,
                                         const std::size_t index) noexcept {
  const std::size_t at = element_offset(field, index).value_or(sample.size());
  switch (field.type) {
    case ULogType::kInt8:
      return as_integer<std::int8_t>(sample, at);
    case ULogType::kUint8:
      return as_integer<std::uint8_t>(sample, at);
    case ULogType::kInt16:
      return as_integer<std::int16_t>(sample, at);
    case ULogType::kUint16:
      return as_integer<std::uint16_t>(sample, at);
    case ULogType::kInt32:
      return as_integer<std::int32_t>(sample, at);
    case ULogType::kUint32:
      return as_integer<std::uint32_t>(sample, at);
    case ULogType::kInt64:
      return as_integer<std::int64_t>(sample, at);
    case ULogType::kUint64:
      return as_integer<std::uint64_t>(sample, at);
    default:
      return std::nullopt;
  }
}

}  // namespace ics::flightlog
