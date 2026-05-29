#pragma once

#include "core/document.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace patchy::bmp {

enum class BmpEncoding {
  Rgba32,
  Rgb24,
  Indexed8,
  Indexed4,
  Indexed2
};

enum class BmpPaletteMode {
  Exact,
  Quantize,
  PaletteFile
};

struct WriteOptions {
  BmpEncoding encoding{BmpEncoding::Rgba32};
  BmpPaletteMode palette_mode{BmpPaletteMode::Exact};
  bool use_imported_palette{true};
  std::filesystem::path palette_path{};
};

class DocumentIo {
public:
  [[nodiscard]] static bool can_read(std::span<const std::uint8_t> bytes) noexcept;
  [[nodiscard]] static Document read(std::span<const std::uint8_t> bytes);
  [[nodiscard]] static Document read_file(const std::filesystem::path& path);
  [[nodiscard]] static std::vector<std::uint8_t> write(const Document& document, WriteOptions options = {});
  static void write_file(const Document& document, const std::filesystem::path& path, WriteOptions options = {});
};

}  // namespace patchy::bmp
