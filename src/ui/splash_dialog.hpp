#pragma once

#include <functional>

class QWidget;

namespace patchy::ui {

struct UpdateInfo;

void show_startup_splash(QWidget* parent = nullptr);
void show_startup_splash(QWidget* parent, std::function<void(const UpdateInfo&)> update_available_callback);
void show_about_splash(QWidget* parent = nullptr);

}  // namespace patchy::ui
