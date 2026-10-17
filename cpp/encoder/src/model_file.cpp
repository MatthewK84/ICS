// The pointing model file ICS-035 writes (schemas/pointing-model.schema.json),
// read with protobuf's JSON parser into a google.protobuf.Struct, as the
// Lattice decoder (ICS-023) reads its events, and hashed with OpenSSL.
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <google/protobuf/struct.pb.h>
#include <google/protobuf/util/json_util.h>
#include <openssl/evp.h>

#include "ics/common/check.hpp"
#include "ics/common/error.hpp"
#include "ics/encoder/pointing_model.hpp"
#include "status.hpp"

namespace ics::encoder {
namespace {

using google::protobuf::Struct;
using google::protobuf::Value;

constexpr std::string_view kFormat = "ics-pointing-model";
constexpr double kFormatVersion = 1.0;
constexpr std::size_t kSha256Bytes = 32;

// The member named key, or nothing.
[[nodiscard]] const Value* member(const Struct& object, const std::string_view key) {
  const auto found = object.fields().find(std::string(key));
  return found == object.fields().end() ? nullptr : &found->second;
}

// A string member that is not empty, or nothing.
[[nodiscard]] std::optional<std::string> text(const Struct& object, const std::string_view key) {
  const Value* value = member(object, key);
  if (value == nullptr || value->kind_case() != Value::kStringValue || value->string_value().empty()) {
    return std::nullopt;
  }
  return value->string_value();
}

// A number member, or nothing. protobuf's JSON parser refuses a number past
// the range of a double, and reads NaN and Infinity as strings, so it is
// finite.
[[nodiscard]] std::optional<double> number(const Struct& object, const std::string_view key) {
  const Value* value = member(object, key);
  if (value == nullptr || value->kind_case() != Value::kNumberValue) {
    return std::nullopt;
  }
  return value->number_value();
}

// One entry of "terms": a known name, value_rad, and sigma_rad of 0 or more.
[[nodiscard]] std::optional<Coefficient> coefficient(const Value& entry) {
  if (entry.kind_case() != Value::kStructValue) {
    return std::nullopt;
  }
  const std::optional<std::string> name = text(entry.struct_value(), "name");
  const std::optional<Term> term = name ? term_named(*name) : std::nullopt;
  const std::optional<double> value = number(entry.struct_value(), "value_rad");
  const std::optional<double> sigma = number(entry.struct_value(), "sigma_rad");
  if (!term || !value || !sigma || *sigma < 0.0) {
    return std::nullopt;
  }
  return Coefficient{.term = *term, .value_rad = *value};
}

// The model from the "terms" list: at least one, each given once.
[[nodiscard]] Result<PointingModel> model_of(const Value* terms) {
  if (terms == nullptr || terms->kind_case() != Value::kListValue || terms->list_value().values().empty() ||
      static_cast<std::size_t>(terms->list_value().values_size()) > kTermCount) {
    return fail(Error::kMalformed);
  }
  std::array<Coefficient, kTermCount> coefficients{};
  std::size_t count = 0;
  for (const Value& entry : terms->list_value().values()) {
    const std::optional<Coefficient> read = coefficient(entry);
    if (!read) {
      return fail(Error::kMalformed);
    }
    // The list's length was checked above.
    static_cast<void>(check(count < coefficients.size()));
    coefficients.at(count) = *read;
    ++count;
  }
  const Result<PointingModel> model = PointingModel::make(std::span<const Coefficient>(coefficients).first(count));
  return model ? model : fail(Error::kMalformed);
}

// The file's bytes, if it is a regular file of at most kMaxModelFileBytes.
// It reads one byte past the limit, so a file that grows is still caught.
[[nodiscard]] Result<std::string> read_bounded(const std::filesystem::path& path) {
  std::error_code error;
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open() || !std::filesystem::is_regular_file(path, error)) {
    return fail(Error::kUnreadable);
  }
  std::string contents(kMaxModelFileBytes + 1, '\0');
  file.read(contents.data(), static_cast<std::streamsize>(contents.size()));
  const auto read = static_cast<std::size_t>(file.gcount());
  if (read > kMaxModelFileBytes) {
    return fail(Error::kUnreadable);
  }
  contents.resize(read);
  return contents;
}

// The SHA-256 of bytes as 64 lowercase hexadecimal digits, as sha256sum
// prints it, through OpenSSL's EVP interface, so a FIPS provider computes it
// when the station loads one.
[[nodiscard]] Result<std::string> sha256_hex(const std::string_view bytes) {
  // EVP_sha256 writes 32 bytes.
  std::array<unsigned char, kSha256Bytes> digest{};
  unsigned int size = 0;
  const int hashed = EVP_Digest(bytes.data(), bytes.size(), digest.data(), &size, EVP_sha256(), nullptr);
  return detail::status_of(hashed == 1, Error::kUnavailable).map([&digest, size] {
    static_cast<void>(check(size == digest.size()));
    constexpr std::string_view kDigits = "0123456789abcdef";
    constexpr unsigned kNibble = 4;
    constexpr unsigned kLowNibble = 0x0F;
    std::string out;
    out.reserve(2 * digest.size());
    for (const unsigned char byte : digest) {
      out.push_back(kDigits[byte >> kNibble]);
      out.push_back(kDigits[byte & kLowNibble]);
    }
    return out;
  });
}

}  // namespace

Result<ParsedModel> parse_pointing_model(const std::string_view json) {
  Struct root;
  if (!google::protobuf::util::JsonStringToMessage(json, &root).ok()) {
    return fail(Error::kMalformed);
  }
  const std::optional<std::string> format = text(root, "format");
  const std::optional<double> version = number(root, "format_version");
  std::optional<std::string> model_id = text(root, "model_id");
  std::optional<std::string> station_id = text(root, "station_id");
  if (format != kFormat || version != kFormatVersion || !model_id || !station_id) {
    return fail(Error::kMalformed);
  }
  const Result<PointingModel> model = model_of(member(root, "terms"));
  if (!model) {
    return fail(model.error());
  }
  return ParsedModel{.model = *model, .model_id = std::move(*model_id), .station_id = std::move(*station_id)};
}

Result<LoadedModel> load_pointing_model(const std::filesystem::path& path) {
  const Result<std::string> contents = read_bounded(path);
  if (!contents) {
    return fail(contents.error());
  }
  Result<ParsedModel> parsed = parse_pointing_model(*contents);
  if (!parsed) {
    return fail(parsed.error());
  }
  return sha256_hex(*contents).map([&parsed, &contents](std::string digest) {
    return LoadedModel{.model = parsed->model,
                       .model_id = std::move(parsed->model_id),
                       .station_id = std::move(parsed->station_id),
                       .sha256_hex = std::move(digest),
                       .size_bytes = contents->size()};
  });
}

}  // namespace ics::encoder
