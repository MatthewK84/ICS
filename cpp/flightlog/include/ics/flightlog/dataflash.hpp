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

// ArduPilot's DataFlash binary log (ICS-025), as its AP_Logger writes it:
// messages of two header bytes (0xA3 0x95), a type number and a body whose
// length the type's FMT message gives. FMT messages, type 128, describe each
// type: its name, its format characters and its column names.

struct DataFlashColumn {
  std::string name;
  // The format character, such as 'f' for a float or 'L' for a latitude.
  char type = 0;
  // Where the column starts in a message, header included, and its size.
  std::size_t offset = 0;
  std::size_t size = 0;
};

struct DataFlashFormat {
  std::uint8_t type = 0;
  std::string name;
  // The length of a whole message, header included.
  std::size_t length = 0;
  std::vector<DataFlashColumn> columns;
};

struct DataFlashCounts {
  // Bytes skipped to find the next message: damage, or a block log's padding.
  std::size_t skipped_bytes = 0;
  // FMT messages whose format characters, columns and length disagree.
  std::size_t bad_formats = 0;
  // Whether the log ends partway through a message.
  bool truncated = false;
};

// A parsed DataFlash log. It holds views into the log's bytes, which must
// outlive it.
class DataFlash {
 public:
  static constexpr std::size_t kMaxBytes = std::size_t{1} << 30U;

  // Reads a log. Bytes that do not start a message of a known type are
  // skipped and counted, so a damaged log keeps every message around the
  // damage. Fails with Error::kInvalidArgument for a log larger than
  // kMaxBytes.
  [[nodiscard]] static Result<DataFlash> parse(std::span<const std::byte> log);

  [[nodiscard]] std::optional<std::reference_wrapper<const DataFlashFormat>> format(std::string_view name) const;

  // The messages of a type, whole, in log order.
  [[nodiscard]] std::vector<std::span<const std::byte>> messages(std::string_view name) const;

  [[nodiscard]] const DataFlashCounts& counts() const noexcept { return counts_; }

 private:
  struct Message {
    std::uint8_t type = 0;
    std::span<const std::byte> bytes;
  };

  std::map<std::uint8_t, DataFlashFormat> formats_;
  std::vector<Message> messages_;
  DataFlashCounts counts_;
};

[[nodiscard]] std::optional<DataFlashColumn> find_column(const DataFlashFormat& format, std::string_view name);

// A numeric column of a message, scaled as its format character says: c, C,
// e and E are hundredths, and L is a latitude or longitude in 1e-7 degrees.
// Nothing for a text or array column, or a message too short.
[[nodiscard]] std::optional<double> read(const DataFlashColumn& column, std::span<const std::byte> message) noexcept;

// A text column (n, N or Z), up to its first NUL.
[[nodiscard]] std::optional<std::string> read_text(const DataFlashColumn& column, std::span<const std::byte> message);

}  // namespace ics::flightlog
