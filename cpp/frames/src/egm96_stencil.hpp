#pragma once

// The least-squares cubic fit GeographicLib's Geoid class uses to interpolate
// a geoid grid (ICS-017). Copied without change from GeographicLib 2.3's
// src/Geoid.cpp (c0_, c3_, c0n_, c3n_, c0s_, c3s_), which also gives the
// Maxima code that derives them. Copyright (c) 2008-2023, Charles Karney; MIT
// License, see cpp/frames/GEOGRAPHICLIB-LICENSE.txt.
//
// The fit takes 12 samples around a cell, in this order (x east, y south):
//
//            x = -1   0   1   2
//   y = -1            0   1
//   y =  0        2   3   4   5
//   y =  1        6   7   8   9
//   y =  2           10  11
//
// and gives the 10 coefficients of a cubic in the cell's fractions fx and fy:
// coefficient i is the sum over samples j of sample[j] * table[10 * j + i],
// divided by the table's denominator.

#include <array>
#include <cstddef>

namespace ics::frames::detail {

inline constexpr std::size_t kStencilSize = 12;
inline constexpr std::size_t kCubicTerms = 10;

struct StencilTable {
  int denominator;
  std::array<int, kStencilSize * kCubicTerms> weights;
};

// Cells away from the poles.
inline constexpr StencilTable kInteriorStencil{
    240,
    {{
        9, -18, -88,    0,  96,   90,   0,   0, -60, -20,
        -9,  18,   8,    0, -96,   30,   0,   0,  60, -20,
        9, -88, -18,   90,  96,    0, -20, -60,   0,   0,
        186, -42, -42, -150, -96, -150,  60,  60,  60,  60,
        54, 162, -78,   30, -24,  -90, -60,  60, -60,  60,
        -9, -32,  18,   30,  24,    0,  20, -60,   0,   0,
        -9,   8,  18,   30, -96,    0, -20,  60,   0,   0,
        54, -78, 162,  -90, -24,   30,  60, -60,  60, -60,
        -54,  78,  78,   90, 144,   90, -60, -60, -60, -60,
        9,  -8, -18,  -30, -24,    0,  20,  60,   0,   0,
        -9,  18, -32,    0,  24,   30,   0,   0, -60,  20,
        9, -18,  -8,    0, -24,  -30,   0,   0,  60,  20,
    }},
};

// The cells touching the north pole: the fit leaves out the terms in x, x^2
// and x^3, so the height at the pole does not depend on longitude.
inline constexpr StencilTable kNorthStencil{
    372,
    {{
        0, 0, -131, 0,  138,  144, 0,   0, -102, -31,
        0, 0,    7, 0, -138,   42, 0,   0,  102, -31,
        62, 0,  -31, 0,    0,  -62, 0,   0,    0,  31,
        124, 0,  -62, 0,    0, -124, 0,   0,    0,  62,
        124, 0,  -62, 0,    0, -124, 0,   0,    0,  62,
        62, 0,  -31, 0,    0,  -62, 0,   0,    0,  31,
        0, 0,   45, 0, -183,   -9, 0,  93,   18,   0,
        0, 0,  216, 0,   33,   87, 0, -93,   12, -93,
        0, 0,  156, 0,  153,   99, 0, -93,  -12, -93,
        0, 0,  -45, 0,   -3,    9, 0,  93,  -18,   0,
        0, 0,  -55, 0,   48,   42, 0,   0,  -84,  31,
        0, 0,   -7, 0,  -48,  -42, 0,   0,   84,  31,
    }},
};

// The cells touching the south pole, likewise.
inline constexpr StencilTable kSouthStencil{
    372,
    {{
        18,  -36, -122,   0,  120,  135, 0,   0,  -84, -31,
        -18,   36,   -2,   0, -120,   51, 0,   0,   84, -31,
        36, -165,  -27,  93,  147,   -9, 0, -93,   18,   0,
        210,   45, -111, -93,  -57, -192, 0,  93,   12,  93,
        162,  141,  -75, -93, -129, -180, 0,  93,  -12,  93,
        -36,  -21,   27,  93,   39,    9, 0, -93,  -18,   0,
        0,    0,   62,   0,    0,   31, 0,   0,    0, -31,
        0,    0,  124,   0,    0,   62, 0,   0,    0, -62,
        0,    0,  124,   0,    0,   62, 0,   0,    0, -62,
        0,    0,   62,   0,    0,   31, 0,   0,    0, -31,
        -18,   36,  -64,   0,   66,   51, 0,   0, -102,  31,
        18,  -36,    2,   0,  -66,  -51, 0,   0,  102,  31,
    }},
};

}  // namespace ics::frames::detail
