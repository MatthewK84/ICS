#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ics/common/error.hpp"

namespace ics::flightlog {

// PX4's ULog format (ICS-025), as PX4's "ULog File Format" page defines it:
// a 16-byte header, then messages of a 2-byte size, a 1-byte type and a body.
// Format messages define each topic's fields; add-logged messages give a
// topic instance a message ID; data messages carry a sample of it.

enum class ULogType : std::uint8_t {
  kInt8,
  kUint8,
  kInt16,
  kUint16,
  kInt32,
  kUint32,
  kInt64,
  kUint64,
  kFloat,
  kDouble,
  kBool,
  kChar,
  // Another format, named by ULogField::nested.
  kNested,
};

struct ULogField {
  std::string name;
  ULogType type = ULogType::kUint8;
  // The format of a nested field.
  std::string nested;
  // How many elements: 1 for a scalar.
  std::size_t count = 1;
  // Where the field starts in a sample, and how many bytes all its elements take.
  std::size_t offset = 0;
  std::size_t size = 0;
};

struct ULogFormat {
  std::string name;
  std::vector<ULogField> fields;
  std::size_t size = 0;
};

// A logged string: a printf-style message, or a tagged one.
struct ULogString {
  std::uint8_t level = 0;
  std::uint64_t timestamp_us = 0;
  std::string text;
};

struct ULogCounts {
  // Messages of a type this reader does not know, skipped as the format asks.
  std::size_t unknown_messages = 0;
  // Formats naming a type never defined, or defined through themselves, or
  // too large; their topics cannot be read.
  std::size_t unresolved_formats = 0;
  // Data messages for a message ID no add-logged message gave.
  std::size_t orphan_samples = 0;
  // Dropout messages: gaps the logger could not write.
  std::size_t dropouts = 0;
  // Whether a section ends partway through a message, as a log does when
  // the logger stopped while writing.
  bool truncated = false;
  // Whether reading stopped at a malformed message.
  bool corrupt = false;
};

// A parsed ULog log. It holds views into the log's bytes, which must outlive
// it.
class ULog {
 public:
  static constexpr std::size_t kMaxBytes = std::size_t{1} << 30U;

  // Reads a log. Fails with Error::kInvalidArgument for one larger than
  // kMaxBytes, and with Error::kMalformed for one without the ULog header or
  // with an incompatible flag this reader does not know. A malformed or
  // truncated message ends a section of the log instead (counts()), so a
  // damaged log keeps what came before the damage. Data appended after a
  // crash, at the offsets the flag-bits message gives, is read too.
  [[nodiscard]] static Result<ULog> parse(std::span<const std::byte> log);

  // A topic's format, once every type it uses is defined.
  [[nodiscard]] std::optional<std::reference_wrapper<const ULogFormat>> format(std::string_view name) const;

  // The samples of one instance of a topic, in log order.
  [[nodiscard]] std::vector<std::span<const std::byte>> samples(std::string_view topic, std::uint8_t multi_id) const;

  // A parameter's first value in the log.
  [[nodiscard]] std::optional<double> parameter(std::string_view name) const;

  // A text information message's value, such as "sys_name".
  [[nodiscard]] std::optional<std::string> info(std::string_view key) const;

  // The logged strings, in log order.
  [[nodiscard]] const std::vector<ULogString>& strings() const noexcept { return strings_; }

  [[nodiscard]] const ULogCounts& counts() const noexcept { return counts_; }

 private:
  struct Subscription {
    std::string topic;
    std::uint8_t multi_id = 0;
  };
  struct Sample {
    std::uint16_t msg_id = 0;
    std::span<const std::byte> data;
  };
  struct RawField {
    std::string type;
    std::size_t count = 1;
    std::string name;
  };
  struct Reader;
  friend struct Reader;

  std::map<std::string, std::vector<RawField>, std::less<>> raw_formats_;
  std::map<std::string, ULogFormat, std::less<>> formats_;
  std::map<std::uint16_t, Subscription> subscriptions_;
  std::vector<Sample> samples_;
  std::map<std::string, double, std::less<>> parameters_;
  std::map<std::string, std::string, std::less<>> info_;
  std::vector<ULogString> strings_;
  ULogCounts counts_;
};

// A field of a format, by name.
[[nodiscard]] std::optional<ULogField> find_field(const ULogFormat& format, std::string_view name);

// One element of a numeric or boolean field in a sample, as a double. Nothing
// for a nested or char field, an element past the field's count, or a
// sample too short to hold it: ULog does not log a format's trailing padding.
[[nodiscard]] std::optional<double> read(const ULogField& field, std::span<const std::byte> sample,
                                         std::size_t index = 0) noexcept;

// One element of an integer field, exactly. Nothing as for read(), for a
// field that is not an integer, or for a uint64 value past int64's range.
[[nodiscard]] std::optional<std::int64_t> read_integer(const ULogField& field, std::span<const std::byte> sample,
                                                       std::size_t index = 0) noexcept;

}  // namespace ics::flightlog
