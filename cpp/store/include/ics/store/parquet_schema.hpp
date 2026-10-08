#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <google/protobuf/descriptor.h>

#include "ics/common/error.hpp"

namespace ics::store {

// The Parquet physical types the PLI columns use, by their Parquet numbers.
enum class PhysicalType : std::int32_t {
  kInt64 = 2,
  kDouble = 5,
  kByteArray = 6,  // Always UTF-8 text here.
};

// One Parquet column: a scalar field of the message, or of a message inside
// it, flattened (ICS-030). Its name joins the field names with dots, such as
// position.latitude_deg.
//
//   int64            INT64
//   uint32           INT64, widened
//   double           DOUBLE
//   string           BYTE_ARRAY, UTF-8
//   enum             BYTE_ARRAY, UTF-8: the value's name, or its number in
//                    decimal for a number the enum does not name
//
// A column is optional (nullable) when the field has presence, such as a
// proto3 optional, or lies inside a message field, which may be unset.
struct Column {
  std::string name;
  PhysicalType type = PhysicalType::kInt64;
  bool optional = false;
  // The fields from the message down to the scalar. Descriptors live as long
  // as the program.
  std::vector<const google::protobuf::FieldDescriptor*> path;
};

// The most fields columns_of visits, message fields included.
inline constexpr std::size_t kMaxSchemaFields = 256;

// The columns of message, depth first in field order. kInvalidArgument for a
// repeated or map field, a scalar type not listed above, or more than
// kMaxSchemaFields fields, such as a message that holds itself.
[[nodiscard]] Result<std::vector<Column>> columns_of(const google::protobuf::Descriptor& message);

}  // namespace ics::store
