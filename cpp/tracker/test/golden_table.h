// golden_table.h: reads the CSV and PGM files of golden/tracker, which python/ics_golden/tracker.py writes (ICS-038).
#ifndef ICS_TRACKER_TEST_GOLDEN_TABLE_H
#define ICS_TRACKER_TEST_GOLDEN_TABLE_H

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include <ics/common/universal_types.h>

namespace ics::tracker_test
{

// A CSV file's header and rows, each a list of fields.
struct Golden_table
{
  // The column names.
  std::vector<std::string> header;
  // The rows' fields.
  std::vector<std::vector<std::string>> rows;
};

// The fields of one CSV line.
inline std::vector<std::string> fields_of(const std::string& line)
{
  std::vector<std::string> fields;
  std::istringstream stream(line);
  std::string field;
  while (std::getline(stream, field, ','))
  {
    fields.push_back(field);
  }
  return fields;
}

// The table in a file of golden/tracker; empty when it cannot be read.
inline Golden_table read_table(const std::string& path)
{
  Golden_table table;
  std::ifstream file(path);
  std::string line;
  if (std::getline(file, line))
  {
    table.header = fields_of(line);
  }
  while (std::getline(file, line))
  {
    table.rows.push_back(fields_of(line));
  }
  return table;
}

// The column of a name in the header; the header's length when absent.
inline int64 column_of(const Golden_table& table, const std::string& name)
{
  const auto found = std::ranges::find(table.header, name);
  const int64 column = std::distance(table.header.begin(), found);
  return column;
}

// The field of a row in a named column; empty when either is absent.
inline std::string field_of(const Golden_table& table,
                            const std::vector<std::string>& row,
                            const std::string& name)
{
  const int64 column = column_of(table, name);
  std::string field;
  if ((column >= 0) && (column < static_cast<int64>(row.size())))
  {
    field = row[static_cast<std::size_t>(column)];
  }
  return field;
}

// A field as a number; 0 when it is not one.
inline float64 number_of(const std::string& field)
{
  float64 value = 0.0;
  std::istringstream stream(field);
  stream >> value;
  return value;
}

// A field as an integer; 0 when it is not one.
inline int64 integer_of(const std::string& field)
{
  int64 value = 0;
  std::istringstream stream(field);
  stream >> value;
  return value;
}

// A plain (P2) PGM image: its width, height and counts, row by row.
struct Golden_image
{
  int32 width = 0;
  int32 height = 0;
  std::vector<uint16> counts;
};

// The image in a PGM file of golden/tracker; empty when it cannot be read.
inline Golden_image read_image(const std::string& path)
{
  Golden_image image;
  std::ifstream file(path);
  std::string magic;
  int32 maximum = 0;
  file >> magic >> image.width >> image.height >> maximum;
  int32 count = 0;
  while (file >> count)
  {
    image.counts.push_back(static_cast<uint16>(count));
  }
  return image;
}

}  // namespace ics::tracker_test

#endif  // ICS_TRACKER_TEST_GOLDEN_TABLE_H
