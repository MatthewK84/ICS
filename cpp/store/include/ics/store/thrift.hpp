#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace ics::store {

// The Thrift compact protocol type codes Parquet's metadata uses.
enum class ThriftType : std::uint8_t {
  kI32 = 5,
  kI64 = 6,
  kBinary = 8,
  kList = 9,
  kStruct = 12,
};

// Appends value as an unsigned LEB128 varint, as Thrift and Parquet's
// run-length encoding write lengths.
void append_varint(std::uint64_t value, std::vector<std::byte>& out);

// Writes Thrift structs in the compact protocol (ICS-030), the encoding of
// Parquet's page headers and file metadata. Only what Parquet's metadata
// needs: i32, i64 and binary fields, lists, and structs nested at most
// kMaxDepth deep. Fields in increasing id order, as Thrift's generated code
// writes them, take the short header. Appends to the caller's buffer.
class ThriftWriter {
 public:
  static constexpr std::size_t kMaxDepth = 8;

  explicit ThriftWriter(std::vector<std::byte>& out) noexcept : out_(out) {}

  // Starts a struct: the top-level one, or an element of a struct list.
  void begin_struct() noexcept;
  // Starts a field that holds a struct.
  void begin_struct_field(std::int16_t id);
  // Ends the struct begun last.
  void end_struct();

  void field_i32(std::int16_t id, std::int32_t value);
  void field_i64(std::int16_t id, std::int64_t value);
  void field_binary(std::int16_t id, std::string_view value);

  // Starts a list field of size elements of type element; write each element
  // next with i32, binary or begin_struct.
  void begin_list_field(std::int16_t id, ThriftType element, std::size_t size);
  void i32(std::int32_t value);
  void binary(std::string_view value);

  // How many structs are open.
  [[nodiscard]] std::size_t depth() const noexcept { return depth_; }

 private:
  void field_header(std::int16_t id, ThriftType type);
  void byte(std::uint8_t value);

  std::vector<std::byte>& out_;
  // The last field id written in each open struct.
  std::array<std::int16_t, kMaxDepth> last_ids_{};
  std::size_t depth_ = 0;
};

}  // namespace ics::store
