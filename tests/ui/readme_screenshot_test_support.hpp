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
