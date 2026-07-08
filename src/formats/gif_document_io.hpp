#pragma once

#include "core/document.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace patchy::gif {

// Encodes a single-frame GIF89a (write-only: reading stays with Qt's bundled qgif plugin).
// palette holds 2..256 opaque RGB entries; indexes is row-major with one byte per pixel,
// each < palette.size(); transparent_index -1 means fully opaque. LZW with dynamic code
// sizes — the patents expired in 2003/2004.
[[nodiscard]] std::vector<std::uint8_t> encode(std::int32_t width, std::int32_t height,
                                               std::span<const RgbColor> palette,
                                               std::span<const std::uint8_t> indexes, int transparent_index);

// Document-level writer: palette-mode documents use the document palette in file order plus
// one transparent slot via the editing alpha threshold (the indexed PNG-8 semantics); RGB
// documents quantize (exact colors when they fit, else deterministic median cut), reserving
// one slot for transparency when the flatten has hidden pixels. No dithering.
[[nodiscard]] std::vector<std::uint8_t> write(const Document& document);
void write_file(const Document& document, const std::filesystem::path& path);

}  // namespace patchy::gif
