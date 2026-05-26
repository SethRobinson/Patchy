#include "ui/edit_conversions.hpp"

#include <algorithm>
#include <cstdint>

namespace photoslop::ui {

EditColor edit_color(QColor color) {
  return EditColor{static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()),
                   static_cast<std::uint8_t>(color.blue()), static_cast<std::uint8_t>(std::max(1, color.alpha()))};
}

}  // namespace photoslop::ui
