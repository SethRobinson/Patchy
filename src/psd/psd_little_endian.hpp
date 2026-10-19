#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Photoshop layer data in little-endian ("IBM PC") byte order, as a TIFF saved with that
// byte order stores it in tag 37724 (docs/tiff.md). Every structural field is little-endian
// there: lengths, counts, rectangles, RLE row counts, compression codes, OSTypes (so '8BIM'
// reads "MIB8"), UTF-16 code units, doubles and descriptor values. Channel pixel samples and
// embedded files stay big-endian. These functions rewrite such data into the big-endian
// form the PSD reader parses. They walk each structure field by field, mirroring the PSD
// parsers, and drop what they cannot walk instead of guessing. Qt-free.
namespace patchy::psd {

struct LittleEndianSource {
  // "Adobe Photoshop Document Data V0002" data: PSB widths (u64 channel lengths and wide
  // block lengths, u32 RLE row counts).
  bool large_document{false};
};

// The contents of a 'Layr', 'Lr16' or 'Lr32' block (layer count, records, channel image
// data) in big-endian. Per-layer tagged blocks whose layout is unknown, or that do not
// walk cleanly, are left out and their keys appended to `dropped`. Throws
// std::runtime_error when the records or channel data themselves are malformed.
[[nodiscard]] std::vector<std::uint8_t> layer_info_to_big_endian(std::span<const std::uint8_t> payload,
                                                                 const LittleEndianSource& source,
                                                                 std::vector<std::string>& dropped);

// One per-layer tagged block's payload in big-endian, or nullopt when the key's layout is
// unknown or the payload does not walk cleanly.
[[nodiscard]] std::optional<std::vector<std::uint8_t>> layer_block_to_big_endian(
    std::string_view key, std::span<const std::uint8_t> payload, const LittleEndianSource& source);

// One document-level block ('LMsk', 'Patt', 'lnk2', 'cinf', ...) in big-endian, or nullopt.
[[nodiscard]] std::optional<std::vector<std::uint8_t>> document_block_to_big_endian(
    std::string_view key, std::span<const std::uint8_t> payload, const LittleEndianSource& source);

// A four-character code read from little-endian bytes ("MIB8" -> "8BIM").
[[nodiscard]] std::string little_endian_ostype(std::span<const std::uint8_t, 4> bytes);

}  // namespace patchy::psd
