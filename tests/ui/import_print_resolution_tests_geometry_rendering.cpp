#include <QStatusBar>
#include "ui/canvas_widget.hpp"
#include "core/adjustment_layer.hpp"
#include "core/layer_metadata.hpp"
#include "ui/dialog_utils.hpp"
#include "formats/pdf_file.hpp"
#include "ui/image_document_io.hpp"
#include "ui/main_window.hpp"
#include "ui/app_settings.hpp"
#include "ui/script_engine.hpp"
#include "synthetic_dng.hpp"
#include "test_harness.hpp"
#include "local_psd_fixtures.hpp"
#include <QApplication>
#include <QBuffer>
#include <QByteArray>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QList>
#include <QListWidget>
#include <QMimeData>
#include <QIODevice>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
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
#include "import_print_resolution_test_support.hpp"

namespace {
using namespace patchy::test::ui;
using namespace patchy::test::ui::import_print_resolution_support;

// A 4 x 4 layer whose bounds sit at `origin`, added straight to the window's document.
patchy::LayerId add_far_layer(patchy::ui::MainWindow& window, const char* name, QPoint origin);

// Photoshop's dialog memory: Image Size keeps its W/H unit and its resolution unit
// across openings (`imageSize/lastUnit`, `imageSize/lastResolutionUnit`, written on
// accept only); a first run seeds the W/H unit from the ruler unit, and a token the
// combo cannot show falls back to Pixels.
// Replaces the window's document with a 2x2 black/white checker (opaque), the sharpest
// possible probe for the resampling method: Nearest Neighbor doubles it into hard 2x2
// blocks, every other method produces midtones at the block edges.
patchy::Document& load_checker_document(patchy::ui::MainWindow& window);

bool checker_doubled_with_hard_edges(const patchy::Document& document);

bool checker_resized_with_midtones(const patchy::Document& document);

patchy::LayerId add_far_layer(patchy::ui::MainWindow& window, const char* name, QPoint origin) {
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  patchy::Layer layer(document.allocate_layer_id(), name, patchy::PixelBuffer(4, 4, patchy::PixelFormat::rgba8()));
  layer.set_bounds(patchy::Rect{origin.x(), origin.y(), 4, 4});
  const auto id = layer.id();
  document.add_layer(std::move(layer));
  return id;
}

patchy::Document& load_checker_document(patchy::ui::MainWindow& window) {
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  document = patchy::Document(2, 2, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer checker(2, 2, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < 2; ++y) {
    for (std::int32_t x = 0; x < 2; ++x) {
      auto* pixel = checker.pixel(x, y);
      const std::uint8_t value = (x + y) % 2 == 0 ? 0 : 255;
      pixel[0] = pixel[1] = pixel[2] = value;
      pixel[3] = 255;
    }
  }
  document.add_pixel_layer("Checker", std::move(checker));
  patchy::ui::MainWindowTestAccess::canvas(window)->set_document(&document);
  return document;
}

bool checker_doubled_with_hard_edges(const patchy::Document& document) {
  if (document.width() != 4 || document.height() != 4 || document.layers().empty()) {
    return false;
  }
  const auto& pixels = std::as_const(document.layers().front()).pixels();
  for (std::int32_t y = 0; y < 4; ++y) {
    for (std::int32_t x = 0; x < 4; ++x) {
      const auto* pixel = pixels.pixel(x, y);
      const std::uint8_t expected = ((x / 2) + (y / 2)) % 2 == 0 ? 0 : 255;
      if (pixel[0] != expected || pixel[3] != 255) {
        return false;
      }
    }
  }
  return true;
}

bool checker_resized_with_midtones(const patchy::Document& document) {
  if (document.layers().empty()) {
    return false;
  }
  const auto& pixels = std::as_const(document.layers().front()).pixels();
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto value = pixels.pixel(x, y)[0];
      if (value != 0 && value != 255) {
        return true;
      }
    }
  }
  return false;
}

void ui_image_size_dialog_unit_and_resolution_links_work() {
  // The dialog remembers its units on accept; this test ends in Inches.
  SettingsValueRestorer restore_unit(QStringLiteral("imageSize/lastUnit"));
  SettingsValueRestorer restore_resolution_unit(QStringLiteral("imageSize/lastResolutionUnit"));
  patchy::ui::app_settings().remove(QStringLiteral("imageSize"));
  patchy::ui::MainWindow window;  // default document: 1024x768 at 72 ppi
  show_window(window);

  QTimer::singleShot(0, [] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyImageSizeDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      auto* width = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("imageSizeWidthSpin"));
      auto* height = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("imageSizeHeightSpin"));
      auto* resolution = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("imageSizeResolutionSpin"));
      auto* width_unit = dialog->findChild<QComboBox*>(QStringLiteral("imageSizeWidthUnitCombo"));
      auto* height_unit = dialog->findChild<QComboBox*>(QStringLiteral("imageSizeHeightUnitCombo"));
      auto* resolution_unit = dialog->findChild<QComboBox*>(QStringLiteral("imageSizeResolutionUnitCombo"));
      auto* dimensions = dialog->findChild<QLabel*>(QStringLiteral("imageSizeDimensionsLabel"));
      auto* resample = dialog->findChild<QCheckBox*>(QStringLiteral("imageSizeResampleCheck"));
      auto* link = dialog->findChild<QToolButton*>(QStringLiteral("imageSizeLinkButton"));
      CHECK(width != nullptr);
      CHECK(height != nullptr);
      CHECK(resolution != nullptr);
      CHECK(width_unit != nullptr);
      CHECK(height_unit != nullptr);
      CHECK(resolution_unit != nullptr);
      CHECK(dimensions != nullptr);
      CHECK(resample != nullptr);
      CHECK(link != nullptr);

      CHECK(width_unit->currentText() == QStringLiteral("Pixels"));
      CHECK(width->value() == 1024.0);
      CHECK(std::abs(resolution->value() - 72.0) < 0.01);
      // Every "Label:" in the dialog scrubs its field (GitHub issue 46), including
      // Width, whose link button shares the grid row.
      CHECK(width->property(patchy::ui::kScrubHandleInstalledProperty).toBool());
      CHECK(height->property(patchy::ui::kScrubHandleInstalledProperty).toBool());
      CHECK(resolution->property(patchy::ui::kScrubHandleInstalledProperty).toBool());
      CHECK(width->singleStep() == 1.0);

      // Physical units display through the resolution; the two unit combos stay in step.
      width_unit->setCurrentIndex(width_unit->findText(QStringLiteral("Inches")));
      QApplication::processEvents();
      CHECK(height_unit->currentText() == QStringLiteral("Inches"));
      CHECK(std::abs(width->value() - 1024.0 / 72.0) < 0.005);
      CHECK(std::abs(height->value() - 768.0 / 72.0) < 0.005);
      // A scrub or arrow step in inches moves a hundredth, never a whole inch.
      CHECK(std::abs(width->singleStep() - 0.01) < 1e-9);

      // Resample ON + physical units: a resolution change keeps the print size and
      // re-derives the pixel dimensions (72 -> 36 halves them).
      resolution->setValue(36.0);
      QApplication::processEvents();
      CHECK(dimensions->text().contains(QStringLiteral("512 px x 384 px")));
      CHECK(std::abs(width->value() - 1024.0 / 72.0) < 0.005);

      // The resolution unit combo only changes the display of the same stored PPI.
      resolution_unit->setCurrentIndex(resolution_unit->findText(QStringLiteral("Pixels/Centimeter")));
      QApplication::processEvents();
      CHECK(std::abs(resolution->value() - 36.0 / 2.54) < 0.01);
      resolution_unit->setCurrentIndex(resolution_unit->findText(QStringLiteral("Pixels/Inch")));
      QApplication::processEvents();
      CHECK(std::abs(resolution->value() - 36.0) < 0.01);

      // Resample OFF: pending resamples revert to the document's pixels, the link
      // and pixel units disable, and W/H/Resolution become the Photoshop tri-link.
      resample->setChecked(false);
      QApplication::processEvents();
      CHECK(!link->isEnabled());
      // The method combo disables and the "pixels are locked" hint appears: Resample
      // off never scales pixels, which the dialog now says instead of implying.
      auto* method = dialog->findChild<QComboBox*>(QStringLiteral("imageSizeResampleCombo"));
      auto* hint = dialog->findChild<QLabel*>(QStringLiteral("imageSizeResampleHintLabel"));
      CHECK(method != nullptr && !method->isEnabled());
      CHECK(hint != nullptr && hint->isVisible());
      CHECK(hint != nullptr && hint->text().contains(QStringLiteral("locked")));
      CHECK(!resample->toolTip().isEmpty());
      CHECK(width_unit->currentText() == QStringLiteral("Inches"));
      CHECK(dimensions->text().contains(QStringLiteral("1024 px x 768 px")));
      CHECK(std::abs(width->value() - 1024.0 / 36.0) < 0.005);
      width->setValue(5.12);
      QApplication::processEvents();
      CHECK(std::abs(resolution->value() - 200.0) < 0.05);
      CHECK(dimensions->text().contains(QStringLiteral("1024 px x 768 px")));
      widget->grab().save(QStringLiteral("test-artifacts/ui_image_size_dialog_units.png"));
      dialog->accept();
      return;
    }
    CHECK(false);
  });
  require_action(window, "imageSizeAction")->trigger();
  QApplication::processEvents();

  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(document.width() == 1024);
  CHECK(document.height() == 768);
  CHECK(std::abs(document.print_settings().horizontal_ppi - 200.0) < 0.05);
  CHECK(std::abs(document.print_settings().vertical_ppi - 200.0) < 0.05);
}

// Canvas Size converts its fields like Image Size: pixels stay the truth, the two
// unit combos stay in step, Percent is relative to the current size, Relative mode
// shows the change in the chosen unit, and the Current Size lines follow the unit.
void ui_canvas_size_dialog_units_convert_through_resolution() {
  SettingsValueRestorer restore_unit(QStringLiteral("canvasSize/lastUnit"));
  patchy::ui::app_settings().remove(QStringLiteral("canvasSize"));
  patchy::ui::MainWindow window;  // default document: 1024x768 at 72 ppi
  show_window(window);

  bool drove_dialog = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyCanvasSizeDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      auto* width = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("canvasSizeWidthSpin"));
      auto* height = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("canvasSizeHeightSpin"));
      auto* width_unit = dialog->findChild<QComboBox*>(QStringLiteral("canvasSizeWidthUnitCombo"));
      auto* height_unit = dialog->findChild<QComboBox*>(QStringLiteral("canvasSizeHeightUnitCombo"));
      auto* relative = dialog->findChild<QCheckBox*>(QStringLiteral("canvasSizeRelativeCheck"));
      auto* current_width = dialog->findChild<QLabel*>(QStringLiteral("canvasSizeCurrentWidthLabel"));
      auto* new_size = dialog->findChild<QLabel*>(QStringLiteral("canvasSizeNewSizeLabel"));
      CHECK(width != nullptr && height != nullptr && width_unit != nullptr && height_unit != nullptr &&
            relative != nullptr && current_width != nullptr && new_size != nullptr);

      CHECK(width_unit->count() == 6);
      CHECK(width_unit->currentText() == QStringLiteral("Pixels"));
      CHECK(width->value() == 1024.0 && height->value() == 768.0);
      CHECK(width->decimals() == 0);
      CHECK(current_width->text() == QStringLiteral("1024 px"));
      // The Width / Height labels scrub the fields (GitHub issue 46); the "New Size:"
      // caption above them must not have taken the width spin instead.
      CHECK(width->property(patchy::ui::kScrubHandleInstalledProperty).toBool());
      CHECK(height->property(patchy::ui::kScrubHandleInstalledProperty).toBool());
      CHECK(new_size->cursor().shape() != Qt::SizeHorCursor);
      QLabel* width_label = nullptr;
      for (auto* label : dialog->findChildren<QLabel*>()) {
        if (label->text() == QStringLiteral("Width") && label->cursor().shape() == Qt::SizeHorCursor) {
          width_label = label;
        }
      }
      CHECK(width_label != nullptr);

      // Inches through the 72 ppi document; the height combo follows the width combo.
      width_unit->setCurrentIndex(width_unit->findText(QStringLiteral("Inches")));
      QApplication::processEvents();
      CHECK(height_unit->currentText() == QStringLiteral("Inches"));
      CHECK(std::abs(width->value() - 1024.0 / 72.0) < 0.005);
      CHECK(std::abs(height->value() - 768.0 / 72.0) < 0.005);
      CHECK(std::abs(width->singleStep() - 0.01) < 1e-9);
      CHECK(current_width->text().startsWith(QStringLiteral("14.222")));
      // The summary is the dialog's "New Size: <megabytes>" line for the target pixels.
      const auto bytes_per_pixel =
          static_cast<double>(patchy::bytes_per_pixel(patchy::ui::MainWindowTestAccess::document(window).format()));
      const auto summary_for = [bytes_per_pixel](int w, int h) {
        return QStringLiteral("New Size: %1M")
            .arg(static_cast<double>(w) * h * bytes_per_pixel / (1024.0 * 1024.0), 0, 'f', 1);
      };
      width->setValue(10.0);
      QApplication::processEvents();
      CHECK(new_size->text() == summary_for(720, 768));

      // Relative mode shows the change in the unit: the width is now 10 in (720 px), so
      // the field reads -4.222 in; a typed +1 in makes the canvas 1096 px wide.
      relative->setChecked(true);
      QApplication::processEvents();
      CHECK(std::abs(width->value() - (720.0 - 1024.0) / 72.0) < 0.005);
      CHECK(std::abs(height->value()) < 0.0005);
      width->setValue(1.0);
      QApplication::processEvents();
      CHECK(new_size->text() == summary_for(1096, 768));

      // Percent is relative to the current size on each axis: +50% of the height.
      height_unit->setCurrentIndex(height_unit->findText(QStringLiteral("Percent")));
      QApplication::processEvents();
      CHECK(width_unit->currentText() == QStringLiteral("Percent"));
      CHECK(std::abs(width->value() - 7.03125) < 0.005);
      height->setValue(50.0);
      QApplication::processEvents();
      CHECK(new_size->text() == summary_for(1096, 1152));

      // Back to absolute pixels: the state is unchanged, only the display flips.
      relative->setChecked(false);
      width_unit->setCurrentIndex(width_unit->findText(QStringLiteral("Pixels")));
      QApplication::processEvents();
      CHECK(width->value() == 1096.0 && height->value() == 1152.0);
      widget->grab().save(QStringLiteral("test-artifacts/ui_canvas_size_dialog_units.png"));
      drove_dialog = true;
      dialog->accept();
      return;
    }
    CHECK(false);
  });
  require_action(window, "imageCanvasSizeAction")->trigger();
  QApplication::processEvents();
  CHECK(drove_dialog);

  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(document.width() == 1096);
  CHECK(document.height() == 1152);
}

// Canvas Size's link button (off by default) keeps the document's aspect ratio: the
// other axis follows an edit in absolute pixels, in Relative mode and Percent too,
// and turning the link on makes the pair proportional from the width right away.
void ui_canvas_size_dialog_link_keeps_aspect_ratio() {
  SettingsValueRestorer restore_unit(QStringLiteral("canvasSize/lastUnit"));
  patchy::ui::app_settings().remove(QStringLiteral("canvasSize"));
  patchy::ui::MainWindow window;  // default document: 1024x768 at 72 ppi
  show_window(window);

  bool drove_dialog = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyCanvasSizeDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      auto* width = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("canvasSizeWidthSpin"));
      auto* height = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("canvasSizeHeightSpin"));
      auto* width_unit = dialog->findChild<QComboBox*>(QStringLiteral("canvasSizeWidthUnitCombo"));
      auto* relative = dialog->findChild<QCheckBox*>(QStringLiteral("canvasSizeRelativeCheck"));
      auto* link = dialog->findChild<QToolButton*>(QStringLiteral("canvasSizeLinkButton"));
      auto* new_size = dialog->findChild<QLabel*>(QStringLiteral("canvasSizeNewSizeLabel"));
      CHECK(width != nullptr && height != nullptr && width_unit != nullptr && relative != nullptr &&
            link != nullptr && new_size != nullptr);
      CHECK(link->isCheckable() && !link->isChecked());
      CHECK(width_unit->currentText() == QStringLiteral("Pixels"));
      const auto bytes_per_pixel =
          static_cast<double>(patchy::bytes_per_pixel(patchy::ui::MainWindowTestAccess::document(window).format()));
      // The dialog prints megabytes at or above 1 MB and kilobytes below it.
      const auto summary_for = [bytes_per_pixel](int w, int h) {
        const auto bytes = static_cast<double>(w) * h * bytes_per_pixel;
        return bytes >= 1024.0 * 1024.0
                   ? QStringLiteral("New Size: %1M").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1)
                   : QStringLiteral("New Size: %1K").arg(bytes / 1024.0, 0, 'f', 1);
      };

      // Unlinked (the default): the axes are independent, as before.
      width->setValue(512.0);
      QApplication::processEvents();
      CHECK(height->value() == 768.0);
      CHECK(new_size->text() == summary_for(512, 768));

      // Linking derives the height from the width at once; edits then follow both ways.
      link->setChecked(true);
      QApplication::processEvents();
      CHECK(height->value() == 384.0);
      CHECK(new_size->text() == summary_for(512, 384));
      height->setValue(600.0);
      QApplication::processEvents();
      CHECK(width->value() == 800.0);
      CHECK(new_size->text() == summary_for(800, 600));

      // Relative mode links the resulting sizes, not the deltas: +256 px of width on a
      // 1024 px document makes 1280 x 960, shown as +192 px of height.
      relative->setChecked(true);
      QApplication::processEvents();
      CHECK(width->value() == -224.0 && height->value() == -168.0);
      width->setValue(256.0);
      QApplication::processEvents();
      CHECK(height->value() == 192.0);
      CHECK(new_size->text() == summary_for(1280, 960));

      // Percent through the link: 50% width is 50% height.
      relative->setChecked(false);
      width_unit->setCurrentIndex(width_unit->findText(QStringLiteral("Percent")));
      QApplication::processEvents();
      width->setValue(50.0);
      QApplication::processEvents();
      CHECK(std::abs(height->value() - 50.0) < 0.005);
      CHECK(new_size->text() == summary_for(512, 384));

      // Unlinking leaves the pair alone and frees the axes again.
      link->setChecked(false);
      height->setValue(100.0);
      QApplication::processEvents();
      CHECK(std::abs(width->value() - 50.0) < 0.005);
      CHECK(new_size->text() == summary_for(512, 768));
      link->setChecked(true);
      QApplication::processEvents();
      CHECK(new_size->text() == summary_for(512, 384));
      widget->grab().save(QStringLiteral("test-artifacts/ui_canvas_size_dialog_link.png"));
      drove_dialog = true;
      dialog->accept();
      return;
    }
    CHECK(false);
  });
  require_action(window, "imageCanvasSizeAction")->trigger();
  QApplication::processEvents();
  CHECK(drove_dialog);

  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(document.width() == 512);
  CHECK(document.height() == 384);
}

// The Canvas Size "delete layers fully off the canvas" checkbox: off by default, it
// removes the layers the resize leaves entirely outside the canvas, applies even when
// the size is unchanged (like the crop checkbox), and the status bar reports the count.
void ui_canvas_size_dialog_deletes_off_canvas_layers() {
  SettingsValueRestorer restore_unit(QStringLiteral("canvasSize/lastUnit"));
  patchy::ui::app_settings().remove(QStringLiteral("canvasSize"));
  patchy::ui::MainWindow window;  // default document: 1024x768 at 72 ppi
  show_window(window);
  const auto far_id = add_far_layer(window, "Far away", QPoint(2000, 2000));
  const auto near_id = add_far_layer(window, "Near", QPoint(1022, 766));
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_count = document.layers().size();

  bool drove_dialog = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyCanvasSizeDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      auto* remove = dialog->findChild<QCheckBox*>(QStringLiteral("canvasSizeDeleteOffCanvasCheck"));
      CHECK(remove != nullptr && !remove->isChecked());
      CHECK(dialog->windowTitle() == QStringLiteral("Canvas Size"));
      remove->setChecked(true);
      drove_dialog = true;
      dialog->accept();
      return;
    }
    CHECK(false);
  });
  require_action(window, "imageCanvasSizeAction")->trigger();
  QApplication::processEvents();
  CHECK(drove_dialog);
  CHECK(document.width() == 1024 && document.height() == 768);
  CHECK(document.find_layer(far_id) == nullptr);
  CHECK(document.find_layer(near_id) != nullptr);
  CHECK(document.layers().size() == layer_count - 1);
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Canvas 1024 x 768, off-canvas layers deleted: 1"));
}

// Both checkboxes together: the layer crop rewrites every pixel layer to canvas-sized
// bounds, so the delete has to be decided against the frame before the crop runs. A
// layer (and a group of layers) outside the selection goes, one inside stays, cropped.
void ui_crop_to_selection_advanced_crops_and_deletes_off_canvas_layers() {
  SettingsValueRestorer restore_unit(QStringLiteral("canvasSize/lastUnit"));
  patchy::ui::app_settings().remove(QStringLiteral("canvasSize"));
  patchy::ui::MainWindow window;  // default document: 1024x768 at 72 ppi
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_snap_enabled(false);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  drag(*canvas, QPoint(60, 60), QPoint(200, 160));
  const auto selection = canvas->selected_document_rect();
  CHECK(selection.has_value() && selection->width() > 8 && selection->height() > 8);

  const auto far_id = add_far_layer(window, "Far away", QPoint(selection->right() + 50, selection->bottom() + 50));
  const auto above_id = add_far_layer(window, "Above", QPoint(selection->x(), selection->y() - 4));
  // Straddles the selection's top-left corner: stays and is cropped.
  const auto inside_id = add_far_layer(window, "Inside", QPoint(selection->x() - 2, selection->y() - 2));
  patchy::Layer group(document.allocate_layer_id(), "Far folder", patchy::LayerKind::Group);
  const auto group_id = group.id();
  patchy::Layer child(document.allocate_layer_id(), "Far child",
                      patchy::PixelBuffer(4, 4, patchy::PixelFormat::rgba8()));
  child.set_bounds(patchy::Rect{selection->right() + 20, selection->y(), 4, 4});
  group.add_child(std::move(child));
  document.add_layer(std::move(group));

  bool drove_dialog = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyCanvasSizeDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      auto* crop = dialog->findChild<QCheckBox*>(QStringLiteral("canvasSizeCropLayersCheck"));
      auto* remove = dialog->findChild<QCheckBox*>(QStringLiteral("canvasSizeDeleteOffCanvasCheck"));
      CHECK(crop != nullptr && remove != nullptr);
      crop->setChecked(true);
      remove->setChecked(true);
      drove_dialog = true;
      dialog->accept();
      return;
    }
    CHECK(false);
  });
  require_action(window, "imageCropToSelectionAdvancedAction")->trigger();
  QApplication::processEvents();
  CHECK(drove_dialog);
  CHECK(document.width() == selection->width());
  CHECK(document.height() == selection->height());
  CHECK(document.find_layer(far_id) == nullptr);
  CHECK(document.find_layer(above_id) == nullptr);
  CHECK(document.find_layer(group_id) == nullptr);
  const auto* inside = std::as_const(document).find_layer(inside_id);
  CHECK(inside != nullptr);
  if (inside != nullptr) {
    const auto bounds = inside->bounds();
    CHECK(bounds.x >= 0 && bounds.y >= 0);
    CHECK(bounds.x + bounds.width <= document.width() && bounds.y + bounds.height <= document.height());
  }
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Canvas %1 x %2, off-canvas layers deleted: 3")
                                                    .arg(selection->width())
                                                    .arg(selection->height()));
}

// Crop to Selection (Advanced) opens the Canvas Size dialog with the selection as its
// frame: the fields prefill to the selection size, Current Size still shows the
// document, the layer crop starts checked (Photoshop's Image > Crop trims the layers)
// while the delete option does not, an unchanged accept crops exactly to the selection
// (every layer trimmed to the new canvas), and the delete option drops what the crop
// left outside.
void ui_crop_to_selection_advanced_prefills_canvas_size_dialog() {
  SettingsValueRestorer restore_unit(QStringLiteral("canvasSize/lastUnit"));
  patchy::ui::app_settings().remove(QStringLiteral("canvasSize"));
  patchy::ui::MainWindow window;  // default document: 1024x768 at 72 ppi
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_snap_enabled(false);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  // No selection: the command refuses like the plain crop, without a dialog.
  require_action(window, "imageCropToSelectionAdvancedAction")->trigger();
  QApplication::processEvents();
  CHECK(document.width() == 1024);
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Make a rectangular selection before cropping"));

  const auto far_id = add_far_layer(window, "Far away", QPoint(900, 700));
  const auto paint_layer_bounds = [&document] {
    for (const auto& layer : std::as_const(document).layers()) {
      if (layer.name() == "Paint Layer") {
        return layer.bounds();
      }
    }
    CHECK(false);
    return patchy::Rect{};
  };
  const auto paint_bounds_before = paint_layer_bounds();
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  drag(*canvas, QPoint(60, 60), QPoint(200, 160));
  const auto selection = canvas->selected_document_rect();
  CHECK(selection.has_value() && !selection->isEmpty());
  CHECK(selection->width() < 1024 && selection->height() < 768);

  bool drove_dialog = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyCanvasSizeDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      auto* width = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("canvasSizeWidthSpin"));
      auto* height = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("canvasSizeHeightSpin"));
      auto* width_unit = dialog->findChild<QComboBox*>(QStringLiteral("canvasSizeWidthUnitCombo"));
      auto* current_width = dialog->findChild<QLabel*>(QStringLiteral("canvasSizeCurrentWidthLabel"));
      auto* crop = dialog->findChild<QCheckBox*>(QStringLiteral("canvasSizeCropLayersCheck"));
      auto* remove = dialog->findChild<QCheckBox*>(QStringLiteral("canvasSizeDeleteOffCanvasCheck"));
      CHECK(width != nullptr && height != nullptr && width_unit != nullptr && current_width != nullptr &&
            crop != nullptr && remove != nullptr);
      CHECK(dialog->windowTitle() == QStringLiteral("Crop to Selection (Advanced)"));
      CHECK(crop->isChecked());
      CHECK(!remove->isChecked());
      CHECK(width_unit->currentText() == QStringLiteral("Pixels"));
      CHECK(width->value() == static_cast<double>(selection->width()));
      CHECK(height->value() == static_cast<double>(selection->height()));
      CHECK(current_width->text() == QStringLiteral("1024 px"));
      remove->setChecked(true);
      widget->grab().save(QStringLiteral("test-artifacts/ui_crop_to_selection_advanced.png"));
      drove_dialog = true;
      dialog->accept();
      return;
    }
    CHECK(false);
  });
  require_action(window, "imageCropToSelectionAdvancedAction")->trigger();
  QApplication::processEvents();
  CHECK(drove_dialog);
  CHECK(document.width() == selection->width());
  CHECK(document.height() == selection->height());
  CHECK(document.find_layer(far_id) == nullptr);
  const auto paint_bounds_after = paint_layer_bounds();
  CHECK(paint_bounds_before.width > selection->width());
  CHECK(paint_bounds_after.x == 0 && paint_bounds_after.y == 0);
  CHECK(paint_bounds_after.width == selection->width() && paint_bounds_after.height == selection->height());
  CHECK(!canvas->selected_document_rect().has_value());
}

void ui_image_size_dialog_remembers_units() {
  SettingsValueRestorer restore_ruler(QStringLiteral("view/rulerUnits"));
  SettingsValueRestorer restore_unit(QStringLiteral("imageSize/lastUnit"));
  SettingsValueRestorer restore_resolution_unit(QStringLiteral("imageSize/lastResolutionUnit"));
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("imageSize"));
    settings.setValue(QStringLiteral("view/rulerUnits"), QStringLiteral("cm"));
  }
  patchy::ui::MainWindow window;  // default document: 1024x768 at 72 ppi
  show_window(window);

  struct Fields {
    QDialog* dialog{nullptr};
    QComboBox* width_unit{nullptr};
    QComboBox* height_unit{nullptr};
    QComboBox* resolution_unit{nullptr};
    QDoubleSpinBox* width{nullptr};
    QDoubleSpinBox* resolution{nullptr};
  };
  bool drove_dialog = false;
  const auto open_dialog = [&](std::function<void(const Fields&)> body) {
    drove_dialog = false;
    QTimer::singleShot(0, [&drove_dialog, body = std::move(body)] {
      auto* dialog = find_top_level_dialog(QStringLiteral("patchyImageSizeDialog"));
      CHECK(dialog != nullptr);
      if (dialog == nullptr) {
        return;
      }
      Fields fields;
      fields.dialog = dialog;
      fields.width_unit = dialog->findChild<QComboBox*>(QStringLiteral("imageSizeWidthUnitCombo"));
      fields.height_unit = dialog->findChild<QComboBox*>(QStringLiteral("imageSizeHeightUnitCombo"));
      fields.resolution_unit = dialog->findChild<QComboBox*>(QStringLiteral("imageSizeResolutionUnitCombo"));
      fields.width = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("imageSizeWidthSpin"));
      fields.resolution = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("imageSizeResolutionSpin"));
      CHECK(fields.width_unit != nullptr && fields.height_unit != nullptr && fields.resolution_unit != nullptr &&
            fields.width != nullptr && fields.resolution != nullptr);
      body(fields);
      drove_dialog = true;
    });
    require_action(window, "imageSizeAction")->trigger();
    QApplication::processEvents();
    CHECK(drove_dialog);
  };
  const auto stored = [](const char* key) {
    return patchy::ui::app_settings().value(QLatin1String(key)).toString();
  };

  // First run: the ruler unit (cm) seeds both combos and the width already reads
  // in it. Pick Millimeters and Pixels/Centimeter, then accept without an edit.
  open_dialog([](const Fields& fields) {
    CHECK(fields.width_unit->currentText() == QStringLiteral("Centimeters"));
    CHECK(fields.height_unit->currentText() == QStringLiteral("Centimeters"));
    CHECK(fields.resolution_unit->currentIndex() == 0);
    CHECK(std::abs(fields.width->value() - 1024.0 / 72.0 * 2.54) < 0.01);
    fields.width_unit->setCurrentIndex(fields.width_unit->findText(QStringLiteral("Millimeters")));
    fields.resolution_unit->setCurrentIndex(1);
    QApplication::processEvents();
    CHECK(fields.height_unit->currentText() == QStringLiteral("Millimeters"));
    fields.dialog->accept();
  });
  CHECK(stored("imageSize/lastUnit") == QStringLiteral("mm"));
  CHECK(stored("imageSize/lastResolutionUnit") == QStringLiteral("cm"));
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(document.width() == 1024 && document.height() == 768);  // an unedited accept changes nothing

  // Reopening restores both units whatever the ruler unit says now; a cancel
  // writes nothing, so a unit picked before Cancel is forgotten.
  patchy::ui::app_settings().setValue(QStringLiteral("view/rulerUnits"), QStringLiteral("in"));
  open_dialog([](const Fields& fields) {
    CHECK(fields.width_unit->currentText() == QStringLiteral("Millimeters"));
    CHECK(fields.resolution_unit->currentIndex() == 1);
    CHECK(std::abs(fields.resolution->value() - 72.0 / 2.54) < 0.01);  // shown as pixels/cm
    CHECK(std::abs(fields.width->value() - 1024.0 / 72.0 * 25.4) < 0.1);
    fields.width_unit->setCurrentIndex(fields.width_unit->findText(QStringLiteral("Inches")));
    fields.resolution_unit->setCurrentIndex(0);
    QApplication::processEvents();
    fields.dialog->reject();
  });
  CHECK(stored("imageSize/lastUnit") == QStringLiteral("mm"));
  CHECK(stored("imageSize/lastResolutionUnit") == QStringLiteral("cm"));

  // A token the combo cannot show falls back to Pixels.
  patchy::ui::app_settings().setValue(QStringLiteral("imageSize/lastUnit"), QStringLiteral("furlongs"));
  open_dialog([](const Fields& fields) {
    CHECK(fields.width_unit->currentText() == QStringLiteral("Pixels"));
    CHECK(fields.width->value() == 1024.0);
    fields.dialog->reject();
  });
}

// The method combo drives the resampler and persists by id (`imageSize/lastResampleMethod`),
// the first opening defaults to Automatic, and the dead "Generative Upscale" label is gone.
void ui_image_size_dialog_method_is_applied_and_remembered() {
  SettingsValueRestorer restore_unit(QStringLiteral("imageSize/lastUnit"));
  SettingsValueRestorer restore_resolution_unit(QStringLiteral("imageSize/lastResolutionUnit"));
  SettingsValueRestorer restore_method(QStringLiteral("imageSize/lastResampleMethod"));
  patchy::ui::app_settings().remove(QStringLiteral("imageSize"));
  patchy::ui::MainWindow window;
  show_window(window);

  const auto resize_through_dialog = [&](const QString& method_id, const QString& expected_preselected) {
    bool drove_dialog = false;
    QTimer::singleShot(0, [&] {
      auto* dialog = find_top_level_dialog(QStringLiteral("patchyImageSizeDialog"));
      CHECK(dialog != nullptr);
      if (dialog == nullptr) {
        return;
      }
      auto* method = dialog->findChild<QComboBox*>(QStringLiteral("imageSizeResampleCombo"));
      auto* width_unit = dialog->findChild<QComboBox*>(QStringLiteral("imageSizeWidthUnitCombo"));
      auto* width = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("imageSizeWidthSpin"));
      auto* height = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("imageSizeHeightSpin"));
      auto* hint = dialog->findChild<QLabel*>(QStringLiteral("imageSizeResampleHintLabel"));
      CHECK(method != nullptr && width_unit != nullptr && width != nullptr && height != nullptr && hint != nullptr);
      if (method == nullptr || width_unit == nullptr || width == nullptr || height == nullptr) {
        dialog->reject();
        return;
      }
      CHECK(dialog->findChild<QLabel*>(QStringLiteral("imageSizeUpscaleLabel")) == nullptr);
      CHECK(method->count() == 6);
      CHECK(method->currentData().toString() == expected_preselected);
      CHECK(hint == nullptr || !hint->isVisible());
      method->setCurrentIndex(method->findData(method_id));
      CHECK(method->currentData().toString() == method_id);
      width_unit->setCurrentIndex(width_unit->findText(QStringLiteral("Pixels")));
      QApplication::processEvents();
      width->setValue(4);
      height->setValue(4);
      dialog->accept();
      drove_dialog = true;
    });
    require_action(window, "imageSizeAction")->trigger();
    QApplication::processEvents();
    process_events_for(120);
    CHECK(drove_dialog);
  };
  const auto stored_method = [] {
    return patchy::ui::app_settings().value(QStringLiteral("imageSize/lastResampleMethod")).toString();
  };

  auto& document = load_checker_document(window);
  resize_through_dialog(QStringLiteral("nearest"), QStringLiteral("automatic"));
  CHECK(checker_doubled_with_hard_edges(document));
  CHECK(stored_method() == QStringLiteral("nearest"));

  // Reopening preselects the remembered method; a cubic resize blends the block edges.
  load_checker_document(window);
  resize_through_dialog(QStringLiteral("bicubic"), QStringLiteral("nearest"));
  CHECK(document.width() == 4 && document.height() == 4);
  CHECK(checker_resized_with_midtones(document));
  CHECK(stored_method() == QStringLiteral("bicubic"));
}

// The dialog's preview resamples the document with the chosen method: the 2x2 checker
// enlarged to the box shows pure blocks under Nearest Neighbor and midtones under
// Bicubic, and switching the combo alone redraws it. (It used to smooth-scale the
// current pixels whatever the method; Seth, October 2026.)
void ui_image_size_dialog_preview_follows_method() {
  SettingsValueRestorer restore_unit(QStringLiteral("imageSize/lastUnit"));
  SettingsValueRestorer restore_method(QStringLiteral("imageSize/lastResampleMethod"));
  patchy::ui::app_settings().remove(QStringLiteral("imageSize"));
  patchy::ui::MainWindow window;
  show_window(window);
  load_checker_document(window);

  // Counts the pure (0 or 255) and the in-between gray values along the preview's
  // middle row, inside the drawn image (the box is 276 wide; the square image fills it).
  const auto classify_middle_row = [](const QLabel& preview) {
    const auto image = preview.pixmap().toImage();
    int pure = 0;
    int midtones = 0;
    const int y = image.height() / 2;
    for (int x = 2; x < image.width() - 2; ++x) {
      const auto value = qGray(image.pixel(x, y));
      if (value == 0 || value == 255) {
        ++pure;
      } else {
        ++midtones;
      }
    }
    return std::pair{pure, midtones};
  };

  bool drove_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyImageSizeDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* method = dialog->findChild<QComboBox*>(QStringLiteral("imageSizeResampleCombo"));
    auto* preview = dialog->findChild<QLabel*>(QStringLiteral("imageSizePreview"));
    CHECK(method != nullptr && preview != nullptr);
    if (method == nullptr || preview == nullptr) {
      dialog->reject();
      return;
    }
    method->setCurrentIndex(method->findData(QStringLiteral("nearest")));
    QApplication::processEvents();
    const auto [nearest_pure, nearest_midtones] = classify_middle_row(*preview);
    CHECK(nearest_pure > 200);
    CHECK(nearest_midtones == 0);
    method->setCurrentIndex(method->findData(QStringLiteral("bicubic")));
    QApplication::processEvents();
    const auto [bicubic_pure, bicubic_midtones] = classify_middle_row(*preview);
    CHECK(bicubic_midtones > 20);
    CHECK(bicubic_pure < nearest_pure);
    drove_dialog = true;
    dialog->reject();
  });
  require_action(window, "imageSizeAction")->trigger();
  QApplication::processEvents();
  process_events_for(120);
  CHECK(drove_dialog);
}

// doc.resizeImage(w, h, {method}) reaches the same resampler; an unknown id throws before
// anything changes, and the default is Automatic.
void ui_script_resize_image_method_option() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto& document = load_checker_document(window);
  auto& host = window.script_engine_host();
  const auto run = [&](const QString& source) {
    patchy::ui::ScriptEngineHost::RunOptions options;
    options.name = QStringLiteral("Resize");
    options.unattended = true;
    // run_source is false when the script throws synchronously, which the bad-id call must do.
    const bool started = host.run_source(source, options);
    CHECK(process_events_until([&] { return !host.run_active(); }));
    return started && !host.last_run_had_error();
  };

  CHECK(run(QStringLiteral("app.activeDocument.resizeImage(4, 4, {method: 'nearest'});")));
  CHECK(checker_doubled_with_hard_edges(document));

  CHECK(!run(QStringLiteral("app.activeDocument.resizeImage(8, 8, {method: 'lanczos'});")));
  CHECK(document.width() == 4 && document.height() == 4);

  CHECK(run(QStringLiteral("app.activeDocument.resizeImage(8, 8);")));
  CHECK(document.width() == 8 && document.height() == 8);
  CHECK(checker_resized_with_midtones(document));
}

// Canvas Size keeps its unit the same way (`canvasSize/lastUnit`); Relative and the
// layer crop stay unremembered.
void ui_canvas_size_dialog_remembers_unit() {
  SettingsValueRestorer restore_ruler(QStringLiteral("view/rulerUnits"));
  SettingsValueRestorer restore_unit(QStringLiteral("canvasSize/lastUnit"));
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("canvasSize"));
    settings.setValue(QStringLiteral("view/rulerUnits"), QStringLiteral("pt"));
  }
  patchy::ui::MainWindow window;  // default document: 1024x768 at 72 ppi
  show_window(window);

  struct Fields {
    QDialog* dialog{nullptr};
    QComboBox* width_unit{nullptr};
    QComboBox* height_unit{nullptr};
    QCheckBox* relative{nullptr};
    QDoubleSpinBox* width{nullptr};
  };
  bool drove_dialog = false;
  const auto open_dialog = [&](std::function<void(const Fields&)> body) {
    drove_dialog = false;
    QTimer::singleShot(0, [&drove_dialog, body = std::move(body)] {
      auto* dialog = find_top_level_dialog(QStringLiteral("patchyCanvasSizeDialog"));
      CHECK(dialog != nullptr);
      if (dialog == nullptr) {
        return;
      }
      Fields fields;
      fields.dialog = dialog;
      fields.width_unit = dialog->findChild<QComboBox*>(QStringLiteral("canvasSizeWidthUnitCombo"));
      fields.height_unit = dialog->findChild<QComboBox*>(QStringLiteral("canvasSizeHeightUnitCombo"));
      fields.relative = dialog->findChild<QCheckBox*>(QStringLiteral("canvasSizeRelativeCheck"));
      fields.width = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("canvasSizeWidthSpin"));
      CHECK(fields.width_unit != nullptr && fields.height_unit != nullptr && fields.relative != nullptr &&
            fields.width != nullptr);
      body(fields);
      drove_dialog = true;
    });
    require_action(window, "imageCanvasSizeAction")->trigger();
    QApplication::processEvents();
    CHECK(drove_dialog);
  };
  const auto stored_unit = [] {
    return patchy::ui::app_settings().value(QStringLiteral("canvasSize/lastUnit")).toString();
  };

  // First run: the ruler unit (points, a unit this dialog offers) seeds the combos.
  open_dialog([](const Fields& fields) {
    CHECK(fields.width_unit->currentText() == QStringLiteral("Points"));
    CHECK(fields.height_unit->currentText() == QStringLiteral("Points"));
    CHECK(std::abs(fields.width->value() - 1024.0) < 0.01);  // 72 ppi: one point per pixel
    fields.width_unit->setCurrentIndex(fields.width_unit->findText(QStringLiteral("Percent")));
    fields.relative->setChecked(true);
    QApplication::processEvents();
    fields.dialog->accept();
  });
  CHECK(stored_unit() == QStringLiteral("percent"));
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(document.width() == 1024 && document.height() == 768);

  // Reopening restores Percent under another ruler unit; Relative starts unchecked
  // again; a cancel forgets the pick made before it.
  patchy::ui::app_settings().setValue(QStringLiteral("view/rulerUnits"), QStringLiteral("in"));
  open_dialog([](const Fields& fields) {
    CHECK(fields.width_unit->currentText() == QStringLiteral("Percent"));
    CHECK(fields.height_unit->currentText() == QStringLiteral("Percent"));
    CHECK(std::abs(fields.width->value() - 100.0) < 0.01);
    CHECK(!fields.relative->isChecked());
    fields.width_unit->setCurrentIndex(fields.width_unit->findText(QStringLiteral("Millimeters")));
    QApplication::processEvents();
    fields.dialog->reject();
  });
  CHECK(stored_unit() == QStringLiteral("percent"));

  // An unknown token falls back to Pixels.
  patchy::ui::app_settings().setValue(QStringLiteral("canvasSize/lastUnit"), QStringLiteral("cubits"));
  open_dialog([](const Fields& fields) {
    CHECK(fields.width_unit->currentText() == QStringLiteral("Pixels"));
    CHECK(fields.width->value() == 1024.0);
    fields.dialog->reject();
  });
}

void ui_imported_image_density_follows_photoshop_conventions() {
  QImage source(8, 6, QImage::Format_RGB32);
  source.fill(QColor(10, 20, 30));

  QByteArray png_bytes;
  {
    QBuffer buffer(&png_bytes);
    buffer.open(QIODevice::WriteOnly);
    QImage tagged = source;
    tagged.setDotsPerMeterX(11811);  // 300 ppi
    tagged.setDotsPerMeterY(5906);   // 150 ppi
    CHECK(tagged.save(&buffer, "png"));
  }
  const auto png_span = std::span<const std::uint8_t>(
      reinterpret_cast<const std::uint8_t*>(png_bytes.constData()), static_cast<std::size_t>(png_bytes.size()));

  auto tagged_document = patchy::ui::document_from_qimage(source, "Tagged");
  patchy::ui::apply_imported_image_density(tagged_document, png_span, source);
  CHECK(std::abs(tagged_document.print_settings().horizontal_ppi - 11811.0 * 0.0254) < 0.001);
  CHECK(std::abs(tagged_document.print_settings().vertical_ppi - 5906.0 * 0.0254) < 0.001);

  // Strip the pHYs chunk (4 length + 4 type + 9 payload + 4 crc bytes): the file is
  // untagged and must open at Photoshop's 72 ppi, never Qt's screen-derived default.
  auto untagged_bytes = png_bytes;
  const auto phys_index = untagged_bytes.indexOf(QByteArrayLiteral("pHYs"));
  CHECK(phys_index > 4);
  untagged_bytes.remove(phys_index - 4, 21);
  const auto untagged_span = std::span<const std::uint8_t>(
      reinterpret_cast<const std::uint8_t*>(untagged_bytes.constData()),
      static_cast<std::size_t>(untagged_bytes.size()));
  auto untagged_document = patchy::ui::document_from_qimage(source, "Untagged");
  patchy::ui::apply_imported_image_density(untagged_document, untagged_span, source);
  CHECK(untagged_document.print_settings().horizontal_ppi == 72.0);
  CHECK(untagged_document.print_settings().vertical_ppi == 72.0);

  // JPEG JFIF densities are honored exactly.
  QByteArray jpeg_bytes;
  {
    QBuffer buffer(&jpeg_bytes);
    buffer.open(QIODevice::WriteOnly);
    QImage tagged = source;
    tagged.setDotsPerMeterX(9449);  // 240 ppi
    tagged.setDotsPerMeterY(9449);
    CHECK(tagged.save(&buffer, "jpg"));
  }
  const auto jpeg_span = std::span<const std::uint8_t>(
      reinterpret_cast<const std::uint8_t*>(jpeg_bytes.constData()), static_cast<std::size_t>(jpeg_bytes.size()));
  auto jpeg_document = patchy::ui::document_from_qimage(source, "Jpeg");
  patchy::ui::apply_imported_image_density(jpeg_document, jpeg_span, source);
  CHECK(std::abs(jpeg_document.print_settings().horizontal_ppi - 240.0) < 0.5);
}

void ui_ruler_unit_preference_changes_ruler_ticks() {
  ensure_artifact_dir();

  // Rendering: a standalone canvas at 100 ppi so 1 in = 100 doc px exactly.
  patchy::Document document(300, 200, patchy::PixelFormat::rgb8());
  document.print_settings().horizontal_ppi = 100.0;
  document.print_settings().vertical_ppi = 100.0;
  document.add_pixel_layer("Background", solid_pixels(300, 200, patchy::PixelFormat::rgb8(), Qt::white));
  patchy::ui::CanvasWidget canvas;
  canvas.resize(420, 300);
  canvas.set_document(&document);
  canvas.set_zoom(1.0);
  canvas.set_rulers_visible(true);
  canvas.show();
  QApplication::processEvents();

  const auto ruler_tick_pixels = [&canvas] {
    const auto strip = canvas.grab(QRect(0, 0, canvas.width(), 24)).toImage();
    int ticks = 0;
    for (int y = 0; y < strip.height(); ++y) {
      for (int x = 0; x < strip.width(); ++x) {
        if (color_close(strip.pixelColor(x, y), QColor(185, 190, 198), 40)) {
          ++ticks;
        }
      }
    }
    return std::pair<QImage, int>(strip, ticks);
  };
  CHECK(canvas.ruler_unit() == patchy::ui::MeasurementUnit::Pixels);
  const auto [pixel_ruler, pixel_ticks] = ruler_tick_pixels();
  CHECK(pixel_ticks > 0);

  canvas.set_ruler_unit(patchy::ui::MeasurementUnit::Inches);
  QApplication::processEvents();
  const auto [inch_ruler, inch_ticks] = ruler_tick_pixels();
  CHECK(inch_ticks > 0);
  CHECK(pixel_ruler != inch_ruler);
  pixel_ruler.save(QStringLiteral("test-artifacts/ui_ruler_units_pixels.png"));
  inch_ruler.save(QStringLiteral("test-artifacts/ui_ruler_units_inches.png"));

  // Preference propagation: the window-level setter reaches the session canvas and
  // persists the settings token.
  SettingsValueRestorer restore_units(QStringLiteral("view/rulerUnits"));
  patchy::ui::MainWindow window;
  show_window(window);
  auto* session_canvas = require_canvas(window);
  patchy::ui::MainWindowTestAccess::set_ruler_unit_preference(window, patchy::ui::MeasurementUnit::Inches);
  QApplication::processEvents();
  CHECK(session_canvas->ruler_unit() == patchy::ui::MeasurementUnit::Inches);
  auto settings = patchy::ui::app_settings();
  CHECK(settings.value(QStringLiteral("view/rulerUnits")).toString() == QStringLiteral("in"));
}

void ui_dragged_image_file_opens_document_tab() {
  ensure_artifact_dir();
  const auto image_path = std::filesystem::absolute(std::filesystem::path("test-artifacts") / "drag-open.png");
  const auto image_path_qt = QString::fromStdString(image_path.string());

  QImage source(6, 4, QImage::Format_RGB32);
  source.fill(QColor(20, 40, 60));
  source.setPixelColor(2, 1, QColor(30, 200, 240));
  CHECK(source.save(image_path_qt));

  patchy::ui::MainWindow window;
  show_window(window);

  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  CHECK(tabs != nullptr);
  CHECK(tabs->count() == 1);
  auto* canvas = require_canvas(window);

  QMimeData mime_data;
  mime_data.setUrls(QList<QUrl>{QUrl::fromLocalFile(image_path_qt)});
  const auto drop_position = canvas->rect().center();

  QDragEnterEvent drag_enter(drop_position, Qt::CopyAction, &mime_data, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(canvas, &drag_enter);
  QApplication::processEvents();
  CHECK(drag_enter.isAccepted());

  QDragMoveEvent drag_move(drop_position, Qt::CopyAction, &mime_data, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(canvas, &drag_move);
  QApplication::processEvents();
  CHECK(drag_move.isAccepted());

  bool saw_open_progress = false;

  QDropEvent drop(QPointF(drop_position), Qt::CopyAction, &mime_data, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(canvas, &drop);
  CHECK(tabs->count() == 1);
  std::exception_ptr progress_error;
  QTimer::singleShot(0, &window, [&] {
    try {
      verify_open_progress_dialog(QStringLiteral("drag-open.png"), saw_open_progress);
    } catch (...) {
      progress_error = std::current_exception();
    }
  });
  QApplication::processEvents();
  if (progress_error) std::rethrow_exception(progress_error);

  CHECK(drop.isAccepted());
  CHECK(saw_open_progress);
  CHECK(tabs->count() == 2);
  CHECK(tabs->tabText(tabs->currentIndex()) == QStringLiteral("drag-open.png"));
  // macOS titles carry the [*] windowModified placeholder (refresh_document_window_title).
  auto dragged_window_title = window.windowTitle();
  dragged_window_title.remove(QStringLiteral("[*]"));
  CHECK(dragged_window_title == QStringLiteral("drag-open.png"));
  canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* active_layer_info = window.findChild<QLabel*>(QStringLiteral("activeLayerInfoLabel"));
  CHECK(layer_list != nullptr);
  CHECK(active_layer_info != nullptr);
  CHECK(layer_list->count() == 1);
  CHECK(layer_list->currentItem() != nullptr);
  CHECK(layer_list->currentItem()->text() == QStringLiteral("drag-open"));
  CHECK(layer_list->selectedItems().size() == 1);
  CHECK(layer_list->currentItem()->isSelected());
  CHECK(active_layer_info->text().contains(QStringLiteral("drag-open")));
  CHECK(color_close(canvas_pixel_center(*canvas, QPoint(2, 1)), QColor(30, 200, 240), 8));
}

void ui_file_drop_discards_work_when_owner_closes() {
  ensure_artifact_dir();
  const auto path = QFileInfo(QStringLiteral("test-artifacts/drop-closed-window.png")).absoluteFilePath();
  QImage source(6, 4, QImage::Format_RGB32);
  source.fill(QColor(30, 60, 90));
  CHECK(source.save(path));

  for (const bool destroy_owner : {false, true}) {
    auto window = std::make_unique<patchy::ui::MainWindow>();
    show_window_empty(*window);
    {
      QMimeData mime;
      mime.setUrls({QUrl::fromLocalFile(path)});
      QDragEnterEvent enter(QPoint(50, 50), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
      QApplication::sendEvent(window.get(), &enter);
      CHECK(enter.isAccepted());
      QDropEvent drop(QPointF(50, 50), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
      QApplication::sendEvent(window.get(), &drop);
      CHECK(drop.isAccepted());
    }
    CHECK(patchy::ui::MainWindowTestAccess::session_count(*window) == 0);
    CHECK(window->close());
    if (destroy_owner) window.reset();
    QApplication::processEvents();
    if (window) CHECK(patchy::ui::MainWindowTestAccess::session_count(*window) == 0);
    CHECK(find_top_level_dialog(QStringLiteral("openProgressDialog")) == nullptr);
  }
}

void ui_reported_psd_open_shows_progress_dialog_if_available() {
  const auto fixture_path =
      patchy::test::local_psd_fixture_path("C2Kyoto Nintendo NES Cartridge Label Template (Front).psd");
  patchy::test::skip_unless_exists(fixture_path);
  const auto psd_path = QString::fromStdString(fixture_path.string());

  patchy::ui::MainWindow window;
  show_window(window);

  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  CHECK(tabs != nullptr);
  const auto original_tab_count = tabs->count();
  auto* canvas = require_canvas(window);

  QMimeData mime_data;
  mime_data.setUrls(QList<QUrl>{QUrl::fromLocalFile(psd_path)});
  const auto drop_position = canvas->rect().center();

  QDragEnterEvent drag_enter(drop_position, Qt::CopyAction, &mime_data, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(canvas, &drag_enter);
  QApplication::processEvents();
  CHECK(drag_enter.isAccepted());

  QDragMoveEvent drag_move(drop_position, Qt::CopyAction, &mime_data, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(canvas, &drag_move);
  QApplication::processEvents();
  CHECK(drag_move.isAccepted());

  bool saw_open_progress = false;
  const auto expected_file_name = QFileInfo(psd_path).fileName();
  const auto compatibility_report_done = std::make_shared<bool>(false);
  accept_compatibility_report_when_present(compatibility_report_done);

  // The template is full of placed smart objects; their import notes ride the status
  // bar (the popup only appears when imports/showPsdWarningsAndInfo is enabled).
  QDropEvent drop(QPointF(drop_position), Qt::CopyAction, &mime_data, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(canvas, &drop);
  CHECK(tabs->count() == original_tab_count);
  std::exception_ptr progress_error;
  QTimer::singleShot(0, &window, [&] {
    try {
      verify_open_progress_dialog(expected_file_name, saw_open_progress);
    } catch (...) {
      progress_error = std::current_exception();
    }
  });
  QApplication::processEvents();
  *compatibility_report_done = true;
  if (progress_error) std::rethrow_exception(progress_error);

  CHECK(drop.isAccepted());
  CHECK(saw_open_progress);
  CHECK(tabs->count() == original_tab_count + 1);
  CHECK(tabs->tabText(tabs->currentIndex()) == expected_file_name);
}

void ui_qimage_render_respects_hidden_layer_groups() {
  patchy::Document document(1, 1, patchy::PixelFormat::rgb8());
  patchy::PixelBuffer background(1, 1, patchy::PixelFormat::rgb8());
  auto* background_px = background.pixel(0, 0);
  background_px[0] = 255;
  background_px[1] = 255;
  background_px[2] = 255;
  document.add_pixel_layer("Background", std::move(background));

  patchy::PixelBuffer child_pixels(1, 1, patchy::PixelFormat::rgba8());
  auto* child_px = child_pixels.pixel(0, 0);
  child_px[0] = 220;
  child_px[1] = 20;
  child_px[2] = 30;
  child_px[3] = 255;
  patchy::Layer group(document.allocate_layer_id(), "Folder", patchy::LayerKind::Group);
  group.add_child(patchy::Layer(document.allocate_layer_id(), "Child", std::move(child_pixels)));
  document.add_layer(std::move(group));

  auto shown = patchy::ui::qimage_from_document(document, false);
  CHECK(shown.pixelColor(0, 0).red() == 220);

  document.layers()[1].set_visible(false);
  CHECK(document.layers()[1].children().front().visible());
  auto hidden = patchy::ui::qimage_from_document(document, false);
  CHECK(hidden.pixelColor(0, 0).red() == 255);
  CHECK(hidden.pixelColor(0, 0).green() == 255);
  CHECK(hidden.pixelColor(0, 0).blue() == 255);
}

void ui_qimage_region_render_matches_full_layer_styles() {
  patchy::Document document(64, 48, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer background(64, 48, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < background.height(); ++y) {
    for (std::int32_t x = 0; x < background.width(); ++x) {
      auto* px = background.pixel(x, y);
      px[0] = 52;
      px[1] = 58;
      px[2] = 66;
      px[3] = 255;
    }
  }
  document.add_pixel_layer("Background", std::move(background));

  patchy::PixelBuffer badge(24, 16, patchy::PixelFormat::rgba8());
  badge.clear(0);
  for (std::int32_t y = 2; y < 14; ++y) {
    for (std::int32_t x = 3; x < 21; ++x) {
      auto* px = badge.pixel(x, y);
      px[0] = 230;
      px[1] = 150;
      px[2] = 35;
      px[3] = 220;
    }
  }

  auto layer = patchy::Layer(document.allocate_layer_id(), "Styled Badge", std::move(badge));
  const auto styled_layer_id = layer.id();
  layer.set_bounds(patchy::Rect{18, 14, 24, 16});
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.distance = 4.0F;
  shadow.size = 5.0F;
  shadow.opacity = 0.6F;
  layer.layer_style().drop_shadows.push_back(shadow);
  patchy::LayerOuterGlow glow;
  glow.enabled = true;
  glow.size = 6.0F;
  glow.opacity = 0.45F;
  glow.color = patchy::RgbColor{255, 230, 120};
  layer.layer_style().outer_glows.push_back(glow);
  patchy::LayerStroke stroke;
  stroke.enabled = true;
  stroke.size = 3.0F;
  stroke.color = patchy::RgbColor{15, 25, 35};
  layer.layer_style().strokes.push_back(stroke);
  document.add_layer(std::move(layer));

  const QRect region(10, 8, 45, 34);
  const auto full = patchy::ui::qimage_from_document(document, true).copy(region);
  const auto partial = patchy::ui::qimage_from_document_rect(document, region, true);
  CHECK(partial.size() == full.size());
  for (int y = 0; y < partial.height(); ++y) {
    for (int x = 0; x < partial.width(); ++x) {
      CHECK(color_close(partial.pixelColor(x, y), full.pixelColor(x, y), 0));
    }
  }

  const QRegion disjoint_region(QRect(10, 8, 14, 12));
  auto multi_region = disjoint_region.united(QRect(44, 29, 11, 10));
  const auto full_original = patchy::ui::qimage_from_document(document, true);
  const auto patches = patchy::ui::qimage_patches_from_document_region(document, multi_region, true);
  CHECK(patches.size() == 2U);
  for (const auto& patch : patches) {
    CHECK(patch.image.size() == patch.document_rect.size());
    const auto expected_patch = full_original.copy(patch.document_rect);
    CHECK(images_equal_rgba(patch.image, expected_patch));
  }

  const auto moved_bounds = patchy::Rect{24, 17, 24, 16};
  const QRect moved_region(10, 8, 50, 36);
  const auto moved_override =
      patchy::ui::qimage_from_document_rect_with_layer_bounds(document, moved_region, true, styled_layer_id,
                                                                 moved_bounds);
  auto* moved_layer = document.find_layer(styled_layer_id);
  CHECK(moved_layer != nullptr);
  moved_layer->set_bounds(moved_bounds);
  const auto moved_actual = patchy::ui::qimage_from_document_rect(document, moved_region, true);
  CHECK(moved_override.size() == moved_actual.size());
  for (int y = 0; y < moved_override.height(); ++y) {
    for (int x = 0; x < moved_override.width(); ++x) {
      CHECK(color_close(moved_override.pixelColor(x, y), moved_actual.pixelColor(x, y), 0));
    }
  }

  QRegion moved_dirty(QRect(16, 12, 18, 18));
  moved_dirty += QRect(43, 24, 14, 12);
  const std::vector<std::pair<patchy::LayerId, patchy::Rect>> moved_overrides{{styled_layer_id, moved_bounds}};
  const auto moved_patches =
      patchy::ui::qimage_patches_from_document_region_with_layer_bounds(document, moved_dirty, true, moved_overrides);
  CHECK(moved_patches.size() >= 2U);
  const auto moved_full = patchy::ui::qimage_from_document(document, true);
  for (const auto& patch : moved_patches) {
    CHECK(images_equal_rgba(patch.image, moved_full.copy(patch.document_rect)));
  }
}

void ui_qimage_region_render_matches_full_with_clipping() {
  patchy::Document document(64, 48, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer background(64, 48, patchy::PixelFormat::rgba8());
  background.clear(255);
  document.add_pixel_layer("Background", std::move(background));

  patchy::Layer base(document.allocate_layer_id(), "Base",
                     solid_pixels(28, 20, patchy::PixelFormat::rgba8(), QColor(190, 40, 40, 255)));
  base.set_bounds(patchy::Rect{12, 10, 28, 20});
  base.set_opacity(0.8F);
  document.add_layer(std::move(base));

  patchy::Layer member(document.allocate_layer_id(), "Member",
                       solid_pixels(64, 48, patchy::PixelFormat::rgba8(), QColor(30, 120, 220, 255)));
  member.set_clipped(true);
  member.set_blend_mode(patchy::BlendMode::Multiply);
  document.add_layer(std::move(member));

  patchy::AdjustmentSettings warm;
  warm.kind = patchy::AdjustmentKind::ColorBalance;
  warm.color_balance = patchy::ColorBalanceAdjustment{35, 0, 0};
  patchy::Layer adjustment(document.allocate_layer_id(), "Warmth", patchy::LayerKind::Adjustment);
  adjustment.set_bounds(patchy::Rect::from_size(64, 48));
  patchy::configure_adjustment_layer(adjustment, warm);
  adjustment.set_clipped(true);
  document.add_layer(std::move(adjustment));

  // Patch renders through the region path must match the full render exactly,
  // including patches that slice through the clip group's interior.
  const QRect region(8, 6, 40, 30);
  const auto full = patchy::ui::qimage_from_document(document, true).copy(region);
  const auto partial = patchy::ui::qimage_from_document_rect(document, region, true);
  CHECK(partial.size() == full.size());
  for (int y = 0; y < partial.height(); ++y) {
    for (int x = 0; x < partial.width(); ++x) {
      CHECK(color_close(partial.pixelColor(x, y), full.pixelColor(x, y), 0));
    }
  }

  const QRegion disjoint_region(QRect(10, 8, 14, 12));
  auto multi_region = disjoint_region.united(QRect(30, 20, 12, 12));
  const auto full_original = patchy::ui::qimage_from_document(document, true);
  const auto patches = patchy::ui::qimage_patches_from_document_region(document, multi_region, true);
  CHECK(patches.size() == 2U);
  for (const auto& patch : patches) {
    CHECK(patch.image.size() == patch.document_rect.size());
    const auto expected_patch = full_original.copy(patch.document_rect);
    CHECK(images_equal_rgba(patch.image, expected_patch));
  }
}

void ui_qimage_layer_bounds_override_moves_linked_masks_only() {
  {
    patchy::Document document(80, 48, patchy::PixelFormat::rgba8());
    document.add_pixel_layer("Background", solid_pixels(80, 48, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

    auto layer = patchy::Layer(document.allocate_layer_id(), "Linked Mask",
                               solid_pixels(12, 12, patchy::PixelFormat::rgba8(), QColor(30, 95, 230, 255)));
    const auto layer_id = layer.id();
    layer.set_bounds(patchy::Rect{10, 10, 12, 12});
    patchy::PixelBuffer mask_pixels(12, 12, patchy::PixelFormat::gray8());
    mask_pixels.clear(255);
    layer.set_mask(patchy::LayerMask{patchy::Rect{10, 10, 12, 12}, std::move(mask_pixels), 0, false});
    document.add_layer(std::move(layer));

    const auto moved_bounds = patchy::Rect{42, 10, 12, 12};
    const QRect region(0, 0, 80, 48);
    const auto preview =
        patchy::ui::qimage_from_document_rect_with_layer_bounds(document, region, true, layer_id, moved_bounds);

    auto* moved_layer = document.find_layer(layer_id);
    CHECK(moved_layer != nullptr);
    moved_layer->set_bounds(moved_bounds);
    auto& mask = *moved_layer->mask();
    mask.bounds.x += 32;
    const auto committed = patchy::ui::qimage_from_document_rect(document, region, true);

    CHECK(images_equal_rgba(preview, committed));
    CHECK(color_close(preview.pixelColor(46, 14), QColor(30, 95, 230), 0));
    CHECK(color_close(preview.pixelColor(14, 14), QColor(Qt::white), 0));
  }

  {
    patchy::Document document(80, 48, patchy::PixelFormat::rgba8());
    document.add_pixel_layer("Background", solid_pixels(80, 48, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

    auto layer = patchy::Layer(document.allocate_layer_id(), "Unlinked Mask",
                               solid_pixels(12, 12, patchy::PixelFormat::rgba8(), QColor(230, 80, 30, 255)));
    const auto layer_id = layer.id();
    layer.set_bounds(patchy::Rect{10, 10, 12, 12});
    patchy::PixelBuffer mask_pixels(12, 12, patchy::PixelFormat::gray8());
    mask_pixels.clear(255);
    layer.set_mask(patchy::LayerMask{patchy::Rect{10, 10, 12, 12}, std::move(mask_pixels), 0, false});
    patchy::set_layer_mask_linked(layer, false);
    document.add_layer(std::move(layer));

    const auto moved_bounds = patchy::Rect{42, 10, 12, 12};
    const QRect region(0, 0, 80, 48);
    const auto preview =
        patchy::ui::qimage_from_document_rect_with_layer_bounds(document, region, true, layer_id, moved_bounds);

    auto* moved_layer = document.find_layer(layer_id);
    CHECK(moved_layer != nullptr);
    moved_layer->set_bounds(moved_bounds);
    const auto committed = patchy::ui::qimage_from_document_rect(document, region, true);

    CHECK(images_equal_rgba(preview, committed));
    CHECK(color_close(preview.pixelColor(46, 14), QColor(Qt::white), 0));
    CHECK(color_close(preview.pixelColor(14, 14), QColor(Qt::white), 0));
  }
}

}  // namespace

std::vector<patchy::test::TestCase> import_print_resolution_tests_part3() {
  return {
      {"ui_image_size_dialog_unit_and_resolution_links_work", ui_image_size_dialog_unit_and_resolution_links_work},
      {"ui_canvas_size_dialog_units_convert_through_resolution", ui_canvas_size_dialog_units_convert_through_resolution},
      {"ui_canvas_size_dialog_link_keeps_aspect_ratio", ui_canvas_size_dialog_link_keeps_aspect_ratio},
      {"ui_canvas_size_dialog_deletes_off_canvas_layers", ui_canvas_size_dialog_deletes_off_canvas_layers},
      {"ui_crop_to_selection_advanced_crops_and_deletes_off_canvas_layers", ui_crop_to_selection_advanced_crops_and_deletes_off_canvas_layers},
      {"ui_crop_to_selection_advanced_prefills_canvas_size_dialog", ui_crop_to_selection_advanced_prefills_canvas_size_dialog},
      {"ui_image_size_dialog_remembers_units", ui_image_size_dialog_remembers_units},
      {"ui_image_size_dialog_method_is_applied_and_remembered", ui_image_size_dialog_method_is_applied_and_remembered},
      {"ui_image_size_dialog_preview_follows_method", ui_image_size_dialog_preview_follows_method},
      {"ui_script_resize_image_method_option", ui_script_resize_image_method_option},
      {"ui_canvas_size_dialog_remembers_unit", ui_canvas_size_dialog_remembers_unit},
      {"ui_imported_image_density_follows_photoshop_conventions", ui_imported_image_density_follows_photoshop_conventions},
      {"ui_ruler_unit_preference_changes_ruler_ticks", ui_ruler_unit_preference_changes_ruler_ticks},
      {"ui_dragged_image_file_opens_document_tab", ui_dragged_image_file_opens_document_tab},
      {"ui_file_drop_discards_work_when_owner_closes", ui_file_drop_discards_work_when_owner_closes},
      {"ui_reported_psd_open_shows_progress_dialog_if_available", ui_reported_psd_open_shows_progress_dialog_if_available},
      {"ui_qimage_render_respects_hidden_layer_groups", ui_qimage_render_respects_hidden_layer_groups},
      {"ui_qimage_region_render_matches_full_layer_styles", ui_qimage_region_render_matches_full_layer_styles},
      {"ui_qimage_region_render_matches_full_with_clipping", ui_qimage_region_render_matches_full_with_clipping},
      {"ui_qimage_layer_bounds_override_moves_linked_masks_only", ui_qimage_layer_bounds_override_moves_linked_masks_only},
  };
}
