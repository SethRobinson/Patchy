#pragma once

#include <functional>

class QUrl;
class QWidget;

namespace patchy::ui {

struct UpdateInfo;

// Help > About. With `open_update`, an update check that finds a newer version shows an
// "Update..." button under the status line; clicking it closes the dialog and then calls
// `open_update` with that update, once the dialog is gone (the update prompt can quit
// Patchy to install; docs/auto-update.md). Without it the dialog only reports the version.
void show_about_splash(QWidget* parent = nullptr, std::function<void(const UpdateInfo&)> open_update = {});

// Tests replace the browser launch behind the dialog's RTsoft link (the mark above
// "rtsoft.com"); an empty function restores it.
void set_about_site_link_opener_for_testing(std::function<void(const QUrl&)> opener);

}  // namespace patchy::ui
