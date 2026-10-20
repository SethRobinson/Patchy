#pragma once

#include <QCursor>

namespace patchy::ui {

// The eyedropper cursor shown while sampling a colour: an eyedropper glyph (barrel,
// collar band, solid bulb) with a blue drip at the lower-left sampling tip, which
// is also the hotspot. Built once and cached. Shared by the canvas Alt/Eyedropper
// cursor and the colour picker's "Pick Screen Color" overlay so they never drift.
[[nodiscard]] QCursor eyedropper_cursor();
// The Eyedropper tool's own cursor: the same glyph with a foreground/background
// swatch badge in the lower-right, the swatch a click sets drawn solid on top.
[[nodiscard]] QCursor eyedropper_target_cursor(bool background);

// The rotate hint: off the pending crop box (the straighten gesture) and over the Move
// and Free Transform rotate handle (GitHub issue 98). A curved double-headed arrow,
// hotspot at its center. Built once and cached.
[[nodiscard]] QCursor rotate_cursor();

// The rotate hint beside a transform or crop box corner (GitHub issue 176): a quarter arc
// bowed outward around the corner with an arrowhead at each end, one of four orientations.
// `corner_direction_degrees` is the corner's outward direction on screen (y down, clockwise;
// 45 = bottom right, 315 = top right); the nearest quarter wins. Hotspot at the arc's middle.
// Built once per orientation and cached.
[[nodiscard]] QCursor corner_rotate_cursor(double corner_direction_degrees);

// The Move tool's pointer where a press selects rather than drags (GitHub issue 170):
// an arrow with a small four-way move badge at its lower right, hotspot on the arrow
// tip. Over draggable content the tool shows the plain four-way move cursor instead.
// Built once and cached.
[[nodiscard]] QCursor move_pointer_cursor();

// The arrow tools' pointers (GitHub issue 248), drawn as Patchy's own art from the shared
// pointer arrow, hotspot on its tip. Path Select: the arrow solid dark with a light edge
// (it moves whole shapes). Direct Select: the arrow light with a dark edge and a small
// hollow anchor square at its lower right (it edits points). Built once and cached.
[[nodiscard]] QCursor path_select_cursor();
[[nodiscard]] QCursor direct_select_cursor();

// Free Transform gesture hints (GitHub issue 225): shown over a box handle or side while
// the held modifiers (or the Edit > Transform mode) turn a drag into that gesture, before
// it starts. The same arrow as move_pointer_cursor with a badge drawing the gesture's
// shape: a slanted parallelogram (skew), an irregular four-sided shape (distort), a
// trapezoid (perspective). Hotspot on the arrow tip. Built once and cached.
enum class TransformGestureCursor { Skew, Distort, Perspective };
[[nodiscard]] QCursor transform_gesture_cursor(TransformGestureCursor gesture);

}  // namespace patchy::ui
