#pragma once

// Color-scheme-aware icons.
//
// The 98 hand-authored SVGs in src/ui/icons are drawn in a fixed ten-color
// vocabulary (icon_ink plus nine accents, see theme_palette.hpp). Rather than
// maintaining a second light-mode icon set, a QIconEngine substitutes those
// colors in the SVG *text* before rasterizing, at paint time. Substituting the
// text rather than the rendered pixels keeps gradients and antialiasing exact:
// tool-gradient.svg's <linearGradient> stops recolor like any other attribute.
//
// This is what makes a live scheme change cheap. In Qt 6 a QIcon holds no pixmap
// cache of its own and nothing outside the stock engines keys on
// QIcon::cacheKey(), so a QIcon handed to a QAction at startup renders in the new
// scheme on its next repaint. A scheme change must NOT call QAction::setIcon;
// nothing needs rebuilding.
//
// Two deliberate exclusions:
//   * default-colors.svg and swap-colors.svg draw the black and white paint
//     swatches. Those fills depict colors and must never be recolored, which is
//     why #ffffff and #111111 are absent from the substitution map. Their
//     outlines are ordinary icon ink and do recolor.
//   * The SVGs referenced from QSS by url() (checkmark, scroll-dither,
//     tool-flyout-corner, the four spin arrows) cannot go through a QIconEngine
//     at all: QStyleSheetStyle loads them as plain files. They get hand-authored
//     light variants under icons/light/, selected by themed_icon_url().
//
// ui_icon_color_map_covers_every_authored_color enforces both.

#include "ui/theme_palette.hpp"

#include <QByteArray>
#include <QIcon>
#include <QString>
#include <QStringList>

#include <span>
#include <utility>

namespace patchy::ui {

// ":/patchy/icons/<name>.svg" recolored for the active scheme.
[[nodiscard]] QByteArray themed_icon_svg(const QString& name);

// A QIcon backed by the recoloring engine. `name` is the resource stem, without
// the directory or the .svg suffix.
[[nodiscard]] QIcon themed_svg_icon(const QString& name);

// Resource path for a QSS url(): the icons/light/ variant when the active scheme
// is Light and that variant exists, otherwise the base path.
[[nodiscard]] QString themed_icon_url(const QString& name);

using IconColorRole = std::pair<QLatin1StringView, QColor ThemePalette::*>;

// Source hex (lowercase, with '#') to palette role. Exposed for the audit test.
[[nodiscard]] std::span<const IconColorRole> icon_color_roles();

// Icon stems whose colors are intentionally not substituted, and why. Exposed for
// the audit test so a new icon cannot quietly join the list.
[[nodiscard]] QStringList literal_color_icon_names();
[[nodiscard]] QStringList stylesheet_referenced_icon_names();

}  // namespace patchy::ui
