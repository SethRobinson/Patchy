#pragma once

#include "color/color_management.hpp"

#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

class QWidget;

namespace patchy::ui {

// Edit > Convert to Color Profile (docs/rgb-profiles.md): the destination RGB profile and the
// conversion options.
struct ConvertProfileChoice {
  QString destination_path;               // empty = sRGB
  std::vector<std::uint8_t> destination;  // the profile bytes; empty = sRGB
  RenderingIntent intent{RenderingIntent::RelativeColorimetric};
  bool black_point_compensation{true};
};

// Shows the document's current profile and asks for a destination: sRGB, the RGB profiles
// installed on this computer, or a profile file. Remembers the choices under color/convert*.
[[nodiscard]] std::optional<ConvertProfileChoice> request_convert_to_profile(QWidget* parent,
                                                                             const QString& current_profile_name);

}  // namespace patchy::ui
