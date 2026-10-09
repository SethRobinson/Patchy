#pragma once

#include "core/layer.hpp"
#include "core/pixel_buffer.hpp"

#include <QString>

#include <cstdint>

class QComboBox;

namespace patchy::ui {

// Which menu is being built. Layer offers every selectable mode; Filter drops
// the ones a recipe / Smart Filter blend step cannot execute (currently
// Dissolve), so the combo never offers a mode that would be rejected on save.
enum class BlendModeMenu : std::uint8_t { Layer, Filter };

[[nodiscard]] QString blend_mode_name(BlendMode mode);
// Fills `combo` with the modes in Photoshop's menu order and makes Left/Right
// step it like Up/Down (Qt handles only Up/Down on a combo box), both closed
// and with the list open.
void add_blend_mode_items(QComboBox* combo, BlendModeMenu menu = BlendModeMenu::Layer);
// Disables the layer blend modes a document of `depth` cannot use (32-bit documents,
// blend_mode_supported_at_depth) and re-enables the rest; Qt's stepping skips them.
void enable_blend_mode_items_for_depth(QComboBox* combo, BitDepth depth);

}  // namespace patchy::ui
