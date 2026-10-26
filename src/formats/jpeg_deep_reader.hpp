#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace patchy::jpeg_deep {

// JPEGs Qt's 8-bit JPEG plug-in cannot read: 12-bit samples (extended sequential and
// progressive DCT) and lossless coding (SOF3/SOF11) at 2 to 16 bits. They decode through
// the vendored, decode-only libjpeg-turbo 3.1.4.1 (src/formats/libjpeg-turbo,
// cmake/patchy_libjpeg.cmake; docs/file-formats.md, "12-bit and lossless JPEG").
// Ordinary 8-bit JPEGs keep opening through Qt. Read only: Patchy never writes these.

// True when the first frame header says a precision other than 8 bits or lossless coding,
// in a frame type libjpeg-turbo decodes (hierarchical frames stay unsupported).
[[nodiscard]] bool needs_deep_decoder(std::span<const std::uint8_t> bytes) noexcept;

struct Image {
  int width{0};
  int height{0};
  int channels{0};      // 1 (gray) or 3 (RGB)
  int precision{0};     // the file's bits per sample
  bool lossless{false};
  int output_bits{16};  // 8 for precisions up to 8 bits, else 16
  // Interleaved, rows top down, scaled to the full 0..255 or 0..65535 range
  // (12-bit v becomes round(v * 65535 / 4095)).
  std::vector<std::uint16_t> samples;
};

// Throws std::runtime_error with a PATCHY_TRANSLATE_NOOP message on a damaged file, a
// CMYK/YCCK one (8-bit CMYK keeps opening through Qt) or one too large to open.
[[nodiscard]] Image read_deep_jpeg(std::span<const std::uint8_t> bytes);

}  // namespace patchy::jpeg_deep
