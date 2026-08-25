#pragma once

#include "core/pixel_tools.hpp"
#include "ui/theme_palette.hpp"

#include <QColor>
#include <QIcon>
#include <QString>

#include <functional>

class QPainter;

namespace patchy::ui {

// Icons resolve their colors when painted, so a live color-scheme change needs no
// setIcon call anywhere; see icon_theme.hpp.
//
// A default-constructed (invalid) `accent` means "the theme's icon ink", which is
// what nearly every caller wants. Pass an explicit color only when it carries
// meaning independent of the scheme, such as a red destructive marker; those are
// never recolored and must stay legible on both light and dark chrome.
QIcon simple_icon(QString text, QColor accent = QColor());

// Same, but the accent is a palette role resolved at paint time, so the glyph
// follows the color scheme. Prefer this over passing theme().some_role directly:
// that would freeze the color at the moment the icon was built.
QIcon simple_icon(QString text, QColor ThemePalette::* accent_role);
QIcon patchy_app_icon();
QIcon window_chrome_icon(QString role);
QIcon canvas_anchor_icon(CanvasAnchor anchor);
// Straight arrow for the divide-photos "top edge points" picker; direction is
// 0 = up, 1 = right, 2 = down, 3 = left (the PhotoUpDirection enum values).
QIcon up_direction_arrow_icon(int direction);

// A one-off procedural mark: a glyph outside the shared icon vocabulary, drawn by
// the caller against an `authored_size` square area with the ink handed to it.
// Use this rather than painting into a QPixmap and wrapping it in a QIcon. A
// baked pixmap freezes the color of the scheme that happened to be active when
// the icon was built, which in Light left window buttons and dialog closers as
// near-white marks on near-white chrome.
QIcon themed_glyph_icon(QString name, qreal authored_size, QColor ThemePalette::* ink_role,
                        std::function<void(QPainter&, const QColor&)> glyph);

}  // namespace patchy::ui
