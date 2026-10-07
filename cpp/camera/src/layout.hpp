#pragma once

#include <cstddef>
#include <cstdint>

// The cine layout ICS reads and writes, as the PIMS reader (BSD-3-Clause,
// github.com/soft-matter/pims, pims/cine.py) lays it out. Offsets are in
// bytes from the start of their structure.
namespace ics::camera::detail {

// The file header: "CI", then the counts, then where the other parts start.
inline constexpr std::uint16_t kCineMark = 0x4943;  // "CI"
inline constexpr std::size_t kHeaderBytes = 44;
inline constexpr std::size_t kHeaderSizeAt = 2;
inline constexpr std::size_t kCompressionAt = 4;
inline constexpr std::size_t kVersionAt = 6;
inline constexpr std::size_t kFirstMovieImageAt = 8;
inline constexpr std::size_t kTotalImageCountAt = 12;
inline constexpr std::size_t kFirstImageNoAt = 16;
inline constexpr std::size_t kImageCountAt = 20;
inline constexpr std::size_t kOffImageHeaderAt = 24;
inline constexpr std::size_t kOffSetupAt = 28;
inline constexpr std::size_t kOffImageOffsetsAt = 32;
inline constexpr std::size_t kTriggerTimeAt = 36;

// The bitmap header, as Windows lays out BITMAPINFOHEADER.
inline constexpr std::size_t kBitmapBytes = 40;
inline constexpr std::size_t kBitmapWidthAt = 4;
inline constexpr std::size_t kBitmapHeightAt = 8;
inline constexpr std::size_t kBitmapPlanesAt = 12;
inline constexpr std::size_t kBitmapBitCountAt = 14;
inline constexpr std::size_t kBitmapImageSizeAt = 20;

// The setup: "ST" and its length mark the layout ICS reads; the frame rate,
// the real bit depth and the exposure in nanoseconds follow at fixed offsets.
inline constexpr std::uint16_t kSetupMark = 0x5453;  // "ST"
inline constexpr std::size_t kSetupMarkAt = 140;
inline constexpr std::size_t kSetupLengthAt = 142;
inline constexpr std::size_t kFrameRateAt = 768;
inline constexpr std::size_t kSoftwareVersionAt = 800;
inline constexpr std::size_t kRealBppAt = 896;
inline constexpr std::size_t kShutterNsAt = 1568;
// The shortest setup that holds every field ICS reads.
inline constexpr std::size_t kSetupMinimum = 1572;

// Each tagged block: its size, including this header, its type, and whether
// another block follows.
inline constexpr std::size_t kTagHeaderBytes = 8;
inline constexpr std::size_t kTagTypeAt = 4;
inline constexpr std::size_t kTagMoreAt = 6;
inline constexpr std::uint16_t kTimeBlock = 1002;
inline constexpr std::uint16_t kExposureBlock = 1003;
// Range data, after which PIMS reads no more blocks.
inline constexpr std::uint16_t kRangeBlock = 1004;

// Each frame's image block: the annotation's size, including its own field
// and the image size that ends it, then the image.
inline constexpr std::size_t kAnnotationMinimum = 8;
inline constexpr std::size_t kImageSizeBytes = 4;

inline constexpr std::uint64_t kNsPerSecond = 1'000'000'000;
inline constexpr unsigned kFractionBits = 32;

}  // namespace ics::camera::detail
