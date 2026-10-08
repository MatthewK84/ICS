#include "ics/store/parquet_schema.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <google/protobuf/descriptor.h>

#include "ics/common/check.hpp"

namespace ics::store {

namespace {

using google::protobuf::Descriptor;
using google::protobuf::FieldDescriptor;

// A field still to visit, with the fields above it.
struct Pending {
  std::vector<const FieldDescriptor*> path;
  bool inside_message = false;
};

[[nodiscard]] std::optional<PhysicalType> physical_type(const FieldDescriptor& field) noexcept {
  switch (field.cpp_type()) {
    case FieldDescriptor::CPPTYPE_INT64:
    case FieldDescriptor::CPPTYPE_UINT32:
      return PhysicalType::kInt64;
    case FieldDescriptor::CPPTYPE_DOUBLE:
      return PhysicalType::kDouble;
    case FieldDescriptor::CPPTYPE_STRING:
      return field.type() == FieldDescriptor::TYPE_STRING ? std::optional(PhysicalType::kByteArray) : std::nullopt;
    case FieldDescriptor::CPPTYPE_ENUM:
      return PhysicalType::kByteArray;
    default:
      return std::nullopt;
  }
}

// Queues message's fields, so the first is visited next.
void push_fields(const Descriptor& message, const Pending& above, std::vector<Pending>& pending) {
  for (int index = message.field_count() - 1; index >= 0; --index) {
    Pending next{.path = above.path, .inside_message = !above.path.empty()};
    next.path.push_back(message.field(index));
    pending.push_back(std::move(next));
  }
}

[[nodiscard]] std::string column_name(const std::vector<const FieldDescriptor*>& path) {
  std::string name;
  for (const FieldDescriptor* field : path) {
    name += name.empty() ? "" : ".";
    name += field->name();
  }
  return name;
}

}  // namespace

Result<std::vector<Column>> columns_of(const Descriptor& message) {
  std::vector<Column> columns;
  std::vector<Pending> pending;
  push_fields(message, Pending{}, pending);
  for (std::size_t visited = 0; !pending.empty(); ++visited) {
    const Pending next = std::move(pending.back());
    pending.pop_back();
    // push_fields queues only paths that end in a field.
    static_cast<void>(ics::check(!next.path.empty()));
    const FieldDescriptor& field = *next.path.back();
    if (visited == kMaxSchemaFields || field.is_repeated()) {
      return fail(Error::kInvalidArgument);
    }
    if (field.cpp_type() == FieldDescriptor::CPPTYPE_MESSAGE) {
      push_fields(*field.message_type(), next, pending);
      continue;
    }
    const std::optional<PhysicalType> type = physical_type(field);
    if (!type) {
      return fail(Error::kInvalidArgument);
    }
    columns.push_back({.name = column_name(next.path),
                       .type = *type,
                       .optional = next.inside_message || field.has_presence(),
                       .path = next.path});
  }
  return columns;
}

}  // namespace ics::store
