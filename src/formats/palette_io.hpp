#pragma once

#include "core/palette.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace patchy::palette_io {

// A palette loaded from a swatch/palette file. transparent_index survives only
// formats that model one (772-byte .act); name only formats that carry one
// (.gpl, .aco v2/.ase entries are ignored for the palette-level name).
struct PaletteFileData {
  std::vector<RgbColor> colors;
  std::optional<std::uint16_t> transparent_index;
  std::string name;
};

enum class PaletteFileFormat {
  JascPal,  // Paint Shop Pro "JASC-PAL" text
  Gpl,      // GIMP / Aseprite / Krita text
  Hex,      // Lospec: one RRGGBB per line
  Act,      // Adobe Color Table (768 or 772 bytes, big-endian trailer)
  Aco       // Adobe Color Swatches, version 1 section
};

// Reads any supported palette file by content sniffing (signatures first: RIFF
// PAL, JASC-PAL, GIMP Palette, ASEF, .act by exact size, .aco by header; bare
// .hex lines last). Indexed BMP files are also accepted (their color table is
// the palette). Throws std::runtime_error with a user-presentable message on
// malformed input; an empty result never returns.
[[nodiscard]] PaletteFileData read_palette_bytes(std::span<const std::uint8_t> bytes);
[[nodiscard]] PaletteFileData read_palette_file(const std::filesystem::path& path);

// The palette-file extensions read_palette_file understands (lowercase, no dot).
[[nodiscard]] std::span<const std::string_view> readable_palette_extensions() noexcept;

[[nodiscard]] std::vector<std::uint8_t> write_palette_bytes(std::span<const RgbColor> colors,
                                                            PaletteFileFormat format, std::string_view name);
void write_palette_file(const std::filesystem::path& path, std::span<const RgbColor> colors,
                        PaletteFileFormat format, std::string_view name);

// Suggested format for a save path's extension; nullopt for unknown extensions.
[[nodiscard]] std::optional<PaletteFileFormat> palette_format_for_extension(std::string_view extension) noexcept;

}  // namespace patchy::palette_io
