#pragma once

#include <QString>

class QWidget;

namespace patchy::ui {

class StyleLibrary;

// Opens the modal Style Manager (the style twin of the Pattern Manager). The
// returned value is the library storage id chosen with Use Style or a
// double-click; an empty value means the dialog was closed without choosing
// one. Library edits are applied immediately.
[[nodiscard]] QString request_style_manager(QWidget* parent, StyleLibrary& library,
                                            const QString& initial_style_id = {});

}  // namespace patchy::ui
