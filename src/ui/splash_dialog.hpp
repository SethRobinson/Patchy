#pragma once

#include <functional>

class QWidget;

namespace patchy::ui {

struct UpdateInfo;

// Help > About. With `open_update`, an update check that finds a newer version shows an
// "Update..." button under the status line; clicking it closes the dialog and then calls
// `open_update` with that update, once the dialog is gone (the update prompt can quit
// Patchy to install; docs/auto-update.md). Without it the dialog only reports the version.
void show_about_splash(QWidget* parent = nullptr, std::function<void(const UpdateInfo&)> open_update = {});

}  // namespace patchy::ui
