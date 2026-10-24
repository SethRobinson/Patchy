#pragma once

#include "core/document.hpp"

#include <filesystem>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace patchy {
class CmykWorkingSpace;
}

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
  // is unchanged. False explicitly requests the RGB conversion path (CMYK Proof included).
  bool preserve_source_color_mode{true};
  // CMYK Proof (docs/cmyk.md): a layered save the source mode does not keep writes an 8 or
  // 16-bit CMYK file with its layers, separated with this space's intent and black point
  // compensation through an imported CMYK file's own profile, else this space's profile,
  // which it embeds. Layers it cannot write in CMYK make the whole file RGB
  // (layered_save_color_mode says why). Ignored without a profile.
  std::shared_ptr<const CmykWorkingSpace> cmyk_proof{};
  // Called on the writing thread as the save advances (the Saving dialog): (0, total, "")
  // before the composite, (i, total, name) as top-level layer i (an artboard, say) is
  // encoded, (total, total, "") before the bytes are assembled. Never changes the output.
  std::function<void(int done, int total, const std::string& label)> progress{};
};

// The color mode a layered save writes.
struct LayeredSaveColorMode {
  // The PSD header mode: 3 RGB, 4 CMYK, or an imported file's own mode.
  std::uint16_t mode{3};
  // With WriteOptions::cmyk_proof and an RGB file: why the file cannot be CMYK, an English
  // "QObject"-context source string for the UI to translate. Null otherwise.
  const char* rgb_reason{nullptr};
};

// The adjustment-layer blocks the reader parses into native adjustment settings (levl,
// curv, hue2, ... vibA). One list for the writer's regenerate rule and the open report's
// known-block check, so a newly modeled kind is never reported as unknown data.
[[nodiscard]] bool is_native_adjustment_block_key(std::string_view key) noexcept;

class DocumentIo {
public:
  [[nodiscard]] static bool can_read(std::span<const std::uint8_t> bytes) noexcept;
  [[nodiscard]] static Document read(std::span<const std::uint8_t> bytes, ReadOptions options = {});
  [[nodiscard]] static Document read_file(const std::filesystem::path& path, ReadOptions options = {});
  // The ICC profile image resource a read file carried (empty without one).
  [[nodiscard]] static std::vector<std::uint8_t> source_icc_profile(const Document& document);
  // Exactly the color mode write_layered_rgb8 with `options` writes: the imported file's own
  // (unchanged CMYK, gray, Lab or duotone content, docs/psd-native-color.md), CMYK for a CMYK
  // Proof save (docs/cmyk.md), else RGB.
  [[nodiscard]] static LayeredSaveColorMode layered_save_color_mode(const Document& document,
                                                                    WriteOptions options = {});
  // layered_save_color_mode is not RGB.
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
