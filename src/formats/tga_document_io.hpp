#pragma once

#include "core/document.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace patchy::tga {

class DocumentIo {
public:
  [[nodiscard]] static bool can_read(std::span<const std::uint8_t> bytes) noexcept;
  // Types 1/2/3/9/10/11 (indexed / truecolor / grayscale, raw + RLE) at 8/24/32 bits with
  // both origin flags. 15/16-bit files are rejected with a clear message. Indexed sources
  // populate Document::indexed_palette() so the adopt-on-open prompt fires like indexed BMP.
  [[nodiscard]] static Document read(std::span<const std::uint8_t> bytes,
                                     std::vector<std::string>* notices = nullptr);
  [[nodiscard]] static Document read_file(const std::filesystem::path& path,
                                          std::vector<std::string>* notices = nullptr);
  // Palette-mode documents write type 1 (8-bit indexed, raw) with the document palette in
  // file order; everything else writes type 10 (32-bit RLE, top-left origin). A document-
  // alpha mask exports non-destructively into the alpha channel.
  [[nodiscard]] static std::vector<std::uint8_t> write(const Document& document);
  static void write_file(const Document& document, const std::filesystem::path& path);
};

}  // namespace patchy::tga
