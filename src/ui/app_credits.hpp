#pragma once

#include <QString>

namespace patchy::ui {

// Comma-joined rich-text GitHub links for the code contributors credited in the
// About dialog and the start panel; link_color is the anchor color for the site.
[[nodiscard]] QString code_contributors_link_html(const QString& link_color);

}  // namespace patchy::ui
