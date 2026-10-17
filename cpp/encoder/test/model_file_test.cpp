// Reading the pointing model file ICS-035 writes, and refusing what it
// would not write.
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "encoder_support.hpp"
#include "ics/common/error.hpp"
#include "ics/encoder/pointing_model.hpp"

namespace {

using ics::encoder::Term;
using ics::encoder::testing::write_temp;

constexpr std::string_view kValid = R"({
  "format": "ics-pointing-model",
  "format_version": 1,
  "model_id": "station-1-20261016",
  "station_id": "station-1",
  "created_utc": "2026-10-16T00:00:00Z",
  "terms": [
    {"name": "IA", "value_rad": 0.002, "sigma_rad": 1e-6},
    {"name": "TF", "value_rad": -0.0001, "sigma_rad": 0}
  ]
}
)";
// sha256sum of kValid.
constexpr std::string_view kValidSha256 = "e22ba0f0fc9c510f30c71e6bda12492dcd264af3a561ab04ad046bd2be2fb71b";

// kValid with one piece of text replaced.
[[nodiscard]] std::string with(const std::string_view from, const std::string_view to) {
  std::string out(kValid);
  const std::size_t at = out.find(from);
  EXPECT_NE(at, std::string::npos) << from;
  out.replace(at, from.size(), to);
  return out;
}

[[nodiscard]] std::string with_terms(const std::string_view terms) {
  return with(R"([
    {"name": "IA", "value_rad": 0.002, "sigma_rad": 1e-6},
    {"name": "TF", "value_rad": -0.0001, "sigma_rad": 0}
  ])",
              terms);
}

TEST(ModelFile, ReadsAModelAndItsNames) {
  const auto parsed = ics::encoder::parse_pointing_model(kValid);
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed->model_id, "station-1-20261016");
  EXPECT_EQ(parsed->station_id, "station-1");
  ASSERT_EQ(parsed->model.coefficients().size(), 2U);
  EXPECT_EQ(parsed->model.coefficients()[0].term, Term::kIA);
  EXPECT_EQ(parsed->model.coefficients()[0].value_rad, 0.002);
  EXPECT_EQ(parsed->model.coefficients()[1].term, Term::kTF);
  EXPECT_EQ(parsed->model.coefficients()[1].value_rad, -0.0001);
}

TEST(ModelFile, LoadsTheGoldenModelWithItsDigest) {
  const auto loaded = ics::encoder::load_pointing_model(std::string(ICS_GOLDEN_POINTING_DIR) + "/model.json");
  ASSERT_TRUE(loaded.has_value());
  EXPECT_EQ(loaded->model_id, "golden");
  EXPECT_EQ(loaded->station_id, "golden");
  EXPECT_EQ(loaded->model.coefficients().size(), ics::encoder::kTermCount);
  EXPECT_EQ(loaded->size_bytes, std::filesystem::file_size(std::string(ICS_GOLDEN_POINTING_DIR) + "/model.json"));
  EXPECT_EQ(loaded->sha256_hex.size(), 64U);
}

TEST(ModelFile, RecordsTheFilesSha256) {
  const auto loaded = ics::encoder::load_pointing_model(write_temp("model.json", kValid));
  ASSERT_TRUE(loaded.has_value());
  EXPECT_EQ(loaded->sha256_hex, kValidSha256);
  EXPECT_EQ(loaded->size_bytes, kValid.size());
}

TEST(ModelFile, RefusesWhatIcs035WouldNotWrite) {
  const std::vector<std::string> bad{
      "not json",
      "[]",
      with(R"("format": "ics-pointing-model",)", ""),
      with("ics-pointing-model", "ics-run-record"),
      with(R"("format_version": 1)", R"("format_version": 2)"),
      with(R"("format_version": 1)", R"("format_version": "1")"),
      with(R"("model_id": "station-1-20261016")", R"("model_id": "")"),
      with(R"("model_id": "station-1-20261016")", R"("model_id": 7)"),
      with(R"("station_id": "station-1",)", ""),
      with(R"("terms")", R"("term")"),
      with_terms("[]"),
      with_terms(R"({"name": "IA", "value_rad": 0.002, "sigma_rad": 0})"),
      with_terms(R"(["IA"])"),
      with_terms(R"([{"name": "ZZ", "value_rad": 0.002, "sigma_rad": 0}])"),
      with_terms(R"([{"value_rad": 0.002, "sigma_rad": 0}])"),
      with_terms(R"([{"name": "IA", "value_rad": "NaN", "sigma_rad": 0}])"),
      with_terms(R"([{"name": "IA", "value_rad": 1e999, "sigma_rad": 0}])"),
      with_terms(R"([{"name": "IA", "sigma_rad": 0}])"),
      with_terms(R"([{"name": "IA", "value_rad": 0.002}])"),
      with_terms(R"([{"name": "IA", "value_rad": 0.002, "sigma_rad": -1e-9}])"),
      with_terms(R"([{"name": "IA", "value_rad": 0.002, "sigma_rad": 0},
                     {"name": "IA", "value_rad": 0.001, "sigma_rad": 0}])"),
  };
  for (const std::string& text : bad) {
    const auto parsed = ics::encoder::parse_pointing_model(text);
    ASSERT_FALSE(parsed.has_value()) << text;
    EXPECT_EQ(parsed.error(), ics::Error::kMalformed) << text;
  }
}

TEST(ModelFile, RefusesMoreTermsThanThereAre) {
  std::string terms = "[";
  for (std::size_t i = 0; i <= ics::encoder::kTermCount; ++i) {
    terms += std::string(i == 0 ? "" : ",") + R"({"name": "IA", "value_rad": 0, "sigma_rad": 0})";
  }
  terms += "]";
  EXPECT_EQ(ics::encoder::parse_pointing_model(with_terms(terms)).error(), ics::Error::kMalformed);
}

TEST(ModelFile, RefusesAFileItCannotRead) {
  const std::filesystem::path dir = write_temp("model.json", kValid).parent_path();
  EXPECT_EQ(ics::encoder::load_pointing_model(dir / "missing.json").error(), ics::Error::kUnreadable);
  EXPECT_EQ(ics::encoder::load_pointing_model(dir).error(), ics::Error::kUnreadable);
  const std::string huge(ics::encoder::kMaxModelFileBytes + 1, ' ');
  EXPECT_EQ(ics::encoder::load_pointing_model(write_temp("huge.json", huge)).error(), ics::Error::kUnreadable);
  EXPECT_EQ(ics::encoder::load_pointing_model(write_temp("bad.json", "{}")).error(), ics::Error::kMalformed);
}

TEST(ModelFile, ReadsAFileAtTheLimit) {
  // Spaces after the document are JSON whitespace.
  std::string padded(kValid);
  padded.resize(ics::encoder::kMaxModelFileBytes, ' ');
  const auto loaded = ics::encoder::load_pointing_model(write_temp("padded.json", padded));
  ASSERT_TRUE(loaded.has_value());
  EXPECT_EQ(loaded->size_bytes, ics::encoder::kMaxModelFileBytes);
}

}  // namespace
