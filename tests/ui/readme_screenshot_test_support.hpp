#pragma once

// Shared README-screenshot helpers moved verbatim from the retired
// tests/ui/readme_screenshot_tests.cpp monolith (used by more than one
// readme_screenshot_tests part TU). Moved, never copied.

#include <cstddef>
#include <memory>
#include <string>
#include <vector>
#include <QSize>
#include <QIcon>
#include <QImage>
#include <QListWidget>
#include <QPoint>
#include <QString>
#include <QWidget>
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

// One captured frame of a README animation and how long it shows.
struct ReadmeAnimationFrame {
  QImage image;
  int duration_ms{100};
};

// Writes test-artifacts/<name>.webp: an animated WebP that loops forever, each frame
// rounded like save_readme_shot, lossy at `quality` (static areas cost nothing: the
// encoder stores only what changed between frames). Every frame must share one size.
void save_readme_animation(const std::string& name, const std::vector<ReadmeAnimationFrame>& frames,
                           int quality = 90);

// The streaming form of save_readme_animation, for live captures: a frame shows until the
// next frame's time, and frames are encoded on a worker thread as they arrive, so the
// capture loop spends its time grabbing and memory holds only the encoder's backlog.
// finish() drains it and writes test-artifacts/<name>.webp.
class ReadmeAnimationWriter {
public:
  ReadmeAnimationWriter(std::string name, QSize size, int quality = 90);
  ~ReadmeAnimationWriter();
  ReadmeAnimationWriter(const ReadmeAnimationWriter&) = delete;
  ReadmeAnimationWriter& operator=(const ReadmeAnimationWriter&) = delete;
  // `time_ms` is when the frame was captured, counted from any fixed start.
  void add(const QImage& image, qint64 time_ms);
  [[nodiscard]] std::size_t frame_count() const;
  void finish(int last_frame_ms);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

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

// A natively framed dialog grabs without its title bar, which the desktop draws. Adds the
// dark caption Windows shows for Patchy's dialogs (app icon, title, close glyph) so the
// overlay reads as the window people see.
[[nodiscard]] QImage readme_captioned_dialog(QWidget& dialog, const QIcon& icon);

}  // namespace patchy::test::ui
