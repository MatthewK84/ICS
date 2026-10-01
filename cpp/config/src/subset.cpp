#include "ics/config/subset.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace ics::config {
namespace {

// Why a check failed, or nullopt when it passed.
using Failure = std::optional<std::string_view>;

constexpr unsigned char kFirstPrintable = 0x20U;
constexpr unsigned char kLastPrintable = 0x7EU;
constexpr std::string_view kAllowedControls = "\t\n\r";
constexpr std::string_view kBlanks = " \t";
constexpr std::string_view kBareKeyCharacters = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-";
constexpr std::string_view kDigits = "0123456789";
constexpr std::string_view kDigitsAndUnderscore = "0123456789_";
constexpr std::string_view kNumberStarts = "+-0123456789";
constexpr std::string_view kSigns = "+-";
constexpr std::string_view kEscapes = "\"\\bfnrt";
// What may follow a value on its line, besides the end of the line.
constexpr std::string_view kValueEnds = " \t,]#";

constexpr std::string_view kDisallowedByte = "config files may hold only printable ASCII, tabs and line breaks";
constexpr std::string_view kExpectedKey = "expected a key of letters, digits, '_' or '-'";
constexpr std::string_view kExpectedEquals = "expected '=' after the key";
constexpr std::string_view kExpectedTableEnd = "expected ']' to close the table name";
constexpr std::string_view kArrayOfTables = "arrays of tables ([[name]]) are not supported";
constexpr std::string_view kExpectedValue = "expected a value: \"text\", 'text', a number, true, false or a [list]";
constexpr std::string_view kExpectedValueEnd = "expected a space, ',', ']', a comment or the end of the line";
constexpr std::string_view kExpectedListEnd = "expected ',' or ']' in the list";
constexpr std::string_view kExpectedLineEnd = "expected a comment or the end of the line";
constexpr std::string_view kUnclosedText = "text must be closed on the line it starts";
constexpr std::string_view kUnsupportedEscape = "unsupported escape; use \\\" \\\\ \\b \\f \\n \\r or \\t";
constexpr std::string_view kInvalidNumber = "invalid number";
constexpr std::string_view kLeadingZero = "a number must not start with 0 unless it is 0";
constexpr std::string_view kLoneCarriageReturn = "a carriage return must be followed by a line feed";

// A position in the text, with its line and column for error messages.
class Cursor {
 public:
  explicit Cursor(const std::string_view text) noexcept : text_(text) {}

  [[nodiscard]] bool at_end() const noexcept { return offset_ >= text_.size(); }
  // The current character, or '\0' at the end; the text holds no NUL.
  [[nodiscard]] char peek() const noexcept { return at_end() ? '\0' : text_[offset_]; }
  [[nodiscard]] bool at_any(const std::string_view set) const noexcept {
    return !at_end() && set.find(peek()) != std::string_view::npos;
  }
  [[nodiscard]] bool at_line_end() const noexcept { return at_end() || peek() == '\n' || peek() == '\r'; }

  // Moves past the current character; call only when not at the end.
  void advance() noexcept { ++offset_; }
  // Moves past character when it is the current one.
  [[nodiscard]] bool accept(const char character) noexcept {
    if (peek() != character) {
      return false;
    }
    advance();
    return true;
  }
  [[nodiscard]] bool accept_word(const std::string_view word) noexcept {
    if (!text_.substr(offset_).starts_with(word)) {
      return false;
    }
    offset_ += word.size();
    return true;
  }
  void skip_any(const std::string_view set) noexcept {
    while (at_any(set)) {
      advance();
    }
  }
  void skip_to_line_end() noexcept {
    while (!at_line_end()) {
      advance();
    }
  }
  // Records that the cursor is at the start of a new line.
  void start_line() noexcept {
    ++line_;
    line_start_ = offset_;
  }

  [[nodiscard]] std::size_t line() const noexcept { return line_; }
  [[nodiscard]] std::size_t column() const noexcept { return offset_ - line_start_ + 1; }

 private:
  std::string_view text_;
  std::size_t offset_ = 0;
  std::size_t line_start_ = 0;
  std::size_t line_ = 1;
};

bool allowed(const char character) {
  const auto byte = static_cast<unsigned char>(character);
  const bool printable = byte >= kFirstPrintable && byte <= kLastPrintable;
  return printable || kAllowedControls.find(character) != std::string_view::npos;
}

// Stops the cursor at the first byte that is not allowed, if there is one.
Failure check_bytes(Cursor& cursor) {
  while (!cursor.at_end() && allowed(cursor.peek())) {
    if (cursor.accept('\n')) {
      cursor.start_line();
    } else {
      cursor.advance();
    }
  }
  return cursor.at_end() ? Failure() : kDisallowedByte;
}

void skip_sign(Cursor& cursor) {
  if (cursor.at_any(kSigns)) {
    cursor.advance();
  }
}

// Digits with single underscores between them, such as 1_000.
Failure check_digits(Cursor& cursor) {
  if (!cursor.at_any(kDigits)) {
    return kInvalidNumber;
  }
  while (cursor.at_any(kDigitsAndUnderscore)) {
    if (cursor.accept('_') && !cursor.at_any(kDigits)) {
      return kInvalidNumber;
    }
    cursor.advance();
  }
  return std::nullopt;
}

// The part of a number before any fraction or exponent: 0, or digits that do
// not start with 0.
Failure check_integer_part(Cursor& cursor) {
  if (!cursor.accept('0')) {
    return check_digits(cursor);
  }
  if (cursor.at_any(kDigitsAndUnderscore)) {
    return kLeadingZero;
  }
  return std::nullopt;
}

// A decimal integer or float, such as -1_000, 1.5e-3 or +inf.
Failure check_number(Cursor& cursor) {
  skip_sign(cursor);
  if (cursor.accept_word("inf") || cursor.accept_word("nan")) {
    return std::nullopt;
  }
  Failure failure = check_integer_part(cursor);
  if (!failure.has_value() && cursor.accept('.')) {
    failure = check_digits(cursor);
  }
  if (!failure.has_value() && (cursor.accept('e') || cursor.accept('E'))) {
    skip_sign(cursor);
    failure = check_digits(cursor);
  }
  return failure;
}

Failure check_basic_string(Cursor& cursor) {
  cursor.advance();
  while (!cursor.accept('"')) {
    if (cursor.at_line_end()) {
      return kUnclosedText;
    }
    if (cursor.accept('\\') && !cursor.at_any(kEscapes)) {
      return kUnsupportedEscape;
    }
    cursor.advance();
  }
  return std::nullopt;
}

Failure check_literal_string(Cursor& cursor) {
  cursor.advance();
  while (!cursor.accept('\'')) {
    if (cursor.at_line_end()) {
      return kUnclosedText;
    }
    cursor.advance();
  }
  return std::nullopt;
}

Failure check_scalar_body(Cursor& cursor) {
  if (cursor.peek() == '"') {
    return check_basic_string(cursor);
  }
  if (cursor.peek() == '\'') {
    return check_literal_string(cursor);
  }
  if (cursor.accept_word("true") || cursor.accept_word("false") || cursor.accept_word("inf") ||
      cursor.accept_word("nan")) {
    return std::nullopt;
  }
  if (cursor.at_any(kNumberStarts)) {
    return check_number(cursor);
  }
  return kExpectedValue;
}

// A value other than a list, which must be followed by a space, a separator,
// a comment or the end of the line.
Failure check_scalar(Cursor& cursor) {
  const Failure failure = check_scalar_body(cursor);
  if (failure.has_value()) {
    return failure;
  }
  if (!cursor.at_line_end() && !cursor.at_any(kValueEnds)) {
    return kExpectedValueEnd;
  }
  return std::nullopt;
}

// A list of values on one line, with an optional comma after the last.
Failure check_list(Cursor& cursor) {
  cursor.advance();
  cursor.skip_any(kBlanks);
  while (!cursor.accept(']')) {
    const Failure failure = check_scalar(cursor);
    if (failure.has_value()) {
      return failure;
    }
    cursor.skip_any(kBlanks);
    if (cursor.accept(',')) {
      cursor.skip_any(kBlanks);
    } else if (cursor.peek() != ']') {
      return kExpectedListEnd;
    }
  }
  return std::nullopt;
}

Failure check_simple_key(Cursor& cursor) {
  if (!cursor.at_any(kBareKeyCharacters)) {
    return kExpectedKey;
  }
  cursor.skip_any(kBareKeyCharacters);
  return std::nullopt;
}

// A bare key, or bare keys joined by dots, and any blanks after it.
Failure check_key(Cursor& cursor) {
  Failure failure = check_simple_key(cursor);
  cursor.skip_any(kBlanks);
  while (!failure.has_value() && cursor.accept('.')) {
    cursor.skip_any(kBlanks);
    failure = check_simple_key(cursor);
    cursor.skip_any(kBlanks);
  }
  return failure;
}

Failure check_table(Cursor& cursor) {
  cursor.advance();
  if (cursor.peek() == '[') {
    return kArrayOfTables;
  }
  cursor.skip_any(kBlanks);
  const Failure failure = check_key(cursor);
  if (failure.has_value()) {
    return failure;
  }
  if (!cursor.accept(']')) {
    return kExpectedTableEnd;
  }
  return std::nullopt;
}

Failure check_setting(Cursor& cursor) {
  const Failure failure = check_key(cursor);
  if (failure.has_value()) {
    return failure;
  }
  if (!cursor.accept('=')) {
    return kExpectedEquals;
  }
  cursor.skip_any(kBlanks);
  if (cursor.peek() == '[') {
    return check_list(cursor);
  }
  return check_scalar(cursor);
}

// One line, up to its line break.
Failure check_line(Cursor& cursor) {
  cursor.skip_any(kBlanks);
  Failure failure = std::nullopt;
  if (cursor.peek() == '[') {
    failure = check_table(cursor);
  } else if (!cursor.at_line_end() && cursor.peek() != '#') {
    failure = check_setting(cursor);
  }
  if (failure.has_value()) {
    return failure;
  }
  cursor.skip_any(kBlanks);
  if (cursor.peek() == '#') {
    cursor.skip_to_line_end();
  }
  return cursor.at_line_end() ? Failure() : kExpectedLineEnd;
}

// Moves past the line break at the cursor: LF, or CR LF.
Failure end_line(Cursor& cursor) {
  if (cursor.accept('\r') && cursor.peek() != '\n') {
    return kLoneCarriageReturn;
  }
  cursor.advance();
  cursor.start_line();
  return std::nullopt;
}

Failure check_lines(Cursor& cursor) {
  Failure failure = check_line(cursor);
  while (!failure.has_value() && !cursor.at_end()) {
    failure = end_line(cursor);
    if (!failure.has_value()) {
      failure = check_line(cursor);
    }
  }
  return failure;
}

std::string describe(const Cursor& at, const std::string_view reason) {
  std::string text = "line " + std::to_string(at.line()) + ", column " + std::to_string(at.column()) + ": ";
  text.append(reason);
  return text;
}

}  // namespace

std::optional<std::string> subset_error(const std::string_view text) {
  Cursor bytes(text);
  const Failure byte_failure = check_bytes(bytes);
  if (byte_failure.has_value()) {
    return describe(bytes, *byte_failure);
  }
  Cursor lines(text);
  const Failure line_failure = check_lines(lines);
  if (line_failure.has_value()) {
    return describe(lines, *line_failure);
  }
  return std::nullopt;
}

}  // namespace ics::config
