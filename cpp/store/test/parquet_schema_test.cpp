#include "ics/store/parquet_schema.hpp"

#include <string>
#include <vector>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>
#include <gtest/gtest.h>

#include "ics/v1/pli.pb.h"
#include "ics/v1/pli_query.pb.h"

namespace {

using google::protobuf::FieldDescriptorProto;
using ics::store::Column;
using ics::store::columns_of;
using ics::store::PhysicalType;

// A column's name, type and nullability, as text: "name type optional".
std::vector<std::string> describe(const std::vector<Column>& columns) {
  std::vector<std::string> out;
  for (const Column& column : columns) {
    out.push_back(column.name + " " + std::to_string(static_cast<int>(column.type)) + " " +
                  (column.optional ? "optional" : "required"));
  }
  return out;
}

TEST(ColumnsOf, FlattensAPliRecord) {
  const std::vector<std::string> expected{
      "entity_id 6 required",
      "role 6 required",
      "source 6 required",
      "valid_utc_ns 2 required",
      "received_utc_ns 2 optional",
      "time_basis 6 required",
      "position.latitude_deg 5 optional",
      "position.longitude_deg 5 optional",
      "position.height_ellipsoid_m 5 optional",
      "velocity_enu_mps.east 5 optional",
      "velocity_enu_mps.north 5 optional",
      "velocity_enu_mps.up 5 optional",
      "horizontal_sigma_m 5 optional",
      "vertical_sigma_m 5 optional",
      "fix_type 6 required",
      "attitude.w 5 optional",
      "attitude.x 5 optional",
      "attitude.y 5 optional",
      "attitude.z 5 optional",
  };
  const std::vector<Column> columns = columns_of(*ics::v1::PliRecord::descriptor()).value();
  EXPECT_EQ(describe(columns), expected);
  ASSERT_EQ(columns[6].path.size(), 2U);
  EXPECT_EQ(columns[6].path[0]->name(), "position");
  EXPECT_EQ(columns[6].path[1]->name(), "latitude_deg");
}

TEST(ColumnsOf, FlattensAPliEvent) {
  const std::vector<std::string> expected{
      "entity_id 6 required", "source 6 required",  "time_utc_ns 2 required",    "time_basis 6 required",
      "kind 6 required",      "command 2 required", "command_result 2 required", "detail 6 required",
  };
  EXPECT_EQ(describe(columns_of(*ics::v1::PliEvent::descriptor()).value()), expected);
}

// Builds message M in package t from fields, each a name, type and label.
class Schema {
 public:
  Schema() {
    file_.set_name("t.proto");
    file_.set_package("t");
    file_.set_syntax("proto3");
    file_.add_message_type()->set_name("M");
  }

  void add(const std::string& name, const FieldDescriptorProto::Type type,
           const FieldDescriptorProto::Label label = FieldDescriptorProto::LABEL_OPTIONAL) {
    FieldDescriptorProto& field = *file_.mutable_message_type(0)->add_field();
    field.set_name(name);
    field.set_number(file_.message_type(0).field_size());
    field.set_type(type);
    field.set_label(label);
    if (type == FieldDescriptorProto::TYPE_MESSAGE) {
      field.set_type_name(".t.M");
    }
  }

  const google::protobuf::Descriptor& build() { return *pool_.BuildFile(file_)->message_type(0); }

 private:
  google::protobuf::FileDescriptorProto file_;
  google::protobuf::DescriptorPool pool_;
};

TEST(ColumnsOf, RejectsRepeatedFields) {
  EXPECT_EQ(columns_of(*ics::v1::QueryPliResponse::descriptor()).error(), ics::Error::kInvalidArgument);
  Schema schema;
  schema.add("values", FieldDescriptorProto::TYPE_INT64, FieldDescriptorProto::LABEL_REPEATED);
  EXPECT_EQ(columns_of(schema.build()).error(), ics::Error::kInvalidArgument);
}

TEST(ColumnsOf, RejectsTypesItHasNoColumnFor) {
  for (const FieldDescriptorProto::Type type :
       {FieldDescriptorProto::TYPE_INT32, FieldDescriptorProto::TYPE_BYTES, FieldDescriptorProto::TYPE_BOOL,
        FieldDescriptorProto::TYPE_FLOAT, FieldDescriptorProto::TYPE_UINT64}) {
    Schema schema;
    schema.add("value", type);
    EXPECT_EQ(columns_of(schema.build()).error(), ics::Error::kInvalidArgument) << type;
  }
}

TEST(ColumnsOf, RejectsAMessageThatHoldsItself) {
  Schema schema;
  schema.add("time_ns", FieldDescriptorProto::TYPE_INT64);
  schema.add("child", FieldDescriptorProto::TYPE_MESSAGE);
  EXPECT_EQ(columns_of(schema.build()).error(), ics::Error::kInvalidArgument);
}

TEST(ColumnsOf, TakesAnUnsignedThirtyTwoBitFieldAsInt64) {
  Schema schema;
  schema.add("count", FieldDescriptorProto::TYPE_UINT32);
  const std::vector<Column> columns = columns_of(schema.build()).value();
  ASSERT_EQ(columns.size(), 1U);
  EXPECT_EQ(columns[0].type, PhysicalType::kInt64);
  EXPECT_FALSE(columns[0].optional);
}

}  // namespace
