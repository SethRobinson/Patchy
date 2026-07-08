#pragma once

#include "core/document.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace patchy::pcx {

class DocumentIo {
public:
  [[nodiscard]] static bool can_read(std::span<const std::uint8_t> bytes) noexcept;
  // 8-bit indexed (EOF palette) and 24-bit (3-plane) RLE files. 1/2/4-bit files are
  // rejected with a clear message. Indexed sources populate Document::indexed_palette().
  [[nodiscard]] static Document read(std::span<const std::uint8_t> bytes,
                                     std::vector<std::string>* notices = nullptr);
  [[nodiscard]] static Document read_file(const std::filesystem::path& path,
                                          std::vector<std::string>* notices = nullptr);
  // Palette-mode documents write 8-bit indexed with the EOF palette; everything else
  // writes 24-bit 3-plane RLE. PCX has no alpha: the flatten composites transparency away.
  [[nodiscard]] static std::vector<std::uint8_t> write(const Document& document);
  static void write_file(const Document& document, const std::filesystem::path& path);
};

}  // namespace patchy::pcx
