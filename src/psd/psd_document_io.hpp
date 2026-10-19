#pragma once

#include "core/document.hpp"

#include <filesystem>
#include <cstdint>
#include <functional>
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
  // Called on the writing thread as the save advances (the Saving dialog): (0, total, "")
  // before the composite, (i, total, name) as top-level layer i (an artboard, say) is
  // encoded, (total, total, "") before the bytes are assembled. Never changes the output.
  std::function<void(int done, int total, const std::string& label)> progress{};
};

class DocumentIo {
public:
  [[nodiscard]] static bool can_read(std::span<const std::uint8_t> bytes) noexcept;
  [[nodiscard]] static Document read(std::span<const std::uint8_t> bytes, ReadOptions options = {});
  [[nodiscard]] static Document read_file(const std::filesystem::path& path, ReadOptions options = {});
  // The ICC profile image resource a read file carried (empty without one).
  [[nodiscard]] static std::vector<std::uint8_t> source_icc_profile(const Document& document);
  // True when write_layered_rgb8 with `options` writes the imported file's own color mode
  // (unchanged CMYK, gray, Lab or duotone content, docs/psd-native-color.md) instead of RGB.
  [[nodiscard]] static bool layered_save_keeps_source_color_mode(const Document& document,
                                                                 WriteOptions options = {});

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
