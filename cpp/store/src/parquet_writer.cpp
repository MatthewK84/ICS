#include "ics/store/parquet_writer.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>

#include "ics/common/check.hpp"
#include "ics/store/thrift.hpp"

namespace ics::store {

namespace {

using google::protobuf::FieldDescriptor;
using google::protobuf::Message;
using google::protobuf::Reflection;

constexpr std::string_view kMagic = "PAR1";
constexpr std::int32_t kFormatVersion = 1;
constexpr std::int32_t kDataPage = 0;
constexpr std::int32_t kPlain = 0;
constexpr std::int32_t kRle = 3;
constexpr std::int32_t kUncompressed = 0;
constexpr std::int32_t kRequired = 0;
constexpr std::int32_t kOptional = 1;
constexpr std::int32_t kUtf8 = 0;
constexpr unsigned kBitsPerByte = 8;
constexpr std::size_t kWordBytes = 4;
constexpr std::size_t kLongBytes = 8;

void put_little_endian(const std::uint64_t value, const std::size_t bytes, std::vector<std::byte>& out) {
  for (std::size_t index = 0; index < bytes; ++index) {
    out.push_back(static_cast<std::byte>(value >> (kBitsPerByte * index)));
  }
}

void put_text(const std::string_view text, std::vector<std::byte>& out) {
  std::ranges::transform(text, std::back_inserter(out), [](const char c) { return static_cast<std::byte>(c); });
}

// A PLAIN byte array: its length, then its bytes.
void put_byte_array(const std::string_view text, std::vector<std::byte>& out) {
  put_little_endian(text.size(), kWordBytes, out);
  put_text(text, out);
}

// The message that holds column's scalar in row: nullopt when a message field
// on the way is unset.
[[nodiscard]] std::optional<std::reference_wrapper<const Message>> holder_of(const Message& row,
                                                                            const Column& column) {
  static_cast<void>(ics::check(!column.path.empty()));
  std::reference_wrapper<const Message> at = row;
  for (std::size_t index = 0; index + 1 < column.path.size(); ++index) {
    const Reflection& reflection = *at.get().GetReflection();
    if (!reflection.HasField(at.get(), column.path[index])) {
      return std::nullopt;
    }
    at = reflection.GetMessage(at.get(), column.path[index]);
  }
  return at;
}

[[nodiscard]] bool holds_value(const Message& holder, const FieldDescriptor& field) {
  return !field.has_presence() || holder.GetReflection()->HasField(holder, &field);
}

void put_string(const Message& holder, const FieldDescriptor& field, std::vector<std::byte>& out) {
  std::string scratch;
  put_byte_array(holder.GetReflection()->GetStringReference(holder, &field, &scratch), out);
}

// An enum value's name, or its number in decimal when the enum names none.
void put_enum(const Message& holder, const FieldDescriptor& field, std::vector<std::byte>& out) {
  static_cast<void>(ics::check(field.cpp_type() == FieldDescriptor::CPPTYPE_ENUM));
  const int number = holder.GetReflection()->GetEnumValue(holder, &field);
  const google::protobuf::EnumValueDescriptor* named = field.enum_type()->FindValueByNumber(number);
  put_byte_array(named != nullptr ? std::string(named->name()) : std::to_string(number), out);
}

// Appends the PLAIN encoding of the field's value; columns_of allows no other
// type than these.
void put_value(const Message& holder, const FieldDescriptor& field, std::vector<std::byte>& out) {
  const Reflection& reflection = *holder.GetReflection();
  switch (field.cpp_type()) {
    case FieldDescriptor::CPPTYPE_INT64:
      put_little_endian(static_cast<std::uint64_t>(reflection.GetInt64(holder, &field)), kLongBytes, out);
      return;
    case FieldDescriptor::CPPTYPE_UINT32:
      put_little_endian(reflection.GetUInt32(holder, &field), kLongBytes, out);
      return;
    case FieldDescriptor::CPPTYPE_DOUBLE:
      put_little_endian(std::bit_cast<std::uint64_t>(reflection.GetDouble(holder, &field)), kLongBytes, out);
      return;
    case FieldDescriptor::CPPTYPE_STRING:
      put_string(holder, field, out);
      return;
    default:
      put_enum(holder, field, out);
      return;
  }
}

// The definition levels of a nullable column, as one bit-packed run of the
// RLE/bit-packed hybrid encoding with bit width 1, after its 4-byte length.
void put_levels(const std::vector<std::byte>& bits, std::vector<std::byte>& out) {
  std::vector<std::byte> run;
  append_varint((std::uint64_t{bits.size()} << 1U) | 1U, run);
  run.insert(run.end(), bits.begin(), bits.end());
  put_little_endian(run.size(), kWordBytes, out);
  out.insert(out.end(), run.begin(), run.end());
}

void put_page_header(const std::size_t values, const std::size_t bytes, std::vector<std::byte>& out) {
  static_cast<void>(ics::check(bytes <= std::numeric_limits<std::int32_t>::max()));
  ThriftWriter thrift(out);
  thrift.begin_struct();
  thrift.field_i32(1, kDataPage);
  thrift.field_i32(2, static_cast<std::int32_t>(bytes));
  thrift.field_i32(3, static_cast<std::int32_t>(bytes));
  thrift.begin_struct_field(5);
  thrift.field_i32(1, static_cast<std::int32_t>(values));
  thrift.field_i32(2, kPlain);
  thrift.field_i32(3, kRle);
  thrift.field_i32(4, kRle);
  thrift.end_struct();
  thrift.end_struct();
}

// Appends one column chunk: a data page holding column's value in each row.
void put_chunk(const Column& column, const std::size_t count, const ParquetWriter::RowAt& row_at,
               std::vector<std::byte>& out) {
  std::vector<std::byte> bits((count + kBitsPerByte - 1) / kBitsPerByte);
  static_cast<void>(ics::check(bits.size() * kBitsPerByte >= count));
  std::vector<std::byte> values;
  for (std::size_t index = 0; index < count; ++index) {
    const std::optional<std::reference_wrapper<const Message>> holder = holder_of(row_at(index), column);
    const bool present = holder.has_value() && holds_value(holder->get(), *column.path.back());
    bits[index / kBitsPerByte] |= std::byte{static_cast<std::uint8_t>(present)} << (index % kBitsPerByte);
    if (present) {
      put_value(holder->get(), *column.path.back(), values);
    }
  }
  std::vector<std::byte> page;
  if (column.optional) {
    put_levels(bits, page);
  }
  page.insert(page.end(), values.begin(), values.end());
  put_page_header(count, page.size(), out);
  out.insert(out.end(), page.begin(), page.end());
}

void put_schema(ThriftWriter& thrift, const std::vector<Column>& columns) {
  static_cast<void>(ics::check(columns.size() <= kMaxSchemaFields));
  thrift.begin_list_field(2, ThriftType::kStruct, columns.size() + 1);
  thrift.begin_struct();
  thrift.field_binary(4, "schema");
  thrift.field_i32(5, static_cast<std::int32_t>(columns.size()));
  thrift.end_struct();
  for (const Column& column : columns) {
    thrift.begin_struct();
    thrift.field_i32(1, static_cast<std::int32_t>(column.type));
    thrift.field_i32(3, column.optional ? kOptional : kRequired);
    thrift.field_binary(4, column.name);
    if (column.type == PhysicalType::kByteArray) {
      thrift.field_i32(6, kUtf8);
      thrift.begin_struct_field(10);  // LogicalType
      thrift.begin_struct_field(1);   // STRING
      thrift.end_struct();
      thrift.end_struct();
    }
    thrift.end_struct();
  }
}

void put_chunk_meta(ThriftWriter& thrift, const Column& column, const ChunkPlace& chunk, const std::uint64_t rows) {
  // Every chunk holds at least its page header.
  static_cast<void>(ics::check(chunk.bytes > 0));
  thrift.begin_struct();
  thrift.field_i64(2, static_cast<std::int64_t>(chunk.offset + chunk.bytes));
  thrift.begin_struct_field(3);
  thrift.field_i32(1, static_cast<std::int32_t>(column.type));
  thrift.begin_list_field(2, ThriftType::kI32, 2);
  thrift.i32(kPlain);
  thrift.i32(kRle);
  thrift.begin_list_field(3, ThriftType::kBinary, 1);
  thrift.binary(column.name);
  thrift.field_i32(4, kUncompressed);
  thrift.field_i64(5, static_cast<std::int64_t>(rows));
  thrift.field_i64(6, static_cast<std::int64_t>(chunk.bytes));
  thrift.field_i64(7, static_cast<std::int64_t>(chunk.bytes));
  thrift.field_i64(9, static_cast<std::int64_t>(chunk.offset));
  thrift.end_struct();
  thrift.end_struct();
}

void put_row_group(ThriftWriter& thrift, const std::vector<Column>& columns, const RowGroupPlace& group) {
  static_cast<void>(ics::check(group.chunks.size() == columns.size()));
  thrift.begin_struct();
  thrift.begin_list_field(1, ThriftType::kStruct, columns.size());
  std::uint64_t bytes = 0;
  for (std::size_t index = 0; index < std::min(columns.size(), group.chunks.size()); ++index) {
    put_chunk_meta(thrift, columns[index], group.chunks[index], group.rows);
    bytes += group.chunks[index].bytes;
  }
  thrift.field_i64(2, static_cast<std::int64_t>(bytes));
  thrift.field_i64(3, static_cast<std::int64_t>(group.rows));
  thrift.end_struct();
}

// The file's footer: its FileMetaData, the metadata's length and the magic.
void put_footer(const std::vector<Column>& columns, const std::vector<RowGroupPlace>& row_groups,
                const std::uint64_t rows, std::vector<std::byte>& out) {
  const std::size_t start = out.size();
  ThriftWriter thrift(out);
  thrift.begin_struct();
  thrift.field_i32(1, kFormatVersion);
  put_schema(thrift, columns);
  thrift.field_i64(3, static_cast<std::int64_t>(rows));
  thrift.begin_list_field(4, ThriftType::kStruct, row_groups.size());
  for (const RowGroupPlace& group : row_groups) {
    put_row_group(thrift, columns, group);
  }
  thrift.field_binary(6, ParquetWriter::kCreatedBy);
  thrift.end_struct();
  static_cast<void>(ics::check(thrift.depth() == 0));
  put_little_endian(out.size() - start, kWordBytes, out);
  put_text(kMagic, out);
}

}  // namespace

ParquetWriter::ParquetWriter(capture::OutputFile file, const google::protobuf::Descriptor& message,
                             std::vector<Column> columns)
    : file_(std::move(file)), descriptor_(&message), columns_(std::move(columns)) {}

Result<ParquetWriter> ParquetWriter::create(const std::filesystem::path& path,
                                            const google::protobuf::Descriptor& message) {
  Result<std::vector<Column>> schema = columns_of(message);
  if (!schema) {
    return fail(schema.error());
  }
  Result<capture::OutputFile> file = capture::OutputFile::create(path);
  if (!file) {
    return fail(file.error());
  }
  ParquetWriter writer(std::move(*file), message, std::move(*schema));
  put_text(kMagic, writer.buffer_);
  return writer.write_buffer().map([&writer] { return std::move(writer); });
}

Status ParquetWriter::write_buffer() {
  return file_->write(buffer_).map([this] {
    offset_ += buffer_.size();
    buffer_.clear();
  });
}

Status ParquetWriter::write_rows(const std::size_t count, const RowAt& row_at) {
  if (!file_) {
    return fail(Error::kInvalidArgument);
  }
  if (count == 0) {
    return {};
  }
  static_cast<void>(ics::check(count <= std::numeric_limits<std::int32_t>::max()));
  RowGroupPlace group{.rows = count, .chunks = {}};
  for (const Column& column : columns_) {
    const std::size_t start = buffer_.size();
    put_chunk(column, count, row_at, buffer_);
    group.chunks.push_back({.offset = offset_ + start, .bytes = buffer_.size() - start});
  }
  return write_buffer().map([this, &group, count] {
    row_groups_.push_back(std::move(group));
    rows_ += count;
  });
}

Status ParquetWriter::close() {
  if (!file_) {
    return fail(Error::kInvalidArgument);
  }
  put_footer(columns_, row_groups_, rows_, buffer_);
  const Status written = write_buffer().and_then([this] { return file_->sync(); });
  file_.reset();
  return written;
}

}  // namespace ics::store
