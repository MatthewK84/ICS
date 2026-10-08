#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>

#include "ics/capture/output_file.hpp"
#include "ics/common/error.hpp"
#include "ics/store/parquet_schema.hpp"

namespace ics::store {

// Where one column chunk lies in the file.
struct ChunkPlace {
  std::uint64_t offset = 0;
  std::uint64_t bytes = 0;
};

// One row group's place in the file.
struct RowGroupPlace {
  std::uint64_t rows = 0;
  std::vector<ChunkPlace> chunks;
};

// Writes protobuf messages of one type to a new Parquet file (ICS-030), one
// row per message, in the columns columns_of gives. Each row group holds one
// uncompressed, PLAIN-encoded data page per column; a nullable column's
// definition levels are bit-packed. The file is written as the Apache Parquet
// format specification describes, with the Thrift compact protocol, and the
// same messages always give the same bytes.
class ParquetWriter {
 public:
  // The row at an index of a row group.
  using RowAt = std::function<const google::protobuf::Message&(std::size_t)>;

  // The creator recorded in each file.
  static constexpr std::string_view kCreatedBy = "ics ics::store (ICS-030)";

  // Creates the file at path for messages of type message. kInvalidArgument
  // when columns_of rejects the type; kUnwritable when the file cannot be
  // created, for example because something is already at path.
  [[nodiscard]] static Result<ParquetWriter> create(const std::filesystem::path& path,
                                                    const google::protobuf::Descriptor& message);

  // Writes rows as one row group; none writes nothing. kInvalidArgument when
  // they are not of the writer's type, or after close(); kUnwritable when the
  // file cannot be written. Each column's page must stay under 2 GiB: keep
  // the rows' total serialized size well below that.
  template <typename Message>
  [[nodiscard]] Status write_row_group(const std::vector<Message>& rows) {
    if (Message::descriptor() != descriptor_) {
      return fail(Error::kInvalidArgument);
    }
    return write_rows(rows.size(), [&rows](const std::size_t index) -> const google::protobuf::Message& {
      return rows[index];
    });
  }

  // Writes the file's footer and waits until the file is on the disk.
  // kInvalidArgument after close(); kUnwritable when the file cannot be
  // written or synced.
  [[nodiscard]] Status close();

  [[nodiscard]] const std::vector<Column>& columns() const noexcept { return columns_; }
  [[nodiscard]] std::uint64_t rows() const noexcept { return rows_; }

 private:
  ParquetWriter(capture::OutputFile file, const google::protobuf::Descriptor& message, std::vector<Column> columns);
  [[nodiscard]] Status write_rows(std::size_t count, const RowAt& row_at);
  [[nodiscard]] Status write_buffer();

  std::optional<capture::OutputFile> file_;
  const google::protobuf::Descriptor* descriptor_;
  std::vector<Column> columns_;
  std::vector<RowGroupPlace> row_groups_;
  std::vector<std::byte> buffer_;
  std::uint64_t offset_ = 0;
  std::uint64_t rows_ = 0;
};

}  // namespace ics::store
