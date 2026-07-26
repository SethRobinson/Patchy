#pragma once

// Shared README-screenshot helpers moved verbatim from the retired
// tests/ui/readme_screenshot_tests.cpp monolith (used by more than one
// readme_screenshot_tests part TU). Moved, never copied.

#include <string>

#include <QImage>
#include <QListWidget>
#include <QPoint>
#include <QString>

#include "ui/main_window.hpp"

namespace patchy::test::ui {

// DWM rounds every window Patchy frames itself, but it does the clipping in the
// compositor (src/ui/window_effects.cpp, DWMWCP_ROUND), so a grabbed window is
// square. README shots are pictures of windows, so they carry the corners the
// desktop shows: 8 px, DWMWCP_ROUND's radius at 96 DPI. Keep this in step with
// the driver's Set-RoundedWindowCorners, which does the same for the
// script-driven scenes.
inline constexpr qreal kReadmeWindowCornerRadius = 8.0;

// Masks a grabbed top-level window to the rounded corners DWM would clip, leaving
// the corner pixels transparent so the shot reads as a window on any page color.
[[nodiscard]] QImage rounded_readme_window_image(const QImage& window_image);

void show_readme_shot_window(patchy::ui::MainWindow& window);

void close_untitled_start_tab(patchy::ui::MainWindow& window);

void save_readme_shot(const std::string& name, const QImage& image);

// Scene setup leaves transient status messages ("Folder expanded", tool names);
// restore the idle text so every shot reads the same.
void reset_readme_status_bar(patchy::ui::MainWindow& window);

// Draws a grabbed popup/dialog onto a grabbed main window with a soft shadow,
// approximating how the floating window looks over the app on screen (each
// top-level widget grabs separately, so the composite is assembled by hand).
void draw_readme_overlay(QImage& base, const QImage& overlay, QPoint position);

// PSD group rows keep the expansion state saved in the file, so a child row
// only exists in the list widget after its folder row is expanded.
void expand_layer_folder_row(QListWidget& layer_list, const QString& folder_name);

}  // namespace patchy::test::ui
