#pragma once

#include <QString>

class QWidget;

namespace patchy::ui {

class PatternLibrary;

// Opens the modal Pattern Manager. The returned value is the library storage id
// chosen with Use Pattern or a double-click; an empty value means the dialog was
// closed without choosing one. Returning the storage id lets callers distinguish
// a library pattern from a document-embedded pattern that happens to use the
// same Photoshop id. Library edits are applied immediately.
[[nodiscard]] QString request_pattern_manager(QWidget* parent, PatternLibrary& library,
                                              const QString& initial_pattern_id = {});

}  // namespace patchy::ui
