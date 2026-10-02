// The ICS-017 "Done when": every golden vector in golden/frames, generated
// with GeographicLib 2.3, is matched within 1 mm. The port should also agree
// to within the vectors' printed precision, which is checked separately.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "ics/common/units.hpp"
#include "ics/frames/egm96.hpp"
#include "ics/frames/enu.hpp"
#include "ics/frames/geodetic.hpp"

namespace {

using ics::Degrees;
using ics::Meters;
using ics::frames::Ecef;
using ics::frames::Egm96;
using ics::frames::Enu;
using ics::frames::EnuFrame;
using ics::frames::Geodetic;

constexpr double kOneMillimetre = 1e-3;
// Printed to 1 nm; a micrometre leaves room for the last bits of arithmetic.
constexpr double kCartesianAgreement = 1e-6;
// Printed to 0.1 mm, so rounding alone is up to 0.05 mm.
constexpr double kGeoidAgreement = 0.06e-3;

// A golden CSV file: its header, and its rows as text.
struct Table {
  std::vector<std::string> columns;
  std::vector<std::vector<std::string>> rows;

  [[nodiscard]] double number(const std::vector<std::string>& row, const std::string& column) const {
    const auto found = std::find(columns.begin(), columns.end(), column);
    EXPECT_NE(found, columns.end()) << column;
    return std::stod(row.at(static_cast<std::size_t>(found - columns.begin())));
  }
};

std::vector<std::string> split(const std::string& line) {
  std::vector<std::string> fields;
  std::stringstream stream(line);
  std::string field;
  while (std::getline(stream, field, ',')) {
    fields.push_back(field);
  }
  return fields;
}

Table read_table(const std::string& name) {
  std::ifstream file(std::string(ICS_GOLDEN_FRAMES_DIR) + "/" + name);
  EXPECT_TRUE(file.is_open()) << name;
  Table table;
  std::string line;
  std::getline(file, line);
  table.columns = split(line);
  while (std::getline(file, line)) {
    table.rows.push_back(split(line));
  }
  EXPECT_FALSE(table.rows.empty()) << name;
  return table;
}

Geodetic point(const Table& table, const std::vector<std::string>& row, const std::string& prefix) {
  return Geodetic::make(Degrees(table.number(row, prefix + "latitude_deg")),
                        Degrees(table.number(row, prefix + "longitude_deg")),
                        Meters(table.number(row, prefix + "height_ellipsoid_m")))
      .value();
}

// The largest difference seen, and that each is within 1 mm.
class Agreement {
 public:
  void add(const std::string& id, const double computed, const double expected) {
    const double difference = std::fabs(computed - expected);
    EXPECT_LE(difference, kOneMillimetre) << id << ": " << computed << " against " << expected;
    largest_ = std::max(largest_, difference);
  }
  [[nodiscard]] double largest() const { return largest_; }

 private:
  double largest_ = 0.0;
};

TEST(GoldenFrames, GeodeticToEcef) {
  const Table table = read_table("geodetic-ecef.csv");
  Agreement agreement;
  for (const std::vector<std::string>& row : table.rows) {
    const Ecef ecef = ics::frames::to_ecef(point(table, row, ""));
    agreement.add(row[0], ecef.x.value(), table.number(row, "x_m"));
    agreement.add(row[0], ecef.y.value(), table.number(row, "y_m"));
    agreement.add(row[0], ecef.z.value(), table.number(row, "z_m"));
  }
  EXPECT_LE(agreement.largest(), kCartesianAgreement);
}

TEST(GoldenFrames, EcefToGeodetic) {
  const Table table = read_table("geodetic-ecef.csv");
  for (const std::vector<std::string>& row : table.rows) {
    const Ecef ecef{Meters(table.number(row, "x_m")), Meters(table.number(row, "y_m")),
                    Meters(table.number(row, "z_m"))};
    const Geodetic expected = point(table, row, "");
    const ics::Result<Geodetic> computed = ics::frames::to_geodetic(ecef);
    ASSERT_TRUE(computed.has_value()) << row[0];
    EXPECT_NEAR(computed->latitude().value(), expected.latitude().value(), 1e-9) << row[0];
    EXPECT_NEAR(computed->height().value(), expected.height().value(), kCartesianAgreement) << row[0];
    if (std::fabs(expected.latitude().value()) < 90.0) {
      EXPECT_NEAR(computed->longitude().value(), expected.longitude().value(), 1e-9) << row[0];
    }
  }
}

TEST(GoldenFrames, GeodeticToEnu) {
  const Table table = read_table("geodetic-enu.csv");
  Agreement agreement;
  for (const std::vector<std::string>& row : table.rows) {
    const EnuFrame frame(point(table, row, "origin_"));
    const Enu enu = frame.to_enu(point(table, row, ""));
    agreement.add(row[0], enu.east.value(), table.number(row, "east_m"));
    agreement.add(row[0], enu.north.value(), table.number(row, "north_m"));
    agreement.add(row[0], enu.up.value(), table.number(row, "up_m"));
  }
  EXPECT_LE(agreement.largest(), kCartesianAgreement);
}

TEST(GoldenFrames, Egm96Heights) {
  const ics::Result<Egm96> grid = Egm96::load(ICS_EGM96_PATH);
  ASSERT_TRUE(grid.has_value()) << ICS_EGM96_PATH;
  const Table table = read_table("egm96-5.csv");
  Agreement agreement;
  for (const std::vector<std::string>& row : table.rows) {
    const ics::Result<Geodetic> above_msl = grid->from_msl(Degrees(table.number(row, "latitude_deg")),
                                                           Degrees(table.number(row, "longitude_deg")),
                                                           Meters(table.number(row, "msl_height_m")));
    ASSERT_TRUE(above_msl.has_value()) << row[0];
    agreement.add(row[0], grid->geoid_height(*above_msl).value(), table.number(row, "geoid_height_m"));
    agreement.add(row[0], above_msl->height().value(), table.number(row, "height_ellipsoid_m"));
  }
  EXPECT_LE(agreement.largest(), kGeoidAgreement);
}

}  // namespace
