#include "ui/action_icons.hpp"
#include "ui/canvas_widget.hpp"
#include "core/adjustment_layer.hpp"
#include "core/contour_presets.hpp"
#include "core/gradient_presets.hpp"
#include "core/layer_metadata.hpp"
#include "core/pattern_presets.hpp"
#include "core/smart_filter.hpp"
#include "core/smart_filter_effects.hpp"
#include "core/smart_object.hpp"
#include "core/text_warp.hpp"
#include "ui/smart_object_render.hpp"
#include "core/layer_tree.hpp"
#include "core/palette.hpp"
#include "core/palette_presets.hpp"
#include "ui/palette_panel.hpp"
#include "ui/pattern_library.hpp"
#include "ui/pattern_manager_dialog.hpp"
#include "ui/photo_pattern_presets.hpp"
#include "ui/style_browser.hpp"
#include "ui/style_library.hpp"
#include "ui/style_manager_dialog.hpp"
#include "psd/asl_io.hpp"
#include "psd/psd_binary.hpp"
#include "psd/psd_layer_effects.hpp"
#include "core/style_presets.hpp"
#include "ui/brush_tip_library.hpp"
#include "ui/brush_tip_manager_dialog.hpp"
#include "ui/brush_tip_picker.hpp"
#include "ui/blend_if_range_editor.hpp"
#include "ui/color_panel.hpp"
#include "ui/default_brush_tips.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/document_float_window.hpp"
#include "ui/compatibility_report.hpp"
#include "ui/curves_editor.hpp"
#include "ui/curves_presets.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/filter_look_library.hpp"
#include "ui/font_picker.hpp"
#include "ui/gradient_stops_editor.hpp"
#include "ui/gradient_library.hpp"
#include "ui/gradient_manager_dialog.hpp"
#include "formats/acv_curves_io.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/ico_document_io.hpp"
#include "formats/tga_document_io.hpp"
#include "ui/image_document_io.hpp"
#include "ui/image_save_options_dialog.hpp"
#include "ui/layer_list_widget.hpp"
#include "ui/layer_style_dialog.hpp"
#include "ui/localization.hpp"
#include "ui/main_window.hpp"
#include "ui/print_dialog.hpp"
#include "ui/selection_outline.hpp"
#include "ui/sprite_sheet_dialog.hpp"
#include "ui/splash_dialog.hpp"
#include "ui/start_panel.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/icon_theme.hpp"
#include "ui/theme_palette.hpp"
#include "ui/theme_qss.hpp"
#include "ui/app_settings.hpp"
#include "ui/build_info.hpp"
#include "ui/update_checker.hpp"
#include "ui/visual_filter_gallery_dialog.hpp"
#include "ui/zoomable_image_preview.hpp"
#include "ui/zoom_status_bar.hpp"
#include "filters/builtin_filters.hpp"
#include "psd/psd_document_io.hpp"
#include "psd/psd_filter_effects.hpp"
#include "render/compositor.hpp"
#include "synthetic_dng.hpp"
#include "test_fonts.hpp"
#include "test_harness.hpp"
#include "local_psd_fixtures.hpp"

#include <QAbstractItemModel>
#include <QAbstractSpinBox>
#include <QAbstractItemView>
#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QByteArray>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDataStream>
#include <QDockWidget>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFrame>
#include <QGroupBox>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QInputDevice>
#include <QInputDialog>
#include <QKeyEvent>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QListView>
#include <QLayout>
#include <QListWidget>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QLocale>
#include <QSizeGrip>
#include <QMetaObject>
#include <QMouseEvent>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QMessageBox>
#include <QIODevice>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPolygonF>
#include <QThread>
#include <QPaintEvent>
#include <QPixmap>
#include <QPointingDevice>
#include <QProgressDialog>
#include <QPushButton>
#include <QStackedWidget>
#include <QRadioButton>
#include <QSpinBox>
#include <QStringList>
#include <QScrollBar>
#include <QScreen>
#include <QSet>
#include <QSettings>
#include <QSlider>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QStyleOptionSpinBox>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTabletEvent>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextLayout>
#include <QTimer>
#include <QAbstractButton>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVariant>
#include <QWheelEvent>
#include <QWindow>
#include <QWidget>

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

void ui_main_window_renders_color_controls() {
  patchy::ui::MainWindow window;
  show_window(window);

  auto* foreground = window.findChild<QPushButton*>(QStringLiteral("foregroundColorButton"));
  auto* background = window.findChild<QPushButton*>(QStringLiteral("backgroundColorButton"));
  CHECK(foreground != nullptr);
  CHECK(background != nullptr);
  CHECK(foreground->text() == QStringLiteral("FG"));
  CHECK(background->text() == QStringLiteral("BG"));
  CHECK(!foreground->text().contains('#'));
  CHECK(!background->text().contains('#'));
  CHECK(window.findChild<QDockWidget*>(QStringLiteral("swatchesDock")) == nullptr);
  CHECK(window.findChild<QToolButton*>(QStringLiteral("swatchesDockCollapseButton")) == nullptr);
  CHECK(window.findChildren<QPushButton*>(QStringLiteral("swatchButton")).isEmpty());
  CHECK(window.findChild<QDockWidget*>(QStringLiteral("paletteDock")) != nullptr);
  const QStringList expected_menus = {QStringLiteral("File"),   QStringLiteral("Edit"),   QStringLiteral("Image"),
                                      QStringLiteral("Layer"),  QStringLiteral("Type"),   QStringLiteral("Select"),
                                      QStringLiteral("Filter"), QStringLiteral("Plugins"), QStringLiteral("View"),
                                      QStringLiteral("Window"), QStringLiteral("Help")};
  QStringList actual_menus;
  for (auto* action : window.menuBar()->actions()) {
    actual_menus << action->text().remove('&');
  }
  CHECK(actual_menus == expected_menus);
  CHECK(window.menuBar()->height() >= 30);
  // Frameless + custom chrome (badge, window buttons) exist only where Patchy draws its
  // own frame (Windows); macOS/Linux use the native frame and must NOT have them.
  CHECK(window.windowFlags().testFlag(Qt::FramelessWindowHint) ==
        patchy::ui::MainWindow::use_custom_window_chrome());
  auto* app_badge = window.menuBar()->findChild<QLabel*>(QStringLiteral("patchyBadge"));
  auto* window_close = window.findChild<QToolButton*>(QStringLiteral("windowCloseButton"));
  if (patchy::ui::MainWindow::use_custom_window_chrome()) {
    CHECK(app_badge != nullptr);
    CHECK(app_badge->pixmap(Qt::ReturnByValue).isNull() == false);
    CHECK(window_close != nullptr);
    CHECK(window_close->mapTo(&window, QPoint(window_close->width(), 0)).x() >= window.width() - 1);
  } else {
    CHECK(app_badge == nullptr);
    CHECK(window_close == nullptr);
  }
  CHECK(window.findChild<QAction*>(QStringLiteral("workspaceHomeAction")) == nullptr);
  CHECK(window.findChild<QAction*>(QStringLiteral("helpHomepageAction")) == nullptr);
  auto* recent_menu = window.findChild<QMenu*>(QStringLiteral("fileOpenRecentMenu"));
  CHECK(recent_menu != nullptr);
  auto* filter_menu = window.findChild<QMenu*>(QStringLiteral("filterMenu"));
  CHECK(filter_menu != nullptr);
  QStringList filter_action_texts;
  const std::function<void(QMenu*)> collect_filter_actions = [&](QMenu* menu) {
    for (auto* action : menu->actions()) {
      if (action->isSeparator()) {
        continue;
      }
      if (auto* submenu = action->menu(); submenu != nullptr) {
        collect_filter_actions(submenu);
        continue;
      }
      auto text = action->text();
      text.remove('&');
      filter_action_texts << text;
    }
  };
  collect_filter_actions(filter_menu);
  CHECK(filter_action_texts.contains(QStringLiteral("Soft Glow")));
  CHECK(filter_action_texts.contains(QStringLiteral("Punchy Color")));
  CHECK(filter_action_texts.contains(QStringLiteral("Noir")));
  CHECK(filter_action_texts.contains(QStringLiteral("Cinematic Matte")));
  CHECK(filter_action_texts.contains(QStringLiteral("Vintage Fade")));
  CHECK(filter_action_texts.contains(QStringLiteral("Twirl")));
  CHECK(filter_action_texts.contains(QStringLiteral("Clouds")));
  CHECK(filter_action_texts.contains(QStringLiteral("Pixel Mosaic")));
  CHECK(filter_action_texts.contains(QStringLiteral("Unsharp Mask")));
  CHECK(filter_action_texts.contains(QStringLiteral("Motion Blur")));
  CHECK(filter_action_texts.contains(QStringLiteral("Color Halftone")));
  CHECK(!filter_action_texts.contains(QStringLiteral("Brightness +24")));
  CHECK(!filter_action_texts.contains(QStringLiteral("Contrast +25%")));
  CHECK(!filter_action_texts.contains(QStringLiteral("Brightness")));
  CHECK(!filter_action_texts.contains(QStringLiteral("Contrast")));
  CHECK(!filter_action_texts.contains(QStringLiteral("Auto Tone")));
  CHECK(!filter_action_texts.contains(QStringLiteral("Auto Contrast")));
  CHECK(!filter_action_texts.contains(QStringLiteral("Auto Color")));
  CHECK(!filter_action_texts.contains(QStringLiteral("Desaturate")));
  for (auto* action : filter_menu->actions()) {
    CHECK(!action->isIconVisibleInMenu());
  }
  auto* adjustments_menu = window.findChild<QMenu*>(QStringLiteral("imageAdjustmentsMenu"));
  CHECK(adjustments_menu != nullptr);
  QStringList adjustment_action_texts;
  for (auto* action : adjustments_menu->actions()) {
    CHECK(!action->isIconVisibleInMenu());
    if (!action->isSeparator()) {
      auto text = action->text();
      text.remove('&');
      adjustment_action_texts << text;
    }
  }
  CHECK(adjustment_action_texts.contains(QStringLiteral("Brightness/Contrast...")));
  CHECK(adjustment_action_texts.contains(QStringLiteral("Auto Tone")));
  CHECK(adjustment_action_texts.contains(QStringLiteral("Auto Contrast")));
  CHECK(adjustment_action_texts.contains(QStringLiteral("Auto Color")));
  CHECK(!adjustment_action_texts.contains(QStringLiteral("Brightness...")));
  CHECK(!adjustment_action_texts.contains(QStringLiteral("Contrast...")));
  CHECK(window.findChild<QAction*>(QStringLiteral("imageAdjustBrightnessAction")) == nullptr);
  CHECK(window.findChild<QAction*>(QStringLiteral("imageAdjustContrastAction")) == nullptr);
  auto* new_adjustments_menu = window.findChild<QMenu*>(QStringLiteral("layerNewAdjustmentMenu"));
  CHECK(new_adjustments_menu != nullptr);
  CHECK(require_action(window, "layerNewLevelsAdjustmentAction") != nullptr);
  CHECK(require_action(window, "layerNewCurvesAdjustmentAction") != nullptr);
  CHECK(require_action(window, "layerNewHueSaturationAdjustmentAction") != nullptr);
  CHECK(require_action(window, "layerNewColorBalanceAdjustmentAction") != nullptr);
  CHECK(require_action(window, "layerNewInvertAdjustmentAction") != nullptr);
  CHECK(require_action(window, "layerNewPosterizeAdjustmentAction") != nullptr);
  CHECK(require_action(window, "layerNewThresholdAdjustmentAction") != nullptr);
  CHECK(require_action(window, "layerNewBrightnessContrastAdjustmentAction") != nullptr);
  CHECK(window.findChild<QToolButton*>(QStringLiteral("layerNewAdjustmentButton")) != nullptr);
  CHECK(window.findChild<QSpinBox*>(QStringLiteral("selectionFeatherSpin")) != nullptr);
  for (auto* button : window.findChildren<QPushButton*>()) {
    CHECK(button->text() != QStringLiteral("Select and Mask..."));
  }
  auto* options_bar = window.findChild<QToolBar*>(QStringLiteral("Options"));
  CHECK(options_bar != nullptr);
  CHECK(options_bar->height() >= 36);
  auto* tool_palette = window.findChild<QToolBar*>(QStringLiteral("toolPalette"));
  CHECK(tool_palette != nullptr);
  CHECK(tool_palette->width() <= 45);
  auto* marquee_button = window.findChild<QToolButton*>(QStringLiteral("marqueeToolButton"));
  CHECK(marquee_button != nullptr);
  CHECK(marquee_button->menu() != nullptr);
  CHECK(marquee_button->menu()->actions().size() == 2);
  CHECK(marquee_button->defaultAction() == require_action_by_text(window, QStringLiteral("Marquee")));
  auto* shape_button = window.findChild<QToolButton*>(QStringLiteral("shapeToolButton"));
  CHECK(shape_button != nullptr);
  CHECK(shape_button->menu() != nullptr);
  CHECK(shape_button->menu()->actions().size() == 5);  // Line/Rect/Ellipse/Polygon/Custom Shape
  CHECK(shape_button->defaultAction() == require_action_by_text(window, QStringLiteral("Rect")));

  save_widget_artifact("ui_main_window", window);
}

struct ToolPaletteOverflowSetup {
  QToolBar* palette{nullptr};
  QPushButton* foreground{nullptr};
  QPushButton* background{nullptr};
  QToolButton* quick_mask{nullptr};
  QToolButton* extension{nullptr};
};

// Looks up the tool palette widgets, verifies everything fits at a tall
// height, then shrinks the window to a ~40px palette shortage so the tail of
// the bottom cluster overflows into the extension button.
ToolPaletteOverflowSetup shrink_window_until_tool_palette_overflows(patchy::ui::MainWindow& window) {
  ToolPaletteOverflowSetup setup;
  setup.palette = window.findChild<QToolBar*>(QStringLiteral("toolPalette"));
  CHECK(setup.palette != nullptr);
  setup.foreground = window.findChild<QPushButton*>(QStringLiteral("foregroundColorButton"));
  setup.background = window.findChild<QPushButton*>(QStringLiteral("backgroundColorButton"));
  setup.quick_mask = window.findChild<QToolButton*>(QStringLiteral("quickMaskButton"));
  setup.extension = setup.palette->findChild<QToolButton*>(QStringLiteral("qt_toolbar_ext_button"));
  CHECK(setup.foreground != nullptr);
  CHECK(setup.background != nullptr);
  CHECK(setup.quick_mask != nullptr);
  CHECK(setup.extension != nullptr);

  // The chrome above and below the palette (menu bar, options bar, status bar)
  // keeps its height across window resizes, so palette height tracks window
  // height 1:1 and the fitting window height can be derived from the hint.
  const int chrome_height = window.height() - setup.palette->height();
  const int fits_height = setup.palette->sizeHint().height() + chrome_height;

  window.resize(window.width(), fits_height + 24);
  QApplication::processEvents();
  process_events_for(60);
  CHECK(setup.quick_mask->isVisible());
  CHECK(!setup.extension->isVisible());

  const int short_height = fits_height - 40;
  CHECK(short_height > 0);
  window.resize(window.width(), short_height);
  QApplication::processEvents();
  process_events_for(60);
  // A minimum-size clamp refusing the resize would make the overflow
  // assertions vacuous, so pin the height that was actually applied.
  CHECK(window.height() == short_height);
  CHECK(setup.extension->isVisible());
  return setup;
}

void ui_tool_palette_overflow_hides_quick_mask_before_swatches() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto setup = shrink_window_until_tool_palette_overflows(window);

  // Vertical overflow hides items tail-first, and the bottom cluster is
  // ordered so Quick Mask, then Swap/Default, give way before the swatches.
  CHECK(!setup.quick_mask->isVisible());
  CHECK(setup.foreground->isVisible());
  CHECK(setup.background->isVisible());
  CHECK(setup.palette->width() <= 45);
}

void ui_tool_palette_extension_button_expands_palette() {
  patchy::ui::MainWindow window;
  // The expanded geometry is otherwise applied through the ~200ms main-window
  // widget animator; disable it so one event-loop settle suffices.
  window.setAnimated(false);
  show_window(window);
  const auto setup = shrink_window_until_tool_palette_overflows(window);
  CHECK(!setup.quick_mask->isVisible());

  setup.extension->click();
  CHECK(process_events_until([&] { return setup.palette->width() > 45; }));
  CHECK(setup.foreground->isVisible());
  CHECK(setup.background->isVisible());
  CHECK(setup.quick_mask->isVisible());
  save_widget_artifact("ui_tool_palette_overflow_expanded", window);

  // Stock Qt collapses the expansion half a second after the pointer leaves
  // the bar; the palette keeps it open until it is explicitly toggled closed.
  QEvent leave_event(QEvent::Leave);
  QApplication::sendEvent(setup.palette, &leave_event);
  process_events_for(700);
  CHECK(setup.palette->width() > 45);
  CHECK(setup.quick_mask->isVisible());

  setup.extension->click();
  CHECK(process_events_until([&] { return setup.palette->width() <= 45; }));
  CHECK(!setup.quick_mask->isVisible());
  CHECK(setup.foreground->isVisible());
}

void ui_tool_palette_expanded_collapses_after_tool_pick() {
  patchy::ui::MainWindow window;
  window.setAnimated(false);
  show_window(window);
  const auto setup = shrink_window_until_tool_palette_overflows(window);

  setup.extension->click();
  CHECK(process_events_until([&] { return setup.palette->width() > 45; }));

  // Picking a palette item is a completed choice: the expansion closes on its
  // own like a flyout instead of lingering over the canvas.
  auto* eraser_button = qobject_cast<QAbstractButton*>(
      setup.palette->widgetForAction(require_action(window, "toolEraserAction")));
  CHECK(eraser_button != nullptr);
  eraser_button->click();
  CHECK(process_events_until([&] { return setup.palette->width() <= 45; }));
  CHECK(!setup.quick_mask->isVisible());
  CHECK(setup.foreground->isVisible());
}

// Dissolve's dither threshold is a pure function of the document coordinate,
// which is exactly what the dirty-rect patch machinery needs: rendering a
// sub-rect must produce the same pixels as the same region of a full render.
// A stateful or scanline-counted noise field would pass every core unit test
// and still crawl under the canvas as the user pans.
void ui_dissolve_clipped_render_matches_full_render() {
  patchy::Document document(160, 120, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background",
                           solid_pixels(160, 120, patchy::PixelFormat::rgba8(), QColor(Qt::black)));
  auto& top = document.add_pixel_layer(
      "Dissolve", solid_pixels(160, 120, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  top.set_blend_mode(patchy::BlendMode::Dissolve);
  top.set_opacity(0.5F);

  const auto full = patchy::ui::qimage_from_document(document, true);
  const std::array<QRect, 4> patches{QRect(0, 0, 71, 53), QRect(71, 0, 89, 53), QRect(0, 53, 71, 67),
                                     QRect(71, 53, 89, 67)};
  int painted = 0;
  for (const auto& patch : patches) {
    const auto clipped = patchy::ui::qimage_from_document_rect(document, patch, true);
    CHECK(clipped.width() == patch.width());
    CHECK(clipped.height() == patch.height());
    for (int y = 0; y < patch.height(); ++y) {
      for (int x = 0; x < patch.width(); ++x) {
        const auto expected = full.pixelColor(patch.x() + x, patch.y() + y);
        CHECK(clipped.pixelColor(x, y) == expected);
        // All or nothing, never a blended grey.
        CHECK(expected.red() == 0 || expected.red() == 255);
        painted += expected.red() == 255 ? 1 : 0;
      }
    }
  }
  // Roughly half of 160x120 dithered on.
  CHECK(painted > 9000);
  CHECK(painted < 10500);
}

void ui_window_force_refresh_action_rebuilds_cache() {
  patchy::Document document(180, 130, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(180, 130, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::Layer layer(document.allocate_layer_id(), "Blue Block",
                      solid_pixels(44, 28, patchy::PixelFormat::rgba8(), QColor(20, 90, 235)));
  layer.set_bounds(patchy::Rect{42, 38, 44, 28});
  const auto expected = patchy::ui::qimage_from_document(document, true);

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Force Refresh"));
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  auto* action = require_action(window, "windowForceRefreshAction");
  CHECK(action->isEnabled());
  CHECK(action->shortcuts().contains(QKeySequence(Qt::Key_F5)));

  const auto before = canvas->render_cache_diagnostics();
  action->trigger();
  QApplication::processEvents();
  const auto after = canvas->render_cache_diagnostics();
  CHECK(after.full_refreshes == before.full_refreshes + 1);
  CHECK(after.forced_refreshes == before.forced_refreshes + 1);

  CHECK(color_close(canvas_pixel(*canvas, QPoint(48, 44)), expected.pixelColor(48, 44), 0));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(16, 16)), expected.pixelColor(16, 16), 0));
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Forced refresh"));
  save_widget_artifact("ui_window_force_refresh", window);
}

void ui_canvas_ignores_opaque_psd_flat_cache_for_first_paint_transparency() {
  patchy::Document document(40, 30, patchy::PixelFormat::rgb8());
  auto layer_pixels = solid_pixels(40, 30, patchy::PixelFormat::rgba8(), Qt::transparent);
  fill_pixel_rect(layer_pixels, QRect(16, 12, 12, 10), QColor(230, 20, 30, 255));
  document.add_pixel_layer("Transparent Layer", std::move(layer_pixels));

  auto flat_composite = solid_pixels(40, 30, patchy::PixelFormat::rgb8(), Qt::black);
  fill_pixel_rect(flat_composite, QRect(16, 12, 12, 10), QColor(230, 20, 30));
  document.metadata().psd_flat_composite = std::move(flat_composite);

  patchy::ui::CanvasWidget canvas;
  canvas.resize(140, 100);
  canvas.set_document(&document);
  canvas.show();
  QApplication::processEvents();

  CHECK(color_close(canvas_pixel(canvas, QPoint(2, 2)), QColor(188, 188, 188), 1));
  CHECK(color_close(canvas_pixel(canvas, QPoint(14, 2)), QColor(236, 236, 236), 1));
  CHECK(color_close(canvas_pixel(canvas, QPoint(18, 14)), QColor(230, 20, 30), 1));
  CHECK(canvas.render_cache_diagnostics().full_refreshes == 1);
}

void ui_top_menu_items_highlight_on_hover() {
  patchy::ui::MainWindow window;
  show_window(window);

  auto* file_menu = window.menuBar()->actions().front()->menu();
  CHECK(file_menu != nullptr);
  auto* open_action = require_action(window, "fileOpenAction");
  CHECK(file_menu->actions().contains(open_action));

  file_menu->popup(window.mapToGlobal(QPoint(40, 40)));
  QApplication::processEvents();
  const auto open_rect = file_menu->actionGeometry(open_action);
  CHECK(open_rect.isValid());
  const QPoint sample_point(open_rect.left() + 8, open_rect.center().y());
  const auto idle_color = file_menu->grab().toImage().pixelColor(sample_point);

  send_mouse(*file_menu, QEvent::MouseMove, open_rect.center(), Qt::NoButton, Qt::NoButton);
  if (file_menu->activeAction() != open_action) {
    file_menu->setActiveAction(open_action);
    QApplication::processEvents();
  }

  const auto hover_color = file_menu->grab().toImage().pixelColor(sample_point);
  CHECK(color_close(idle_color, QColor(58, 58, 58), 6));
  CHECK(color_close(hover_color, QColor(78, 111, 149), 6));
  CHECK(!color_close(idle_color, hover_color, 10));
  file_menu->close();
}

void ui_save_as_dialog_lists_recent_files() {
  ensure_artifact_dir();
  const auto first_path =
      QFileInfo(QStringLiteral("test-artifacts/recent-save-target.psd")).absoluteFilePath();
  const auto second_path =
      QFileInfo(QStringLiteral("test-artifacts/recent-save-backup.png")).absoluteFilePath();
  {
    QFile first(first_path);
    CHECK(first.open(QIODevice::WriteOnly));
    CHECK(first.write("patchy psd placeholder") > 0);
    QFile second(second_path);
    CHECK(second.open(QIODevice::WriteOnly));
    CHECK(second.write("patchy png placeholder") > 0);
  }

  SettingsValueRestorer recent_files_restorer(QStringLiteral("recentFiles"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("recentFiles"), QStringList{first_path, second_path});
    settings.sync();
  }

  patchy::ui::MainWindow window;
  show_window(window);

  auto* recent_menu = window.findChild<QMenu*>(QStringLiteral("fileOpenRecentMenu"));
  CHECK(recent_menu != nullptr);
  // The filter row leads the menu; file entries follow it.
  CHECK(!recent_menu->actions().isEmpty());
  CHECK(recent_menu->actions().front()->objectName() == QStringLiteral("fileOpenRecentFilterAction"));
  QList<QAction*> file_actions;
  for (auto* action : recent_menu->actions()) {
    if (action != nullptr && !action->isSeparator() && !action->data().toString().isEmpty()) {
      file_actions << action;
    }
  }
  CHECK(file_actions.size() == 2);
  CHECK(file_actions[0]->data().toString() == first_path);
  CHECK(file_actions[0]->text().remove('&') == QStringLiteral("1 %1").arg(QDir::toNativeSeparators(first_path)));
  CHECK(file_actions[1]->data().toString() == second_path);
  CHECK(file_actions[1]->text().remove('&') == QStringLiteral("2 %1").arg(QDir::toNativeSeparators(second_path)));

  auto* save_as_action = require_action(window, "fileSaveAsAction");
  CHECK(save_as_action->shortcut() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));

  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QFileDialog*>(find_top_level_dialog(QStringLiteral("saveAsFileDialog")));
    CHECK(dialog != nullptr);
    auto* combo = dialog->findChild<QComboBox*>(QStringLiteral("saveAsRecentFileNameCombo"));
    CHECK(combo != nullptr);
    CHECK(combo->isEditable());
    CHECK(combo->count() == 2);
    CHECK(combo->itemText(0) == first_path);
    CHECK(combo->itemData(0).toString() == first_path);
    CHECK(combo->itemData(0, Qt::ToolTipRole).toString() == first_path);
    CHECK(combo->itemText(1) == second_path);
    CHECK(combo->itemData(1).toString() == second_path);
    combo->setCurrentIndex(1);
    QApplication::processEvents();
    const auto selected_files = dialog->selectedFiles();
    CHECK(!selected_files.isEmpty());
    CHECK(QFileInfo(selected_files.first()).absoluteFilePath() == second_path);
    saw_dialog = true;
    dialog->reject();
  });
  save_as_action->trigger();
  CHECK(saw_dialog);
}

void ui_open_recent_keeps_two_hundred_files_in_grouped_menu() {
  ensure_artifact_dir();
  QStringList recent_files;
  for (int i = 0; i < 205; ++i) {
    const auto path =
        QFileInfo(QStringLiteral("test-artifacts/recent-file-%1.psd").arg(i, 3, 10, QLatin1Char('0')))
            .absoluteFilePath();
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    CHECK(file.write("patchy recent placeholder") > 0);
    recent_files << path;
  }

  SettingsValueRestorer recent_files_restorer(QStringLiteral("recentFiles"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("recentFiles"), recent_files);
    settings.sync();
  }

  patchy::ui::MainWindow window;
  show_window(window);

  auto* recent_menu = window.findChild<QMenu*>(QStringLiteral("fileOpenRecentMenu"));
  CHECK(recent_menu != nullptr);
  QList<QAction*> direct_file_actions;
  QList<QMenu*> page_menus;
  for (auto* action : recent_menu->actions()) {
    if (action != nullptr && !action->isSeparator() && !action->data().toString().isEmpty()) {
      direct_file_actions << action;
    }
    if (auto* submenu = action == nullptr ? nullptr : action->menu();
        submenu != nullptr && submenu->objectName().startsWith(QStringLiteral("fileOpenRecentRangeMenu"))) {
      page_menus << submenu;
    }
  }
  CHECK(direct_file_actions.size() == 50);
  CHECK(page_menus.size() == 3);
  for (int i = 0; i < direct_file_actions.size(); ++i) {
    CHECK(direct_file_actions[i]->data().toString() == recent_files[i]);
  }
  CHECK(direct_file_actions.front()->text().remove('&') ==
        QStringLiteral("1 %1").arg(QDir::toNativeSeparators(recent_files.front())));
  CHECK(direct_file_actions.back()->text().remove('&') ==
        QStringLiteral("50 %1").arg(QDir::toNativeSeparators(recent_files[49])));
  CHECK(page_menus[0]->title() == QStringLiteral("Recent Files 51-100"));
  CHECK(page_menus[1]->title() == QStringLiteral("Recent Files 101-150"));
  CHECK(page_menus[2]->title() == QStringLiteral("Recent Files 151-200"));

  QStringList all_menu_paths;
  const std::function<void(QMenu*)> collect_file_paths = [&](QMenu* menu) {
    for (auto* action : menu->actions()) {
      if (action == nullptr || action->isSeparator()) {
        continue;
      }
      const auto path = action->data().toString();
      if (!path.isEmpty()) {
        all_menu_paths << path;
      }
      if (auto* submenu = action->menu(); submenu != nullptr) {
        collect_file_paths(submenu);
      }
    }
  };
  collect_file_paths(recent_menu);
  CHECK(all_menu_paths.size() == 200);
  for (int i = 0; i < all_menu_paths.size(); ++i) {
    CHECK(all_menu_paths[i] == recent_files[i]);
  }
  CHECK(!all_menu_paths.contains(recent_files[200]));
  CHECK(recent_menu->actions().contains(require_action(window, "fileClearRecentAction")));

  recent_menu->popup(window.mapToGlobal(QPoint(40, 40)));
  QApplication::processEvents();
  if (auto* screen = QApplication::primaryScreen(); screen != nullptr) {
    CHECK(recent_menu->height() <= screen->availableGeometry().height());
  }
  recent_menu->close();

  auto* first_older_action = page_menus.front()->actions().front();
  CHECK(first_older_action->data().toString() == recent_files[50]);
  CHECK(first_older_action->text().remove('&') ==
        QStringLiteral("51 %1").arg(QDir::toNativeSeparators(recent_files[50])));

  QApplication::clipboard()->clear();
  page_menus.front()->popup(window.mapToGlobal(QPoint(80, 80)));
  QApplication::processEvents();

  bool saw_context_menu = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* menu = qobject_cast<QMenu*>(widget);
      if (menu == nullptr || menu->objectName() != QStringLiteral("recentFileContextMenu")) {
        continue;
      }
      auto* copy_action = find_menu_action_by_text(*menu, QStringLiteral("Copy File Path"));
      CHECK(copy_action != nullptr);
      CHECK(copy_action->objectName() == QStringLiteral("recentFileCopyPathAction"));
      copy_action->trigger();
      menu->close();
      saw_context_menu = true;
      return;
    }
    CHECK(false);
  });

  const auto context_point = page_menus.front()->actionGeometry(first_older_action).center();
  QContextMenuEvent context_event(QContextMenuEvent::Mouse, context_point,
                                  page_menus.front()->mapToGlobal(context_point));
  QApplication::sendEvent(page_menus.front(), &context_event);
  QApplication::processEvents();
  CHECK(saw_context_menu);
  CHECK(QApplication::clipboard()->text() == QDir::toNativeSeparators(recent_files[50]));

  CHECK(QFile::remove(recent_files[50]));
  first_older_action->trigger();
  QApplication::processEvents();
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Recent file is missing"));

  QStringList refreshed_menu_paths;
  const std::function<void(QMenu*)> collect_refreshed_file_paths = [&](QMenu* menu) {
    for (auto* action : menu->actions()) {
      if (action == nullptr || action->isSeparator()) {
        continue;
      }
      const auto path = action->data().toString();
      if (!path.isEmpty()) {
        refreshed_menu_paths << path;
      }
      if (auto* submenu = action->menu(); submenu != nullptr) {
        collect_refreshed_file_paths(submenu);
      }
    }
  };
  collect_refreshed_file_paths(recent_menu);
  CHECK(refreshed_menu_paths.size() == 199);
  CHECK(!refreshed_menu_paths.contains(recent_files[50]));
}

void ui_open_recent_filter_narrows_entries_and_opens_first_match() {
  ensure_artifact_dir();
  QStringList recent_files;
  for (int i = 0; i < 120; ++i) {
    const bool beta = (i % 2) == 1;
    const auto pattern = beta ? QStringLiteral("test-artifacts/recent-filter-beta-%1.png")
                              : QStringLiteral("test-artifacts/recent-filter-alpha-%1.psd");
    const auto path = QFileInfo(pattern.arg(i, 3, 10, QLatin1Char('0'))).absoluteFilePath();
    if (i == 1) {
      // Enter opens this one for real, so it must be a loadable image.
      QImage image(48, 32, QImage::Format_RGB32);
      image.fill(QColor(90, 150, 210));
      CHECK(image.save(path));
    } else {
      QFile file(path);
      CHECK(file.open(QIODevice::WriteOnly));
      CHECK(file.write("patchy recent placeholder") > 0);
    }
    recent_files << path;
  }

  SettingsValueRestorer recent_files_restorer(QStringLiteral("recentFiles"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("recentFiles"), recent_files);
    settings.sync();
  }

  patchy::ui::MainWindow window;
  show_window_empty(window);

  auto* recent_menu = window.findChild<QMenu*>(QStringLiteral("fileOpenRecentMenu"));
  CHECK(recent_menu != nullptr);
  CHECK(!recent_menu->actions().isEmpty());
  auto* filter_action = recent_menu->actions().front();
  CHECK(filter_action->objectName() == QStringLiteral("fileOpenRecentFilterAction"));
  auto* filter_edit = recent_menu->findChild<QLineEdit*>(QStringLiteral("fileOpenRecentFilterEdit"));
  CHECK(filter_edit != nullptr);
  auto* no_matches_action = recent_menu->findChild<QAction*>(QStringLiteral("fileOpenRecentNoMatchesAction"));
  CHECK(no_matches_action != nullptr);

  recent_menu->popup(window.mapToGlobal(QPoint(40, 40)));
  QApplication::processEvents();
  CHECK(filter_edit->text().isEmpty());

  const auto visible_file_actions = [recent_menu] {
    QList<QAction*> actions;
    for (auto* action : recent_menu->actions()) {
      if (action != nullptr && !action->isSeparator() && action->isVisible() &&
          !action->data().toString().isEmpty()) {
        actions << action;
      }
    }
    return actions;
  };
  const auto page_menu_actions = [recent_menu] {
    QList<QAction*> actions;
    for (auto* action : recent_menu->actions()) {
      if (auto* submenu = action == nullptr ? nullptr : action->menu();
          submenu != nullptr && submenu->objectName().startsWith(QStringLiteral("fileOpenRecentRangeMenu"))) {
        actions << action;
      }
    }
    return actions;
  };

  // Baseline: 50 direct rows, two page submenus (51-100, 101-120), Clear visible.
  CHECK(visible_file_actions().size() == 50);
  CHECK(page_menu_actions().size() == 2);
  auto* clear_action = require_action(window, "fileClearRecentAction");
  CHECK(clear_action->isVisible());

  // Typing narrows to matches from the whole list, shown flat.
  filter_edit->setText(QStringLiteral("beta"));
  QApplication::processEvents();
  const auto filtered = visible_file_actions();
  CHECK(filtered.size() == 50);  // 60 beta files, capped at the page size
  QStringList filtered_paths;
  for (auto* action : filtered) {
    CHECK(action->data().toString().contains(QStringLiteral("beta")));
    filtered_paths << action->data().toString();
  }
  CHECK(filtered_paths.contains(recent_files[99]));  // past the 50-row page boundary: results are flat
  CHECK(!filtered_paths.contains(recent_files[0]));
  CHECK(filtered.front()->data().toString() == recent_files[1]);
  CHECK(filtered.front()->text().remove('&') ==
        QStringLiteral("2 %1").arg(QDir::toNativeSeparators(recent_files[1])));
  for (auto* action : page_menu_actions()) {
    CHECK(!action->isVisible());
  }
  CHECK(!clear_action->isVisible());
  CHECK(!no_matches_action->isVisible());

  // Space-separated words AND together, matching case-insensitively.
  filter_edit->setText(QStringLiteral("BETA 003"));
  QApplication::processEvents();
  const auto multi_word = visible_file_actions();
  CHECK(multi_word.size() == 1);
  CHECK(multi_word.front()->data().toString() == recent_files[3]);

  // Arrow keys forward from the edit into the menu's highlight.
  filter_edit->setText(QStringLiteral("beta"));
  QApplication::processEvents();
  send_key(*filter_edit, Qt::Key_Down);
  if (recent_menu->activeAction() == nullptr || recent_menu->activeAction()->data().toString().isEmpty()) {
    send_key(*filter_edit, Qt::Key_Down);  // the filter row itself may take the first highlight
  }
  CHECK(recent_menu->activeAction() != nullptr);
  CHECK(recent_menu->activeAction()->data().toString() == recent_files[1]);

  // No matches: only the disabled placeholder row shows.
  filter_edit->setText(QStringLiteral("zzz-no-such-file"));
  QApplication::processEvents();
  CHECK(visible_file_actions().isEmpty());
  CHECK(no_matches_action->isVisible());
  CHECK(!no_matches_action->isEnabled());

  // Right-click on the filter row must not open the recent-file context menu.
  const auto filter_point = recent_menu->actionGeometry(filter_action).center();
  QContextMenuEvent filter_context_event(QContextMenuEvent::Mouse, filter_point,
                                         recent_menu->mapToGlobal(filter_point));
  QApplication::sendEvent(recent_menu, &filter_context_event);
  QApplication::processEvents();
  CHECK(!top_level_widget_exists(QStringLiteral("recentFileContextMenu")));

  // Clearing the filter restores the paged structure.
  filter_edit->clear();
  QApplication::processEvents();
  CHECK(visible_file_actions().size() == 50);
  CHECK(visible_file_actions().front()->data().toString() == recent_files[0]);
  for (auto* action : page_menu_actions()) {
    CHECK(action->isVisible());
  }
  CHECK(clear_action->isVisible());
  CHECK(!no_matches_action->isVisible());

  // Enter opens the first match (recent_files[1], the real PNG).
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(tabs != nullptr);
  CHECK(tabs->count() == 0);
  filter_edit->setText(QStringLiteral("beta"));
  QApplication::processEvents();
  send_key(*filter_edit, Qt::Key_Return);
  QApplication::processEvents();  // the deferred open fires
  CHECK(!recent_menu->isVisible());
  CHECK(tabs->count() == 1);
  auto* info = window.findChild<QLabel*>(QStringLiteral("documentInfoLabel"));
  CHECK(info != nullptr);
  CHECK(info->text().contains(QStringLiteral("48 x 32 px")));
}

void ui_open_recent_keeps_two_hundred_folders_in_grouped_menu() {
  ensure_artifact_dir();
  QStringList recent_folders;
  for (int i = 0; i < 205; ++i) {
    const auto folder =
        QFileInfo(QStringLiteral("test-artifacts/recent-folder-%1").arg(i, 3, 10, QLatin1Char('0')))
            .absoluteFilePath();
    CHECK(QDir().mkpath(folder));
    recent_folders << folder;
  }

  SettingsValueRestorer recent_folders_restorer(QStringLiteral("recentFolders"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("recentFolders"), recent_folders);
    settings.sync();
  }

  patchy::ui::MainWindow window;
  show_window(window);

  auto* folders_menu = window.findChild<QMenu*>(QStringLiteral("fileOpenRecentFolderMenu"));
  CHECK(folders_menu != nullptr);
  QList<QAction*> direct_folder_actions;
  QList<QMenu*> page_menus;
  for (auto* action : folders_menu->actions()) {
    if (action != nullptr && !action->isSeparator() && !action->data().toString().isEmpty()) {
      direct_folder_actions << action;
    }
    if (auto* submenu = action == nullptr ? nullptr : action->menu();
        submenu != nullptr && submenu->objectName().startsWith(QStringLiteral("fileOpenRecentFolderRangeMenu"))) {
      page_menus << submenu;
    }
  }
  CHECK(direct_folder_actions.size() == 50);
  CHECK(page_menus.size() == 3);
  for (int i = 0; i < direct_folder_actions.size(); ++i) {
    CHECK(direct_folder_actions[i]->data().toString() == recent_folders[i]);
  }
  CHECK(direct_folder_actions.front()->text().remove('&') ==
        QStringLiteral("1 %1").arg(QDir::toNativeSeparators(recent_folders.front())));
  CHECK(page_menus[0]->title() == QStringLiteral("Recent Folders 51-100"));
  CHECK(page_menus[1]->title() == QStringLiteral("Recent Folders 101-150"));
  CHECK(page_menus[2]->title() == QStringLiteral("Recent Folders 151-200"));

  QStringList all_menu_paths;
  const std::function<void(QMenu*)> collect_folder_paths = [&](QMenu* menu) {
    for (auto* action : menu->actions()) {
      if (action == nullptr || action->isSeparator()) {
        continue;
      }
      const auto path = action->data().toString();
      if (!path.isEmpty()) {
        all_menu_paths << path;
      }
      if (auto* submenu = action->menu(); submenu != nullptr) {
        collect_folder_paths(submenu);
      }
    }
  };
  collect_folder_paths(folders_menu);
  CHECK(all_menu_paths.size() == 200);
  for (int i = 0; i < all_menu_paths.size(); ++i) {
    CHECK(all_menu_paths[i] == recent_folders[i]);
  }
  CHECK(!all_menu_paths.contains(recent_folders[200]));
  CHECK(folders_menu->actions().contains(require_action(window, "fileClearRecentFoldersAction")));

  folders_menu->popup(window.mapToGlobal(QPoint(40, 40)));
  QApplication::processEvents();
  if (auto* screen = QApplication::primaryScreen(); screen != nullptr) {
    CHECK(folders_menu->height() <= screen->availableGeometry().height());
  }
  folders_menu->close();

  auto* first_older_action = page_menus.front()->actions().front();
  CHECK(first_older_action->data().toString() == recent_folders[50]);
  CHECK(first_older_action->text().remove('&') ==
        QStringLiteral("51 %1").arg(QDir::toNativeSeparators(recent_folders[50])));

  QApplication::clipboard()->clear();
  page_menus.front()->popup(window.mapToGlobal(QPoint(80, 80)));
  QApplication::processEvents();

  bool saw_context_menu = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* menu = qobject_cast<QMenu*>(widget);
      if (menu == nullptr || menu->objectName() != QStringLiteral("recentFileContextMenu")) {
        continue;
      }
      auto* copy_action = find_menu_action_by_text(*menu, QStringLiteral("Copy Folder Path"));
      CHECK(copy_action != nullptr);
      CHECK(copy_action->objectName() == QStringLiteral("recentFolderCopyPathAction"));
      copy_action->trigger();
      menu->close();
      saw_context_menu = true;
      return;
    }
    CHECK(false);
  });

  const auto context_point = page_menus.front()->actionGeometry(first_older_action).center();
  QContextMenuEvent context_event(QContextMenuEvent::Mouse, context_point,
                                  page_menus.front()->mapToGlobal(context_point));
  QApplication::sendEvent(page_menus.front(), &context_event);
  QApplication::processEvents();
  CHECK(saw_context_menu);
  CHECK(QApplication::clipboard()->text() == QDir::toNativeSeparators(recent_folders[50]));
  page_menus.front()->close();
}

void ui_recent_file_context_menu_copies_path() {
  ensure_artifact_dir();
  const auto first_path = QFileInfo(QStringLiteral("test-artifacts/recent-copy-target.psd")).absoluteFilePath();
  {
    QFile first(first_path);
    CHECK(first.open(QIODevice::WriteOnly));
    CHECK(first.write("patchy psd placeholder") > 0);
  }

  SettingsValueRestorer recent_files_restorer(QStringLiteral("recentFiles"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("recentFiles"), QStringList{first_path});
    settings.sync();
  }

  QApplication::clipboard()->clear();

  patchy::ui::MainWindow window;
  show_window(window);

  auto* recent_menu = window.findChild<QMenu*>(QStringLiteral("fileOpenRecentMenu"));
  CHECK(recent_menu != nullptr);
  CHECK(recent_menu->contextMenuPolicy() == Qt::CustomContextMenu);
  CHECK(!recent_menu->actions().isEmpty());
  // The filter row leads the menu; the file entry is the first action with data.
  QAction* recent_action = nullptr;
  for (auto* action : recent_menu->actions()) {
    if (action != nullptr && !action->data().toString().isEmpty()) {
      recent_action = action;
      break;
    }
  }
  CHECK(recent_action != nullptr);
  CHECK(recent_action->data().toString() == first_path);

  recent_menu->popup(window.mapToGlobal(QPoint(40, 40)));
  QApplication::processEvents();

  bool saw_context_menu = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* menu = qobject_cast<QMenu*>(widget);
      if (menu == nullptr || menu->objectName() != QStringLiteral("recentFileContextMenu")) {
        continue;
      }
      auto* copy_action = find_menu_action_by_text(*menu, QStringLiteral("Copy File Path"));
      CHECK(copy_action != nullptr);
      CHECK(copy_action->objectName() == QStringLiteral("recentFileCopyPathAction"));
      auto* explorer_action = find_menu_action_by_text(*menu, QStringLiteral("Open in File Explorer"));
      CHECK(explorer_action != nullptr);
      CHECK(explorer_action->objectName() == QStringLiteral("recentFileOpenInExplorerAction"));
      copy_action->trigger();
      menu->close();
      saw_context_menu = true;
      return;
    }
    CHECK(false);
  });

  const auto context_point = recent_menu->actionGeometry(recent_action).center();
  QContextMenuEvent context_event(QContextMenuEvent::Mouse, context_point,
                                  recent_menu->mapToGlobal(context_point));
  QApplication::sendEvent(recent_menu, &context_event);
  QApplication::processEvents();

  CHECK(saw_context_menu);
  CHECK(QApplication::clipboard()->text() == QDir::toNativeSeparators(first_path));
  recent_menu->close();
}

void ui_recent_folder_context_menu_copies_path_and_offers_explorer() {
  ensure_artifact_dir();
  const auto folder = QFileInfo(QStringLiteral("test-artifacts/recent-folder-context")).absoluteFilePath();
  CHECK(QDir().mkpath(folder));

  SettingsValueRestorer recent_folders_restorer(QStringLiteral("recentFolders"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("recentFolders"), QStringList{folder});
    settings.sync();
  }

  QApplication::clipboard()->clear();

  patchy::ui::MainWindow window;
  show_window(window);

  auto* folders_menu = window.findChild<QMenu*>(QStringLiteral("fileOpenRecentFolderMenu"));
  CHECK(folders_menu != nullptr);
  CHECK(folders_menu->contextMenuPolicy() == Qt::CustomContextMenu);
  CHECK(!folders_menu->actions().isEmpty());
  auto* folder_action = folders_menu->actions().front();
  CHECK(folder_action->data().toString() == folder);

  folders_menu->popup(window.mapToGlobal(QPoint(40, 40)));
  QApplication::processEvents();

  bool saw_context_menu = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* menu = qobject_cast<QMenu*>(widget);
      if (menu == nullptr || menu->objectName() != QStringLiteral("recentFileContextMenu")) {
        continue;
      }
      auto* copy_action = find_menu_action_by_text(*menu, QStringLiteral("Copy Folder Path"));
      CHECK(copy_action != nullptr);
      CHECK(copy_action->objectName() == QStringLiteral("recentFolderCopyPathAction"));
      // Confirm the Explorer option is present, but do not trigger it (it would spawn explorer.exe).
      auto* explorer_action = find_menu_action_by_text(*menu, QStringLiteral("Open in File Explorer"));
      CHECK(explorer_action != nullptr);
      CHECK(explorer_action->objectName() == QStringLiteral("recentFolderOpenInExplorerAction"));
      copy_action->trigger();
      menu->close();
      saw_context_menu = true;
      return;
    }
    CHECK(false);
  });

  const auto context_point = folders_menu->actionGeometry(folder_action).center();
  QContextMenuEvent context_event(QContextMenuEvent::Mouse, context_point,
                                  folders_menu->mapToGlobal(context_point));
  QApplication::sendEvent(folders_menu, &context_event);
  QApplication::processEvents();

  CHECK(saw_context_menu);
  CHECK(QApplication::clipboard()->text() == QDir::toNativeSeparators(folder));
  folders_menu->close();
}

void ui_save_as_remembers_last_save_directory_between_windows() {
  ensure_artifact_dir();
  const auto remembered_dir = QFileInfo(QStringLiteral("test-artifacts/remembered-save-dir")).absoluteFilePath();
  CHECK(QDir().mkpath(remembered_dir));
  const auto saved_path = QDir(remembered_dir).filePath(QStringLiteral("remembered-save.psd"));
  QFile::remove(saved_path);

  SettingsValueRestorer last_save_directory_restorer(QStringLiteral("lastSaveDirectory"));
  SettingsValueRestorer recent_files_restorer(QStringLiteral("recentFiles"));

  {
    patchy::ui::MainWindow window;
    show_window(window);
    bool saved = false;
    QTimer::singleShot(0, [&] {
      auto* dialog = qobject_cast<QFileDialog*>(find_top_level_dialog(QStringLiteral("saveAsFileDialog")));
      CHECK(dialog != nullptr);
      dialog->setDirectory(remembered_dir);
      dialog->selectFile(saved_path);
      saved = true;
      static_cast<QDialog*>(dialog)->accept();
    });
    require_action(window, "fileSaveAsAction")->trigger();
    CHECK(saved);
    CHECK(QFileInfo::exists(saved_path));
  }

  {
    auto settings = patchy::ui::app_settings();
    CHECK(QFileInfo(settings.value(QStringLiteral("lastSaveDirectory")).toString()).absoluteFilePath() ==
          QFileInfo(remembered_dir).absoluteFilePath());
  }

  patchy::ui::MainWindow next_window;
  show_window(next_window);

  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QFileDialog*>(find_top_level_dialog(QStringLiteral("saveAsFileDialog")));
    CHECK(dialog != nullptr);
    CHECK(QFileInfo(dialog->directory().absolutePath()).absoluteFilePath() ==
          QFileInfo(remembered_dir).absoluteFilePath());
    const auto selected_files = dialog->selectedFiles();
    CHECK(!selected_files.isEmpty());
    CHECK(QFileInfo(selected_files.first()).absolutePath() == QFileInfo(remembered_dir).absoluteFilePath());
    saw_dialog = true;
    dialog->reject();
  });
  require_action(next_window, "fileSaveAsAction")->trigger();
  CHECK(saw_dialog);
}

void ui_open_remembers_last_directory_and_lists_recent_folders() {
  ensure_artifact_dir();
  const auto folder_a = QFileInfo(QStringLiteral("test-artifacts/recent-open-dir-a")).absoluteFilePath();
  const auto folder_b = QFileInfo(QStringLiteral("test-artifacts/recent-open-dir-b")).absoluteFilePath();
  const auto missing_folder = QFileInfo(QStringLiteral("test-artifacts/recent-open-dir-missing")).absoluteFilePath();
  CHECK(QDir().mkpath(folder_a));
  CHECK(QDir().mkpath(folder_b));
  QDir(missing_folder).removeRecursively();

  SettingsValueRestorer last_open_directory_restorer(QStringLiteral("lastOpenDirectory"));
  SettingsValueRestorer recent_folders_restorer(QStringLiteral("recentFolders"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("lastOpenDirectory"), folder_a);
    // Include a stale entry to confirm self-healing drops folders that no longer exist.
    settings.setValue(QStringLiteral("recentFolders"), QStringList{folder_a, missing_folder, folder_b});
    settings.sync();
  }

  patchy::ui::MainWindow window;
  show_window(window);

  auto* folders_menu = window.findChild<QMenu*>(QStringLiteral("fileOpenRecentFolderMenu"));
  CHECK(folders_menu != nullptr);
  QStringList listed_folders;
  for (auto* action : folders_menu->actions()) {
    if (action != nullptr && !action->isSeparator() && !action->data().toString().isEmpty()) {
      listed_folders << action->data().toString();
    }
  }
  CHECK(listed_folders == QStringList({folder_a, folder_b}));
  CHECK(folders_menu->actions().contains(require_action(window, "fileClearRecentFoldersAction")));

  // The Open dialog starts in the remembered directory.
  bool saw_open_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QFileDialog*>(find_top_level_dialog(QStringLiteral("openFileDialog")));
    CHECK(dialog != nullptr);
    CHECK(QFileInfo(dialog->directory().absolutePath()).absoluteFilePath() ==
          QFileInfo(folder_a).absoluteFilePath());
    saw_open_dialog = true;
    dialog->reject();
  });
  require_action(window, "fileOpenAction")->trigger();
  CHECK(saw_open_dialog);

  // Picking a recent folder opens the dialog pointed at that folder.
  bool saw_recent_folder_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QFileDialog*>(find_top_level_dialog(QStringLiteral("openFileDialog")));
    CHECK(dialog != nullptr);
    CHECK(QFileInfo(dialog->directory().absolutePath()).absoluteFilePath() ==
          QFileInfo(folder_b).absoluteFilePath());
    saw_recent_folder_dialog = true;
    dialog->reject();
  });
  listed_folders.clear();
  for (auto* action : folders_menu->actions()) {
    if (action != nullptr && action->data().toString() == folder_b) {
      action->trigger();
      break;
    }
  }
  CHECK(saw_recent_folder_dialog);

  // Clearing empties both the menu and the persisted list.
  require_action(window, "fileClearRecentFoldersAction")->trigger();
  QApplication::processEvents();
  for (auto* action : folders_menu->actions()) {
    CHECK(action->data().toString().isEmpty());
  }
  CHECK(!folders_menu->isEnabled());
  {
    auto settings = patchy::ui::app_settings();
    CHECK(settings.value(QStringLiteral("recentFolders")).toStringList().isEmpty());
  }
}

void ui_open_dialog_hides_name_filter_details() {
  patchy::ui::MainWindow window;
  show_window(window);

  // The Open dialog lists one row per format so every supported filetype is readable;
  // the all-formats row alone summarizes (its ~50 patterns overflow the native
  // dropdown). The dialog runs with HideNameFilterDetails and each row embeds its
  // visible patterns in the display name ("TIFF Image (*.tif *.tiff)"): a "*." token
  // must survive stripping or the Windows 11 dialog re-appends the full pattern spec.
  bool saw_open_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QFileDialog*>(find_top_level_dialog(QStringLiteral("openFileDialog")));
    CHECK(dialog != nullptr);
    CHECK(dialog->testOption(QFileDialog::HideNameFilterDetails));
    const auto filters = dialog->nameFilters();
    CHECK(!filters.isEmpty());
    CHECK(filters.first().contains(QStringLiteral("*.psd")));
    CHECK(filters.first().contains(QStringLiteral("*.tif")));
    auto* type_combo = dialog->findChild<QComboBox*>(QStringLiteral("fileTypeCombo"));
    CHECK(type_combo != nullptr);
    QStringList displayed;
    for (int i = 0; i < type_combo->count(); ++i) {
      displayed.push_back(type_combo->itemText(i));
    }
    CHECK(displayed.size() > 10);
    CHECK(displayed.first().contains(QStringLiteral("*.psd")));
    CHECK(!displayed.first().contains(QStringLiteral("*.tif")));
    CHECK(displayed.contains(QStringLiteral("Photoshop Document (*.psd *.psb)")));
    CHECK(displayed.contains(QStringLiteral("TIFF Image (*.tif *.tiff)")));
    CHECK(displayed.contains(QStringLiteral("Affinity Document (*.af *.afphoto *.afdesign *.afpub)")));
    CHECK(displayed.last() == QStringLiteral("All Files (*.*)"));
    saw_open_dialog = true;
    dialog->reject();
  });
  require_action(window, "fileOpenAction")->trigger();
  CHECK(saw_open_dialog);

  // Save As filters are one short entry per format; their extension details stay visible.
  bool saw_save_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QFileDialog*>(find_top_level_dialog(QStringLiteral("saveAsFileDialog")));
    CHECK(dialog != nullptr);
    CHECK(!dialog->testOption(QFileDialog::HideNameFilterDetails));
    saw_save_dialog = true;
    dialog->reject();
  });
  require_action(window, "fileSaveAsAction")->trigger();
  CHECK(saw_save_dialog);
}

QStringList top_level_menu_texts(QMenuBar& menu_bar) {
  QStringList texts;
  for (auto* action : menu_bar.actions()) {
    texts << action->text().remove('&');
  }
  return texts;
}

void choose_preferences_language(patchy::ui::MainWindow& window, const QString& language_code) {
  auto* preferences = require_action(window, "filePreferencesAction");
  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    auto* combo = dialog->findChild<QComboBox*>(QStringLiteral("preferencesLanguageCombo"));
    CHECK(combo != nullptr);
    const auto index = combo->findData(language_code);
    CHECK(index >= 0);
    combo->setCurrentIndex(index);
    saw_dialog = true;
    dialog->accept();
  });
  preferences->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);
}

void update_manifest_parser_handles_supported_cases() {
  const QByteArray newer_manifest = R"({
    "platforms": {
      "windows": {
        "version": "0.2",
        "download_url": "https://rtsoft.com/patchy/PatchyWindowsInstaller.exe"
      },
      "macos": {
        "version": "0.3.0",
        "download_url": "https://rtsoft.com/patchy/PatchyMacOS.dmg"
      }
    }
  })";
  const auto update = patchy::ui::parse_update_manifest(newer_manifest, QStringLiteral("windows"),
                                                        QStringLiteral("0.1.0"));
  CHECK(update.has_value());
  CHECK(update->platform == QStringLiteral("windows"));
  CHECK(update->version == QStringLiteral("0.2"));
  CHECK(update->download_url == QUrl(QStringLiteral("https://rtsoft.com/patchy/PatchyWindowsInstaller.exe")));
  const auto update_result =
      patchy::ui::inspect_update_manifest(newer_manifest, QStringLiteral("windows"), QStringLiteral("0.1.0"));
  CHECK(update_result.status == patchy::ui::UpdateCheckStatus::UpdateAvailable);
  CHECK(update_result.update.has_value());
  CHECK(update_result.latest_version == QStringLiteral("0.2"));
  CHECK(!patchy::ui::update_version_is_newer(QStringLiteral("0.2.0"), QStringLiteral("0.2")));
  CHECK(!patchy::ui::update_version_is_newer(QStringLiteral("0.2"), QStringLiteral("0.2.0")));
  CHECK(patchy::ui::update_version_is_newer(QStringLiteral("0.10"), QStringLiteral("0.2")));
  CHECK(!patchy::ui::update_version_is_newer(QStringLiteral("0.1.0"), QStringLiteral("0.1.0")));
  CHECK(!patchy::ui::update_version_is_newer(QStringLiteral("0.0.9"), QStringLiteral("0.1.0")));

  const QByteArray equal_manifest = R"({
    "platforms": {
      "windows": {
        "version": "0.1.0",
        "download_url": "https://rtsoft.com/patchy/PatchyWindowsInstaller.exe"
      }
    }
  })";
  CHECK(!patchy::ui::parse_update_manifest(equal_manifest, QStringLiteral("windows"), QStringLiteral("0.1.0"))
             .has_value());
  const auto equal_result =
      patchy::ui::inspect_update_manifest(equal_manifest, QStringLiteral("windows"), QStringLiteral("0.1.0"));
  CHECK(equal_result.status == patchy::ui::UpdateCheckStatus::NoUpdateAvailable);
  CHECK(equal_result.latest_version == QStringLiteral("0.1.0"));
  const QByteArray lower_manifest = R"({
    "platforms": {
      "windows": {
        "version": "0.0.9",
        "download_url": "https://rtsoft.com/patchy/PatchyWindowsInstaller.exe"
      }
    }
  })";
  CHECK(!patchy::ui::parse_update_manifest(lower_manifest, QStringLiteral("windows"), QStringLiteral("0.1.0"))
             .has_value());
  CHECK(!patchy::ui::parse_update_manifest(newer_manifest, QStringLiteral("linux"), QStringLiteral("0.1.0"))
             .has_value());
  const auto missing_platform_result =
      patchy::ui::inspect_update_manifest(newer_manifest, QStringLiteral("linux"), QStringLiteral("0.1.0"));
  CHECK(missing_platform_result.status == patchy::ui::UpdateCheckStatus::MissingPlatform);
  // A manifest that does carry a linux entry parses for the linux platform id.
  const QByteArray linux_manifest = R"({
    "platforms": {
      "linux": {
        "version": "0.2",
        "download_url": "https://rtsoft.com/patchy/Patchy.flatpak"
      }
    }
  })";
  const auto linux_update =
      patchy::ui::parse_update_manifest(linux_manifest, QStringLiteral("linux"), QStringLiteral("0.1.0"));
  CHECK(linux_update.has_value());
  CHECK(linux_update->platform == QStringLiteral("linux"));
  CHECK(linux_update->download_url == QUrl(QStringLiteral("https://rtsoft.com/patchy/Patchy.flatpak")));
  const auto invalid_manifest_result =
      patchy::ui::inspect_update_manifest(QByteArray("{"), QStringLiteral("windows"), QStringLiteral("0.1.0"));
  CHECK(invalid_manifest_result.status == patchy::ui::UpdateCheckStatus::InvalidManifest);
  CHECK(!patchy::ui::parse_update_manifest(QByteArray("{"), QStringLiteral("windows"), QStringLiteral("0.1.0"))
             .has_value());

  const QByteArray empty_url_manifest = R"({
    "platforms": {
      "windows": {
        "version": "0.2.0",
        "download_url": ""
      }
    }
  })";
  CHECK(!patchy::ui::parse_update_manifest(empty_url_manifest, QStringLiteral("windows"), QStringLiteral("0.1.0"))
             .has_value());
  const auto empty_url_result =
      patchy::ui::inspect_update_manifest(empty_url_manifest, QStringLiteral("windows"), QStringLiteral("0.1.0"));
  CHECK(empty_url_result.status == patchy::ui::UpdateCheckStatus::InvalidDownloadUrl);

  const QByteArray relative_url_manifest = R"({
    "platforms": {
      "windows": {
        "version": "0.2.0",
        "download_url": "PatchyWindowsInstaller.exe"
      }
    }
  })";
  CHECK(!patchy::ui::parse_update_manifest(relative_url_manifest, QStringLiteral("windows"), QStringLiteral("0.1.0"))
             .has_value());
}

void ui_update_available_dialog_warns_to_close_patchy_before_installing() {
  patchy::ui::MainWindow window;
  show_window(window);

  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("updateAvailableMessageBox")));
    CHECK(dialog != nullptr);
    // The install advice is per-platform (installer exe / DMG / Flatpak bundle).
#if defined(Q_OS_MACOS)
    CHECK(dialog->text().contains(QStringLiteral("drag the new Patchy into Applications")));
#elif defined(Q_OS_LINUX)
    CHECK(dialog->text().contains(QStringLiteral("flatpak install")));
    CHECK(dialog->text().contains(QStringLiteral("curl -L -o")));
    CHECK(dialog->findChild<QAbstractButton*>(QStringLiteral("updateCopyCommandButton")) != nullptr);
#else
    CHECK(dialog->text().contains(
        QStringLiteral("Save your work and close Patchy before running the installer.")));
#endif
    saw_dialog = true;
    dialog->reject();
  });

  window.show_update_available({QStringLiteral("windows"), QStringLiteral("9.9"),
                                QUrl(QStringLiteral("https://rtsoft.com/files/PatchyWindowsInstaller.exe"))});
  CHECK(saw_dialog);
}

void ui_update_preference_persists_startup_check_setting() {
  SettingsValueRestorer restore_update_check(QStringLiteral("updates/checkOnStartup"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("updates/checkOnStartup"), false);
    settings.sync();
  }

  patchy::ui::MainWindow window;
  show_window(window);

  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("updates/checkOnStartup"), true);
    settings.sync();
  }

  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    auto* check = dialog->findChild<QCheckBox*>(QStringLiteral("preferencesCheckForUpdatesCheck"));
    CHECK(check != nullptr);
    CHECK(check->isChecked());
    check->setChecked(false);
    saw_dialog = true;
    dialog->accept();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);

  auto settings = patchy::ui::app_settings();
  CHECK(!settings.value(QStringLiteral("updates/checkOnStartup"), true).toBool());
}

void ui_gui_scale_preference_persists_setting() {
  SettingsValueRestorer restore_gui_scale(QStringLiteral("preferences/guiScalePercent"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("preferences/guiScalePercent"), 100);
    settings.sync();
  }

  patchy::ui::MainWindow window;
  show_window(window);

  bool saw_dialog = false;
  bool dismissed_message = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    auto* combo = dialog->findChild<QComboBox*>(QStringLiteral("preferencesGuiScaleCombo"));
    CHECK(combo != nullptr);
    const int index = combo->findData(150);
    CHECK(index >= 0);
    combo->setCurrentIndex(index);
    saw_dialog = true;
    // Accepting with a changed scale shows a modal restart-required message box; dismiss it.
    QTimer::singleShot(0, [&] {
      auto* message = qobject_cast<QMessageBox*>(
          find_top_level_dialog(QStringLiteral("preferencesInterfaceScaleMessageBox")));
      CHECK(message != nullptr);
      if (message != nullptr) {
        message->accept();
        dismissed_message = true;
      }
    });
    dialog->accept();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);
  CHECK(dismissed_message);

  auto settings = patchy::ui::app_settings();
  CHECK(settings.value(QStringLiteral("preferences/guiScalePercent"), 100).toInt() == 150);
}

// Drives the Preferences color-scheme combo and returns the dialog result, so the
// persist and cancel tests differ only in how they leave the dialog.
int choose_preferences_color_scheme(patchy::ui::MainWindow& window, const QString& token,
                                    bool accept) {
  bool saw_dialog = false;
  int result = QDialog::Rejected;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* combo = dialog->findChild<QComboBox*>(QStringLiteral("preferencesColorSchemeCombo"));
    CHECK(combo != nullptr);
    if (combo == nullptr) {
      dialog->reject();
      return;
    }
    const auto index = combo->findData(token);
    CHECK(index >= 0);
    combo->setCurrentIndex(index);
    saw_dialog = true;
    result = accept ? QDialog::Accepted : QDialog::Rejected;
    if (accept) {
      dialog->accept();
    } else {
      dialog->reject();
    }
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);
  return result;
}

void ui_color_scheme_preference_persists_setting() {
  SettingsValueRestorer restore_scheme(QStringLiteral("preferences/colorScheme"));
  ColorSchemeRestorer restore_active;

  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(patchy::ui::active_color_scheme() == patchy::ui::ColorScheme::Dark);

  choose_preferences_color_scheme(window, QStringLiteral("light"), /*accept=*/true);

  auto settings = patchy::ui::app_settings();
  CHECK(settings.value(QStringLiteral("preferences/colorScheme")).toString() ==
        QStringLiteral("light"));
  CHECK(patchy::ui::ThemeManager::instance().preference() == patchy::ui::ColorSchemePreference::Light);
  CHECK(patchy::ui::active_color_scheme() == patchy::ui::ColorScheme::Light);

  // Unlike interface scale, the scheme applies immediately, so no restart notice
  // may appear. A stray modal here would also hang the rest of the suite.
  CHECK(find_top_level_dialog(QStringLiteral("preferencesInterfaceScaleMessageBox")) == nullptr);
}

void ui_color_scheme_cancel_restores_entry_scheme() {
  SettingsValueRestorer restore_scheme(QStringLiteral("preferences/colorScheme"));
  ColorSchemeRestorer restore_active;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);
  // Seeded rather than asserted absent: accepting Preferences persists the scheme
  // like every other setting on that page, so any earlier test that accepted the
  // dialog has already written this key.
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("preferences/colorScheme"), QStringLiteral("dark"));
    settings.sync();
  }

  patchy::ui::MainWindow window;
  show_window(window);

  // The combo previews live; closing without accepting has to undo the preview.
  choose_preferences_color_scheme(window, QStringLiteral("light"), /*accept=*/false);
  QApplication::processEvents();

  CHECK(patchy::ui::ThemeManager::instance().preference() == patchy::ui::ColorSchemePreference::Dark);
  CHECK(patchy::ui::active_color_scheme() == patchy::ui::ColorScheme::Dark);
  auto settings = patchy::ui::app_settings();
  CHECK(settings.value(QStringLiteral("preferences/colorScheme")).toString() == QStringLiteral("dark"));
}

void ui_color_scheme_follow_system_tracks_style_hints() {
  SettingsValueRestorer restore_scheme(QStringLiteral("preferences/colorScheme"));
  ColorSchemeRestorer restore_active;

  auto& manager = patchy::ui::ThemeManager::instance();
  manager.set_preference(patchy::ui::ColorSchemePreference::FollowSystem, /*persist=*/false);

  // The offscreen platform reports Qt::ColorScheme::Unknown and never emits
  // colorSchemeChanged, so the system side is driven through the test seam.
  manager.set_system_color_scheme_for_testing(Qt::ColorScheme::Light);
  CHECK(manager.resolved_scheme() == patchy::ui::ColorScheme::Light);
  CHECK(patchy::ui::active_color_scheme() == patchy::ui::ColorScheme::Light);

  manager.set_system_color_scheme_for_testing(Qt::ColorScheme::Dark);
  CHECK(manager.resolved_scheme() == patchy::ui::ColorScheme::Dark);
  CHECK(patchy::ui::active_color_scheme() == patchy::ui::ColorScheme::Dark);

  // Unknown is what every platform without a color-scheme notion reports; it must
  // land on Patchy's historical Dark rather than an invalid state.
  manager.set_system_color_scheme_for_testing(Qt::ColorScheme::Unknown);
  CHECK(manager.resolved_scheme() == patchy::ui::ColorScheme::Dark);

  // An explicit choice ignores the system entirely.
  manager.set_preference(patchy::ui::ColorSchemePreference::Light, /*persist=*/false);
  manager.set_system_color_scheme_for_testing(Qt::ColorScheme::Dark);
  CHECK(manager.resolved_scheme() == patchy::ui::ColorScheme::Light);
  CHECK(patchy::ui::active_color_scheme() == patchy::ui::ColorScheme::Light);
}

// The regression guard for "live, no restart": an already-built window has to
// restyle in place, and flipping back has to land exactly where it started.
void ui_color_scheme_switch_updates_existing_window() {
  ColorSchemeRestorer restore_active;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);

  patchy::ui::MainWindow window;
  show_window(window);
  QApplication::processEvents();

  const auto dark_sheet = window.styleSheet();
  const auto dark_shot = window.grab().toImage();
  CHECK(dark_sheet.contains(patchy::ui::dark_palette().window_bg.name(QColor::HexRgb)));
  CHECK(!dark_shot.isNull());

  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Light);
  QApplication::processEvents();

  const auto light_sheet = window.styleSheet();
  CHECK(light_sheet != dark_sheet);
  CHECK(light_sheet.contains(patchy::ui::light_palette().window_bg.name(QColor::HexRgb)));
  CHECK(window.grab().toImage() != dark_shot);
  // The light variants of the stylesheet-referenced SVGs are a separate
  // mechanism from the icon engine, and the easiest one to forget.
  CHECK(light_sheet.contains(QStringLiteral("icons/light/scroll-dither.svg")));
  CHECK(dark_sheet.contains(QStringLiteral("icons/scroll-dither.svg")));
  CHECK(!dark_sheet.contains(QStringLiteral("icons/light/")));

  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);
  QApplication::processEvents();
  CHECK(window.styleSheet() == dark_sheet);
}

// Icons resolve their colors when painted, so the SAME QIcon a QAction was given
// at startup must render the new scheme after a flip. Capturing it by value up
// front is the whole point of the assertion: if this ever needs a setIcon call,
// the engine has regressed.
void ui_themed_icons_recolor_between_schemes() {
  ColorSchemeRestorer restore_active;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);

  patchy::ui::MainWindow window;
  show_window(window);

  const QIcon action_icon = require_action(window, "layerNewAction")->icon();
  CHECK(!action_icon.isNull());
  const auto dark_render = action_icon.pixmap(QSize(32, 32)).toImage();
  CHECK(!dark_render.isNull());

  // The paint swatches depict black and white. Their outlines are icon ink and
  // SHOULD recolor so the glyph stays visible on a light toolbar; only the two
  // fills are the subject of the drawing and must not move.
  const QPoint white_fill(23, 23);
  const QPoint black_fill(13, 13);
  const auto dark_swatch =
      patchy::ui::themed_svg_icon(QStringLiteral("default-colors")).pixmap(QSize(32, 32)).toImage();
  CHECK(dark_swatch.pixelColor(white_fill) == QColor(Qt::white));
  CHECK(dark_swatch.pixelColor(black_fill) == QColor(0x11, 0x11, 0x11));

  // Glyphs with no authored SVG go through a second engine that resolves a
  // palette role instead of substituting SVG text. The clipping badge is the one
  // users notice, and it is the reason that overload exists.
  const QIcon clip_icon =
      patchy::ui::simple_icon(QStringLiteral("clip"), &patchy::ui::ThemePalette::layer_clip_badge);
  const auto dark_clip = clip_icon.pixmap(QSize(20, 20)).toImage();
  CHECK(!dark_clip.isNull());

  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Light);

  const auto light_render = action_icon.pixmap(QSize(32, 32)).toImage();
  CHECK(light_render != dark_render);

  const auto ink_luminance = [](const QImage& image) {
    qint64 total = 0;
    qint64 covered = 0;
    for (int y = 0; y < image.height(); ++y) {
      for (int x = 0; x < image.width(); ++x) {
        const auto pixel = image.pixelColor(x, y);
        if (pixel.alpha() < 128) {
          continue;
        }
        total += qGray(pixel.rgb());
        ++covered;
      }
    }
    return covered > 0 ? total / covered : qint64{0};
  };
  // Light-on-dark ink becomes dark-on-light ink.
  CHECK(ink_luminance(light_render) < ink_luminance(dark_render));

  const auto light_swatch =
      patchy::ui::themed_svg_icon(QStringLiteral("default-colors")).pixmap(QSize(32, 32)).toImage();
  CHECK(light_swatch.pixelColor(white_fill) == QColor(Qt::white));
  CHECK(light_swatch.pixelColor(black_fill) == QColor(0x11, 0x11, 0x11));
  // ...while the outline around them did follow the scheme.
  CHECK(light_swatch != dark_swatch);

  // Same QIcon instance, so the role really is resolved at paint time.
  const auto light_clip = clip_icon.pixmap(QSize(20, 20)).toImage();
  CHECK(light_clip != dark_clip);
  CHECK(ink_luminance(light_clip) < ink_luminance(dark_clip));
}

void ui_main_window_persists_window_geometry() {
  SettingsValueRestorer restore_geometry(QStringLiteral("window/normalGeometry"));
  SettingsValueRestorer restore_maximized(QStringLiteral("window/maximized"));
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("window/normalGeometry"));
    settings.remove(QStringLiteral("window/maximized"));
    settings.sync();
  }

  // Derive the target from the available screen so the on-screen clamp performed during restore is
  // an identity operation; otherwise a small offscreen test screen would shrink/move the geometry.
  const QScreen* primary = QApplication::primaryScreen();
  CHECK(primary != nullptr);
  const QRect available = primary->availableGeometry();
  CHECK(available.isValid());
  const QRect target = available.adjusted(20, 20, -120, -100);
  CHECK(target.width() > 0 && target.height() > 0);
  {
    patchy::ui::MainWindow window;
    window.show();
    QApplication::processEvents();
    window.setGeometry(target);
    QApplication::processEvents();
    // Closing a window with no modified documents accepts the close and persists geometry.
    window.close();
    QApplication::processEvents();
  }

  QRect stored;
  {
    auto settings = patchy::ui::app_settings();
    stored = settings.value(QStringLiteral("window/normalGeometry")).toRect();
    CHECK(stored.isValid());
    CHECK(stored.size() == target.size());
    CHECK(!settings.value(QStringLiteral("window/maximized"), false).toBool());
  }

  patchy::ui::MainWindow restored;
  restored.show();
  QApplication::processEvents();
  CHECK(restored.size() == stored.size());
}

void ui_update_preference_defaults_startup_check_setting_to_enabled() {
  SettingsValueRestorer restore_update_check(QStringLiteral("updates/checkOnStartup"));
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("updates/checkOnStartup"));
    settings.sync();
  }

  auto settings = patchy::ui::app_settings();
  CHECK(!settings.contains(QStringLiteral("updates/checkOnStartup")));
  CHECK(settings.value(QStringLiteral("updates/checkOnStartup"), true).toBool());
}

void ui_psd_import_warning_preference_defaults_to_hidden() {
  SettingsValueRestorer restore_psd_warning_check(QStringLiteral("imports/showPsdWarningsAndInfo"));
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("imports/showPsdWarningsAndInfo"));
    settings.sync();
  }

  patchy::ui::MainWindow window;
  show_window(window);

  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    auto* check = dialog->findChild<QCheckBox*>(QStringLiteral("preferencesShowPsdImportWarningsCheck"));
    CHECK(check != nullptr);
    CHECK(check->text() == QStringLiteral("Show import warnings and notes in a popup (status bar otherwise)"));
    CHECK(!check->isChecked());
    saw_dialog = true;
    dialog->accept();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);
}

void ui_psd_import_warning_preference_persists_enabled_setting() {
  SettingsValueRestorer restore_psd_warning_check(QStringLiteral("imports/showPsdWarningsAndInfo"));
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("imports/showPsdWarningsAndInfo"));
    settings.sync();
  }

  patchy::ui::MainWindow window;
  show_window(window);

  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    auto* check = dialog->findChild<QCheckBox*>(QStringLiteral("preferencesShowPsdImportWarningsCheck"));
    CHECK(check != nullptr);
    CHECK(!check->isChecked());
    check->setChecked(true);
    saw_dialog = true;
    dialog->accept();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);

  auto settings = patchy::ui::app_settings();
  CHECK(settings.value(QStringLiteral("imports/showPsdWarningsAndInfo"), false).toBool());
}

void ui_language_switch_updates_existing_window() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  CHECK(tabs != nullptr);
  const auto initial_tab_count = tabs->count();

  choose_preferences_language(window, QStringLiteral("ja"));

  CHECK(patchy::ui::LocalizationManager::instance().current_language() == QStringLiteral("ja"));
  const auto japanese_menus = top_level_menu_texts(*window.menuBar());
  CHECK(japanese_menus.contains(QStringLiteral("ファイル(F)")));
  CHECK(!japanese_menus.contains(QStringLiteral("環境設定(P)")));
  CHECK(tabs->count() == initial_tab_count);
  CHECK(require_action(window, "preferencesLanguageJapaneseAction")->isChecked());

  choose_preferences_language(window, QStringLiteral("en"));

  CHECK(patchy::ui::LocalizationManager::instance().current_language() == QStringLiteral("en"));
  const auto english_menus = top_level_menu_texts(*window.menuBar());
  CHECK(english_menus.contains(QStringLiteral("File")));
  CHECK(!english_menus.contains(QStringLiteral("Preferences")));
  CHECK(require_action(window, "fileSaveAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_S));
  CHECK(tabs->count() == initial_tab_count);
  CHECK(require_action(window, "preferencesLanguageEnglishAction")->isChecked());
}

void ui_language_preference_applies_at_startup() {
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("preferences/language"), QStringLiteral("ja"));
    settings.sync();
  }
  patchy::ui::LocalizationManager::instance().load_saved_language();

  patchy::ui::MainWindow window;
  show_window(window);

  CHECK(patchy::ui::LocalizationManager::instance().current_language() == QStringLiteral("ja"));
  const auto menus = top_level_menu_texts(*window.menuBar());
  CHECK(menus.contains(QStringLiteral("ファイル(F)")));
  CHECK(!menus.contains(QStringLiteral("環境設定(P)")));
  CHECK(require_action(window, "preferencesLanguageJapaneseAction")->isChecked());
}

void ui_language_missing_preference_uses_system_language() {
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("preferences/language"));
    settings.sync();
  }
  patchy::ui::LocalizationManager::instance().load_saved_language(QLocale(QLocale::Japanese, QLocale::Japan));

  patchy::ui::MainWindow window;
  show_window(window);

  CHECK(patchy::ui::LocalizationManager::instance().current_language() == QStringLiteral("ja"));
  const auto menus = top_level_menu_texts(*window.menuBar());
  CHECK(menus.contains(QStringLiteral("ファイル(F)")));
  CHECK(require_action(window, "preferencesLanguageJapaneseAction")->isChecked());
  auto settings = patchy::ui::app_settings();
  CHECK(!settings.contains(QStringLiteral("preferences/language")));
}

void ui_language_saved_preference_overrides_system_language() {
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("preferences/language"), QStringLiteral("en"));
    settings.sync();
  }
  patchy::ui::LocalizationManager::instance().load_saved_language(QLocale(QLocale::Japanese, QLocale::Japan));

  patchy::ui::MainWindow window;
  show_window(window);

  CHECK(patchy::ui::LocalizationManager::instance().current_language() == QStringLiteral("en"));
  const auto menus = top_level_menu_texts(*window.menuBar());
  CHECK(menus.contains(QStringLiteral("File")));
  CHECK(require_action(window, "preferencesLanguageEnglishAction")->isChecked());
  auto settings = patchy::ui::app_settings();
  CHECK(settings.value(QStringLiteral("preferences/language")).toString() == QStringLiteral("en"));
}

void ui_language_invalid_preference_falls_back_to_english() {
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("preferences/language"), QStringLiteral("zz"));
    settings.sync();
  }
  patchy::ui::LocalizationManager::instance().load_saved_language();

  patchy::ui::MainWindow window;
  show_window(window);

  CHECK(patchy::ui::LocalizationManager::instance().current_language() == QStringLiteral("en"));
  const auto menus = top_level_menu_texts(*window.menuBar());
  CHECK(menus.contains(QStringLiteral("File")));
  CHECK(!menus.contains(QStringLiteral("Preferences")));
  CHECK(require_action(window, "preferencesLanguageEnglishAction")->isChecked());
}

void ui_language_catalog_covers_dialog_status_and_properties() {
  CHECK(patchy::ui::LocalizationManager::instance().set_language(QStringLiteral("ja"), false));
  QApplication::processEvents();

  const auto canvas_status = QCoreApplication::translate(
      "patchy::ui::CanvasWidget", "Select a normal pixel layer before painting on text");
  CHECK(canvas_status == QStringLiteral("テキスト上に描画する前に通常のピクセルレイヤーを選択してください"));

  const auto save_title = QCoreApplication::translate("patchy::ui::MainWindow", "Save changes?");
  CHECK(save_title == QStringLiteral("変更を保存しますか?"));
  const auto save_prompt =
      QCoreApplication::translate("patchy::ui::MainWindow", "Save changes to %1 before closing?");
  CHECK(save_prompt == QStringLiteral("閉じる前に %1 への変更を保存しますか?"));

  const auto no_layer = QCoreApplication::translate("patchy::ui::MainWindow", "Layer: No active layer");
  CHECK(no_layer == QStringLiteral("レイヤー: アクティブレイヤーなし"));
  const auto document_info = QCoreApplication::translate(
      "patchy::ui::MainWindow", "Document: %1 x %2 px | %3 x %4 %5 | %6 ppi | %7 | %8 layers | Zoom %9% | %10");
  CHECK(document_info.startsWith(QStringLiteral("ドキュメント:")));
  const auto bmp_depth = QCoreApplication::translate("QObject", "Color depth");
  CHECK(bmp_depth == QStringLiteral("色深度"));
  const auto bmp_quantize = QCoreApplication::translate("QObject", "Reduce colors automatically");
  CHECK(bmp_quantize == QStringLiteral("色数を自動的に減らす"));
  const auto bmp_palette_file = QCoreApplication::translate("QObject", "Use palette file");
  CHECK(bmp_palette_file == QStringLiteral("パレットファイルを使用"));
  const auto settings_file = QCoreApplication::translate("QObject", "Settings file:");
  CHECK(settings_file == QStringLiteral("設定ファイル:"));
  const auto open_settings_folder = QCoreApplication::translate("QObject", "Open Settings Folder");
  CHECK(open_settings_folder == QStringLiteral("設定フォルダーを開く"));
  const auto settings_folder_failed = QCoreApplication::translate("QObject", "Could not open settings folder.");
  CHECK(settings_folder_failed == QStringLiteral("設定フォルダーを開けませんでした。"));
  const auto checking_updates = QCoreApplication::translate("QObject", "Checking for updates...");
  CHECK(checking_updates == QStringLiteral("更新を確認しています..."));
  const auto up_to_date = QCoreApplication::translate("QObject", "Patchy is up to date (%1).");
  CHECK(up_to_date == QStringLiteral("Patchy は最新です (%1)。"));
  const auto update_failed =
      QCoreApplication::translate("QObject", "Update check failed: invalid update manifest.");
  CHECK(update_failed == QStringLiteral("更新確認に失敗しました: 更新マニフェストが無効です。"));
  const auto show_effects = QCoreApplication::translate("QObject", "Show Effects");
  CHECK(show_effects == QStringLiteral("効果を表示"));
  const auto gloss_contour = QCoreApplication::translate("QObject", "Gloss Contour");
  CHECK(gloss_contour == QStringLiteral("光沢輪郭"));
  const auto link_with_layer = QCoreApplication::translate("QObject", "Link with Layer");
  CHECK(link_with_layer == QStringLiteral("レイヤーにリンク"));
  const auto pattern_basketweave = QCoreApplication::translate("QObject", "Basketweave");
  CHECK(pattern_basketweave == QStringLiteral("バスケット編み"));
  const auto contour_ring_double = QCoreApplication::translate("QObject", "Ring - Double");
  CHECK(contour_ring_double == QStringLiteral("リング - 二重"));
  const auto curves_graph = QCoreApplication::translate("QObject", "Curves graph");
  CHECK(curves_graph == QStringLiteral("トーンカーブグラフ"));
  const auto curves_input = QCoreApplication::translate("QObject", "Input:");
  CHECK(curves_input == QStringLiteral("入力:"));
  const auto curves_auto =
      QCoreApplication::translate("QObject", "Set the active channel from its histogram");
  CHECK(curves_auto == QStringLiteral("ヒストグラムに基づいて選択中のチャンネルを自動調整"));
  const auto curves_summary =
      QCoreApplication::translate("QObject", "Curves: RGB %1, Red %2, Green %3, Blue %4 points");
  CHECK(curves_summary == QStringLiteral("トーンカーブ: RGB %1、赤 %2、緑 %3、青 %4 ポイント"));
  const auto curves_presets = QCoreApplication::translate("QObject", "Curves presets");
  CHECK(curves_presets == QStringLiteral("トーンカーブのプリセット"));
  const auto medium_contrast = QCoreApplication::translate("QObject", "Medium Contrast");
  CHECK(medium_contrast == QStringLiteral("中程度のコントラスト"));
  const auto soft_glow = QCoreApplication::translate("QObject", "Soft Glow");
  CHECK(soft_glow == QStringLiteral("ソフトグロー"));
  const auto filter_radius = QCoreApplication::translate("QObject", "Radius");
  CHECK(filter_radius == QStringLiteral("半径"));
  CHECK(patchy::ui::filter_progress_stage_text(patchy::FilterProgressStage::GeneratingClouds) ==
        QStringLiteral("雲模様を生成しています"));
  const auto filter_gallery_action =
      QCoreApplication::translate("patchy::ui::MainWindow", "Filter &Gallery...");
  CHECK(filter_gallery_action == QStringLiteral("フィルターギャラリー(&G)..."));
  const auto filter_gallery_tip = QCoreApplication::translate(
      "patchy::ui::MainWindow", "Preview and apply visual filters and photo looks");
  CHECK(filter_gallery_tip == QStringLiteral("ビジュアルフィルターとフォトルックをプレビューして適用"));
  const auto filter_gallery_cancelled =
      QCoreApplication::translate("patchy::ui::MainWindow", "Cancelled Filter Gallery");
  CHECK(filter_gallery_cancelled == QStringLiteral("フィルターギャラリーをキャンセルしました"));
  const auto filter_gallery_none =
      QCoreApplication::translate("patchy::ui::MainWindow", "No visual filter applied");
  CHECK(filter_gallery_none == QStringLiteral("ビジュアルフィルターは適用されませんでした"));
  const auto filter_gallery_title = QCoreApplication::translate("QObject", "Filter Gallery");
  CHECK(filter_gallery_title == QStringLiteral("フィルターギャラリー"));
  const auto filter_gallery_original = QCoreApplication::translate("QObject", "Original");
  CHECK(filter_gallery_original == QStringLiteral("元画像"));
  const auto filter_gallery_canvas = QCoreApplication::translate("QObject", "Live Canvas Preview");
  CHECK(filter_gallery_canvas == QStringLiteral("キャンバスでライブプレビュー"));
  const auto filter_gallery_apply = QCoreApplication::translate("QObject", "Apply");
  CHECK(filter_gallery_apply == QStringLiteral("適用"));
  const auto filter_gallery_rendering = QCoreApplication::translate("QObject", "Rendering preview...");
  CHECK(filter_gallery_rendering == QStringLiteral("プレビューを描画しています..."));
  const auto filter_gallery_ready = QCoreApplication::translate("QObject", "Ready");
  CHECK(filter_gallery_ready == QStringLiteral("準備完了"));
  const auto curves_load = QCoreApplication::translate("QObject", "Load...");
  CHECK(curves_load == QStringLiteral("読み込み..."));
  const auto curves_save = QCoreApplication::translate("QObject", "Save...");
  CHECK(curves_save == QStringLiteral("保存..."));
  const auto curves_load_title = QCoreApplication::translate("QObject", "Load Curves Preset");
  CHECK(curves_load_title == QStringLiteral("トーンカーブプリセットを読み込み"));
  const auto curves_save_title = QCoreApplication::translate("QObject", "Save Curves Preset");
  CHECK(curves_save_title == QStringLiteral("トーンカーブプリセットを保存"));
  const auto curves_preset_filter =
      QCoreApplication::translate("QObject", "Photoshop Curves Preset (*.acv)");
  CHECK(curves_preset_filter == QStringLiteral("Photoshop トーンカーブプリセット (*.acv)"));
  const auto curves_load_error = QCoreApplication::translate(
      "QObject", "The Curves preset could not be loaded. The file may be damaged or unsupported.");
  CHECK(curves_load_error == QStringLiteral(
                                  "トーンカーブプリセットを読み込めませんでした。ファイルが破損しているか、対応していない可能性があります。"));
  const auto curves_save_error =
      QCoreApplication::translate("QObject", "The Curves preset could not be saved.");
  CHECK(curves_save_error == QStringLiteral("トーンカーブプリセットを保存できませんでした。"));
  const auto curves_target = QCoreApplication::translate("QObject", "Target");
  CHECK(curves_target == QStringLiteral("画像内調整"));
  const auto curves_before = QCoreApplication::translate("QObject", "Before");
  CHECK(curves_before == QStringLiteral("調整前"));
  const auto curves_clipping =
      QCoreApplication::translate("QObject", "Show shadow and highlight clipping together");
  CHECK(curves_clipping == QStringLiteral("シャドウとハイライトのクリッピングを同時に表示"));

  CHECK(patchy::ui::LocalizationManager::instance().set_language(QStringLiteral("en"), false));
  QApplication::processEvents();
}

void ui_filter_gallery_action_retranslates() {
  CHECK(patchy::ui::LocalizationManager::instance().set_language(QStringLiteral("en"), false));
  patchy::ui::MainWindow window;
  show_window(window);
  auto* action = require_action(window, "filterGalleryAction");
  CHECK(action->text() == QStringLiteral("Filter &Gallery..."));
  CHECK(action->statusTip() == QStringLiteral("Preview and apply visual filters and photo looks"));

  CHECK(patchy::ui::LocalizationManager::instance().set_language(QStringLiteral("ja"), false));
  QApplication::processEvents();
  CHECK(action->text() == QStringLiteral("フィルターギャラリー(&G)..."));
  CHECK(action->statusTip() == QStringLiteral("ビジュアルフィルターとフォトルックをプレビューして適用"));
  patchy::ui::ZoomableImagePreview translated_preview;
  CHECK(translated_preview.toolTip() ==
        QStringLiteral("ドラッグで表示位置を移動できます。マウスホイールでズームします。"));

  CHECK(patchy::ui::LocalizationManager::instance().set_language(QStringLiteral("en"), false));
  QApplication::processEvents();
  CHECK(action->text() == QStringLiteral("Filter &Gallery..."));
  CHECK(action->statusTip() == QStringLiteral("Preview and apply visual filters and photo looks"));
}

void ui_about_dialog_shows_labeled_external_links() {
  bool inspected = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchySplashScreen"));
    CHECK(dialog != nullptr);

    const auto link_labels = dialog->findChildren<QLabel*>(QStringLiteral("splashHome"));
    CHECK(link_labels.size() == 2);
    QString combined_text;
    for (const auto* label : link_labels) {
      CHECK(label->textFormat() == Qt::RichText);
      CHECK(label->textInteractionFlags().testFlag(Qt::LinksAccessibleByMouse));
      CHECK(label->openExternalLinks());
      combined_text += label->text();
      combined_text += QLatin1Char('\n');
    }

    CHECK(combined_text.contains(QStringLiteral("GitHub: ")));
    CHECK(combined_text.contains(QStringLiteral("href=\"https://github.com/SethRobinson/Patchy\"")));
    CHECK(combined_text.contains(QStringLiteral(">SethRobinson/Patchy</a>")));
    CHECK(combined_text.contains(QStringLiteral("Seth's site: ")));
    CHECK(combined_text.contains(QStringLiteral("href=\"https://rtsoft.com\"")));
    CHECK(combined_text.contains(QStringLiteral(">rtsoft.com</a>")));

    // The version label shares the splashCredit object name with the credit
    // line below it; creation order puts the version first.
    const auto credit_labels = dialog->findChildren<QLabel*>(QStringLiteral("splashCredit"));
    CHECK(credit_labels.size() == 2);
    CHECK(credit_labels.first()->text().startsWith(QStringLiteral("Version ")));
    CHECK(credit_labels.first()->text().endsWith(
        QStringLiteral("(built %1)").arg(patchy::ui::build_timestamp_text())));

    auto* contributors = dialog->findChild<QLabel*>(QStringLiteral("splashContributors"));
    CHECK(contributors != nullptr);
    CHECK(contributors->textFormat() == Qt::RichText);
    CHECK(contributors->openExternalLinks());
    CHECK(contributors->text().startsWith(QStringLiteral("Code contributions from ")));
    CHECK(contributors->text().contains(QStringLiteral("href=\"https://github.com/mcapogna\"")));
    CHECK(contributors->text().contains(QStringLiteral(">Michael Capogna</a>")));

    auto* settings_caption = dialog->findChild<QLabel*>(QStringLiteral("splashSettingsCaption"));
    CHECK(settings_caption != nullptr);
    CHECK(settings_caption->text() == QStringLiteral("Settings file:"));
    auto* settings_path = dialog->findChild<QLabel*>(QStringLiteral("splashSettingsPath"));
    CHECK(settings_path != nullptr);
    CHECK(settings_path->text() == QDir::toNativeSeparators(patchy::ui::app_settings().fileName()));
    CHECK(settings_path->textInteractionFlags().testFlag(Qt::TextSelectableByMouse));
    CHECK(settings_path->wordWrap());
    auto* open_settings_folder = dialog->findChild<QPushButton*>(QStringLiteral("splashOpenSettingsFolderButton"));
    CHECK(open_settings_folder != nullptr);
    CHECK(open_settings_folder->text() == QStringLiteral("Open Settings Folder"));

    save_widget_artifact("ui_about_dialog_links", *dialog);
    inspected = true;
    dialog->accept();
  });

  patchy::ui::show_about_splash();
  CHECK(inspected);
}

void ui_frameless_window_edges_resize() {
  if (!patchy::ui::MainWindow::use_custom_window_chrome()) {
    // macOS/Linux use the native frame; the OS owns the resize borders and the Qt-level
    // edge machinery under test here is deliberately inert.
    std::cout << "[SKIP] native window frame owns resize borders on this platform\n";
    return;
  }
  patchy::ui::MainWindow window;
  show_window(window);
  window.resize(980, 720);
  QApplication::processEvents();

#ifdef Q_OS_WIN
  if (QGuiApplication::platformName() == QStringLiteral("windows")) {
    const auto style = GetWindowLongPtrW(reinterpret_cast<HWND>(window.winId()), GWL_STYLE);
    CHECK((style & WS_THICKFRAME) != 0);
    CHECK((style & WS_CAPTION) == 0);
  }
#endif

  const auto start = window.geometry();
  const QPoint right_edge(window.width() - 2, window.height() / 2);
  send_mouse(window, QEvent::MouseButtonPress, right_edge, Qt::LeftButton, Qt::LeftButton);
  send_mouse(window, QEvent::MouseMove, right_edge + QPoint(90, 0), Qt::NoButton, Qt::LeftButton);
  send_mouse(window, QEvent::MouseButtonRelease, right_edge + QPoint(90, 0), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(window.width() >= start.width() + 70);
  CHECK(window.height() == start.height());

  const auto widened = window.geometry();
  const QPoint bottom_right(window.width() - 2, window.height() - 2);
  send_mouse(window, QEvent::MouseButtonPress, bottom_right, Qt::LeftButton, Qt::LeftButton);
  send_mouse(window, QEvent::MouseMove, bottom_right + QPoint(45, 55), Qt::NoButton, Qt::LeftButton);
  send_mouse(window, QEvent::MouseButtonRelease, bottom_right + QPoint(45, 55), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(window.width() >= widened.width() + 30);
  CHECK(window.height() >= widened.height() + 40);

  const auto expanded = window.geometry();
  const QPoint left_edge(2, window.height() / 2);
  send_mouse(window, QEvent::MouseButtonPress, left_edge, Qt::LeftButton, Qt::LeftButton);
  send_mouse(window, QEvent::MouseMove, left_edge - QPoint(60, 0), Qt::NoButton, Qt::LeftButton);
  send_mouse(window, QEvent::MouseButtonRelease, left_edge - QPoint(60, 0), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(window.x() <= expanded.x() - 45);
  CHECK(window.width() >= expanded.width() + 45);
}

void ui_right_edge_scrollbars_remain_draggable() {
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  for (int index = 0; index < 48; ++index) {
    document.add_pixel_layer("Scrollable Layer " + std::to_string(index + 1),
                             solid_pixels(64, 64, patchy::PixelFormat::rgba8(),
                                          QColor(40 + index * 3 % 180, 80, 220, 255)));
  }

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Scrollbar Edge"));
  QApplication::processEvents();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  layer_list->setFixedHeight(180);
  QApplication::processEvents();

  auto* scroll = layer_list->verticalScrollBar();
  CHECK(scroll != nullptr);
  CHECK(scroll->isVisible());
  CHECK(scroll->maximum() > 0);
  scroll->setValue(scroll->maximum() / 3);
  QApplication::processEvents();

  QStyleOptionSlider option;
  option.initFrom(scroll);
  option.orientation = scroll->orientation();
  option.minimum = scroll->minimum();
  option.maximum = scroll->maximum();
  option.singleStep = scroll->singleStep();
  option.pageStep = scroll->pageStep();
  option.sliderPosition = scroll->sliderPosition();
  option.sliderValue = scroll->value();
  option.upsideDown = scroll->invertedAppearance();
  const auto handle = scroll->style()->subControlRect(QStyle::CC_ScrollBar, &option,
                                                      QStyle::SC_ScrollBarSlider, scroll);
  CHECK(handle.isValid());
  const auto start_x = std::clamp(scroll->width() - 2, handle.left(), handle.right());
  const QPoint start(start_x, handle.center().y());
  CHECK(handle.contains(start));
  CHECK(scroll->mapTo(&window, start).x() >= window.width() - 10);

  const auto geometry_before = window.geometry();
  const auto scroll_before = scroll->value();
  const auto end = start + QPoint(0, 55);
  send_mouse(*scroll, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*scroll, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
  send_mouse(*scroll, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(window.geometry() == geometry_before);
  CHECK(scroll->value() > scroll_before);
}

void ui_startup_opens_empty_workspace_with_start_panel() {
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("newDocument"));
  }
  patchy::ui::MainWindow window;
  show_window_empty(window);

  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  auto* panel = window.findChild<QWidget*>(QStringLiteral("startPanel"));
  auto* new_button = window.findChild<QPushButton*>(QStringLiteral("startPanelNewButton"));
  auto* open_button = window.findChild<QPushButton*>(QStringLiteral("startPanelOpenButton"));
  auto* info = window.findChild<QLabel*>(QStringLiteral("documentInfoLabel"));
  CHECK(tabs != nullptr);
  CHECK(panel != nullptr);
  CHECK(new_button != nullptr);
  CHECK(open_button != nullptr);
  CHECK(info != nullptr);

  // Startup: no auto-created document, the start panel fills the tab area.
  CHECK(tabs->count() == 0);
  CHECK(patchy::ui::MainWindowTestAccess::session_count(window) == 0);
  CHECK(panel->isVisible());
  CHECK(panel->size() == tabs->size());
  CHECK(info->text() == QStringLiteral("No document"));
  CHECK(require_action(window, "fileNewAction")->isEnabled());
  CHECK(require_action(window, "fileOpenAction")->isEnabled());
  CHECK(!require_action(window, "fileSaveAction")->isEnabled());
  save_widget_artifact("ui_start_panel", window);

  // The New Document button runs the regular dialog flow and the panel hides.
  accept_new_document_dialog(400, 300);
  new_button->click();
  QApplication::processEvents();
  CHECK(tabs->count() == 1);
  CHECK(!panel->isVisible());
  CHECK(info->text().contains(QStringLiteral("400 x 300 px")));

  // Closing the last document brings the panel back.
  CHECK(QMetaObject::invokeMethod(tabs, "tabCloseRequested", Qt::DirectConnection, Q_ARG(int, 0)));
  QApplication::processEvents();
  CHECK(tabs->count() == 0);
  CHECK(panel->isVisible());
}

void ui_start_panel_recent_files_open_on_click() {
  ensure_artifact_dir();
  const auto recent_path = QFileInfo(QStringLiteral("test-artifacts/start_panel_recent.png")).absoluteFilePath();
  {
    QImage recent_image(48, 32, QImage::Format_RGB32);
    recent_image.fill(QColor(90, 150, 210));
    CHECK(recent_image.save(recent_path));
    auto settings = patchy::ui::app_settings();
    // A dead entry ahead of the live one: the panel list filters to existing files.
    settings.setValue(QStringLiteral("recentFiles"),
                      QStringList{QStringLiteral("Z:/definitely/missing/file.png"), recent_path});
    settings.sync();
  }

  patchy::ui::MainWindow window;
  show_window_empty(window);
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  auto* panel = window.findChild<QWidget*>(QStringLiteral("startPanel"));
  auto* recent_list = window.findChild<QListWidget*>(QStringLiteral("startPanelRecentList"));
  auto* info = window.findChild<QLabel*>(QStringLiteral("documentInfoLabel"));
  CHECK(tabs != nullptr);
  CHECK(panel != nullptr);
  CHECK(recent_list != nullptr);
  CHECK(info != nullptr);
  CHECK(panel->isVisible());
  CHECK(recent_list->isVisible());
  CHECK(recent_list->count() == 1);
  CHECK(recent_list->item(0)->text() == QStringLiteral("start_panel_recent.png"));

  const auto row_center = recent_list->visualItemRect(recent_list->item(0)).center();
  send_mouse(*recent_list->viewport(), QEvent::MouseButtonPress, row_center, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*recent_list->viewport(), QEvent::MouseButtonRelease, row_center, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(tabs->count() == 1);
  CHECK(!panel->isVisible());
  CHECK(info->text().contains(QStringLiteral("48 x 32 px")));
}

void ui_start_panel_shows_about_info_and_update_status() {
  patchy::ui::MainWindow window;
  show_window_empty(window);

  auto* panel = window.findChild<patchy::ui::StartPanel*>(QStringLiteral("startPanel"));
  CHECK(panel != nullptr);
  CHECK(panel->isVisible());

  // The about branding lives on the panel now: tagline, version, credit, and links.
  auto* tagline = window.findChild<QLabel*>(QStringLiteral("startPanelTagline"));
  CHECK(tagline != nullptr);
  CHECK(tagline->text() == QStringLiteral("Open source photo editing. Free forever, no subscriptions."));
  auto* version = window.findChild<QLabel*>(QStringLiteral("startPanelVersion"));
  CHECK(version != nullptr);
  CHECK(version->text().startsWith(QStringLiteral("Version ")));
  CHECK(version->text().endsWith(QStringLiteral("(built %1)").arg(patchy::ui::build_timestamp_text())));
  auto* credit = window.findChild<QLabel*>(QStringLiteral("startPanelCredit"));
  CHECK(credit != nullptr);
  CHECK(credit->text() == QStringLiteral("Created by Seth A. Robinson"));
  auto* contributors = window.findChild<QLabel*>(QStringLiteral("startPanelContributors"));
  CHECK(contributors != nullptr);
  CHECK(contributors->textFormat() == Qt::RichText);
  CHECK(contributors->openExternalLinks());
  CHECK(contributors->text().startsWith(QStringLiteral("Code contributions from ")));
  CHECK(contributors->text().contains(QStringLiteral("href=\"https://github.com/mcapogna\"")));
  CHECK(contributors->text().contains(QStringLiteral(">Michael Capogna</a>")));

  const auto link_labels = panel->findChildren<QLabel*>(QStringLiteral("startPanelHome"));
  CHECK(link_labels.size() == 2);
  QString combined_text;
  for (const auto* label : link_labels) {
    CHECK(label->textFormat() == Qt::RichText);
    CHECK(label->textInteractionFlags().testFlag(Qt::LinksAccessibleByMouse));
    CHECK(label->openExternalLinks());
    combined_text += label->text();
    combined_text += QLatin1Char('\n');
  }
  CHECK(combined_text.contains(QStringLiteral("href=\"https://github.com/SethRobinson/Patchy\"")));
  CHECK(combined_text.contains(QStringLiteral(">SethRobinson/Patchy</a>")));
  CHECK(combined_text.contains(QStringLiteral("href=\"https://rtsoft.com\"")));
  CHECK(combined_text.contains(QStringLiteral(">rtsoft.com</a>")));

  // The update-status footer line stays hidden until a status is pushed and
  // hides again when cleared.
  auto* status = window.findChild<QLabel*>(QStringLiteral("startPanelUpdateStatus"));
  CHECK(status != nullptr);
  CHECK(!status->isVisible());
  panel->set_update_status(QStringLiteral("Patchy is up to date (9.99)."));
  QApplication::processEvents();
  CHECK(status->isVisible());
  CHECK(status->text() == QStringLiteral("Patchy is up to date (9.99)."));
  save_widget_artifact("ui_start_panel_about_info", window);
  panel->set_update_status(QString());
  CHECK(!status->isVisible());
}

void ui_status_bar_error_message_flashes_then_persists_until_replaced() {
  patchy::ui::ZoomStatusBar bar;
  bar.resize(600, 24);
  bar.show();
  QApplication::processEvents();

  bar.showMessage(QStringLiteral("Ready"));
  CHECK(!bar.error_message_active());

  bar.show_error_message(QStringLiteral("Layer pixels are locked."));
  CHECK(bar.currentMessage() == QStringLiteral("Layer pixels are locked."));
  CHECK(bar.error_message_active());
  CHECK(bar.error_flash_running());

  // The flash is finite (~1s); the red error presentation persists after it.
  CHECK(process_events_until([&bar] { return !bar.error_flash_running(); }));
  CHECK(bar.error_message_active());

  // Re-showing the SAME error restarts the flash even though QStatusBar
  // suppresses messageChanged for an identical string.
  bar.show_error_message(QStringLiteral("Layer pixels are locked."));
  CHECK(bar.error_flash_running());
  CHECK(bar.currentMessage() == QStringLiteral("Layer pixels are locked."));

  // Any different message ends the error presentation.
  bar.showMessage(QStringLiteral("Applied Levels"));
  CHECK(!bar.error_message_active());
  CHECK(!bar.error_flash_running());

  bar.show_error_message(QStringLiteral("Layer pixels are locked."));
  CHECK(bar.error_message_active());
  bar.clearMessage();
  CHECK(!bar.error_message_active());
}

void ui_blocking_refusal_shows_error_status_and_info_clears_it() {
  patchy::ui::MainWindow window;
  show_window(window);

  require_action(window, "filterConvertForSmartFiltersAction")->trigger();
  QApplication::processEvents();
  require_action(window, "filterLiquifyAction")->trigger();
  QApplication::processEvents();

  CHECK(window.statusBar()->currentMessage() ==
        QStringLiteral("Rasterize the Smart Object before using Liquify"));
  auto* bar = qobject_cast<patchy::ui::ZoomStatusBar*>(window.statusBar());
  CHECK(bar != nullptr);
  CHECK(bar->error_message_active());
  save_widget_artifact("status_bar_error_flash", *bar);

  window.statusBar()->showMessage(QStringLiteral("Ready"));
  QApplication::processEvents();
  CHECK(!bar->error_message_active());
}

void ui_svg_icon_resources_are_registered() {
  patchy::ui::MainWindow window;
  show_window(window);

  CHECK(QFile::exists(QStringLiteral(":/patchy/icons/new.svg")));
  CHECK(QFile::exists(QStringLiteral(":/patchy/icons/mask.svg")));
  CHECK(QFile::exists(QStringLiteral(":/patchy/icons/selection-add.svg")));
  CHECK(QFile::exists(QStringLiteral(":/patchy/icons/channel-save-selection.svg")));
  CHECK(QFile::exists(QStringLiteral(":/patchy/icons/channel-load-selection.svg")));

  const QIcon icon(QStringLiteral(":/patchy/icons/new.svg"));
  CHECK(!icon.isNull());
  CHECK(!icon.pixmap(QSize(32, 32)).isNull());

  CHECK(!require_action(window, "layerNewAction")->icon().isNull());
  CHECK(!require_action(window, "layerAddMaskAction")->icon().isNull());
}

// The icon engine recolors a fixed ten-color vocabulary. A new icon drawn with a
// color outside it would render fine in Dark and then stay dark-on-dark in Light,
// which no rendered test would catch. Walk the resources instead.
void ui_icon_color_map_covers_every_authored_color() {
  QSet<QString> mapped;
  for (const auto& [source, member] : patchy::ui::icon_color_roles()) {
    mapped.insert(QString(source).toLower());
  }
  CHECK(!mapped.isEmpty());

  QSet<QString> exempt;
  for (const auto& name : patchy::ui::literal_color_icon_names()) {
    exempt.insert(name);
  }
  for (const auto& name : patchy::ui::stylesheet_referenced_icon_names()) {
    exempt.insert(name);
  }

  const QDir icons(QStringLiteral(":/patchy/icons"));
  const auto entries = icons.entryList({QStringLiteral("*.svg")}, QDir::Files, QDir::Name);
  CHECK(entries.size() > 50);

  static const QRegularExpression hex(QStringLiteral("#[0-9a-fA-F]{6}"));
  int checked = 0;
  for (const auto& entry : entries) {
    QFile file(icons.filePath(entry));
    CHECK(file.open(QIODevice::ReadOnly));
    const QString svg = QString::fromUtf8(file.readAll());
    const QString stem = QFileInfo(entry).completeBaseName();

    auto matches = hex.globalMatch(svg);
    while (matches.hasNext()) {
      const auto value = matches.next().captured(0);
      // The substitution is a plain byte replace, so authored hex must be lowercase.
      if (value != value.toLower()) {
        fprintf(stderr, "  %s: uppercase hex %s\n", qPrintable(entry), qPrintable(value));
      }
      CHECK(value == value.toLower());
      if (exempt.contains(stem)) {
        continue;
      }
      if (!mapped.contains(value)) {
        fprintf(stderr, "  %s: %s is not in the icon color map\n", qPrintable(entry), qPrintable(value));
      }
      CHECK(mapped.contains(value));
      ++checked;
    }
  }
  CHECK(checked > 100);

  // Every stylesheet-referenced icon must actually exist under the base path; the
  // light variants are checked separately once they exist.
  for (const auto& name : patchy::ui::stylesheet_referenced_icon_names()) {
    CHECK(QFile::exists(QStringLiteral(":/patchy/icons/%1.svg").arg(name)));
  }
}

// A role omitted from a palette's aggregate initializer default-constructs to an
// invalid QColor, which QSS then renders as a black fill rather than failing.
// Walking the role table is the only thing that catches it.
void ui_theme_palettes_define_every_role() {
  const auto roles = patchy::ui::theme_palette_roles();
  CHECK(!roles.empty());

  for (const auto& [name, member] : roles) {
    const auto dark = patchy::ui::dark_palette().*member;
    const auto light = patchy::ui::light_palette().*member;
    if (!dark.isValid() || !light.isValid()) {
      fprintf(stderr, "  role \"%s\" is missing a value\n", QString(name).toUtf8().constData());
    }
    CHECK(dark.isValid());
    CHECK(light.isValid());
  }

  // Role names are generated from the member spelling, so duplicates mean a
  // copy-paste in the table rather than a typo.
  QSet<QString> seen;
  for (const auto& [name, member] : roles) {
    CHECK(!seen.contains(QString(name)));
    seen.insert(QString(name));
  }
}

// Handing token text to setStyleSheet is silently destructive: Qt drops the whole
// declaration, so a rule simply stops existing and only shows up as a wrongly
// drawn control in one state. ThemedQss makes that a compile error for the shared
// builders; this catches an inline blob that forgot to go through the helpers.
void ui_no_widget_ships_unresolved_theme_tokens() {
  patchy::ui::MainWindow window;
  show_window(window);
  QApplication::processEvents();

  int styled = 0;
  for (auto* widget : QApplication::allWidgets()) {
    if (widget == nullptr) {
      continue;
    }
    const auto sheet = widget->styleSheet();
    if (sheet.isEmpty()) {
      continue;
    }
    ++styled;
    if (sheet.contains(QLatin1Char('@'))) {
      fprintf(stderr, "  %s (%s) ships an unresolved theme token\n",
              qPrintable(widget->objectName()), widget->metaObject()->className());
    }
    CHECK(!sheet.contains(QLatin1Char('@')));
  }
  CHECK(styled > 0);
}

[[nodiscard]] int channel_delta(const QColor& first, const QColor& second) {
  return std::max({std::abs(first.red() - second.red()), std::abs(first.green() - second.green()),
                   std::abs(first.blue() - second.blue())});
}

// Light is derived from Dark, so two roles that never touch in Dark can land on
// the same value in Light. That happened: title_bar_bg and canvas_backdrop both
// resolved to the same gray and a dialog floating over the canvas lost its title
// bar entirely. Encode the pairs that actually sit against each other on screen.
// Thresholds are set below what Dark already achieves, so this asserts "Light is
// at least as readable here as the scheme we have always shipped".
void ui_theme_adjacent_roles_stay_distinguishable() {
  struct RolePair {
    const char* description;
    QColor patchy::ui::ThemePalette::* first;
    QColor patchy::ui::ThemePalette::* second;
    int minimum_delta;
  };
  const std::array<RolePair, 4> pairs{{
      {"a dialog title bar floating over the canvas", &patchy::ui::ThemePalette::title_bar_bg,
       &patchy::ui::ThemePalette::canvas_backdrop, 24},
      {"a dialog title bar against the dialog body it heads", &patchy::ui::ThemePalette::title_bar_bg,
       &patchy::ui::ThemePalette::window_bg, 12},
      {"the chrome bar against a docked panel", &patchy::ui::ThemePalette::title_bar_bg,
       &patchy::ui::ThemePalette::panel_bg, 16},
      {"the clipping badge on an unselected layer row", &patchy::ui::ThemePalette::layer_clip_badge,
       &patchy::ui::ThemePalette::layer_row_bg, 60},
  }};

  for (const auto scheme : {patchy::ui::ColorScheme::Dark, patchy::ui::ColorScheme::Light}) {
    const auto& palette = patchy::ui::theme(scheme);
    const char* label = scheme == patchy::ui::ColorScheme::Light ? "light" : "dark";
    for (const auto& pair : pairs) {
      const int measured = channel_delta(palette.*pair.first, palette.*pair.second);
      if (measured < pair.minimum_delta) {
        fprintf(stderr, "  %s: %s -> delta %d, want >= %d\n", label, pair.description, measured,
                pair.minimum_delta);
      }
      CHECK(measured >= pair.minimum_delta);
    }
  }
}

[[nodiscard]] double relative_luminance(const QColor& color) {
  const auto channel = [](double value) {
    return value <= 0.03928 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
  };
  return 0.2126 * channel(color.redF()) + 0.7152 * channel(color.greenF()) +
         0.0722 * channel(color.blueF());
}

[[nodiscard]] double contrast_ratio(const QColor& first, const QColor& second) {
  const double a = relative_luminance(first);
  const double b = relative_luminance(second);
  return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
}

// Text roles pinned to one value across both schemes are the trap here:
// text_on_accent is correctly white over a saturated selection, and was also
// being used over neutral raised surfaces, where Light turns the background
// pale and the label vanishes. Every text/background pair has to hold up in
// both schemes, not just the one it was authored against.
void ui_theme_text_roles_contrast_with_their_backgrounds() {
  struct TextPair {
    const char* description;
    QColor patchy::ui::ThemePalette::* text;
    QColor patchy::ui::ThemePalette::* background;
  };
  const std::array<TextPair, 10> pairs{{
      {"a highlighted menu item", &patchy::ui::ThemePalette::text_on_accent,
       &patchy::ui::ThemePalette::menu_item_selected_bg},
      {"a checked options-bar button", &patchy::ui::ThemePalette::text_on_accent,
       &patchy::ui::ThemePalette::accent_checked_bg},
      {"the selected layer-style category", &patchy::ui::ThemePalette::text_on_accent,
       &patchy::ui::ThemePalette::category_selected_bg},
      {"the About dialog's primary button", &patchy::ui::ThemePalette::text_on_accent,
       &patchy::ui::ThemePalette::splash_primary_bg},
      {"the selected document tab", &patchy::ui::ThemePalette::text_on_raised,
       &patchy::ui::ThemePalette::tab_selected_bg},
      {"the selected Preferences tab", &patchy::ui::ThemePalette::text_on_raised,
       &patchy::ui::ThemePalette::dialog_tab_selected_bg},
      {"an Image Size panel label", &patchy::ui::ThemePalette::text_on_raised,
       &patchy::ui::ThemePalette::dlg_raised_bg},
      {"body text on the window surface", &patchy::ui::ThemePalette::text_primary,
       &patchy::ui::ThemePalette::window_bg},
      {"the menu bar", &patchy::ui::ThemePalette::text_bright,
       &patchy::ui::ThemePalette::title_bar_bg},
      {"a selected layer row's name", &patchy::ui::ThemePalette::layer_row_name_text,
       &patchy::ui::ThemePalette::layer_row_selected_bg},
  }};

  // 3:1 is the floor for UI text and large type; every pair here clears it in
  // Dark, so this asserts Light is no worse than what already ships.
  constexpr double kMinimumContrast = 3.0;
  for (const auto scheme : {patchy::ui::ColorScheme::Dark, patchy::ui::ColorScheme::Light}) {
    const auto& palette = patchy::ui::theme(scheme);
    const char* label = scheme == patchy::ui::ColorScheme::Light ? "light" : "dark";
    for (const auto& pair : pairs) {
      const double ratio = contrast_ratio(palette.*pair.text, palette.*pair.background);
      if (ratio < kMinimumContrast) {
        fprintf(stderr, "  %s: %s -> contrast %.2f, want >= %.1f\n", label, pair.description, ratio,
                kMinimumContrast);
      }
      CHECK(ratio >= kMinimumContrast);
    }
  }
}

// The rendered counterpart to the pair above: the palette can be right and the
// chrome still wrong if a rule stops applying. Opens the real dialog over the
// real canvas in Light and looks at the pixels.
void ui_light_scheme_dialog_chrome_separates_from_canvas() {
  ColorSchemeRestorer restore_active;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Light);

  patchy::ui::MainWindow window;
  show_window(window);
  QApplication::processEvents();

  bool sampled = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* title_bar = dialog->findChild<QWidget*>(QStringLiteral("dialogChromeTitleBar"));
    CHECK(title_bar != nullptr);
    if (title_bar != nullptr) {
      const auto shot = title_bar->grab().toImage();
      CHECK(!shot.isNull());
      if (!shot.isNull()) {
        // Mid-height, right of the badge and left of the close button.
        const auto bar = shot.pixelColor(shot.width() / 2, shot.height() / 2);
        const int against_canvas = channel_delta(bar, patchy::ui::theme().canvas_backdrop);
        const int against_body = channel_delta(bar, patchy::ui::theme().window_bg);
        if (against_canvas < 20 || against_body < 10) {
          fprintf(stderr, "  title bar %s: delta %d vs canvas, %d vs dialog body\n",
                  qPrintable(bar.name(QColor::HexRgb)), against_canvas, against_body);
        }
        CHECK(against_canvas >= 20);
        CHECK(against_body >= 10);
        sampled = true;
      }
    }
    save_widget_artifact("ui_light_scheme_preferences_dialog", *dialog);
    dialog->reject();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(sampled);
}

// A floating dialog gets no window-manager shadow on wasm, and its title bar
// shares @title_bar_bg with the main window's own chrome bar, so the 1px
// @window_border outline is the only thing that can separate a dialog's edge
// from the chrome behind it. Assert the outline is actually the outermost
// pixel of the rendered dialog and that it reads against both the title-bar
// fill inside it and the chrome surface it typically floats over.
void ui_dialog_chrome_outline_reads_at_the_dialog_edge() {
  ColorSchemeRestorer restore_active;

  for (const auto preference :
       {patchy::ui::ColorSchemePreference::Dark, patchy::ui::ColorSchemePreference::Light}) {
    ColorSchemeRestorer::apply(preference);
    const bool light = preference == patchy::ui::ColorSchemePreference::Light;
    const char* label = light ? "light" : "dark";

    QDialog dialog;
    auto* root = new QVBoxLayout(&dialog);
    auto* content =
        patchy::ui::install_dark_dialog_chrome(dialog, root, QStringLiteral("Outline Probe"));
    content->addWidget(new QLabel(QStringLiteral("Probe body"), &dialog));
    dialog.resize(300, 140);
    dialog.show();
    QApplication::processEvents();

    const auto shot = dialog.grab().toImage();
    CHECK(!shot.isNull());
    if (shot.isNull()) {
      return;
    }
    if (light) {
      save_widget_artifact("ui_light_scheme_dialog_chrome_outline", dialog);
    }

    // Mid-width avoids the badge and the close button; row 0 is the outline
    // if it is drawn at all, and row 17 is inside the 34px title bar's fill.
    const auto edge = shot.pixelColor(shot.width() / 2, 0);
    const auto title_fill = shot.pixelColor(shot.width() / 2, 17);
    const int edge_vs_title = channel_delta(edge, title_fill);
    const int edge_vs_chrome = channel_delta(edge, patchy::ui::theme().title_bar_bg);
    if (edge_vs_title < 40 || edge_vs_chrome < 40) {
      fprintf(stderr, "  %s: dialog edge %s vs title fill %s (%d), vs chrome bar %s (%d), want >= 40\n",
              label, qPrintable(edge.name(QColor::HexRgb)), qPrintable(title_fill.name(QColor::HexRgb)),
              edge_vs_title, qPrintable(patchy::ui::theme().title_bar_bg.name(QColor::HexRgb)),
              edge_vs_chrome);
    }
    CHECK(edge_vs_title >= 40);
    CHECK(edge_vs_chrome >= 40);
  }
}

// Panel and list scroll bars are styled by the application sheet on every
// platform rather than left to the native style. They have to be: the global
// QWidget background rule reaches them anyway, and once QSS touches a scroll bar
// QStyleSheetStyle owns the whole complex control, so one without subcontrol
// rules fills its groove with the window background and lets the base style draw
// the rest on top. Against Dark's near-black surface that passed for native
// rendering, which is how it survived review. In Light the groove, the handle and
// the panel behind them all derived to the same near-white and the bar vanished,
// leaving two faint arrow glyphs as the only evidence it was there.
//
// A role-pair check cannot catch this, because the track is deliberately the same
// value as the surface in both schemes and the separation comes from the handle
// and the dither texture over it. So render a real bar and read it.
void ui_light_scheme_panel_scroll_bar_handle_reads_against_track() {
  ColorSchemeRestorer restore_active;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Light);

  patchy::ui::MainWindow window;
  show_window(window);
  QApplication::processEvents();

  // Far more rows than fit, so the handle covers a small fraction of the groove
  // and the bare track below it is actually rendered.
  auto* list = new QListWidget(&window);
  list->setObjectName(QStringLiteral("lightScrollBarProbeList"));
  for (int row = 0; row < 400; ++row) {
    list->addItem(QStringLiteral("row %1").arg(row));
  }
  list->setGeometry(0, 0, 200, 160);
  list->show();
  QApplication::processEvents();

  auto* bar = list->verticalScrollBar();
  CHECK(bar != nullptr);
  if (bar == nullptr) {
    return;
  }
  const auto shot = bar->grab().toImage();
  CHECK(!shot.isNull());
  if (shot.isNull()) {
    return;
  }
  save_widget_artifact("ui_light_scheme_panel_scroll_bar", *list);

  // The list sits at row 0, so the handle is parked at the top of the groove and
  // bare track runs below it. Compare the two rows across the full width rather
  // than at one column: a handle is placed by its border as much as by its fill,
  // and the fill alone sits deliberately close to the track in a light scheme.
  // What has to hold is that the two rows do not render as the same thing, which
  // is exactly what failed here.
  const int handle_y = 6;
  const int track_y = shot.height() - 6;
  int separation = 0;
  int at_column = 0;
  for (int x = 0; x < shot.width(); ++x) {
    const int delta = channel_delta(shot.pixelColor(x, handle_y), shot.pixelColor(x, track_y));
    if (delta > separation) {
      separation = delta;
      at_column = x;
    }
  }
  if (separation < 40) {
    fprintf(stderr, "  light panel scroll bar: handle row %s vs track row %s, best delta %d at x=%d, want >= 40\n",
            qPrintable(shot.pixelColor(at_column, handle_y).name(QColor::HexRgb)),
            qPrintable(shot.pixelColor(at_column, track_y).name(QColor::HexRgb)), separation,
            at_column);
  }
  CHECK(separation >= 40);
}

// The compact color picker styles its own tab bar because it wants more
// selected-tab contrast than the global theme gives, and borrowing roles to get
// it is what left it behind when the document tabs were corrected for Light. Its
// selected fill came from field_border, which sits lighter than its neighbours
// in Dark and darker than them in Light, so the current mode read as the
// inactive one. Reading the roles back would only restate whichever pairing the
// sheet happens to name, so render the bar and compare the tabs as drawn.
void ui_light_scheme_color_picker_selected_tab_reads_as_selected() {
  ColorSchemeRestorer restore_active;

  for (const auto preference :
       {patchy::ui::ColorSchemePreference::Dark, patchy::ui::ColorSchemePreference::Light}) {
    ColorSchemeRestorer::apply(preference);
    const bool light = preference == patchy::ui::ColorSchemePreference::Light;
    const char* label = light ? "light" : "dark";

    patchy::ui::PatchyColorPicker picker(QColor(120, 60, 30));
    picker.show();
    QApplication::processEvents();

    auto* tabs = picker.findChild<QTabWidget*>(QStringLiteral("patchyColorPickerTabs"));
    CHECK(tabs != nullptr);
    if (tabs == nullptr) {
      return;
    }
    auto* bar = tabs->tabBar();
    CHECK(bar != nullptr);
    if (bar == nullptr || bar->count() < 2) {
      CHECK(false);
      return;
    }
    const auto shot = bar->grab().toImage();
    CHECK(!shot.isNull());
    if (shot.isNull()) {
      return;
    }
    if (light) {
      save_widget_artifact("ui_light_scheme_color_picker", picker);
    }

    // The picker reopens on whichever mode was used last, so read the current
    // tab rather than forcing one: setting it would write that mode back into
    // the shared QSettings store for every test after this one.
    const int selected_index = bar->currentIndex();
    const int other_index = selected_index == 0 ? 1 : 0;
    // Inside the left padding, clear of the border and of the label glyphs.
    const auto fill = [&shot, bar](int index) {
      const auto rect = bar->tabRect(index);
      return shot.pixelColor(rect.left() + 5, rect.center().y());
    };
    const auto selected = fill(selected_index);
    const auto unselected = fill(other_index);

    // Lightness carries the selection in both schemes: the current mode is the
    // raised tab, whichever end of the value range the scheme lives at. The
    // threshold is what Dark already ships (0x34 against 0x5a), so this asserts
    // Light separates them at least as well.
    const int separation = selected.lightness() - unselected.lightness();
    if (separation < 30) {
      fprintf(stderr, "  %s: picker selected tab %s vs unselected %s, lightness %+d, want >= +30\n",
              label, qPrintable(selected.name(QColor::HexRgb)),
              qPrintable(unselected.name(QColor::HexRgb)), separation);
    }
    CHECK(separation >= 30);
  }
}

// An unresolved @token makes Qt drop the entire declaration containing it,
// silently. Nothing downstream would notice, so assert no token survives and no
// raw hex was left behind in either scheme.
void ui_theme_qss_resolves_every_token() {
  const patchy::ui::ColorScheme entry_scheme = patchy::ui::active_color_scheme();

  for (const auto scheme : {patchy::ui::ColorScheme::Dark, patchy::ui::ColorScheme::Light}) {
    patchy::ui::set_active_color_scheme(scheme);
    const auto style = patchy::ui::photoshop_style();
    CHECK(!style.isEmpty());
    CHECK(!style.contains(QLatin1Char('@')));
  }

  patchy::ui::set_active_color_scheme(entry_scheme);

  // The template is the thing that must stay token-only: a hex literal added
  // there would be invisible to the check above.
  const auto resolved = patchy::ui::apply_theme_tokens(QStringLiteral("a: @window_bg; b: @accent;"));
  CHECK(resolved ==
        QStringLiteral("a: %1; b: %2;")
            .arg(patchy::ui::theme().window_bg.name(QColor::HexRgb),
                 patchy::ui::theme().accent.name(QColor::HexRgb)));
}

// A QFont holds its size in points OR pixels and the unused accessor returns -1.
// macOS resolves inherited widget fonts by pixel size, so the natural
// `f.setPointSizeF(f.pointSizeF() * k)` silently left every derived font at full
// size there (and spammed "Point size <= 0" through the offscreen suite). The
// pixel-defined cases below are the macOS condition, reproduced on any platform.
void ui_derived_font_sizes_scale_in_points_and_pixels() {
  QFont point_font;
  point_font.setPointSizeF(20.0);
  CHECK(std::abs(patchy::ui::scaled_font(point_font, 0.85).pointSizeF() - 17.0) < 0.0001);
  CHECK(patchy::ui::scaled_font(point_font, 0.85).pixelSize() == -1);
  CHECK(std::abs(patchy::ui::offset_font(point_font, -1, false).pointSizeF() - 19.0) < 0.0001);

  QFont pixel_font;
  pixel_font.setPixelSize(20);
  // The bug: this must shrink, not stay 20 and not become a negative point size.
  CHECK(patchy::ui::scaled_font(pixel_font, 0.85).pixelSize() == 17);
  CHECK(patchy::ui::scaled_font(pixel_font, 0.85).pointSizeF() == -1.0);
  CHECK(patchy::ui::scaled_font(pixel_font, 1.9).pixelSize() == 38);
  CHECK(patchy::ui::offset_font(pixel_font, -1, false).pixelSize() == 19);

  // Floors keep a derived size positive rather than clamping a real one.
  CHECK(patchy::ui::scaled_font(pixel_font, 0.001).pixelSize() == 1);
  QFont small_point_font;
  small_point_font.setPointSizeF(8.0);
  CHECK(patchy::ui::offset_font(small_point_font, -4, false).pointSizeF() >= 7.0);

  // Bold rides along with offset_font; scaling never touches weight.
  CHECK(patchy::ui::offset_font(point_font, 0, true).bold());
  CHECK(!patchy::ui::scaled_font(point_font, 2.0).bold());

  // A non-positive or non-finite scale is refused rather than corrupting the font.
  QFont untouched = point_font;
  patchy::ui::scale_font_size(untouched, 0.0);
  CHECK(std::abs(untouched.pointSizeF() - 20.0) < 0.0001);
  patchy::ui::scale_font_size(untouched, std::numeric_limits<double>::quiet_NaN());
  CHECK(std::abs(untouched.pointSizeF() - 20.0) < 0.0001);
}

}  // namespace

std::vector<patchy::test::TestCase> app_shell_tests() {
  return {
      {"ui_derived_font_sizes_scale_in_points_and_pixels", ui_derived_font_sizes_scale_in_points_and_pixels},
      {"ui_startup_opens_empty_workspace_with_start_panel", ui_startup_opens_empty_workspace_with_start_panel},
      {"ui_start_panel_recent_files_open_on_click", ui_start_panel_recent_files_open_on_click},
      {"ui_start_panel_shows_about_info_and_update_status", ui_start_panel_shows_about_info_and_update_status},
      {"ui_main_window_renders_color_controls", ui_main_window_renders_color_controls},
      {"ui_tool_palette_overflow_hides_quick_mask_before_swatches",
       ui_tool_palette_overflow_hides_quick_mask_before_swatches},
      {"ui_tool_palette_extension_button_expands_palette", ui_tool_palette_extension_button_expands_palette},
      {"ui_tool_palette_expanded_collapses_after_tool_pick", ui_tool_palette_expanded_collapses_after_tool_pick},
      {"ui_dissolve_clipped_render_matches_full_render", ui_dissolve_clipped_render_matches_full_render},
      {"ui_window_force_refresh_action_rebuilds_cache", ui_window_force_refresh_action_rebuilds_cache},
      {"ui_canvas_ignores_opaque_psd_flat_cache_for_first_paint_transparency",
       ui_canvas_ignores_opaque_psd_flat_cache_for_first_paint_transparency},
      {"ui_top_menu_items_highlight_on_hover", ui_top_menu_items_highlight_on_hover},
      {"ui_save_as_dialog_lists_recent_files", ui_save_as_dialog_lists_recent_files},
      {"ui_open_recent_keeps_two_hundred_files_in_grouped_menu",
       ui_open_recent_keeps_two_hundred_files_in_grouped_menu},
      {"ui_open_recent_filter_narrows_entries_and_opens_first_match",
       ui_open_recent_filter_narrows_entries_and_opens_first_match},
      {"ui_open_recent_keeps_two_hundred_folders_in_grouped_menu",
       ui_open_recent_keeps_two_hundred_folders_in_grouped_menu},
      {"ui_recent_file_context_menu_copies_path", ui_recent_file_context_menu_copies_path},
      {"ui_recent_folder_context_menu_copies_path_and_offers_explorer",
       ui_recent_folder_context_menu_copies_path_and_offers_explorer},
      {"ui_save_as_remembers_last_save_directory_between_windows",
       ui_save_as_remembers_last_save_directory_between_windows},
      {"ui_open_remembers_last_directory_and_lists_recent_folders",
       ui_open_remembers_last_directory_and_lists_recent_folders},
      {"ui_open_dialog_hides_name_filter_details", ui_open_dialog_hides_name_filter_details},
      {"update_manifest_parser_handles_supported_cases", update_manifest_parser_handles_supported_cases},
      {"ui_update_available_dialog_warns_to_close_patchy_before_installing",
       ui_update_available_dialog_warns_to_close_patchy_before_installing},
      {"ui_update_preference_defaults_startup_check_setting_to_enabled",
       ui_update_preference_defaults_startup_check_setting_to_enabled},
      {"ui_update_preference_persists_startup_check_setting", ui_update_preference_persists_startup_check_setting},
      {"ui_gui_scale_preference_persists_setting", ui_gui_scale_preference_persists_setting},
      {"ui_color_scheme_preference_persists_setting", ui_color_scheme_preference_persists_setting},
      {"ui_color_scheme_cancel_restores_entry_scheme", ui_color_scheme_cancel_restores_entry_scheme},
      {"ui_color_scheme_follow_system_tracks_style_hints",
       ui_color_scheme_follow_system_tracks_style_hints},
      {"ui_color_scheme_switch_updates_existing_window", ui_color_scheme_switch_updates_existing_window},
      {"ui_themed_icons_recolor_between_schemes", ui_themed_icons_recolor_between_schemes},
      {"ui_main_window_persists_window_geometry", ui_main_window_persists_window_geometry},
      {"ui_psd_import_warning_preference_defaults_to_hidden",
       ui_psd_import_warning_preference_defaults_to_hidden},
      {"ui_psd_import_warning_preference_persists_enabled_setting",
       ui_psd_import_warning_preference_persists_enabled_setting},
      {"ui_language_switch_updates_existing_window", ui_language_switch_updates_existing_window},
      {"ui_language_preference_applies_at_startup", ui_language_preference_applies_at_startup},
      {"ui_language_missing_preference_uses_system_language", ui_language_missing_preference_uses_system_language},
      {"ui_language_saved_preference_overrides_system_language",
       ui_language_saved_preference_overrides_system_language},
      {"ui_language_invalid_preference_falls_back_to_english", ui_language_invalid_preference_falls_back_to_english},
      {"ui_language_catalog_covers_dialog_status_and_properties",
       ui_language_catalog_covers_dialog_status_and_properties},
      {"ui_filter_gallery_action_retranslates", ui_filter_gallery_action_retranslates},
      {"ui_about_dialog_shows_labeled_external_links", ui_about_dialog_shows_labeled_external_links},
      {"ui_frameless_window_edges_resize", ui_frameless_window_edges_resize},
      {"ui_right_edge_scrollbars_remain_draggable", ui_right_edge_scrollbars_remain_draggable},
      {"ui_svg_icon_resources_are_registered", ui_svg_icon_resources_are_registered},
      {"ui_icon_color_map_covers_every_authored_color", ui_icon_color_map_covers_every_authored_color},
      {"ui_no_widget_ships_unresolved_theme_tokens", ui_no_widget_ships_unresolved_theme_tokens},
      {"ui_theme_palettes_define_every_role", ui_theme_palettes_define_every_role},
      {"ui_theme_adjacent_roles_stay_distinguishable", ui_theme_adjacent_roles_stay_distinguishable},
      {"ui_theme_text_roles_contrast_with_their_backgrounds",
       ui_theme_text_roles_contrast_with_their_backgrounds},
      {"ui_light_scheme_dialog_chrome_separates_from_canvas",
       ui_light_scheme_dialog_chrome_separates_from_canvas},
      {"ui_dialog_chrome_outline_reads_at_the_dialog_edge",
       ui_dialog_chrome_outline_reads_at_the_dialog_edge},
      {"ui_light_scheme_panel_scroll_bar_handle_reads_against_track",
       ui_light_scheme_panel_scroll_bar_handle_reads_against_track},
      {"ui_light_scheme_color_picker_selected_tab_reads_as_selected",
       ui_light_scheme_color_picker_selected_tab_reads_as_selected},
      {"ui_theme_qss_resolves_every_token", ui_theme_qss_resolves_every_token},
      {"ui_status_bar_error_message_flashes_then_persists_until_replaced",
       ui_status_bar_error_message_flashes_then_persists_until_replaced},
      {"ui_blocking_refusal_shows_error_status_and_info_clears_it",
       ui_blocking_refusal_shows_error_status_and_info_clears_it},
  };
}
