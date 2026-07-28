#pragma once

#include "core/layer.hpp"

#include <QString>

#include <cstdint>

class QComboBox;

namespace patchy::ui {

// Which menu is being built. Layer offers every selectable mode; Filter drops
// the ones a recipe / Smart Filter blend step cannot execute (currently
// Dissolve), so the combo never offers a mode that would be rejected on save.
enum class BlendModeMenu : std::uint8_t { Layer, Filter };

[[nodiscard]] QString blend_mode_name(BlendMode mode);
void add_blend_mode_items(QComboBox* combo, BlendModeMenu menu = BlendModeMenu::Layer);

}  // namespace patchy::ui
