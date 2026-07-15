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

void ui_color_picker_changes_foreground_color() {
  ensure_artifact_dir();
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* foreground = window.findChild<QPushButton*>(QStringLiteral("foregroundColorButton"));
  CHECK(foreground != nullptr);

  foreground->click();
  QApplication::processEvents();
  QDialog* dialog = nullptr;
  for (auto* widget : QApplication::topLevelWidgets()) {
    if (widget->objectName() == QStringLiteral("patchyColorDialog")) {
      dialog = qobject_cast<QDialog*>(widget);
      break;
    }
  }
  CHECK(dialog != nullptr);
  CHECK(dialog->isVisible());
  CHECK(!dialog->isModal());
  CHECK(dialog->windowModality() == Qt::NonModal);
  CHECK(dialog->windowFlags().testFlag(Qt::FramelessWindowHint));
  CHECK(dialog->findChild<QWidget*>(QStringLiteral("dialogChromeTitleBar")) != nullptr);
  CHECK(dialog->findChild<QToolButton*>(QStringLiteral("dialogChromeCloseButton")) != nullptr);

  auto* picker = dialog->findChild<patchy::ui::PatchyColorPicker*>(QStringLiteral("patchyAdvancedColorPicker"));
  CHECK(picker != nullptr);
  auto* color_plane = picker->findChild<QWidget*>(QStringLiteral("patchyColorPlane"));
  CHECK(color_plane != nullptr);
  const auto color_plane_image = color_plane->grab().toImage();
  CHECK(color_close(color_plane_image.pixelColor(1, 1), QColor(255, 255, 255), 4));
  CHECK(color_close(color_plane_image.pixelColor(color_plane_image.width() - 2, 1), QColor(255, 0, 0), 4));
  CHECK(color_close(color_plane_image.pixelColor(color_plane_image.width() / 2, color_plane_image.height() - 2),
                    QColor(0, 0, 0), 4));
  send_mouse(*color_plane, QEvent::MouseButtonPress, QPoint(0, 0), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*color_plane, QEvent::MouseButtonRelease, QPoint(0, 0), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(canvas->primary_color() == QColor(255, 255, 255));
  auto custom_swatches = picker->findChildren<QPushButton*>(QStringLiteral("patchyCustomColorSwatch"));
  auto* set_custom = picker->findChild<QPushButton*>(QStringLiteral("patchySetCustomColorButton"));
  CHECK(custom_swatches.size() == 16);
  CHECK(set_custom != nullptr);
  // The single Set button replaced the old Add/Update/Delete trio.
  CHECK(picker->findChild<QPushButton*>(QStringLiteral("patchyAddCustomColorButton")) == nullptr);
  CHECK(picker->findChild<QPushButton*>(QStringLiteral("patchyUpdateCustomColorButton")) == nullptr);
  CHECK(picker->findChild<QPushButton*>(QStringLiteral("patchyDeleteCustomColorButton")) == nullptr);
  CHECK(!set_custom->isEnabled());
  // Selecting a swatch must not resize it or shift the rows (a state-dependent
  // QSS border once grew the content box by 2px and made the grid jump).
  const auto unselected_geometry = custom_swatches[1]->geometry();
  custom_swatches[1]->click();
  QApplication::processEvents();
  CHECK(custom_swatches[1]->geometry() == unselected_geometry);
  custom_swatches.front()->click();
  QApplication::processEvents();
  CHECK(custom_swatches[1]->geometry() == unselected_geometry);
  CHECK(set_custom->isEnabled());
  picker->setCurrentColor(QColor(10, 20, 30));
  set_custom->click();
  QApplication::processEvents();
  picker->setCurrentColor(QColor(200, 210, 220));
  custom_swatches.front()->click();
  QApplication::processEvents();
  CHECK(canvas->primary_color() == QColor(10, 20, 30));
  picker->setCurrentColor(QColor(30, 40, 50));
  set_custom->click();
  picker->setCurrentColor(QColor(90, 80, 70));
  custom_swatches.front()->click();
  QApplication::processEvents();
  CHECK(canvas->primary_color() == QColor(30, 40, 50));
  picker->setCurrentColor(QColor(80, 120, 200));
  QApplication::processEvents();
  CHECK(canvas->primary_color() == QColor(80, 120, 200));
  dialog->grab().save(QStringLiteral("test-artifacts/ui_color_picker_gradient.png"));
  picker->setCurrentColor(QColor(12, 180, 240));
  QApplication::processEvents();
  dialog->grab().save(QStringLiteral("test-artifacts/ui_color_picker.png"));
  CHECK(canvas->primary_color() == QColor(12, 180, 240));
  CHECK(std::filesystem::exists(std::filesystem::path("test-artifacts") / "ui_color_picker.png"));
  save_widget_artifact("ui_color_picker_result", window);
  dialog->close();
  QApplication::processEvents();
}

void ui_color_picker_ignores_reentrant_requests() {
  ensure_artifact_dir();
  const auto count_visible_pickers = [] {
    int count = 0;
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() == QStringLiteral("patchyColorDialog") && widget->isVisible()) {
        ++count;
      }
    }
    return count;
  };

  bool nested_request_returned = false;
  std::optional<QColor> nested_result;
  int pickers_while_first_open = 0;
  QTimer::singleShot(0, [&] {
    QDialog* first = nullptr;
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() == QStringLiteral("patchyColorDialog") && widget->isVisible()) {
        first = qobject_cast<QDialog*>(widget);
        break;
      }
    }
    CHECK(first != nullptr);
    // Re-enter while the first picker is still open; the guard must reject this
    // immediately instead of stacking a second identical picker on top.
    nested_result = patchy::ui::request_patchy_color(nullptr, QColor(1, 2, 3), QStringLiteral("Nested"));
    nested_request_returned = true;
    pickers_while_first_open = count_visible_pickers();
    if (first != nullptr) {
      first->accept();
    }
  });
  const auto result = patchy::ui::request_patchy_color(nullptr, QColor(10, 20, 30), QStringLiteral("Re-entrancy"));
  CHECK(nested_request_returned);
  CHECK(!nested_result.has_value());
  CHECK(pickers_while_first_open == 1);
  CHECK(result.has_value());
  QApplication::processEvents();
  CHECK(count_visible_pickers() == 0);
}

void ui_color_picker_closes_with_parent_dialog() {
  // Pickers launched from a dialog (layer style swatches, gradient stops) are
  // non-modal, so the user can close the parent dialog while the picker is still
  // open. The orphaned picker used to survive its parent, drop behind the main
  // window on the next click, and its stuck request guard then silently swallowed
  // every later color-pick request in the app.
  QDialog parent;
  parent.setObjectName(QStringLiteral("testPickerParentDialog"));
  parent.show();
  QApplication::processEvents();

  bool closed_parent_while_picker_open = false;
  QTimer::singleShot(0, &parent, [&] {
    closed_parent_while_picker_open = true;
    parent.reject();
  });
  const auto orphaned =
      patchy::ui::request_patchy_color(&parent, QColor(10, 20, 30), QStringLiteral("Orphaned"));
  CHECK(closed_parent_while_picker_open);
  CHECK(!orphaned.has_value());
  for (auto* widget : QApplication::topLevelWidgets()) {
    CHECK(!(widget->objectName() == QStringLiteral("patchyColorDialog") && widget->isVisible()));
  }

  // The single-picker guard must have been released with it: a later request
  // opens a fresh picker instead of silently doing nothing.
  bool second_picker_opened = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() == QStringLiteral("patchyColorDialog") && widget->isVisible()) {
        second_picker_opened = true;
        qobject_cast<QDialog*>(widget)->accept();
        return;
      }
    }
    CHECK(false);
  });
  const auto second = patchy::ui::request_patchy_color(nullptr, QColor(3, 2, 1), QStringLiteral("Fresh"));
  CHECK(second_picker_opened);
  CHECK(second.has_value());
}

void ui_color_picker_wheel_and_sliders_modes() {
  ensure_artifact_dir();
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* foreground = window.findChild<QPushButton*>(QStringLiteral("foregroundColorButton"));
  CHECK(foreground != nullptr);

  // Clear any persisted tab so the picker opens on Square (the unset default).
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("colorPanel/lastTab"));
    settings.sync();
  }

  foreground->click();
  QApplication::processEvents();
  QDialog* dialog = nullptr;
  for (auto* widget : QApplication::topLevelWidgets()) {
    if (widget->objectName() == QStringLiteral("patchyColorDialog") && widget->isVisible()) {
      dialog = qobject_cast<QDialog*>(widget);
      break;
    }
  }
  CHECK(dialog != nullptr);

  auto* picker = dialog->findChild<patchy::ui::PatchyColorPicker*>(QStringLiteral("patchyAdvancedColorPicker"));
  CHECK(picker != nullptr);
  auto* tabs = picker->findChild<QTabWidget*>(QStringLiteral("patchyColorPickerTabs"));
  CHECK(tabs != nullptr);
  CHECK(tabs->count() == 3);
  CHECK(tabs->currentIndex() == 0);  // Square mode is the unset default.

  // Start from a fully saturated colour so hue edits on the wheel are visible.
  picker->setCurrentColor(QColor(0, 128, 255));
  QApplication::processEvents();

  // --- Wheel mode: hue ring + inner saturation/value square. ---
  tabs->setCurrentIndex(1);
  QApplication::processEvents();
  auto* wheel = picker->findChild<QWidget*>(QStringLiteral("patchyColorWheel"));
  CHECK(wheel != nullptr);
  CHECK(wheel->width() >= 150);
  CHECK(wheel->height() >= 150);

  // Mirror the widget's own geometry so the clicks land on the ring / inner square
  // regardless of how large the wheel expanded to.
  const int cx = wheel->width() / 2;
  const int cy = wheel->height() / 2;
  const double side = std::min(wheel->width(), wheel->height());
  const double outer = side / 2.0 - 2.0;
  const double inner = outer - 20.0;  // kColorWheelRing
  const int ring_radius = static_cast<int>(std::lround((outer + inner) / 2.0));

  // The ring is a static hue wheel; its 3 o'clock band is red (hue 0).
  const auto wheel_image = wheel->grab().toImage();
  CHECK(color_close(wheel_image.pixelColor(cx + ring_radius, cy), QColor(255, 0, 0), 80));

  // Click the top of the ring (12 o'clock) -> hue ~90.
  auto* hue_spin = picker->findChild<QSpinBox*>(QStringLiteral("patchyColorHueSpin"));
  CHECK(hue_spin != nullptr);
  send_mouse(*wheel, QEvent::MouseButtonPress, QPoint(cx, cy - ring_radius), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*wheel, QEvent::MouseButtonRelease, QPoint(cx, cy - ring_radius), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(std::abs(hue_spin->value() - 90) <= 6);
  dialog->grab().save(QStringLiteral("test-artifacts/ui_color_picker_wheel.png"));

  // Click the centre of the inner square -> mid saturation/value (well inside the
  // ring), confirming the square drives sat/val while the hue stays put.
  auto* sat_spin = picker->findChild<QSpinBox*>(QStringLiteral("patchyColorSaturationSpin"));
  auto* val_spin = picker->findChild<QSpinBox*>(QStringLiteral("patchyColorValueSpin"));
  CHECK(sat_spin != nullptr);
  CHECK(val_spin != nullptr);
  send_mouse(*wheel, QEvent::MouseButtonPress, QPoint(cx, cy), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*wheel, QEvent::MouseButtonRelease, QPoint(cx, cy), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(std::abs(sat_spin->value() - 128) <= 24);
  CHECK(std::abs(val_spin->value() - 128) <= 24);
  CHECK(std::abs(hue_spin->value() - 90) <= 6);  // hue unchanged by the square

  // --- Sliders mode: one gradient track per channel. ---
  picker->setCurrentColor(QColor(40, 60, 80));
  tabs->setCurrentIndex(2);
  QApplication::processEvents();

  // Switching tabs is remembered for next time.
  {
    auto settings = patchy::ui::app_settings();
    CHECK(settings.value(QStringLiteral("colorPanel/lastTab"), -1).toInt() == 2);
  }

  auto* red_slider = picker->findChild<QWidget*>(QStringLiteral("patchyChannelSliderRed"));
  CHECK(red_slider != nullptr);
  CHECK(red_slider->width() > 40);
  const int slider_y = red_slider->height() / 2;

  // Drag the red track to the far right -> red 255, other channels untouched.
  send_mouse(*red_slider, QEvent::MouseButtonPress, QPoint(red_slider->width() - 1, slider_y), Qt::LeftButton,
             Qt::LeftButton);
  send_mouse(*red_slider, QEvent::MouseButtonRelease, QPoint(red_slider->width() - 1, slider_y), Qt::LeftButton,
             Qt::NoButton);
  QApplication::processEvents();
  CHECK(canvas->primary_color() == QColor(255, 60, 80));
  dialog->grab().save(QStringLiteral("test-artifacts/ui_color_picker_sliders.png"));

  // Far left -> red 0.
  send_mouse(*red_slider, QEvent::MouseButtonPress, QPoint(0, slider_y), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*red_slider, QEvent::MouseButtonRelease, QPoint(0, slider_y), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(canvas->primary_color().red() == 0);

  dialog->close();
  QApplication::processEvents();
}

void ui_dialog_position_memory_restores_last_position() {
  const auto settings_group = QStringLiteral("dialogPositions/patchyDialogPositionMemoryTest");
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(settings_group);
    settings.sync();
  }

  const auto screen_rect = QApplication::primaryScreen() != nullptr
                               ? QApplication::primaryScreen()->availableGeometry()
                               : QRect(0, 0, 640, 480);
  const auto target_position = screen_rect.topLeft() + QPoint(37, 43);
  QPoint saved_position;

  {
    QDialog dialog;
    dialog.setObjectName(QStringLiteral("patchyDialogPositionMemoryTest"));
    dialog.resize(140, 90);
    patchy::ui::remember_dialog_position(dialog);
    dialog.show();
    QApplication::processEvents();
    dialog.move(target_position);
    QApplication::processEvents();
    saved_position = dialog.pos();
    dialog.close();
    QApplication::processEvents();
  }

  {
    QDialog dialog;
    dialog.setObjectName(QStringLiteral("patchyDialogPositionMemoryTest"));
    dialog.resize(140, 90);
    patchy::ui::remember_dialog_position(dialog);
    dialog.show();
    QApplication::processEvents();
    CHECK((dialog.pos() - saved_position).manhattanLength() <= 2);
    dialog.close();
    QApplication::processEvents();
  }

  auto settings = patchy::ui::app_settings();
  settings.remove(settings_group);
  settings.sync();
}

void ui_dialog_position_memory_centers_unmoved_dialogs_on_parent() {
  const auto settings_group = QStringLiteral("dialogPositions/patchyDialogCenterTest");
  const auto screen_rect = QApplication::primaryScreen() != nullptr
                               ? QApplication::primaryScreen()->availableGeometry()
                               : QRect(0, 0, 640, 480);
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(settings_group);
    settings.setValue(settings_group + QStringLiteral("/pos"), screen_rect.topLeft() + QPoint(4, 5));
    settings.sync();
  }

  QWidget parent;
  parent.resize(420, 260);
  parent.move(screen_rect.topLeft() + QPoint(80, 70));
  parent.show();
  QApplication::processEvents();

  QDialog dialog(&parent);
  dialog.setObjectName(QStringLiteral("patchyDialogCenterTest"));
  dialog.resize(140, 90);
  patchy::ui::remember_dialog_position(dialog);
  dialog.show();
  QApplication::processEvents();

  const auto expected_position =
      parent.frameGeometry().center() - QPoint(dialog.size().width() / 2, dialog.size().height() / 2);
  CHECK((dialog.pos() - expected_position).manhattanLength() <= 10);
  dialog.close();
  QApplication::processEvents();

  auto settings = patchy::ui::app_settings();
  CHECK(!settings.value(settings_group + QStringLiteral("/pos")).isValid());
  settings.remove(settings_group);
  settings.sync();
}

void ui_dirty_state_marks_tabs_and_undo_restores_saved_revision() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  CHECK(tabs != nullptr);
  CHECK(!tabs->tabText(tabs->currentIndex()).endsWith(QStringLiteral("*")));

  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  CHECK(tabs->tabText(tabs->currentIndex()).endsWith(QStringLiteral("*")));

  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(!tabs->tabText(tabs->currentIndex()).endsWith(QStringLiteral("*")));
}

void ui_compatibility_report_flags_psd_text_placeholders() {
  patchy::Document document(120, 90, patchy::PixelFormat::rgba8());
  auto& layer = document.add_pixel_layer("PSD Text", solid_pixels(120, 90, patchy::PixelFormat::rgba8(),
                                                                  QColor(0, 0, 0, 0)));
  layer.metadata()[patchy::kLayerMetadataText] = "Title";
  layer.metadata()[patchy::kLayerMetadataTextFont] = "PSD Text";
  layer.metadata()[patchy::kLayerMetadataTextSourceBlock] = "TySh";
  layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "placeholder";
  layer.unknown_psd_blocks().push_back(patchy::UnknownPsdBlock{"TySh", {1, 2, 3}});
  auto warnings = patchy::ui::compatibility_warnings_for_document(document);
  CHECK(!warnings.isEmpty());
  CHECK(warnings.join(QLatin1Char('\n')).contains(QStringLiteral("placeholder")));
  CHECK(warnings.join(QLatin1Char('\n')).contains(QStringLiteral("TySh")));
  CHECK(warnings.join(QLatin1Char('\n')).contains(QStringLiteral("unknown PSD layer block")));
}

void ui_compatibility_report_ignores_patchy_written_psd_blocks() {
  patchy::Document document(120, 90, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_pixels(120, 90, patchy::PixelFormat::rgb8(), QColor(Qt::white)));
  auto& text_layer = document.add_pixel_layer(
      "Text: Hey man", solid_pixels(120, 90, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0)));
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.distance = 4.0F;
  shadow.size = 6.0F;
  text_layer.layer_style().drop_shadows.push_back(shadow);

  const auto round_tripped = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document));
  const auto warnings = patchy::ui::compatibility_warnings_for_document(round_tripped);
  CHECK(warnings.isEmpty());
}

void ui_compatibility_report_treats_levels_as_native_psd_adjustment() {
  patchy::Document document(120, 90, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_pixels(120, 90, patchy::PixelFormat::rgb8(), QColor(Qt::white)));
  patchy::AdjustmentSettings settings;
  settings.kind = patchy::AdjustmentKind::Levels;
  settings.levels.red.black_output = 128;
  patchy::Layer levels(document.allocate_layer_id(), "Levels", patchy::LayerKind::Adjustment);
  levels.set_bounds(patchy::Rect::from_size(document.width(), document.height()));
  patchy::configure_adjustment_layer(levels, settings);
  document.add_layer(std::move(levels));

  const auto warnings = patchy::ui::compatibility_warnings_for_document(document);
  CHECK(warnings.isEmpty());
}

void ui_compatibility_report_flags_cmyk_rgb_conversion() {
  patchy::Document document(120, 90, patchy::PixelFormat::rgb8());
  document.metadata().values["psd.color_mode"] = "CMYK";
  document.add_pixel_layer("Background", solid_pixels(120, 90, patchy::PixelFormat::rgb8(), QColor(Qt::white)));

  const auto warnings = patchy::ui::compatibility_warnings_for_document(document);
  CHECK(!warnings.isEmpty());
  const auto text = warnings.join(QLatin1Char('\n'));
  CHECK(text.contains(QStringLiteral("CMYK")));
  CHECK(text.contains(QStringLiteral("converted")));
  CHECK(text.contains(QStringLiteral("RGB/RGBA")));
}

void ui_compatibility_report_flags_unrendered_styles_on_groups() {
  patchy::Document document(120, 90, patchy::PixelFormat::rgba8());
  patchy::Layer group(document.allocate_layer_id(), "Styled Group", patchy::LayerKind::Group);
  patchy::LayerSatin satin;
  satin.enabled = true;
  group.layer_style().satins.push_back(satin);
  patchy::LayerPatternOverlay pattern;
  pattern.enabled = true;
  group.layer_style().pattern_overlays.push_back(pattern);
  group.add_child(patchy::Layer(document.allocate_layer_id(), "Child",
                               solid_pixels(20, 20, patchy::PixelFormat::rgba8(), QColor(Qt::white))));
  document.add_layer(std::move(group));

  const auto warnings = patchy::ui::compatibility_warnings_for_document(document);
  const auto text = warnings.join(QLatin1Char('\n'));
  CHECK(text.contains(QStringLiteral("Styled Group")));
  CHECK(text.contains(QStringLiteral("group with layer effects")));
  CHECK(text.contains(QStringLiteral("does not render group layer effects")));
  // Pattern Overlay is a supported effect now; the report must not warn about it.
  CHECK(!text.contains(QStringLiteral("Pattern Overlay")));

  auto& imported_satin = document.layers().front().layer_style().satins.front();
  imported_satin.enabled = false;
  imported_satin.unsupported_contour_options = true;
  const auto contour_text =
      patchy::ui::compatibility_warnings_for_document(document).join(QLatin1Char('\n'));
  CHECK(contour_text.contains(QStringLiteral("Satin contour settings")));
  CHECK(contour_text.contains(QStringLiteral("custom curve or anti-aliasing")));
  CHECK(contour_text.contains(QStringLiteral("non-anti-aliased Linear contour")));
}

void ui_compatibility_report_handles_supported_unsupported_and_boundary_blend_if() {
  constexpr std::array<std::uint8_t, 8> identity_range{0, 0, 255, 255, 0, 0, 255, 255};
  std::vector<std::uint8_t> identity_payload;
  for (int channel = 0; channel < 5; ++channel) {
    identity_payload.insert(identity_payload.end(), identity_range.begin(), identity_range.end());
  }

  patchy::Document document(120, 90, patchy::PixelFormat::rgba8());
  patchy::Layer group(document.allocate_layer_id(), "Blend If Group", patchy::LayerKind::Group);
  group.raw_psd_blending_ranges() = identity_payload;
  group.raw_psd_group_boundary_blending_ranges() = identity_payload;
  patchy::Layer child(document.allocate_layer_id(), "Blend If Layer",
                      solid_pixels(20, 20, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  child.raw_psd_blending_ranges() = identity_payload;
  group.add_child(std::move(child));
  document.add_layer(std::move(group));

  CHECK(patchy::ui::compatibility_warnings_for_document(document).isEmpty());

  const auto supported_nondefault = patchy::encode_layer_blend_if(blend_if_ui_test_settings());
  auto& child_layer = document.layers().front().children().front();
  child_layer.set_blend_if_payload(supported_nondefault, true);
  CHECK(patchy::ui::compatibility_warnings_for_document(document).isEmpty());

  child_layer.set_blend_if_payload(std::vector<std::uint8_t>(identity_range.begin(), identity_range.end()), true);
  const auto unsupported_text =
      patchy::ui::compatibility_warnings_for_document(document).join(QLatin1Char('\n'));
  CHECK(unsupported_text.contains(QStringLiteral("Blend If Layer")));
  CHECK(unsupported_text.contains(QStringLiteral("unsupported color mode or payload shape")));
  CHECK(unsupported_text.contains(QStringLiteral("preserves it for PSD round-trip")));
  CHECK(unsupported_text.contains(QStringLiteral("does not render or edit")));

  child_layer.set_blend_if_payload(supported_nondefault, true);
  document.layers().front().raw_psd_group_boundary_blending_ranges() = supported_nondefault;
  const auto boundary_text =
      patchy::ui::compatibility_warnings_for_document(document).join(QLatin1Char('\n'));
  CHECK(boundary_text.contains(QStringLiteral("Blend If Group")));
  CHECK(boundary_text.contains(QStringLiteral("group-boundary record")));
  CHECK(boundary_text.contains(QStringLiteral("preserves that boundary data")));
  CHECK(boundary_text.contains(QStringLiteral("does not render or edit")));
}

void ui_psd_import_notice_reports_only_unsupported_blend_if() {
  SettingsValueRestorer restore_psd_warning_check(QStringLiteral("imports/showPsdWarningsAndInfo"));
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("imports/showPsdWarningsAndInfo"));
    settings.sync();
  }

  QTemporaryDir temp;
  CHECK(temp.isValid());
  const auto write_blend_if_fixture = [&](const QString& name, const std::vector<std::uint8_t>& payload) {
    patchy::Document document(32, 24, patchy::PixelFormat::rgba8());
    patchy::Layer layer(document.allocate_layer_id(), name.toStdString(),
                        solid_pixels(16, 12, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
    layer.set_blend_if_payload(payload, true);
    document.add_layer(std::move(layer));
    const auto path = temp.filePath(name + QStringLiteral(".psd"));
    patchy::psd::DocumentIo::write_layered_rgb8_file(document, path.toStdString());
    return path;
  };

  const auto supported_path =
      write_blend_if_fixture(QStringLiteral("supported-blend-if"),
                             patchy::encode_layer_blend_if(blend_if_ui_test_settings()));
  constexpr std::array<std::uint8_t, 8> unsupported_payload{0, 0, 255, 255, 0, 0, 255, 255};
  const auto unsupported_path = write_blend_if_fixture(
      QStringLiteral("unsupported-blend-if"),
      std::vector<std::uint8_t>(unsupported_payload.begin(), unsupported_payload.end()));

  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(window, supported_path);
  QApplication::processEvents();
  CHECK(!window.statusBar()->currentMessage().contains(QStringLiteral("Blend If")));

  patchy::ui::MainWindowTestAccess::open_document_path(window, unsupported_path);
  QApplication::processEvents();
  const auto notice = window.statusBar()->currentMessage();
  CHECK(notice.contains(QStringLiteral("unsupported Photoshop Blend If payloads")));
  CHECK(notice.contains(QStringLiteral("does not render or edit")));
  CHECK(notice.contains(QStringLiteral("1 layer(s)")));
}

void ui_compatibility_report_describes_linked_smart_object_updates() {
  patchy::Document document(120, 90, patchy::PixelFormat::rgba8());
  auto& layer = document.add_pixel_layer(
      "Linked Object", solid_pixels(120, 90, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  layer.metadata()[patchy::kLayerMetadataSmartObject] = "linked-source-uuid";
  layer.metadata()[patchy::kLayerMetadataSmartObjectLock] = "external";

  const auto text = patchy::ui::compatibility_warnings_for_document(document).join(QLatin1Char('\n'));
  CHECK(text.contains(QStringLiteral("linked to an external file")));
  CHECK(text.contains(QStringLiteral("can update it from disk when the source file is available")));
  CHECK(!text.contains(QStringLiteral("cannot update it from disk")));
}

void ui_psd_import_notice_reports_unrendered_layer_effects() {
  SettingsValueRestorer restore_psd_warning_check(QStringLiteral("imports/showPsdWarningsAndInfo"));
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("imports/showPsdWarningsAndInfo"));
    settings.sync();
  }

  patchy::Document document(32, 24, patchy::PixelFormat::rgba8());
  patchy::Layer root(document.allocate_layer_id(), "Root Satin",
                     solid_pixels(16, 12, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::LayerSatin root_satin;
  root_satin.enabled = true;
  root.layer_style().satins.push_back(root_satin);
  document.add_layer(std::move(root));

  patchy::Layer group(document.allocate_layer_id(), "Effects Group", patchy::LayerKind::Group);
  patchy::LayerSatin group_satin;
  group_satin.enabled = true;
  group.layer_style().satins.push_back(group_satin);
  patchy::Layer child(document.allocate_layer_id(), "Nested Effects",
                      solid_pixels(16, 12, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::LayerSatin nested_satin;
  nested_satin.enabled = true;
  child.layer_style().satins.push_back(nested_satin);
  patchy::LayerPatternOverlay pattern;
  pattern.enabled = true;
  pattern.pattern_name = "Test Pattern";
  pattern.pattern_id = "test-pattern";
  child.layer_style().pattern_overlays.push_back(pattern);
  pattern.enabled = false;
  child.layer_style().pattern_overlays.push_back(pattern);
  group.add_child(std::move(child));
  document.add_layer(std::move(group));

  QTemporaryDir temp;
  CHECK(temp.isValid());
  const auto path = temp.filePath(QStringLiteral("unrendered-effects.psd"));
  patchy::psd::DocumentIo::write_layered_rgb8_file(document, path.toStdString());

  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();
  const auto notice = window.statusBar()->currentMessage();
  CHECK(notice.contains(QStringLiteral(
      "Patchy preserved group layer effects for PSD round-trip but does not render them yet (groups: 1).")));
  CHECK(!notice.contains(QStringLiteral("Pattern Overlay")));
}

void ui_psd_import_warning_dialog_is_hidden_by_default() {
  SettingsValueRestorer restore_psd_warning_check(QStringLiteral("imports/showPsdWarningsAndInfo"));
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("imports/showPsdWarningsAndInfo"));
    settings.sync();
  }
  const auto psd_path = write_psd_import_warning_fixture(QStringLiteral("psd-import-warning-default.psd"));

  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(tabs != nullptr);
  const auto original_tab_count = tabs->count();

  const auto compatibility_report_done = std::make_shared<bool>(false);
  accept_compatibility_report_when_present(compatibility_report_done);
  patchy::ui::MainWindowTestAccess::open_document_path(window, psd_path);
  const bool saw_compatibility_report = *compatibility_report_done;
  *compatibility_report_done = true;
  QApplication::processEvents();

  CHECK(!saw_compatibility_report);
  CHECK(tabs->count() == original_tab_count + 1);
  CHECK(tabs->tabText(tabs->currentIndex()) == QFileInfo(psd_path).fileName());
}

void ui_psd_import_warning_dialog_shows_when_enabled() {
  SettingsValueRestorer restore_psd_warning_check(QStringLiteral("imports/showPsdWarningsAndInfo"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("imports/showPsdWarningsAndInfo"), true);
    settings.sync();
  }
  const auto psd_path = write_psd_import_warning_fixture(QStringLiteral("psd-import-warning-enabled.psd"));

  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(tabs != nullptr);
  const auto original_tab_count = tabs->count();

  const auto compatibility_report_done = std::make_shared<bool>(false);
  accept_compatibility_report_when_present(compatibility_report_done);
  patchy::ui::MainWindowTestAccess::open_document_path(window, psd_path);
  const bool saw_compatibility_report = *compatibility_report_done;
  *compatibility_report_done = true;
  QApplication::processEvents();

  CHECK(saw_compatibility_report);
  CHECK(tabs->count() == original_tab_count + 1);
  CHECK(tabs->tabText(tabs->currentIndex()) == QFileInfo(psd_path).fileName());
}

void ui_alt_left_click_samples_foreground_color() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(12, 180, 240));
  use_solid_fill_settings(canvas);
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();

  const QPoint sample_document_point(80, 80);
  const auto sampled_before_click = canvas_pixel(*canvas, sample_document_point);
  CHECK(color_close(sampled_before_click, QColor(12, 180, 240), 4));

  canvas->set_primary_color(QColor(230, 20, 20));
  const auto sample_widget_point = canvas->widget_position_for_document_point(sample_document_point);
  send_mouse(*canvas, QEvent::MouseButtonPress, sample_widget_point, Qt::LeftButton, Qt::LeftButton,
             Qt::AltModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, sample_widget_point, Qt::LeftButton, Qt::NoButton,
             Qt::AltModifier);
  QApplication::processEvents();

  CHECK(color_close(canvas->primary_color(), QColor(12, 180, 240), 4));
  CHECK(color_close(canvas_pixel(*canvas, sample_document_point), sampled_before_click, 0));
}

void ui_alt_color_pick_shows_rgb_status_and_updates_open_color_panel() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(40, 160, 220));
  use_solid_fill_settings(canvas);
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();

  canvas->set_primary_color(QColor(Qt::black));
  auto* foreground_button = window.findChild<QPushButton*>(QStringLiteral("foregroundColorButton"));
  CHECK(foreground_button != nullptr);
  foreground_button->click();
  QApplication::processEvents();

  auto* color_dialog = find_top_level_dialog(QStringLiteral("patchyColorDialog"));
  CHECK(color_dialog != nullptr);
  CHECK(color_dialog->isVisible());
  auto* picker =
      color_dialog->findChild<patchy::ui::PatchyColorPicker*>(QStringLiteral("patchyAdvancedColorPicker"));
  CHECK(picker != nullptr);
  CHECK(picker->currentColor() == QColor(Qt::black));

  const QPoint sample_document_point(80, 80);
  const auto sample_widget_point = canvas->widget_position_for_document_point(sample_document_point);
  send_mouse(*canvas, QEvent::MouseButtonPress, sample_widget_point, Qt::LeftButton, Qt::LeftButton,
             Qt::AltModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, sample_widget_point, Qt::LeftButton, Qt::NoButton,
             Qt::AltModifier);
  QApplication::processEvents();

  const auto picked = canvas->primary_color();
  CHECK(color_close(picked, QColor(40, 160, 220), 4));
  const auto message = window.statusBar()->currentMessage();
  CHECK(message.contains(
      QStringLiteral("%1, %2, %3").arg(picked.red()).arg(picked.green()).arg(picked.blue())));
  CHECK(message.contains(picked.name(QColor::HexRgb).toUpper()));
  CHECK(!message.contains(QStringLiteral("Foreground color changed")));
  CHECK(picker->currentColor() == picked);

  color_dialog->close();
  QApplication::processEvents();
}

void ui_eyedropper_starts_in_gray_area_and_drags_to_document_color() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(34, 201, 145));
  use_solid_fill_settings(canvas);
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();

  const auto grey_start = canvas->widget_position_for_document_point(QPoint(-24, -24));
  const QPoint sample_document_point(80, 80);
  const auto sample_widget_point = canvas->widget_position_for_document_point(sample_document_point);
  CHECK(canvas->rect().contains(grey_start));
  CHECK(color_close(canvas_pixel(*canvas, sample_document_point), QColor(34, 201, 145), 4));

  canvas->set_primary_color(QColor(230, 20, 20));
  require_action_by_text(window, QStringLiteral("Pick"))->trigger();
  send_mouse(*canvas, QEvent::MouseButtonPress, grey_start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, sample_widget_point, Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, sample_widget_point, Qt::LeftButton, Qt::NoButton);

  CHECK(color_close(canvas->primary_color(), QColor(34, 201, 145), 4));
}

void ui_photoshop_shortcuts_are_registered() {
  patchy::ui::MainWindow window;
  show_window(window);

  CHECK(require_action_by_text(window, QStringLiteral("New"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_N));
  CHECK(require_action_by_text(window, QStringLiteral("Open..."))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_O));
  CHECK(require_action_by_text(window, QStringLiteral("Save"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_S));
  CHECK(require_action_by_text(window, QStringLiteral("Save As..."))->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
  CHECK(require_action_by_text(window, QStringLiteral("Close"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_W));
  CHECK(require_action_by_text(window, QStringLiteral("Close All"))->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_W));
  CHECK(require_action(window, "filePrintAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_P));
  CHECK(require_action(window, "filePageSetupAction") != nullptr);
  CHECK(require_action_by_text(window, QStringLiteral("Undo"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_Z));
  CHECK(require_action_by_text(window, QStringLiteral("Redo"))->shortcut() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z));
  CHECK(require_action_by_text(window, QStringLiteral("Redo"))->shortcuts().contains(QKeySequence(Qt::CTRL | Qt::Key_Y)));
  CHECK(require_action_by_text(window, QStringLiteral("Cut"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_X));
  CHECK(require_action_by_text(window, QStringLiteral("Copy"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_C));
  CHECK(require_action_by_text(window, QStringLiteral("Paste"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_V));
  CHECK(require_action_by_text(window, QStringLiteral("Free Transform..."))->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::Key_T));
  CHECK(require_action_by_text(window, QStringLiteral("Select All"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_A));
  CHECK(require_action_by_text(window, QStringLiteral("Clear Selection"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_D));
  CHECK(require_action_by_text(window, QStringLiteral("Reselect"))->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D));
  CHECK(require_action_by_text(window, QStringLiteral("Inverse"))->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I));
  CHECK(require_action(window, "selectExpandAction") != nullptr);
  CHECK(require_action(window, "selectContractAction") != nullptr);
  CHECK(require_action(window, "selectBorderAction") != nullptr);
  CHECK(require_action(window, "selectLayerTransparencyAction") != nullptr);
  CHECK(require_action(window, "selectGrowAction") != nullptr);
  CHECK(require_action(window, "selectSimilarAction") != nullptr);
  CHECK(require_action(window, "layerEditAdjustmentAction") != nullptr);
  CHECK(require_action_by_text(window, QStringLiteral("New Layer"))->shortcut() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N));
  CHECK(require_action_by_text(window, QStringLiteral("Layer Via Copy"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_J));
  CHECK(require_action_by_text(window, QStringLiteral("Layer Via Cut"))->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_J));
  CHECK(require_action_by_text(window, QStringLiteral("Duplicate Layer"))->shortcut().isEmpty());
  CHECK(require_action(window, "layerFillForegroundAction")->shortcut() == QKeySequence(Qt::ALT | Qt::Key_Backspace));
  CHECK(require_action(window, "layerFillBackgroundAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_Backspace));
  CHECK(require_action_by_text(window, QStringLiteral("Merge Down"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_E));
  CHECK(require_action_by_text(window, QStringLiteral("Merge Visible to New Layer"))->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));
  CHECK(require_action(window, "imageSizeAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_I));
  CHECK(require_action_by_text(window, QStringLiteral("Canvas Size..."))->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_C));
  CHECK(require_action(window, "imageAdjustInvertAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_I));
  CHECK(require_action(window, "imageAdjustLevelsAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_L));
  CHECK(require_action(window, "imageAdjustCurvesAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_M));
  CHECK(require_action(window, "imageAdjustHueSaturationAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_U));
  CHECK(require_action(window, "imageAdjustColorBalanceAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_B));
  CHECK(require_action(window, "imageAdjustDesaturateAction")->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_U));
  CHECK(require_action(window, "imageAdjustAutoContrastAction")->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_L));
  CHECK(require_action(window, "imageAdjustBrightnessContrastAction")->shortcut().isEmpty());
  CHECK(window.findChild<QAction*>(QStringLiteral("imageAdjustBrightnessAction")) == nullptr);
  CHECK(window.findChild<QAction*>(QStringLiteral("imageAdjustContrastAction")) == nullptr);
  CHECK(require_action(window, "viewToggleSelectionEdgesAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_H));
  CHECK(require_action(window, "viewToggleRulersAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_R));
  CHECK(require_action(window, "viewToggleGridAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_Apostrophe));
  CHECK(require_action(window, "viewToggleGuidesAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_Semicolon));
  CHECK(require_action(window, "viewToggleSnapAction")->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Semicolon));
  CHECK(require_action(window, "viewLockGuidesAction")->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Semicolon));
  CHECK(require_action(window, "viewSnapToGuidesAction")->isCheckable());
  CHECK(require_action(window, "viewSnapToGridAction")->isCheckable());
  CHECK(require_action(window, "viewSnapToDocumentAction")->isCheckable());
  CHECK(require_action(window, "viewSnapToLayersAction")->isCheckable());
  CHECK(require_action(window, "viewSnapToSelectionAction")->isCheckable());
  CHECK(require_action(window, "viewNewGuideAction") != nullptr);
  CHECK(require_action(window, "viewNewGuideLayoutAction") != nullptr);
  CHECK(require_action_by_text(window, QStringLiteral("Default Colors"))->shortcut() == QKeySequence(Qt::Key_D));
  CHECK(require_action_by_text(window, QStringLiteral("Swap Colors"))->shortcut() == QKeySequence(Qt::Key_X));
  CHECK(require_action_by_text(window, QStringLiteral("Move"))->shortcut() == QKeySequence(Qt::Key_V));
  CHECK(require_action_by_text(window, QStringLiteral("Marquee"))->shortcut() == QKeySequence(Qt::Key_M));
  CHECK(require_action_by_text(window, QStringLiteral("Elliptical Marquee"))->shortcut() ==
        QKeySequence(Qt::SHIFT | Qt::Key_M));
  CHECK(require_action_by_text(window, QStringLiteral("Lasso"))->shortcut() == QKeySequence(Qt::Key_L));
  CHECK(require_action_by_text(window, QStringLiteral("Magic Wand"))->shortcut() == QKeySequence(Qt::Key_W));
  CHECK(require_action_by_text(window, QStringLiteral("Brush"))->shortcut() == QKeySequence(Qt::Key_B));
  CHECK(require_action_by_text(window, QStringLiteral("Clone"))->shortcut() == QKeySequence(Qt::Key_S));
  CHECK(require_action_by_text(window, QStringLiteral("Healing Brush"))->shortcut() ==
        QKeySequence(Qt::SHIFT | Qt::Key_S));
  CHECK(require_action_by_text(window, QStringLiteral("Smudge"))->shortcut() == QKeySequence(Qt::Key_R));
  CHECK(require_action(window, "toolBlurAction")->shortcut() == QKeySequence(Qt::SHIFT | Qt::Key_R));
  CHECK(require_action(window, "toolSharpenAction")->shortcut().isEmpty());
  CHECK(require_action(window, "toolDodgeAction")->shortcut() == QKeySequence(Qt::Key_O));
  CHECK(require_action(window, "toolBurnAction")->shortcut() == QKeySequence(Qt::SHIFT | Qt::Key_O));
  CHECK(require_action(window, "toolSpongeAction")->shortcut().isEmpty());
  CHECK(require_action_by_text(window, QStringLiteral("Eraser"))->shortcut() == QKeySequence(Qt::Key_E));
  CHECK(require_action_by_text(window, QStringLiteral("Gradient"))->shortcut() == QKeySequence(Qt::Key_G));
  CHECK(require_action_by_text(window, QStringLiteral("Fill"))->shortcut() == QKeySequence(Qt::SHIFT | Qt::Key_G));
  CHECK(require_action_by_text(window, QStringLiteral("Rect"))->shortcut() == QKeySequence(Qt::Key_U));
  // Line ships unbound: its old Ctrl+Shift+U default collided with Desaturate, so neither fired.
  CHECK(require_action_by_text(window, QStringLiteral("Line"))->shortcut().isEmpty());
  CHECK(require_action_by_text(window, QStringLiteral("Ellipse"))->shortcut() ==
        QKeySequence(Qt::SHIFT | Qt::Key_U));
  CHECK(require_action_by_text(window, QStringLiteral("Pick"))->shortcut() == QKeySequence(Qt::Key_I));
  CHECK(require_action_by_text(window, QStringLiteral("Type"))->shortcut() == QKeySequence(Qt::Key_T));
  CHECK(require_action_by_text(window, QStringLiteral("Hand"))->shortcut() == QKeySequence(Qt::Key_H));
  CHECK(require_action_by_text(window, QStringLiteral("Zoom"))->shortcut() == QKeySequence(Qt::Key_Z));
  CHECK(require_action(window, "brushSmallerAction")->shortcut() == QKeySequence(Qt::Key_BracketLeft));
  CHECK(require_action(window, "brushLargerAction")->shortcut() == QKeySequence(Qt::Key_BracketRight));
  CHECK(require_action(window, "brushMuchSmallerAction")->shortcut() ==
        QKeySequence(Qt::SHIFT | Qt::Key_BracketLeft));
  CHECK(require_action(window, "brushMuchLargerAction")->shortcut() ==
        QKeySequence(Qt::SHIFT | Qt::Key_BracketRight));
  const auto tooltip_matches_shortcut = [](QAction* action) {
    const auto shortcut = action->shortcut().toString(QKeySequence::NativeText);
    CHECK(!shortcut.isEmpty());
    CHECK(action->toolTip().contains(action->text().remove('&')));
    CHECK(action->toolTip().contains(shortcut));
  };
  tooltip_matches_shortcut(require_action_by_text(window, QStringLiteral("Move")));
  tooltip_matches_shortcut(require_action_by_text(window, QStringLiteral("Brush")));
  tooltip_matches_shortcut(require_action_by_text(window, QStringLiteral("Smudge")));
  tooltip_matches_shortcut(require_action(window, "toolBlurAction"));
  tooltip_matches_shortcut(require_action(window, "toolDodgeAction"));
  tooltip_matches_shortcut(require_action(window, "toolBurnAction"));
  tooltip_matches_shortcut(require_action_by_text(window, QStringLiteral("Clone")));
  tooltip_matches_shortcut(require_action_by_text(window, QStringLiteral("Healing Brush")));
  tooltip_matches_shortcut(require_action_by_text(window, QStringLiteral("Type")));
  tooltip_matches_shortcut(require_action_by_text(window, QStringLiteral("Cut")));
  tooltip_matches_shortcut(require_action_by_text(window, QStringLiteral("Default Colors")));
  tooltip_matches_shortcut(require_action_by_text(window, QStringLiteral("Swap Colors")));
  tooltip_matches_shortcut(require_action(window, "brushLargerAction"));

  auto* canvas = require_canvas(window);
  auto* blend_combo = window.findChild<QComboBox*>(QStringLiteral("layerBlendModeCombo"));
  CHECK(blend_combo != nullptr);
  CHECK(blend_combo->findText(QStringLiteral("Difference")) >= 0);
  CHECK(blend_combo->findText(QStringLiteral("Color Dodge")) >= 0);
  CHECK(blend_combo->findText(QStringLiteral("Pin Light")) >= 0);
  CHECK(blend_combo->findText(QStringLiteral("Luminosity")) >= 0);
  CHECK(window.findChild<QToolButton*>(QStringLiteral("layerLockTransparentButton")) != nullptr);
  CHECK(window.findChild<QToolButton*>(QStringLiteral("layerLockPixelsButton")) != nullptr);
  CHECK(window.findChild<QToolButton*>(QStringLiteral("layerLockPositionButton")) != nullptr);
  CHECK(window.findChild<QToolButton*>(QStringLiteral("layerLockAllButton")) != nullptr);
  CHECK(require_action(window, "selectionNewModeAction")->isCheckable());
  CHECK(require_action(window, "selectionAddModeAction")->isCheckable());
  CHECK(require_action(window, "selectionSubtractModeAction")->isCheckable());
  CHECK(require_action(window, "selectionIntersectModeAction")->isCheckable());
  auto* brush_size = window.findChild<QSpinBox*>(QStringLiteral("brushSizeSpin"));
  auto* brush_size_slider = window.findChild<QSlider*>(QStringLiteral("brushSizeSlider"));
  auto* brush_opacity = window.findChild<QSpinBox*>(QStringLiteral("brushOpacitySpin"));
  auto* brush_opacity_slider = window.findChild<QSlider*>(QStringLiteral("brushOpacitySlider"));
  auto* brush_flow = window.findChild<QSpinBox*>(QStringLiteral("brushFlowSpin"));
  auto* brush_airbrush = window.findChild<QCheckBox*>(QStringLiteral("brushAirbrushCheck"));
  auto* brush_softness = window.findChild<QSpinBox*>(QStringLiteral("brushSoftnessSpin"));
  auto* brush_softness_slider = window.findChild<QSlider*>(QStringLiteral("brushSoftnessSlider"));
  auto* brush_preset = window.findChild<QComboBox*>(QStringLiteral("brushPresetCombo"));
  CHECK(brush_size != nullptr);
  CHECK(brush_size_slider != nullptr);
  CHECK(brush_opacity != nullptr);
  CHECK(brush_opacity_slider != nullptr);
  CHECK(brush_flow != nullptr);
  CHECK(brush_airbrush != nullptr);
  CHECK(brush_softness != nullptr);
  CHECK(brush_softness_slider != nullptr);
  CHECK(brush_preset != nullptr);
  CHECK(brush_size->maximum() == patchy::ui::kMaxBrushSize);
  CHECK(brush_size_slider->maximum() == patchy::ui::kMaxBrushSize);
  CHECK(brush_size->buttonSymbols() == QAbstractSpinBox::NoButtons);
  CHECK(brush_opacity->buttonSymbols() == QAbstractSpinBox::NoButtons);
  CHECK(brush_flow->buttonSymbols() == QAbstractSpinBox::NoButtons);
  CHECK(brush_softness->buttonSymbols() == QAbstractSpinBox::NoButtons);
  CHECK(brush_preset->currentData().toString() == QStringLiteral("round"));
  CHECK(brush_size->value() == 25);
  CHECK(brush_opacity->value() == 100);
  CHECK(brush_flow->value() == 100);
  CHECK(!brush_airbrush->isChecked());
  CHECK(brush_softness->value() == 0);
  CHECK(brush_softness_slider->value() == 0);
  CHECK(canvas->brush_size() == 25);
  CHECK(canvas->brush_opacity() == 100);
  CHECK(canvas->brush_flow() == 100);
  CHECK(canvas->brush_softness() == 0);
  const auto airbrush_index = brush_preset->findData(QStringLiteral("airbrush"));
  CHECK(airbrush_index >= 0);
  brush_preset->setCurrentIndex(airbrush_index);
  CHECK(brush_size->value() == 56);
  CHECK(brush_opacity->value() == 100);
  CHECK(brush_flow->value() == 12);
  CHECK(brush_softness->value() == 100);
  CHECK(canvas->brush_build_up());
  CHECK(brush_airbrush->isChecked());
  const auto soft_round_index = brush_preset->findData(QStringLiteral("soft_round"));
  CHECK(soft_round_index >= 0);
  brush_preset->setCurrentIndex(soft_round_index);
  CHECK(!canvas->brush_build_up());
  CHECK(brush_flow->value() == 100);
  CHECK(!brush_airbrush->isChecked());
  // Bracket keys resize proportionally (Photoshop-like): the step scales with
  // the current size, so at 20 px the plain step is +2 (10%) and Shift is +6
  // (30%), and grow-then-shrink returns to the same size.
  brush_size->setValue(20);
  CHECK(brush_size_slider->value() == 20);
  require_action(window, "brushLargerAction")->trigger();
  CHECK(brush_size->value() == 22);
  CHECK(brush_size_slider->value() == 22);
  CHECK(canvas->brush_size() == 22);
  require_action(window, "brushSmallerAction")->trigger();
  CHECK(brush_size->value() == 20);
  require_action(window, "brushMuchLargerAction")->trigger();
  CHECK(brush_size->value() == 26);
  CHECK(canvas->brush_size() == 26);
  require_action(window, "brushMuchSmallerAction")->trigger();
  CHECK(brush_size->value() == 20);
  // Small brushes keep 1-px precision; large brushes jump proportionally.
  brush_size->setValue(5);
  require_action(window, "brushLargerAction")->trigger();
  CHECK(brush_size->value() == 6);
  brush_size->setValue(100);
  require_action(window, "brushLargerAction")->trigger();
  CHECK(brush_size->value() == 110);
  require_action(window, "brushSmallerAction")->trigger();
  CHECK(brush_size->value() == 100);
  require_action(window, "brushMuchLargerAction")->trigger();
  CHECK(brush_size->value() == 130);
  require_action(window, "brushMuchSmallerAction")->trigger();
  CHECK(brush_size->value() == 100);
  brush_size->setValue(patchy::ui::kMaxBrushSize);
  CHECK(brush_size_slider->value() == patchy::ui::kMaxBrushSize);
  CHECK(canvas->brush_size() == patchy::ui::kMaxBrushSize);
  require_action(window, "brushLargerAction")->trigger();
  CHECK(brush_size->value() == patchy::ui::kMaxBrushSize);
  brush_size->setValue(patchy::ui::kMaxBrushSize - 5);
  require_action(window, "brushMuchLargerAction")->trigger();
  CHECK(brush_size->value() == patchy::ui::kMaxBrushSize);
  CHECK(canvas->brush_size() == patchy::ui::kMaxBrushSize);
  brush_size->setValue(20);
  brush_opacity_slider->setValue(45);
  CHECK(brush_opacity->value() == 45);
  CHECK(canvas->brush_opacity() == 45);
  brush_softness_slider->setValue(65);
  CHECK(brush_softness->value() == 65);
  CHECK(canvas->brush_softness() == 65);
  auto settings = patchy::ui::app_settings();
  settings.remove(QStringLiteral("tools"));
  settings.sync();
}

// Snapshots and restores the whole "hotkeys" settings group so hotkey tests
// cannot leak overrides into each other or into a developer's settings.
class HotkeySettingsGroupRestorer {
public:
  HotkeySettingsGroupRestorer() {
    auto settings = patchy::ui::app_settings();
    settings.beginGroup(QStringLiteral("hotkeys"));
    const auto keys = settings.childKeys();
    for (const auto& key : keys) {
      saved_.insert(key, settings.value(key));
    }
    settings.endGroup();
  }

  ~HotkeySettingsGroupRestorer() {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("hotkeys"));
    settings.beginGroup(QStringLiteral("hotkeys"));
    for (auto it = saved_.constBegin(); it != saved_.constEnd(); ++it) {
      settings.setValue(it.key(), it.value());
    }
    settings.endGroup();
    settings.sync();
  }

private:
  QHash<QString, QVariant> saved_;
};

void clear_hotkey_overrides() {
  auto settings = patchy::ui::app_settings();
  settings.remove(QStringLiteral("hotkeys"));
  settings.sync();
}

void ui_hotkey_resolution_rules() {
  using patchy::ui::HotkeyCommand;
  using patchy::ui::HotkeyOverrideMap;
  using patchy::ui::resolve_hotkey_assignments;

  std::vector<HotkeyCommand> commands;
  commands.push_back({QStringLiteral("first"), QString(), nullptr, {QKeySequence(QStringLiteral("Ctrl+K"))}});
  commands.push_back({QStringLiteral("second"), QString(), nullptr, {QKeySequence(QStringLiteral("Ctrl+L"))}});
  commands.push_back({QStringLiteral("late_duplicate"), QString(), nullptr, {QKeySequence(QStringLiteral("Ctrl+K"))}});

  {
    const auto resolved = resolve_hotkey_assignments(commands, {});
    CHECK(resolved.effective.value(QStringLiteral("first")) ==
          QList<QKeySequence>{QKeySequence(QStringLiteral("Ctrl+K"))});
    CHECK(resolved.effective.value(QStringLiteral("second")) ==
          QList<QKeySequence>{QKeySequence(QStringLiteral("Ctrl+L"))});
    // Duplicate defaults resolve deterministically: first registered wins.
    CHECK(resolved.effective.value(QStringLiteral("late_duplicate")).isEmpty());
    CHECK(resolved.suppressions.size() == 1);
    CHECK(resolved.suppressions[0].id == QStringLiteral("late_duplicate"));
    CHECK(resolved.suppressions[0].winner_id == QStringLiteral("first"));
  }
  {
    // A user override beats another command's default, and the loser is only
    // suppressed at resolution time, never rewritten.
    HotkeyOverrideMap overrides;
    overrides.insert(QStringLiteral("second"), {QKeySequence(QStringLiteral("Ctrl+K"))});
    const auto resolved = resolve_hotkey_assignments(commands, overrides);
    CHECK(resolved.effective.value(QStringLiteral("second")) ==
          QList<QKeySequence>{QKeySequence(QStringLiteral("Ctrl+K"))});
    CHECK(resolved.effective.value(QStringLiteral("first")).isEmpty());
    bool first_suppressed_by_second = false;
    for (const auto& suppression : resolved.suppressions) {
      if (suppression.id == QStringLiteral("first") && suppression.winner_id == QStringLiteral("second")) {
        first_suppressed_by_second = true;
      }
    }
    CHECK(first_suppressed_by_second);
  }
  {
    // An empty override means explicitly unbound.
    HotkeyOverrideMap overrides;
    overrides.insert(QStringLiteral("first"), {});
    const auto resolved = resolve_hotkey_assignments(commands, overrides);
    CHECK(resolved.effective.value(QStringLiteral("first")).isEmpty());
    // With Ctrl+K free again, the late duplicate default gets it.
    CHECK(resolved.effective.value(QStringLiteral("late_duplicate")) ==
          QList<QKeySequence>{QKeySequence(QStringLiteral("Ctrl+K"))});
  }
  {
    // Storage round-trips sequences whose text contains separators (Ctrl+;).
    const QList<QKeySequence> shortcuts{QKeySequence(QStringLiteral("Ctrl+;")),
                                        QKeySequence(QStringLiteral("Ctrl+Shift+Z"))};
    const auto stored = patchy::ui::hotkey_shortcuts_to_storage(shortcuts);
    CHECK(patchy::ui::hotkey_shortcuts_from_storage(stored) == shortcuts);
    CHECK(patchy::ui::hotkey_shortcuts_from_storage(QString()).isEmpty());
    CHECK(patchy::ui::hotkey_shortcuts_to_storage({}).isEmpty());
  }
}

void ui_hotkey_defaults_have_no_conflicts() {
  HotkeySettingsGroupRestorer restore_hotkeys;
  clear_hotkey_overrides();
  patchy::ui::MainWindow window;
  const auto& registry = window.hotkey_registry();
  CHECK(registry.commands().size() > 80);
  QSet<QString> ids;
  for (const auto& command : registry.commands()) {
    CHECK(!command.id.isEmpty());
    CHECK(!ids.contains(command.id));
    ids.insert(command.id);
  }
  // No two default shortcuts may collide: Qt treats ambiguous application
  // shortcuts as dead keys (this regressed once via Desaturate vs Line).
  const auto resolved = patchy::ui::resolve_hotkey_assignments(registry.commands(), {});
  for (const auto& suppression : resolved.suppressions) {
    std::fprintf(stderr, "duplicate default shortcut: %s on %s and %s\n",
                 suppression.sequence.toString(QKeySequence::PortableText).toUtf8().constData(),
                 suppression.id.toUtf8().constData(), suppression.winner_id.toUtf8().constData());
  }
  CHECK(resolved.suppressions.empty());
}

void ui_hotkey_override_applies_at_startup() {
  HotkeySettingsGroupRestorer restore_hotkeys;
  clear_hotkey_overrides();
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("hotkeys/file.new"), QStringLiteral("Ctrl+F9"));
    settings.sync();
  }
  {
    patchy::ui::MainWindow window;
    auto* new_action = require_action(window, "fileNewAction");
    CHECK(new_action->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_F9));
    CHECK(new_action->toolTip().contains(
        QKeySequence(Qt::CTRL | Qt::Key_F9).toString(QKeySequence::NativeText)));
  }
  clear_hotkey_overrides();
  {
    // Unmodified commands track the shipped defaults again.
    patchy::ui::MainWindow window;
    CHECK(require_action(window, "fileNewAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_N));
    const auto* undo_command = window.hotkey_registry().find_command(QStringLiteral("edit.undo"));
    CHECK(undo_command != nullptr);
    CHECK(undo_command->action != nullptr);
    CHECK(undo_command->action->shortcuts().size() == 2);
  }
}

void ui_hotkey_editor_assigns_and_persists_custom_shortcut() {
  HotkeySettingsGroupRestorer restore_hotkeys;
  clear_hotkey_overrides();
  patchy::ui::MainWindow window;
  show_window(window);

  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    auto* tabs = dialog->findChild<QTabWidget*>(QStringLiteral("preferencesTabWidget"));
    CHECK(tabs != nullptr);
    tabs->setCurrentIndex(tabs->count() - 1);
    QApplication::processEvents();
    CHECK(dialog->findChild<QWidget*>(QStringLiteral("hotkeyEditorPanel")) != nullptr);
    save_widget_artifact("hotkey_editor_tab", *dialog);

    auto* chip = dialog->findChild<QPushButton*>(QStringLiteral("hotkeyChip.file.new.0"));
    CHECK(chip != nullptr);
    CHECK(chip->text() == QKeySequence(Qt::CTRL | Qt::Key_N).toString(QKeySequence::NativeText));
    chip->click();
    QApplication::processEvents();
    auto* capture = dialog->findChild<QLineEdit*>(QStringLiteral("hotkeyCaptureEdit"));
    CHECK(capture != nullptr);
    QKeyEvent press(QEvent::KeyPress, Qt::Key_F9, Qt::ControlModifier);
    QApplication::sendEvent(capture, &press);
    QApplication::processEvents();
    QApplication::processEvents();

    // The row is rebuilt with the staged shortcut, a modified marker, and a
    // visible per-row reset button.
    auto* updated_chip = dialog->findChild<QPushButton*>(QStringLiteral("hotkeyChip.file.new.0"));
    CHECK(updated_chip != nullptr);
    CHECK(updated_chip->text() == QKeySequence(Qt::CTRL | Qt::Key_F9).toString(QKeySequence::NativeText));
    auto* reset_button = dialog->findChild<QToolButton*>(QStringLiteral("hotkeyRowReset.file.new"));
    CHECK(reset_button != nullptr);
    CHECK(reset_button->isVisible());

    // Filtering by shortcut text keeps the matching row visible.
    auto* search = dialog->findChild<QLineEdit*>(QStringLiteral("hotkeySearchEdit"));
    CHECK(search != nullptr);
    search->setText(QStringLiteral("ctrl+f9"));
    QApplication::processEvents();
    auto* row = dialog->findChild<QWidget*>(QStringLiteral("hotkeyRow.file.new"));
    CHECK(row != nullptr);
    CHECK(row->isVisible());
    auto* other_row = dialog->findChild<QWidget*>(QStringLiteral("hotkeyRow.file.open"));
    CHECK(other_row != nullptr);
    CHECK(!other_row->isVisible());
    search->clear();
    QApplication::processEvents();

    saw_dialog = true;
    dialog->accept();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);

  auto settings = patchy::ui::app_settings();
  settings.beginGroup(QStringLiteral("hotkeys"));
  const auto keys = settings.childKeys();
  const auto stored = settings.value(QStringLiteral("file.new")).toString();
  settings.endGroup();
  CHECK(keys == QStringList{QStringLiteral("file.new")});
  CHECK(stored == QKeySequence(Qt::CTRL | Qt::Key_F9).toString(QKeySequence::PortableText));
  CHECK(require_action(window, "fileNewAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_F9));
}

void ui_hotkey_editor_steals_conflicting_shortcut() {
  HotkeySettingsGroupRestorer restore_hotkeys;
  clear_hotkey_overrides();
  patchy::ui::MainWindow window;
  show_window(window);

  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    auto* tabs = dialog->findChild<QTabWidget*>(QStringLiteral("preferencesTabWidget"));
    CHECK(tabs != nullptr);
    tabs->setCurrentIndex(tabs->count() - 1);
    QApplication::processEvents();

    // The Line tool ships unbound, so it renders an assign chip.
    auto* assign_chip = dialog->findChild<QPushButton*>(QStringLiteral("hotkeyAssignChip.tools.line"));
    CHECK(assign_chip != nullptr);
    assign_chip->click();
    QApplication::processEvents();
    auto* capture = dialog->findChild<QLineEdit*>(QStringLiteral("hotkeyCaptureEdit"));
    CHECK(capture != nullptr);
    QKeyEvent press(QEvent::KeyPress, Qt::Key_L, Qt::ControlModifier);
    QApplication::sendEvent(capture, &press);
    QApplication::processEvents();
    QApplication::processEvents();

    // Ctrl+L belongs to Levels, so a confirmation banner appears instead of a
    // silent reassignment.
    auto* banner = dialog->findChild<QFrame*>(QStringLiteral("hotkeyConflictBanner"));
    CHECK(banner != nullptr);
    save_widget_artifact("hotkey_editor_conflict_banner", *dialog);
    auto* take_button = dialog->findChild<QPushButton*>(QStringLiteral("hotkeyConflictAssignButton"));
    CHECK(take_button != nullptr);
    take_button->click();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* line_chip = dialog->findChild<QPushButton*>(QStringLiteral("hotkeyChip.tools.line.0"));
    CHECK(line_chip != nullptr);
    CHECK(line_chip->text() == QKeySequence(Qt::CTRL | Qt::Key_L).toString(QKeySequence::NativeText));
    // Levels falls back to unbound at runtime, with a note explaining why; its
    // stored settings stay untouched so a future default change still applies.
    CHECK(dialog->findChild<QPushButton*>(QStringLiteral("hotkeyAssignChip.image.levels")) != nullptr);
    auto* note = dialog->findChild<QLabel*>(QStringLiteral("hotkeyRowNote.image.levels"));
    CHECK(note != nullptr);
    CHECK(note->isVisible());

    saw_dialog = true;
    dialog->accept();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);

  auto settings = patchy::ui::app_settings();
  settings.beginGroup(QStringLiteral("hotkeys"));
  const auto keys = settings.childKeys();
  settings.endGroup();
  CHECK(keys == QStringList{QStringLiteral("tools.line")});
  CHECK(require_action(window, "toolLineAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_L));
  CHECK(require_action(window, "imageAdjustLevelsAction")->shortcut().isEmpty());
}

void ui_hotkey_editor_reset_all_clears_overrides() {
  HotkeySettingsGroupRestorer restore_hotkeys;
  clear_hotkey_overrides();
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("hotkeys/file.new"), QStringLiteral("Ctrl+F9"));
    settings.setValue(QStringLiteral("hotkeys/edit.undo"), QString());
    settings.sync();
  }
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(require_action(window, "fileNewAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_F9));
  const auto* undo_command = window.hotkey_registry().find_command(QStringLiteral("edit.undo"));
  CHECK(undo_command != nullptr && undo_command->action != nullptr);
  CHECK(undo_command->action->shortcuts().isEmpty());

  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    auto* tabs = dialog->findChild<QTabWidget*>(QStringLiteral("preferencesTabWidget"));
    CHECK(tabs != nullptr);
    tabs->setCurrentIndex(tabs->count() - 1);
    QApplication::processEvents();
    auto* reset_all = dialog->findChild<QPushButton*>(QStringLiteral("hotkeyResetAllButton"));
    CHECK(reset_all != nullptr);
    reset_all->click();
    QApplication::processEvents();
    QApplication::processEvents();
    saw_dialog = true;
    dialog->accept();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);

  auto settings = patchy::ui::app_settings();
  settings.beginGroup(QStringLiteral("hotkeys"));
  CHECK(settings.childKeys().isEmpty());
  settings.endGroup();
  CHECK(require_action(window, "fileNewAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_N));
  CHECK(undo_command->action->shortcuts().size() == 2);
}

}  // namespace

std::vector<patchy::test::TestCase> pickers_notices_hotkeys_tests() {
  return {
      {"ui_color_picker_changes_foreground_color", ui_color_picker_changes_foreground_color},
      {"ui_color_picker_ignores_reentrant_requests", ui_color_picker_ignores_reentrant_requests},
      {"ui_color_picker_closes_with_parent_dialog", ui_color_picker_closes_with_parent_dialog},
      {"ui_color_picker_wheel_and_sliders_modes", ui_color_picker_wheel_and_sliders_modes},
      {"ui_dialog_position_memory_restores_last_position", ui_dialog_position_memory_restores_last_position},
      {"ui_dialog_position_memory_centers_unmoved_dialogs_on_parent",
       ui_dialog_position_memory_centers_unmoved_dialogs_on_parent},
      {"ui_dirty_state_marks_tabs_and_undo_restores_saved_revision",
       ui_dirty_state_marks_tabs_and_undo_restores_saved_revision},
      {"ui_compatibility_report_flags_psd_text_placeholders",
       ui_compatibility_report_flags_psd_text_placeholders},
      {"ui_compatibility_report_ignores_patchy_written_psd_blocks",
       ui_compatibility_report_ignores_patchy_written_psd_blocks},
      {"ui_compatibility_report_treats_levels_as_native_psd_adjustment",
       ui_compatibility_report_treats_levels_as_native_psd_adjustment},
      {"ui_compatibility_report_flags_cmyk_rgb_conversion",
       ui_compatibility_report_flags_cmyk_rgb_conversion},
      {"ui_compatibility_report_flags_unrendered_styles_on_groups",
       ui_compatibility_report_flags_unrendered_styles_on_groups},
      {"ui_compatibility_report_handles_supported_unsupported_and_boundary_blend_if",
       ui_compatibility_report_handles_supported_unsupported_and_boundary_blend_if},
      {"ui_compatibility_report_describes_linked_smart_object_updates",
       ui_compatibility_report_describes_linked_smart_object_updates},
      {"ui_psd_import_notice_reports_unrendered_layer_effects",
       ui_psd_import_notice_reports_unrendered_layer_effects},
      {"ui_psd_import_notice_reports_only_unsupported_blend_if",
       ui_psd_import_notice_reports_only_unsupported_blend_if},
      {"ui_psd_import_warning_dialog_is_hidden_by_default",
       ui_psd_import_warning_dialog_is_hidden_by_default},
      {"ui_psd_import_warning_dialog_shows_when_enabled",
       ui_psd_import_warning_dialog_shows_when_enabled},
      {"ui_alt_left_click_samples_foreground_color", ui_alt_left_click_samples_foreground_color},
      {"ui_alt_color_pick_shows_rgb_status_and_updates_open_color_panel",
       ui_alt_color_pick_shows_rgb_status_and_updates_open_color_panel},
      {"ui_eyedropper_starts_in_gray_area_and_drags_to_document_color",
       ui_eyedropper_starts_in_gray_area_and_drags_to_document_color},
      {"ui_photoshop_shortcuts_are_registered", ui_photoshop_shortcuts_are_registered},
      {"ui_hotkey_resolution_rules", ui_hotkey_resolution_rules},
      {"ui_hotkey_defaults_have_no_conflicts", ui_hotkey_defaults_have_no_conflicts},
      {"ui_hotkey_override_applies_at_startup", ui_hotkey_override_applies_at_startup},
      {"ui_hotkey_editor_assigns_and_persists_custom_shortcut",
       ui_hotkey_editor_assigns_and_persists_custom_shortcut},
      {"ui_hotkey_editor_steals_conflicting_shortcut", ui_hotkey_editor_steals_conflicting_shortcut},
      {"ui_hotkey_editor_reset_all_clears_overrides", ui_hotkey_editor_reset_all_clears_overrides},
  };
}
