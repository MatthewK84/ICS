#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

namespace ics::flightlog::testing {

// Writes ULog logs for tests, message by message.
class ULogBuilder {
 public:
  ULogBuilder() {
    constexpr std::array<std::uint8_t, 8> kHeader{0x55, 0x4c, 0x6f, 0x67, 0x01, 0x12, 0x35, 0x01};
    for (const std::uint8_t b : kHeader) {
      bytes_.push_back(static_cast<std::byte>(b));
    }
    bytes_.resize(bytes_.size() + sizeof(std::uint64_t));
  }

  template <typename T>
  static void put(std::vector<std::byte>& out, const T value) {
    std::array<std::byte, sizeof(T)> raw{};
    std::memcpy(raw.data(), &value, sizeof(T));
    out.insert(out.end(), raw.begin(), raw.end());
  }

  static void put_text(std::vector<std::byte>& out, const std::string_view text) {
    for (const char c : text) {
      out.push_back(static_cast<std::byte>(c));
    }
  }

  ULogBuilder& message(const char type, const std::vector<std::byte>& body) {
    put(bytes_, static_cast<std::uint16_t>(body.size()));
    bytes_.push_back(static_cast<std::byte>(type));
    bytes_.insert(bytes_.end(), body.begin(), body.end());
    return *this;
  }

  ULogBuilder& format(const std::string_view text) {
    std::vector<std::byte> body;
    put_text(body, text);
    return message('F', body);
  }

  ULogBuilder& add_logged(const std::uint8_t multi_id, const std::uint16_t msg_id, const std::string_view topic) {
    std::vector<std::byte> body;
    put(body, multi_id);
    put(body, msg_id);
    put_text(body, topic);
    return message('A', body);
  }

  ULogBuilder& data(const std::uint16_t msg_id, const std::vector<std::byte>& sample) {
    std::vector<std::byte> body;
    put(body, msg_id);
    body.insert(body.end(), sample.begin(), sample.end());
    return message('D', body);
  }

  ULogBuilder& logging(const std::uint64_t timestamp_us, const std::string_view text) {
    std::vector<std::byte> body;
    put(body, std::uint8_t{54});
    put(body, timestamp_us);
    put_text(body, text);
    return message('L', body);
  }

  ULogBuilder& tagged(const std::uint64_t timestamp_us, const std::string_view text) {
    std::vector<std::byte> body;
    put(body, std::uint8_t{52});
    put(body, std::uint16_t{7});
    put(body, timestamp_us);
    put_text(body, text);
    return message('C', body);
  }

  // An information ('I') or parameter ('P') message: "type name", then a value.
  ULogBuilder& key_value(const char type, const std::string_view key, const std::vector<std::byte>& value) {
    std::vector<std::byte> body;
    put(body, static_cast<std::uint8_t>(key.size()));
    put_text(body, key);
    body.insert(body.end(), value.begin(), value.end());
    return message(type, body);
  }

  ULogBuilder& flag_bits(const std::uint8_t incompatible, const std::array<std::uint64_t, 3>& appended,
                         const std::uint8_t second_incompatible = 0) {
    std::vector<std::byte> body(16);
    body[8] = static_cast<std::byte>(incompatible);
    body[9] = static_cast<std::byte>(second_incompatible);
    for (const std::uint64_t offset : appended) {
      put(body, offset);
    }
    return message('B', body);
  }

  [[nodiscard]] std::size_t size() const { return bytes_.size(); }
  [[nodiscard]] const std::vector<std::byte>& bytes() const { return bytes_; }
  [[nodiscard]] std::vector<std::byte>& bytes() { return bytes_; }

 private:
  std::vector<std::byte> bytes_;
};

// The bytes of values, in order.
template <typename... T>
std::vector<std::byte> sample(const T... values) {
  std::vector<std::byte> out;
  (ULogBuilder::put(out, values), ...);
  return out;
}

}  // namespace ics::flightlog::testing
