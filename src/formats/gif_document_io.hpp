#pragma once

#include "core/document.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace patchy::gif {

// The alpha threshold RGB documents quantize with (write() below and the animated writer
// in ui/ must agree so a flattened save and an animation frame index identically).
inline constexpr std::uint8_t kQuantizeAlphaThreshold = 128;

// Encodes a single-frame GIF89a (write-only: reading stays with Qt's bundled qgif plugin).
// palette holds 2..256 opaque RGB entries; indexes is row-major with one byte per pixel,
// each < palette.size(); transparent_index -1 means fully opaque. LZW with dynamic code
// sizes — the patents expired in 2003/2004.
[[nodiscard]] std::vector<std::uint8_t> encode(std::int32_t width, std::int32_t height,
                                               std::span<const RgbColor> palette,
                                               std::span<const std::uint8_t> indexes, int transparent_index);

// One animation frame: a full-canvas indexed image plus its display delay. Every frame
// carries its own palette; encode_animation writes frame 1's as the global color table and
// later frames' as local color tables.
struct GifFrame {
  std::vector<RgbColor> palette;      // 1..256 opaque RGB entries
  int transparent_index{-1};          // -1 = fully opaque frame
  std::uint16_t delay_cs{0};          // display delay in centiseconds (the GIF wire unit)
  std::vector<std::uint8_t> indexes;  // row-major width*height, each < palette.size()
};

// Encodes a looping animated GIF89a: NETSCAPE2.0 loop-forever extension, then per frame a
// Graphic Control Extension (disposal "restore to background" so every frame displays
// alone, the delay, transparency when transparent_index >= 0) and a full-canvas image.
// The single-frame encode() byte stream is pinned by gif_encoder_bytes_are_stable and
// stays untouched by this path.
[[nodiscard]] std::vector<std::uint8_t> encode_animation(std::int32_t width, std::int32_t height,
                                                         std::span<const GifFrame> frames);
void write_animation_file(std::int32_t width, std::int32_t height, std::span<const GifFrame> frames,
                          const std::filesystem::path& path);

// Trailing delay token of a layer name ("blink 0.25s" -> 25 centiseconds). Grammar: the
// last whitespace-separated token is digits with at most one '.', at least one digit,
// then a lowercase 's'. Hand-parsed (locale-free); seconds round half-up to centiseconds
// and clamp to the u16 wire range. The import path stamps names with
// format_delay_seconds_token and the export path parses them back with this.
[[nodiscard]] std::optional<std::uint16_t> parse_layer_name_delay_cs(std::string_view layer_name);

// 10 -> "0.1s", 4 -> "0.04s", 100 -> "1s", 0 -> "0s" (trailing zeros trimmed).
[[nodiscard]] std::string format_delay_seconds_token(std::uint16_t delay_cs);

// Document-level writer: palette-mode documents use the document palette in file order plus
// one transparent slot via the editing alpha threshold (the indexed PNG-8 semantics); RGB
// documents quantize (exact colors when they fit, else deterministic median cut), reserving
// one slot for transparency when the flatten has hidden pixels. No dithering.
[[nodiscard]] std::vector<std::uint8_t> write(const Document& document);
void write_file(const Document& document, const std::filesystem::path& path);

}  // namespace patchy::gif
