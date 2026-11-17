#pragma once

#include <QString>

class QWidget;

namespace patchy::ui {

class ContourLibrary;

// Opens the modal Contour Manager (GitHub issue 726 follow-up; Edit > Presets
// and Preferences > Presets). It lists the built-in contours first, marked
// Built-in and locked (they can be duplicated, never renamed, edited or
// deleted), then the user's saved contours from `library`, the list the Layer
// Style contour pickers show. A saved contour can be renamed (the Name field),
// have its curve edited (Edit Curve, the contour editor), be duplicated and
// deleted; Load Contours and Save Contours read and write Photoshop .shc files
// as the pickers' Library menu does. Every change is saved to the library at
// once. `initial_storage_id` preselects a saved contour.
void request_contour_manager(QWidget* parent, ContourLibrary& library, const QString& initial_storage_id = {});

}  // namespace patchy::ui
