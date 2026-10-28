#pragma once

#include <QString>

class QWidget;

namespace patchy::ui {

class CustomShapeLibrary;

// Settings key of the Shapes Manager's view: true for the thumbnail grid (the default), false
// for the list with names.
inline constexpr char kCustomShapeManagerGridViewKey[] = "tools/customShapeManagerGridView";

// Opens the modal Shapes Manager (Edit > Presets > Shapes Manager...),
// the shape twin of the Pattern Manager: browse the library the options bar
// Shape picker lists, rename, move into folders, duplicate, delete, import SVG
// files, and restore the built-in shapes. Library edits apply at once; the
// picker follows the library's changed() signal. The returned value is the
// storage id chosen with Use Shape or a double-click; empty when the dialog
// was closed without choosing one.
[[nodiscard]] QString request_custom_shape_manager(QWidget* parent, CustomShapeLibrary& library,
                                                   const QString& initial_shape_id = {});

}  // namespace patchy::ui
