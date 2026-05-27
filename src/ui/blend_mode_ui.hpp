#pragma once

#include "core/layer.hpp"

#include <QString>

class QComboBox;

namespace patchy::ui {

[[nodiscard]] QString blend_mode_name(BlendMode mode);
void add_blend_mode_items(QComboBox* combo);

}  // namespace patchy::ui
