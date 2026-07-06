#pragma once

#include "core/palette.hpp"
#include "core/pixel_buffer.hpp"

#include <optional>

class QWidget;

namespace patchy::ui {

struct PaletteConvertSettings {
  Palette palette;  // the resolved final palette
  PaletteDither dither{PaletteDither::None};
  int alpha_threshold{128};
};

// Modal "Convert to Indexed (Palette)" dialog: palette source (optimize to N
// colors, exact image colors, the document's attached palette, built-in presets,
// or a palette file), dither method, alpha threshold, and a debounced preview of
// the flattened document rendered through the chosen palette. Returns the
// resolved settings on OK, nullopt on Cancel; the caller applies them.
[[nodiscard]] std::optional<PaletteConvertSettings> request_palette_convert_settings(
    QWidget* parent, const PixelBuffer& flattened_rgb8, const std::optional<Palette>& current_palette);

}  // namespace patchy::ui
