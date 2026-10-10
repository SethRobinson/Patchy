#pragma once

#include "core/document.hpp"

#include <filesystem>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace patchy::psd {

struct ReadOptions {
  bool preserve_unknown_blocks{true};
  bool prefer_flat_composite{false};
  bool retain_flat_composite{false};
  // When set, the reader appends plain-English import notes (smart-object handling,
  // etc.) for the UI's import-notices dialog.
  std::vector<std::string>* notices{nullptr};
  // A 16 or 32-bit file keeps its bit depth (docs/high-bit-depth.md) instead of
  // converting to 8 bits. Unset follows the deep-editing gate (deep_editing_enabled()).
  std::optional<bool> keep_bit_depth{};
};

struct WriteOptions {
  bool large_document{false};
  // Layered saves retain imported 8/16-bit CMYK/gray/Lab when all layer color content
  // is unchanged. False explicitly requests the RGB conversion path.
  bool preserve_source_color_mode{true};
};

class DocumentIo {
public:
  [[nodiscard]] static bool can_read(std::span<const std::uint8_t> bytes) noexcept;
  [[nodiscard]] static Document read(std::span<const std::uint8_t> bytes, ReadOptions options = {});
  [[nodiscard]] static Document read_file(const std::filesystem::path& path, ReadOptions options = {});

  [[nodiscard]] static std::vector<std::uint8_t> write_flat_rgb8(const Document& document,
                                                                 WriteOptions options = {});
  static void write_flat_rgb8_file(const Document& document, const std::filesystem::path& path,
                                   WriteOptions options = {});

  [[nodiscard]] static std::vector<std::uint8_t> write_layered_rgb8(const Document& document,
                                                                    WriteOptions options = {});
  static void write_layered_rgb8_file(const Document& document, const std::filesystem::path& path,
                                      WriteOptions options = {});
};

}  // namespace patchy::psd
