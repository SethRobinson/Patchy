#pragma once

#include <QString>

#include <functional>

class QWidget;

namespace patchy {
class PixelBuffer;
}

namespace patchy::ui {

class PatternLibrary;

// Opens the modal Pattern Manager. The returned value is the library storage id
// chosen with Use Pattern or a double-click; an empty value means the dialog was
// closed without choosing one. Returning the storage id lets callers distinguish
// a library pattern from a document-embedded pattern that happens to use the
// same Photoshop id. Library edits are applied immediately.
//
// open_as_image (optional) enables the "Open as Image" button: it receives the
// selected pattern's display name and full-resolution tile. The callback only
// REQUESTS the open — the caller may defer the actual document creation (the
// layer style dialog holds MainWindow's preview-dialog edit lock, under which
// add_document_session must not run).
[[nodiscard]] QString request_pattern_manager(
    QWidget* parent, PatternLibrary& library, const QString& initial_pattern_id = {},
    std::function<void(const QString& name, const PixelBuffer& tile)> open_as_image = {});

}  // namespace patchy::ui
