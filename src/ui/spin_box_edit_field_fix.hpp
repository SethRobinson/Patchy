#pragma once

class QAbstractSpinBox;
class QRect;

namespace patchy::ui {

// Qt 6.11.3 (the KDE Flatpak runtime 6.11 since October 31, 2026) and 6.12.0 lay out a style
// sheet spin box without buttons with a 1 px edit field at x = 0: QStyleSheetStyle cuts the field
// at buttons it no longer has, empty rectangles at the origin. Every Patchy field is such a box
// (a border from the window style sheet, NoButtons), so its text vanished on Linux. The filter
// gives the line edit the field Qt 6.8 computes, the box minus its style sheet margins, borders
// and padding; on a Qt without the bug it never changes anything. See docs/linux-testing.md.

// The field `spin`'s style should have given its line edit, or an empty rectangle when the style's
// own field is not the broken one (any Qt without the bug, buttons, right-to-left).
QRect corrected_spin_box_edit_field(const QAbstractSpinBox& spin);

// Installs the filter on the application. Call after the QApplication and its style exist.
void install_spin_box_edit_field_fix();

}  // namespace patchy::ui
