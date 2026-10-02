// Ported from GeographicLib 2.3's Geoid class (src/Geoid.cpp): the PGM header
// it accepts, the reflection of the grid across the poles, and the cubic
// interpolation. Copyright (c) 2008-2023, Charles Karney; MIT License, see
// cpp/frames/GEOGRAPHICLIB-LICENSE.txt.
#include "ics/frames/egm96.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "egm96_stencil.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/frames/geodetic.hpp"

namespace ics::frames {
namespace {

constexpr std::string_view kMagic = "P5";
constexpr std::string_view kWhitespace = " \t\r\n\v\f";
constexpr unsigned long kMaxSample = 65535;
constexpr std::size_t kBytesPerSample = 2;
constexpr unsigned kBitsPerByte = 8;
constexpr double kFullTurn = 360.0;
constexpr double kHalfTurn = 180.0;

// What the header of a grid file says.
struct Header {
  std::optional<double> offset;
  std::optional<double> scale;
  int width = 0;
  int height = 0;
  std::size_t data_start = 0;
};

// The line starting at offset, without its line feed, and where the next one
// starts.
std::pair<std::string_view, std::size_t> line_at(const std::string_view text, const std::size_t offset) {
  const std::size_t end = std::min(text.find('\n', offset), text.size());
  return {text.substr(offset, end - offset), std::min(end + 1, text.size())};
}

// The next whitespace-separated token at or after offset, and where it ends.
std::pair<std::string_view, std::size_t> token_at(const std::string_view text, const std::size_t offset) {
  const std::size_t start = std::min(text.find_first_not_of(kWhitespace, offset), text.size());
  const std::size_t end = std::min(text.find_first_of(kWhitespace, start), text.size());
  return {text.substr(start, end - start), end};
}

// A number at the start of text; like an istream, trailing characters are
// ignored.
template <typename T>
std::optional<T> number_at(const std::string_view text) {
  T value{};
  const std::from_chars_result read = std::from_chars(text.begin(), text.end(), value);
  if (read.ec != std::errc{}) {
    return std::nullopt;
  }
  return value;
}

// Reads "# Offset <number>" and "# Scale <number>"; other comments are notes.
void read_comment(const std::string_view line, Header& header) {
  const auto [mark, after_mark] = token_at(line, 0);
  if (mark != "#") {
    return;
  }
  const auto [key, after_key] = token_at(line, after_mark);
  const std::string_view value = token_at(line, after_key).first;
  if (key == "Offset") {
    header.offset = number_at<double>(value);
  } else if (key == "Scale") {
    header.scale = number_at<double>(value);
  }
}

// Reads the "<width> <height>" line, then maxval, which must be 65535 and be
// followed by one whitespace byte before the samples.
Status read_size(const std::string_view text, const std::string_view line, const std::size_t next, Header& header) {
  const auto [width, after_width] = token_at(line, 0);
  const std::optional<int> parsed_width = number_at<int>(width);
  const std::optional<int> parsed_height = number_at<int>(token_at(line, after_width).first);
  const auto [maxval, after_maxval] = token_at(text, next);
  if (!parsed_width || !parsed_height || number_at<unsigned long>(maxval) != kMaxSample) {
    return fail(Error::kMalformed);
  }
  header.width = *parsed_width;
  header.height = *parsed_height;
  header.data_start = after_maxval + 1;
  return {};
}

Result<Header> read_header(const std::string_view text) {
  auto [magic, offset] = line_at(text, 0);
  if (magic != kMagic) {
    return fail(Error::kMalformed);
  }
  Header header;
  while (offset < text.size()) {
    const auto [line, next] = line_at(text, offset);
    offset = next;
    if (line.empty() || line.front() == '#') {
      read_comment(line, header);
      continue;
    }
    const Status size = read_size(text, line, next, header);
    if (!size) {
      return tl::unexpected(size.error());
    }
    return header;
  }
  return fail(Error::kMalformed);
}

// The rules GeographicLib's Geoid enforces: an offset and a positive scale;
// an even width of at least 2, so a row can be reflected across a pole; an
// odd height of at least 3, so a row lies on the equator; and exactly one
// 16-bit sample per grid point.
bool valid(const Header& header, const std::size_t file_size) {
  const bool numbers = header.offset.has_value() && std::isfinite(*header.offset) && header.scale.has_value() &&
                       std::isfinite(*header.scale) && *header.scale > 0.0;
  const bool shape = header.width >= 2 && header.width % 2 == 0 && header.height >= 3 && header.height % 2 != 0;
  return numbers && shape && header.data_start <= file_size &&
         file_size - header.data_start == static_cast<std::size_t>(header.width) *
                                               static_cast<std::size_t>(header.height) * kBytesPerSample;
}

// The big-endian 16-bit samples after the header.
std::vector<std::uint16_t> read_samples(const std::string_view data) {
  std::vector<std::uint16_t> samples(data.size() / kBytesPerSample);
  for (std::size_t i = 0; i < samples.size(); ++i) {
    const auto high = static_cast<unsigned char>(data[kBytesPerSample * i]);
    const auto low = static_cast<unsigned char>(data[kBytesPerSample * i + 1]);
    samples[i] = static_cast<std::uint16_t>((static_cast<unsigned>(high) << kBitsPerByte) | low);
  }
  return samples;
}

// The cubic in the cell's fractions fx (east) and fy (south), from its 10
// coefficients.
double evaluate(const std::array<double, detail::kCubicTerms>& t, const double fx, const double fy) {
  return t[0] + fx * (t[1] + fx * (t[3] + fx * t[6])) + fy * (t[2] + fx * (t[4] + fx * t[7]) +
                                                              fy * (t[5] + fx * t[8] + fy * t[9]));
}

}  // namespace

Egm96::Egm96(std::vector<std::uint16_t> samples, const int width, const int height, const double offset,
             const double scale) noexcept
    : samples_(std::move(samples)), width_(width), height_(height), offset_(offset), scale_(scale) {}

Result<Egm96> Egm96::load(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  // is_regular_file is false for a folder, and on any error.
  std::error_code error;
  if (!file.is_open() || !std::filesystem::is_regular_file(path, error)) {
    return fail(Error::kUnreadable);
  }
  const std::string contents{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  return parse(contents);
}

Result<Egm96> Egm96::parse(const std::string_view file) {
  const Result<Header> header = read_header(file);
  if (!header || !valid(*header, file.size())) {
    return fail(Error::kMalformed);
  }
  return Egm96(read_samples(file.substr(header->data_start)), header->width, header->height, *header->offset,
               *header->scale);
}

double Egm96::sample(const int x, const int y) const noexcept {
  // The caller's columns are at most one grid width away from the grid.
  int column = x < 0 ? x + width_ : (x >= width_ ? x - width_ : x);
  int row = y;
  if (y < 0 || y >= height_) {
    // Beyond a pole: the same distance back from it, half a turn around.
    row = y < 0 ? -y : 2 * (height_ - 1) - y;
    column += column < width_ / 2 ? width_ / 2 : -(width_ / 2);
  }
  const std::size_t index = static_cast<std::size_t>(row) * static_cast<std::size_t>(width_) +
                            static_cast<std::size_t>(column);
  return static_cast<double>(samples_[index]);
}

Meters Egm96::geoid_height(const Geodetic& point) const noexcept {
  // Columns from longitude 0 eastward; rows from the north pole southward.
  double fx = point.longitude().value() * (width_ / kFullTurn);
  double fy = -point.latitude().value() * ((height_ - 1) / kHalfTurn);
  const int x0 = static_cast<int>(std::floor(fx));
  // At the south pole, use the last cell rather than one beyond the grid.
  const int y0 = std::min((height_ - 1) / 2 - 1, static_cast<int>(std::floor(fy)));
  fx -= x0;
  fy -= y0;
  // Longitudes are in [-180, 180), so only a western column needs wrapping.
  const int x = x0 < 0 ? x0 + width_ : x0;
  const int y = y0 + (height_ - 1) / 2;
  const std::array<double, detail::kStencilSize> v{
      sample(x, y - 1),     sample(x + 1, y - 1),                                             //
      sample(x - 1, y),     sample(x, y),         sample(x + 1, y),     sample(x + 2, y),     //
      sample(x - 1, y + 1), sample(x, y + 1),     sample(x + 1, y + 1), sample(x + 2, y + 1), //
      sample(x, y + 2),     sample(x + 1, y + 2),
  };
  const detail::StencilTable& table =
      y == 0 ? detail::kNorthStencil : (y == height_ - 2 ? detail::kSouthStencil : detail::kInteriorStencil);
  std::array<double, detail::kCubicTerms> t{};
  for (std::size_t i = 0; i < detail::kCubicTerms; ++i) {
    for (std::size_t j = 0; j < detail::kStencilSize; ++j) {
      t[i] += v[j] * table.weights[detail::kCubicTerms * j + i];
    }
    t[i] /= table.denominator;
  }
  return Meters(offset_ + scale_ * evaluate(t, fx, fy));
}

Result<Geodetic> Egm96::from_msl(const Degrees latitude, const Degrees longitude, const Meters msl_height) const noexcept {
  const Result<Geodetic> point = Geodetic::make(latitude, longitude, msl_height);
  if (!point) {
    return point;
  }
  return Geodetic::make(latitude, longitude, msl_height + geoid_height(*point));
}

Meters Egm96::msl_height(const Geodetic& point) const noexcept { return point.height() - geoid_height(point); }

}  // namespace ics::frames
