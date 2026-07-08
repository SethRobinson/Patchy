#pragma once

#include "core/document.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace patchy::aseprite {

// True when the bytes carry the Aseprite header magic (0xA5E0 at offset 4). Used to
// disambiguate ".ase" from Adobe swatch palettes, which share the extension.
[[nodiscard]] bool sniff(std::span<const std::uint8_t> bytes) noexcept;

class DocumentIo {
public:
  [[nodiscard]] static bool can_read(std::span<const std::uint8_t> bytes) noexcept;
  // Imports frame 1 (animation is future work; multi-frame files add an import notice):
  // layer tree with groups/visibility/opacity/blend modes, zlib-compressed cels via the
  // vendored miniz, indexed/grayscale/RGBA color modes, and the palette into
  // Document::indexed_palette() for the adopt-on-open prompt.
  [[nodiscard]] static Document read(std::span<const std::uint8_t> bytes,
                                     std::vector<std::string>* notices = nullptr);
  [[nodiscard]] static Document read_file(const std::filesystem::path& path,
                                          std::vector<std::string>* notices = nullptr);
  // Writes a single-frame file: palette-mode documents with room for a transparent slot
  // become indexed; everything else is RGBA (with the palette chunk attached when one
  // exists). Layer order, nesting, visibility, opacity, and mappable blend modes persist;
  // adjustment layers are skipped (Aseprite has no equivalent).
  [[nodiscard]] static std::vector<std::uint8_t> write(const Document& document);
  static void write_file(const Document& document, const std::filesystem::path& path);
};

}  // namespace patchy::aseprite
