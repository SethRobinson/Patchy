#pragma once

#include "core/document.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace patchy::ilbm {

class DocumentIo {
public:
  [[nodiscard]] static bool can_read(std::span<const std::uint8_t> bytes) noexcept;
  // FORM ILBM (planar) and PBM (chunky) with BMHD/CMAP/CAMG/BODY: ByteRun1 compression,
  // masking (interleaved mask plane or transparent color), and EHB (extra-halfbrite).
  // HAM images are rejected with a clear message. The palette populates
  // Document::indexed_palette() for the adopt-on-open prompt.
  [[nodiscard]] static Document read(std::span<const std::uint8_t> bytes,
                                     std::vector<std::string>* notices = nullptr);
  [[nodiscard]] static Document read_file(const std::filesystem::path& path,
                                          std::vector<std::string>* notices = nullptr);
  // Writes planar ILBM: palette-mode documents use the document palette; RGB documents
  // quantize to 256 colors first (median cut, same policy as GIF). Transparency becomes
  // masking type 2 (transparent color) when a transparent slot exists.
  [[nodiscard]] static std::vector<std::uint8_t> write(const Document& document);
  static void write_file(const Document& document, const std::filesystem::path& path);
};

}  // namespace patchy::ilbm
