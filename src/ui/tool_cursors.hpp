#pragma once

#include <QCursor>

namespace patchy::ui {

// The eyedropper cursor shown while sampling a colour: an eyedropper glyph (barrel,
// collar band, solid bulb) with a blue drip at the lower-left sampling tip, which
// is also the hotspot. Built once and cached. Shared by the canvas Alt/Eyedropper
// cursor and the colour picker's "Pick Screen Color" overlay so they never drift.
[[nodiscard]] QCursor eyedropper_cursor();

}  // namespace patchy::ui
