#include <QStatusBar>
#include "ui/canvas_widget.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/main_window.hpp"
#include "ui/app_settings.hpp"
#include "test_harness.hpp"
#include "local_psd_fixtures.hpp"
#include "ui/qt_paths.hpp"
#include "ui/script_engine.hpp"
#include <QCoreApplication>
#include <QEvent>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QListWidget>
#include <QMenu>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QTest>
#include <QToolButton>
#include <algorithm>
#include <atomic>
#include <array>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

namespace {

using namespace patchy::test::ui;

void ui_gradient_and_magic_wand_render_visually() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_primary_color(QColor(255, 0, 0));
  canvas->set_secondary_color(QColor(0, 0, 255));
  canvas->set_tool(patchy::ui::CanvasTool::Gradient);
  send_mouse(*canvas, QEvent::MouseButtonPress, QPoint(60, 140), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, QPoint(280, 140), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();

  const auto live_preview = canvas->grab().toImage().convertToFormat(QImage::Format_RGB32);
  const auto live_fill_pixel = QColor(live_preview.pixel(170, 80));
  CHECK(live_fill_pixel.green() < 80);
  CHECK(live_fill_pixel.red() > 80);
  CHECK(live_fill_pixel.blue() > 80);

  send_mouse(*canvas, QEvent::MouseButtonRelease, QPoint(280, 140), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  const auto gradient_image = canvas->grab().toImage().convertToFormat(QImage::Format_RGB32);
  CHECK(color_close(QColor(gradient_image.pixel(62, 140)), QColor(255, 0, 0), 35));
  CHECK(color_close(QColor(gradient_image.pixel(278, 140)), QColor(0, 0, 255), 35));
  save_widget_artifact("ui_gradient_tool", *canvas);

  canvas->set_tool(patchy::ui::CanvasTool::MagicWand);
  send_mouse(*canvas, QEvent::MouseButtonPress, QPoint(62, 140), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, QPoint(62, 140), Qt::LeftButton, Qt::NoButton);
  CHECK(canvas->selected_document_rect().has_value());
  save_widget_artifact("ui_magic_wand_selection", *canvas);
}

void ui_radial_gradient_tool_renders_custom_transparency() {
  SettingsValueRestorer saved_gradient_method(QStringLiteral("tools/gradientMethod"));
  SettingsValueRestorer saved_gradient_reverse(QStringLiteral("tools/gradientReverse"));
  SettingsValueRestorer saved_gradient_opacity(QStringLiteral("tools/gradientOpacity"));
  SettingsValueRestorer saved_gradient_use_custom(QStringLiteral("tools/gradientUseCustomStops"));
  SettingsValueRestorer saved_gradient_stops(QStringLiteral("tools/gradientStops"));
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_tool(patchy::ui::CanvasTool::Gradient);
  canvas->set_gradient_method(patchy::GradientMethod::Radial);
  canvas->set_gradient_opacity(100);
  canvas->set_gradient_reverse(false);
  canvas->set_gradient_stops(std::vector<patchy::GradientStop>{
      patchy::GradientStop{0.0F, patchy::EditColor{255, 0, 0, 255}},
      patchy::GradientStop{1.0F, patchy::EditColor{0, 0, 255, 0}},
  });
  drag(*canvas, QPoint(160, 140), QPoint(220, 140));
  QApplication::processEvents();

  const auto image = canvas->grab().toImage().convertToFormat(QImage::Format_RGB32);
  CHECK(color_close(QColor(image.pixel(160, 140)), QColor(255, 0, 0), 35));
  CHECK(color_close(QColor(image.pixel(220, 140)), Qt::white, 35));
  save_widget_artifact("ui_radial_gradient_transparency", *canvas);
}

void ui_magic_wand_contiguous_and_sample_all_layers_options_work() {
  patchy::Document document(320, 220, patchy::PixelFormat::rgba8());
  auto lower_pixels = solid_pixels(320, 220, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(lower_pixels, QRect(210, 42, 38, 38), QColor(220, 30, 55, 255));
  document.add_pixel_layer("Lower Match", std::move(lower_pixels));

  auto active_pixels = solid_pixels(320, 220, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(active_pixels, QRect(30, 42, 38, 38), QColor(220, 30, 55, 255));
  fill_pixel_rect(active_pixels, QRect(110, 42, 38, 38), QColor(220, 30, 55, 255));
  document.add_pixel_layer("Active Matches", std::move(active_pixels));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Magic Wand Options"));
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::MagicWand);
  canvas->set_wand_tolerance(0);
  canvas->set_selection_feather_radius(0);

  const auto click_wand = [canvas](QPoint document_point) {
    const auto widget_point = canvas->widget_position_for_document_point(document_point);
    send_mouse(*canvas, QEvent::MouseButtonPress, widget_point, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
  };

  canvas->set_wand_contiguous(true);
  canvas->set_wand_sample_all_layers(false);
  click_wand(QPoint(40, 52));
  CHECK(canvas->selected_document_region().contains(QPoint(40, 52)));
  CHECK(!canvas->selected_document_region().contains(QPoint(120, 52)));
  CHECK(!canvas->selected_document_region().contains(QPoint(220, 52)));

  canvas->clear_selection();
  canvas->set_wand_contiguous(false);
  canvas->set_wand_sample_all_layers(false);
  click_wand(QPoint(40, 52));
  CHECK(canvas->selected_document_region().contains(QPoint(40, 52)));
  CHECK(canvas->selected_document_region().contains(QPoint(120, 52)));
  CHECK(!canvas->selected_document_region().contains(QPoint(220, 52)));

  canvas->clear_selection();
  canvas->set_wand_contiguous(false);
  canvas->set_wand_sample_all_layers(true);
  click_wand(QPoint(40, 52));
  CHECK(canvas->selected_document_region().contains(QPoint(40, 52)));
  CHECK(canvas->selected_document_region().contains(QPoint(120, 52)));
  CHECK(canvas->selected_document_region().contains(QPoint(220, 52)));
  save_widget_artifact("ui_magic_wand_options", *canvas);
}

// GitHub issue 435: the Magic Wand bar's Sample combo is the Pick tool's Size (one shared
// setting), and the wand's seed averages that square. On noise, Point Sample selects exactly
// the pixels near the clicked one (the pre-435 result); 5 by 5 selects those near the mean.
void ui_magic_wand_sample_size_averages_the_seed() {
  SettingsValueRestorer saved_sample_size(QStringLiteral("tools/eyedropperSampleSize"));
  constexpr int width = 120;
  constexpr int height = 90;
  auto pixels = solid_pixels(width, height, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 255));
  std::uint64_t state = 435U;
  const auto next = [&state] {
    std::uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31U);
  };
  const QPoint click(60, 45);
  std::vector<QColor> colors;
  colors.reserve(static_cast<std::size_t>(width) * height);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const bool speck = QPoint(x, y) == click;
      const auto noise = [&next] { return 104 + static_cast<int>(next() % 49U); };
      const int red = noise();
      const int green = noise();
      const int blue = noise();
      colors.push_back(speck ? QColor(160, 96, 150, 255) : QColor(red, green, blue, 255));
      fill_pixel_rect(pixels, QRect(x, y, 1, 1), colors.back());
    }
  }
  patchy::Document document(width, height, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Noise", std::move(pixels));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Wand Sample Size"));
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  require_action(window, "toolMagicWandAction")->trigger();
  canvas->set_wand_tolerance(12);
  canvas->set_wand_contiguous(false);
  canvas->set_wand_sample_all_layers(false);
  canvas->set_selection_feather_radius(0);
  canvas->set_selection_antialias(false);
  auto* wand_combo = window.findChild<QComboBox*>(QStringLiteral("wandSampleSizeCombo"));
  auto* pick_combo = window.findChild<QComboBox*>(QStringLiteral("eyedropperSampleSizeCombo"));
  CHECK(wand_combo != nullptr && pick_combo != nullptr);
  if (wand_combo == nullptr || pick_combo == nullptr) {
    return;
  }

  const auto color_at = [&colors](int x, int y) {
    return colors[static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)];
  };
  const auto expected_count = [&](const QColor& seed) {
    int count = 0;
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        const auto color = color_at(x, y);
        const int dr = color.red() - seed.red();
        const int dg = color.green() - seed.green();
        const int db = color.blue() - seed.blue();
        count += dr * dr + dg * dg + db * db <= 4 * 12 * 12 ? 1 : 0;
      }
    }
    return count;
  };
  const auto selected_count = [canvas] {
    int count = 0;
    for (const auto& rect : canvas->selected_document_region()) {
      count += rect.width() * rect.height();
    }
    return count;
  };
  const auto click_wand = [canvas, click] {
    canvas->clear_selection();
    const auto widget_point = canvas->widget_position_for_document_point(click);
    send_mouse(*canvas, QEvent::MouseButtonPress, widget_point, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
  };

  // Point Sample: the seed is the clicked speck.
  wand_combo->setCurrentIndex(wand_combo->findData(1));
  CHECK(canvas->eyedropper_sample_size() == 1);
  click_wand();
  const int point_count = selected_count();
  CHECK(point_count == expected_count(QColor(160, 96, 150)));
  CHECK(canvas->selected_document_region().contains(click));

  // 5 by 5 Average, chosen on the wand's bar: the Pick bar shows it too.
  wand_combo->setCurrentIndex(wand_combo->findData(5));
  CHECK(canvas->eyedropper_sample_size() == 5);
  CHECK(pick_combo->currentData().toInt() == 5);
  click_wand();
  int sums[3] = {0, 0, 0};
  for (int y = click.y() - 2; y <= click.y() + 2; ++y) {
    for (int x = click.x() - 2; x <= click.x() + 2; ++x) {
      const auto color = color_at(x, y);
      sums[0] += color.red();
      sums[1] += color.green();
      sums[2] += color.blue();
    }
  }
  const QColor mean((sums[0] + 12) / 25, (sums[1] + 12) / 25, (sums[2] + 12) / 25);
  const int averaged_count = selected_count();
  std::printf("  wand: Point Sample selects %d px, 5 by 5 Average %d px of %d\n", point_count, averaged_count,
              width * height);
  CHECK(averaged_count == expected_count(mean));
  CHECK(averaged_count > point_count * 10);
  save_widget_artifact("ui_magic_wand_sample_size_averages_the_seed", *canvas);

  // The Pick bar's combo drives the wand's too.
  pick_combo->setCurrentIndex(pick_combo->findData(3));
  CHECK(wand_combo->currentData().toInt() == 3);
  CHECK(canvas->eyedropper_sample_size() == 3);
  wand_combo->setCurrentIndex(wand_combo->findData(1));
}

void ui_quick_mask_feathered_round_trip_is_exact_and_temporary() {
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  document.add_pixel_layer(
      "Pixels", solid_pixels(96, 72, patchy::PixelFormat::rgba8(), QColor(40, 150, 220)));
  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Quick Mask Round Trip"));
  show_window(window);
  auto* canvas = require_canvas(window);

  patchy::PixelBuffer feathered(96, 72, patchy::PixelFormat::gray8());
  feathered.clear(0);
  for (int y = 16; y < 56; ++y) {
    for (int x = 20; x < 76; ++x) {
      const auto edge_distance = std::min({x - 20, 75 - x, y - 16, 55 - y});
      *feathered.pixel(x, y) = static_cast<std::uint8_t>(
          std::clamp(edge_distance * 32, 0, 255));
    }
  }
  canvas->replace_selection_from_grayscale(
      feathered, QStringLiteral("Feathered Quick Mask fixture"));
  CHECK(canvas->selection_has_partial_alpha());

  std::vector<std::uint8_t> alpha_before;
  alpha_before.reserve(96U * 72U);
  for (int y = 0; y < 72; ++y) {
    for (int x = 0; x < 96; ++x) {
      alpha_before.push_back(canvas->selection_alpha_at(QPoint(x, y)));
    }
  }
  const auto undo_before =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  CHECK(!patchy::ui::MainWindowTestAccess::active_session_is_modified(window));

  auto* quick_mask = require_hotkey_action(window, QStringLiteral("select.quick_mask"));
  CHECK(quick_mask == require_action(window, "selectQuickMaskAction"));
  CHECK(quick_mask->isCheckable());
  CHECK(quick_mask->shortcut() == QKeySequence(Qt::Key_Q));
  quick_mask->trigger();
  QApplication::processEvents();
  CHECK(canvas->quick_mask_active());
  CHECK(quick_mask->isChecked());
  quick_mask->trigger();
  QApplication::processEvents();
  CHECK(!canvas->quick_mask_active());
  CHECK(!quick_mask->isChecked());

  std::size_t index = 0;
  for (int y = 0; y < 72; ++y) {
    for (int x = 0; x < 96; ++x) {
      CHECK(canvas->selection_alpha_at(QPoint(x, y)) == alpha_before[index++]);
    }
  }
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before);
  CHECK(!patchy::ui::MainWindowTestAccess::active_session_is_modified(window));
}

void ui_quick_mask_overlay_channels_and_tab_state_are_temporary() {
  const auto make_document = [](QColor color) {
    patchy::Document document(80, 60, patchy::PixelFormat::rgba8());
    document.add_pixel_layer(
        "Pixels", solid_pixels(80, 60, patchy::PixelFormat::rgba8(), color));
    return document;
  };

  patchy::ui::MainWindow window;
  window.add_document_session(make_document(QColor(40, 150, 220)),
                              QStringLiteral("Quick Mask First"));
  auto* first_canvas = require_canvas(window);
  const QColor first_primary(31, 92, 201);
  const QColor first_secondary(241, 188, 63);
  first_canvas->set_primary_color(first_primary);
  first_canvas->set_secondary_color(first_secondary);

  window.add_document_session(make_document(QColor(80, 130, 190)),
                              QStringLiteral("Quick Mask Second"));
  auto* second_canvas = require_canvas(window);
  const QColor second_primary(183, 44, 96);
  const QColor second_secondary(24, 202, 125);
  second_canvas->set_primary_color(second_primary);
  second_canvas->set_secondary_color(second_secondary);
  show_window(window);

  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  auto* channels = window.findChild<QListWidget*>(QStringLiteral("channelList"));
  auto* channels_dock = window.findChild<QWidget*>(QStringLiteral("channelsDock"));
  auto* quick_mask_button =
      window.findChild<QToolButton*>(QStringLiteral("quickMaskButton"));
  auto* quick_mask = require_hotkey_action(window, QStringLiteral("select.quick_mask"));
  CHECK(tabs != nullptr);
  CHECK(channels != nullptr);
  CHECK(channels_dock != nullptr);
  CHECK(quick_mask_button != nullptr);
  CHECK(quick_mask_button->defaultAction() == quick_mask);
  CHECK(quick_mask_button->isCheckable());

  // The foreground/background pair is application-wide (GitHub issue 339): the first
  // document takes the pair last set in the second one.
  tabs->setCurrentIndex(tabs->indexOf(first_canvas));
  QApplication::processEvents();
  CHECK(require_canvas(window) == first_canvas);
  CHECK(first_canvas->primary_color() == second_primary);
  CHECK(first_canvas->secondary_color() == second_secondary);
  quick_mask->trigger();
  QApplication::processEvents();
  CHECK(first_canvas->quick_mask_active());
  CHECK(first_canvas->primary_color() == QColor(Qt::black));
  CHECK(first_canvas->secondary_color() == QColor(Qt::white));
  CHECK(channels->count() == 5);
  CHECK(channels->currentRow() == 4);
  auto* row = channels->item(4);
  CHECK(row != nullptr);
  CHECK(row->text() == QStringLiteral("Quick Mask"));
  CHECK(row->flags().testFlag(Qt::ItemIsEnabled));
  CHECK(row->flags().testFlag(Qt::ItemIsSelectable));
  CHECK(!row->flags().testFlag(Qt::ItemIsDragEnabled));
  CHECK(!row->flags().testFlag(Qt::ItemIsDropEnabled));
  CHECK(!row->flags().testFlag(Qt::ItemIsUserCheckable));
  CHECK(!row->toolTip().isEmpty());
  CHECK(std::all_of(first_canvas->quick_mask_pixels().data().begin(),
                    first_canvas->quick_mask_pixels().data().end(),
                    [](std::uint8_t value) { return value == 0; }));

  const auto overlaid = canvas_pixel_center(*first_canvas, QPoint(40, 30));
  CHECK(overlaid.red() > 135);
  CHECK(overlaid.green() >= 70 && overlaid.green() <= 82);
  CHECK(overlaid.blue() >= 105 && overlaid.blue() <= 116);
  patchy::ui::MainWindowTestAccess::show_panel(window, QStringLiteral("channelsDock"));
  process_events_for(100);
  CHECK(channels->isVisible());
  save_widget_artifact("ui_quick_mask_overlay_channels", window);

  tabs->setCurrentIndex(tabs->indexOf(second_canvas));
  QApplication::processEvents();
  CHECK(require_canvas(window) == second_canvas);
  CHECK(!quick_mask->isChecked());
  CHECK(second_canvas->primary_color() == second_primary);
  CHECK(second_canvas->secondary_color() == second_secondary);
  CHECK(channels->count() == 4);

  tabs->setCurrentIndex(tabs->indexOf(first_canvas));
  QApplication::processEvents();
  CHECK(require_canvas(window) == first_canvas);
  CHECK(quick_mask->isChecked());
  CHECK(channels->count() == 5);
  quick_mask->trigger();
  QApplication::processEvents();
  CHECK(!first_canvas->quick_mask_active());
  CHECK(first_canvas->primary_color() == second_primary);
  CHECK(first_canvas->secondary_color() == second_secondary);
  CHECK(channels->count() == 4);

  tabs->setCurrentIndex(tabs->indexOf(second_canvas));
  QApplication::processEvents();
  CHECK(second_canvas->primary_color() == second_primary);
  CHECK(second_canvas->secondary_color() == second_secondary);
}

void ui_quick_mask_brush_gestures_edit_outside_selection_with_selection_history() {
  patchy::Document document(120, 90, patchy::PixelFormat::rgba8());
  document.add_pixel_layer(
      "Pixels", solid_pixels(120, 90, patchy::PixelFormat::rgba8(), QColor(55, 125, 205)));
  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Quick Mask Brush"));
  show_window(window);
  auto* canvas = require_canvas(window);

  patchy::PixelBuffer initial_selection(120, 90, patchy::PixelFormat::gray8());
  initial_selection.clear(0);
  for (int y = 28; y <= 62; ++y) {
    auto row = initial_selection.row(y);
    std::fill(row.begin() + 44, row.begin() + 78, std::uint8_t{255});
  }
  canvas->replace_selection_from_grayscale(initial_selection,
                                           QStringLiteral("Quick Mask initial selection"));
  const auto& active_document =
      std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  const auto layer_id = active_document.active_layer_id();
  CHECK(layer_id.has_value());
  const auto* layer = active_document.find_layer(*layer_id);
  CHECK(layer != nullptr);
  const auto layer_pixels_before = layer->pixels();
  const auto undo_before =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);

  require_hotkey_action(window, QStringLiteral("select.quick_mask"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->quick_mask_active());
  canvas->set_tool(patchy::ui::CanvasTool::Brush);
  canvas->set_brush_size(10);
  canvas->set_brush_opacity(100);
  canvas->set_brush_softness(0);

  const auto paint_at = [&](QPoint point, QColor color) {
    canvas->set_primary_color(color);
    const auto widget_point = canvas->widget_position_for_document_point(point);
    send_mouse(*canvas, QEvent::MouseButtonPress, widget_point, Qt::LeftButton,
               Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point,
               Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
  };

  const QPoint white_outside(16, 18);
  paint_at(white_outside, Qt::white);
  CHECK(*canvas->quick_mask_pixels().pixel(white_outside.x(), white_outside.y()) ==
        255);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before + 1U);

  const QPoint black_inside(58, 44);
  paint_at(black_inside, Qt::black);
  CHECK(*canvas->quick_mask_pixels().pixel(black_inside.x(), black_inside.y()) ==
        0);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before + 2U);

  const QPoint gray_outside(102, 70);
  paint_at(gray_outside, QColor(128, 128, 128));
  CHECK(*canvas->quick_mask_pixels().pixel(gray_outside.x(), gray_outside.y()) ==
        128);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before + 3U);

  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->quick_mask_active());
  CHECK(*canvas->quick_mask_pixels().pixel(gray_outside.x(), gray_outside.y()) ==
        0);
  CHECK(*canvas->quick_mask_pixels().pixel(white_outside.x(), white_outside.y()) ==
        255);
  CHECK(*canvas->quick_mask_pixels().pixel(black_inside.x(), black_inside.y()) ==
        0);
  require_hotkey_action(window, QStringLiteral("edit.redo"))->trigger();
  QApplication::processEvents();
  CHECK(*canvas->quick_mask_pixels().pixel(gray_outside.x(), gray_outside.y()) ==
        128);
  const auto undo_after_gestures =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  require_hotkey_action(window, QStringLiteral("select.quick_mask"))->trigger();
  QApplication::processEvents();
  CHECK(!canvas->quick_mask_active());
  CHECK(canvas->selection_alpha_at(white_outside) == 255);
  CHECK(canvas->selection_alpha_at(black_inside) == 0);
  CHECK(canvas->selection_alpha_at(gray_outside) == 128);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_after_gestures);
  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->selection_alpha_at(gray_outside) == 0);
  CHECK(canvas->selection_alpha_at(white_outside) == 255);
  CHECK(canvas->selection_alpha_at(black_inside) == 0);
  require_hotkey_action(window, QStringLiteral("edit.redo"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->selection_alpha_at(gray_outside) == 128);

  const auto& after_document =
      std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  const auto* after_layer = after_document.find_layer(*layer_id);
  CHECK(after_layer != nullptr);
  CHECK(patchy::ui::pixel_buffers_equal(after_layer->pixels(), layer_pixels_before));
  CHECK(!patchy::ui::MainWindowTestAccess::active_session_is_modified(window));
}

void ui_quick_mask_fill_clear_invert_and_blocked_tools_are_selection_only() {
  patchy::Document document(84, 64, patchy::PixelFormat::rgba8());
  document.add_pixel_layer(
      "Pixels", solid_pixels(84, 64, patchy::PixelFormat::rgba8(), QColor(70, 135, 210)));
  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Quick Mask Commands"));
  show_window(window);
  auto* canvas = require_canvas(window);
  const auto& initial_document =
      std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  const auto layer_id = initial_document.active_layer_id();
  CHECK(layer_id.has_value());
  const auto* initial_layer = initial_document.find_layer(*layer_id);
  CHECK(initial_layer != nullptr);
  const auto layer_pixels_before = initial_layer->pixels();
  const auto undo_before =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);

  require_hotkey_action(window, QStringLiteral("select.quick_mask"))->trigger();
  QApplication::processEvents();
  canvas->set_primary_color(QColor(128, 128, 128));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  CHECK(std::all_of(canvas->quick_mask_pixels().data().begin(),
                    canvas->quick_mask_pixels().data().end(),
                    [](std::uint8_t value) { return value == 128; }));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before + 1U);

  require_action(window, "selectInverseAction")->trigger();
  QApplication::processEvents();
  CHECK(std::all_of(canvas->quick_mask_pixels().data().begin(),
                    canvas->quick_mask_pixels().data().end(),
                    [](std::uint8_t value) { return value == 127; }));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before + 2U);

  require_action(window, "layerClearAction")->trigger();
  QApplication::processEvents();
  CHECK(std::all_of(canvas->quick_mask_pixels().data().begin(),
                    canvas->quick_mask_pixels().data().end(),
                    [](std::uint8_t value) { return value == 0; }));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before + 3U);
  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  CHECK(std::all_of(canvas->quick_mask_pixels().data().begin(),
                    canvas->quick_mask_pixels().data().end(),
                    [](std::uint8_t value) { return value == 127; }));

  CHECK(!require_hotkey_action(window, QStringLiteral("tools.clone"))->isEnabled());
  CHECK(!require_hotkey_action(window, QStringLiteral("tools.smudge"))->isEnabled());
  CHECK(!require_hotkey_action(window, QStringLiteral("tools.type"))->isEnabled());
  auto* filter = require_action(window, "filterAction_patchy_filters_edge_detect");
  CHECK(!filter->isEnabled());
  const auto mask_before_blocked_tools = canvas->quick_mask_pixels();
  const auto undo_before_blocked_tools =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  const auto click_with_tool = [&](patchy::ui::CanvasTool tool, QPoint point) {
    canvas->set_tool(tool);
    const auto widget_point = canvas->widget_position_for_document_point(point);
    send_mouse(*canvas, QEvent::MouseButtonPress, widget_point, Qt::LeftButton,
               Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point,
               Qt::LeftButton, Qt::NoButton);
  };
  click_with_tool(patchy::ui::CanvasTool::Clone, QPoint(18, 18));
  click_with_tool(patchy::ui::CanvasTool::Smudge, QPoint(34, 28));
  click_with_tool(patchy::ui::CanvasTool::Text, QPoint(52, 42));
  filter->trigger();
  QApplication::processEvents();
  CHECK(patchy::ui::pixel_buffers_equal(canvas->quick_mask_pixels(),
                                        mask_before_blocked_tools));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before_blocked_tools);

  const auto& final_document =
      std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  const auto* final_layer = final_document.find_layer(*layer_id);
  CHECK(final_layer != nullptr);
  CHECK(patchy::ui::pixel_buffers_equal(final_layer->pixels(), layer_pixels_before));
  CHECK(!patchy::ui::MainWindowTestAccess::active_session_is_modified(window));
}

void ui_quick_select_stroke_selects_object_and_is_undoable() {
  patchy::Document document(320, 220, patchy::PixelFormat::rgba8());
  auto pixels = solid_pixels(320, 220, patchy::PixelFormat::rgba8(), QColor(Qt::white));
  fill_pixel_rect(pixels, QRect(80, 50, 120, 90), QColor(200, 30, 20, 255));
  document.add_pixel_layer("Art", std::move(pixels));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Quick Select"));
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::QuickSelect);
  canvas->set_quick_select_size(24);
  canvas->set_selection_feather_radius(0);
  auto* undo_action = require_action_by_text(window, QStringLiteral("Undo"));

  const auto widget_point = [canvas](QPoint document_point) {
    return canvas->widget_position_for_document_point(document_point);
  };
  send_mouse(*canvas, QEvent::MouseButtonPress, widget_point(QPoint(105, 95)), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, widget_point(QPoint(140, 95)), Qt::NoButton, Qt::LeftButton);
  // No live classification during the drag: the selection must only appear on release (this
  // also pins the US-8050498 patent-avoidance behaviour, see finish_quick_select_stroke).
  CHECK(!canvas->has_selection());
  send_mouse(*canvas, QEvent::MouseMove, widget_point(QPoint(175, 95)), Qt::NoButton, Qt::LeftButton);
  CHECK(!canvas->has_selection());
  send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point(QPoint(175, 95)), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(canvas->has_selection());
  const auto region = canvas->selected_document_region();
  CHECK(region.contains(QPoint(90, 60)));
  CHECK(region.contains(QPoint(190, 130)));
  CHECK(!region.contains(QPoint(40, 30)));
  CHECK(!region.contains(QPoint(260, 180)));
  // A stroke in New mode leaves the tool in Add (Photoshop behaviour).
  CHECK(canvas->selection_mode() == patchy::ui::CanvasWidget::SelectionMode::Add);
  save_widget_artifact("ui_quick_select_selection", *canvas);

  undo_action->trigger();
  QApplication::processEvents();
  CHECK(!canvas->has_selection());
}

void ui_magnetic_lasso_traces_edge_and_commits_selection() {
  patchy::Document document(320, 280, patchy::PixelFormat::rgba8());
  auto pixels = solid_pixels(320, 280, patchy::PixelFormat::rgba8(), QColor(Qt::white));
  fill_pixel_rect(pixels, QRect(60, 60, 160, 160), QColor(20, 20, 20, 255));
  document.add_pixel_layer("Art", std::move(pixels));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Magnetic Lasso"));
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::MagneticLasso);
  canvas->set_selection_feather_radius(0);
  canvas->set_magnetic_lasso_width(10);
  canvas->set_magnetic_lasso_edge_contrast(10);
  canvas->set_magnetic_lasso_frequency(57);
  auto* undo_action = require_action_by_text(window, QStringLiteral("Undo"));

  const auto widget_point = [canvas](QPoint document_point) {
    return canvas->widget_position_for_document_point(document_point);
  };
  const auto click = [canvas, widget_point](QPoint document_point) {
    send_mouse(*canvas, QEvent::MouseButtonPress, widget_point(document_point), Qt::LeftButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point(document_point), Qt::LeftButton, Qt::NoButton);
  };
  const auto hover = [canvas, widget_point](QPoint document_point) {
    send_mouse(*canvas, QEvent::MouseMove, widget_point(document_point), Qt::NoButton, Qt::NoButton);
  };

  // Click near the left edge of the dark square, then trace all four sides with
  // the button up. The wire must snap onto the edge and drop anchors on its own.
  click(QPoint(56, 70));
  CHECK(canvas->magnetic_lasso_active());
  hover(QPoint(56, 110));
  hover(QPoint(56, 150));
  hover(QPoint(56, 190));
  hover(QPoint(57, 212));
  CHECK(canvas->magnetic_lasso_anchor_count() >= 2);
  // The trace is only a path preview: no selection region may exist mid-trace.
  CHECK(!canvas->has_selection());
  save_widget_artifact("ui_magnetic_lasso_trace", *canvas);

  click(QPoint(70, 223));  // manual anchor around the bottom-left corner
  hover(QPoint(120, 223));
  hover(QPoint(170, 223));
  hover(QPoint(214, 223));
  hover(QPoint(224, 190));
  hover(QPoint(224, 140));
  hover(QPoint(224, 90));
  hover(QPoint(190, 56));
  hover(QPoint(120, 56));
  hover(QPoint(80, 56));
  send_mouse(*canvas, QEvent::MouseButtonPress, widget_point(QPoint(64, 60)), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point(QPoint(64, 60)), Qt::LeftButton, Qt::NoButton);
  send_mouse(*canvas, QEvent::MouseButtonDblClick, widget_point(QPoint(64, 60)), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point(QPoint(64, 60)), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(!canvas->magnetic_lasso_active());
  CHECK(canvas->has_selection());
  const auto region = canvas->selected_document_region();
  CHECK(region.contains(QPoint(140, 140)));
  CHECK(region.contains(QPoint(70, 140)));   // hugging the left edge (boundary ~x 59-61)
  CHECK(!region.contains(QPoint(50, 140)));
  CHECK(!region.contains(QPoint(140, 30)));
  CHECK(!region.contains(QPoint(240, 140)));
  CHECK(!region.contains(QPoint(140, 250)));
  // Unlike Quick Select there is no auto-switch to Add: the latched mode stays.
  CHECK(canvas->selection_mode() == patchy::ui::CanvasWidget::SelectionMode::Replace);
  save_widget_artifact("ui_magnetic_lasso_selection", *canvas);

  undo_action->trigger();
  QApplication::processEvents();
  CHECK(!canvas->has_selection());
}

void ui_magnetic_lasso_backspace_escape_and_enter() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::MagneticLasso);
  canvas->set_selection_feather_radius(0);
  canvas->set_magnetic_lasso_frequency(57);

  const auto widget_point = [canvas](QPoint document_point) {
    return canvas->widget_position_for_document_point(document_point);
  };
  const auto click = [canvas, widget_point](QPoint document_point) {
    send_mouse(*canvas, QEvent::MouseButtonPress, widget_point(document_point), Qt::LeftButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point(document_point), Qt::LeftButton, Qt::NoButton);
  };
  const auto hover = [canvas, widget_point](QPoint document_point) {
    send_mouse(*canvas, QEvent::MouseMove, widget_point(document_point), Qt::NoButton, Qt::NoButton);
  };

  // On the flat default document the wire is a straight line; long hovers still
  // cool into auto anchors, and Backspace pops them one at a time.
  click(QPoint(100, 100));
  CHECK(canvas->magnetic_lasso_active());
  hover(QPoint(200, 100));
  hover(QPoint(260, 100));
  const auto anchors = canvas->magnetic_lasso_anchor_count();
  CHECK(anchors >= 3);
  send_key(*canvas, Qt::Key_Backspace);
  CHECK(canvas->magnetic_lasso_anchor_count() == anchors - 1);
  send_key(*canvas, Qt::Key_Escape);
  CHECK(!canvas->magnetic_lasso_active());
  CHECK(!canvas->has_selection());

  // Enter closes the current path with a straight segment back to the start.
  click(QPoint(100, 100));
  hover(QPoint(200, 100));
  click(QPoint(200, 100));
  hover(QPoint(150, 180));
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(!canvas->magnetic_lasso_active());
  CHECK(canvas->has_selection());
  CHECK(canvas->selected_document_region().contains(QPoint(150, 120)));
}

void ui_magnetic_lasso_delete_and_backspace_pop_anchors_not_layer_clear() {
  // Backspace binds layer.clear on macOS (and Delete everywhere): a live trace must keep
  // both keys for anchor-popping. QTest::keyClick dispatches through the platform path so
  // QShortcutMap participates -- what is under test is CanvasWidget::event() accepting
  // the ShortcutOverride while a trace is live (send_key would bypass the shortcut map).
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::MagneticLasso);
  canvas->set_selection_feather_radius(0);
  canvas->set_magnetic_lasso_frequency(57);
  canvas->setFocus();
  QApplication::processEvents();

  const auto widget_point = [canvas](QPoint document_point) {
    return canvas->widget_position_for_document_point(document_point);
  };
  const auto click = [canvas, widget_point](QPoint document_point) {
    send_mouse(*canvas, QEvent::MouseButtonPress, widget_point(document_point), Qt::LeftButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point(document_point), Qt::LeftButton, Qt::NoButton);
  };
  const auto hover = [canvas, widget_point](QPoint document_point) {
    send_mouse(*canvas, QEvent::MouseMove, widget_point(document_point), Qt::NoButton, Qt::NoButton);
  };

  click(QPoint(100, 100));
  CHECK(canvas->magnetic_lasso_active());
  hover(QPoint(200, 100));
  hover(QPoint(260, 100));
  click(QPoint(260, 100));
  const auto anchors = canvas->magnetic_lasso_anchor_count();
  CHECK(anchors >= 3);

  QTest::keyClick(canvas, Qt::Key_Delete);
  QApplication::processEvents();
  CHECK(canvas->magnetic_lasso_active());
  CHECK(canvas->magnetic_lasso_anchor_count() == anchors - 1);

  QTest::keyClick(canvas, Qt::Key_Backspace);
  QApplication::processEvents();
  CHECK(canvas->magnetic_lasso_active());
  CHECK(canvas->magnetic_lasso_anchor_count() == anchors - 2);

  // layer.clear must NOT have fired: the background layer is still opaque.
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(!document.layers().empty());
  const auto* pixel = document.layers().front().pixels().pixel(5, 5);
  CHECK(pixel != nullptr);
  CHECK(pixel[3] == 255);

  send_key(*canvas, Qt::Key_Escape);
  CHECK(!canvas->magnetic_lasso_active());
}

void ui_magnetic_lasso_anchor_density_follows_zoom() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::MagneticLasso);
  canvas->set_magnetic_lasso_frequency(57);
  canvas->set_zoom(4.0);
  QApplication::processEvents();

  const auto widget_point = [canvas](QPoint document_point) {
    return canvas->widget_position_for_document_point(document_point);
  };
  // Anchor spacing is SCREEN pixels: at 400% zoom a short 40-document-px trace
  // covers 160 screen px and must cool into several fastening points (the
  // pre-fix document-space spacing dropped none, which is why the boxes never
  // appeared at high zoom).
  send_mouse(*canvas, QEvent::MouseButtonPress, widget_point(QPoint(100, 100)), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point(QPoint(100, 100)), Qt::LeftButton, Qt::NoButton);
  CHECK(canvas->magnetic_lasso_active());
  send_mouse(*canvas, QEvent::MouseMove, widget_point(QPoint(120, 100)), Qt::NoButton, Qt::NoButton);
  send_mouse(*canvas, QEvent::MouseMove, widget_point(QPoint(140, 100)), Qt::NoButton, Qt::NoButton);
  CHECK(canvas->magnetic_lasso_anchor_count() >= 4);
  send_key(*canvas, Qt::Key_Escape);
  CHECK(!canvas->magnetic_lasso_active());
}

void ui_magnetic_lasso_enter_closes_along_edges() {
  patchy::Document document(320, 280, patchy::PixelFormat::rgba8());
  auto pixels = solid_pixels(320, 280, patchy::PixelFormat::rgba8(), QColor(Qt::white));
  fill_pixel_rect(pixels, QRect(60, 60, 160, 160), QColor(20, 20, 20, 255));
  document.add_pixel_layer("Art", std::move(pixels));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Magnetic Close"));
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::MagneticLasso);
  canvas->set_selection_feather_radius(0);
  canvas->set_magnetic_lasso_width(10);
  canvas->set_magnetic_lasso_edge_contrast(10);

  const auto widget_point = [canvas](QPoint document_point) {
    return canvas->widget_position_for_document_point(document_point);
  };
  const auto hover = [canvas, widget_point](QPoint document_point) {
    send_mouse(*canvas, QEvent::MouseMove, widget_point(document_point), Qt::NoButton, Qt::NoButton);
  };

  // Trace left, bottom, and right edges of the dark square, then press Enter
  // from near the top-right corner. The closing segment must snap along the
  // TOP edge (Photoshop's magnetic close), not cut a straight chord through
  // the square: the chord from (220, 66) to (59, 70) would exclude (140, 62).
  send_mouse(*canvas, QEvent::MouseButtonPress, widget_point(QPoint(56, 70)), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point(QPoint(56, 70)), Qt::LeftButton, Qt::NoButton);
  CHECK(canvas->magnetic_lasso_active());
  hover(QPoint(56, 120));
  hover(QPoint(56, 180));
  hover(QPoint(57, 214));
  hover(QPoint(80, 224));
  hover(QPoint(140, 224));
  hover(QPoint(200, 224));
  hover(QPoint(224, 200));
  hover(QPoint(224, 140));
  hover(QPoint(224, 90));
  hover(QPoint(222, 66));
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();

  CHECK(!canvas->magnetic_lasso_active());
  CHECK(canvas->has_selection());
  const auto region = canvas->selected_document_region();
  CHECK(region.contains(QPoint(140, 140)));
  CHECK(region.contains(QPoint(140, 62)));   // inside only when the close hugged the top edge
  CHECK(!region.contains(QPoint(140, 40)));
  CHECK(!region.contains(QPoint(50, 140)));
  CHECK(!region.contains(QPoint(240, 140)));
  save_widget_artifact("ui_magnetic_lasso_magnetic_close", *canvas);
}

void ui_magnetic_lasso_line_trace_double_click_closes_straight() {
  // A dark base slab with a bump on top. Tracing the top profile (up and over
  // the bump) and double-clicking must connect finish to start with a straight
  // segment, enclosing the bump. The naive magnetic close retraces the traced
  // edge backwards and winding-fill collapses the polygon to two tiny slivers
  // (the July 2026 report).
  patchy::Document document(320, 240, patchy::PixelFormat::rgba8());
  auto pixels = solid_pixels(320, 240, patchy::PixelFormat::rgba8(), QColor(Qt::white));
  fill_pixel_rect(pixels, QRect(40, 120, 240, 100), QColor(20, 20, 20, 255));
  fill_pixel_rect(pixels, QRect(120, 70, 80, 50), QColor(20, 20, 20, 255));
  document.add_pixel_layer("Art", std::move(pixels));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Line Trace"));
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::MagneticLasso);
  canvas->set_selection_feather_radius(0);
  canvas->set_magnetic_lasso_width(10);
  canvas->set_magnetic_lasso_edge_contrast(10);

  const auto widget_point = [canvas](QPoint document_point) {
    return canvas->widget_position_for_document_point(document_point);
  };
  const auto hover = [canvas, widget_point](QPoint document_point) {
    send_mouse(*canvas, QEvent::MouseMove, widget_point(document_point), Qt::NoButton, Qt::NoButton);
  };

  send_mouse(*canvas, QEvent::MouseButtonPress, widget_point(QPoint(50, 116)), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point(QPoint(50, 116)), Qt::LeftButton, Qt::NoButton);
  CHECK(canvas->magnetic_lasso_active());
  hover(QPoint(90, 116));
  hover(QPoint(116, 100));
  hover(QPoint(124, 72));
  hover(QPoint(170, 66));
  hover(QPoint(196, 76));
  hover(QPoint(204, 110));
  hover(QPoint(240, 116));
  send_mouse(*canvas, QEvent::MouseButtonPress, widget_point(QPoint(240, 118)), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point(QPoint(240, 118)), Qt::LeftButton, Qt::NoButton);
  send_mouse(*canvas, QEvent::MouseButtonDblClick, widget_point(QPoint(240, 118)), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point(QPoint(240, 118)), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(!canvas->magnetic_lasso_active());
  CHECK(canvas->has_selection());
  const auto region = canvas->selected_document_region();
  CHECK(region.contains(QPoint(160, 95)));    // inside the bump: enclosed by the straight close
  CHECK(!region.contains(QPoint(160, 170)));  // base body below the close stays out
  CHECK(!region.contains(QPoint(60, 100)));   // above the slab, left of the bump
  CHECK(!region.contains(QPoint(260, 100)));  // above the slab, right of the bump
  save_widget_artifact("ui_magnetic_lasso_line_trace_close", *canvas);
}

void ui_magnetic_lasso_antialias_clear_leaves_partial_edge_pixels() {
  // Anti-alias (feather 0) must commit the traced boundary as a coverage mask,
  // not a hard QRegion: clearing under the selection then leaves partially
  // erased pixels along diagonal edges like Photoshop (the July 2026 hard
  // staircase-delete report).
  patchy::Document document(200, 160, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("White background",
                           solid_pixels(200, 160, patchy::PixelFormat::rgb8(), QColor(Qt::white)));
  document.add_pixel_layer("Ink", solid_pixels(200, 160, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 255)));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("AA Clear"));
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::MagneticLasso);
  canvas->set_selection_feather_radius(0);
  canvas->set_selection_antialias(true);

  const auto widget_point = [canvas](QPoint document_point) {
    return canvas->widget_position_for_document_point(document_point);
  };
  const auto click = [canvas, widget_point](QPoint document_point) {
    send_mouse(*canvas, QEvent::MouseButtonPress, widget_point(document_point), Qt::LeftButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point(document_point), Qt::LeftButton, Qt::NoButton);
  };
  const auto hover = [canvas, widget_point](QPoint document_point) {
    send_mouse(*canvas, QEvent::MouseMove, widget_point(document_point), Qt::NoButton, Qt::NoButton);
  };

  // Flat black layer: the wire draws straight lines, giving a triangle whose
  // top edge runs at slope 1:4 from (30, 30) to (150, 60).
  click(QPoint(30, 30));
  hover(QPoint(150, 60));
  click(QPoint(150, 60));
  hover(QPoint(30, 90));
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(canvas->has_selection());

  // The committed selection itself must carry partial coverage on the diagonal.
  CHECK(canvas->selection_has_partial_alpha());
  bool mask_has_partial = false;
  for (int x = 86; x <= 94 && !mask_has_partial; ++x) {
    for (int y = 40; y <= 50 && !mask_has_partial; ++y) {
      const auto alpha = canvas->selection_alpha_at(QPoint(x, y));
      mask_has_partial = alpha > 16 && alpha < 240;
    }
  }
  CHECK(mask_has_partial);

  require_action(window, "layerClearAction")->trigger();
  QApplication::processEvents();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();

  CHECK(color_close(canvas_pixel_center(*canvas, QPoint(90, 70)), Qt::white, 12));  // cleared interior
  CHECK(color_close(canvas_pixel_center(*canvas, QPoint(90, 15)), Qt::black, 12));  // untouched outside
  // The diagonal boundary near (90, 45) must hold at least one partially
  // erased pixel (mid-gray composite over the white background).
  bool found_partial = false;
  for (int x = 86; x <= 94 && !found_partial; ++x) {
    for (int y = 40; y <= 50 && !found_partial; ++y) {
      const auto color = canvas_pixel_center(*canvas, QPoint(x, y));
      const auto value = (color.red() + color.green() + color.blue()) / 3;
      found_partial = value > 40 && value < 215;
    }
  }
  CHECK(found_partial);
  save_widget_artifact("ui_magnetic_lasso_aa_clear", *canvas);
}

void ui_magnetic_lasso_click_near_start_closes() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::MagneticLasso);
  canvas->set_selection_feather_radius(0);

  const auto widget_point = [canvas](QPoint document_point) {
    return canvas->widget_position_for_document_point(document_point);
  };
  const auto click = [canvas, widget_point](QPoint document_point) {
    send_mouse(*canvas, QEvent::MouseButtonPress, widget_point(document_point), Qt::LeftButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point(document_point), Qt::LeftButton, Qt::NoButton);
  };
  const auto hover = [canvas, widget_point](QPoint document_point) {
    send_mouse(*canvas, QEvent::MouseMove, widget_point(document_point), Qt::NoButton, Qt::NoButton);
  };

  click(QPoint(100, 100));
  hover(QPoint(200, 100));
  click(QPoint(200, 100));
  hover(QPoint(150, 180));
  click(QPoint(150, 180));
  hover(QPoint(104, 103));
  click(QPoint(100, 100));  // lands on the start anchor: closes and commits
  QApplication::processEvents();
  CHECK(!canvas->magnetic_lasso_active());
  CHECK(canvas->has_selection());
  const auto region = canvas->selected_document_region();
  CHECK(region.contains(QPoint(150, 120)));
  CHECK(!region.contains(QPoint(115, 170)));
}

// Polygonal Lasso (GitHub issue 287). These drive the tool through its action, the mouse and
// the keyboard only and read back the committed selection, so they need nothing the tool
// itself adds to the canvas API.
struct PolygonLassoFixture {
  patchy::ui::MainWindow window;
  patchy::ui::CanvasWidget* canvas{nullptr};

  // tool_action picks the starting tool; the Lasso's own tests start there.
  explicit PolygonLassoFixture(const char* tool_action = "toolPolygonalLassoAction") {
    patchy::Document document(320, 280, patchy::PixelFormat::rgba8());
    auto pixels = solid_pixels(320, 280, patchy::PixelFormat::rgba8(), QColor(Qt::white));
    document.add_pixel_layer("Art", std::move(pixels));
    show_window(window);
    window.add_document_session(std::move(document), QStringLiteral("Polygonal Lasso"));
    QApplication::processEvents();
    canvas = require_canvas(window);
    auto* action = require_action(window, tool_action);
    action->trigger();
    QApplication::processEvents();
    CHECK(action->isChecked());
    canvas->setFocus();
  }

  [[nodiscard]] QPoint at(QPoint document_point) const {
    return canvas->widget_position_for_document_point(document_point);
  }
  void hover(QPoint document_point, Qt::KeyboardModifiers modifiers = Qt::NoModifier) const {
    send_mouse(*canvas, QEvent::MouseMove, at(document_point), Qt::NoButton, Qt::NoButton, modifiers);
  }
  void click(QPoint document_point, Qt::KeyboardModifiers modifiers = Qt::NoModifier) const {
    hover(document_point, modifiers);
    send_mouse(*canvas, QEvent::MouseButtonPress, at(document_point), Qt::LeftButton, Qt::LeftButton, modifiers);
    send_mouse(*canvas, QEvent::MouseButtonRelease, at(document_point), Qt::LeftButton, Qt::NoButton, modifiers);
  }
  void double_click(QPoint document_point) const {
    click(document_point);
    send_mouse(*canvas, QEvent::MouseButtonDblClick, at(document_point), Qt::LeftButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseButtonRelease, at(document_point), Qt::LeftButton, Qt::NoButton);
  }
  void clicks(std::initializer_list<QPoint> points) const {
    for (const auto& point : points) {
      click(point);
    }
  }
  [[nodiscard]] int alpha(QPoint document_point) const {
    const auto mask = canvas->selection_as_grayscale();
    const auto* pixel = mask.pixel(document_point.x(), document_point.y());
    CHECK(pixel != nullptr);
    return pixel[0];
  }
  [[nodiscard]] bool selected(QPoint document_point) const { return alpha(document_point) >= 128; }
  [[nodiscard]] QString last_history_label() const {
    auto* list = window.findChild<QListWidget*>(QStringLiteral("historyList"));
    CHECK(list != nullptr);
    CHECK(list->count() > 0);
    return list->item(list->count() - 1)->text();
  }
};

void ui_polygonal_lasso_places_points_and_closes() {
  PolygonLassoFixture f;
  // Clicks place straight segments; nothing is selected until the outline closes.
  f.clicks({QPoint(60, 60), QPoint(200, 60), QPoint(200, 200), QPoint(60, 200)});
  f.hover(QPoint(62, 61));
  CHECK(!f.canvas->has_selection());
  save_widget_artifact("ui_polygonal_lasso_in_progress", *f.canvas);
  f.click(QPoint(61, 61));  // on the start point: closes
  QApplication::processEvents();
  CHECK(f.canvas->has_selection());
  CHECK(f.selected(QPoint(130, 130)));
  CHECK(f.selected(QPoint(63, 197)));
  CHECK(!f.selected(QPoint(40, 130)));
  CHECK(!f.selected(QPoint(220, 130)));
  CHECK(!f.selected(QPoint(130, 220)));
  CHECK(f.canvas->selection_mode() == patchy::ui::CanvasWidget::SelectionMode::Replace);
  // A click after the close starts a new outline instead of extending the old one.
  f.click(QPoint(240, 240));
  CHECK(f.selected(QPoint(130, 130)));
  send_key(*f.canvas, Qt::Key_Escape);

  // Double-click closes: a triangle under its hypotenuse, nothing above it.
  f.click(QPoint(100, 100));
  f.click(QPoint(180, 100));
  f.double_click(QPoint(180, 180));
  QApplication::processEvents();
  CHECK(f.selected(QPoint(170, 120)));
  CHECK(!f.selected(QPoint(110, 170)));
  CHECK(!f.selected(QPoint(70, 150)));  // the earlier square was replaced

  // Enter closes too, straight back to the start; the rubber band is not a vertex.
  f.clicks({QPoint(40, 40), QPoint(120, 40), QPoint(120, 120)});
  f.hover(QPoint(300, 260));
  send_key(*f.canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(f.selected(QPoint(110, 60)));
  CHECK(!f.selected(QPoint(60, 110)));
  CHECK(!f.selected(QPoint(200, 150)));
}

void ui_polygonal_lasso_shift_keeps_45_degrees() {
  PolygonLassoFixture f;
  f.click(QPoint(60, 60));
  // Shift after the start click aims the segment at the nearest 45 degree step: the
  // pointer 15 px low still places a horizontal edge, and one 62 x 80 px away a diagonal.
  f.click(QPoint(200, 75), Qt::ShiftModifier);
  f.click(QPoint(262, 140), Qt::ShiftModifier);
  f.click(QPoint(60, 200));
  send_key(*f.canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(f.canvas->has_selection());
  // The top edge is y = 60 all the way (a free segment would pass y ~67 at x = 130).
  CHECK(f.selected(QPoint(130, 63)));
  CHECK(f.selected(QPoint(195, 63)));
  CHECK(!f.selected(QPoint(130, 56)));
  // The diagonal from (200, 60) runs at exactly 45 degrees, to (271, 131): at x = 240 the
  // edge is y = 100 (a free segment to (262, 140) would pass y ~112).
  CHECK(f.selected(QPoint(240, 106)));
  CHECK(!f.selected(QPoint(240, 94)));
  CHECK(f.selected(QPoint(262, 126)));
}

void ui_polygonal_lasso_backspace_delete_and_escape() {
  PolygonLassoFixture f;
  // Backspace drops the newest point, which a stray click placed.
  f.clicks({QPoint(60, 60), QPoint(200, 60), QPoint(200, 200), QPoint(100, 260)});
  send_key(*f.canvas, Qt::Key_Backspace);
  f.click(QPoint(60, 200));
  send_key(*f.canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(f.selected(QPoint(130, 130)));
  CHECK(!f.selected(QPoint(100, 230)));

  // Delete and Backspace reach the tool through the shortcut map (layer.clear binds Delete,
  // and Backspace on macOS): the layer keeps its pixels.
  f.canvas->clear_selection();
  f.clicks({QPoint(60, 60), QPoint(200, 60), QPoint(200, 200), QPoint(130, 260), QPoint(90, 260)});
  QTest::keyClick(f.canvas, Qt::Key_Delete);
  QTest::keyClick(f.canvas, Qt::Key_Backspace);
  QApplication::processEvents();
  f.click(QPoint(60, 200));
  send_key(*f.canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(f.selected(QPoint(130, 130)));
  CHECK(!f.selected(QPoint(120, 225)));
  auto& document = patchy::ui::MainWindowTestAccess::document(f.window);
  const auto* pixel = document.layers().front().pixels().pixel(5, 5);
  CHECK(pixel != nullptr);
  CHECK(pixel[3] == 255);

  // Esc drops the whole outline and keeps the selection as it was.
  f.clicks({QPoint(220, 220), QPoint(300, 220), QPoint(300, 270)});
  send_key(*f.canvas, Qt::Key_Escape);
  send_key(*f.canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(f.selected(QPoint(130, 130)));
  CHECK(!f.selected(QPoint(280, 230)));

  // Backspace past the first point cancels too.
  f.click(QPoint(220, 220));
  send_key(*f.canvas, Qt::Key_Backspace);
  f.clicks({QPoint(10, 10), QPoint(50, 10), QPoint(50, 50)});
  send_key(*f.canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(f.selected(QPoint(45, 20)));
  CHECK(!f.selected(QPoint(130, 130)));
}

void ui_polygonal_lasso_combine_modes() {
  PolygonLassoFixture f;
  const auto square = [&f](int left, int top, int size, Qt::KeyboardModifiers start_modifiers) {
    f.click(QPoint(left, top), start_modifiers);
    f.clicks({QPoint(left + size, top), QPoint(left + size, top + size), QPoint(left, top + size)});
    send_key(*f.canvas, Qt::Key_Return);
    QApplication::processEvents();
  };
  const auto reset = [&f, &square] {
    f.canvas->clear_selection();
    square(40, 40, 120, Qt::NoModifier);
    CHECK(f.selected(QPoint(60, 60)));
    CHECK(!f.selected(QPoint(220, 220)));
  };

  reset();  // Shift at the start click adds
  square(120, 120, 120, Qt::ShiftModifier);
  CHECK(f.selected(QPoint(60, 60)));
  CHECK(f.selected(QPoint(140, 140)));
  CHECK(f.selected(QPoint(220, 220)));

  reset();  // Alt subtracts
  square(120, 120, 120, Qt::AltModifier);
  CHECK(f.selected(QPoint(60, 60)));
  CHECK(!f.selected(QPoint(140, 140)));
  CHECK(!f.selected(QPoint(220, 220)));

  reset();  // Shift+Alt intersects
  square(120, 120, 120, Qt::ShiftModifier | Qt::AltModifier);
  CHECK(!f.selected(QPoint(60, 60)));
  CHECK(f.selected(QPoint(140, 140)));
  CHECK(!f.selected(QPoint(220, 220)));

  // The options-bar mode applies without modifiers and is the tool's own.
  reset();
  require_action(f.window, "selectionSubtractModeAction")->trigger();
  QApplication::processEvents();
  square(120, 120, 120, Qt::NoModifier);
  CHECK(f.selected(QPoint(60, 60)));
  CHECK(!f.selected(QPoint(140, 140)));
  require_action(f.window, "toolLassoAction")->trigger();
  QApplication::processEvents();
  CHECK(f.canvas->selection_mode() == patchy::ui::CanvasWidget::SelectionMode::Replace);
  require_action(f.window, "toolPolygonalLassoAction")->trigger();
  QApplication::processEvents();
  CHECK(f.canvas->selection_mode() == patchy::ui::CanvasWidget::SelectionMode::Subtract);
  require_action(f.window, "selectionNewModeAction")->trigger();
  QApplication::processEvents();
}

void ui_polygonal_lasso_feather_and_antialias() {
  SettingsValueRestorer saved_feather(QStringLiteral("tools/polygonalLassoFeather"));
  SettingsValueRestorer saved_anti_alias(QStringLiteral("tools/polygonalLassoAntiAlias"));
  PolygonLassoFixture f;
  auto* feather = f.window.findChild<QSpinBox*>(QStringLiteral("selectionFeatherSpin"));
  auto* anti_alias = f.window.findChild<QCheckBox*>(QStringLiteral("selectionAntiAliasCheck"));
  CHECK(feather != nullptr);
  CHECK(anti_alias != nullptr);
  CHECK(feather->isVisible());
  CHECK(anti_alias->isVisible());

  // Feather softens the closed outline across the edge.
  feather->setValue(8);
  CHECK(f.canvas->selection_feather_radius() == 8);
  f.clicks({QPoint(60, 60), QPoint(200, 60), QPoint(200, 200), QPoint(60, 200)});
  send_key(*f.canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(f.canvas->selection_has_partial_alpha());
  CHECK(f.alpha(QPoint(130, 130)) == 255);
  const auto edge = f.alpha(QPoint(60, 130));
  CHECK(edge > 40 && edge < 215);
  CHECK(f.alpha(QPoint(130, 20)) == 0);
  save_widget_artifact("ui_polygonal_lasso_feathered", *f.canvas);

  // Feather and Anti-alias are the tool's own: the Lasso keeps its values.
  require_action(f.window, "toolLassoAction")->trigger();
  QApplication::processEvents();
  CHECK(feather->value() == 0);
  require_action(f.window, "toolPolygonalLassoAction")->trigger();
  QApplication::processEvents();
  CHECK(feather->value() == 8);

  // With Anti-alias off and no feather a slanted edge stays hard.
  feather->setValue(0);
  anti_alias->setChecked(false);
  f.clicks({QPoint(60, 60), QPoint(230, 90), QPoint(150, 220)});
  send_key(*f.canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(f.canvas->has_selection());
  CHECK(!f.canvas->selection_has_partial_alpha());
  // Anti-alias on gives the same slant partial edge pixels.
  anti_alias->setChecked(true);
  f.clicks({QPoint(60, 60), QPoint(230, 90), QPoint(150, 220)});
  send_key(*f.canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(f.canvas->selection_has_partial_alpha());
}

void ui_polygonal_lasso_is_one_undo_step() {
  PolygonLassoFixture f;
  const auto depth = patchy::ui::MainWindowTestAccess::active_session_undo_depth(f.window);
  f.clicks({QPoint(60, 60), QPoint(200, 60), QPoint(200, 200)});
  // Placing points is not history.
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(f.window) == depth);
  send_key(*f.canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(f.window) == depth + 1);
  CHECK(f.last_history_label() == QStringLiteral("Polygonal Lasso"));
  CHECK(f.selected(QPoint(180, 100)));

  patchy::ui::MainWindowTestAccess::undo(f.window);
  QApplication::processEvents();
  CHECK(!f.canvas->has_selection());
  patchy::ui::MainWindowTestAccess::redo(f.window);
  QApplication::processEvents();
  CHECK(f.selected(QPoint(180, 100)));
  CHECK(!patchy::ui::MainWindowTestAccess::active_session_is_modified(f.window));
}

void ui_polygonal_lasso_alt_drag_adds_a_freehand_run() {
  PolygonLassoFixture f;
  f.clicks({QPoint(60, 60), QPoint(200, 60)});
  // Alt held while dragging from a new point traces freehand; the release ends the run.
  send_mouse(*f.canvas, QEvent::MouseButtonPress, f.at(QPoint(200, 60)), Qt::LeftButton, Qt::LeftButton,
             Qt::AltModifier);
  for (const auto& point : {QPoint(220, 90), QPoint(240, 130), QPoint(220, 170), QPoint(200, 200)}) {
    send_mouse(*f.canvas, QEvent::MouseMove, f.at(point), Qt::NoButton, Qt::LeftButton, Qt::AltModifier);
  }
  send_mouse(*f.canvas, QEvent::MouseButtonRelease, f.at(QPoint(200, 200)), Qt::LeftButton, Qt::NoButton,
             Qt::AltModifier);
  CHECK(!f.canvas->has_selection());
  f.click(QPoint(60, 200));
  send_key(*f.canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(f.selected(QPoint(130, 130)));
  CHECK(f.selected(QPoint(225, 130)));  // the freehand bulge past x = 200
  CHECK(!f.selected(QPoint(250, 130)));

  // Backspace drops the run with its end point.
  f.clicks({QPoint(60, 60), QPoint(200, 60)});
  send_mouse(*f.canvas, QEvent::MouseButtonPress, f.at(QPoint(200, 60)), Qt::LeftButton, Qt::LeftButton,
             Qt::AltModifier);
  for (const auto& point : {QPoint(240, 130), QPoint(200, 200)}) {
    send_mouse(*f.canvas, QEvent::MouseMove, f.at(point), Qt::NoButton, Qt::LeftButton, Qt::AltModifier);
  }
  send_mouse(*f.canvas, QEvent::MouseButtonRelease, f.at(QPoint(200, 200)), Qt::LeftButton, Qt::NoButton,
             Qt::AltModifier);
  send_key(*f.canvas, Qt::Key_Backspace);
  f.clicks({QPoint(200, 200), QPoint(60, 200)});
  send_key(*f.canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(f.selected(QPoint(130, 130)));
  CHECK(!f.selected(QPoint(225, 130)));
}

void ui_lasso_alt_release_switches_to_straight_segments() {
  PolygonLassoFixture f("toolLassoAction");
  CHECK(f.canvas->tool() == patchy::ui::CanvasTool::Lasso);
  // A freehand drag with Alt pressed partway and still held at the release keeps going as
  // straight segments: clicks add points, and letting go of Alt closes the outline.
  send_mouse(*f.canvas, QEvent::MouseButtonPress, f.at(QPoint(60, 60)), Qt::LeftButton, Qt::LeftButton);
  for (const auto& point : {QPoint(120, 58), QPoint(200, 60)}) {
    send_mouse(*f.canvas, QEvent::MouseMove, f.at(point), Qt::NoButton, Qt::LeftButton);
  }
  send_key_press(*f.canvas, Qt::Key_Alt, Qt::AltModifier);
  send_mouse(*f.canvas, QEvent::MouseMove, f.at(QPoint(200, 120)), Qt::NoButton, Qt::LeftButton, Qt::AltModifier);
  send_mouse(*f.canvas, QEvent::MouseButtonRelease, f.at(QPoint(200, 200)), Qt::LeftButton, Qt::NoButton,
             Qt::AltModifier);
  CHECK(!f.canvas->has_selection());
  f.click(QPoint(60, 200), Qt::AltModifier);
  CHECK(!f.canvas->has_selection());
  send_key_release(*f.canvas, Qt::Key_Alt, Qt::NoModifier);
  QApplication::processEvents();
  CHECK(f.canvas->has_selection());
  CHECK(f.selected(QPoint(130, 130)));
  CHECK(f.selected(QPoint(70, 190)));  // the straight segment to (60, 200), not a chord to the start
  CHECK(!f.selected(QPoint(130, 230)));
  CHECK(f.last_history_label() == QStringLiteral("Lasso"));

  // Alt held from the press is Subtract, committed on release as before.
  send_mouse(*f.canvas, QEvent::MouseButtonPress, f.at(QPoint(100, 100)), Qt::LeftButton, Qt::LeftButton,
             Qt::AltModifier);
  for (const auto& point : {QPoint(160, 100), QPoint(160, 160), QPoint(100, 160)}) {
    send_mouse(*f.canvas, QEvent::MouseMove, f.at(point), Qt::NoButton, Qt::LeftButton, Qt::AltModifier);
  }
  send_mouse(*f.canvas, QEvent::MouseButtonRelease, f.at(QPoint(100, 160)), Qt::LeftButton, Qt::NoButton,
             Qt::AltModifier);
  QApplication::processEvents();
  CHECK(!f.selected(QPoint(130, 130)));
  CHECK(f.selected(QPoint(70, 80)));
}

// GitHub issue 287's shots, in both schemes: the Lasso flyout with the new member, and a
// polygon outline in progress around a shape (the rubber band runs to the pointer, which
// rests near the start point, so the closing ring shows).
void ui_polygonal_lasso_issue_shots() {
  for (const auto scheme : {patchy::ui::ColorSchemePreference::Dark, patchy::ui::ColorSchemePreference::Light}) {
    ColorSchemeRestorer restore_scheme(scheme);
    const std::string suffix = scheme == patchy::ui::ColorSchemePreference::Light ? "_light" : "_dark";
    patchy::ui::MainWindow window;
    show_window(window);
    window.resize(1100, 720);
    QApplication::processEvents();

    auto* button = window.findChild<QToolButton*>(QStringLiteral("lassoToolButton"));
    CHECK(button != nullptr && button->menu() != nullptr);
    if (button == nullptr || button->menu() == nullptr) {
      return;
    }
    auto* menu = button->menu();
    bool captured = false;
    std::exception_ptr capture_error;
    QTimer::singleShot(0, [&] {
      // Never throw across Qt's event dispatch (docs/testing.md): keep the failure for later.
      try {
        save_window_popup_artifact("issue_287_lasso_flyout" + suffix, window, *button, *menu);
        captured = true;
      } catch (...) {
        capture_error = std::current_exception();
      }
      menu->close();
    });
    button->showMenu();
    QApplication::processEvents();
    if (capture_error) {
      std::rethrow_exception(capture_error);
    }
    CHECK(captured);

    patchy::Document document(480, 360, patchy::PixelFormat::rgba8());
    auto pixels = solid_pixels(480, 360, patchy::PixelFormat::rgba8(), QColor(236, 232, 222));
    fill_pixel_rect(pixels, QRect(120, 150, 240, 150), QColor(196, 92, 60));
    fill_pixel_rect(pixels, QRect(150, 190, 50, 60), QColor(70, 110, 160));
    fill_pixel_rect(pixels, QRect(260, 200, 60, 100), QColor(90, 60, 40));
    document.add_pixel_layer("House", std::move(pixels));
    window.add_document_session(std::move(document), QStringLiteral("Polygonal Lasso"));
    QApplication::processEvents();
    auto* canvas = require_canvas(window);
    require_action(window, "toolPolygonalLassoAction")->trigger();
    QApplication::processEvents();
    const auto at = [canvas](QPoint point) { return canvas->widget_position_for_document_point(point); };
    for (const auto& point : {QPoint(110, 145), QPoint(240, 60), QPoint(370, 145), QPoint(370, 310)}) {
      send_mouse(*canvas, QEvent::MouseMove, at(point), Qt::NoButton, Qt::NoButton);
      send_mouse(*canvas, QEvent::MouseButtonPress, at(point), Qt::LeftButton, Qt::LeftButton);
      send_mouse(*canvas, QEvent::MouseButtonRelease, at(point), Qt::LeftButton, Qt::NoButton);
    }
    send_mouse(*canvas, QEvent::MouseMove, at(QPoint(108, 300)), Qt::NoButton, Qt::NoButton);
    QApplication::processEvents();
    CHECK(!canvas->has_selection());
    save_widget_artifact("issue_287_polygon_in_progress" + suffix, window);
    send_mouse(*canvas, QEvent::MouseMove, at(QPoint(112, 148)), Qt::NoButton, Qt::NoButton);
    QApplication::processEvents();
    save_widget_artifact("issue_287_polygon_closing" + suffix, *canvas);
    send_key(*canvas, Qt::Key_Escape);
  }
}

void ui_magnetic_lasso_options_apply_to_new_documents() {
  SettingsValueRestorer saved_width(QStringLiteral("tools/magneticLassoWidth"));
  SettingsValueRestorer saved_contrast(QStringLiteral("tools/magneticLassoEdgeContrast"));
  SettingsValueRestorer saved_frequency(QStringLiteral("tools/magneticLassoFrequency"));
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Magnetic Lasso"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::MagneticLasso);

  auto* width_spin = window.findChild<QSpinBox*>(QStringLiteral("magneticLassoWidthSpin"));
  auto* contrast_spin = window.findChild<QSpinBox*>(QStringLiteral("magneticLassoContrastSpin"));
  auto* frequency_spin = window.findChild<QSpinBox*>(QStringLiteral("magneticLassoFrequencySpin"));
  CHECK(width_spin != nullptr);
  CHECK(contrast_spin != nullptr);
  CHECK(frequency_spin != nullptr);
  CHECK(width_spin->isVisible());
  width_spin->setValue(25);
  contrast_spin->setValue(30);
  frequency_spin->setValue(80);
  QApplication::processEvents();
  CHECK(canvas->magnetic_lasso_width() == 25);
  CHECK(canvas->magnetic_lasso_edge_contrast() == 30);
  CHECK(canvas->magnetic_lasso_frequency() == 80);

  accept_new_document_dialog(320, 240);
  require_action_by_text(window, QStringLiteral("New"))->trigger();
  QApplication::processEvents();
  auto* new_canvas = require_canvas(window);
  CHECK(new_canvas != canvas);
  CHECK(new_canvas->tool() == patchy::ui::CanvasTool::MagneticLasso);
  CHECK(new_canvas->magnetic_lasso_width() == 25);
  CHECK(new_canvas->magnetic_lasso_edge_contrast() == 30);
  CHECK(new_canvas->magnetic_lasso_frequency() == 80);
  CHECK(width_spin->value() == 25);
  CHECK(contrast_spin->value() == 30);
  CHECK(frequency_spin->value() == 80);
}

void ui_selection_feather_and_antialias_are_per_tool() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* feather = window.findChild<QSpinBox*>(QStringLiteral("selectionFeatherSpin"));
  auto* anti_alias = window.findChild<QCheckBox*>(QStringLiteral("selectionAntiAliasCheck"));
  CHECK(feather != nullptr);
  CHECK(anti_alias != nullptr);

  require_action_by_text(window, QStringLiteral("Magic Wand"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::MagicWand);
  CHECK(feather->value() == 0);
  CHECK(anti_alias->isChecked());
  feather->setValue(7);
  anti_alias->setChecked(false);
  CHECK(canvas->selection_feather_radius() == 7);
  CHECK(!canvas->selection_antialias());

  require_action_by_text(window, QStringLiteral("Lasso"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Lasso);
  CHECK(feather->value() == 0);
  CHECK(anti_alias->isChecked());
  CHECK(canvas->selection_feather_radius() == 0);
  CHECK(canvas->selection_antialias());
  feather->setValue(3);
  CHECK(canvas->selection_feather_radius() == 3);

  require_action_by_text(window, QStringLiteral("Magic Wand"))->trigger();
  QApplication::processEvents();
  CHECK(feather->value() == 7);
  CHECK(!anti_alias->isChecked());
  CHECK(canvas->selection_feather_radius() == 7);
  CHECK(!canvas->selection_antialias());

  // A new document's canvas takes the active tool's values.
  accept_new_document_dialog(320, 240);
  require_action_by_text(window, QStringLiteral("New"))->trigger();
  QApplication::processEvents();
  auto* new_canvas = require_canvas(window);
  CHECK(new_canvas != canvas);
  CHECK(new_canvas->selection_feather_radius() == 7);
  CHECK(!new_canvas->selection_antialias());
}

void ui_selection_edge_settings_persist_across_windows() {
  {
    patchy::ui::MainWindow window;
    show_window(window);
    require_canvas(window);
    auto* feather = window.findChild<QSpinBox*>(QStringLiteral("selectionFeatherSpin"));
    auto* anti_alias = window.findChild<QCheckBox*>(QStringLiteral("selectionAntiAliasCheck"));
    CHECK(feather != nullptr);
    CHECK(anti_alias != nullptr);
    require_action_by_text(window, QStringLiteral("Magic Wand"))->trigger();
    QApplication::processEvents();
    feather->setValue(9);
    anti_alias->setChecked(false);
    require_action_by_text(window, QStringLiteral("Lasso"))->trigger();
    QApplication::processEvents();
    feather->setValue(4);
    patchy::ui::MainWindowTestAccess::save_tool_settings(window);
  }
  QApplication::processEvents();
  auto settings = patchy::ui::app_settings();
  CHECK(settings.value(QStringLiteral("tools/wandFeather")).toInt() == 9);
  CHECK(!settings.value(QStringLiteral("tools/wandAntiAlias")).toBool());
  CHECK(settings.value(QStringLiteral("tools/lassoFeather")).toInt() == 4);
  CHECK(settings.value(QStringLiteral("tools/lassoAntiAlias")).toBool());

  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* feather = window.findChild<QSpinBox*>(QStringLiteral("selectionFeatherSpin"));
  auto* anti_alias = window.findChild<QCheckBox*>(QStringLiteral("selectionAntiAliasCheck"));
  CHECK(feather != nullptr);
  CHECK(anti_alias != nullptr);
  require_action_by_text(window, QStringLiteral("Magic Wand"))->trigger();
  QApplication::processEvents();
  CHECK(feather->value() == 9);
  CHECK(!anti_alias->isChecked());
  CHECK(canvas->selection_feather_radius() == 9);
  CHECK(!canvas->selection_antialias());
  require_action_by_text(window, QStringLiteral("Lasso"))->trigger();
  QApplication::processEvents();
  CHECK(feather->value() == 4);
  CHECK(anti_alias->isChecked());
  CHECK(canvas->selection_feather_radius() == 4);
  require_action_by_text(window, QStringLiteral("Elliptical Marquee"))->trigger();
  QApplication::processEvents();
  CHECK(feather->value() == 0);
  CHECK(anti_alias->isChecked());
}

void ui_quick_select_add_and_subtract_strokes() {
  patchy::Document document(360, 200, patchy::PixelFormat::rgba8());
  auto pixels = solid_pixels(360, 200, patchy::PixelFormat::rgba8(), QColor(Qt::white));
  fill_pixel_rect(pixels, QRect(40, 50, 90, 70), QColor(30, 90, 200, 255));
  fill_pixel_rect(pixels, QRect(210, 50, 90, 70), QColor(30, 90, 200, 255));
  document.add_pixel_layer("Art", std::move(pixels));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Quick Select Modes"));
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::QuickSelect);
  canvas->set_quick_select_size(20);
  canvas->set_selection_feather_radius(0);

  const auto stroke = [canvas](QPoint from, QPoint to, Qt::KeyboardModifiers modifiers) {
    const auto from_widget = canvas->widget_position_for_document_point(from);
    const auto to_widget = canvas->widget_position_for_document_point(to);
    send_mouse(*canvas, QEvent::MouseButtonPress, from_widget, Qt::LeftButton, Qt::LeftButton, modifiers);
    send_mouse(*canvas, QEvent::MouseMove, (from_widget + to_widget) / 2, Qt::NoButton, Qt::LeftButton,
               modifiers);
    send_mouse(*canvas, QEvent::MouseMove, to_widget, Qt::NoButton, Qt::LeftButton, modifiers);
    send_mouse(*canvas, QEvent::MouseButtonRelease, to_widget, Qt::LeftButton, Qt::NoButton, modifiers);
    QApplication::processEvents();
  };

  // New mode stroke grabs blob A and flips the tool to Add.
  stroke(QPoint(70, 85), QPoint(100, 85), Qt::NoModifier);
  CHECK(canvas->selected_document_region().contains(QPoint(50, 60)));
  CHECK(!canvas->selected_document_region().contains(QPoint(250, 85)));
  CHECK(canvas->selection_mode() == patchy::ui::CanvasWidget::SelectionMode::Add);
  auto* add_mode_action = window.findChild<QAction*>(QStringLiteral("selectionAddModeAction"));
  CHECK(add_mode_action != nullptr);
  CHECK(add_mode_action->isChecked());

  // Add stroke joins blob B.
  stroke(QPoint(240, 85), QPoint(270, 85), Qt::NoModifier);
  CHECK(canvas->selected_document_region().contains(QPoint(50, 60)));
  CHECK(canvas->selected_document_region().contains(QPoint(290, 110)));

  // Alt = subtract: blob B leaves, blob A stays.
  stroke(QPoint(240, 85), QPoint(270, 85), Qt::AltModifier);
  CHECK(canvas->selected_document_region().contains(QPoint(50, 60)));
  CHECK(!canvas->selected_document_region().contains(QPoint(250, 85)));

  // Quick Select has no Intersect: Shift+Alt clamps to Add.
  stroke(QPoint(240, 85), QPoint(270, 85), Qt::ShiftModifier | Qt::AltModifier);
  CHECK(canvas->selected_document_region().contains(QPoint(50, 60)));
  CHECK(canvas->selected_document_region().contains(QPoint(250, 85)));
  save_widget_artifact("ui_quick_select_modes", *canvas);
}

// Photo-like content (noisy skin-tone gradient + soft dark ellipse): the stroke must select
// the "eye" without flooding the surrounding "skin". Regression artifact for the July 2026
// over-selection report; eyeball ui_quick_select_photo_texture.png after model/lambda changes.
void ui_quick_select_photo_texture_selects_eye_not_face() {
  const std::int32_t width = 320;
  const std::int32_t height = 240;
  auto pixels = solid_pixels(width, height, patchy::PixelFormat::rgba8(), QColor(Qt::white));
  std::uint32_t noise_state = 0xFACE0FF5u;
  const auto next_noise = [&noise_state] {
    noise_state ^= noise_state << 13;
    noise_state ^= noise_state >> 17;
    noise_state ^= noise_state << 5;
    return noise_state;
  };
  const double center_x = 160.0;
  const double center_y = 120.0;
  const double radius_x = 55.0;
  const double radius_y = 34.0;
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      const int gradient = x * 30 / width;
      const int jitter = static_cast<int>(next_noise() % 25) - 12;
      const int skin_r = 215 + gradient / 2 + jitter;
      const int skin_g = 170 + gradient / 3 + jitter;
      const int skin_b = 140 + gradient / 3 + jitter;
      const int dark_r = 70 + jitter / 2;
      const int dark_g = 45 + jitter / 2;
      const int dark_b = 35 + jitter / 2;
      const double dx = (x - center_x) / radius_x;
      const double dy = (y - center_y) / radius_y;
      const double edge = std::clamp((std::sqrt(dx * dx + dy * dy) - 1.0) * (radius_x / 4.0), 0.0, 1.0);
      auto* px = pixels.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(std::clamp(
          static_cast<int>(std::lround(dark_r + (skin_r - dark_r) * edge)), 0, 255));
      px[1] = static_cast<std::uint8_t>(std::clamp(
          static_cast<int>(std::lround(dark_g + (skin_g - dark_g) * edge)), 0, 255));
      px[2] = static_cast<std::uint8_t>(std::clamp(
          static_cast<int>(std::lround(dark_b + (skin_b - dark_b) * edge)), 0, 255));
      px[3] = 255;
    }
  }
  patchy::Document document(width, height, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Photo", std::move(pixels));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Quick Select Photo"));
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::QuickSelect);
  canvas->set_quick_select_size(20);
  canvas->set_selection_feather_radius(0);

  const auto widget_point = [canvas](QPoint document_point) {
    return canvas->widget_position_for_document_point(document_point);
  };
  send_mouse(*canvas, QEvent::MouseButtonPress, widget_point(QPoint(140, 118)), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, widget_point(QPoint(165, 120)), Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, widget_point(QPoint(185, 120)), Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point(QPoint(185, 120)), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  const auto region = canvas->selected_document_region();
  CHECK(region.contains(QPoint(160, 120)));   // eye interior selected
  CHECK(!region.contains(QPoint(40, 40)));    // skin corners stay unselected
  CHECK(!region.contains(QPoint(280, 200)));
  CHECK(!region.contains(QPoint(160, 30)));   // skin above the eye stays unselected
  CHECK(!region.contains(QPoint(160, 210)));  // and below
  save_widget_artifact("ui_quick_select_photo_texture", *canvas);
}

// GitHub issue 428: Alt+right-drag sizes the Quick Select brush like the Brush's gesture,
// size only (the tool has no softness), with the size readout and the options bar in step.
void ui_quick_select_alt_right_drag_sets_size() {
  SettingsValueRestorer size_restorer(QStringLiteral("tools/quickSelectSize"));
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_view_zoom_centered(1.0);
  QApplication::processEvents();
  auto* size_spin = window.findChild<QSpinBox*>(QStringLiteral("quickSelectSizeSpin"));
  CHECK(size_spin != nullptr);
  size_spin->setValue(30);
  canvas->set_tool(patchy::ui::CanvasTool::QuickSelect);
  QApplication::processEvents();
  CHECK(canvas->quick_select_size() == 30);
  const auto brush_size = canvas->brush_size();
  const auto brush_softness = canvas->brush_softness();
  const auto origin = canvas->rect().center();
  send_mouse(*canvas, QEvent::MouseButtonPress, origin, Qt::RightButton, Qt::RightButton, Qt::AltModifier);
  send_mouse(*canvas, QEvent::MouseMove, origin + QPoint(20, -40), Qt::NoButton, Qt::RightButton, Qt::AltModifier);
  QApplication::processEvents();
  CHECK(canvas->quick_select_size() == 70);
  CHECK(canvas->brush_size() == brush_size);
  CHECK(canvas->brush_softness() == brush_softness);
  save_widget_artifact("issue428_quick_select_size_readout", *canvas);
  send_mouse(*canvas, QEvent::MouseButtonRelease, origin + QPoint(20, -40), Qt::RightButton, Qt::NoButton,
             Qt::AltModifier);
  QApplication::processEvents();
  CHECK(canvas->quick_select_size() == 70);
  CHECK(size_spin->value() == 70);
  CHECK(!canvas->has_selection());

  // Escape mid-drag puts the size back.
  send_mouse(*canvas, QEvent::MouseButtonPress, origin, Qt::RightButton, Qt::RightButton, Qt::AltModifier);
  send_mouse(*canvas, QEvent::MouseMove, origin + QPoint(-10, 0), Qt::NoButton, Qt::RightButton, Qt::AltModifier);
  CHECK(canvas->quick_select_size() == 50);
  send_key(*canvas, Qt::Key_Escape);
  CHECK(canvas->quick_select_size() == 70);
  send_mouse(*canvas, QEvent::MouseButtonRelease, origin + QPoint(-10, 0), Qt::RightButton, Qt::NoButton,
             Qt::AltModifier);
  CHECK(canvas->quick_select_size() == 70);
  CHECK(size_spin->value() == 70);
}

void ui_quick_select_options_persist_across_windows() {
  SettingsValueRestorer size_restorer(QStringLiteral("tools/quickSelectSize"));
  SettingsValueRestorer sample_restorer(QStringLiteral("tools/quickSelectSampleAllLayers"));
  SettingsValueRestorer enhance_restorer(QStringLiteral("tools/quickSelectEnhanceEdge"));
  {
    patchy::ui::MainWindow window;
    show_window(window);
    require_canvas(window);
    auto* size_spin = window.findChild<QSpinBox*>(QStringLiteral("quickSelectSizeSpin"));
    CHECK(size_spin != nullptr);
    size_spin->setValue(77);
    auto* sample_check = window.findChild<QCheckBox*>(QStringLiteral("quickSelectSampleAllLayersCheck"));
    CHECK(sample_check != nullptr);
    sample_check->setChecked(true);
    auto* enhance_check = window.findChild<QCheckBox*>(QStringLiteral("quickSelectEnhanceEdgeCheck"));
    CHECK(enhance_check != nullptr);
    enhance_check->setChecked(true);  // checkbox toggles run an immediate save_tool_settings()
    QApplication::processEvents();
  }
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  CHECK(canvas->quick_select_size() == 77);
  CHECK(canvas->quick_select_sample_all_layers());
  CHECK(canvas->quick_select_enhance_edge());
}

void ui_magic_wand_sample_all_layers_clear_transparent_active_layer_is_noop() {
  patchy::Document document(160, 120, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("White background",
                           solid_pixels(160, 120, patchy::PixelFormat::rgb8(), QColor(Qt::white)));
  auto active_pixels = solid_pixels(160, 120, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(active_pixels, QRect(48, 36, 36, 28), QColor(35, 95, 220, 255));
  document.add_pixel_layer("Active art", std::move(active_pixels));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Magic Wand Delete Noop"));
  QApplication::processEvents();

  auto* canvas = require_canvas(window);
  auto* undo_action = require_action_by_text(window, QStringLiteral("Undo"));
  CHECK(!undo_action->isEnabled());

  canvas->set_tool(patchy::ui::CanvasTool::MagicWand);
  canvas->set_wand_tolerance(0);
  canvas->set_wand_contiguous(true);
  canvas->set_wand_sample_all_layers(true);
  const auto click_point = canvas->widget_position_for_document_point(QPoint(8, 8));
  send_mouse(*canvas, QEvent::MouseButtonPress, click_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, click_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(canvas->has_selection());
  CHECK(canvas->selected_document_region().contains(QPoint(8, 8)));
  CHECK(!canvas->selected_document_region().contains(QPoint(55, 44)));
  // The wand selection itself is an undoable step.
  CHECK(undo_action->isEnabled());

  require_action(window, "layerClearAction")->trigger();
  QApplication::processEvents();

  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("Nothing to clear")));
  // The no-op clear added no history: undoing once reverts the wand selection
  // and leaves the stack empty, and the pixels were never touched.
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(!canvas->has_selection());
  CHECK(!undo_action->isEnabled());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(55, 44)), QColor(35, 95, 220), 8));
}

void ui_magic_wand_complex_selection_is_responsive() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(0, 0, 0));
  canvas->set_brush_size(10);
  for (int y = 80; y <= 240; y += 40) {
    for (int x = 90; x <= 290; x += 40) {
      const auto point = canvas->widget_position_for_document_point(QPoint(x, y));
      drag(*canvas, point, point + QPoint(1, 1));
    }
  }
  QApplication::processEvents();

  canvas->set_tool(patchy::ui::CanvasTool::MagicWand);
  QElapsedTimer timer;
  timer.start();
  const auto click_point = canvas->widget_position_for_document_point(QPoint(25, 25));
  send_mouse(*canvas, QEvent::MouseButtonPress, click_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, click_point, Qt::LeftButton, Qt::NoButton);
  const auto elapsed_ms = timer.elapsed();
  CHECK(elapsed_ms < 1500);
  CHECK(canvas->selected_document_region().contains(QPoint(25, 25)));
  CHECK(!canvas->selected_document_region().contains(QPoint(90, 80)));
  save_widget_artifact("ui_magic_wand_complex_selection", *canvas);
}

void ui_bundled_legacy_plugin_action_applies_filter() {
#ifndef Q_OS_WIN
  // The bundled shim plug-ins are Windows PE binaries; the probe rejects them off-Windows
  // (legacy 8BF support is Windows-only), so no legacyPluginAction exists to trigger.
  patchy::test::skip("bundled legacy 8BF plug-ins are Windows-only");
#else
  require_filter_foundry_fixtures();
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(255, 0, 0));
  canvas->set_brush_size(24);
  const QPoint sample_document_point(72, 72);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(60, 72)),
       canvas->widget_position_for_document_point(QPoint(95, 72)));
  QApplication::processEvents();

  const auto sample_widget_point = canvas->widget_position_for_document_point(sample_document_point);
  const auto before = canvas->grab().toImage().pixelColor(sample_widget_point);
  CHECK(before.red() > 180);
  CHECK(before.green() < 100);
  CHECK(before.blue() < 100);

  wait_for_legacy_plugin_scan(window);
  // The menu path runs the Parameters selector with the real window as the
  // plug-in's owner. The bundled 64-bit Greyscale (the one the menu shows,
  // standing for both bitnesses) cannot take that: Filter Foundry's 64-bit
  // standalone build tries a dialog it does not carry and shows a
  // "DialogBoxParam failed" box that waits for a click. A folder holding only
  // a 32-bit copy gives the menu path a fixture whose Parameters is silent.
  const auto menu_dir = QDir::current().filePath(QStringLiteral("test-artifacts/legacy-plugins/menu32-bundled"));
  remove_test_scratch_dir(menu_dir);
  CHECK(QDir().mkpath(menu_dir));
  CHECK(QFile::copy(patchy::ui::to_qstring(patchy::test::filter_foundry_fixture_path("Greyscale.8bf")),
                    menu_dir + QStringLiteral("/Bundled Grey32.8bf")));
  {
    auto& host = window.script_engine_host();
    patchy::ui::ScriptEngineHost::RunOptions options;
    options.name = QStringLiteral("bundled-plugin-test");
    (void)host.run_source(QStringLiteral("patchy.plugins.folders = ['%1'];").arg(menu_dir), std::move(options));
    QElapsedTimer timer;
    timer.start();
    while (host.run_active() && timer.elapsed() < 60000) {
      QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    CHECK(!host.last_run_had_error());
  }
  QAction* greyscale = nullptr;
  for (auto* action : window.findChildren<QAction*>(QStringLiteral("legacyPluginAction"))) {
    if (action->data().toString() == QStringLiteral("legacy.photoshop.Bundled Grey32")) {
      greyscale = action;
      break;
    }
  }
  CHECK(greyscale != nullptr);
  if (greyscale == nullptr) {
    return;
  }
  greyscale->trigger();
  QApplication::processEvents();

  const auto after = canvas->grab().toImage().pixelColor(sample_widget_point);
  CHECK(std::abs(after.red() - after.green()) <= 8);
  CHECK(std::abs(after.green() - after.blue()) <= 8);
  CHECK(after.red() > 45);
  CHECK(after.red() < 120);
  save_widget_artifact("ui_legacy_plugin_greyscale", window);
#endif
}

}  // namespace

std::vector<patchy::test::TestCase> selection_engines_tests() {
  return {
      {"ui_gradient_and_magic_wand_render_visually", ui_gradient_and_magic_wand_render_visually},
      {"ui_radial_gradient_tool_renders_custom_transparency",
       ui_radial_gradient_tool_renders_custom_transparency},
      {"ui_magic_wand_contiguous_and_sample_all_layers_options_work",
       ui_magic_wand_contiguous_and_sample_all_layers_options_work},
      {"ui_magic_wand_sample_size_averages_the_seed", ui_magic_wand_sample_size_averages_the_seed},
      {"ui_quick_mask_feathered_round_trip_is_exact_and_temporary",
       ui_quick_mask_feathered_round_trip_is_exact_and_temporary},
      {"ui_quick_mask_overlay_channels_and_tab_state_are_temporary",
       ui_quick_mask_overlay_channels_and_tab_state_are_temporary},
      {"ui_quick_mask_brush_gestures_edit_outside_selection_with_selection_history",
       ui_quick_mask_brush_gestures_edit_outside_selection_with_selection_history},
      {"ui_quick_mask_fill_clear_invert_and_blocked_tools_are_selection_only",
       ui_quick_mask_fill_clear_invert_and_blocked_tools_are_selection_only},
      {"ui_quick_select_stroke_selects_object_and_is_undoable",
       ui_quick_select_stroke_selects_object_and_is_undoable},
      {"ui_quick_select_add_and_subtract_strokes", ui_quick_select_add_and_subtract_strokes},
      {"ui_magnetic_lasso_traces_edge_and_commits_selection",
       ui_magnetic_lasso_traces_edge_and_commits_selection},
      {"ui_magnetic_lasso_backspace_escape_and_enter", ui_magnetic_lasso_backspace_escape_and_enter},
      {"ui_magnetic_lasso_delete_and_backspace_pop_anchors_not_layer_clear",
       ui_magnetic_lasso_delete_and_backspace_pop_anchors_not_layer_clear},
      {"ui_magnetic_lasso_anchor_density_follows_zoom", ui_magnetic_lasso_anchor_density_follows_zoom},
      {"ui_magnetic_lasso_enter_closes_along_edges", ui_magnetic_lasso_enter_closes_along_edges},
      {"ui_magnetic_lasso_line_trace_double_click_closes_straight",
       ui_magnetic_lasso_line_trace_double_click_closes_straight},
      {"ui_magnetic_lasso_antialias_clear_leaves_partial_edge_pixels",
       ui_magnetic_lasso_antialias_clear_leaves_partial_edge_pixels},
      {"ui_magnetic_lasso_click_near_start_closes", ui_magnetic_lasso_click_near_start_closes},
      {"ui_magnetic_lasso_options_apply_to_new_documents", ui_magnetic_lasso_options_apply_to_new_documents},
      {"ui_polygonal_lasso_places_points_and_closes", ui_polygonal_lasso_places_points_and_closes},
      {"ui_polygonal_lasso_shift_keeps_45_degrees", ui_polygonal_lasso_shift_keeps_45_degrees},
      {"ui_polygonal_lasso_backspace_delete_and_escape", ui_polygonal_lasso_backspace_delete_and_escape},
      {"ui_polygonal_lasso_combine_modes", ui_polygonal_lasso_combine_modes},
      {"ui_polygonal_lasso_feather_and_antialias", ui_polygonal_lasso_feather_and_antialias},
      {"ui_polygonal_lasso_is_one_undo_step", ui_polygonal_lasso_is_one_undo_step},
      {"ui_polygonal_lasso_alt_drag_adds_a_freehand_run", ui_polygonal_lasso_alt_drag_adds_a_freehand_run},
      {"ui_lasso_alt_release_switches_to_straight_segments", ui_lasso_alt_release_switches_to_straight_segments},
      {"ui_polygonal_lasso_issue_shots", ui_polygonal_lasso_issue_shots},
      {"ui_selection_feather_and_antialias_are_per_tool", ui_selection_feather_and_antialias_are_per_tool},
      {"ui_selection_edge_settings_persist_across_windows", ui_selection_edge_settings_persist_across_windows},
      {"ui_quick_select_photo_texture_selects_eye_not_face", ui_quick_select_photo_texture_selects_eye_not_face},
      {"ui_quick_select_alt_right_drag_sets_size", ui_quick_select_alt_right_drag_sets_size},
      {"ui_quick_select_options_persist_across_windows", ui_quick_select_options_persist_across_windows},
      {"ui_magic_wand_sample_all_layers_clear_transparent_active_layer_is_noop",
       ui_magic_wand_sample_all_layers_clear_transparent_active_layer_is_noop},
      {"ui_magic_wand_complex_selection_is_responsive", ui_magic_wand_complex_selection_is_responsive},
      {"ui_bundled_legacy_plugin_action_applies_filter", ui_bundled_legacy_plugin_action_applies_filter},
  };
}
