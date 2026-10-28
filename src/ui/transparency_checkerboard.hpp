#pragma once

#include <QColor>

namespace patchy::ui {

// The transparency checkerboard's two squares, for the canvas and every preview that shows
// a layer's alpha (Liquify, the dialog previews). Deliberately theme-exempt (the
// color-scheme section of docs/ui-conventions.md): it depicts alpha, not chrome, so it
// stays light in both schemes. The artboard label ink reads them as the backing where the
// composite is clear.
inline QColor checkerboard_dark_square() { return QColor(188, 188, 188); }
inline QColor checkerboard_light_square() { return QColor(236, 236, 236); }

}  // namespace patchy::ui
