#pragma once

#include <QString>

class QWidget;

namespace patchy::ui {

class CustomShapeLibrary;

// Opens the modal Custom Shapes manager (Edit > Presets > Custom Shapes...),
// the shape twin of the Pattern Manager: browse the library the options bar
// Shape picker lists, rename, move into folders, duplicate, delete, import SVG
// files, and restore the built-in shapes. Library edits apply at once; the
// picker follows the library's changed() signal. The returned value is the
// storage id chosen with Use Shape or a double-click; empty when the dialog
// was closed without choosing one.
[[nodiscard]] QString request_custom_shape_manager(QWidget* parent, CustomShapeLibrary& library,
                                                   const QString& initial_shape_id = {});

}  // namespace patchy::ui
