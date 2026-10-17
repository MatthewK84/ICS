// universal_types.h: the fixed-size types that ICS's JSF AV C++ code uses in
// place of int, short, long, float and double (JSF AV Rule 209; see
// docs/cpp-jsf-av.md). It is the one file of that code where those basic
// types may be named.
#ifndef ICS_COMMON_UNIVERSAL_TYPES_H
#define ICS_COMMON_UNIVERSAL_TYPES_H

#include <cstdint>
#include <limits>

namespace ics
{

// Signed integers of 8, 16, 32 and 64 bits.
using int8 = std::int8_t;
using int16 = std::int16_t;
using int32 = std::int32_t;
using int64 = std::int64_t;

// Unsigned integers of 8, 16, 32 and 64 bits, for bit patterns and the
// fields of wire formats only: AV Rule 163 forbids unsigned arithmetic.
using uint8 = std::uint8_t;
using uint16 = std::uint16_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;

// IEEE 754 binary32 and binary64 floating-point numbers (AV Rule 146).
using float32 = float;
using float64 = double;

// The floating-point types are IEEE 754 binary32 and binary64, as AV
// Rule 146 requires.
static_assert(std::numeric_limits<float32>::is_iec559, "float32 must be IEEE 754 binary32");
static_assert(std::numeric_limits<float64>::is_iec559, "float64 must be IEEE 754 binary64");

}  // namespace ics

#endif  // ICS_COMMON_UNIVERSAL_TYPES_H
