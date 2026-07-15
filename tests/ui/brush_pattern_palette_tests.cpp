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
#include "ui/app_settings.hpp"
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
#include <QScrollArea>
#include <QScrollBar>
#include <QScreen>
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

QImage make_bar_tip_image() {
  // 16x16 coverage mask with an opaque horizontal bar through the middle (rows 6-9).
  QImage mask(16, 16, QImage::Format_Grayscale8);
  mask.fill(0);
  for (int y = 6; y <= 9; ++y) {
    auto* row = mask.scanLine(y);
    std::memset(row, 255, 16);
  }
  return mask;
}

void ui_brush_tip_paints_and_erases_with_bitmap_stamp() {
  clear_brush_tip_test_state();
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  auto& library = window.brush_tip_library();
  const auto tip_id = library.add_tip(QStringLiteral("Test Bar"), make_bar_tip_image(), 0.25);
  CHECK(!tip_id.isEmpty());
  window.set_active_brush_tip(tip_id, false);
  CHECK(canvas->has_brush_tip());
  CHECK(canvas->brush_tip_id() == tip_id);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(220, 30, 40));
  canvas->set_brush_size(16);
  canvas->set_brush_softness(0);  // hard stamp: this test checks exact tip semantics
  const auto stamp_widget = canvas->widget_position_for_document_point(QPoint(100, 60));
  drag(*canvas, stamp_widget, stamp_widget);
  QApplication::processEvents();

  const auto document_color = [canvas](QPoint document_point) {
    const auto image = canvas->grab().toImage().convertToFormat(QImage::Format_RGB32);
    return QColor(image.pixel(canvas->widget_position_for_document_point(document_point)));
  };

  // The bar tip paints a 16px-wide, 4px-tall band centered on the click; above/below stay white.
  CHECK(color_close(document_color(QPoint(100, 60)), QColor(220, 30, 40), 90));
  CHECK(color_close(document_color(QPoint(104, 60)), QColor(220, 30, 40), 90));
  CHECK(color_close(document_color(QPoint(100, 72)), QColor(255, 255, 255), 20));
  CHECK(color_close(document_color(QPoint(100, 48)), QColor(255, 255, 255), 20));

  // The eraser shares the bitmap tip: erasing at the same spot restores the white background.
  require_action_by_text(window, QStringLiteral("Eraser"))->trigger();
  canvas->set_brush_size(16);
  canvas->set_brush_softness(0);
  drag(*canvas, stamp_widget, stamp_widget);
  QApplication::processEvents();
  CHECK(color_close(document_color(QPoint(100, 60)), QColor(255, 255, 255), 20));
  save_widget_artifact("ui_brush_tip_stamp_and_erase", *canvas);
}

void ui_brush_tip_abr_import_populates_library_and_picker() {
  clear_brush_tip_test_state();
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  auto& library = window.brush_tip_library();
  QString error;
  QStringList warnings;
  const auto fixture =
      QString::fromStdString(patchy::test::committed_abr_fixture_path("myer-settlement-brushes.abr").string());
  const auto first_id = library.import_abr(fixture, error, warnings);
  CHECK(!first_id.isEmpty());
  CHECK(error.isEmpty());
  CHECK(warnings.isEmpty());
  CHECK(library.entries().size() == 148);
  const auto* first_entry = library.find_entry(first_id);
  CHECK(first_entry != nullptr);
  CHECK(first_entry->name == QStringLiteral("Individual Tree 001"));
  CHECK(first_entry->size == QSize(36, 36));
  CHECK(!first_entry->thumbnail.isNull());

  window.set_active_brush_tip(first_id, false);
  CHECK(canvas->has_brush_tip());
  CHECK(canvas->brush_tip_id() == first_id);
  auto* picker = window.findChild<patchy::ui::BrushTipPicker*>(QStringLiteral("brushTipPicker"));
  CHECK(picker != nullptr);
  CHECK(picker->current_tip_id() == first_id);

  // Painting with the imported tip marks the canvas (tree silhouette, not a round blob).
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(20, 20, 20));
  canvas->set_brush_size(64);
  const auto stamp_widget = canvas->widget_position_for_document_point(QPoint(200, 150));
  drag(*canvas, stamp_widget, stamp_widget);
  QApplication::processEvents();
  const auto image = canvas->grab().toImage().convertToFormat(QImage::Format_RGB32);
  int dark_samples = 0;
  int total_samples = 0;
  for (int y = 110; y <= 190; ++y) {
    for (int x = 160; x <= 240; ++x) {
      const auto widget_point = canvas->widget_position_for_document_point(QPoint(x, y));
      if (!image.rect().contains(widget_point)) {
        continue;
      }
      ++total_samples;
      if (QColor(image.pixel(widget_point)).lightness() < 128) {
        ++dark_samples;
      }
    }
  }
  CHECK(total_samples > 0);
  CHECK(dark_samples > 100);               // the stamp landed
  CHECK(dark_samples < total_samples / 2); // and it is sparse, unlike a filled round dab
  save_widget_artifact("ui_brush_tip_abr_import_stamp", *canvas);

  // Removing the active tip falls back to the procedural round brush.
  CHECK(library.remove_tip(first_id));
  QApplication::processEvents();
  CHECK(!canvas->has_brush_tip());
  CHECK(picker->current_tip_id() == patchy::ui::builtin_round_brush_tip_id());
}

void ui_brush_tip_manager_edits_dynamics() {
  clear_brush_tip_test_state();
  patchy::ui::MainWindow window;
  show_window(window);
  auto& library = window.brush_tip_library();
  const auto tip_id = library.add_tip(QStringLiteral("Bar"), make_bar_tip_image(), 0.25);
  CHECK(!tip_id.isEmpty());

  bool saw_editor = false;
  QTimer::singleShot(0, [&] {
    QDialog* manager = nullptr;
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() == QStringLiteral("brushTipManagerDialog")) {
        manager = qobject_cast<QDialog*>(widget);
      }
    }
    if (manager == nullptr) {
      return;
    }
    QApplication::processEvents();
    auto* dynamics_button = manager->findChild<QPushButton*>(QStringLiteral("brushTipManagerDynamicsButton"));
    CHECK(dynamics_button != nullptr);
    CHECK(dynamics_button->isEnabled());  // the initial tip is selected

    QTimer::singleShot(0, [&] {
      QDialog* editor = nullptr;
      for (auto* widget : QApplication::topLevelWidgets()) {
        if (widget->objectName() == QStringLiteral("brushTipManagerDynamicsDialog")) {
          editor = qobject_cast<QDialog*>(widget);
        }
      }
      if (editor == nullptr) {
        return;
      }
      saw_editor = true;
      editor->findChild<QSpinBox*>(QStringLiteral("dynamicsScatterSpin"))->setValue(200);
      editor->findChild<QSpinBox*>(QStringLiteral("dynamicsCountSpin"))->setValue(4);
      process_events_for(300);  // let the debounced apply run
      save_widget_artifact("ui_brush_tip_manager_dynamics_editor", *editor);
      editor->reject();
    });
    dynamics_button->click();  // blocks in the editor's exec loop until the inner callback closes it
    manager->reject();
  });
  patchy::ui::request_brush_tip_manager(&window, library, tip_id, {}, {});

  CHECK(saw_editor);
  const auto* entry = library.find_entry(tip_id);
  CHECK(entry != nullptr);
  CHECK(std::abs(entry->dynamics.scatter - 2.00) < 1e-9);
  CHECK(entry->dynamics.count == 4);
  CHECK(patchy::ui::brush_tip_entry_has_dynamics(*entry));
  // The badge marks dynamic tips: the decorated thumbnail differs from the plain one.
  CHECK(patchy::ui::brush_tip_thumbnail_with_badge(*entry).toImage() != entry->thumbnail.toImage());
  clear_brush_tip_test_state();
}

void ui_brush_tip_picker_popup_resizes_and_persists() {
  clear_brush_tip_test_state();
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("ui/brushTipPickerPopupSize"));
    settings.sync();
  }
  patchy::ui::MainWindow window;
  show_window(window);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  QApplication::processEvents();
  auto* picker = window.findChild<patchy::ui::BrushTipPicker*>(QStringLiteral("brushTipPicker"));
  CHECK(picker != nullptr);

  const auto find_popup = []() -> QWidget* {
    QWidget* popup = nullptr;
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() == QStringLiteral("brushTipPickerPopup") && widget->isVisible()) {
        popup = widget;
      }
    }
    return popup;
  };

  picker->click();
  QApplication::processEvents();
  auto* popup = find_popup();
  CHECK(popup != nullptr);
  CHECK(popup->findChild<QSizeGrip*>() != nullptr);  // the resize handle
  auto* list = popup->findChild<QListWidget*>(QStringLiteral("brushTipPickerList"));
  CHECK(list != nullptr);
  CHECK(list->maximumWidth() == QWIDGETSIZE_MAX);  // no longer a fixed grid
  CHECK(popup->width() >= 5 * 70 + 28);            // first-open default keeps the classic footprint

  popup->resize(700, 520);
  QApplication::processEvents();
  popup->close();
  QApplication::processEvents();
  CHECK(patchy::ui::app_settings().value(QStringLiteral("ui/brushTipPickerPopupSize")).toSize() ==
        QSize(700, 520));

  picker->click();
  QApplication::processEvents();
  auto* reopened = find_popup();
  CHECK(reopened != nullptr);
  CHECK(reopened->size() == QSize(700, 520));
  reopened->close();
  QApplication::processEvents();
  clear_brush_tip_test_state();
}

void ui_brush_tip_define_from_image_uses_inverted_luminance() {
  clear_brush_tip_test_state();
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  // Paint a black blob on the white document, then capture it as a tip: dark pixels must become
  // coverage, white background must stay clear.
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(0, 0, 0));
  canvas->set_brush_size(40);
  const auto blob_widget = canvas->widget_position_for_document_point(QPoint(512, 384));
  drag(*canvas, blob_widget, blob_widget);
  QApplication::processEvents();

  const auto mask = window.capture_brush_tip_define_source();
  CHECK(!mask.isNull());
  CHECK(mask.size() == QSize(1024, 768));  // no selection: the whole document
  const auto gray = mask.convertToFormat(QImage::Format_Grayscale8);
  CHECK(gray.pixelColor(512, 384).red() > 200);  // blob center = strong coverage
  CHECK(gray.pixelColor(50, 50).red() == 0);     // white background = no coverage

  auto& library = window.brush_tip_library();
  const auto tip_id = library.add_tip(QStringLiteral("Defined Blob"), mask, 0.25);
  CHECK(!tip_id.isEmpty());
  const auto* entry = library.find_entry(tip_id);
  CHECK(entry != nullptr);
  // The stored tip is cropped to the blob, far smaller than the full document.
  CHECK(entry->size.width() < 80);
  CHECK(entry->size.height() < 80);
  CHECK(entry->size.width() >= 30);
}

void ui_brush_tip_cursor_shows_tip_shape() {
  clear_brush_tip_test_state();
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_brush_size(48);

  const auto opaque_bounds = [](const QPixmap& pixmap) {
    const auto image = pixmap.toImage();
    QRect bounds;
    for (int y = 0; y < image.height(); ++y) {
      for (int x = 0; x < image.width(); ++x) {
        if (qAlpha(image.pixel(x, y)) > 0) {
          bounds = bounds.isNull() ? QRect(x, y, 1, 1) : bounds.united(QRect(x, y, 1, 1));
        }
      }
    }
    return bounds;
  };

  // Procedural round brush: the cursor outline is a circle, roughly square bounds.
  const auto round_bounds = opaque_bounds(canvas->cursor().pixmap());
  CHECK(!round_bounds.isNull());
  CHECK(std::abs(round_bounds.width() - round_bounds.height()) <= 6);

  // A wide flat tip must produce a clearly wide, short outline instead.
  auto& library = window.brush_tip_library();
  const auto tip_id = library.add_tip(QStringLiteral("Bar"), make_bar_tip_image(), 0.25);
  CHECK(!tip_id.isEmpty());
  window.set_active_brush_tip(tip_id, false);
  const auto bar_bounds = opaque_bounds(canvas->cursor().pixmap());
  CHECK(!bar_bounds.isNull());
  CHECK(bar_bounds.width() > bar_bounds.height() * 2);

  // Back to round: the circle cursor returns.
  window.set_active_brush_tip(patchy::ui::builtin_round_brush_tip_id(), false);
  const auto restored_bounds = opaque_bounds(canvas->cursor().pixmap());
  CHECK(std::abs(restored_bounds.width() - restored_bounds.height()) <= 6);
  clear_brush_tip_test_state();
}

void ui_brush_tip_picker_popup_offers_define_from_selection() {
  clear_brush_tip_test_state();
  patchy::ui::MainWindow window;
  show_window(window);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  QApplication::processEvents();
  auto* picker = window.findChild<patchy::ui::BrushTipPicker*>(QStringLiteral("brushTipPicker"));
  CHECK(picker != nullptr);

  picker->click();
  QApplication::processEvents();
  QWidget* popup = nullptr;
  for (auto* widget : QApplication::topLevelWidgets()) {
    if (widget->objectName() == QStringLiteral("brushTipPickerPopup") && widget->isVisible()) {
      popup = widget;
    }
  }
  CHECK(popup != nullptr);
  auto* define_button = popup->findChild<QPushButton*>(QStringLiteral("brushTipDefineButton"));
  CHECK(define_button != nullptr);
  CHECK(define_button->isVisible());
  CHECK(popup->findChild<QPushButton*>(QStringLiteral("brushTipImportButton")) != nullptr);
  CHECK(popup->findChild<QPushButton*>(QStringLiteral("brushTipManageButton")) != nullptr);
  save_widget_artifact("ui_brush_tip_picker_popup", *popup);
  popup->close();
  QApplication::processEvents();
  clear_brush_tip_test_state();
}

void ui_brush_tip_picker_keeps_options_bar_height() {
  clear_brush_tip_test_state();
  patchy::ui::MainWindow window;
  show_window(window);
  // Wide enough that every tool's options fit on a single row.
  window.resize(1500, 800);
  QApplication::processEvents();

  auto* options_bar = window.findChild<QToolBar*>(QStringLiteral("Options"));
  CHECK(options_bar != nullptr);

  // The Options bar must keep one constant height across every tool, or the
  // canvas shifts vertically on each tool switch. Brush/Eraser are the
  // regression case: their Tip picker is a QToolButton, whose default style
  // made it the tallest control in the row.
  int common_height = 0;
  for (const char* tool : {"Move", "Marquee", "Lasso", "Magic Wand", "Clone", "Pattern Stamp",
                           "Healing Brush", "Smudge",
                           "Mixer Brush",
                           "Dodge", "Burn", "Sponge", "Fill", "Gradient", "Line", "Rect",
                           "Zoom", "Brush", "Eraser"}) {
    require_action_by_text(window, QString::fromLatin1(tool))->trigger();
    QApplication::processEvents();
    process_events_for(30);
    if (common_height == 0) {
      common_height = options_bar->height();
    }
    CHECK(options_bar->height() == common_height);
  }
  CHECK(common_height > 0);

  // Eraser is the active tool now, so the Tip picker is visible; it must not
  // exceed the shared 26px control height its neighbors use.
  auto* picker = window.findChild<patchy::ui::BrushTipPicker*>(QStringLiteral("brushTipPicker"));
  CHECK(picker != nullptr);
  CHECK(picker->isVisible());
  auto* preset_combo = window.findChild<QComboBox*>(QStringLiteral("brushPresetCombo"));
  CHECK(preset_combo != nullptr);
  CHECK(picker->height() <= preset_combo->height());
  save_widget_artifact("ui_brush_options_bar_height", *options_bar);

  // The Brush-only Dynamics button must obey the same 26px cap (QToolButton +3px size-hint
  // gotcha), and must not appear on the Eraser bar.
  auto* dynamics_button = window.findChild<QToolButton*>(QStringLiteral("brushDynamicsButton"));
  CHECK(dynamics_button != nullptr);
  CHECK(!dynamics_button->isVisible());  // Eraser is active
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  QApplication::processEvents();
  process_events_for(30);
  CHECK(options_bar->height() == common_height);
  CHECK(dynamics_button->isVisible());
  CHECK(dynamics_button->height() <= preset_combo->height());
  clear_brush_tip_test_state();
}

void ui_default_brush_tips_carry_curated_dynamics() {
  clear_brush_tip_test_state();
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("brushes/defaultTipsVersion"), 0);
    settings.sync();
  }

  QString spatter_id;
  QString star_id;
  {
    patchy::ui::MainWindow window;
    show_window(window);
    auto& library = window.brush_tip_library();
    const auto folder = patchy::ui::default_brush_tips_folder_name();
    const auto entry_named = [&library, &folder](const QString& name) -> const patchy::ui::BrushTipEntry* {
      for (const auto& entry : library.entries()) {
        if (entry.folder == folder && entry.name == name) {
          return &entry;
        }
      }
      return nullptr;
    };

    // Fresh seeding lands the curated dynamics with the tips.
    const auto* spatter = entry_named(QStringLiteral("Spatter"));
    CHECK(spatter != nullptr);
    CHECK(spatter->dynamics.active());
    CHECK(std::abs(spatter->dynamics.scatter - 1.50) < 1e-9);
    CHECK(spatter->dynamics.count == 2);
    const auto* bristle = entry_named(QStringLiteral("Bristle"));
    CHECK(bristle != nullptr);
    CHECK(bristle->dynamics.angle_control == patchy::BrushDynamicControl::Direction);
    const auto* grass = entry_named(QStringLiteral("Grass"));
    CHECK(grass != nullptr);
    CHECK(grass->dynamics.angle_control == patchy::BrushDynamicControl::Direction);
    CHECK(grass->dynamics.count == 2);
    // Stability tips deliberately ship without dynamics (Dotted Line and the logo stamp clean).
    for (const auto* name : {"Canvas", "Square", "Calligraphy", "Dotted Line", "RTsoft Logo"}) {
      const auto* entry = entry_named(QString::fromLatin1(name));
      CHECK(entry != nullptr);
      CHECK(!entry->dynamics.active());
    }
    // v3 stamp tips: path stamps follow the stroke direction, scatter stamps scatter.
    const auto* brick = entry_named(QStringLiteral("Brick Road"));
    CHECK(brick != nullptr);
    CHECK(brick->dynamics.angle_control == patchy::BrushDynamicControl::Direction);
    CHECK(std::abs(brick->spacing - 1.0) < 1e-9);
    const auto* leaf = entry_named(QStringLiteral("Leaf"));
    CHECK(leaf != nullptr);
    CHECK(std::abs(leaf->dynamics.scatter - 1.50) < 1e-9);
    CHECK(leaf->dynamics.scatter_both_axes);
    CHECK(leaf->dynamics.count == 2);
    CHECK(leaf->dynamics.flip_x_jitter);
    CHECK(std::abs(leaf->dynamics.angle_jitter - 1.0) < 1e-9);
    const auto* rain = entry_named(QStringLiteral("Rain"));
    CHECK(rain != nullptr);
    CHECK(rain->dynamics.angle_control == patchy::BrushDynamicControl::Off);
    CHECK(rain->dynamics.angle_jitter == 0.0);  // streaks must stay parallel
    CHECK(std::abs(rain->dynamics.scatter - 2.50) < 1e-9);
    const auto* textured_chalk = entry_named(QStringLiteral("Textured Chalk"));
    CHECK(textured_chalk != nullptr);
    CHECK(textured_chalk->dynamics.texture_enabled);
    CHECK(textured_chalk->dynamics.texture_style == patchy::BrushTextureStyle::FineGrain);
    const auto* dual_dots = entry_named(QStringLiteral("Dual Brush Dots"));
    CHECK(dual_dots != nullptr);
    CHECK(dual_dots->dynamics.dual_brush_enabled);
    const auto* color_scatter = entry_named(QStringLiteral("Color Scatter"));
    CHECK(color_scatter != nullptr);
    CHECK(color_scatter->dynamics.color_dynamics_enabled);
    CHECK(std::abs(color_scatter->dynamics.foreground_background_jitter - 0.70) < 1e-9);
    const auto* wet_wash = entry_named(QStringLiteral("Wet Edge Wash"));
    CHECK(wet_wash != nullptr);
    CHECK(wet_wash->dynamics.wet_edges);

    // Prepare the migration scenario: one default tip "reset" to no dynamics (pre-dynamics
    // install state) and one customized by the user.
    spatter_id = spatter->id;
    const auto* star = entry_named(QStringLiteral("Star"));
    CHECK(star != nullptr);
    star_id = star->id;
    CHECK(library.set_tip_dynamics(spatter_id, {}, 0.0, 100.0));
    patchy::BrushDynamics custom;
    custom.scatter = 9.5;
    custom.count = 7;
    CHECK(library.set_tip_dynamics(star_id, custom, 0.0, 100.0));
  }
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("brushes/defaultTipsVersion"), 1);
    settings.sync();
  }

  // A window seeing version 1 runs the one-shot migration: untouched-default tips regain the
  // curated dynamics, customized tips keep the user's values.
  {
    patchy::ui::MainWindow window;
    show_window(window);
    auto& library = window.brush_tip_library();
    const auto* migrated = library.find_entry(spatter_id);
    CHECK(migrated != nullptr);
    CHECK(std::abs(migrated->dynamics.scatter - 1.50) < 1e-9);
    CHECK(migrated->dynamics.count == 2);
    const auto* custom = library.find_entry(star_id);
    CHECK(custom != nullptr);
    CHECK(std::abs(custom->dynamics.scatter - 9.5) < 1e-9);
    CHECK(custom->dynamics.count == 7);
    CHECK(patchy::ui::app_settings().value(QStringLiteral("brushes/defaultTipsVersion")).toInt() == 4);

    // "Restore Default Brushes" also un-messes customized defaults: spacing, tip shape, and
    // dynamics all reset to factory (the migration above deliberately left them alone).
    CHECK(library.set_tip_spacing(star_id, 0.77));
    CHECK(library.reset_default_tips_to_factory() == 1);  // only Star differs from its spec
    const auto* reset_star = library.find_entry(star_id);
    CHECK(reset_star != nullptr);
    CHECK(std::abs(reset_star->spacing - 1.30) < 1e-9);
    CHECK(std::abs(reset_star->dynamics.scatter - 2.00) < 1e-9);
    CHECK(reset_star->dynamics.count == 2);
    CHECK(reset_star->base_angle_degrees == 0.0);
    CHECK(reset_star->base_roundness == 100.0);
    CHECK(library.reset_default_tips_to_factory() == 0);  // idempotent once factory
  }
  clear_brush_tip_test_state();
}

void ui_brush_dynamics_round_brush_session() {
  clear_brush_tip_test_state();
  {
    patchy::ui::MainWindow window;
    show_window(window);
    auto* canvas = require_canvas(window);
    require_action_by_text(window, QStringLiteral("Brush"))->trigger();
    QApplication::processEvents();

    auto* button = window.findChild<QToolButton*>(QStringLiteral("brushDynamicsButton"));
    CHECK(button != nullptr);
    CHECK(button->isVisible());
    CHECK(button->isEnabled());  // the Round brush carries session-only dynamics
    CHECK(!canvas->brush_dynamics().active());

    // Popup edits apply to the canvas without touching the library.
    button->click();
    QApplication::processEvents();
    QWidget* popup = nullptr;
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() == QStringLiteral("brushDynamicsPopup") && widget->isVisible()) {
        popup = widget;
      }
    }
    CHECK(popup != nullptr);
    popup->findChild<QSpinBox*>(QStringLiteral("dynamicsScatterSpin"))->setValue(200);
    popup->findChild<QSpinBox*>(QStringLiteral("dynamicsCountSpin"))->setValue(2);
    popup->close();
    process_events_for(350);  // the popup edit commit is debounced ~200ms
    CHECK(std::abs(canvas->brush_dynamics().scatter - 2.0) < 1e-9);
    CHECK(canvas->brush_dynamics().count == 2);
    CHECK(!canvas->has_brush_tip());                      // still the procedural Round brush
    CHECK(window.brush_tip_library().entries().empty());  // nothing persisted to the library

    // Switching to a bitmap tip and back preserves the session values.
    auto& library = window.brush_tip_library();
    const auto tip_id = library.add_tip(QStringLiteral("Bar"), make_bar_tip_image(), 0.25);
    CHECK(!tip_id.isEmpty());
    window.set_active_brush_tip(tip_id, false);
    CHECK(button->isEnabled());
    CHECK(!canvas->brush_dynamics().active());  // the Bar tip ships without dynamics
    window.set_active_brush_tip(patchy::ui::builtin_round_brush_tip_id(), false);
    CHECK(button->isEnabled());
    CHECK(std::abs(canvas->brush_dynamics().scatter - 2.0) < 1e-9);

    // With dynamics active the Round brush stamps through the synthesized disc tip: scattered
    // dabs land clearly off the stroke line.
    canvas->set_primary_color(QColor(0, 0, 0));
    canvas->set_brush_size(16);
    canvas->set_brush_softness(0);
    canvas->set_brush_dynamics_test_seed(42);
    const auto from = canvas->widget_position_for_document_point(QPoint(300, 200));
    const auto to = canvas->widget_position_for_document_point(QPoint(500, 200));
    drag(*canvas, from, to);
    QApplication::processEvents();
    const auto image = canvas->grab().toImage().convertToFormat(QImage::Format_RGB32);
    int off_line_dark = 0;
    for (int y = 160; y <= 240; y += 2) {
      if (std::abs(y - 200) < 12) {
        continue;  // the plain 16px round stroke corridor
      }
      for (int x = 280; x <= 520; x += 2) {
        const auto widget_point = canvas->widget_position_for_document_point(QPoint(x, y));
        if (!image.rect().contains(widget_point)) {
          continue;
        }
        if (QColor(image.pixel(widget_point)).lightness() < 100) {
          ++off_line_dark;
        }
      }
    }
    CHECK(off_line_dark > 5);
    save_widget_artifact("ui_brush_dynamics_round_scatter_stroke", *canvas);
    canvas->set_brush_dynamics_test_seed(std::nullopt);
  }
  // Session-only by design: a fresh window starts with a plain Round brush again.
  {
    patchy::ui::MainWindow window;
    show_window(window);
    auto* canvas = require_canvas(window);
    CHECK(!canvas->brush_dynamics().active());
  }
  clear_brush_tip_test_state();
}

void ui_brush_dynamics_popup_edits_apply_and_persist() {
  clear_brush_tip_test_state();
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  QApplication::processEvents();

  auto& library = window.brush_tip_library();
  const auto tip_id = library.add_tip(QStringLiteral("Bar"), make_bar_tip_image(), 0.25);
  CHECK(!tip_id.isEmpty());
  patchy::BrushDynamics seeded_dynamics;
  seeded_dynamics.texture_seed = 0x13579BDFU;
  CHECK(library.set_tip_dynamics(tip_id, seeded_dynamics, 0.0, 100.0));
  window.set_active_brush_tip(tip_id, false);

  auto* button = window.findChild<QToolButton*>(QStringLiteral("brushDynamicsButton"));
  CHECK(button != nullptr);
  CHECK(button->isEnabled());
  button->click();
  QApplication::processEvents();

  QWidget* popup = nullptr;
  for (auto* widget : QApplication::topLevelWidgets()) {
    if (widget->objectName() == QStringLiteral("brushDynamicsPopup") && widget->isVisible()) {
      popup = widget;
    }
  }
  CHECK(popup != nullptr);
  popup->findChild<QSpinBox*>(QStringLiteral("dynamicsSizeJitterSpin"))->setValue(40);
  popup->findChild<QSpinBox*>(QStringLiteral("dynamicsScatterSpin"))->setValue(150);
  popup->findChild<QSpinBox*>(QStringLiteral("dynamicsCountSpin"))->setValue(3);
  popup->findChild<QComboBox*>(QStringLiteral("dynamicsAngleControlCombo"))->setCurrentIndex(6);  // Direction
  popup->findChild<QSpinBox*>(QStringLiteral("dynamicsBaseAngleSpin"))->setValue(30);
  popup->findChild<QSpinBox*>(QStringLiteral("dynamicsOpacityJitterSpin"))->setValue(25);
  popup->findChild<QSpinBox*>(QStringLiteral("dynamicsFlowJitterSpin"))->setValue(35);
  auto* texture_enabled =
      popup->findChild<QCheckBox*>(QStringLiteral("dynamicsTextureEnabledCheck"));
  auto* texture_style =
      popup->findChild<QComboBox*>(QStringLiteral("dynamicsTextureStyleCombo"));
  CHECK(texture_enabled != nullptr);
  CHECK(texture_style != nullptr);
  texture_enabled->setChecked(true);
  texture_style->setCurrentIndex(
      texture_style->findData(static_cast<int>(patchy::BrushTextureStyle::Canvas)));
  popup->findChild<QSpinBox*>(QStringLiteral("dynamicsTextureScaleSpin"))->setValue(175);
  popup->findChild<QSpinBox*>(QStringLiteral("dynamicsTextureDepthSpin"))->setValue(65);
  popup->findChild<QCheckBox*>(QStringLiteral("dynamicsTextureInvertCheck"))->setChecked(true);
  popup->findChild<QCheckBox*>(QStringLiteral("dynamicsDualBrushEnabledCheck"))->setChecked(true);
  popup->findChild<QSpinBox*>(QStringLiteral("dynamicsDualBrushSizeSpin"))->setValue(35);
  popup->findChild<QSpinBox*>(QStringLiteral("dynamicsDualBrushHardnessSpin"))->setValue(70);
  popup->findChild<QSpinBox*>(QStringLiteral("dynamicsDualBrushSpacingSpin"))->setValue(125);
  popup->findChild<QCheckBox*>(QStringLiteral("dynamicsColorEnabledCheck"))->setChecked(true);
  popup->findChild<QSpinBox*>(QStringLiteral("dynamicsColorForegroundBackgroundJitterSpin"))
      ->setValue(60);
  popup->findChild<QSpinBox*>(QStringLiteral("dynamicsColorHueJitterSpin"))->setValue(15);
  popup->findChild<QSpinBox*>(QStringLiteral("dynamicsColorSaturationJitterSpin"))->setValue(25);
  popup->findChild<QSpinBox*>(QStringLiteral("dynamicsColorBrightnessJitterSpin"))->setValue(10);
  popup->findChild<QSpinBox*>(QStringLiteral("dynamicsColorPuritySpin"))->setValue(-20);
  popup->findChild<QCheckBox*>(QStringLiteral("dynamicsColorPerTipCheck"))->setChecked(false);
  auto* wet_edges = popup->findChild<QCheckBox*>(QStringLiteral("dynamicsWetEdgesCheck"));
  CHECK(wet_edges != nullptr);
  CHECK(wet_edges->toolTip().contains(QStringLiteral("does not smear"), Qt::CaseInsensitive));
  CHECK(wet_edges->toolTip().contains(QStringLiteral("Smudge")));
  wet_edges->setChecked(true);
  QApplication::processEvents();
  save_widget_artifact("ui_brush_dynamics_popup", *popup);
  auto* scroll_area = popup->findChild<QScrollArea*>();
  CHECK(scroll_area != nullptr);
  scroll_area->verticalScrollBar()->setValue(scroll_area->verticalScrollBar()->maximum());
  QApplication::processEvents();
  save_widget_artifact("ui_brush_dynamics_popup_transfer", *popup);
  popup->close();
  process_events_for(350);  // the popup edit commit is debounced ~200ms

  const auto& dynamics = canvas->brush_dynamics();
  CHECK(std::abs(dynamics.size_jitter - 0.40) < 1e-9);
  CHECK(std::abs(dynamics.scatter - 1.50) < 1e-9);
  CHECK(dynamics.count == 3);
  CHECK(dynamics.angle_control == patchy::BrushDynamicControl::Direction);
  CHECK(std::abs(dynamics.opacity_jitter - 0.25) < 1e-9);
  CHECK(std::abs(dynamics.flow_jitter - 0.35) < 1e-9);
  CHECK(dynamics.texture_enabled);
  CHECK(dynamics.texture_style == patchy::BrushTextureStyle::Canvas);
  CHECK(std::abs(dynamics.texture_scale - 1.75) < 1e-9);
  CHECK(std::abs(dynamics.texture_depth - 0.65) < 1e-9);
  CHECK(dynamics.texture_invert);
  CHECK(dynamics.texture_seed == 0x13579BDFU);  // hidden imported identity survives UI edits
  CHECK(dynamics.dual_brush_enabled);
  CHECK(std::abs(dynamics.dual_brush_size - 0.35) < 1e-9);
  CHECK(std::abs(dynamics.dual_brush_hardness - 0.70) < 1e-9);
  CHECK(std::abs(dynamics.dual_brush_spacing - 1.25) < 1e-9);
  CHECK(dynamics.color_dynamics_enabled);
  CHECK(std::abs(dynamics.foreground_background_jitter - 0.60) < 1e-9);
  CHECK(std::abs(dynamics.hue_jitter - 0.15) < 1e-9);
  CHECK(std::abs(dynamics.saturation_jitter - 0.25) < 1e-9);
  CHECK(std::abs(dynamics.brightness_jitter - 0.10) < 1e-9);
  CHECK(std::abs(dynamics.purity + 0.20) < 1e-9);
  CHECK(!dynamics.color_per_tip);
  CHECK(dynamics.wet_edges);
  CHECK(std::abs(canvas->brush_base_angle_degrees() - 30.0) < 1e-9);

  // The edit persisted to the tip's sidecar on disk.
  QFile sidecar(brush_tip_test_storage_dir() + QStringLiteral("/") + tip_id + QStringLiteral(".json"));
  CHECK(sidecar.open(QIODevice::ReadOnly));
  const auto object = QJsonDocument::fromJson(sidecar.readAll()).object();
  CHECK(object.value(QStringLiteral("baseAngle")).toDouble() == 30.0);
  const auto dynamics_json = object.value(QStringLiteral("dynamics")).toObject();
  CHECK(!dynamics_json.isEmpty());
  CHECK(std::abs(dynamics_json.value(QStringLiteral("sizeJitter")).toDouble() - 0.40) < 1e-9);
  CHECK(dynamics_json.value(QStringLiteral("angleControl")).toString() == QStringLiteral("direction"));
  CHECK(dynamics_json.value(QStringLiteral("count")).toInt() == 3);
  CHECK(std::abs(dynamics_json.value(QStringLiteral("flowJitter")).toDouble() - 0.35) < 1e-9);
  CHECK(dynamics_json.value(QStringLiteral("textureEnabled")).toBool());
  CHECK(dynamics_json.value(QStringLiteral("textureStyle")).toString() == QStringLiteral("canvas"));
  CHECK(dynamics_json.value(QStringLiteral("textureSeed")).toDouble() ==
        static_cast<double>(0x13579BDFU));
  CHECK(dynamics_json.value(QStringLiteral("dualBrushEnabled")).toBool());
  CHECK(dynamics_json.value(QStringLiteral("colorDynamicsEnabled")).toBool());
  CHECK(!dynamics_json.value(QStringLiteral("colorPerTip")).toBool(true));
  CHECK(dynamics_json.value(QStringLiteral("wetEdges")).toBool());

  // A fresh library over the same storage reads the identical dynamics (sidecar round trip);
  // a legacy sidecar without the new keys reads as defaults.
  patchy::ui::BrushTipLibrary second(brush_tip_test_storage_dir());
  const auto* reloaded = second.find_entry(tip_id);
  CHECK(reloaded != nullptr);
  CHECK(std::abs(reloaded->dynamics.scatter - 1.50) < 1e-9);
  CHECK(reloaded->dynamics.count == 3);
  CHECK(reloaded->dynamics.angle_control == patchy::BrushDynamicControl::Direction);
  CHECK(std::abs(reloaded->dynamics.flow_jitter - 0.35) < 1e-9);
  CHECK(reloaded->dynamics.texture_enabled);
  CHECK(reloaded->dynamics.texture_style == patchy::BrushTextureStyle::Canvas);
  CHECK(reloaded->dynamics.texture_seed == 0x13579BDFU);
  CHECK(reloaded->dynamics.dual_brush_enabled);
  CHECK(reloaded->dynamics.color_dynamics_enabled);
  CHECK(!reloaded->dynamics.color_per_tip);
  CHECK(reloaded->dynamics.wet_edges);
  CHECK(std::abs(reloaded->base_angle_degrees - 30.0) < 1e-9);
  const auto legacy_id = second.add_tip(QStringLiteral("Legacy"), make_bar_tip_image(), 0.25);
  const auto* legacy = second.find_entry(legacy_id);
  CHECK(legacy != nullptr);
  CHECK(!legacy->dynamics.active());
  clear_brush_tip_test_state();
}

void ui_brush_dynamics_popup_control_edits_persist() {
  clear_brush_tip_test_state();
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  QApplication::processEvents();

  auto& library = window.brush_tip_library();
  const auto tip_id = library.add_tip(QStringLiteral("Bar"), make_bar_tip_image(), 0.25);
  CHECK(!tip_id.isEmpty());
  window.set_active_brush_tip(tip_id, false);

  auto* button = window.findChild<QToolButton*>(QStringLiteral("brushDynamicsButton"));
  CHECK(button != nullptr);
  button->click();
  QApplication::processEvents();
  QWidget* popup = nullptr;
  for (auto* widget : QApplication::topLevelWidgets()) {
    if (widget->objectName() == QStringLiteral("brushDynamicsPopup") && widget->isVisible()) {
      popup = widget;
    }
  }
  CHECK(popup != nullptr);
  // The popup sizes from the panel's hint, clamped to the screen. On the 600px offscreen
  // display that means the full clamp height — QScrollArea::sizeHint's ~24-font-height cap
  // must not shrink it (the "needless scrollbar on a big monitor" bug).
  CHECK(popup->height() >= 450);

  const auto select_control = [popup](const char* combo_name, patchy::BrushDynamicControl control) {
    auto* combo = popup->findChild<QComboBox*>(QLatin1String(combo_name));
    CHECK(combo != nullptr);
    const auto index = combo->findData(static_cast<int>(control));
    CHECK(index >= 0);
    combo->setCurrentIndex(index);
  };
  // The data-mapped combos default to the slot's default as the first item.
  select_control("dynamicsSizeControlCombo", patchy::BrushDynamicControl::Off);
  select_control("dynamicsOpacityControlCombo", patchy::BrushDynamicControl::PenPressure);
  auto* minimum_opacity = popup->findChild<QSpinBox*>(QStringLiteral("dynamicsMinimumOpacitySpin"));
  CHECK(minimum_opacity != nullptr);
  CHECK(minimum_opacity->isEnabled());  // live once the opacity control has a source
  minimum_opacity->setValue(30);
  select_control("dynamicsFlowControlCombo", patchy::BrushDynamicControl::Fade);
  auto* minimum_flow = popup->findChild<QSpinBox*>(QStringLiteral("dynamicsMinimumFlowSpin"));
  auto* flow_fade = popup->findChild<QSpinBox*>(QStringLiteral("dynamicsFlowFadeStepsSpin"));
  CHECK(minimum_flow != nullptr);
  CHECK(flow_fade != nullptr);
  CHECK(minimum_flow->isEnabled());
  CHECK(flow_fade->isVisible());
  minimum_flow->setValue(15);
  flow_fade->setValue(45);
  auto* scatter_fade = popup->findChild<QSpinBox*>(QStringLiteral("dynamicsScatterFadeStepsSpin"));
  CHECK(scatter_fade != nullptr);
  CHECK(!scatter_fade->isVisible());
  select_control("dynamicsScatterControlCombo", patchy::BrushDynamicControl::Fade);
  QApplication::processEvents();
  CHECK(scatter_fade->isVisible());  // fade steps appear only for a Fade control
  scatter_fade->setValue(40);
  QApplication::processEvents();
  save_widget_artifact("ui_brush_dynamics_popup_controls", *popup);
  popup->close();
  process_events_for(350);  // the popup edit commit is debounced ~200ms

  const auto& dynamics = canvas->brush_dynamics();
  CHECK(dynamics.size_control == patchy::BrushDynamicControl::Off);
  CHECK(dynamics.opacity_control == patchy::BrushDynamicControl::PenPressure);
  CHECK(std::abs(dynamics.minimum_opacity - 0.30) < 1e-9);
  CHECK(dynamics.flow_control == patchy::BrushDynamicControl::Fade);
  CHECK(std::abs(dynamics.minimum_flow - 0.15) < 1e-9);
  CHECK(dynamics.flow_fade_steps == 45);
  CHECK(dynamics.scatter_control == patchy::BrushDynamicControl::Fade);
  CHECK(dynamics.scatter_fade_steps == 40);
  CHECK(dynamics.roundness_control == patchy::BrushDynamicControl::GlobalDefault);  // untouched

  // The sidecar carries the new tokens...
  QFile sidecar(brush_tip_test_storage_dir() + QStringLiteral("/") + tip_id + QStringLiteral(".json"));
  CHECK(sidecar.open(QIODevice::ReadOnly));
  const auto dynamics_json =
      QJsonDocument::fromJson(sidecar.readAll()).object().value(QStringLiteral("dynamics")).toObject();
  CHECK(dynamics_json.value(QStringLiteral("sizeControl")).toString() == QStringLiteral("off"));
  CHECK(dynamics_json.value(QStringLiteral("opacityControl")).toString() == QStringLiteral("penPressure"));
  CHECK(std::abs(dynamics_json.value(QStringLiteral("minimumOpacity")).toDouble() - 0.30) < 1e-9);
  CHECK(dynamics_json.value(QStringLiteral("scatterControl")).toString() == QStringLiteral("fade"));
  CHECK(dynamics_json.value(QStringLiteral("scatterFadeSteps")).toInt() == 40);
  CHECK(dynamics_json.value(QStringLiteral("flowControl")).toString() == QStringLiteral("fade"));
  CHECK(std::abs(dynamics_json.value(QStringLiteral("minimumFlow")).toDouble() - 0.15) < 1e-9);
  CHECK(dynamics_json.value(QStringLiteral("flowFadeSteps")).toInt() == 45);

  // ...and a fresh library over the same storage round-trips them.
  patchy::ui::BrushTipLibrary second(brush_tip_test_storage_dir());
  const auto* reloaded = second.find_entry(tip_id);
  CHECK(reloaded != nullptr);
  CHECK(reloaded->dynamics.size_control == patchy::BrushDynamicControl::Off);
  CHECK(reloaded->dynamics.opacity_control == patchy::BrushDynamicControl::PenPressure);
  CHECK(std::abs(reloaded->dynamics.minimum_opacity - 0.30) < 1e-9);
  CHECK(reloaded->dynamics.scatter_control == patchy::BrushDynamicControl::Fade);
  CHECK(reloaded->dynamics.scatter_fade_steps == 40);
  CHECK(reloaded->dynamics.flow_control == patchy::BrushDynamicControl::Fade);
  CHECK(std::abs(reloaded->dynamics.minimum_flow - 0.15) < 1e-9);
  CHECK(reloaded->dynamics.flow_fade_steps == 45);

  // An Off-only override is not active() (no per-dab work) but must still light the button
  // badge as a deliberate setup.
  patchy::BrushDynamics off_only;
  off_only.size_control = patchy::BrushDynamicControl::Off;
  CHECK(library.set_tip_dynamics(tip_id, off_only, 0.0, 100.0));
  QApplication::processEvents();
  CHECK(!off_only.active());
  CHECK(button->property("dynamicsActive").toBool());
  clear_brush_tip_test_state();
}

void ui_brush_dynamics_button_toggles_popup_closed() {
  clear_brush_tip_test_state();
  patchy::ui::MainWindow window;
  show_window(window);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  QApplication::processEvents();
  auto& library = window.brush_tip_library();
  const auto tip_id = library.add_tip(QStringLiteral("Bar"), make_bar_tip_image(), 0.25);
  window.set_active_brush_tip(tip_id, false);
  auto* button = window.findChild<QToolButton*>(QStringLiteral("brushDynamicsButton"));
  CHECK(button != nullptr);

  const auto find_popup = []() -> QWidget* {
    QWidget* popup = nullptr;
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() == QStringLiteral("brushDynamicsPopup") && widget->isVisible()) {
        popup = widget;
      }
    }
    return popup;
  };

  button->click();
  QApplication::processEvents();
  CHECK(find_popup() != nullptr);

  // A second click while the popup is open (the replayed dismissal click) must close it and
  // NOT immediately reopen it — the close-then-instant-reopen was the July 2026 toggle bug.
  button->click();
  QApplication::processEvents();
  CHECK(find_popup() == nullptr);

  // And the button must not stay dead: a later click opens the popup again.
  process_events_for(350);
  button->click();
  QApplication::processEvents();
  auto* reopened = find_popup();
  CHECK(reopened != nullptr);
  reopened->close();
  QApplication::processEvents();
  clear_brush_tip_test_state();
}

void ui_brush_dynamics_stroke_scatters_with_seed() {
  clear_brush_tip_test_state();
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  QApplication::processEvents();

  auto& library = window.brush_tip_library();
  const auto tip_id = library.add_tip(QStringLiteral("Bar"), make_bar_tip_image(), 2.0);  // sparse dabs
  CHECK(!tip_id.isEmpty());
  window.set_active_brush_tip(tip_id, false);
  canvas->set_primary_color(QColor(0, 0, 0));
  canvas->set_brush_size(16);
  canvas->set_brush_softness(0);

  patchy::BrushDynamics dynamics;
  dynamics.scatter = 1.5;  // up to 24px perpendicular offsets at size 16
  dynamics.count = 3;
  canvas->set_brush_dynamics(dynamics);
  canvas->set_brush_dynamics_test_seed(42);

  const auto from = canvas->widget_position_for_document_point(QPoint(300, 200));
  const auto to = canvas->widget_position_for_document_point(QPoint(500, 200));
  drag(*canvas, from, to);
  QApplication::processEvents();

  // Scattered dabs must land clearly off the stroke line (the bar tip alone spans y 198-202).
  const auto image = canvas->grab().toImage().convertToFormat(QImage::Format_RGB32);
  int off_line_dark = 0;
  for (int y = 160; y <= 240; y += 2) {
    if (std::abs(y - 200) < 10) {
      continue;
    }
    for (int x = 280; x <= 520; x += 2) {
      const auto widget_point = canvas->widget_position_for_document_point(QPoint(x, y));
      if (!image.rect().contains(widget_point)) {
        continue;
      }
      if (QColor(image.pixel(widget_point)).lightness() < 100) {
        ++off_line_dark;
      }
    }
  }
  CHECK(off_line_dark > 5);
  save_widget_artifact("ui_brush_dynamics_scatter_stroke", *canvas);
  canvas->set_brush_dynamics_test_seed(std::nullopt);
  clear_brush_tip_test_state();
}

void ui_brush_color_dynamics_reaches_stroke_compositor() {
  clear_brush_tip_test_state();
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  QApplication::processEvents();

  QImage solid(16, 16, QImage::Format_Grayscale8);
  solid.fill(255);
  auto& library = window.brush_tip_library();
  const auto tip_id = library.add_tip(QStringLiteral("Color Dynamics Probe"), solid, 1.5);
  CHECK(!tip_id.isEmpty());
  window.set_active_brush_tip(tip_id, false);
  canvas->set_primary_color(QColor(255, 0, 0));
  canvas->set_secondary_color(QColor(0, 0, 255));
  canvas->set_brush_size(16);
  canvas->set_brush_softness(0);
  patchy::BrushDynamics dynamics;
  dynamics.color_dynamics_enabled = true;
  dynamics.foreground_background_jitter = 1.0;
  canvas->set_brush_dynamics(dynamics);
  canvas->set_brush_dynamics_test_seed(0xC0104U);

  const auto from = canvas->widget_position_for_document_point(QPoint(300, 200));
  const auto to = canvas->widget_position_for_document_point(QPoint(500, 200));
  drag(*canvas, from, to);
  QApplication::processEvents();

  const auto image = canvas->grab().toImage().convertToFormat(QImage::Format_RGB32);
  int mixed_pixels = 0;
  for (int y = 185; y <= 215; ++y) {
    for (int x = 285; x <= 515; ++x) {
      const auto point = canvas->widget_position_for_document_point(QPoint(x, y));
      if (!image.rect().contains(point)) {
        continue;
      }
      const auto color = QColor(image.pixel(point));
      if (color.red() > 20 && color.blue() > 20 && color.green() < 40) {
        ++mixed_pixels;
      }
    }
  }
  CHECK(mixed_pixels > 100);  // not the captured all-red stroke-start color
  save_widget_artifact("ui_brush_color_dynamics_stroke", *canvas);
  canvas->set_brush_dynamics_test_seed(std::nullopt);
  clear_brush_tip_test_state();
}

void ui_brush_dynamics_abr_import_carries_dynamics() {
  clear_brush_tip_test_state();
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  QApplication::processEvents();

  auto& library = window.brush_tip_library();
  QString error;
  QStringList warnings;
  const auto fixture =
      QString::fromStdString(patchy::test::committed_abr_fixture_path("photoshop-dynamics.abr").string());
  const auto first_id = library.import_abr(fixture, error, warnings);
  CHECK(!first_id.isEmpty());
  CHECK(error.isEmpty());
  CHECK(warnings.isEmpty());
  const auto* entry = library.find_entry(first_id);
  CHECK(entry != nullptr);
  CHECK(entry->dynamics.active());
  CHECK(std::abs(entry->dynamics.size_jitter - 0.37) < 1e-9);
  CHECK(entry->dynamics.angle_control == patchy::BrushDynamicControl::Direction);
  // Photoshop "Off" (no control chosen) imports as follow-the-global-preferences for the
  // aspects the global pen settings cover, plain Off elsewhere.
  CHECK(entry->dynamics.size_control == patchy::BrushDynamicControl::GlobalDefault);
  CHECK(entry->dynamics.opacity_control == patchy::BrushDynamicControl::GlobalDefault);
  CHECK(entry->dynamics.flow_control == patchy::BrushDynamicControl::Off);
  CHECK(entry->dynamics.scatter_control == patchy::BrushDynamicControl::Off);
  CHECK(std::abs(entry->base_angle_degrees - 30.0) < 1e-9);
  CHECK(std::abs(entry->base_roundness - 60.0) < 1e-9);
  CHECK(entry->tool_flow_percent == 100);
  CHECK(entry->tool_airbrush == false);
  patchy::ui::BrushTipLibrary reloaded_library(brush_tip_test_storage_dir());
  const auto* reloaded_entry = reloaded_library.find_entry(first_id);
  CHECK(reloaded_entry != nullptr);
  CHECK(reloaded_entry->tool_flow_percent == 100);
  CHECK(reloaded_entry->tool_airbrush == false);

  // Selecting the tip pushes its dynamics, base shape, and included tool settings to the canvas.
  auto* flow = window.findChild<QSpinBox*>(QStringLiteral("brushFlowSpin"));
  auto* airbrush = window.findChild<QCheckBox*>(QStringLiteral("brushAirbrushCheck"));
  CHECK(flow != nullptr);
  CHECK(airbrush != nullptr);
  flow->setValue(23);
  airbrush->setChecked(true);
  window.set_active_brush_tip(first_id, false);
  CHECK(flow->value() == 100);
  CHECK(!airbrush->isChecked());
  CHECK(canvas->brush_flow() == 100);
  CHECK(!canvas->brush_build_up());
  CHECK(std::abs(canvas->brush_dynamics().scatter - 2.50) < 1e-9);
  CHECK(canvas->brush_dynamics().count == 4);
  CHECK(canvas->brush_base_roundness() == 60);
  auto* button = window.findChild<QToolButton*>(QStringLiteral("brushDynamicsButton"));
  CHECK(button != nullptr);
  CHECK(button->isEnabled());
  CHECK(button->property("dynamicsActive").toBool());

  // A sidecar/library refresh re-applies the tip without repeatedly forcing its imported tool
  // settings over later user edits.
  flow->setValue(41);
  airbrush->setChecked(true);
  CHECK(library.set_tip_dynamics(first_id, entry->dynamics, entry->base_angle_degrees,
                                 entry->base_roundness));
  QApplication::processEvents();
  CHECK(flow->value() == 41);
  CHECK(airbrush->isChecked());

  // Back to Round: dynamics reset with the tip.
  window.set_active_brush_tip(patchy::ui::builtin_round_brush_tip_id(), false);
  CHECK(!canvas->brush_dynamics().active());
  CHECK(canvas->brush_base_roundness() == 100);
  clear_brush_tip_test_state();
}

void ui_brush_tip_softness_feathers_stroke_and_size_reaches_1024() {
  clear_brush_tip_test_state();
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  // The options-bar size controls must allow the full configured brush-size range.
  auto* size_spin = window.findChild<QSpinBox*>(QStringLiteral("brushSizeSpin"));
  auto* size_slider = window.findChild<QSlider*>(QStringLiteral("brushSizeSlider"));
  CHECK(size_spin != nullptr && size_spin->maximum() == patchy::ui::kMaxBrushSize);
  CHECK(size_slider != nullptr && size_slider->maximum() == patchy::ui::kMaxBrushSize);
  size_spin->setValue(patchy::ui::kMaxBrushSize);
  CHECK(canvas->brush_size() == patchy::ui::kMaxBrushSize);
  size_spin->setValue(32);

  auto& library = window.brush_tip_library();
  const auto square = [] {
    QImage mask(16, 16, QImage::Format_Grayscale8);
    mask.fill(255);
    return mask;
  }();
  const auto tip_id = library.add_tip(QStringLiteral("Solid"), square, 0.25);
  CHECK(!tip_id.isEmpty());
  window.set_active_brush_tip(tip_id, false);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(0, 0, 0));
  canvas->set_brush_size(32);

  const auto document_color = [canvas](QPoint document_point) {
    const auto image = canvas->grab().toImage().convertToFormat(QImage::Format_RGB32);
    return QColor(image.pixel(canvas->widget_position_for_document_point(document_point)));
  };

  // Hard stamp: 8px outside the 32px square's edge stays white.
  canvas->set_brush_softness(0);
  const auto hard_center = canvas->widget_position_for_document_point(QPoint(100, 100));
  drag(*canvas, hard_center, hard_center);
  QApplication::processEvents();
  CHECK(color_close(document_color(QPoint(100, 100)), QColor(0, 0, 0), 60));
  CHECK(color_close(document_color(QPoint(124, 100)), QColor(255, 255, 255), 12));

  // Soft stamp elsewhere: mid-feather now carries gray coverage instead of a hard edge.
  canvas->set_brush_softness(100);
  const auto soft_center = canvas->widget_position_for_document_point(QPoint(300, 100));
  drag(*canvas, soft_center, soft_center);
  QApplication::processEvents();
  CHECK(color_close(document_color(QPoint(300, 100)), QColor(0, 0, 0), 60));
  const auto feathered = document_color(QPoint(318, 100));
  CHECK(feathered.lightness() > 40 && feathered.lightness() < 245);
  clear_brush_tip_test_state();
}

std::vector<std::uint8_t> collect_document_pixel_bytes(const patchy::Document& document) {
  std::vector<std::uint8_t> bytes;
  const std::function<void(const std::vector<patchy::Layer>&)> walk = [&](const std::vector<patchy::Layer>& layers) {
    for (const auto& layer : layers) {
      if (layer.kind() == patchy::LayerKind::Group) {
        walk(layer.children());
        continue;
      }
      const auto& pixels = layer.pixels();
      const auto channels = pixels.format().channels;
      for (std::int32_t y = 0; y < pixels.height(); ++y) {
        for (std::int32_t x = 0; x < pixels.width(); ++x) {
          const auto* px = pixels.pixel(x, y);
          for (std::uint16_t channel = 0; channel < channels; ++channel) {
            bytes.push_back(px[channel]);
          }
        }
      }
    }
  };
  walk(document.layers());
  return bytes;
}

void ui_palette_panel_click_sets_foreground_and_chip_tracks_mode() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  const auto* preset = patchy::find_builtin_palette_preset("pico8");
  CHECK(preset != nullptr);
  patchy::DocumentPaletteEditing editing;
  editing.palette.colors.assign(preset->colors.begin(), preset->colors.end());
  editing.palette_revision = 901;
  document.palette_editing() = editing;
  patchy::ui::MainWindowTestAccess::refresh_document_info(window);
  QApplication::processEvents();

  auto* grid = window.findChild<QWidget*>(QStringLiteral("paletteSwatchGrid"));
  CHECK(grid != nullptr);
  CHECK(grid->isVisibleTo(grid->parentWidget()));

  // Cell 2 center: 12 cells per row, 18px cells with a 2px gap.
  const QPoint cell(2 * 20 + 9, 9);
  send_mouse(*grid, QEvent::MouseButtonPress, cell, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*grid, QEvent::MouseButtonRelease, cell, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  const auto expected = preset->colors[2];
  CHECK(canvas->primary_color() == QColor(expected.red, expected.green, expected.blue));

  auto* chip = window.findChild<QToolButton*>(QStringLiteral("paletteModeChip"));
  CHECK(chip != nullptr);
  CHECK(!chip->isHidden());
  CHECK(chip->text().contains(QStringLiteral("16")));
  CHECK(require_action_by_text(window, QStringLiteral("Snap Image to Palette"))->isEnabled());

  // Artifact from a standalone panel: the docked one can be squeezed flat by the
  // other right-area docks on the small offscreen screen.
  patchy::ui::PalettePanel standalone_panel;
  standalone_panel.set_palette(std::vector<patchy::RgbColor>(preset->colors.begin(), preset->colors.end()), true);
  standalone_panel.resize(standalone_panel.sizeHint().expandedTo(QSize(260, 200)));
  save_widget_artifact("ui_palette_panel", standalone_panel);

  // Leaving palette mode hides the chip and disables the snap commands. The
  // white background is off the PICO-8 palette, so the keep-look prompt
  // appears; restore the original colors to keep this test about the chip.
  QTimer::singleShot(0, [&] {
    auto* box =
        qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("convertToRgbKeepLookMessageBox")));
    CHECK(box != nullptr);
    for (auto* button : box->buttons()) {
      if (box->buttonRole(button) == QMessageBox::DestructiveRole) {
        button->click();
        return;
      }
    }
    CHECK(false);
  });
  require_action_by_text(window, QStringLiteral("RGB Color"))->trigger();
  QApplication::processEvents();
  CHECK(!document.palette_editing().has_value());
  CHECK(chip->isHidden());
  CHECK(!require_action_by_text(window, QStringLiteral("Snap Image to Palette"))->isEnabled());
  // The palette stays attached for a lossless round trip back to indexed mode.
  CHECK(document.indexed_palette().has_value());
  CHECK(document.indexed_palette()->colors.size() == 16);
}

void ui_convert_to_indexed_dialog_converts_and_undoes() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  // Paint an off-palette red blob first so the conversion has real work to do.
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(203, 47, 58));
  canvas->set_brush_size(30);
  canvas->set_brush_softness(100);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(60, 60)),
       canvas->widget_position_for_document_point(QPoint(180, 90)));
  QApplication::processEvents();

  const auto rgb_bytes = collect_document_pixel_bytes(document);

  const std::function<void(int)> drive_dialog = [&](int attempts) {
    QTimer::singleShot(0, [&, attempts] {
      auto* dialog = find_top_level_dialog(QStringLiteral("paletteConvertDialog"));
      if (dialog == nullptr) {
        if (attempts > 0) {
          drive_dialog(attempts - 1);
        }
        return;
      }
      auto* source = dialog->findChild<QComboBox*>(QStringLiteral("paletteConvertSourceCombo"));
      CHECK(source != nullptr);
      const auto pico8_row = source->findText(QStringLiteral("PICO-8"));
      CHECK(pico8_row >= 0);
      source->setCurrentIndex(pico8_row);
      // The spin boxes must show the large - / + dialog buttons (24px), not the
      // native micro arrows (the dialog_spinbox_button_style convention).
      auto* colors_spin = dialog->findChild<QSpinBox*>(QStringLiteral("paletteConvertColorsSpin"));
      CHECK(colors_spin != nullptr);
      QStyleOptionSpinBox spin_option;
      spin_option.initFrom(colors_spin);
      const auto up_rect = colors_spin->style()->subControlRect(QStyle::CC_SpinBox, &spin_option,
                                                                QStyle::SC_SpinBoxUp, colors_spin);
      CHECK(up_rect.width() >= 20);
      CHECK(up_rect.height() >= 20);
      save_widget_artifact("ui_palette_convert_dialog", *dialog);
      auto* buttons = dialog->findChild<QDialogButtonBox*>();
      CHECK(buttons != nullptr);
      buttons->button(QDialogButtonBox::Ok)->click();
    });
  };
  drive_dialog(5);
  require_action_by_text(window, QStringLiteral("Indexed (Palette)..."))->trigger();
  QApplication::processEvents();

  CHECK(document.palette_editing().has_value());
  CHECK(document.palette_editing()->palette.colors.size() == 16);
  patchy::PaletteLut lut;
  lut.build(document.palette_editing()->palette.colors);
  for (const auto& layer : document.layers()) {
    if (layer.kind() == patchy::LayerKind::Group || layer.kind() == patchy::LayerKind::Adjustment) {
      continue;
    }
    CHECK(patchy::pixels_are_palette_clean(layer.pixels(), lut));
  }
  CHECK(require_action_by_text(window, QStringLiteral("Indexed (Palette)..."))->isChecked());

  // One undo restores both the RGB pixels and the mode flag.
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(!document.palette_editing().has_value());
  CHECK(collect_document_pixel_bytes(document) == rgb_bytes);
  CHECK(require_action_by_text(window, QStringLiteral("RGB Color"))->isChecked());

  require_action_by_text(window, QStringLiteral("Redo"))->trigger();
  QApplication::processEvents();
  CHECK(document.palette_editing().has_value());
  save_widget_artifact("ui_palette_convert", *canvas);
}

void ui_indexed_bmp_open_adopts_palette() {
  SettingsValueRestorer policy_restorer(QStringLiteral("imports/adoptIndexedPalette"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("imports/adoptIndexedPalette"), QStringLiteral("always"));
  }

  // A 4-color indexed BMP written through Patchy's own writer.
  patchy::Document source(8, 8, patchy::PixelFormat::rgb8());
  patchy::PixelBuffer pixels(8, 8, patchy::PixelFormat::rgb8());
  const std::array<patchy::RgbColor, 4> colors = {{{0, 0, 0}, {255, 255, 255}, {200, 30, 40}, {40, 60, 200}}};
  for (std::int32_t y = 0; y < 8; ++y) {
    for (std::int32_t x = 0; x < 8; ++x) {
      const auto& color = colors[static_cast<std::size_t>((x / 2 + y / 2) % 4)];
      auto* px = pixels.pixel(x, y);
      px[0] = color.red;
      px[1] = color.green;
      px[2] = color.blue;
    }
  }
  source.add_pixel_layer("Art", std::move(pixels));
  std::filesystem::create_directories("test-artifacts");
  const auto path =
      QFileInfo(QDir(QStringLiteral("test-artifacts")).filePath(QStringLiteral("ui_palette_adopt_source.bmp")))
          .absoluteFilePath();
  patchy::bmp::WriteOptions options;
  options.encoding = patchy::bmp::BmpEncoding::Indexed4;
  options.palette_mode = patchy::bmp::BmpPaletteMode::Quantize;
  patchy::bmp::DocumentIo::write_file(source, std::filesystem::path(path.toStdWString()), options);

  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();

  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(document.palette_editing().has_value());
  CHECK(document.palette_editing()->palette.colors.size() == 4);
  auto* chip = window.findChild<QToolButton*>(QStringLiteral("paletteModeChip"));
  CHECK(chip != nullptr);
  CHECK(!chip->isHidden());
}

void ui_indexed_tga_open_adopts_palette() {
  SettingsValueRestorer policy_restorer(QStringLiteral("imports/adoptIndexedPalette"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("imports/adoptIndexedPalette"), QStringLiteral("always"));
  }

  // A 4-color indexed TGA written through Patchy's own palette-mode writer.
  patchy::Document source(8, 8, patchy::PixelFormat::rgb8());
  const std::array<patchy::RgbColor, 4> colors = {{{0, 0, 0}, {255, 255, 255}, {200, 30, 40}, {40, 60, 200}}};
  patchy::DocumentPaletteEditing editing;
  editing.palette.colors.assign(colors.begin(), colors.end());
  editing.palette_revision = 1;
  source.palette_editing() = editing;
  patchy::PixelBuffer pixels(8, 8, patchy::PixelFormat::rgb8());
  for (std::int32_t y = 0; y < 8; ++y) {
    for (std::int32_t x = 0; x < 8; ++x) {
      const auto& color = colors[static_cast<std::size_t>((x / 2 + y / 2) % 4)];
      auto* px = pixels.pixel(x, y);
      px[0] = color.red;
      px[1] = color.green;
      px[2] = color.blue;
    }
  }
  source.add_pixel_layer("Art", std::move(pixels));
  std::filesystem::create_directories("test-artifacts");
  const auto path =
      QFileInfo(QDir(QStringLiteral("test-artifacts")).filePath(QStringLiteral("ui_palette_adopt_source.tga")))
          .absoluteFilePath();
  patchy::tga::DocumentIo::write_file(source, std::filesystem::path(path.toStdWString()));

  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();

  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(document.palette_editing().has_value());
  CHECK(document.palette_editing()->palette.colors.size() == 4);
  const auto* corner = document.layers().front().pixels().pixel(0, 0);
  CHECK(corner[0] == 0);
  const auto* red = document.layers().front().pixels().pixel(4, 0);
  CHECK(red[0] == 200);
}

void ui_palette_mode_bmp_save_defaults_to_exact_indexed() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  // No palette mode: the helper returns the stored defaults untouched.
  const auto stored = patchy::ui::load_image_save_option_defaults();
  const auto plain = patchy::ui::MainWindowTestAccess::image_save_defaults(window);
  CHECK(plain.bmp_encoding == stored.bmp_encoding);
  CHECK(plain.bmp_palette_mode == stored.bmp_palette_mode);

  // A 4-color palette defaults to 2-bit exact indexed.
  const auto* gameboy = patchy::find_builtin_palette_preset("gameboy");
  CHECK(gameboy != nullptr);
  patchy::DocumentPaletteEditing editing;
  editing.palette.colors.assign(gameboy->colors.begin(), gameboy->colors.end());
  editing.palette_revision = 1;
  document.palette_editing() = editing;
  auto defaults = patchy::ui::MainWindowTestAccess::image_save_defaults(window);
  CHECK(defaults.bmp_encoding == patchy::bmp::BmpEncoding::Indexed2);
  CHECK(defaults.bmp_palette_mode == patchy::bmp::BmpPaletteMode::Exact);

  // A 54-color palette defaults to 8-bit exact indexed.
  const auto* nes = patchy::find_builtin_palette_preset("nes");
  CHECK(nes != nullptr);
  document.palette_editing()->palette.colors.assign(nes->colors.begin(), nes->colors.end());
  document.palette_editing()->palette_revision = 2;
  defaults = patchy::ui::MainWindowTestAccess::image_save_defaults(window);
  CHECK(defaults.bmp_encoding == patchy::bmp::BmpEncoding::Indexed8);
  CHECK(defaults.bmp_palette_mode == patchy::bmp::BmpPaletteMode::Exact);
}

void ui_preferences_indexed_open_policy_persists() {
  SettingsValueRestorer policy_restorer(QStringLiteral("imports/adoptIndexedPalette"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("imports/adoptIndexedPalette"), QStringLiteral("always"));
    settings.sync();
  }

  patchy::ui::MainWindow window;
  show_window(window);

  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    auto* combo = dialog->findChild<QComboBox*>(QStringLiteral("preferencesIndexedOpenCombo"));
    CHECK(combo != nullptr);
    CHECK(combo->currentData().toString() == QStringLiteral("always"));
    const auto ask_index = combo->findData(QStringLiteral("ask"));
    CHECK(ask_index >= 0);
    combo->setCurrentIndex(ask_index);
    saw_dialog = true;
    dialog->accept();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);

  auto settings = patchy::ui::app_settings();
  CHECK(settings.value(QStringLiteral("imports/adoptIndexedPalette")).toString() == QStringLiteral("ask"));
}

void ui_palette_panel_swap_copy_paste_and_index_readout() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  const auto* preset = patchy::find_builtin_palette_preset("pico8");
  CHECK(preset != nullptr);
  patchy::DocumentPaletteEditing editing;
  editing.palette.colors.assign(preset->colors.begin(), preset->colors.end());
  editing.palette_revision = 1;
  document.palette_editing() = editing;
  patchy::ui::MainWindowTestAccess::refresh_document_info(window);
  QApplication::processEvents();

  auto* grid = window.findChild<QWidget*>(QStringLiteral("paletteSwatchGrid"));
  CHECK(grid != nullptr);
  auto* count_label = window.findChild<QLabel*>(QStringLiteral("paletteCountLabel"));
  CHECK(count_label != nullptr);
  const auto cell_center = [](int index) { return QPoint((index % 12) * 20 + 9, (index / 12) * 20 + 9); };

  // Clicking a swatch reports its index in the panel readout.
  send_mouse(*grid, QEvent::MouseButtonPress, cell_center(2), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*grid, QEvent::MouseButtonRelease, cell_center(2), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(count_label->text().contains(QStringLiteral("Index 2")));
  const auto original_2 = preset->colors[2];
  const auto original_5 = preset->colors[5];

  // Dragging swatch 2 onto swatch 5 swaps the colors; the selection follows the
  // dragged color to its new index.
  send_mouse(*grid, QEvent::MouseButtonPress, cell_center(2), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*grid, QEvent::MouseMove, cell_center(3), Qt::NoButton, Qt::LeftButton);
  send_mouse(*grid, QEvent::MouseMove, cell_center(5), Qt::NoButton, Qt::LeftButton);
  send_mouse(*grid, QEvent::MouseButtonRelease, cell_center(5), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  const auto& swapped = document.palette_editing()->palette.colors;
  CHECK(patchy::palette_color_key(swapped[2]) == patchy::palette_color_key(original_5));
  CHECK(patchy::palette_color_key(swapped[5]) == patchy::palette_color_key(original_2));
  CHECK(count_label->text().contains(QStringLiteral("Index 5")));

  // Edit > Copy acts on the swatch while focus is inside the panel.
  grid->setFocus();
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Copy"))->trigger();
  QApplication::processEvents();
  CHECK(QApplication::clipboard()->text() ==
        QColor(original_2.red, original_2.green, original_2.blue).name());

  // Edit > Paste writes a hex clipboard color into the selected swatch, undoably.
  QApplication::clipboard()->setText(QStringLiteral("#123456"));
  require_action_by_text(window, QStringLiteral("Paste"))->trigger();
  QApplication::processEvents();
  CHECK(patchy::palette_color_key(document.palette_editing()->palette.colors[5]) == 0x123456U);
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(patchy::palette_color_key(document.palette_editing()->palette.colors[5]) ==
        patchy::palette_color_key(original_2));

  // Duplicate colors flag the readout: identical entries cannot be told apart in
  // the artwork, so exports use the first matching index.
  CHECK(!count_label->text().contains(QStringLiteral("duplicates")));
  document.palette_editing()->palette.colors[1] = document.palette_editing()->palette.colors[0];
  patchy::ui::MainWindowTestAccess::refresh_document_info(window);
  QApplication::processEvents();
  CHECK(count_label->text().contains(QStringLiteral("duplicates")));
}

void ui_color_picker_palette_dropdown_tracks_mode_and_choice() {
  ensure_artifact_dir();
  SettingsValueRestorer choice_restorer(QStringLiteral("palettes/lastPaletteChoice"));
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("palettes/lastPaletteChoice"));
  }

  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* foreground = window.findChild<QPushButton*>(QStringLiteral("foregroundColorButton"));
  CHECK(foreground != nullptr);

  // No palette mode and no remembered choice: the picker opens on Basic colors
  // with the Current-palette row disabled (nothing attached).
  foreground->click();
  QApplication::processEvents();
  auto* dialog = find_top_level_dialog(QStringLiteral("patchyColorDialog"));
  CHECK(dialog != nullptr);
  auto* combo = dialog->findChild<QComboBox*>(QStringLiteral("patchyColorPaletteCombo"));
  CHECK(combo != nullptr);
  CHECK(combo->currentData().toString() == QStringLiteral("basic"));
  auto* model = qobject_cast<QStandardItemModel*>(combo->model());
  CHECK(model != nullptr);
  CHECK(!model->item(1)->isEnabled());

  // The big built-in palettes are present with their full color counts.
  auto* grid = dialog->findChild<QWidget*>(QStringLiteral("patchyColorPaletteGrid"));
  CHECK(grid != nullptr);
  const auto vga_row = combo->findData(QStringLiteral("vga256"));
  CHECK(vga_row >= 0);
  combo->setCurrentIndex(vga_row);
  QApplication::processEvents();
  CHECK(grid->property("paletteColorCount").toInt() == 246);
  const auto dink_row = combo->findData(QStringLiteral("dink"));
  CHECK(dink_row >= 0);
  combo->setCurrentIndex(dink_row);
  QApplication::processEvents();
  CHECK(grid->property("paletteColorCount").toInt() == 256);

  // Choosing a preset shows its swatches, a swatch click picks that color, and
  // the choice is remembered (shared with the Palette panel's preset menu).
  const auto pico8_row = combo->findData(QStringLiteral("pico8"));
  CHECK(pico8_row >= 0);
  combo->setCurrentIndex(pico8_row);
  QApplication::processEvents();
  const auto* preset = patchy::find_builtin_palette_preset("pico8");
  CHECK(preset != nullptr);
  CHECK(grid->property("paletteColorCount").toInt() == static_cast<int>(preset->colors.size()));
  const auto cell = grid->property("paletteCellSize").toInt() + grid->property("paletteCellGap").toInt();
  const auto columns = grid->property("paletteColumns").toInt();
  CHECK(cell > 0);
  CHECK(columns >= 2);
  const auto cell_center = [cell, columns](int index) {
    return QPoint((index % columns) * cell + 5, (index / columns) * cell + 5);
  };
  send_mouse(*grid, QEvent::MouseButtonPress, cell_center(1), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*grid, QEvent::MouseButtonRelease, cell_center(1), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  const auto expected_preset = preset->colors[1];
  CHECK(canvas->primary_color() == QColor(expected_preset.red, expected_preset.green, expected_preset.blue));
  {
    auto settings = patchy::ui::app_settings();
    CHECK(settings.value(QStringLiteral("palettes/lastPaletteChoice")).toString() == QStringLiteral("pico8"));
  }

  // Palette mode turning on switches the open picker to the current palette and
  // enables the row; a swatch click picks from the document palette.
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  patchy::DocumentPaletteEditing editing;
  editing.palette.colors = {patchy::RgbColor{10, 20, 30}, patchy::RgbColor{200, 100, 50},
                            patchy::RgbColor{240, 240, 240}};
  // Far above anything MainWindow::next_palette_revision() hands out during a
  // test run: the paste below edits the palette through the real hook, and a
  // colliding hand-set revision would leave the canvas LUT cache stale.
  editing.palette_revision = 0xFEED0001ULL;
  document.palette_editing() = editing;
  patchy::ui::MainWindowTestAccess::refresh_document_info(window);
  QApplication::processEvents();
  CHECK(combo->currentData().toString() == QStringLiteral("current"));
  CHECK(model->item(1)->isEnabled());
  CHECK(grid->property("paletteColorCount").toInt() == 3);
  send_mouse(*grid, QEvent::MouseButtonPress, cell_center(1), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*grid, QEvent::MouseButtonRelease, cell_center(1), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(canvas->primary_color() == QColor(200, 100, 50));

  // Pasting into the selected cell edits the document palette entry, undoably
  // (the picker routes through MainWindow's apply_palette_entry_color hook).
  CHECK(grid->property("paletteSelectedIndex").toInt() == 1);
  grid->setFocus();
  QApplication::processEvents();
  QApplication::clipboard()->setText(QStringLiteral("#0F4C81"));
  require_action(window, "editPasteAction")->trigger();
  QApplication::processEvents();
  CHECK(document.palette_editing().has_value());
  CHECK(patchy::palette_color_key(document.palette_editing()->palette.colors[1]) == 0x0F4C81U);
  auto* picker = dialog->findChild<patchy::ui::PatchyColorPicker*>(QStringLiteral("patchyAdvancedColorPicker"));
  CHECK(picker != nullptr);
  CHECK(picker->currentColor() == QColor(0x0F, 0x4C, 0x81));
  CHECK(canvas->primary_color() == QColor(0x0F, 0x4C, 0x81));
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(patchy::palette_color_key(document.palette_editing()->palette.colors[1]) == 0xC86432U);
  save_widget_artifact("ui_color_picker_palette_dropdown", *dialog);

  // The mode switch never overwrites the remembered choice: a fresh picker
  // outside palette mode re-opens on the remembered preset.
  dialog->close();
  QApplication::processEvents();
  document.palette_editing().reset();
  patchy::ui::MainWindowTestAccess::refresh_document_info(window);
  QApplication::processEvents();
  foreground->click();
  QApplication::processEvents();
  dialog = find_top_level_dialog(QStringLiteral("patchyColorDialog"));
  CHECK(dialog != nullptr);
  combo = dialog->findChild<QComboBox*>(QStringLiteral("patchyColorPaletteCombo"));
  CHECK(combo != nullptr);
  CHECK(combo->currentData().toString() == QStringLiteral("pico8"));
  dialog->close();
  QApplication::processEvents();
}

void ui_palette_panel_copy_hex_and_updates_open_picker() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  const auto* preset = patchy::find_builtin_palette_preset("pico8");
  CHECK(preset != nullptr);
  patchy::DocumentPaletteEditing editing;
  editing.palette.colors.assign(preset->colors.begin(), preset->colors.end());
  editing.palette_revision = 1;
  document.palette_editing() = editing;
  patchy::ui::MainWindowTestAccess::refresh_document_info(window);
  QApplication::processEvents();

  auto* grid = window.findChild<QWidget*>(QStringLiteral("paletteSwatchGrid"));
  CHECK(grid != nullptr);
  auto* copy_button = window.findChild<QToolButton*>(QStringLiteral("paletteCopyHexButton"));
  CHECK(copy_button != nullptr);
  const auto cell_center = [](int index) { return QPoint((index % 12) * 20 + 9, (index / 12) * 20 + 9); };

  // The readout's Copy button copies the selected swatch's hex code.
  send_mouse(*grid, QEvent::MouseButtonPress, cell_center(2), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*grid, QEvent::MouseButtonRelease, cell_center(2), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(copy_button->isEnabled());
  QApplication::clipboard()->setText(QString());
  copy_button->click();
  QApplication::processEvents();
  CHECK(QApplication::clipboard()->text() ==
        QColor(preset->colors[2].red, preset->colors[2].green, preset->colors[2].blue).name());

  // Clicking a swatch while a transient request picker is open (the layer-style
  // "Choose color" shape) pushes the color into the picker and fires its live
  // callback, so the requesting dialog updates too.
  QColor live_color;
  bool drove_picker = false;
  QTimer::singleShot(0, [&] {
    auto* request_dialog = find_top_level_dialog(QStringLiteral("patchyColorDialog"));
    CHECK(request_dialog != nullptr);
    send_mouse(*grid, QEvent::MouseButtonPress, cell_center(5), Qt::LeftButton, Qt::LeftButton);
    send_mouse(*grid, QEvent::MouseButtonRelease, cell_center(5), Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    auto* picker =
        request_dialog->findChild<patchy::ui::PatchyColorPicker*>(QStringLiteral("patchyAdvancedColorPicker"));
    CHECK(picker != nullptr);
    const QColor expected(preset->colors[5].red, preset->colors[5].green, preset->colors[5].blue);
    CHECK(picker->currentColor() == expected);
    CHECK(live_color == expected);
    drove_picker = true;
    request_dialog->reject();
  });
  const auto result = patchy::ui::request_patchy_color(&window, QColor(1, 2, 3), QStringLiteral("Overlay Color"),
                                                       [&live_color](QColor color) { live_color = color; });
  CHECK(drove_picker);
  CHECK(!result.has_value());

  // The persistent Foreground color panel mirrors palette clicks as well.
  auto* foreground = window.findChild<QPushButton*>(QStringLiteral("foregroundColorButton"));
  CHECK(foreground != nullptr);
  foreground->click();
  QApplication::processEvents();
  auto* panel_dialog = find_top_level_dialog(QStringLiteral("patchyColorDialog"));
  CHECK(panel_dialog != nullptr);
  auto* panel_picker =
      panel_dialog->findChild<patchy::ui::PatchyColorPicker*>(QStringLiteral("patchyAdvancedColorPicker"));
  CHECK(panel_picker != nullptr);
  send_mouse(*grid, QEvent::MouseButtonPress, cell_center(7), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*grid, QEvent::MouseButtonRelease, cell_center(7), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  const QColor expected_panel(preset->colors[7].red, preset->colors[7].green, preset->colors[7].blue);
  CHECK(panel_picker->currentColor() == expected_panel);
  CHECK(canvas->primary_color() == expected_panel);
  panel_dialog->close();
  QApplication::processEvents();
}

void ui_convert_to_indexed_preview_zoom_and_pan() {
  ensure_artifact_dir();
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  // Texture at the document center so panning visibly shifts the zoomed preview.
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(203, 47, 58));
  canvas->set_brush_size(40);
  const QPoint center(document.width() / 2, document.height() / 2);
  drag(*canvas, canvas->widget_position_for_document_point(center + QPoint(-50, -20)),
       canvas->widget_position_for_document_point(center + QPoint(50, 20)));
  QApplication::processEvents();

  bool drove_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("paletteConvertDialog"));
    CHECK(dialog != nullptr);
    auto* preview = dialog->findChild<QWidget*>(QStringLiteral("paletteConvertPreview"));
    CHECK(preview != nullptr);
    CHECK(preview->property("previewFitMode").toBool());
    auto* zoom_label = dialog->findChild<QLabel*>(QStringLiteral("paletteConvertZoomLabel"));
    CHECK(zoom_label != nullptr);
    CHECK(zoom_label->text().contains(QStringLiteral("%")));

    auto* zoom_fit = dialog->findChild<QToolButton*>(QStringLiteral("paletteConvertZoomFit"));
    auto* zoom_100 = dialog->findChild<QToolButton*>(QStringLiteral("paletteConvertZoom100"));
    auto* zoom_in = dialog->findChild<QToolButton*>(QStringLiteral("paletteConvertZoomIn"));
    auto* zoom_out = dialog->findChild<QToolButton*>(QStringLiteral("paletteConvertZoomOut"));
    CHECK(zoom_fit != nullptr);
    CHECK(zoom_100 != nullptr);
    CHECK(zoom_in != nullptr);
    CHECK(zoom_out != nullptr);

    zoom_100->click();
    QApplication::processEvents();
    CHECK(!preview->property("previewFitMode").toBool());
    CHECK(preview->property("previewZoomPercent").toInt() == 100);
    zoom_in->click();
    QApplication::processEvents();
    CHECK(preview->property("previewZoomPercent").toInt() == 150);

    // Dragging pans the zoomed view: the rendered pixels shift.
    const auto before = preview->grab().toImage();
    drag(*preview, QPoint(preview->width() / 2, preview->height() / 2),
         QPoint(preview->width() / 2 - 60, preview->height() / 2 - 40));
    QApplication::processEvents();
    const auto after = preview->grab().toImage();
    CHECK(before != after);
    CHECK(preview->property("previewZoomPercent").toInt() == 150);

    zoom_out->click();
    QApplication::processEvents();
    CHECK(preview->property("previewZoomPercent").toInt() == 100);
    zoom_fit->click();
    QApplication::processEvents();
    CHECK(preview->property("previewFitMode").toBool());
    save_widget_artifact("ui_palette_convert_zoom", *dialog);
    drove_dialog = true;
    dialog->reject();
  });
  require_action(window, "imageModeIndexedAction")->trigger();
  QApplication::processEvents();
  CHECK(drove_dialog);
  CHECK(!document.palette_editing().has_value());
}

void ui_convert_to_rgb_prompts_to_keep_palettized_look() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  // Two-color palette, then write an off-palette red block straight into the
  // active layer's pixels — the way an advisory operation (filter, adjustment,
  // paste) drifts a palette-mode document off its palette. The canvas displays
  // it snapped to black; the pixels secretly stay red.
  patchy::DocumentPaletteEditing editing;
  editing.palette.colors = {patchy::RgbColor{0, 0, 0}, patchy::RgbColor{255, 255, 255}};
  editing.palette_revision = 903;
  document.palette_editing() = editing;
  const auto layer_id = document.active_layer_id();
  CHECK(layer_id.has_value());
  auto* layer = document.find_layer(*layer_id);
  CHECK(layer != nullptr);
  auto& pixels = layer->pixels();
  CHECK(pixels.format().channels == 4);
  for (std::int32_t y = 30; y < 90; ++y) {
    for (std::int32_t x = 40; x < 120; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = 200;
      px[1] = 40;
      px[2] = 60;
      px[3] = 255;
    }
  }
  canvas->document_changed();
  patchy::ui::MainWindowTestAccess::refresh_document_info(window);
  QApplication::processEvents();
  const auto dirty_bytes = collect_document_pixel_bytes(document);

  const auto click_prompt_button = [&](QMessageBox::ButtonRole role) {
    auto* box =
        qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("convertToRgbKeepLookMessageBox")));
    CHECK(box != nullptr);
    for (auto* button : box->buttons()) {
      if (box->buttonRole(button) == role) {
        button->click();
        return;
      }
    }
    CHECK(false);
  };

  // Keep Palettized Look: the snapped display colors become the pixels ((200,
  // 40, 60) is nearer black than white), in the same undo step as the mode
  // change.
  QTimer::singleShot(0, [&] { click_prompt_button(QMessageBox::AcceptRole); });
  require_action_by_text(window, QStringLiteral("RGB Color"))->trigger();
  QApplication::processEvents();
  CHECK(!document.palette_editing().has_value());
  {
    auto* converted = document.find_layer(*layer_id);
    CHECK(converted != nullptr);
    const auto* px = converted->pixels().pixel(50, 40);
    CHECK(px[0] == 0);
    CHECK(px[1] == 0);
    CHECK(px[2] == 0);
    CHECK(px[3] == 255);
  }

  // One undo restores palette mode and the off-palette pixels.
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(document.palette_editing().has_value());
  CHECK(collect_document_pixel_bytes(document) == dirty_bytes);

  // Restore Original Colors: the lossless exit — pixels stay byte-identical.
  QTimer::singleShot(0, [&] { click_prompt_button(QMessageBox::DestructiveRole); });
  require_action_by_text(window, QStringLiteral("RGB Color"))->trigger();
  QApplication::processEvents();
  CHECK(!document.palette_editing().has_value());
  CHECK(collect_document_pixel_bytes(document) == dirty_bytes);

  // A palette-clean document never prompts: converting is silent and lossless.
  patchy::DocumentPaletteEditing clean_editing;
  clean_editing.palette.colors = {patchy::RgbColor{0, 0, 0}, patchy::RgbColor{255, 255, 255},
                                  patchy::RgbColor{200, 40, 60}};
  clean_editing.palette_revision = 904;
  document.palette_editing() = clean_editing;
  patchy::ui::MainWindowTestAccess::refresh_document_info(window);
  QApplication::processEvents();
  bool prompt_seen = false;
  QTimer::singleShot(0, [&] {
    if (auto* box =
            qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("convertToRgbKeepLookMessageBox")))) {
      prompt_seen = true;
      box->reject();
    }
  });
  require_action_by_text(window, QStringLiteral("RGB Color"))->trigger();
  QApplication::processEvents();
  CHECK(!prompt_seen);
  CHECK(!document.palette_editing().has_value());
  CHECK(collect_document_pixel_bytes(document) == dirty_bytes);
}

void ui_color_picker_file_palette_clipboard_and_drop() {
  ensure_artifact_dir();
  SettingsValueRestorer choice_restorer(QStringLiteral("palettes/lastPaletteChoice"));
  SettingsValueRestorer file_restorer(QStringLiteral("palettes/lastPaletteFile"));

  // A small .gpl on disk stands in for the user's own palette file.
  const auto palette_path =
      QFileInfo(QDir(QStringLiteral("test-artifacts")).filePath(QStringLiteral("ui_picker_palette.gpl")))
          .absoluteFilePath();
  {
    QFile file(palette_path);
    CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("GIMP Palette\nName: PickerTest\n#\n171 18 205\tc0\n68 85 102\tc1\n119 136 153\tc2\n");
  }
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("palettes/lastPaletteChoice"), QStringLiteral("file"));
    settings.setValue(QStringLiteral("palettes/lastPaletteFile"), palette_path);
  }

  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* foreground = window.findChild<QPushButton*>(QStringLiteral("foregroundColorButton"));
  CHECK(foreground != nullptr);
  foreground->click();
  QApplication::processEvents();
  auto* dialog = find_top_level_dialog(QStringLiteral("patchyColorDialog"));
  CHECK(dialog != nullptr);
  auto* combo = dialog->findChild<QComboBox*>(QStringLiteral("patchyColorPaletteCombo"));
  CHECK(combo != nullptr);

  // The remembered palette file reloads quietly, the dropdown shows it, and the
  // Load/Save action rows are present.
  CHECK(combo->currentData().toString() == QStringLiteral("file"));
  CHECK(combo->currentText().contains(QStringLiteral("ui_picker_palette.gpl")));
  CHECK(combo->findData(QStringLiteral("load")) >= 0);
  CHECK(combo->findData(QStringLiteral("save")) >= 0);
  auto* grid = dialog->findChild<QWidget*>(QStringLiteral("patchyColorPaletteGrid"));
  CHECK(grid != nullptr);
  CHECK(grid->property("paletteColorCount").toInt() == 3);

  const auto cell = grid->property("paletteCellSize").toInt() + grid->property("paletteCellGap").toInt();
  send_mouse(*grid, QEvent::MouseButtonPress, QPoint(cell + 5, 5), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*grid, QEvent::MouseButtonRelease, QPoint(cell + 5, 5), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(canvas->primary_color() == QColor(68, 85, 102));

  // Edit > Copy with focus inside the picker copies the current color as hex.
  grid->setFocus();
  QApplication::processEvents();
  require_action(window, "editCopyAction")->trigger();
  QApplication::processEvents();
  CHECK(QApplication::clipboard()->text() == QStringLiteral("#445566"));

  // Edit > Paste applies a clipboard hex color to the picker (and the callback
  // pushes it to the foreground color).
  QApplication::clipboard()->setText(QStringLiteral("#AB12CD"));
  require_action(window, "editPasteAction")->trigger();
  QApplication::processEvents();
  auto* picker = dialog->findChild<patchy::ui::PatchyColorPicker*>(QStringLiteral("patchyAdvancedColorPicker"));
  CHECK(picker != nullptr);
  CHECK(picker->currentColor() == QColor(0xAB, 0x12, 0xCD));
  CHECK(canvas->primary_color() == QColor(0xAB, 0x12, 0xCD));

  // Without picker focus the same action keeps its canvas meaning: the picker
  // color must not change. Activate the main window so app focus really leaves
  // the picker dialog (offscreen keeps the dialog active otherwise).
  window.activateWindow();
  canvas->setFocus();
  QApplication::processEvents();
  CHECK(QApplication::focusWidget() == canvas);
  QApplication::clipboard()->setText(QStringLiteral("#0F0E0D"));
  require_action(window, "editPasteAction")->trigger();
  QApplication::processEvents();
  CHECK(picker->currentColor() == QColor(0xAB, 0x12, 0xCD));

  // Dropping a color onto a custom slot writes and selects that slot (drags
  // carry the standard color mime).
  auto custom_swatches = picker->findChildren<QPushButton*>(QStringLiteral("patchyCustomColorSwatch"));
  CHECK(custom_swatches.size() == 16);
  auto* slot = custom_swatches.front();
  CHECK(slot->acceptDrops());
  // Real drops always begin with a DragEnter: QApplication registers the drop
  // target there and routes the following Drop to it (a bare synthetic Drop
  // event is silently discarded).
  const auto send_color_drop = [](QWidget* target, const QMimeData& mime, QPoint position = QPoint(5, 5)) {
    QDragEnterEvent enter(position, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(target, &enter);
    QDropEvent drop(QPointF(position), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(target, &drop);
    QApplication::processEvents();
  };
  {
    QMimeData mime;
    mime.setColorData(QColor(119, 136, 153));
    send_color_drop(slot, mime);
  }
  CHECK(slot->toolTip() == QStringLiteral("#778899"));

  // Edit > Cut with the slot selected copies its color and empties the slot
  // (the dialog must be the active window again for focus to land in it).
  dialog->activateWindow();
  slot->setFocus();
  QApplication::processEvents();
  CHECK(QApplication::focusWidget() == slot);
  require_action(window, "editCutAction")->trigger();
  QApplication::processEvents();
  CHECK(QApplication::clipboard()->text() == QStringLiteral("#778899"));
  CHECK(slot->toolTip() == QStringLiteral("#FFFFFF"));

  // Dropping a color (hex-text form) onto the current-color preview selects it.
  auto* preview = picker->findChild<QFrame*>(QStringLiteral("patchyColorPreview"));
  CHECK(preview != nullptr);
  {
    QMimeData mime;
    mime.setText(QStringLiteral("#112233"));
    send_color_drop(preview, mime);
  }
  CHECK(picker->currentColor() == QColor(0x11, 0x22, 0x33));

  // "Set Custom Color" writes the current color into the chosen slot.
  custom_swatches[2]->click();
  QApplication::processEvents();
  picker->setCurrentColor(QColor(0x65, 0x43, 0x21));
  auto* set_custom = picker->findChild<QPushButton*>(QStringLiteral("patchySetCustomColorButton"));
  CHECK(set_custom != nullptr);
  CHECK(set_custom->isEnabled());
  set_custom->click();
  QApplication::processEvents();
  CHECK(custom_swatches[2]->toolTip() == QStringLiteral("#654321"));

  // Paste with a custom slot focused writes the clipboard color into that slot.
  custom_swatches[3]->setFocus();
  QApplication::processEvents();
  CHECK(QApplication::focusWidget() == custom_swatches[3]);
  QApplication::clipboard()->setText(QStringLiteral("#224466"));
  require_action(window, "editPasteAction")->trigger();
  QApplication::processEvents();
  CHECK(custom_swatches[3]->toolTip() == QStringLiteral("#224466"));
  CHECK(picker->currentColor() == QColor(0x22, 0x44, 0x66));

  // The loaded file palette is editable: dropping a color onto a cell rewrites
  // that entry, and paste with a cell selected does the same.
  const auto grid_cell = grid->property("paletteCellSize").toInt() + grid->property("paletteCellGap").toInt();
  {
    QMimeData mime;
    mime.setColorData(QColor(0x0A, 0x0B, 0x0C));
    send_color_drop(grid, mime, QPoint(2 * grid_cell + 5, 5));
  }
  send_mouse(*grid, QEvent::MouseButtonPress, QPoint(2 * grid_cell + 5, 5), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*grid, QEvent::MouseButtonRelease, QPoint(2 * grid_cell + 5, 5), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(picker->currentColor() == QColor(0x0A, 0x0B, 0x0C));

  send_mouse(*grid, QEvent::MouseButtonPress, QPoint(grid_cell + 5, 5), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*grid, QEvent::MouseButtonRelease, QPoint(grid_cell + 5, 5), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(grid->property("paletteSelectedIndex").toInt() == 1);
  grid->setFocus();
  QApplication::processEvents();
  QApplication::clipboard()->setText(QStringLiteral("#31415F"));
  require_action(window, "editPasteAction")->trigger();
  QApplication::processEvents();
  CHECK(picker->currentColor() == QColor(0x31, 0x41, 0x5F));
  send_mouse(*grid, QEvent::MouseButtonPress, QPoint(5, 5), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*grid, QEvent::MouseButtonRelease, QPoint(5, 5), Qt::LeftButton, Qt::NoButton);
  send_mouse(*grid, QEvent::MouseButtonPress, QPoint(grid_cell + 5, 5), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*grid, QEvent::MouseButtonRelease, QPoint(grid_cell + 5, 5), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(picker->currentColor() == QColor(0x31, 0x41, 0x5F));

  save_widget_artifact("ui_color_picker_file_palette", *dialog);
  dialog->close();
  QApplication::processEvents();
}

void ui_palette_mode_display_quantizes_layer_styles() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  const auto* preset = patchy::find_builtin_palette_preset("pico8");
  CHECK(preset != nullptr);
  patchy::DocumentPaletteEditing editing;
  editing.palette.colors.assign(preset->colors.begin(), preset->colors.end());
  editing.palette.colors.push_back(patchy::RgbColor{255, 255, 255});
  editing.palette_revision = 1;
  document.palette_editing() = editing;
  patchy::PaletteLut lut;
  lut.build(editing.palette.colors);

  // Palette-clean content plus DELIBERATELY off-palette live effects: a teal
  // stroke and a translucent purple color overlay. Both render at composite
  // time, so only display quantization can keep the canvas palette-true.
  const auto layer_id = document.active_layer_id();
  CHECK(layer_id.has_value());
  auto* layer = document.find_layer(*layer_id);
  CHECK(layer != nullptr);
  auto& pixels = layer->pixels();
  const auto bounds = layer->bounds();
  const auto fill_color = preset->colors[8];
  for (int y = 70; y <= 130; ++y) {
    for (int x = 60; x <= 180; ++x) {
      auto* px = pixels.pixel(x - bounds.x, y - bounds.y);
      px[0] = fill_color.red;
      px[1] = fill_color.green;
      px[2] = fill_color.blue;
      px[3] = 255;
    }
  }
  patchy::LayerStroke stroke;
  stroke.enabled = true;
  stroke.color = patchy::RgbColor{37, 199, 201};
  stroke.size = 8.0F;
  layer->layer_style().strokes.push_back(stroke);
  patchy::LayerColorOverlay overlay;
  overlay.enabled = true;
  overlay.color = patchy::RgbColor{123, 45, 210};
  overlay.opacity = 0.6F;
  layer->layer_style().color_overlays.push_back(overlay);

  canvas->set_zoom_centered(1.0);
  canvas->document_changed();
  patchy::ui::MainWindowTestAccess::refresh_document_info(window);
  QApplication::processEvents();

  const auto image = canvas->grab().toImage().convertToFormat(QImage::Format_RGB32);
  int checked = 0;
  for (int doc_y = 55; doc_y <= 145; doc_y += 3) {
    for (int doc_x = 45; doc_x <= 195; doc_x += 3) {
      const auto widget_point = canvas->widget_position_for_document_point(QPoint(doc_x, doc_y));
      if (!image.rect().contains(widget_point)) {
        continue;
      }
      const auto color = QColor(image.pixel(widget_point));
      CHECK(lut.contains(patchy::RgbColor{static_cast<std::uint8_t>(color.red()),
                                          static_cast<std::uint8_t>(color.green()),
                                          static_cast<std::uint8_t>(color.blue())}));
      ++checked;
    }
  }
  CHECK(checked > 200);

  // Back in RGB mode the effects show their true full colors again.
  require_action_by_text(window, QStringLiteral("RGB Color"))->trigger();
  QApplication::processEvents();
  const auto rgb_image = canvas->grab().toImage().convertToFormat(QImage::Format_RGB32);
  bool found_off_palette = false;
  for (int doc_y = 55; doc_y <= 145 && !found_off_palette; ++doc_y) {
    for (int doc_x = 45; doc_x <= 195 && !found_off_palette; ++doc_x) {
      const auto widget_point = canvas->widget_position_for_document_point(QPoint(doc_x, doc_y));
      if (!rgb_image.rect().contains(widget_point)) {
        continue;
      }
      const auto color = QColor(rgb_image.pixel(widget_point));
      found_off_palette = !lut.contains(patchy::RgbColor{static_cast<std::uint8_t>(color.red()),
                                                         static_cast<std::uint8_t>(color.green()),
                                                         static_cast<std::uint8_t>(color.blue())});
    }
  }
  CHECK(found_off_palette);
  save_widget_artifact("ui_palette_display_quantized", *canvas);
}

void ui_png8_export_round_trips_indexed() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  const auto* preset = patchy::find_builtin_palette_preset("gameboy");
  CHECK(preset != nullptr);
  patchy::DocumentPaletteEditing editing;
  editing.palette.colors.assign(preset->colors.begin(), preset->colors.end());
  editing.palette_revision = 1;
  document.palette_editing() = editing;
  patchy::ui::MainWindowTestAccess::refresh_document_info(window);

  std::filesystem::create_directories("test-artifacts");
  const auto path =
      QFileInfo(QDir(QStringLiteral("test-artifacts")).filePath(QStringLiteral("ui_palette_export.png")))
          .absoluteFilePath();
  patchy::ui::write_flat_image_file(document, path, QStringLiteral("png"), patchy::ui::ImageSaveOptions{});

  QImageReader reader(path);
  const auto image = reader.read();
  CHECK(!image.isNull());
  // Palette-mode PNG export writes a color-type-3 palette PNG carrying the
  // document palette in order.
  CHECK(image.format() == QImage::Format_Indexed8);
  CHECK(image.colorCount() == 4);
  for (int index = 0; index < 4; ++index) {
    const auto& expected = preset->colors[static_cast<std::size_t>(index)];
    CHECK(image.color(index) == qRgba(expected.red, expected.green, expected.blue, 255));
  }

  // Converting to RGB restores plain RGBA PNG export.
  document.palette_editing().reset();
  const auto rgb_path =
      QFileInfo(QDir(QStringLiteral("test-artifacts")).filePath(QStringLiteral("ui_palette_export_rgb.png")))
          .absoluteFilePath();
  patchy::ui::write_flat_image_file(document, rgb_path, QStringLiteral("png"), patchy::ui::ImageSaveOptions{});
  QImageReader rgb_reader(rgb_path);
  CHECK(rgb_reader.read().format() != QImage::Format_Indexed8);
}

void ui_palette_mode_brush_stroke_writes_only_palette_colors() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  // PICO-8 plus pure white, so the untouched startup background counts as clean.
  const auto* preset = patchy::find_builtin_palette_preset("pico8");
  CHECK(preset != nullptr);
  patchy::DocumentPaletteEditing editing;
  editing.palette.colors.assign(preset->colors.begin(), preset->colors.end());
  editing.palette.colors.push_back(patchy::RgbColor{255, 255, 255});
  editing.palette_revision = 1;
  document.palette_editing() = editing;
  patchy::PaletteLut lut;
  lut.build(editing.palette.colors);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(211, 32, 41));  // off-palette red
  const auto primary = canvas->primary_color();
  CHECK(lut.contains(patchy::RgbColor{static_cast<std::uint8_t>(primary.red()),
                                      static_cast<std::uint8_t>(primary.green()),
                                      static_cast<std::uint8_t>(primary.blue())}));

  // A big soft brush through the stroke-snapshot compositor (the common paint
  // path, which bypasses core write_pixel) must still write hard palette pixels.
  canvas->set_brush_size(24);
  canvas->set_brush_softness(100);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(40, 80)),
       canvas->widget_position_for_document_point(QPoint(200, 120)));
  QApplication::processEvents();

  const auto layer_id = document.active_layer_id();
  CHECK(layer_id.has_value());
  const auto* layer = document.find_layer(*layer_id);
  CHECK(layer != nullptr);
  CHECK(patchy::pixels_are_palette_clean(layer->pixels(), lut));

  const auto bounds = layer->bounds();
  const auto* center = layer->pixels().pixel(120 - bounds.x, 100 - bounds.y);
  CHECK(center[0] == primary.red());
  CHECK(center[1] == primary.green());
  CHECK(center[2] == primary.blue());
  if (layer->pixels().format().channels >= 4) {
    CHECK(center[3] == 255);
  }

  // Mixer Brush uses the same canvas-side writer, including its hard palette snap.
  require_action_by_text(window, QStringLiteral("Mixer Brush"))->trigger();
  canvas->set_mixer_wet(100);
  canvas->set_mixer_load(100);
  canvas->set_mixer_mix(75);
  canvas->set_mixer_flow(100);
  canvas->set_brush_size(18);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(120, 100)),
       canvas->widget_position_for_document_point(QPoint(230, 100)));
  QApplication::processEvents();
  CHECK(patchy::pixels_are_palette_clean(document.find_layer(*layer_id)->pixels(), lut));

  // A soft eraser pass over the stroke keeps the layer palette-clean too.
  require_action_by_text(window, QStringLiteral("Eraser"))->trigger();
  canvas->set_brush_size(20);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(80, 80)),
       canvas->widget_position_for_document_point(QPoint(160, 120)));
  QApplication::processEvents();
  CHECK(patchy::pixels_are_palette_clean(document.find_layer(*layer_id)->pixels(), lut));

  // Local adjustment brushes are a separate write path. Their partially
  // adjusted RGB result must snap back to a palette color too.
  require_action(window, "toolDodgeAction")->trigger();
  canvas->set_brush_size(18);
  canvas->set_brush_softness(60);
  canvas->set_local_adjustment_strength(43);
  canvas->set_local_tone_range(patchy::ui::CanvasWidget::LocalToneRange::Midtones);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(90, 92)),
       canvas->widget_position_for_document_point(QPoint(150, 108)));
  QApplication::processEvents();
  CHECK(patchy::pixels_are_palette_clean(document.find_layer(*layer_id)->pixels(), lut));

  save_widget_artifact("ui_palette_mode_stroke", *canvas);
}

void ui_brush_tip_soft_stamps_accumulate_without_seams() {
  clear_brush_tip_test_state();
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  // A solid square tip at 100% spacing stamps edge-to-edge like the brick/pattern defaults.
  // With a soft tip, adjacent stamps' feathered edges meet on the stroke's center line; the
  // overlap must accumulate toward solid instead of dipping to a light seam between stamps.
  auto& library = window.brush_tip_library();
  const auto square = [] {
    QImage mask(16, 16, QImage::Format_Grayscale8);
    mask.fill(255);
    return mask;
  }();
  const auto tip_id = library.add_tip(QStringLiteral("Seam Square"), square, 1.0);
  CHECK(!tip_id.isEmpty());
  window.set_active_brush_tip(tip_id, false);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(0, 0, 0));
  canvas->set_brush_size(32);
  canvas->set_brush_softness(100);

  const auto start = QPoint(60, 100);
  const auto end = QPoint(188, 100);  // five stamps at 32px spacing: x = 60,92,124,156,188
  drag(*canvas, canvas->widget_position_for_document_point(start),
       canvas->widget_position_for_document_point(end));
  QApplication::processEvents();

  const auto image = canvas->grab().toImage().convertToFormat(QImage::Format_RGB32);
  const auto lightness_at = [canvas, &image](QPoint document_point) {
    return QColor(image.pixel(canvas->widget_position_for_document_point(document_point))).lightness();
  };

  CHECK(lightness_at(start) < 60);  // stamp centers are solid
  int max_lightness = 0;
  for (int x = start.x(); x <= end.x(); ++x) {
    max_lightness = std::max(max_lightness, lightness_at(QPoint(x, start.y())));
  }
  // Under the old per-pixel max coverage cap the inter-stamp seams stayed near 50% coverage
  // (lightness ~128); accumulation keeps the whole center line clearly dark.
  CHECK(max_lightness < 110);
  save_widget_artifact("ui_brush_tip_soft_stamp_seams", *canvas);
  clear_brush_tip_test_state();
}

void ui_brush_outline_overlay_tracks_large_brushes() {
  clear_brush_tip_test_state();
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();

  // Large procedural brush: the OS cursor gives way to a crosshair + canvas overlay.
  canvas->set_brush_size(400);
  CHECK(canvas->cursor().shape() == Qt::CrossCursor);

  const QPoint hover(canvas->width() / 2, canvas->height() / 2);
  send_mouse(*canvas, QEvent::MouseMove, hover, Qt::NoButton, Qt::NoButton);
  QApplication::processEvents();
  const auto image = canvas->grab().toImage().convertToFormat(QImage::Format_RGB32);
  // The outline ring must appear far beyond the old 160px cursor cap. The 1px stroke is
  // antialiased over white, so probe a band around the radius for clearly-darkened pixels.
  const auto display_radius = static_cast<int>(std::round(400.0 * canvas->zoom() / 2.0));
  CHECK(display_radius > 100);
  int dark_ring_pixels = 0;
  for (int angle_step = 0; angle_step < 360; angle_step += 5) {
    const auto radians = angle_step * 3.14159265358979323846 / 180.0;
    for (int probe = -3; probe <= 3; ++probe) {
      const QPoint point(hover.x() + static_cast<int>(std::round((display_radius + probe) * std::cos(radians))),
                         hover.y() + static_cast<int>(std::round((display_radius + probe) * std::sin(radians))));
      if (image.rect().contains(point) && QColor(image.pixel(point)).lightness() < 200) {
        ++dark_ring_pixels;
        break;
      }
    }
  }
  save_widget_artifact("ui_brush_outline_overlay_large", *canvas);
  CHECK(dark_ring_pixels > 20);

  // Shrinking the brush returns to a pixmap cursor (no overlay crosshair).
  canvas->set_brush_size(48);
  CHECK(canvas->cursor().shape() != Qt::CrossCursor);
  clear_brush_tip_test_state();
}

void ui_brush_tip_manager_folder_rows_fit_thumbnails() {
  clear_brush_tip_test_state();
  patchy::ui::MainWindow window;
  show_window(window);
  auto& library = window.brush_tip_library();
  QString first_id;
  for (int index = 0; index < 5; ++index) {
    const auto id = library.add_tip(QStringLiteral("Tip %1").arg(index + 1), make_bar_tip_image(), 0.25,
                                    QStringLiteral("Folder A"));
    CHECK(!id.isEmpty());
    if (first_id.isEmpty()) {
      first_id = id;
    }
  }

  // The manager is modal: measure the open dialog from a queued callback, then close it.
  int measured_tip_rows = 0;
  int short_tip_rows = 0;
  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    QDialog* dialog = nullptr;
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() == QStringLiteral("brushTipManagerDialog")) {
        dialog = qobject_cast<QDialog*>(widget);
      }
    }
    if (dialog == nullptr) {
      return;
    }
    saw_dialog = true;
    QApplication::processEvents();
    auto* tree = dialog->findChild<QTreeWidget*>(QStringLiteral("brushTipManagerTree"));
    if (tree != nullptr) {
      for (int top = 0; top < tree->topLevelItemCount(); ++top) {
        auto* folder_item = tree->topLevelItem(top);
        for (int child = 0; child < folder_item->childCount(); ++child) {
          const auto rect = tree->visualItemRect(folder_item->child(child));
          ++measured_tip_rows;
          if (rect.height() < 44) {
            ++short_tip_rows;
          }
        }
      }
    }
    save_widget_artifact("ui_brush_tip_manager_dialog", *dialog);
    dialog->reject();
  });
  patchy::ui::request_brush_tip_manager(&window, library, first_id, {}, {});

  CHECK(saw_dialog);
  CHECK(measured_tip_rows == 5);
  CHECK(short_tip_rows == 0);  // every tip row must have room for its 40px thumbnail
  clear_brush_tip_test_state();
}

void ui_pattern_library_imports_pat_and_persists_folders() {
  clear_pattern_test_state();
  patchy::ui::PatternLibrary library(pattern_test_storage_dir());
  QString error;
  QStringList warnings;
  const auto fixture = QString::fromStdString(
      patchy::test::committed_format_fixture_path("pat", "hue.pat").string());
  const auto first_storage_id = library.import_pat(fixture, error, warnings);
  CHECK(!first_storage_id.isEmpty());
  CHECK(error.isEmpty());
  CHECK(warnings.isEmpty());
  CHECK(library.entries().size() == 1);
  const auto* entry = library.find_entry(first_storage_id);
  CHECK(entry != nullptr);
  CHECK(entry->name == QStringLiteral("hue"));
  CHECK(entry->folder == QStringLiteral("hue"));
  CHECK(entry->size == QSize(100, 100));
  CHECK(!entry->id.isEmpty());
  CHECK(entry->id != entry->storage_id);
  const auto original_pattern_id = entry->id;
  const auto resource = library.resource(original_pattern_id);
  CHECK(resource.has_value());
  CHECK(resource->provenance == patchy::PatternProvenance::Authored);
  const auto resource_by_storage_id = library.resource_for_entry(first_storage_id);
  CHECK(resource_by_storage_id.has_value());
  CHECK(resource_by_storage_id->id == resource->id);
  CHECK(resource_by_storage_id->name == resource->name);
  CHECK(!library.resource_for_entry(QStringLiteral("missing-storage-id")).has_value());
  const auto* top_left = resource->tile.pixel(0, 0);
  CHECK(top_left[0] == 255 && top_left[1] == 167 && top_left[2] == 0 && top_left[3] == 255);
  const auto* bottom_left = resource->tile.pixel(0, 99);
  CHECK(bottom_left[0] == 255 && bottom_left[1] == 0 && bottom_left[2] == 198 &&
        bottom_left[3] == 10);

  const auto reimported_storage_id = library.import_pat(fixture, error, warnings);
  CHECK(reimported_storage_id == first_storage_id);
  CHECK(error.isEmpty());
  CHECK(warnings.isEmpty());
  CHECK(library.entries().size() == 1);  // same Photoshop id and pixels deduplicate

  CHECK(library.rename_pattern(first_storage_id, QStringLiteral("Hue Wheel")));
  CHECK(library.set_pattern_folder(first_storage_id, QStringLiteral("Color Tests")));
  const auto duplicate_storage_id = library.duplicate_pattern(first_storage_id);
  CHECK(!duplicate_storage_id.isEmpty());
  const auto* duplicate = library.find_entry(duplicate_storage_id);
  CHECK(duplicate != nullptr);
  CHECK(duplicate->id != original_pattern_id);
  CHECK(duplicate->folder == QStringLiteral("Color Tests"));

  patchy::ui::PatternLibrary reloaded(pattern_test_storage_dir());
  CHECK(reloaded.entries().size() == 2);
  CHECK(reloaded.find_entry(first_storage_id) != nullptr);
  CHECK(reloaded.find_entry(first_storage_id)->name == QStringLiteral("Hue Wheel"));
  CHECK(reloaded.find_entry(first_storage_id)->folder == QStringLiteral("Color Tests"));

  int changed_signals = 0;
  QObject::connect(&library, &patchy::ui::PatternLibrary::changed, &library,
                   [&changed_signals] { ++changed_signals; });
  CHECK(library.remove_patterns({first_storage_id, duplicate_storage_id}) == 2);
  CHECK(changed_signals == 1);
  patchy::ui::PatternLibrary empty_reload(pattern_test_storage_dir());
  CHECK(empty_reload.entries().empty());
  clear_pattern_test_state();
}

void ui_pattern_library_aggregates_parser_warnings() {
  QTemporaryDir directory;
  CHECK(directory.isValid());
  const auto fixture = QString::fromStdString(
      patchy::test::committed_format_fixture_path("pat", "hue.pat").string());
  QFile source(fixture);
  CHECK(source.open(QIODevice::ReadOnly));
  auto bytes = source.readAll();
  CHECK(bytes.size() > 10);
  // Keep the valid first record but claim a second one. The reader imports the
  // first and reports the missing/trailing second record as a parser warning.
  bytes[6] = 0;
  bytes[7] = 0;
  bytes[8] = 0;
  bytes[9] = 2;
  const auto warning_fixture = directory.filePath(QStringLiteral("warning.pat"));
  QFile destination(warning_fixture);
  CHECK(destination.open(QIODevice::WriteOnly));
  CHECK(destination.write(bytes) == bytes.size());
  destination.close();

  patchy::ui::PatternLibrary library(directory.filePath(QStringLiteral("library")));
  QString error;
  QStringList warnings;
  CHECK(!library.import_pat(warning_fixture, error, warnings).isEmpty());
  CHECK(error.isEmpty());
  CHECK(warnings == QStringList{QStringLiteral(
                        "Some pattern data was skipped or repaired because it is unsupported or damaged.")});
}

void ui_pattern_library_reimport_deduplicates_remapped_source_id() {
  QTemporaryDir directory;
  CHECK(directory.isValid());
  patchy::ui::PatternLibrary library(directory.filePath(QStringLiteral("library")));
  const auto fixture = QString::fromStdString(
      patchy::test::committed_format_fixture_path("pat", "hue.pat").string());
  const auto source_id = QStringLiteral("e7f7ad0e-6537-0b48-a350-1a98c4160671");
  const auto occupying_storage = library.add_pattern(
      QStringLiteral("Different Pixels"),
      solid_pixels(2, 2, patchy::PixelFormat::rgba8(), QColor(10, 20, 30, 255)),
      QStringLiteral("Existing"), source_id);
  CHECK(!occupying_storage.isEmpty());

  QString error;
  QStringList warnings;
  const auto remapped_storage = library.import_pat(fixture, error, warnings);
  CHECK(!remapped_storage.isEmpty());
  CHECK(remapped_storage != occupying_storage);
  CHECK(error.isEmpty());
  CHECK(warnings.size() == 1);
  CHECK(library.entries().size() == 2);
  const auto* remapped = library.find_entry(remapped_storage);
  CHECK(remapped != nullptr);
  CHECK(remapped->id != source_id);
  CHECK(remapped->source_id == source_id);

  warnings.clear();
  CHECK(library.import_pat(fixture, error, warnings) == remapped_storage);
  CHECK(error.isEmpty() && warnings.isEmpty());
  CHECK(library.entries().size() == 2);

  patchy::ui::PatternLibrary reloaded(directory.filePath(QStringLiteral("library")));
  const auto* persisted = reloaded.find_entry(remapped_storage);
  CHECK(persisted != nullptr && persisted->source_id == source_id);
  CHECK(reloaded.import_pat(fixture, error, warnings) == remapped_storage);
  CHECK(error.isEmpty() && warnings.isEmpty());
  CHECK(reloaded.entries().size() == 2);
}

void ui_pattern_library_rejects_oversized_pat_before_read() {
  QTemporaryDir directory;
  CHECK(directory.isValid());
  const auto oversized_path = directory.filePath(QStringLiteral("oversized.pat"));
  QFile oversized(oversized_path);
  CHECK(oversized.open(QIODevice::WriteOnly));
  CHECK(oversized.resize(32LL * 1024LL * 1024LL + 1LL));
  oversized.close();

  patchy::ui::PatternLibrary library(directory.filePath(QStringLiteral("library")));
  QString error;
  QStringList warnings;
  CHECK(library.import_pat(oversized_path, error, warnings).isEmpty());
  CHECK(error == QStringLiteral("\"oversized.pat\" is too large to import safely."));
  CHECK(warnings.isEmpty());
  CHECK(library.entries().empty());
}

void ui_pattern_library_delete_requires_tile_removal() {
  QTemporaryDir directory;
  CHECK(directory.isValid());
  patchy::ui::PatternLibrary library(directory.filePath(QStringLiteral("library")));
  QString error;
  QStringList warnings;
  const auto fixture = QString::fromStdString(
      patchy::test::committed_format_fixture_path("pat", "hue.pat").string());
  const auto storage_id = library.import_pat(fixture, error, warnings);
  CHECK(!storage_id.isEmpty());

  const auto tile_path = library.storage_dir() + QStringLiteral("/") + storage_id +
                         QStringLiteral(".png");
  const auto sidecar_path = library.storage_dir() + QStringLiteral("/") + storage_id +
                            QStringLiteral(".json");
  CHECK(QFile::remove(tile_path));
  CHECK(QFile::remove(sidecar_path));
  // Directories at the file paths provide a cross-platform removal failure.
  CHECK(QDir().mkpath(tile_path));
  CHECK(QDir().mkpath(sidecar_path));

  int changed_signals = 0;
  QObject::connect(&library, &patchy::ui::PatternLibrary::changed, &library,
                   [&changed_signals] { ++changed_signals; });
  CHECK(!library.remove_pattern(storage_id));
  CHECK(library.find_entry(storage_id) != nullptr);
  CHECK(changed_signals == 0);

  CHECK(QDir(tile_path).removeRecursively());
  CHECK(library.remove_pattern(storage_id));
  CHECK(library.find_entry(storage_id) == nullptr);
  CHECK(changed_signals == 1);
  CHECK(QFileInfo(sidecar_path).isDir());  // sidecar cleanup is deliberately best-effort
}

void ui_default_patterns_seed_once_and_restore() {
  clear_pattern_test_state();
  clear_brush_tip_test_state();
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("patterns/defaultPatternsVersion"), 0);
    settings.sync();
  }

  const auto presets = patchy::builtin_pattern_presets();
  const auto photo_presets = patchy::photo_pattern_presets();
  const auto total = presets.size() + photo_presets.size();
  QString deleted_pattern_id;
  {
    patchy::ui::MainWindow window;
    show_window(window);
    auto& library = window.pattern_library();
    CHECK(library.entries().size() == total);
    CHECK(library.has_all_default_patterns_introduced_after(0));
    CHECK(library.default_patterns_match_factory());
    CHECK(library.folders() == (QStringList{patchy::ui::default_patterns_folder_name(),
                                            patchy::ui::photo_patterns_folder_name()}));
    for (const auto& preset : presets) {
      const auto* entry =
          library.find_entry_by_pattern_id(QString::fromLatin1(preset.id));
      CHECK(entry != nullptr);
      CHECK(entry->folder == patchy::ui::default_patterns_folder_name());
      CHECK(entry->name == QString::fromLatin1(preset.english_name));
    }
    for (const auto& preset : photo_presets) {
      const auto* entry =
          library.find_entry_by_pattern_id(QString::fromLatin1(preset.id));
      CHECK(entry != nullptr);
      CHECK(entry->folder == patchy::ui::photo_patterns_folder_name());
      CHECK(entry->name == QString::fromLatin1(preset.english_name));
      CHECK(entry->size == QSize(512, 512));
    }
    const auto first = library.entries().front();
    deleted_pattern_id = first.id;
    CHECK(library.remove_pattern(first.storage_id));
    CHECK(library.entries().size() == total - 1);
    CHECK(!library.has_all_default_patterns_introduced_after(0));
    CHECK(!library.default_patterns_match_factory());
    CHECK(library.has_all_default_patterns_introduced_after(
        patchy::ui::kDefaultPatternsVersion));
  }
  CHECK(patchy::ui::app_settings()
            .value(QStringLiteral("patterns/defaultPatternsVersion"))
            .toInt() == patchy::ui::kDefaultPatternsVersion);

  {
    patchy::ui::MainWindow window;
    show_window(window);
    auto& library = window.pattern_library();
    CHECK(library.entries().size() == total - 1);
    CHECK(library.find_entry_by_pattern_id(deleted_pattern_id) == nullptr);
    CHECK(!library.has_all_default_patterns_introduced_after(0));
    CHECK(library.restore_default_patterns() == 1);
    CHECK(library.entries().size() == total);
    CHECK(library.find_entry_by_pattern_id(deleted_pattern_id) != nullptr);
    CHECK(library.has_all_default_patterns_introduced_after(0));
    CHECK(library.default_patterns_match_factory());
    CHECK(library.restore_default_patterns() == 0);
    CHECK(library.reset_default_patterns_to_factory() == 0);
  }
  clear_pattern_test_state();
  clear_brush_tip_test_state();
}

void ui_pattern_library_imports_image_files() {
  QTemporaryDir directory;
  CHECK(directory.isValid());
  patchy::ui::PatternLibrary library(directory.filePath(QStringLiteral("library")));

  // An RGBA image with distinct pixels and partial alpha round-trips exactly.
  QImage image(7, 5, QImage::Format_RGBA8888);
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      image.setPixelColor(x, y, QColor(x * 30, y * 40, 90, x == 3 && y == 2 ? 128 : 255));
    }
  }
  const auto png_path = directory.filePath(QStringLiteral("Mossy Rock.png"));
  CHECK(image.save(png_path, "PNG"));

  QString error;
  QStringList warnings;
  const auto storage_id = library.import_image(png_path, error, warnings);
  CHECK(!storage_id.isEmpty());
  CHECK(error.isEmpty());
  CHECK(warnings.isEmpty());
  const auto* entry = library.find_entry(storage_id);
  CHECK(entry != nullptr);
  CHECK(entry->name == QStringLiteral("Mossy Rock"));
  CHECK(entry->folder.isEmpty());  // image imports land ungrouped
  CHECK(entry->size == QSize(7, 5));
  const auto resource = library.resource_for_entry(storage_id);
  CHECK(resource.has_value());
  CHECK(resource->tile.width() == 7);
  CHECK(resource->tile.height() == 5);
  bool pixels_match = true;
  for (int y = 0; y < 5 && pixels_match; ++y) {
    for (int x = 0; x < 7 && pixels_match; ++x) {
      const auto* px = resource->tile.pixel(x, y);
      const auto expected = image.pixelColor(x, y);
      pixels_match = px[0] == expected.red() && px[1] == expected.green() &&
                     px[2] == expected.blue() && px[3] == expected.alpha();
    }
  }
  CHECK(pixels_match);
  CHECK(resource->tile.pixel(3, 2)[3] == 128);  // alpha preserved

  // Read through a fresh library so the PNG and sidecar on disk, rather than
  // the importing instance's tile cache, carry the same name and straight-alpha pixels.
  patchy::ui::PatternLibrary reloaded(library.storage_dir());
  const auto* reloaded_entry = reloaded.find_entry(storage_id);
  CHECK(reloaded_entry != nullptr);
  CHECK(reloaded_entry->name == QStringLiteral("Mossy Rock"));
  CHECK(reloaded_entry->folder.isEmpty());
  CHECK(reloaded_entry->size == QSize(7, 5));
  const auto reloaded_resource = reloaded.resource_for_entry(storage_id);
  CHECK(reloaded_resource.has_value());
  bool reloaded_pixels_match = true;
  for (int y = 0; y < image.height() && reloaded_pixels_match; ++y) {
    for (int x = 0; x < image.width() && reloaded_pixels_match; ++x) {
      const auto* px = reloaded_resource->tile.pixel(x, y);
      const auto expected = image.pixelColor(x, y);
      reloaded_pixels_match = px[0] == expected.red() && px[1] == expected.green() &&
                              px[2] == expected.blue() && px[3] == expected.alpha();
    }
  }
  CHECK(reloaded_pixels_match);

  // A second import of the same file adds a new entry (no id-based dedup for images).
  const auto second = library.import_image(png_path, error, warnings);
  CHECK(!second.isEmpty());
  CHECK(second != storage_id);
  CHECK(library.entries().size() == 2);
  CHECK(library.find_entry(storage_id)->id != library.find_entry(second)->id);

  // A non-image file reports a per-file error and adds nothing.
  const auto text_path = directory.filePath(QStringLiteral("notes.txt"));
  {
    QFile file(text_path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write("not an image");
  }
  CHECK(library.import_image(text_path, error, warnings).isEmpty());
  CHECK(error.contains(QStringLiteral("notes.txt")));
  CHECK(library.entries().size() == 2);

  // Header-only BMPs prove dimension guards run before the decoder tries to
  // allocate or rejects the deliberately missing pixel payload.
  const auto write_header_only_bmp = [&](const QString& file_name, qint32 width, qint32 height) {
    QByteArray bmp_header;
    QDataStream stream(&bmp_header, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    CHECK(stream.writeRawData("BM", 2) == 2);
    stream << quint32(54) << quint16(0) << quint16(0) << quint32(54);
    stream << quint32(40) << width << height << quint16(1) << quint16(24);
    stream << quint32(0) << quint32(0) << qint32(0) << qint32(0) << quint32(0) << quint32(0);
    const auto path = directory.filePath(file_name);
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    CHECK(file.write(bmp_header) == bmp_header.size());
    return path;
  };

  // Reject a declared >8 Mpx raster from its header.
  const auto oversized_path =
      write_header_only_bmp(QStringLiteral("oversized.bmp"), 4096, 2049);
  CHECK(library.import_image(oversized_path, error, warnings).isEmpty());
  CHECK(error.contains(QStringLiteral("8 million")));
  CHECK(library.entries().size() == 2);

  // A thin image can stay below the pixel budget while exceeding the maximum
  // dimension that Patchy's PSD Patt writer can preserve.
  const auto too_wide_path =
      write_header_only_bmp(QStringLiteral("too-wide.bmp"), 30001, 1);
  CHECK(library.import_image(too_wide_path, error, warnings).isEmpty());
  CHECK(error.contains(QStringLiteral("30,000")));
  CHECK(library.entries().size() == 2);

  // Animated formats import frame one and make that loss explicit.
  const auto gif_path = QString::fromStdWString(
      patchy::test::committed_format_fixture_path("gif", "pillow-animated.gif").wstring());
  CHECK(QFileInfo::exists(gif_path));
  const auto gif_storage_id = library.import_image(gif_path, error, warnings);
  CHECK(!gif_storage_id.isEmpty());
  CHECK(error.isEmpty());
  CHECK(warnings.join(QLatin1Char('\n')).contains(QStringLiteral("first frame"),
                                                  Qt::CaseInsensitive));
  const auto* gif_entry = library.find_entry(gif_storage_id);
  CHECK(gif_entry != nullptr);
  CHECK(gif_entry->size == QSize(32, 24));
  CHECK(library.entries().size() == 3);
}

void ui_default_brush_tips_seed_once_and_render_sheet() {
  clear_brush_tip_test_state();
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("brushes/defaultTipsVersion"), 0);
    settings.sync();
  }

  patchy::ui::MainWindow window;
  show_window(window);
  auto& library = window.brush_tip_library();
  const auto specs = patchy::ui::generate_default_brush_tips();
  CHECK(specs.size() == 40);
  CHECK(library.entries().size() == specs.size());
  const auto folder = patchy::ui::default_brush_tips_folder_name();
  for (const auto& entry : library.entries()) {
    CHECK(entry.folder == folder);
    CHECK(entry.size.width() > 0 && entry.size.width() <= 200);
    CHECK(entry.size.height() > 0 && entry.size.height() <= 200);
  }
  CHECK(library.folders() == QStringList{folder});

  // Thumbnails ride on a light paper chip so they stay visible on the dark toolbar/popup;
  // save a strip on the UI background color for visual review.
  {
    constexpr int kThumb = 48;
    constexpr int kPad = 8;
    const auto count = static_cast<int>(library.entries().size());
    QImage strip((kThumb + kPad) * count + kPad, kThumb + 2 * kPad, QImage::Format_RGB32);
    strip.fill(QColor(0x2B, 0x2B, 0x2B));
    QPainter strip_painter(&strip);
    int strip_x = kPad;
    int visible_thumbnails = 0;
    for (const auto& entry : library.entries()) {
      strip_painter.drawPixmap(strip_x, kPad, entry.thumbnail);
      const auto image = entry.thumbnail.toImage();
      int bright_pixels = 0;
      for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
          if (qAlpha(image.pixel(x, y)) > 200 && qGray(image.pixel(x, y)) > 150) {
            ++bright_pixels;
          }
        }
      }
      // Every thumbnail must carry a light backing (not just black-on-transparent ink).
      if (bright_pixels > kThumb * kThumb / 10) {
        ++visible_thumbnails;
      }
      strip_x += kThumb + kPad;
    }
    strip_painter.end();
    CHECK(visible_thumbnails == count);
    ensure_artifact_dir();
    CHECK(strip.save(QStringLiteral("test-artifacts/ui_brush_tip_thumbnails_dark.png")));
  }

  // Deleting a seeded tip must stick: the version gate stops re-seeding. "Restore Defaults"
  // is the explicit way back.
  const auto first_id = library.entries().front().id;
  CHECK(library.remove_tip(first_id));
  {
    patchy::ui::MainWindow second_window;
    show_window(second_window);
    auto& second_library = second_window.brush_tip_library();
    CHECK(second_library.entries().size() == specs.size() - 1);
    CHECK(second_library.restore_default_tips() == 1);
    CHECK(second_library.entries().size() == specs.size());
    CHECK(second_library.restore_default_tips() == 0);  // idempotent when nothing is missing
  }

  // Contact sheet: a real engine stroke plus a single stamp per tip, for visual review.
  constexpr int kCellWidth = 340;
  constexpr int kCellHeight = 96;
  constexpr int kColumns = 2;
  const int rows = (static_cast<int>(specs.size()) + kColumns - 1) / kColumns;
  QImage sheet(kColumns * kCellWidth, rows * kCellHeight, QImage::Format_RGB32);
  sheet.fill(Qt::white);
  QPainter painter(&sheet);
  int cell_index = 0;
  for (const auto& spec : specs) {
    const auto cell_x = (cell_index % kColumns) * kCellWidth;
    const auto cell_y = (cell_index / kColumns) * kCellHeight;
    ++cell_index;

    patchy::Document document(kCellWidth, kCellHeight, patchy::PixelFormat::rgba8());
    patchy::PixelBuffer pixels(kCellWidth, kCellHeight, patchy::PixelFormat::rgba8());
    pixels.clear(0);
    const auto layer_id = document.add_pixel_layer("Stroke", std::move(pixels)).id();
    const auto mips = patchy::build_brush_tip_mips(spec.tip);
    const auto brush_size = 44;
    const auto scaled = patchy::make_scaled_brush_tip(mips, brush_size);
    CHECK(!scaled.empty());
    patchy::EditOptions options;
    options.primary = patchy::EditColor{0, 0, 0, 255};
    options.brush_size = brush_size;
    options.brush_tip = &scaled;
    options.brush_tip_spacing = spec.spacing;
    options.brush_dynamics = spec.dynamics;
    options.brush_dynamics.seed = 0xD3FA017U;
    // Matches the app's stroke behavior at full opacity: overlapping dabs accumulate toward
    // solid coverage (CanvasWidget's stroke compositor), so no per-pixel gate is needed here —
    // plain source-over dab compositing produces the same result.
    patchy::BrushTipStrokeState state;
    double previous_x = 120.0;
    double previous_y = kCellHeight / 2.0;
    for (int step = 1; step <= 32; ++step) {
      const auto t = static_cast<double>(step) / 32.0;
      const auto x = 120.0 + t * (kCellWidth - 180.0);
      const auto y = kCellHeight / 2.0 - std::sin(t * 2.0 * 3.14159265358979323846) * (kCellHeight / 2.0 - 26.0);
      (void)patchy::paint_brush_segment(document, layer_id, previous_x, previous_y, x, y, options, false, state);
      previous_x = x;
      previous_y = y;
    }
    const auto* layer = document.find_layer(layer_id);
    CHECK(layer != nullptr);
    int painted = 0;
    QImage stroke(kCellWidth, kCellHeight, QImage::Format_RGBA8888);
    for (int y = 0; y < kCellHeight; ++y) {
      const auto row = layer->pixels().row(y);
      std::memcpy(stroke.scanLine(y), row.data(), static_cast<std::size_t>(kCellWidth) * 4U);
      for (int x = 0; x < kCellWidth; ++x) {
        painted += row.data()[static_cast<std::size_t>(x) * 4U + 3U] > 0U ? 1 : 0;
      }
    }
    // Dynamic scatter can place most of a sparse tip outside this deliberately small preview;
    // static tips retain the stronger historical coverage check.
    CHECK(painted > (spec.dynamics.active() ? 20 : 200));
    painter.drawImage(cell_x, cell_y, stroke);
    painter.setPen(Qt::black);
    painter.drawText(QRect(cell_x + 6, cell_y + 4, 110, 40), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                     QStringLiteral("%1\n%2%").arg(spec.name).arg(std::lround(spec.spacing * 100.0)));
    painter.setPen(QColor(0, 0, 0, 30));
    painter.drawRect(cell_x, cell_y, kCellWidth - 1, kCellHeight - 1);
  }
  painter.end();
  ensure_artifact_dir();
  CHECK(sheet.save(QStringLiteral("test-artifacts/ui_default_brush_tips_sheet.png")));

  // since_version filtering: a v2 install gains the v3 and v4 additions on upgrade; the
  // parameterless overload (the manager's Restore button) still restores everything missing.
  {
    patchy::ui::BrushTipLibrary versioned(brush_tip_test_storage_dir() + QStringLiteral("/v2-upgrade"));
    CHECK(versioned.restore_default_tips(2) == 24);  // v3 stamps plus v4 effect presets seed
    CHECK(versioned.restore_default_tips(2) == 0);   // idempotent
    CHECK(versioned.restore_default_tips() == 16);   // explicit restore brings back the originals
    CHECK(versioned.entries().size() == specs.size());
  }
  {
    patchy::ui::BrushTipLibrary versioned(brush_tip_test_storage_dir() + QStringLiteral("/v3-upgrade"));
    CHECK(versioned.restore_default_tips(3) == 4);  // only the v4 effect presets seed
  }

  // Upgrade end-to-end: simulate a v2 install that deleted an original default ("Chalk") and
  // never had a v3 tip ("Snowflake"). The version-gated seeding must re-add only the new tip
  // and leave the deletion alone, then advance the stored version.
  {
    patchy::ui::BrushTipLibrary storage(brush_tip_test_storage_dir());
    const auto entry_id = [&storage, &folder](const QString& name) {
      for (const auto& entry : storage.entries()) {
        if (entry.folder == folder && entry.name == name) {
          return entry.id;
        }
      }
      return QString();
    };
    CHECK(storage.remove_tip(entry_id(QStringLiteral("Chalk"))));
    CHECK(storage.remove_tip(entry_id(QStringLiteral("Snowflake"))));
  }
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("brushes/defaultTipsVersion"), 2);
    settings.sync();
  }
  {
    patchy::ui::MainWindow upgraded_window;
    show_window(upgraded_window);
    auto& upgraded = upgraded_window.brush_tip_library();
    bool has_chalk = false;
    bool has_snowflake = false;
    for (const auto& entry : upgraded.entries()) {
      has_chalk = has_chalk || (entry.folder == folder && entry.name == QStringLiteral("Chalk"));
      has_snowflake =
          has_snowflake || (entry.folder == folder && entry.name == QStringLiteral("Snowflake"));
    }
    CHECK(!has_chalk);      // the deliberate deletion is respected across the upgrade
    CHECK(has_snowflake);   // the tip introduced after v2 is seeded
    CHECK(upgraded.entries().size() == specs.size() - 1);
    CHECK(patchy::ui::app_settings().value(QStringLiteral("brushes/defaultTipsVersion")).toInt() == 4);
  }

  // A stale MASK (a tip seeded before a generator artwork fix) also heals via the factory
  // reset: the PNG is rewritten in place under the same id.
  QString stale_star_id;
  {
    patchy::ui::BrushTipLibrary storage(brush_tip_test_storage_dir());
    for (const auto& entry : storage.entries()) {
      if (entry.folder == folder && entry.name == QStringLiteral("Star")) {
        stale_star_id = entry.id;
      }
    }
    CHECK(!stale_star_id.isEmpty());
    QImage stale(8, 8, QImage::Format_Grayscale8);
    stale.fill(255);
    CHECK(stale.save(
        brush_tip_test_storage_dir() + QStringLiteral("/") + stale_star_id + QStringLiteral(".png"),
        "PNG"));
  }
  {
    patchy::ui::BrushTipLibrary storage(brush_tip_test_storage_dir());
    const auto* stale_star = storage.find_entry(stale_star_id);
    CHECK(stale_star != nullptr);
    CHECK(stale_star->size == QSize(8, 8));
    CHECK(storage.reset_default_tips_to_factory() == 1);  // only the doctored Star differs
    const auto* fixed = storage.find_entry(stale_star_id);
    CHECK(fixed != nullptr);
    CHECK(fixed->size.width() > 100 && fixed->size.height() > 100);
    const auto reloaded_tip = storage.tip(stale_star_id);
    CHECK(reloaded_tip != nullptr);
    CHECK(reloaded_tip->width == fixed->size.width());
    CHECK(storage.reset_default_tips_to_factory() == 0);  // idempotent once factory
  }
  clear_brush_tip_test_state();
}

void ui_brush_tip_folders_and_bulk_delete() {
  clear_brush_tip_test_state();
  patchy::ui::MainWindow window;
  show_window(window);
  auto& library = window.brush_tip_library();

  // Imported sets land in a folder named after the file.
  QString error;
  QStringList warnings;
  const auto fixture =
      QString::fromStdString(patchy::test::committed_abr_fixture_path("myer-settlement-brushes.abr").string());
  const auto first_id = library.import_abr(fixture, error, warnings);
  CHECK(!first_id.isEmpty());
  CHECK(library.entries().size() == 148);
  CHECK(library.folders() == QStringList{QStringLiteral("myer-settlement-brushes")});
  CHECK(library.find_entry(first_id)->folder == QStringLiteral("myer-settlement-brushes"));

  const auto loose_id = library.add_tip(QStringLiteral("Loose"), make_bar_tip_image(), 0.25);
  CHECK(!loose_id.isEmpty());
  CHECK(library.find_entry(loose_id)->folder.isEmpty());
  // Ungrouped tips sort ahead of foldered ones.
  CHECK(library.entries().front().id == loose_id);

  // Moving between folders rewrites the sidecar and regroups.
  CHECK(library.set_tip_folder(loose_id, QStringLiteral("My Set")));
  CHECK(library.find_entry(loose_id)->folder == QStringLiteral("My Set"));
  CHECK(library.folders().contains(QStringLiteral("My Set")));

  // Bulk delete removes everything in one changed() notification.
  int changed_signals = 0;
  QObject::connect(&library, &patchy::ui::BrushTipLibrary::changed, &window, [&changed_signals] {
    ++changed_signals;
  });
  QStringList doomed;
  for (const auto& entry : library.entries()) {
    if (entry.folder == QStringLiteral("myer-settlement-brushes")) {
      doomed.append(entry.id);
    }
  }
  CHECK(doomed.size() == 148);
  CHECK(library.remove_tips(doomed) == 148);
  CHECK(changed_signals == 1);
  CHECK(library.entries().size() == 1);
  CHECK(library.folders() == QStringList{QStringLiteral("My Set")});

  // The removal is durable: a fresh library sees the same state.
  patchy::ui::BrushTipLibrary reloaded(brush_tip_test_storage_dir());
  CHECK(reloaded.entries().size() == 1);
  CHECK(reloaded.find_entry(loose_id) != nullptr);
  clear_brush_tip_test_state();
}

void ui_brush_tip_resets_to_round_on_startup() {
  clear_brush_tip_test_state();
  QString tip_id;
  {
    patchy::ui::MainWindow window;
    show_window(window);
    auto& library = window.brush_tip_library();
    tip_id = library.add_tip(QStringLiteral("Session Bar"), make_bar_tip_image(), 0.5);
    CHECK(!tip_id.isEmpty());
    window.set_active_brush_tip(tip_id, false);
    CHECK(require_canvas(window)->has_brush_tip());
    process_events_for(400);  // any debounced tool-settings save must not record the tip
    CHECK(!patchy::ui::app_settings().contains(QStringLiteral("tools/brushTip")));
  }
  {
    // A fresh launch always starts with the procedural Round tip at 100% opacity /
    // 0% soft, even though a bitmap tip was active when the last window closed.
    patchy::ui::MainWindow window;
    show_window(window);
    auto* canvas = require_canvas(window);
    CHECK(!canvas->has_brush_tip());
    CHECK(canvas->brush_opacity() == 100);
    CHECK(canvas->brush_softness() == 0);
    auto* picker = window.findChild<patchy::ui::BrushTipPicker*>(QStringLiteral("brushTipPicker"));
    CHECK(picker != nullptr);
    CHECK(picker->current_tip_id() == patchy::ui::builtin_round_brush_tip_id());
  }
  clear_brush_tip_test_state();
}

}  // namespace

std::vector<patchy::test::TestCase> brush_pattern_palette_tests() {
  return {
      {"ui_brush_tip_paints_and_erases_with_bitmap_stamp", ui_brush_tip_paints_and_erases_with_bitmap_stamp},
      {"ui_brush_tip_abr_import_populates_library_and_picker",
       ui_brush_tip_abr_import_populates_library_and_picker},
      {"ui_brush_tip_define_from_image_uses_inverted_luminance",
       ui_brush_tip_define_from_image_uses_inverted_luminance},
      {"ui_brush_tip_folders_and_bulk_delete", ui_brush_tip_folders_and_bulk_delete},
      {"ui_brush_tip_manager_folder_rows_fit_thumbnails", ui_brush_tip_manager_folder_rows_fit_thumbnails},
      {"ui_pattern_library_imports_pat_and_persists_folders",
       ui_pattern_library_imports_pat_and_persists_folders},
      {"ui_pattern_library_aggregates_parser_warnings",
       ui_pattern_library_aggregates_parser_warnings},
      {"ui_pattern_library_reimport_deduplicates_remapped_source_id",
       ui_pattern_library_reimport_deduplicates_remapped_source_id},
      {"ui_pattern_library_rejects_oversized_pat_before_read",
       ui_pattern_library_rejects_oversized_pat_before_read},
      {"ui_pattern_library_delete_requires_tile_removal",
       ui_pattern_library_delete_requires_tile_removal},
      {"ui_pattern_library_imports_image_files", ui_pattern_library_imports_image_files},
      {"ui_default_patterns_seed_once_and_restore", ui_default_patterns_seed_once_and_restore},
      {"ui_brush_tip_softness_feathers_stroke_and_size_reaches_1024",
       ui_brush_tip_softness_feathers_stroke_and_size_reaches_1024},
      {"ui_palette_panel_click_sets_foreground_and_chip_tracks_mode",
       ui_palette_panel_click_sets_foreground_and_chip_tracks_mode},
      {"ui_convert_to_indexed_dialog_converts_and_undoes", ui_convert_to_indexed_dialog_converts_and_undoes},
      {"ui_convert_to_indexed_preview_zoom_and_pan", ui_convert_to_indexed_preview_zoom_and_pan},
      {"ui_convert_to_rgb_prompts_to_keep_palettized_look", ui_convert_to_rgb_prompts_to_keep_palettized_look},
      {"ui_color_picker_file_palette_clipboard_and_drop", ui_color_picker_file_palette_clipboard_and_drop},
      {"ui_color_picker_palette_dropdown_tracks_mode_and_choice",
       ui_color_picker_palette_dropdown_tracks_mode_and_choice},
      {"ui_palette_panel_copy_hex_and_updates_open_picker", ui_palette_panel_copy_hex_and_updates_open_picker},
      {"ui_indexed_bmp_open_adopts_palette", ui_indexed_bmp_open_adopts_palette},
      {"ui_indexed_tga_open_adopts_palette", ui_indexed_tga_open_adopts_palette},
      {"ui_png8_export_round_trips_indexed", ui_png8_export_round_trips_indexed},
      {"ui_palette_mode_display_quantizes_layer_styles", ui_palette_mode_display_quantizes_layer_styles},
      {"ui_palette_panel_swap_copy_paste_and_index_readout", ui_palette_panel_swap_copy_paste_and_index_readout},
      {"ui_palette_mode_bmp_save_defaults_to_exact_indexed", ui_palette_mode_bmp_save_defaults_to_exact_indexed},
      {"ui_preferences_indexed_open_policy_persists", ui_preferences_indexed_open_policy_persists},
      {"ui_palette_mode_brush_stroke_writes_only_palette_colors",
       ui_palette_mode_brush_stroke_writes_only_palette_colors},
      {"ui_brush_tip_soft_stamps_accumulate_without_seams", ui_brush_tip_soft_stamps_accumulate_without_seams},
      {"ui_brush_outline_overlay_tracks_large_brushes", ui_brush_outline_overlay_tracks_large_brushes},
      {"ui_brush_tip_cursor_shows_tip_shape", ui_brush_tip_cursor_shows_tip_shape},
      {"ui_brush_tip_picker_popup_offers_define_from_selection",
       ui_brush_tip_picker_popup_offers_define_from_selection},
      {"ui_brush_tip_picker_keeps_options_bar_height", ui_brush_tip_picker_keeps_options_bar_height},
      {"ui_default_brush_tips_carry_curated_dynamics", ui_default_brush_tips_carry_curated_dynamics},
      {"ui_brush_tip_manager_edits_dynamics", ui_brush_tip_manager_edits_dynamics},
      {"ui_brush_tip_picker_popup_resizes_and_persists", ui_brush_tip_picker_popup_resizes_and_persists},
      {"ui_brush_dynamics_round_brush_session", ui_brush_dynamics_round_brush_session},
      {"ui_brush_dynamics_popup_edits_apply_and_persist", ui_brush_dynamics_popup_edits_apply_and_persist},
      {"ui_brush_dynamics_popup_control_edits_persist", ui_brush_dynamics_popup_control_edits_persist},
      {"ui_brush_dynamics_button_toggles_popup_closed", ui_brush_dynamics_button_toggles_popup_closed},
      {"ui_brush_dynamics_stroke_scatters_with_seed", ui_brush_dynamics_stroke_scatters_with_seed},
      {"ui_brush_color_dynamics_reaches_stroke_compositor",
       ui_brush_color_dynamics_reaches_stroke_compositor},
      {"ui_brush_dynamics_abr_import_carries_dynamics", ui_brush_dynamics_abr_import_carries_dynamics},
      {"ui_default_brush_tips_seed_once_and_render_sheet", ui_default_brush_tips_seed_once_and_render_sheet},
      {"ui_brush_tip_resets_to_round_on_startup", ui_brush_tip_resets_to_round_on_startup},
  };
}
