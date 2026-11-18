#pragma once

#include "core/document.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace patchy::aseprite {

// True when the bytes carry the Aseprite header magic (0xA5E0 at offset 4). Used to
// disambiguate ".ase" from Adobe swatch palettes, which share the extension.
[[nodiscard]] bool sniff(std::span<const std::uint8_t> bytes) noexcept;

// True when an Aseprite layer can store `mode` as itself. Pass Through writes as Normal
// (the writer saves isolated Aseprite groups), so it reports false; it maps every other
// mode it lacks to Normal too.
[[nodiscard]] bool stores_blend_mode(BlendMode mode) noexcept;

using FrameName = std::function<std::string(int frame_number, std::uint32_t delay_ms)>;

class DocumentIo {
public:
  [[nodiscard]] static bool can_read(std::span<const std::uint8_t> bytes) noexcept;
  // A still keeps its layer tree. An animation imports one timed group per frame,
  // frame 1 on top, each containing the source tree and that frame's cels.
  // Animations set partial_import so ordinary Save asks for a new destination.
  // frame_name supplies the UI's translated group names.
  [[nodiscard]] static Document read(std::span<const std::uint8_t> bytes,
                                     std::vector<std::string>* notices = nullptr,
                                     bool* partial_import = nullptr, const FrameName& frame_name = {});
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
