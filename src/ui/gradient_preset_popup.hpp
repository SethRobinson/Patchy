#pragma once

#include <QStringList>

#include <functional>

class QPushButton;

namespace patchy::ui {

class GradientLibrary;
struct GradientLibraryEntry;

// Anchored quick-picker popup (QFrame Qt::Popup, WA_DeleteOnClose) showing the
// gradient library as a folder tree of thumbnails. Child objectNames derive
// from the anchor: "<anchor>Popup", "<anchor>Tree", "<anchor>ManageButton";
// tests and QSS depend on them. use_gradient fires when a leaf is clicked;
// manage_gradients fires after the popup closes itself via the
// Manage Gradients... button, so a modal manager never coexists with the
// popup's grab.
void show_gradient_preset_popup(
    QPushButton *anchor, GradientLibrary &library,
    std::function<void(const GradientLibraryEntry &)> use_gradient,
    std::function<void()> manage_gradients);

// The library's storage ids in the order the popup lists them (each folder's
// entries and subfolders in first-appearance order, depth first), which is the
// order Previous Preset / Next Preset step through (GitHub issue 543).
[[nodiscard]] QStringList gradient_preset_popup_order(const GradientLibrary &library);

} // namespace patchy::ui
