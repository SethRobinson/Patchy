#pragma once

#include <array>
#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <utility>
#include <vector>

namespace patchy::test {

// Minimal uncompressed 16-bit Bayer DNG (TIFF little-endian), synthesized byte-by-byte in
// the house adversarial-fixture tradition so the LibRaw-backed raw pipeline can be
// exercised on every platform with no committed camera files. The synthetic camera's
// ColorMatrix1 is exactly XYZ(D65) -> linear sRGB and AsShotNeutral is (1, 1, 1) — the
// sRGB response to D65 white — so a neutral gray card reads the same value on every CFA
// site and as-shot white balance renders neutral gray.
struct SyntheticDngOptions {
  std::uint16_t orientation{1};
  std::uint16_t red_value{11796};
  std::uint16_t green_value{11796};
  std::uint16_t blue_value{11796};
};

inline std::vector<std::uint8_t> synthetic_bayer_dng(std::int32_t width, std::int32_t height,
                                                     const SyntheticDngOptions& options = {}) {
  struct Entry {
    std::uint16_t tag;
    std::uint16_t type;
    std::uint32_t count;
    std::uint32_t value;
  };
  constexpr std::uint16_t kTypeByte = 1;
  constexpr std::uint16_t kTypeAscii = 2;
  constexpr std::uint16_t kTypeShort = 3;
  constexpr std::uint16_t kTypeLong = 4;
  constexpr std::uint16_t kTypeRational = 5;
  constexpr std::uint16_t kTypeSrational = 10;

  constexpr std::uint32_t kEntryCount = 20;
  constexpr std::uint32_t kIfdOffset = 8;
  constexpr std::uint32_t kDataArea = kIfdOffset + 2 + kEntryCount * 12 + 4;

  std::vector<std::uint8_t> extra;  // out-of-line values, file offsets relative to kDataArea
  const auto extra_offset = [&] { return kDataArea + static_cast<std::uint32_t>(extra.size()); };
  const auto pad_extra = [&] {
    if (extra.size() % 2 != 0) {
      extra.push_back(0);
    }
  };
  const auto put16 = [](std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
  };
  const auto put32 = [&put16](std::vector<std::uint8_t>& out, std::uint32_t value) {
    put16(out, static_cast<std::uint16_t>(value & 0xFFFFU));
    put16(out, static_cast<std::uint16_t>(value >> 16));
  };
  const auto add_ascii = [&](std::string_view text) {
    pad_extra();
    const auto offset = extra_offset();
    extra.insert(extra.end(), text.begin(), text.end());
    extra.push_back(0);
    return std::pair<std::uint32_t, std::uint32_t>(offset, static_cast<std::uint32_t>(text.size() + 1));
  };
  const auto add_srationals = [&](std::initializer_list<std::int32_t> numerators, std::int32_t denominator) {
    pad_extra();
    const auto offset = extra_offset();
    for (const auto numerator : numerators) {
      put32(extra, static_cast<std::uint32_t>(numerator));
      put32(extra, static_cast<std::uint32_t>(denominator));
    }
    return offset;
  };
  const auto add_rationals = [&](std::initializer_list<std::uint32_t> numerators, std::uint32_t denominator) {
    pad_extra();
    const auto offset = extra_offset();
    for (const auto numerator : numerators) {
      put32(extra, numerator);
      put32(extra, denominator);
    }
    return offset;
  };

  const auto [make_offset, make_count] = add_ascii("Patchy");
  const auto [model_offset, model_count] = add_ascii("Synthetic");
  const auto [unique_offset, unique_count] = add_ascii("Patchy Synthetic");
  // XYZ(D65) -> linear sRGB rows: the synthetic camera IS sRGB, keeping color math exact.
  const auto color_matrix_offset =
      add_srationals({32405, -15371, -4985, -9693, 18760, 416, 556, -2040, 10572}, 10000);
  const auto as_shot_neutral_offset = add_rationals({10000, 10000, 10000}, 10000);
  pad_extra();
  const auto pixel_offset = extra_offset();
  const auto pixel_count = static_cast<std::uint32_t>(width) * static_cast<std::uint32_t>(height);

  const std::array<Entry, kEntryCount> entries = {{
      {254, kTypeLong, 1, 0},                                  // NewSubfileType: primary image
      {256, kTypeLong, 1, static_cast<std::uint32_t>(width)},  // ImageWidth
      {257, kTypeLong, 1, static_cast<std::uint32_t>(height)},  // ImageLength
      {258, kTypeShort, 1, 16},                                 // BitsPerSample
      {259, kTypeShort, 1, 1},                                  // Compression: none
      {262, kTypeShort, 1, 32803},                              // Photometric: CFA
      {271, kTypeAscii, make_count, make_offset},               // Make
      {272, kTypeAscii, model_count, model_offset},             // Model
      {273, kTypeLong, 1, pixel_offset},                        // StripOffsets
      {274, kTypeShort, 1, options.orientation},                // Orientation
      {277, kTypeShort, 1, 1},                                  // SamplesPerPixel
      {278, kTypeLong, 1, static_cast<std::uint32_t>(height)},  // RowsPerStrip
      {279, kTypeLong, 1, pixel_count * 2},                     // StripByteCounts
      {33421, kTypeShort, 2, 0x00020002U},                      // CFARepeatPatternDim 2x2
      {33422, kTypeByte, 4, 0x02010100U},                       // CFAPattern RGGB
      {50706, kTypeByte, 4, 0x00000401U},                       // DNGVersion 1.4.0.0
      {50708, kTypeAscii, unique_count, unique_offset},         // UniqueCameraModel
      {50721, kTypeSrational, 9, color_matrix_offset},          // ColorMatrix1
      {50728, kTypeRational, 3, as_shot_neutral_offset},        // AsShotNeutral
      {50778, kTypeShort, 1, 21},                               // CalibrationIlluminant1: D65
  }};

  std::vector<std::uint8_t> bytes;
  bytes.reserve(kDataArea + extra.size() + pixel_count * 2);
  bytes.push_back('I');
  bytes.push_back('I');
  put16(bytes, 42);
  put32(bytes, kIfdOffset);
  put16(bytes, static_cast<std::uint16_t>(kEntryCount));
  for (const auto& entry : entries) {
    put16(bytes, entry.tag);
    put16(bytes, entry.type);
    put32(bytes, entry.count);
    put32(bytes, entry.value);
  }
  put32(bytes, 0);  // no next IFD
  bytes.insert(bytes.end(), extra.begin(), extra.end());
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      const bool even_row = y % 2 == 0;
      const bool even_column = x % 2 == 0;
      std::uint16_t value = options.green_value;
      if (even_row && even_column) {
        value = options.red_value;
      } else if (!even_row && !even_column) {
        value = options.blue_value;
      }
      put16(bytes, value);
    }
  }
  return bytes;
}

}  // namespace patchy::test
