#pragma once

#include <QString>

namespace patchy::ui {

// Comma-joined rich text for everyone credited in the About dialog, as
// "handle (N)": the GitHub handle linked to its profile and N the person's
// points (merged pull requests plus accepted issues), most points first;
// link_color is the anchor color for the site.
[[nodiscard]] QString contributors_link_html(const QString& link_color);

}  // namespace patchy::ui
