#include <QStatusBar>
#include "ui/canvas_widget.hpp"
#include "ui/main_window.hpp"
#include "ui/app_settings.hpp"
#include "test_harness.hpp"
#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
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
#include <numbers>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "ui_test_access.hpp"
#include "scripting_test_support.hpp"
#include "core/layer_metadata.hpp"
#include "core/pixel_tools.hpp"
#include "core/warp_mesh.hpp"
#include <QTest>
#include "local_psd_fixtures.hpp"
// Crop tool: the canvas frame on activation, drag-out geometry, handle
// adjustment, ratio re-fits, Enter/Esc commit-reset, canvas expansion on
// commit, and the options-bar ratio/apply/reset row.
#include "ui_test_support.hpp"
#include "ui_test_groups.hpp"
#include <QAction>
#include <QCheckBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QListWidget>
#include <QAbstractItemView>
#include <QMenu>
#include <QPainter>
#include <QScopeGuard>
#include <QRadioButton>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include "ui/color_panel.hpp"
#include "ui/crop_presets.hpp"
#include "ui/hotkey_registry.hpp"
#include "ui/new_document_dialog.hpp"
#include "core/pixel_depth.hpp"
#include "formats/document_trim.hpp"
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardItem>
#include <QStandardItemModel>
#include "scripting_test_support.hpp"
#include "ui/script_engine.hpp"
#include <cstdio>
#include <cstdlib>

namespace {

using namespace patchy::test::ui;

using CropKind = patchy::ui::CropPresetKind;

// The Preset list's row of a kind (and of a ratio or pixel size, when given); -1 when absent.
int crop_preset_index(const QComboBox& combo, CropKind kind, QSizeF value = QSizeF()) {
  for (int index = 0; index < combo.count(); ++index) {
    const auto data = combo.itemData(index, patchy::ui::kCropPresetKindRole);
    if (!data.isValid() || static_cast<CropKind>(data.toInt()) != kind) {
      continue;
    }
    if (value.isEmpty() || combo.itemData(index, patchy::ui::kCropPresetValueRole).toSizeF() == value) {
      return index;
    }
  }
  return -1;
}

// The kind of the row the Preset list shows.
CropKind crop_preset_kind(const QComboBox& combo) {
  return static_cast<CropKind>(combo.currentData(patchy::ui::kCropPresetKindRole).toInt());
}

bool crop_preset_row_enabled(const QComboBox& combo, int index) {
  const auto* model = qobject_cast<const QStandardItemModel*>(combo.model());
  return model != nullptr && model->item(index) != nullptr && model->item(index)->isEnabled();
}

void ui_crop_tool_activates_with_c_hotkey() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  auto* crop_tool = require_action(window, "toolCropAction");
  CHECK(crop_tool->shortcut() == QKeySequence(Qt::Key_C));
  // Plain C moved off the menu command (persisted id unchanged, empty default).
  CHECK(require_action(window, "imageCropToSelectionAction")->shortcut().isEmpty());

  crop_tool->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Crop);
  // GitHub issue 66: the tool starts with the whole canvas framed, handles
  // ready, nothing to apply yet.
  CHECK(canvas->crop_session_active());
  CHECK(canvas->crop_session_rect() == QRect(0, 0, 1024, 768));
  CHECK(canvas->crop_session_angle() == 0.0);
  CHECK(!canvas->crop_session_has_changes());
  CHECK(window.statusBar()->currentMessage().startsWith(QStringLiteral("Crop: drag the handles")));
}

// The ratio fields drop trailing zeros like the pixel fields beside them: "0" for
// unconstrained, "2", "1.5", never "2.00" (GitHub issue 201).
void ui_crop_ratio_fields_show_trimmed_numbers() {
  SettingsValueRestorer saved_ratio_w(QStringLiteral("tools/cropRatioWidth"));
  SettingsValueRestorer saved_ratio_h(QStringLiteral("tools/cropRatioHeight"));
  patchy::ui::MainWindow window;
  show_window(window);
  require_canvas(window);
  require_action(window, "toolCropAction")->trigger();
  QApplication::processEvents();
  auto* ratio_w = window.findChild<QDoubleSpinBox*>(QStringLiteral("cropRatioWidthSpin"));
  auto* ratio_h = window.findChild<QDoubleSpinBox*>(QStringLiteral("cropRatioHeightSpin"));
  CHECK(ratio_w != nullptr && ratio_h != nullptr);
  if (ratio_w == nullptr || ratio_h == nullptr) {
    return;
  }
  CHECK(ratio_w->decimals() == 2);
  ratio_w->setValue(0.0);
  ratio_h->setValue(0.0);
  QApplication::processEvents();
  CHECK(ratio_w->text() == QStringLiteral("0") && ratio_h->text() == QStringLiteral("0"));
  ratio_w->setValue(2.0);
  ratio_h->setValue(1.0);
  QApplication::processEvents();
  CHECK(ratio_w->text() == QStringLiteral("2") && ratio_h->text() == QStringLiteral("1"));
  ratio_w->setValue(1.5);
  QApplication::processEvents();
  CHECK(ratio_w->text() == QStringLiteral("1.5"));
  // Typing keeps two decimals of input; "1.50" displays as "1.5".
  auto* editor = ratio_h->findChild<QLineEdit*>();
  CHECK(editor != nullptr);
  if (editor != nullptr) {
    editor->setText(QStringLiteral("1.25"));
    ratio_h->interpretText();
    CHECK(ratio_h->value() == 1.25);
    CHECK(ratio_h->text() == QStringLiteral("1.25"));
    editor->setText(QStringLiteral("1.50"));
    ratio_h->interpretText();
    CHECK(ratio_h->value() == 1.5);
    CHECK(ratio_h->text() == QStringLiteral("1.5"));
  }
}

void ui_crop_drag_out_geometry() {
  SettingsValueRestorer saved_ratio_w(QStringLiteral("tools/cropRatioWidth"));
  SettingsValueRestorer saved_ratio_h(QStringLiteral("tools/cropRatioHeight"));
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action(window, "toolCropAction")->trigger();
  QApplication::processEvents();
  canvas->set_snap_enabled(false);
  canvas->set_crop_ratio(0.0, 0.0);

  // A plain click keeps the canvas frame.
  const auto click_point = canvas->widget_position_for_document_point(QPoint(60, 60));
  send_mouse(*canvas, QEvent::MouseButtonPress, click_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, click_point, Qt::LeftButton, Qt::NoButton);
  CHECK(canvas->crop_session_rect() == QRect(0, 0, 1024, 768));

  // A drag inside the frame lays out the rect exactly (inclusive of both
  // endpoints, the marquee convention).
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(40, 40)),
       canvas->widget_position_for_document_point(QPoint(100, 80)));
  CHECK(canvas->crop_session_active());
  auto rect = canvas->crop_session_rect();
  CHECK(rect.has_value());
  CHECK(*rect == QRect(40, 40, 61, 41));

  // Esc puts the frame back instead of ending the session.
  send_key(*canvas, Qt::Key_Escape);
  CHECK(canvas->crop_session_active());
  CHECK(canvas->crop_session_rect() == QRect(0, 0, 1024, 768));

  // Shift constrains the drag-out to a square.
  send_mouse(*canvas, QEvent::MouseButtonPress, canvas->widget_position_for_document_point(QPoint(40, 40)),
             Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
  send_mouse(*canvas, QEvent::MouseMove, canvas->widget_position_for_document_point(QPoint(200, 100)),
             Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, canvas->widget_position_for_document_point(QPoint(200, 100)),
             Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
  rect = canvas->crop_session_rect();
  CHECK(rect.has_value());
  CHECK(rect->width() == rect->height());
  send_key(*canvas, Qt::Key_Escape);

  // Ratio fields re-fit the canvas frame at once (centered, the long axis
  // shrinks) and constrain the next drag-out.
  auto* ratio_w = window.findChild<QDoubleSpinBox*>(QStringLiteral("cropRatioWidthSpin"));
  auto* ratio_h = window.findChild<QDoubleSpinBox*>(QStringLiteral("cropRatioHeightSpin"));
  CHECK(ratio_w != nullptr);
  CHECK(ratio_h != nullptr);
  CHECK(ratio_w->isVisible());
  ratio_w->setValue(2.0);
  ratio_h->setValue(1.0);
  QApplication::processEvents();
  CHECK(canvas->crop_ratio_width() == 2.0);
  CHECK(canvas->crop_ratio_height() == 1.0);
  CHECK(canvas->crop_session_rect() == QRect(0, 128, 1024, 512));
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(120, 120)),
       canvas->widget_position_for_document_point(QPoint(240, 220)));
  rect = canvas->crop_session_rect();
  CHECK(rect.has_value());
  const auto ratio = static_cast<double>(rect->width()) / static_cast<double>(rect->height());
  CHECK(ratio > 1.9);
  CHECK(ratio < 2.1);
  send_key(*canvas, Qt::Key_Escape);
  CHECK(canvas->crop_session_rect() == QRect(0, 128, 1024, 512));

  // Clear zeroes both fields, lifts the constraint, and grows the frame back.
  auto* clear_button = window.findChild<QPushButton*>(QStringLiteral("cropRatioClearButton"));
  CHECK(clear_button != nullptr);
  clear_button->click();
  QApplication::processEvents();
  CHECK(canvas->crop_ratio_width() == 0.0);
  CHECK(canvas->crop_ratio_height() == 0.0);
  CHECK(canvas->crop_session_rect() == QRect(0, 0, 1024, 768));

  // The Preset list shows Ratio while nothing is set, a ratio preset fills the fields,
  // manual values read back as Ratio (the free mode), and Original Ratio derives from the
  // document (GitHub issue 696).
  auto* preset_combo = window.findChild<QComboBox*>(QStringLiteral("cropPresetCombo"));
  CHECK(preset_combo != nullptr);
  if (preset_combo == nullptr) {
    return;
  }
  CHECK(preset_combo->currentText() == QStringLiteral("Ratio"));
  preset_combo->setCurrentIndex(crop_preset_index(*preset_combo, CropKind::Ratio, QSizeF(1.0, 1.0)));
  QApplication::processEvents();
  CHECK(preset_combo->currentText() == QStringLiteral("1 : 1 (Square)"));
  CHECK(canvas->crop_ratio_width() == 1.0);
  CHECK(canvas->crop_ratio_height() == 1.0);
  CHECK(canvas->crop_session_rect() == QRect(128, 0, 768, 768));
  ratio_w->setValue(3.0);
  ratio_h->setValue(7.0);
  QApplication::processEvents();
  CHECK(crop_preset_kind(*preset_combo) == CropKind::FreeRatio);
  // Original Ratio: 1024 x 768 reduces to 4 : 3.
  preset_combo->setCurrentIndex(crop_preset_index(*preset_combo, CropKind::OriginalRatio));
  QApplication::processEvents();
  CHECK(canvas->crop_ratio_width() == 4.0);
  CHECK(canvas->crop_ratio_height() == 3.0);
  CHECK(crop_preset_kind(*preset_combo) == CropKind::OriginalRatio);
  CHECK(canvas->crop_session_rect() == QRect(0, 0, 1024, 768));
  clear_button->click();
  QApplication::processEvents();
  CHECK(crop_preset_kind(*preset_combo) == CropKind::FreeRatio);
}

// GitHub issue 66 items 1 and 2: the frame carries handles from the start (Alt
// over one still shows the resize cursor), a ratio re-fits the frame from the
// whole canvas and a custom box inside itself about its center, and the
// interior of a custom box moves it while the frame's interior lays out anew.
void ui_crop_tool_frames_canvas_and_fits_ratio() {
  SettingsValueRestorer saved_ratio_w(QStringLiteral("tools/cropRatioWidth"));
  SettingsValueRestorer saved_ratio_h(QStringLiteral("tools/cropRatioHeight"));
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action(window, "toolCropAction")->trigger();
  QApplication::processEvents();
  canvas->set_snap_enabled(false);
  canvas->set_crop_ratio(0.0, 0.0);
  CHECK(canvas->crop_session_rect() == QRect(0, 0, 1024, 768));

  // The bottom-right handle sits on the canvas corner; Alt does not demote it.
  const auto corner = canvas->widget_position_for_document_point(QPoint(1024, 768));
  send_mouse(*canvas, QEvent::MouseMove, corner, Qt::NoButton, Qt::NoButton);
  CHECK(canvas->cursor().shape() == Qt::SizeFDiagCursor);
  send_mouse(*canvas, QEvent::MouseMove, corner, Qt::NoButton, Qt::NoButton, Qt::AltModifier);
  CHECK(canvas->cursor().shape() == Qt::SizeFDiagCursor);

  // Dragging the right edge inward crops; the frame becomes a custom box.
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(1024, 384)),
       canvas->widget_position_for_document_point(QPoint(824, 384)));
  CHECK(canvas->crop_session_rect() == QRect(0, 0, 824, 768));
  CHECK(canvas->crop_session_has_changes());
  // Dragging the left edge outward extends past the canvas.
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(0, 384)),
       canvas->widget_position_for_document_point(QPoint(-50, 384)));
  CHECK(canvas->crop_session_rect() == QRect(-50, 0, 874, 768));
  send_key(*canvas, Qt::Key_Escape);
  CHECK(canvas->crop_session_rect() == QRect(0, 0, 1024, 768));
  CHECK(!canvas->crop_session_has_changes());

  // Ratios re-fit the frame from the whole canvas, centered.
  auto* ratio_w = window.findChild<QDoubleSpinBox*>(QStringLiteral("cropRatioWidthSpin"));
  auto* ratio_h = window.findChild<QDoubleSpinBox*>(QStringLiteral("cropRatioHeightSpin"));
  CHECK(ratio_w != nullptr);
  CHECK(ratio_h != nullptr);
  ratio_w->setValue(1.0);
  ratio_h->setValue(1.0);
  QApplication::processEvents();
  CHECK(canvas->crop_session_rect() == QRect(128, 0, 768, 768));
  ratio_w->setValue(16.0);
  ratio_h->setValue(9.0);
  QApplication::processEvents();
  CHECK(canvas->crop_session_rect() == QRect(0, 96, 1024, 576));
  // A press on the canvas outside the fitted frame still lays out a new box.
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(20, 20)),
       canvas->widget_position_for_document_point(QPoint(180, 60)));
  auto rect = canvas->crop_session_rect();
  CHECK(rect.has_value());
  CHECK(rect->x() == 20);
  CHECK(rect->y() == 20);
  CHECK(std::abs(rect->width() * 9 - rect->height() * 16) <= 16);
  send_key(*canvas, Qt::Key_Escape);
  window.findChild<QPushButton*>(QStringLiteral("cropRatioClearButton"))->click();
  QApplication::processEvents();
  CHECK(canvas->crop_session_rect() == QRect(0, 0, 1024, 768));

  // A custom box keeps its width and center and takes the ratio's height, so
  // the result does not depend on the ratios typed on the way; Clear keeps it.
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(100, 100)),
       canvas->widget_position_for_document_point(QPoint(300, 200)));
  CHECK(canvas->crop_session_rect() == QRect(100, 100, 201, 101));
  ratio_w->setValue(1.0);
  ratio_h->setValue(1.0);
  QApplication::processEvents();
  CHECK(canvas->crop_session_rect() == QRect(100, 50, 201, 201));
  ratio_h->setValue(4.0);  // 1 : 4 on the way to 1 : 2
  ratio_h->setValue(2.0);
  QApplication::processEvents();
  CHECK(canvas->crop_session_rect() == QRect(100, -50, 201, 402));
  window.findChild<QPushButton*>(QStringLiteral("cropRatioClearButton"))->click();
  QApplication::processEvents();
  CHECK(canvas->crop_session_rect() == QRect(100, -50, 201, 402));

  // The interior of a custom box moves it.
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(200, 150)),
       canvas->widget_position_for_document_point(QPoint(220, 170)));
  CHECK(canvas->crop_session_rect() == QRect(120, -30, 201, 402));

  // Enter on the untouched frame is a no-op (no undo step); the options-bar
  // buttons are disabled for it.
  send_key(*canvas, Qt::Key_Escape);
  auto* apply = window.findChild<QPushButton*>(QStringLiteral("cropApplyButton"));
  CHECK(apply != nullptr);
  CHECK(!apply->isEnabled());
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(canvas->crop_session_rect() == QRect(0, 0, 1024, 768));
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Nothing to crop: the box matches the canvas"));
  CHECK(!require_action_by_text(window, QStringLiteral("Undo"))->isEnabled());
  save_widget_artifact("ui_crop_canvas_frame", *canvas);
}

// GitHub issue 66 item 5: an active selection becomes the crop box when the
// tool is picked, overriding the ratio; the marching ants hide meanwhile, a
// tool switch gives the selection back, and a commit consumes it.
void ui_crop_tool_adopts_active_selection() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* info_label = window.findChild<QLabel*>(QStringLiteral("documentInfoLabel"));
  CHECK(info_label != nullptr);
  canvas->set_snap_enabled(false);
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(100, 100)),
       canvas->widget_position_for_document_point(QPoint(300, 200)));
  const auto selected = canvas->selected_document_rect();
  CHECK(selected.has_value());
  CHECK(selected == QRect(100, 100, 201, 101));
  const auto ants_image = render_widget_image(*canvas);

  canvas->set_crop_ratio(1.0, 1.0);
  require_action(window, "toolCropAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->crop_session_active());
  CHECK(canvas->crop_session_rect() == selected);
  CHECK(canvas->crop_session_has_changes());
  canvas->set_crop_ratio(0.0, 0.0);  // Clear leaves the adopted box alone
  CHECK(canvas->crop_session_rect() == selected);
  // The selection outline is gone while the box stands in for it: the top
  // edge pixel row of the selection no longer alternates black and white.
  const auto crop_image = render_widget_image(*canvas);
  const auto edge = canvas->widget_position_for_document_point(QPoint(150, 100));
  bool outline_changed = false;
  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = 0; dx <= 8; dx += 4) {
      const auto at = edge + QPoint(dx, dy);
      outline_changed = outline_changed || ants_image.pixelColor(at) != crop_image.pixelColor(at);
    }
  }
  CHECK(outline_changed);

  // It is a custom box: the interior moves it.
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(200, 150)),
       canvas->widget_position_for_document_point(QPoint(220, 160)));
  CHECK(canvas->crop_session_rect() == QRect(120, 110, 201, 101));

  // Leaving the tool cancels the box and the selection is still there.
  require_action(window, "toolMoveAction")->trigger();
  QApplication::processEvents();
  CHECK(!canvas->crop_session_active());
  CHECK(canvas->selected_document_rect() == selected);

  // Picking it again adopts the selection again; Enter crops to it and clears it.
  require_action(window, "toolCropAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->crop_session_rect() == selected);
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(info_label->text().contains(QStringLiteral("201 x 101 px")));
  CHECK(!canvas->has_selection());
  // The committed document is framed afresh.
  CHECK(canvas->crop_session_rect() == QRect(0, 0, 201, 101));
  CHECK(!canvas->crop_session_has_changes());

  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(info_label->text().contains(QStringLiteral("1024 x 768 px")));
  CHECK(canvas->crop_session_rect() == QRect(0, 0, 1024, 768));
}

// The Preset list's two free modes: Ratio shows the ratio pair, Size shows unit Width /
// Height fields that mirror the box and resize it about its center (linked
// when the chain button is down), handle drags flow back into the fields, and
// picking Ratio again shows the ratio fields with nothing set. tools/cropStyle persists.
void ui_crop_size_style_fields_mirror_and_resize_box() {
  SettingsValueRestorer saved_style(QStringLiteral("tools/cropStyle"));
  SettingsValueRestorer saved_ratio_w(QStringLiteral("tools/cropRatioWidth"));
  SettingsValueRestorer saved_ratio_h(QStringLiteral("tools/cropRatioHeight"));
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action(window, "toolCropAction")->trigger();
  QApplication::processEvents();
  canvas->set_snap_enabled(false);

  auto* style = window.findChild<QComboBox*>(QStringLiteral("cropPresetCombo"));
  auto* ratio_w = window.findChild<QDoubleSpinBox*>(QStringLiteral("cropRatioWidthSpin"));
  auto* ratio_h = window.findChild<QDoubleSpinBox*>(QStringLiteral("cropRatioHeightSpin"));
  auto* width = window.findChild<QSpinBox*>(QStringLiteral("cropWidthSpin"));
  auto* height = window.findChild<QSpinBox*>(QStringLiteral("cropHeightSpin"));
  auto* link = window.findChild<QPushButton*>(QStringLiteral("cropLinkSizeButton"));
  CHECK(style != nullptr);
  CHECK(ratio_w != nullptr);
  CHECK(ratio_h != nullptr);
  CHECK(width != nullptr);
  CHECK(height != nullptr);
  CHECK(link != nullptr);
  if (style == nullptr || ratio_w == nullptr || ratio_h == nullptr || width == nullptr || height == nullptr ||
      link == nullptr) {
    return;
  }
  // The free modes lead the list.
  const int ratio_row = crop_preset_index(*style, CropKind::FreeRatio);
  const int size_row = crop_preset_index(*style, CropKind::FreeSize);
  CHECK(ratio_row == 0 && size_row == 1);
  style->setCurrentIndex(ratio_row);
  ratio_w->setValue(2.0);
  ratio_h->setValue(1.0);
  QApplication::processEvents();
  CHECK(style->currentText() == QStringLiteral("Ratio"));
  CHECK(ratio_w->isVisible());
  CHECK(!width->isVisible());
  CHECK(!height->isVisible());
  CHECK(canvas->crop_session_rect() == QRect(0, 128, 1024, 512));

  // Size: the ratio fields hide, the constraint lifts (the automatic frame grows
  // back to the canvas; a custom box would keep its shape), and the fields
  // mirror the box.
  style->setCurrentIndex(size_row);
  QApplication::processEvents();
  CHECK(style->currentText() == QStringLiteral("Size"));
  CHECK(!ratio_w->isVisible());
  CHECK(width->isVisible());
  CHECK(height->isVisible());
  CHECK(width->value() == 1024);
  CHECK(height->value() == 768);
  CHECK(canvas->crop_ratio_width() == 0.0);
  CHECK(canvas->crop_session_rect() == QRect(0, 0, 1024, 768));

  // Typing a width resizes the box about its center; height alone stays.
  width->setValue(500);
  QApplication::processEvents();
  CHECK(canvas->crop_session_rect() == QRect(262, 0, 500, 768));
  CHECK(canvas->crop_session_has_changes());
  height->setValue(400);
  QApplication::processEvents();
  CHECK(canvas->crop_session_rect() == QRect(262, 184, 500, 400));

  // A handle drag flows back into the fields (right edge out by 100).
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(762, 384)),
       canvas->widget_position_for_document_point(QPoint(862, 384)));
  CHECK(canvas->crop_session_rect() == QRect(262, 184, 600, 400));
  CHECK(width->value() == 600);
  CHECK(height->value() == 400);

  save_widget_artifact("ui_crop_size_options_bar", window);

  // Linked: the other axis keeps the box's proportion (600 : 400).
  link->setChecked(true);
  width->setValue(300);
  QApplication::processEvents();
  CHECK(canvas->crop_session_rect() == QRect(412, 284, 300, 200));
  CHECK(height->value() == 200);
  height->setValue(100);
  QApplication::processEvents();
  CHECK(canvas->crop_session_rect() == QRect(487, 334, 150, 100));
  CHECK(width->value() == 150);
  link->setChecked(false);

  // A typed unit token converts like the marquee's fields: percent of the
  // document width (1024) here, so no PPI enters the expectation.
  auto* width_editor = width->findChild<QLineEdit*>();
  CHECK(width_editor != nullptr);
  if (width_editor != nullptr) {
    width_editor->setText(QStringLiteral("50%"));
    width->interpretText();
    QApplication::processEvents();
    CHECK(width->value() == 512);
    CHECK(canvas->crop_session_rect()->width() == 512);
  }

  // Esc still resets to the canvas frame and the fields follow.
  send_key(*canvas, Qt::Key_Escape);
  CHECK(width->value() == 1024);
  CHECK(height->value() == 768);

  // Back to Ratio: the ratio fields return with nothing set (the list's Ratio entry is the
  // free mode, GitHub issue 696), so the frame stays the whole canvas.
  style->setCurrentIndex(ratio_row);
  QApplication::processEvents();
  CHECK(canvas->crop_ratio_width() == 0.0);
  CHECK(ratio_w->value() == 0.0);
  CHECK(canvas->crop_session_rect() == QRect(0, 0, 1024, 768));
  CHECK(ratio_w->isVisible());
  CHECK(!width->isVisible());

  // The style persists across windows.
  style->setCurrentIndex(size_row);
  QApplication::processEvents();
  window.close();
  QApplication::processEvents();
  patchy::ui::MainWindow second;
  show_window(second);
  require_action(second, "toolCropAction")->trigger();
  QApplication::processEvents();
  auto* second_style = second.findChild<QComboBox*>(QStringLiteral("cropPresetCombo"));
  CHECK(second_style != nullptr);
  if (second_style != nullptr) {
    CHECK(crop_preset_kind(*second_style) == CropKind::FreeSize);
  }
  auto* second_width = second.findChild<QSpinBox*>(QStringLiteral("cropWidthSpin"));
  CHECK(second_width != nullptr && second_width->isVisible());
  CHECK(require_canvas(second)->crop_ratio_width() == 0.0);
}

void ui_crop_handles_resize_move_and_nudge() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action(window, "toolCropAction")->trigger();
  QApplication::processEvents();
  canvas->set_snap_enabled(false);
  canvas->set_crop_ratio(0.0, 0.0);

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(100, 100)),
       canvas->widget_position_for_document_point(QPoint(200, 180)));
  auto rect = canvas->crop_session_rect();
  CHECK(rect.has_value());
  CHECK(*rect == QRect(100, 100, 101, 81));

  // Bottom-right corner handle grows the rect; a handle drag places the edge
  // AT the cursor (exclusive), unlike the inclusive drag-out.
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(201, 181)),
       canvas->widget_position_for_document_point(QPoint(240, 220)));
  rect = canvas->crop_session_rect();
  CHECK(rect.has_value());
  CHECK(*rect == QRect(100, 100, 140, 120));

  // Left edge handle moves only that side.
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(100, 160)),
       canvas->widget_position_for_document_point(QPoint(80, 300)));
  rect = canvas->crop_session_rect();
  CHECK(rect.has_value());
  CHECK(*rect == QRect(80, 100, 160, 120));

  // An interior drag translates the rect wholesale.
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(160, 160)),
       canvas->widget_position_for_document_point(QPoint(180, 170)));
  rect = canvas->crop_session_rect();
  CHECK(rect.has_value());
  CHECK(*rect == QRect(100, 110, 160, 120));
  CHECK(canvas->crop_session_active());

  // Arrows nudge; Shift-arrows nudge by 10.
  send_key(*canvas, Qt::Key_Right);
  send_key(*canvas, Qt::Key_Down, Qt::ShiftModifier);
  rect = canvas->crop_session_rect();
  CHECK(rect.has_value());
  CHECK(*rect == QRect(101, 120, 160, 120));

  send_key(*canvas, Qt::Key_Left, Qt::KeypadModifier);
  send_key(*canvas, Qt::Key_Up, Qt::KeypadModifier | Qt::ShiftModifier);
  CHECK(canvas->crop_session_rect() == QRect(100, 110, 160, 120));
  send_key(*canvas, Qt::Key_Right, Qt::KeypadModifier);
  send_key(*canvas, Qt::Key_Down, Qt::KeypadModifier | Qt::ShiftModifier);
  CHECK(canvas->crop_session_rect() == rect);

  // Hovering off the box hints the rotate gesture with the custom bitmap
  // cursor; the handles keep their resize cursors.
  const auto outside = canvas->widget_position_for_document_point(QPoint(500, 500));
  send_mouse(*canvas, QEvent::MouseMove, outside, Qt::NoButton, Qt::NoButton);
  CHECK(canvas->cursor().shape() == Qt::BitmapCursor);
  send_mouse(*canvas, QEvent::MouseMove, canvas->widget_position_for_document_point(QPoint(261, 240)),
             Qt::NoButton, Qt::NoButton);
  CHECK(canvas->cursor().shape() == Qt::SizeFDiagCursor);

  // A click off the rect keeps the session, the rect, and the angle.
  send_mouse(*canvas, QEvent::MouseButtonPress, outside, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, outside, Qt::LeftButton, Qt::NoButton);
  rect = canvas->crop_session_rect();
  CHECK(canvas->crop_session_active());
  CHECK(rect.has_value());
  CHECK(*rect == QRect(101, 120, 160, 120));
  CHECK(canvas->crop_session_angle() == 0.0);

  // A drag off the rect rotates the box about its center (the straighten
  // gesture); the rect itself stays put. Center of the box is (181, 180).
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(400, 180)),
       canvas->widget_position_for_document_point(QPoint(181, 400)));
  CHECK(std::abs(canvas->crop_session_angle() - 90.0) < 1.0);
  rect = canvas->crop_session_rect();
  CHECK(rect.has_value());
  CHECK(*rect == QRect(101, 120, 160, 120));
  save_widget_artifact("ui_crop_handles", *canvas);
  send_key(*canvas, Qt::Key_Escape);
  CHECK(canvas->crop_session_rect() == QRect(0, 0, 1024, 768));
  CHECK(canvas->crop_session_angle() == 0.0);
}

// GitHub issue 66: Alt while dragging a crop handle resizes the box about its
// center (the opposite side mirrors the dragged one), free or ratio-locked.
// Size style's Width/Height link binds handle drags too (GitHub issue 332): a
// corner scales both sides and an edge derives the other side, both at the
// drag-start proportion; with the link off (or in Ratio style) handles are free.
void ui_crop_size_link_keeps_proportion_on_handle_drags() {
  SettingsValueRestorer saved_style(QStringLiteral("tools/cropStyle"));
  SettingsValueRestorer saved_ratio_w(QStringLiteral("tools/cropRatioWidth"));
  SettingsValueRestorer saved_ratio_h(QStringLiteral("tools/cropRatioHeight"));
  patchy::ui::MainWindow window;
  show_window(window);
  // The drags below reach the box's right and bottom handles (x 812, y 584 at 100%), which
  // the default test window's canvas does not show once the panels and the wrapped options
  // bar take their room; a press outside the canvas misses the handle.
  window.resize(1600, 1000);
  auto* canvas = require_canvas(window);
  require_action(window, "toolCropAction")->trigger();
  QApplication::processEvents();
  canvas->set_snap_enabled(false);

  auto* style = window.findChild<QComboBox*>(QStringLiteral("cropPresetCombo"));
  auto* width = window.findChild<QSpinBox*>(QStringLiteral("cropWidthSpin"));
  auto* height = window.findChild<QSpinBox*>(QStringLiteral("cropHeightSpin"));
  auto* link = window.findChild<QPushButton*>(QStringLiteral("cropLinkSizeButton"));
  CHECK(style != nullptr);
  CHECK(width != nullptr);
  CHECK(height != nullptr);
  CHECK(link != nullptr);
  if (style == nullptr || width == nullptr || height == nullptr || link == nullptr) {
    return;
  }
  style->setCurrentIndex(crop_preset_index(*style, CropKind::FreeSize));
  link->setChecked(false);
  width->setValue(600);
  height->setValue(400);
  QApplication::processEvents();
  CHECK(canvas->crop_session_rect() == QRect(212, 184, 600, 400));
  CHECK(!canvas->crop_keep_proportions());

  const auto at = [&](int x, int y) { return canvas->widget_position_for_document_point(QPoint(x, y)); };
  // 3:2 within a pixel of rounding.
  const auto keeps_three_two = [](QRect rect) { return std::abs(rect.width() * 2 - rect.height() * 3) <= 3; };

  link->setChecked(true);
  QApplication::processEvents();
  CHECK(canvas->crop_keep_proportions());

  // Bottom-right corner out, mostly sideways: both sides scale, the top-left
  // corner stays put, and the fields follow in proportion.
  drag(*canvas, at(812, 584), at(962, 614));
  auto rect = canvas->crop_session_rect();
  CHECK(rect.has_value());
  CHECK(rect->topLeft() == QPoint(212, 184));
  CHECK(rect->width() > 600);
  CHECK(rect->height() > 400);
  CHECK(keeps_three_two(*rect));
  CHECK(width->value() == rect->width());
  CHECK(height->value() == rect->height());

  // Right edge out by 60: the height follows, re-centered on the box; the left
  // edge stays.
  const auto before_edge = *rect;
  drag(*canvas, at(before_edge.x() + before_edge.width(), before_edge.y() + before_edge.height() / 2),
       at(before_edge.x() + before_edge.width() + 60, before_edge.y() + before_edge.height() / 2));
  rect = canvas->crop_session_rect();
  CHECK(rect.has_value());
  CHECK(rect->x() == before_edge.x());
  CHECK(std::abs(rect->width() - (before_edge.width() + 60)) <= 1);
  CHECK(rect->height() > before_edge.height());
  CHECK(keeps_three_two(*rect));
  CHECK(std::abs((rect->y() + rect->height() / 2) - (before_edge.y() + before_edge.height() / 2)) <= 1);

  // Bottom edge in by 30: the width follows.
  const auto before_bottom = *rect;
  drag(*canvas, at(before_bottom.x() + before_bottom.width() / 2, before_bottom.y() + before_bottom.height()),
       at(before_bottom.x() + before_bottom.width() / 2, before_bottom.y() + before_bottom.height() - 30));
  rect = canvas->crop_session_rect();
  CHECK(rect.has_value());
  CHECK(rect->y() == before_bottom.y());
  CHECK(std::abs(rect->height() - (before_bottom.height() - 30)) <= 1);
  CHECK(rect->width() < before_bottom.width());
  CHECK(keeps_three_two(*rect));

  // Ratio style lifts the link from the canvas (its own ratio, if any, rules).
  style->setCurrentIndex(0);
  QApplication::processEvents();
  CHECK(!canvas->crop_keep_proportions());
  style->setCurrentIndex(1);
  QApplication::processEvents();
  CHECK(canvas->crop_keep_proportions());

  // Link off: the edge resizes one axis only, the corner is free.
  link->setChecked(false);
  QApplication::processEvents();
  CHECK(!canvas->crop_keep_proportions());
  const auto before_free = *canvas->crop_session_rect();
  drag(*canvas, at(before_free.x() + before_free.width(), before_free.y() + before_free.height() / 2),
       at(before_free.x() + before_free.width() + 60, before_free.y() + before_free.height() / 2));
  rect = canvas->crop_session_rect();
  CHECK(rect.has_value());
  CHECK(*rect == QRect(before_free.x(), before_free.y(), before_free.width() + 60, before_free.height()));
  const auto before_corner = *rect;
  drag(*canvas, at(before_corner.x() + before_corner.width(), before_corner.y() + before_corner.height()),
       at(before_corner.x() + before_corner.width() + 100, before_corner.y() + before_corner.height() + 10));
  rect = canvas->crop_session_rect();
  CHECK(rect.has_value());
  CHECK(*rect == QRect(before_corner.x(), before_corner.y(), before_corner.width() + 100, before_corner.height() + 10));
}

void ui_crop_alt_handle_drag_resizes_about_center() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action(window, "toolCropAction")->trigger();
  QApplication::processEvents();
  canvas->set_snap_enabled(false);
  canvas->set_crop_ratio(0.0, 0.0);

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(100, 100)),
       canvas->widget_position_for_document_point(QPoint(200, 180)));
  auto rect = canvas->crop_session_rect();
  CHECK(rect.has_value());
  CHECK(*rect == QRect(100, 100, 101, 81));

  // Alt pressed after the grab: the handle still drives the drag.
  const auto alt_drag = [&](QPoint from_document, QPoint to_document) {
    const auto from = canvas->widget_position_for_document_point(from_document);
    const auto to = canvas->widget_position_for_document_point(to_document);
    send_mouse(*canvas, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseMove, (from + to) / 2, Qt::NoButton, Qt::LeftButton, Qt::AltModifier);
    send_mouse(*canvas, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton, Qt::AltModifier);
    send_mouse(*canvas, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton, Qt::AltModifier);
    QApplication::processEvents();
  };
  const auto within = [](int actual, int expected) { return std::abs(actual - expected) <= 1; };

  // Bottom-right corner, center (150.5, 140.5): both sides grow equally.
  alt_drag(QPoint(201, 181), QPoint(240, 220));
  rect = canvas->crop_session_rect();
  CHECK(rect.has_value());
  CHECK(within(rect->width(), 179));
  CHECK(within(rect->height(), 159));
  CHECK(within(rect->x() + rect->width() / 2, 150));
  CHECK(within(rect->y() + rect->height() / 2, 140));

  // Left edge: the width grows on both sides, the height stays.
  const auto before_edge = *rect;
  alt_drag(QPoint(before_edge.x(), before_edge.y() + before_edge.height() / 2), QPoint(before_edge.x() - 20, 300));
  rect = canvas->crop_session_rect();
  CHECK(rect.has_value());
  CHECK(within(rect->width(), before_edge.width() + 40));
  CHECK(rect->height() == before_edge.height());
  CHECK(within(rect->x() + rect->width() / 2, before_edge.x() + before_edge.width() / 2));

  // A 2:1 ratio holds about the center too.
  canvas->set_crop_ratio(2.0, 1.0);
  const auto before_ratio = *rect;
  alt_drag(QPoint(before_ratio.x() + before_ratio.width(), before_ratio.y() + before_ratio.height()),
           QPoint(before_ratio.x() + before_ratio.width() + 20, before_ratio.y() + before_ratio.height() + 20));
  rect = canvas->crop_session_rect();
  CHECK(rect.has_value());
  CHECK(std::abs(rect->width() - rect->height() * 2) <= 2);
  CHECK(within(rect->x() + rect->width() / 2, before_ratio.x() + before_ratio.width() / 2));
  CHECK(within(rect->y() + rect->height() / 2, before_ratio.y() + before_ratio.height() / 2));
  send_key(*canvas, Qt::Key_Escape);
  CHECK(canvas->crop_session_rect() == QRect(0, 128, 1024, 512));
}

// Alt mirrors a crop drag-out about the press point, whether held from the
// press or pressed mid-drag; releasing it re-anchors at the press corner, and a
// set ratio still constrains the mirrored box (GitHub issue 78).
void ui_crop_alt_drag_out_draws_from_center() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action(window, "toolCropAction")->trigger();
  QApplication::processEvents();
  canvas->set_snap_enabled(false);
  canvas->set_crop_ratio(0.0, 0.0);
  const auto at = [&](int x, int y) { return canvas->widget_position_for_document_point(QPoint(x, y)); };
  const auto within = [](int actual, int expected) { return std::abs(actual - expected) <= 1; };
  const auto reset = [&] {
    send_key(*canvas, Qt::Key_Escape);
    QApplication::processEvents();
  };

  // Alt held from the press: the 100x80 drag extent doubles around (300, 300).
  send_mouse(*canvas, QEvent::MouseButtonPress, at(300, 300), Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
  send_mouse(*canvas, QEvent::MouseMove, at(400, 380), Qt::NoButton, Qt::LeftButton, Qt::AltModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, at(400, 380), Qt::LeftButton, Qt::NoButton, Qt::AltModifier);
  QApplication::processEvents();
  auto rect = canvas->crop_session_rect();
  CHECK(rect.has_value());
  if (rect.has_value()) {
    CHECK(within(rect->x(), 200));
    CHECK(within(rect->y(), 220));
    CHECK(within(rect->width(), 200));
    CHECK(within(rect->height(), 160));
  }
  reset();

  // Alt pressed after the drag starts, as a key event with the pointer still,
  // then kept through the rest of the drag.
  send_mouse(*canvas, QEvent::MouseButtonPress, at(300, 300), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, at(350, 340), Qt::NoButton, Qt::LeftButton);
  send_key_press(*canvas, Qt::Key_Alt);
  send_mouse(*canvas, QEvent::MouseMove, at(400, 380), Qt::NoButton, Qt::LeftButton, Qt::AltModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, at(400, 380), Qt::LeftButton, Qt::NoButton, Qt::AltModifier);
  send_key_release(*canvas, Qt::Key_Alt, Qt::AltModifier);
  QApplication::processEvents();
  rect = canvas->crop_session_rect();
  CHECK(rect.has_value());
  if (rect.has_value()) {
    CHECK(within(rect->x(), 200));
    CHECK(within(rect->y(), 220));
    CHECK(within(rect->width(), 200));
    CHECK(within(rect->height(), 160));
  }
  reset();

  // Alt released mid-drag: the box re-anchors at the press corner.
  send_mouse(*canvas, QEvent::MouseButtonPress, at(300, 300), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, at(350, 340), Qt::NoButton, Qt::LeftButton, Qt::AltModifier);
  send_key_release(*canvas, Qt::Key_Alt, Qt::AltModifier);
  send_mouse(*canvas, QEvent::MouseMove, at(400, 380), Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, at(400, 380), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  rect = canvas->crop_session_rect();
  CHECK(rect.has_value());
  CHECK(rect == QRect(300, 300, 101, 81));
  reset();

  // A 2:1 ratio holds about the center: the 80 px half height wins, so the half
  // width becomes 160.
  canvas->set_crop_ratio(2.0, 1.0);
  send_mouse(*canvas, QEvent::MouseButtonPress, at(300, 300), Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
  send_mouse(*canvas, QEvent::MouseMove, at(400, 380), Qt::NoButton, Qt::LeftButton, Qt::AltModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, at(400, 380), Qt::LeftButton, Qt::NoButton, Qt::AltModifier);
  QApplication::processEvents();
  rect = canvas->crop_session_rect();
  CHECK(rect.has_value());
  if (rect.has_value()) {
    CHECK(within(rect->x(), 140));
    CHECK(within(rect->y(), 220));
    CHECK(within(rect->width(), 320));
    CHECK(within(rect->height(), 160));
  }
  reset();
  CHECK(canvas->crop_session_rect() == QRect(0, 128, 1024, 512));
}

// Space held during a crop handle drag slides the whole box, and releasing it
// resumes the resize from the slid position (the marquee handle rule).
void ui_crop_handle_drag_space_slides_box_then_resumes() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action(window, "toolCropAction")->trigger();
  QApplication::processEvents();
  canvas->set_snap_enabled(false);
  canvas->set_crop_ratio(0.0, 0.0);

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(100, 100)),
       canvas->widget_position_for_document_point(QPoint(200, 180)));
  CHECK(canvas->crop_session_rect() == QRect(100, 100, 101, 81));

  // Right edge handle out by 30.
  send_mouse(*canvas, QEvent::MouseButtonPress, canvas->widget_position_for_document_point(QPoint(201, 140)),
             Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, canvas->widget_position_for_document_point(QPoint(231, 140)),
             Qt::NoButton, Qt::LeftButton);
  CHECK(canvas->crop_session_rect() == QRect(100, 100, 131, 81));
  CHECK(canvas->cursor().shape() == Qt::SizeHorCursor);

  // Space held: the pointer slides the whole box, size intact.
  send_key_press(*canvas, Qt::Key_Space);
  CHECK(canvas->cursor().shape() == Qt::SizeAllCursor);
  send_mouse(*canvas, QEvent::MouseMove, canvas->widget_position_for_document_point(QPoint(251, 155)),
             Qt::NoButton, Qt::LeftButton);
  CHECK(canvas->crop_session_rect() == QRect(120, 115, 131, 81));

  // Space released: the resize resumes from the slid box, the right edge
  // tracking the pointer and the left edge staying put.
  send_key_release(*canvas, Qt::Key_Space);
  CHECK(canvas->cursor().shape() == Qt::SizeHorCursor);
  send_mouse(*canvas, QEvent::MouseMove, canvas->widget_position_for_document_point(QPoint(261, 155)),
             Qt::NoButton, Qt::LeftButton);
  CHECK(canvas->crop_session_rect() == QRect(120, 115, 141, 81));
  send_mouse(*canvas, QEvent::MouseButtonRelease, canvas->widget_position_for_document_point(QPoint(261, 155)),
             Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(canvas->crop_session_rect() == QRect(120, 115, 141, 81));
  CHECK(canvas->crop_session_active());

  // A later handle drag starts clean (no leftover slide state).
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(120, 155)),
       canvas->widget_position_for_document_point(QPoint(110, 155)));
  CHECK(canvas->crop_session_rect() == QRect(110, 115, 151, 81));
}

void ui_crop_rotated_commit_straightens_box() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* info_label = window.findChild<QLabel*>(QStringLiteral("documentInfoLabel"));
  CHECK(info_label != nullptr);
  require_action(window, "toolCropAction")->trigger();
  QApplication::processEvents();
  canvas->set_snap_enabled(false);
  canvas->set_crop_ratio(0.0, 0.0);

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(200, 200)),
       canvas->widget_position_for_document_point(QPoint(320, 280)));
  auto rect = canvas->crop_session_rect();
  CHECK(rect.has_value());
  CHECK(*rect == QRect(200, 200, 121, 81));

  // Shift snaps the rotate gesture to 15-degree steps; a quarter turn around
  // the center (260, 240) lands exactly on 90.
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(400, 240)),
       canvas->widget_position_for_document_point(QPoint(260, 400)), Qt::ShiftModifier);
  CHECK(canvas->crop_session_angle() == 90.0);
  rect = canvas->crop_session_rect();
  CHECK(rect.has_value());
  CHECK(*rect == QRect(200, 200, 121, 81));
  save_widget_artifact("ui_crop_rotated_box", *canvas);

  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  // The new document is framed afresh, upright.
  CHECK(canvas->crop_session_rect() == QRect(0, 0, 121, 81));
  CHECK(canvas->crop_session_angle() == 0.0);
  CHECK(!canvas->crop_session_has_changes());
  CHECK(info_label->text().contains(QStringLiteral("121 x 81 px")));

  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(info_label->text().contains(QStringLiteral("1024 x 768 px")));
  CHECK(canvas->crop_session_rect() == QRect(0, 0, 1024, 768));
}

void ui_crop_enter_commits_expanding_document() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* info_label = window.findChild<QLabel*>(QStringLiteral("documentInfoLabel"));
  CHECK(info_label != nullptr);
  CHECK(info_label->text().contains(QStringLiteral("1024 x 768 px")));
  canvas->set_secondary_color(QColor(30, 200, 90));
  const auto original_corner = canvas_pixel(*canvas, QPoint(1000, 750));

  require_action(window, "toolCropAction")->trigger();
  QApplication::processEvents();
  canvas->set_snap_enabled(false);
  canvas->set_crop_ratio(0.0, 0.0);
  canvas->zoom_to_document_rect(QRect(850, 650, 320, 220));

  // The rect hangs past the right/bottom canvas edges onto the pasteboard.
  // (At a fractional zoom the widget-to-document mapping can wobble a pixel,
  // so the expectations derive from the actual session rect.)
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(900, 700)),
       canvas->widget_position_for_document_point(QPoint(1100, 800)));
  auto rect = canvas->crop_session_rect();
  CHECK(rect.has_value());
  CHECK(std::abs(rect->x() - 900) <= 2);
  CHECK(std::abs(rect->y() - 700) <= 2);
  CHECK(rect->x() + rect->width() > 1024);
  CHECK(rect->y() + rect->height() > 768);
  const auto expected_info =
      QStringLiteral("%1 x %2 px").arg(rect->width()).arg(rect->height());

  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(canvas->crop_session_rect() == QRect(0, 0, rect->width(), rect->height()));
  CHECK(!canvas->crop_session_has_changes());
  CHECK(info_label->text().contains(expected_info));
  // Old canvas content lands at the origin; the expansion under the Background
  // layer is filled with the background (secondary) color.
  CHECK(color_close(canvas_pixel(*canvas, QPoint(20, 20)), Qt::white, 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(160, 80)), QColor(30, 200, 90), 8));
  save_widget_artifact("ui_crop_expanding_commit", *canvas);

  // The whole-document snapshot restores dimensions and pixels on undo.
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(info_label->text().contains(QStringLiteral("1024 x 768 px")));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(1000, 750)), original_corner, 8));
}

void ui_crop_escape_resets_and_tool_switch_cancels() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* info_label = window.findChild<QLabel*>(QStringLiteral("documentInfoLabel"));
  CHECK(info_label != nullptr);
  require_action(window, "toolCropAction")->trigger();
  QApplication::processEvents();
  canvas->set_snap_enabled(false);
  canvas->set_crop_ratio(0.0, 0.0);

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(50, 50)),
       canvas->widget_position_for_document_point(QPoint(150, 130)));
  CHECK(canvas->crop_session_has_changes());
  send_key(*canvas, Qt::Key_Escape);
  CHECK(canvas->crop_session_active());
  CHECK(!canvas->crop_session_has_changes());
  CHECK(canvas->crop_session_rect() == QRect(0, 0, 1024, 768));
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Crop box reset to the canvas"));
  CHECK(info_label->text().contains(QStringLiteral("1024 x 768 px")));

  // Re-picking the Crop tool keeps the box; a real switch cancels the session
  // without committing.
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(50, 50)),
       canvas->widget_position_for_document_point(QPoint(150, 130)));
  CHECK(canvas->crop_session_rect() == QRect(50, 50, 101, 81));
  require_action(window, "toolCropAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->crop_session_rect() == QRect(50, 50, 101, 81));
  require_action(window, "toolMoveAction")->trigger();
  QApplication::processEvents();
  CHECK(!canvas->crop_session_active());
  CHECK(info_label->text().contains(QStringLiteral("1024 x 768 px")));
}

void ui_crop_apply_cancel_buttons_follow_session() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* info_label = window.findChild<QLabel*>(QStringLiteral("documentInfoLabel"));
  CHECK(info_label != nullptr);
  require_action(window, "toolCropAction")->trigger();
  QApplication::processEvents();
  canvas->set_snap_enabled(false);
  canvas->set_crop_ratio(0.0, 0.0);

  auto* apply = window.findChild<QPushButton*>(QStringLiteral("cropApplyButton"));
  auto* cancel = window.findChild<QPushButton*>(QStringLiteral("cropCancelButton"));
  CHECK(apply != nullptr);
  CHECK(cancel != nullptr);
  CHECK(apply->isVisible());
  // The untouched canvas frame has nothing to apply or reset.
  CHECK(canvas->crop_session_active());
  CHECK(!apply->isEnabled());
  CHECK(!cancel->isEnabled());

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(100, 100)),
       canvas->widget_position_for_document_point(QPoint(400, 300)));
  CHECK(canvas->crop_session_has_changes());
  CHECK(apply->isEnabled());
  CHECK(cancel->isEnabled());

  // The X resets the box to the canvas frame, like Esc.
  cancel->click();
  QApplication::processEvents();
  CHECK(canvas->crop_session_rect() == QRect(0, 0, 1024, 768));
  CHECK(!apply->isEnabled());
  CHECK(!cancel->isEnabled());

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(100, 100)),
       canvas->widget_position_for_document_point(QPoint(400, 300)));
  const auto committed = canvas->crop_session_rect();
  CHECK(committed.has_value());
  apply->click();
  QApplication::processEvents();
  CHECK(canvas->crop_session_rect() == QRect(0, 0, committed->width(), committed->height()));
  CHECK(!apply->isEnabled());
  CHECK(info_label->text().contains(
      QStringLiteral("%1 x %2 px").arg(committed->width()).arg(committed->height())));

  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(info_label->text().contains(QStringLiteral("1024 x 768 px")));
}

// With a ratio set the automatic frame is smaller than the canvas: Apply crops
// to it, but the X has nothing to reset and stays off, also right after a reset
// through the X or Esc (GitHub issue 318). Size style frames the whole canvas,
// so both buttons start off there.
void ui_crop_cancel_button_off_for_ratio_frame() {
  SettingsValueRestorer saved_style(QStringLiteral("tools/cropStyle"));
  SettingsValueRestorer saved_ratio_w(QStringLiteral("tools/cropRatioWidth"));
  SettingsValueRestorer saved_ratio_h(QStringLiteral("tools/cropRatioHeight"));
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action(window, "toolCropAction")->trigger();
  QApplication::processEvents();
  canvas->set_snap_enabled(false);

  auto* style = window.findChild<QComboBox*>(QStringLiteral("cropPresetCombo"));
  auto* ratio_w = window.findChild<QDoubleSpinBox*>(QStringLiteral("cropRatioWidthSpin"));
  auto* ratio_h = window.findChild<QDoubleSpinBox*>(QStringLiteral("cropRatioHeightSpin"));
  auto* apply = window.findChild<QPushButton*>(QStringLiteral("cropApplyButton"));
  auto* cancel = window.findChild<QPushButton*>(QStringLiteral("cropCancelButton"));
  CHECK(style != nullptr);
  CHECK(ratio_w != nullptr);
  CHECK(ratio_h != nullptr);
  CHECK(apply != nullptr);
  CHECK(cancel != nullptr);
  if (style == nullptr || ratio_w == nullptr || ratio_h == nullptr || apply == nullptr || cancel == nullptr) {
    return;
  }
  style->setCurrentIndex(crop_preset_index(*style, CropKind::FreeRatio));
  ratio_w->setValue(16.0);
  ratio_h->setValue(9.0);
  QApplication::processEvents();
  const QRect fitted(0, 96, 1024, 576);
  CHECK(canvas->crop_session_rect() == fitted);
  CHECK(canvas->crop_session_has_changes());
  CHECK(!canvas->crop_session_can_reset());
  CHECK(apply->isEnabled());
  CHECK(!cancel->isEnabled());

  // A handle drag makes the box custom: the X turns on.
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(1024, 384)),
       canvas->widget_position_for_document_point(QPoint(824, 384)));
  CHECK(canvas->crop_session_rect() != fitted);
  CHECK(canvas->crop_session_can_reset());
  CHECK(apply->isEnabled());
  CHECK(cancel->isEnabled());

  // The X puts the fitted frame back and turns itself off, not left pressed.
  cancel->click();
  QApplication::processEvents();
  CHECK(canvas->crop_session_rect() == fitted);
  CHECK(apply->isEnabled());
  CHECK(!cancel->isEnabled());
  CHECK(!cancel->isDown());

  // Esc does the same after a drag-out.
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(100, 100)),
       canvas->widget_position_for_document_point(QPoint(400, 300)));
  CHECK(cancel->isEnabled());
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(canvas->crop_session_rect() == fitted);
  CHECK(apply->isEnabled());
  CHECK(!cancel->isEnabled());

  // Size style frames the whole canvas: nothing to apply or reset.
  style->setCurrentIndex(crop_preset_index(*style, CropKind::FreeSize));
  QApplication::processEvents();
  CHECK(canvas->crop_session_rect() == QRect(0, 0, 1024, 768));
  CHECK(!apply->isEnabled());
  CHECK(!cancel->isEnabled());
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(1024, 384)),
       canvas->widget_position_for_document_point(QPoint(824, 384)));
  CHECK(apply->isEnabled());
  CHECK(cancel->isEnabled());
  cancel->click();
  QApplication::processEvents();
  CHECK(canvas->crop_session_rect() == QRect(0, 0, 1024, 768));
  CHECK(!apply->isEnabled());
  CHECK(!cancel->isEnabled());
  CHECK(!cancel->isDown());
}

void ui_crop_overlay_renders_shield_and_thirds() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action(window, "toolCropAction")->trigger();
  QApplication::processEvents();
  canvas->set_snap_enabled(false);
  canvas->set_crop_ratio(0.0, 0.0);

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(200, 200)),
       canvas->widget_position_for_document_point(QPoint(500, 440)));
  CHECK(canvas->crop_session_active());
  QApplication::processEvents();

  // Both points sit over the white Background; the one outside the crop rect
  // reads darker through the shield.
  const auto image = render_widget_image(*canvas);
  const auto inside = image.pixelColor(canvas->widget_position_for_document_point(QPoint(350, 320)));
  const auto outside = image.pixelColor(canvas->widget_position_for_document_point(QPoint(60, 60)));
  CHECK(inside.value() > outside.value() + 60);
  save_widget_artifact("ui_crop_overlay_shield", *canvas);
  send_key(*canvas, Qt::Key_Escape);
}

// The Crop options popup (GitHub issue 326): the shield's color and opacity
// tint the area outside the box, the shield toggle leaves it untouched, Show
// Cropped Area off hides it behind the backdrop, and every value persists.
void ui_crop_options_popup_sets_shield_and_cropped_area() {
  SettingsValueRestorer saved_show(QStringLiteral("tools/cropShowCroppedArea"));
  SettingsValueRestorer saved_enabled(QStringLiteral("tools/cropShieldEnabled"));
  SettingsValueRestorer saved_color(QStringLiteral("tools/cropShieldColor"));
  SettingsValueRestorer saved_opacity(QStringLiteral("tools/cropShieldOpacity"));
  for (const auto* key : {"tools/cropShowCroppedArea", "tools/cropShieldEnabled", "tools/cropShieldColor",
                          "tools/cropShieldOpacity"}) {
    patchy::ui::app_settings().remove(QLatin1String(key));
  }
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action(window, "toolCropAction")->trigger();
  QApplication::processEvents();
  canvas->set_snap_enabled(false);
  canvas->set_crop_ratio(0.0, 0.0);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(200, 200)),
       canvas->widget_position_for_document_point(QPoint(500, 440)));
  CHECK(canvas->crop_session_active());
  QApplication::processEvents();

  auto* options_button = window.findChild<QToolButton*>(QStringLiteral("cropOptionsButton"));
  auto* show_area = window.findChild<QAction*>(QStringLiteral("cropShowCroppedAreaAction"));
  auto* shield = window.findChild<QAction*>(QStringLiteral("cropShieldAction"));
  auto* color_button = window.findChild<QPushButton*>(QStringLiteral("cropShieldColorButton"));
  auto* opacity = window.findChild<QSpinBox*>(QStringLiteral("cropShieldOpacitySpin"));
  CHECK(options_button != nullptr && options_button->isVisible() && options_button->menu() != nullptr);
  CHECK(show_area != nullptr && shield != nullptr && color_button != nullptr && opacity != nullptr);
  if (options_button == nullptr || options_button->menu() == nullptr || show_area == nullptr || shield == nullptr ||
      color_button == nullptr || opacity == nullptr) {
    return;
  }
  // Defaults are the original look: shield on, black, 47% (alpha 120).
  CHECK(show_area->isChecked() && shield->isChecked());
  CHECK(opacity->value() == 47);
  CHECK(canvas->crop_overlay_options() == patchy::ui::CropOverlayOptions{});

  const auto inside_point = canvas->widget_position_for_document_point(QPoint(350, 320));
  const auto outside_point = canvas->widget_position_for_document_point(QPoint(60, 60));
  const auto outside_color = [&] { return render_widget_image(*canvas).pixelColor(outside_point); };
  const auto color_near = [](QColor actual, QColor expected) {
    return std::abs(actual.red() - expected.red()) <= 3 && std::abs(actual.green() - expected.green()) <= 3 &&
           std::abs(actual.blue() - expected.blue()) <= 3;
  };
  CHECK(color_near(outside_color(), QColor(135, 135, 135)));

  // Color through the picker, then the opacity spin.
  bool picked = false;
  std::function<void()> pick_when_open;
  pick_when_open = guarded([&] {
    auto* picker = patchy::ui::open_color_picker_dialog();
    if (picker == nullptr) {
      QTimer::singleShot(10, &window, pick_when_open);
      return;
    }
    CHECK(patchy::ui::apply_color_to_open_color_picker(QColor(200, 0, 0)));
    picked = true;
    picker->accept();
  });
  color_button->click();
  QTimer::singleShot(0, &window, pick_when_open);
  QElapsedTimer timer;
  timer.start();
  while (!picked && timer.elapsed() < 5000) {
    QApplication::processEvents(QEventLoop::AllEvents, 20);
  }
  QApplication::processEvents();
  CHECK(picked);
  CHECK(canvas->crop_overlay_options().shield_color == QColor(200, 0, 0));
  opacity->setValue(100);
  QApplication::processEvents();
  CHECK(color_near(outside_color(), QColor(200, 0, 0)));
  opacity->setValue(60);
  QApplication::processEvents();
  // White under 60% of (200, 0, 0).
  CHECK(color_near(outside_color(), QColor(222, 102, 102)));
  options_button->menu()->popup(options_button->mapToGlobal(QPoint(0, options_button->height())));
  QApplication::processEvents();
  save_window_popup_artifact("ui_crop_options_popup", window, *options_button, *options_button->menu());
  {
    // The whole window with the open popup painted under its button, for the issue
    // shot (the offscreen screen is narrower than the window, so Qt moves the real
    // popup left to fit it).
    auto shot = window.grab().toImage();
    auto* menu = options_button->menu();
    auto origin = options_button->mapTo(&window, QPoint(0, options_button->height()));
    origin.setX(std::min(origin.x(), window.width() - menu->width()));
    QPainter painter(&shot);
    painter.drawPixmap(origin, menu->grab());
    painter.end();
    save_image_artifact("ui_crop_options_after", shot);
  }
  options_button->menu()->hide();
  QApplication::processEvents();
  save_widget_artifact("ui_crop_options_custom_shield", window);

  // Shield off: the outside reads like the inside, and the shield fields disable.
  shield->setChecked(false);
  QApplication::processEvents();
  CHECK(!color_button->isEnabled() && !opacity->isEnabled());
  CHECK(color_near(outside_color(), QColor(255, 255, 255)));

  // Show Cropped Area off: the outside is the backdrop, the inside stays the artwork.
  shield->setChecked(true);
  show_area->setChecked(false);
  QApplication::processEvents();
  const auto hidden = render_widget_image(*canvas);
  CHECK(color_near(hidden.pixelColor(outside_point), canvas->backdrop_color()));
  CHECK(color_near(hidden.pixelColor(inside_point), QColor(255, 255, 255)));
  save_widget_artifact("ui_crop_options_hide_cropped_area", *canvas);

  // Everything persists into a fresh window.
  timer.restart();
  while (patchy::ui::app_settings().value(QStringLiteral("tools/cropShowCroppedArea")).toString() !=
             QStringLiteral("false") &&
         timer.elapsed() < 3000) {
    QApplication::processEvents(QEventLoop::AllEvents, 20);
  }
  send_key(*canvas, Qt::Key_Escape);
  patchy::ui::MainWindow second;
  show_window(second);
  auto* second_canvas = require_canvas(second);
  patchy::ui::CropOverlayOptions expected;
  expected.show_cropped_area = false;
  expected.shield_color = QColor(200, 0, 0);
  expected.shield_opacity_percent = 60;
  CHECK(second_canvas->crop_overlay_options() == expected);
  auto* second_opacity = second.findChild<QSpinBox*>(QStringLiteral("cropShieldOpacitySpin"));
  auto* second_show = second.findChild<QAction*>(QStringLiteral("cropShowCroppedAreaAction"));
  CHECK(second_opacity != nullptr && second_opacity->value() == 60);
  CHECK(second_show != nullptr && !second_show->isChecked());
}

// GitHub issue 325: the Overlay combo picks the guides inside the box. Each
// choice draws its lines where it should (thirds at 1/3, golden ratio at 0.382
// and 0.618, diagonals through the corners, the spiral's first division at
// 0.618), Turn Overlay flips the Triangle and the Golden Spiral, "While
// Dragging" hides the guides until a handle is held, and all three choices
// survive a new window. The issue shots use a photo.
void ui_crop_overlay_choices_draw_their_guides() {
  SettingsValueRestorer saved_overlay(QStringLiteral("tools/cropOverlay"));
  SettingsValueRestorer saved_orientation(QStringLiteral("tools/cropOverlayOrientation"));
  SettingsValueRestorer saved_show(QStringLiteral("tools/cropOverlayShow"));
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("tools/cropOverlay"));
    settings.remove(QStringLiteral("tools/cropOverlayOrientation"));
    settings.remove(QStringLiteral("tools/cropOverlayShow"));
  }
  using Overlay = patchy::ui::CanvasWidget::CropOverlay;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action(window, "toolCropAction")->trigger();
  QApplication::processEvents();
  canvas->set_crop_ratio(0.0, 0.0);
  auto* combo = window.findChild<QComboBox*>(QStringLiteral("cropOverlayCombo"));
  auto* turn = window.findChild<QAction*>(QStringLiteral("cropOverlayTurnAction"));
  auto* options = window.findChild<QToolButton*>(QStringLiteral("cropOptionsButton"));
  auto* show_always = window.findChild<QAction*>(QStringLiteral("cropOverlayShowAlwaysAction"));
  auto* show_dragging = window.findChild<QAction*>(QStringLiteral("cropOverlayShowWhileDraggingAction"));
  CHECK(combo != nullptr && turn != nullptr && options != nullptr && show_always != nullptr &&
        show_dragging != nullptr);
  if (combo == nullptr || turn == nullptr || options == nullptr || show_always == nullptr ||
      show_dragging == nullptr) {
    return;
  }
  CHECK(combo->isVisible());
  CHECK(combo->count() == 7);
  const QStringList expected_items = {QStringLiteral("No Overlay"), QStringLiteral("Rule of Thirds"),
                                      QStringLiteral("Grid"),     QStringLiteral("Diagonal"),
                                      QStringLiteral("Triangle"), QStringLiteral("Golden Ratio"),
                                      QStringLiteral("Golden Spiral")};
  for (int i = 0; i < std::min(combo->count(), static_cast<int>(expected_items.size())); ++i) {
    CHECK(combo->itemText(i) == expected_items[i]);
  }
  // Rule of Thirds by default, shown all the time: today's look.
  CHECK(combo->currentIndex() == 1);
  CHECK(canvas->crop_overlay() == Overlay::Thirds);
  CHECK(!canvas->crop_overlay_only_while_dragging());
  CHECK(show_always->isChecked());
  CHECK(!turn->isEnabled());

  // A 500 x 360 box over the white canvas, inside the offscreen view (the
  // 1024 x 768 canvas runs past its right edge at 100%). Guides read through
  // their dark twin (the light line vanishes on white). A guide counts as
  // present when a pixel within 2 px of the point differs from the None render.
  canvas->set_snap_enabled(false);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(100, 100)),
       canvas->widget_position_for_document_point(QPoint(600, 460)));
  const auto box = canvas->crop_session_rect();
  CHECK(box.has_value());
  if (!box.has_value()) {
    return;
  }
  const auto top_left = canvas->widget_position_for_document_point(box->topLeft());
  const auto bottom_right =
      canvas->widget_position_for_document_point(box->topLeft() + QPoint(box->width(), box->height()));
  CHECK(QRect(top_left, bottom_right).intersected(canvas->rect()) == QRect(top_left, bottom_right));
  const auto at = [&](double u, double v) {
    return QPoint(static_cast<int>(std::lround(top_left.x() + u * (bottom_right.x() - top_left.x()))),
                  static_cast<int>(std::lround(top_left.y() + v * (bottom_right.y() - top_left.y()))));
  };
  const auto render = [&] {
    QApplication::processEvents();
    return render_widget_image(*canvas);
  };
  combo->setCurrentIndex(0);
  CHECK(canvas->crop_overlay() == Overlay::None);
  CHECK(options->isEnabled());  // the shield options stay reachable
  CHECK(!show_always->isEnabled() && !show_dragging->isEnabled());
  const auto plain = render();
  // The largest brightness change within 2 px of the point.
  const auto change_near = [&](const QImage& image, double u, double v) {
    const auto center = at(u, v);
    int largest = 0;
    for (int dy = -2; dy <= 2; ++dy) {
      for (int dx = -2; dx <= 2; ++dx) {
        const QPoint point(center.x() + dx, center.y() + dy);
        if (image.rect().contains(point)) {
          largest = std::max(largest, std::abs(image.pixelColor(point).value() - plain.pixelColor(point).value()));
        }
      }
    }
    return largest;
  };
  // Misses are collected so one run reports every one of them.
  std::vector<std::string> misses;
  const auto expect = [&](const char* name, const QImage& image, double u, double v, bool present) {
    const auto change = change_near(image, u, v);
    if ((change > 20) != present) {
      char line[160];
      std::snprintf(line, sizeof(line), "%s: guide at (%.3f, %.3f) %s (change %d)", name, u, v,
                    present ? "missing" : "unexpected", change);
      misses.emplace_back(line);
    }
  };

  combo->setCurrentIndex(1);
  CHECK(show_always->isEnabled() && show_dragging->isEnabled());
  const auto thirds = render();
  expect("thirds", thirds, 1.0 / 3.0, 0.2, true);
  expect("thirds", thirds, 2.0 / 3.0, 0.2, true);
  expect("thirds", thirds, 0.1, 1.0 / 3.0, true);
  expect("thirds", thirds, 0.1, 2.0 / 3.0, true);
  expect("thirds", thirds, 0.382, 0.2, false);
  expect("thirds", thirds, 0.25, 0.25, false);

  // 1024 x 768: six rows and eight columns of near-square cells.
  combo->setCurrentIndex(2);
  CHECK(canvas->crop_overlay() == Overlay::Grid);
  const auto grid = render();
  expect("grid", grid, 1.0 / 8.0, 0.25, true);
  expect("grid", grid, 3.0 / 8.0, 0.25, true);
  expect("grid", grid, 0.06, 1.0 / 6.0, true);
  expect("grid", grid, 0.06, 0.5, true);
  expect("grid", grid, 1.0 / 3.0, 0.25, false);

  combo->setCurrentIndex(3);
  const auto diagonal = render();
  expect("diagonal", diagonal, 0.25, 0.25, true);
  expect("diagonal", diagonal, 0.75, 0.25, true);
  expect("diagonal", diagonal, 0.6, 0.6, true);
  expect("diagonal", diagonal, 1.0 / 3.0, 0.2, false);

  combo->setCurrentIndex(5);
  CHECK(canvas->crop_overlay() == Overlay::GoldenRatio);
  CHECK(!turn->isEnabled());
  const auto golden = render();
  expect("golden ratio", golden, 0.382, 0.2, true);
  expect("golden ratio", golden, 0.618, 0.2, true);
  expect("golden ratio", golden, 0.1, 0.382, true);
  expect("golden ratio", golden, 0.1, 0.618, true);
  expect("golden ratio", golden, 1.0 / 3.0, 0.2, false);

  // The spiral's first square fills the left of the box: its edge at 0.618
  // runs the full height, its arc passes (0.181, 0.293). One turn mirrors it.
  combo->setCurrentIndex(6);
  CHECK(turn->isEnabled());
  CHECK(canvas->crop_overlay_orientation() == 0);
  const auto spiral = render();
  expect("spiral", spiral, 0.618, 0.5, true);
  expect("spiral", spiral, 0.181, 0.293, true);
  expect("spiral", spiral, 0.382, 0.5, false);
  turn->trigger();
  CHECK(canvas->crop_overlay_orientation() == 1);
  const auto spiral_turned = render();
  expect("spiral turned", spiral_turned, 0.382, 0.5, true);
  expect("spiral turned", spiral_turned, 0.819, 0.293, true);
  expect("spiral turned", spiral_turned, 0.618, 0.5, false);
  for (int i = 0; i < 3; ++i) {
    turn->trigger();
  }
  CHECK(canvas->crop_overlay_orientation() == 0);

  // Triangle: the top-left to bottom-right diagonal; a turn takes the other.
  combo->setCurrentIndex(4);
  CHECK(turn->isEnabled());
  const auto triangle = render();
  expect("triangle", triangle, 0.25, 0.25, true);
  expect("triangle", triangle, 0.75, 0.25, false);
  turn->trigger();
  const auto triangle_turned = render();
  expect("triangle turned", triangle_turned, 0.75, 0.25, true);
  expect("triangle turned", triangle_turned, 0.25, 0.25, false);

  // While Dragging: nothing at rest, the guides while a handle is held.
  combo->setCurrentIndex(1);
  show_dragging->trigger();
  CHECK(canvas->crop_overlay_only_while_dragging());
  CHECK(show_dragging->isChecked());
  CHECK(!show_always->isChecked());
  const auto resting = render();
  expect("while dragging, at rest", resting, 1.0 / 3.0, 0.2, false);
  const auto right_edge = at(1.0, 0.2);
  send_mouse(*canvas, QEvent::MouseButtonPress, right_edge, Qt::LeftButton, Qt::LeftButton);
  const auto held = render();
  expect("while dragging, held", held, 1.0 / 3.0, 0.2, true);
  send_mouse(*canvas, QEvent::MouseButtonRelease, right_edge, Qt::LeftButton, Qt::NoButton);
  const auto released = render();
  expect("while dragging, released", released, 1.0 / 3.0, 0.2, false);
  for (const auto& miss : misses) {
    std::fprintf(stderr, "  %s\n", miss.c_str());
  }
  CHECK(misses.empty());

  // The three choices persist and a new window starts with them.
  combo->setCurrentIndex(6);
  turn->trigger();
  turn->trigger();
  CHECK(canvas->crop_overlay_orientation() == 3);
  window.close();
  QApplication::processEvents();
  {
    const auto settings = patchy::ui::app_settings();
    CHECK(settings.value(QStringLiteral("tools/cropOverlay")).toString() == QStringLiteral("goldenSpiral"));
    CHECK(settings.value(QStringLiteral("tools/cropOverlayOrientation")).toInt() == 3);
    CHECK(settings.value(QStringLiteral("tools/cropOverlayShow")).toString() == QStringLiteral("dragging"));
  }
  {
    patchy::ui::MainWindow second;
    show_window(second);
    auto* second_canvas = require_canvas(second);
    auto* second_combo = second.findChild<QComboBox*>(QStringLiteral("cropOverlayCombo"));
    auto* second_dragging = second.findChild<QAction*>(QStringLiteral("cropOverlayShowWhileDraggingAction"));
    CHECK(second_combo != nullptr && second_dragging != nullptr);
    if (second_combo != nullptr && second_dragging != nullptr) {
      CHECK(second_combo->currentIndex() == 6);
      CHECK(second_dragging->isChecked());
    }
    CHECK(second_canvas->crop_overlay() == Overlay::GoldenSpiral);
    CHECK(second_canvas->crop_overlay_orientation() == 3);
    CHECK(second_canvas->crop_overlay_only_while_dragging());
    second.close();
    QApplication::processEvents();
  }
  // An unknown token falls back to Rule of Thirds.
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("tools/cropOverlay"), QStringLiteral("spirograph"));
    settings.setValue(QStringLiteral("tools/cropOverlayOrientation"), 9);
    settings.remove(QStringLiteral("tools/cropOverlayShow"));
    settings.sync();
  }
  patchy::ui::MainWindow third;
  show_window(third);
  auto* third_canvas = require_canvas(third);
  CHECK(third_canvas->crop_overlay() == Overlay::Thirds);
  CHECK(third_canvas->crop_overlay_orientation() == 3);
  CHECK(!third_canvas->crop_overlay_only_while_dragging());

  // Issue shots: a photo with the open Overlay list, then three overlays.
  const auto photo = patchy::test::committed_format_fixture_path("readme", "havana_vintage_car_cc0.jpg");
  patchy::ui::MainWindowTestAccess::open_document_path(third, QString::fromStdString(photo.string()));
  QApplication::processEvents();
  auto* photo_canvas = require_canvas(third);
  require_action(third, "toolCropAction")->trigger();
  QApplication::processEvents();
  auto* third_combo = third.findChild<QComboBox*>(QStringLiteral("cropOverlayCombo"));
  auto* third_turn = third.findChild<QAction*>(QStringLiteral("cropOverlayTurnAction"));
  CHECK(third_combo != nullptr && third_turn != nullptr);
  if (third_combo == nullptr || third_turn == nullptr) {
    return;
  }
  third_combo->setCurrentIndex(1);
  QApplication::processEvents();
  third_combo->showPopup();
  QApplication::processEvents();
  if (auto* popup = third_combo->view() != nullptr ? third_combo->view()->window() : nullptr; popup != nullptr) {
    // The list's container keeps Qt's default background under the themed view,
    // which save_window_popup_artifact's theme check refuses; compose it here
    // like the issue 235 blend-list shot does.
    auto shot = third.grab().toImage();
    const auto origin = third.mapToGlobal(QPoint(0, 0));
    const auto popup_position = popup->mapToGlobal(QPoint(0, 0)) - origin;
    {
      QPainter painter(&shot);
      painter.drawPixmap(popup_position, popup->grab());
    }
    const auto field_position = third_combo->mapToGlobal(QPoint(0, 0)) - origin;
    const auto area = QRect(field_position, third_combo->size())
                          .united(QRect(popup_position, popup->size()))
                          .adjusted(-170, -14, 220, 14)
                          .intersected(shot.rect());
    save_stacked_artifact("issue_325_overlay_menu", {shot.copy(area)});
  }
  third_combo->hidePopup();
  QApplication::processEvents();
  for (const auto& [index, name] : {std::pair{2, "issue_325_grid"}, std::pair{6, "issue_325_golden_spiral"},
                                    std::pair{4, "issue_325_triangle"}, std::pair{1, "issue_325_thirds_photo"}}) {
    third_combo->setCurrentIndex(index);
    if (index == 6 && photo_canvas->crop_overlay_orientation() != 0) {
      while (photo_canvas->crop_overlay_orientation() != 0) {
        third_turn->trigger();
      }
    }
    QApplication::processEvents();
    save_widget_artifact(name, *photo_canvas);
  }
}

// Image > Crop to Selection and Crop to Selection (Advanced) act on the Crop
// tool's pending box while the tool is active: the plain command commits like
// Enter, the advanced one prefills the Canvas Size dialog with the box, and the
// untouched canvas frame with no selection is refused with a message. A marquee
// selection still wins while the box is the automatic frame (after Esc), and a
// selection made under the tool (Select All) re-frames the box.
void ui_crop_menu_commands_use_crop_box() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* info_label = window.findChild<QLabel*>(QStringLiteral("documentInfoLabel"));
  CHECK(info_label != nullptr);
  require_action(window, "toolCropAction")->trigger();
  QApplication::processEvents();
  canvas->set_snap_enabled(false);
  canvas->set_crop_ratio(0.0, 0.0);
  CHECK(!canvas->has_selection());

  // The default frame is the whole canvas: nothing to crop, nothing changes.
  require_action(window, "imageCropToSelectionAction")->trigger();
  QApplication::processEvents();
  CHECK(info_label->text().contains(QStringLiteral("1024 x 768 px")));
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Nothing to crop: the box matches the canvas"));
  require_action(window, "imageCropToSelectionAdvancedAction")->trigger();
  QApplication::processEvents();
  CHECK(info_label->text().contains(QStringLiteral("1024 x 768 px")));
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Nothing to crop: the crop box matches the canvas"));

  // The plain command commits the laid-out box.
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(50, 50)),
       canvas->widget_position_for_document_point(QPoint(350, 250)));
  CHECK(canvas->crop_session_rect() == QRect(50, 50, 301, 201));
  require_action(window, "imageCropToSelectionAction")->trigger();
  QApplication::processEvents();
  CHECK(info_label->text().contains(QStringLiteral("301 x 201 px")));
  CHECK(canvas->crop_session_active());
  CHECK(canvas->crop_session_rect() == QRect(0, 0, 301, 201));
  CHECK(!canvas->crop_session_has_changes());

  // The advanced command opens the Canvas Size dialog prefilled with the box
  // (its two layer options; the tool's own pair lives on the options bar).
  canvas->zoom_to_document_rect(QRect(0, 0, 301, 201));
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(40, 30)),
       canvas->widget_position_for_document_point(QPoint(160, 110)));
  // (At a fractional zoom the widget-to-document mapping can wobble a pixel,
  // so the expectations derive from the actual session rect.)
  const auto box = canvas->crop_session_rect();
  CHECK(box.has_value());
  CHECK(std::abs(box->x() - 40) <= 2 && std::abs(box->y() - 30) <= 2);
  CHECK(std::abs(box->width() - 121) <= 2 && std::abs(box->height() - 81) <= 2);
  const auto expected_info = QStringLiteral("%1 x %2 px").arg(box->width()).arg(box->height());
  bool drove_dialog = false;
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyCanvasSizeDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* width = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("canvasSizeWidthSpin"));
    auto* height = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("canvasSizeHeightSpin"));
    CHECK(width != nullptr && height != nullptr);
    CHECK(dialog->windowTitle() == QStringLiteral("Crop to Selection (Advanced)"));
    CHECK(width->value() == static_cast<double>(box->width()));
    CHECK(height->value() == static_cast<double>(box->height()));
    drove_dialog = true;
    dialog->accept();
  }));
  require_action(window, "imageCropToSelectionAdvancedAction")->trigger();
  QApplication::processEvents();
  CHECK(drove_dialog);
  CHECK(info_label->text().contains(expected_info));
  CHECK(canvas->crop_session_rect() == QRect(0, 0, box->width(), box->height()));

  // A rotated box is refused by the advanced command (the dialog cannot straighten).
  canvas->zoom_to_document_rect(QRect(0, 0, box->width(), box->height()));
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(30, 25)),
       canvas->widget_position_for_document_point(QPoint(90, 60)));
  CHECK(canvas->crop_session_has_changes());
  CHECK(canvas->crop_session_angle() == 0.0);
  // A drag off the custom box rotates it about its center.
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(110, 70)),
       canvas->widget_position_for_document_point(QPoint(110, 15)));
  CHECK(canvas->crop_session_angle() != 0.0);
  require_action(window, "imageCropToSelectionAdvancedAction")->trigger();
  QApplication::processEvents();
  CHECK(info_label->text().contains(expected_info));
  CHECK(window.statusBar()->currentMessage().startsWith(QStringLiteral("Crop to Selection (Advanced) cannot")));
  send_key(*canvas, Qt::Key_Escape);
  CHECK(!canvas->crop_session_has_changes());

  // Select All under the tool re-frames the box from the selection (the
  // whole canvas here, so still nothing to apply); a marquee drawn with the
  // Marquee tool is adopted on the way back to Crop, Esc puts the canvas frame
  // back with the marquee visible again, and the plain command then crops to
  // the marquee rather than reporting nothing to crop.
  canvas->select_all();
  QApplication::processEvents();
  CHECK(canvas->has_selection());
  CHECK(canvas->crop_session_rect() == QRect(0, 0, box->width(), box->height()));
  canvas->clear_selection();
  QApplication::processEvents();
  CHECK(!canvas->has_selection());
  CHECK(canvas->crop_session_rect() == QRect(0, 0, box->width(), box->height()));
  const auto doc_w = box->width();
  const auto doc_h = box->height();
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(10, 10)),
       canvas->widget_position_for_document_point(QPoint(doc_w - 20, doc_h - 10)));
  const auto marquee = canvas->selected_document_rect();
  CHECK(marquee.has_value() && marquee->width() < doc_w && marquee->height() < doc_h);
  require_action(window, "toolCropAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->crop_session_rect() == marquee);
  CHECK(canvas->crop_session_has_changes());
  send_key(*canvas, Qt::Key_Escape);
  CHECK(!canvas->crop_session_has_changes());
  CHECK(canvas->selected_document_rect() == marquee);
  require_action(window, "imageCropToSelectionAction")->trigger();
  QApplication::processEvents();
  CHECK(info_label->text().contains(QStringLiteral("%1 x %2 px").arg(marquee->width()).arg(marquee->height())));
  CHECK(!canvas->has_selection());
}

// The options-bar layer options: "Delete Cropped Pixels" (tools/cropDeletePixels,
// on by default like Photoshop's) and "Delete Off-Canvas Layers"
// (tools/cropDeleteLayers, off). Pixels off, a commit keeps every layer's pixels
// beyond the new canvas (the Canvas Size frame resize); on, layers are cropped to
// the canvas. Layers on, layers left fully outside the box are deleted, with the
// count in the status line. Both settings persist.
void ui_crop_delete_options_crop_and_delete_layers() {
  SettingsValueRestorer restore_pixels(QStringLiteral("tools/cropDeletePixels"));
  SettingsValueRestorer restore_layers(QStringLiteral("tools/cropDeleteLayers"));
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("tools/cropDeletePixels"));
    settings.remove(QStringLiteral("tools/cropDeleteLayers"));
  }
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  auto* pixels_check = window.findChild<QCheckBox*>(QStringLiteral("cropDeletePixelsCheck"));
  auto* layers_check = window.findChild<QCheckBox*>(QStringLiteral("cropDeleteLayersCheck"));
  CHECK(pixels_check != nullptr && layers_check != nullptr);
  CHECK(pixels_check->isChecked());
  CHECK(!layers_check->isChecked());
  CHECK(pixels_check->text() == QStringLiteral("Delete Cropped Pixels"));
  CHECK(layers_check->text() == QStringLiteral("Delete Off-Canvas Layers"));
  require_action(window, "toolCropAction")->trigger();
  QApplication::processEvents();
  CHECK(pixels_check->isVisible() && layers_check->isVisible());
  canvas->set_snap_enabled(false);
  canvas->set_crop_ratio(0.0, 0.0);

  const auto add_layer = [&document](const char* name, QRect bounds) {
    patchy::Layer layer(document.allocate_layer_id(), name,
                        solid_pixels(bounds.width(), bounds.height(), patchy::PixelFormat::rgba8(), QColor(20, 90, 200)));
    layer.set_bounds(patchy::Rect{bounds.x(), bounds.y(), bounds.width(), bounds.height()});
    const auto id = layer.id();
    document.add_layer(std::move(layer));
    return id;
  };
  const auto straddling_id = add_layer("Straddling", QRect(80, 80, 60, 60));
  const auto outside_id = add_layer("Outside", QRect(600, 500, 40, 40));
  canvas->set_document(&document);
  QApplication::processEvents();
  const auto crop_to_box = [&] {
    drag(*canvas, canvas->widget_position_for_document_point(QPoint(100, 100)),
         canvas->widget_position_for_document_point(QPoint(300, 250)));
    CHECK(canvas->crop_session_rect() == QRect(100, 100, 201, 151));
    send_key(*canvas, Qt::Key_Return);
    QApplication::processEvents();
    CHECK(document.width() == 201 && document.height() == 151);
  };
  const auto undo = [&] {
    require_action_by_text(window, QStringLiteral("Undo"))->trigger();
    QApplication::processEvents();
    CHECK(document.width() == 1024 && document.height() == 768);
    CHECK(std::as_const(document).find_layer(outside_id) != nullptr);
  };

  // Defaults (pixels on, layers off): the straddling layer is cropped to the
  // canvas, the outside layer survives emptied (the tool's long-standing crop).
  crop_to_box();
  {
    const auto* straddling = std::as_const(document).find_layer(straddling_id);
    const auto* outside = std::as_const(document).find_layer(outside_id);
    CHECK(straddling != nullptr && outside != nullptr);
    if (straddling != nullptr) {
      CHECK(straddling->bounds().x == 0 && straddling->bounds().y == 0);
      CHECK(straddling->bounds().width == 40 && straddling->bounds().height == 40);
    }
    if (outside != nullptr) {
      CHECK(outside->bounds().empty());
    }
  }
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Cropped"));
  undo();

  // Pixels off: the frame resize keeps the straddling layer's full buffer
  // (translated by the box origin) and the outside layer's bounds.
  pixels_check->setChecked(false);
  QApplication::processEvents();
  CHECK(!patchy::ui::app_settings().value(QStringLiteral("tools/cropDeletePixels"), true).toBool());
  crop_to_box();
  {
    const auto* straddling = std::as_const(document).find_layer(straddling_id);
    const auto* outside = std::as_const(document).find_layer(outside_id);
    CHECK(straddling != nullptr && outside != nullptr);
    if (straddling != nullptr) {
      CHECK(straddling->bounds().x == -20 && straddling->bounds().y == -20);
      CHECK(straddling->bounds().width == 60 && straddling->bounds().height == 60);
    }
    if (outside != nullptr) {
      CHECK(outside->bounds().x == 500 && outside->bounds().y == 400);
    }
  }
  undo();

  // Pixels off, layers on: the outside layer goes, the straddling buffer stays whole.
  layers_check->setChecked(true);
  QApplication::processEvents();
  CHECK(patchy::ui::app_settings().value(QStringLiteral("tools/cropDeleteLayers")).toBool());
  crop_to_box();
  {
    const auto* straddling = std::as_const(document).find_layer(straddling_id);
    CHECK(straddling != nullptr);
    CHECK(std::as_const(document).find_layer(outside_id) == nullptr);
    if (straddling != nullptr) {
      CHECK(straddling->bounds().width == 60 && straddling->bounds().height == 60);
    }
  }
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Cropped, off-canvas layers deleted: 1"));
  undo();

  // Both on: cropped and deleted (the Advanced dialog's pair at once).
  pixels_check->setChecked(true);
  QApplication::processEvents();
  crop_to_box();
  {
    const auto* straddling = std::as_const(document).find_layer(straddling_id);
    CHECK(straddling != nullptr);
    CHECK(std::as_const(document).find_layer(outside_id) == nullptr);
    if (straddling != nullptr) {
      CHECK(straddling->bounds().width == 40 && straddling->bounds().height == 40);
    }
  }
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Cropped, off-canvas layers deleted: 1"));
  save_widget_artifact("ui_crop_delete_options", window);
  undo();

  // Both settings persist into a fresh window.
  pixels_check->setChecked(false);
  QApplication::processEvents();
  patchy::ui::MainWindow second;
  show_window(second);
  auto* second_pixels = second.findChild<QCheckBox*>(QStringLiteral("cropDeletePixelsCheck"));
  auto* second_layers = second.findChild<QCheckBox*>(QStringLiteral("cropDeleteLayersCheck"));
  CHECK(second_pixels != nullptr && !second_pixels->isChecked());
  CHECK(second_layers != nullptr && second_layers->isChecked());
}

// GitHub issue 701. With Delete Cropped Pixels off a crop keeps the Background's pixels
// beyond the new canvas, and a Background's bounds are the canvas, so the layer becomes an
// ordinary "Layer 0" (the Background locks cleared, every pixel kept) in the Crop undo step.
// It used to keep its name and both locks while no longer being the Background, so the row
// showed the plain transparency and position badges. With the option on, the Background is
// cut to the canvas and stays the Background.
void ui_crop_keeping_pixels_turns_background_into_layer_0() {
  SettingsValueRestorer restore_pixels(QStringLiteral("tools/cropDeletePixels"));
  SettingsValueRestorer restore_layers(QStringLiteral("tools/cropDeleteLayers"));
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("tools/cropDeletePixels"));
    settings.remove(QStringLiteral("tools/cropDeleteLayers"));
  }
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* layers_dock = window.findChild<QWidget*>(QStringLiteral("layersDock"));
  auto* pixels_check = window.findChild<QCheckBox*>(QStringLiteral("cropDeletePixelsCheck"));
  CHECK(layer_list != nullptr && layers_dock != nullptr && pixels_check != nullptr);
  const auto background = [&document] {
    return patchy::background_layer(std::as_const(document).layers(), document.width(), document.height());
  };
  // The lock badges on the bottom row: one padlock marked layerBackgroundBadge for the
  // Background, none for an unlocked ordinary layer.
  const auto bottom_badges = [layer_list] {
    auto* row = layer_list->itemWidget(layer_list->item(layer_list->count() - 1));
    return row != nullptr ? row->findChildren<QLabel*>(QStringLiteral("layerLockBadge")) : QList<QLabel*>{};
  };
  const auto bottom_is_background_row = [&bottom_badges] {
    const auto badges = bottom_badges();
    return badges.size() == 1 && badges.front()->property("layerBackgroundBadge").toBool();
  };
  CHECK(background() != nullptr);
  if (background() == nullptr) {
    return;
  }
  const auto background_id = background()->id();
  const auto canvas_width = document.width();
  const auto canvas_height = document.height();
  CHECK(bottom_is_background_row());

  require_action(window, "toolCropAction")->trigger();
  QApplication::processEvents();
  canvas->set_snap_enabled(false);
  canvas->set_crop_ratio(0.0, 0.0);
  const auto crop_to_box = [&] {
    drag(*canvas, canvas->widget_position_for_document_point(QPoint(100, 100)),
         canvas->widget_position_for_document_point(QPoint(300, 250)));
    CHECK(canvas->crop_session_rect() == QRect(100, 100, 201, 151));
    send_key(*canvas, Qt::Key_Return);
    QApplication::processEvents();
    CHECK(document.width() == 201 && document.height() == 151);
  };
  const auto undo = [&] {
    patchy::ui::MainWindowTestAccess::undo(window);
    QApplication::processEvents();
    CHECK(document.width() == canvas_width && document.height() == canvas_height);
    CHECK(background() != nullptr && background()->id() == background_id);
    CHECK(std::as_const(document).layers().front().name() == "Background");
    CHECK(bottom_is_background_row());
  };

  // Delete Cropped Pixels on (the default): cut to the canvas, still the Background.
  CHECK(pixels_check->isChecked());
  crop_to_box();
  CHECK(background() != nullptr && background()->id() == background_id);
  CHECK(std::as_const(document).layers().front().name() == "Background");
  CHECK(bottom_is_background_row());
  undo();

  // Off: the layer keeps all its pixels around the new canvas and is "Layer 0".
  pixels_check->setChecked(false);
  QApplication::processEvents();
  const auto undo_depth = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  crop_to_box();
  // Captured before the checks, so a failing run still shows the row.
  save_widget_artifact("issue701_crop_keep_pixels_layers_panel", *layers_dock);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_depth + 1);
  CHECK(background() == nullptr);
  {
    const auto& bottom = std::as_const(document).layers().front();
    CHECK(bottom.id() == background_id);
    CHECK(bottom.name() == "Layer 0");
    CHECK((patchy::layer_lock_flags(bottom) &
           (patchy::kLayerLockPosition | patchy::kLayerLockTransparentPixels | patchy::kLayerLockArtboardAutonest)) ==
          0U);
    CHECK(bottom.bounds().x == -100 && bottom.bounds().y == -100);
    CHECK(bottom.bounds().width == canvas_width && bottom.bounds().height == canvas_height);
    CHECK(bottom.pixels().width() == canvas_width && bottom.pixels().height() == canvas_height);
  }
  CHECK(bottom_badges().isEmpty());
  CHECK(require_layer_item(*layer_list, QStringLiteral("Layer 0")) != nullptr);
  // One undo brings back the Background with its padlock.
  undo();
}

// Image > Trim... (GitHub issue 297). Answers the next Trim dialog: checks whether
// Transparent Pixels is offered, picks `basis` (an object name, or nullptr to keep the
// remembered choice), sets the sides and accepts.
struct TrimDialogAnswer {
  const char* basis{nullptr};
  std::array<bool, 4> sides{true, true, true, true};  // top, bottom, left, right
  bool transparent_enabled{true};
  const char* expected_basis{nullptr};  // the radio checked on open, when given
  const char* artifact{nullptr};
  int tolerance{-1};           // set before accepting, when not negative
  int expected_tolerance{-1};  // the Tolerance shown on open, when not negative
};

// The dialog driver runs inside Qt's event dispatch, where a throwing CHECK must not land.
void trim_expect(bool condition, const char* what) {
  if (!condition) {
    patchy::test::report_failure(std::string("Trim dialog: ") + what);
  }
}

void answer_trim_dialog(TrimDialogAnswer answer, bool* seen) {
  *seen = false;
  QTimer::singleShot(0, [answer, seen] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyTrimDialog") || !widget->isVisible()) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      trim_expect(dialog != nullptr, "dialog != nullptr");
      if (dialog == nullptr) {
        return;
      }
      *seen = true;
      auto* transparent = dialog->findChild<QRadioButton*>(QStringLiteral("trimTransparentRadio"));
      trim_expect(transparent != nullptr, "transparent != nullptr");
      trim_expect(dialog->findChild<QRadioButton*>(QStringLiteral("trimTopLeftRadio")) != nullptr, "dialog->findChild<QRadioButton*>(QStringLiteral(\"trimTopLeftRadio\")) != nullptr");
      trim_expect(dialog->findChild<QRadioButton*>(QStringLiteral("trimBottomRightRadio")) != nullptr, "dialog->findChild<QRadioButton*>(QStringLiteral(\"trimBottomRightRadio\")) != nullptr");
      if (transparent != nullptr) {
        trim_expect(transparent->isEnabled() == answer.transparent_enabled, "transparent->isEnabled() == answer.transparent_enabled");
      }
      trim_expect(!dialog->styleSheet().contains(QLatin1Char('@')), "!dialog->styleSheet().contains(QLatin1Char('@'))");
      if (answer.expected_basis != nullptr) {
        auto* expected = dialog->findChild<QRadioButton*>(QString::fromLatin1(answer.expected_basis));
        trim_expect(expected != nullptr && expected->isChecked(), "expected != nullptr && expected->isChecked()");
      }
      if (answer.basis != nullptr) {
        auto* radio = dialog->findChild<QRadioButton*>(QString::fromLatin1(answer.basis));
        trim_expect(radio != nullptr, "radio != nullptr");
        if (radio != nullptr) {
          radio->setChecked(true);
        }
      }
      auto* tolerance = dialog->findChild<QSpinBox*>(QStringLiteral("trimToleranceSpin"));
      trim_expect(tolerance != nullptr, "tolerance != nullptr");
      if (tolerance != nullptr) {
        trim_expect(tolerance->minimum() == 0 && tolerance->maximum() == 255, "tolerance range 0..255");
        if (answer.expected_tolerance >= 0) {
          trim_expect(tolerance->value() == answer.expected_tolerance, "tolerance->value() == answer.expected_tolerance");
        }
        if (answer.tolerance >= 0) {
          tolerance->setValue(answer.tolerance);
        }
      }
      const std::array<const char*, 4> names{"trimTopCheck", "trimBottomCheck", "trimLeftCheck", "trimRightCheck"};
      for (std::size_t index = 0; index < names.size(); ++index) {
        auto* check = dialog->findChild<QCheckBox*>(QString::fromLatin1(names[index]));
        trim_expect(check != nullptr, "check != nullptr");
        if (check != nullptr) {
          check->setChecked(answer.sides[index]);
        }
      }
      if (answer.artifact != nullptr) {
        widget->grab().save(QStringLiteral("test-artifacts/%1.png").arg(QString::fromLatin1(answer.artifact)));
      }
      dialog->accept();
      return;
    }
    trim_expect(false, "no visible Trim dialog");
  });
}

void run_trim(patchy::ui::MainWindow& window, const TrimDialogAnswer& answer) {
  bool seen = false;
  answer_trim_dialog(answer, &seen);
  require_hotkey_action(window, QStringLiteral("image.trim"))->trigger();
  QApplication::processEvents();
  CHECK(seen);
}

bool run_trim_script(patchy::ui::MainWindow& window, const QString& source) {
  const auto clean = patchy::test::ui::scripting_support::run_script(window, source);
  if (!clean) {
    for (const auto& line : window.script_engine_host().message_backlog()) {
      fprintf(stderr, "  script: %s\n", qPrintable(line));
    }
  }
  return clean;
}

void check_trim_size(patchy::ui::MainWindow& window, const patchy::Document& document, int width, int height) {
  if (document.width() != width || document.height() != height) {
    fprintf(stderr, "  trim: expected %d x %d, document is %d x %d, status \"%s\"\n", width, height, document.width(),
            document.height(), qPrintable(window.statusBar()->currentMessage()));
  }
  CHECK(document.width() == width && document.height() == height);
}

void clear_trim_settings() {
  auto settings = patchy::ui::app_settings();
  settings.remove(QStringLiteral("trim"));
}

// A 200x150 document whose Background was removed: a pink block and a blue square on
// transparency, together spanning x 40..130 and y 30..110.
constexpr const char* kTrimStickerSetup = R"JS(
  var d = app.newDocument(200, 150);
  var base = d.activeLayer;
  var art = d.addLayer('Art');
  art.fillRect(40, 30, 60, 50, '#c83c78');
  d.addLayer('Dot').fillRect(120, 100, 10, 10, '#3080f0');
  base.remove();
)JS";

void ui_trim_transparent_pixels_trims_sides_and_undoes() {
  clear_trim_settings();
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_trim_script(window, QString::fromLatin1(kTrimStickerSetup)));
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  check_trim_size(window, document, 200, 150);
  const auto undo = [&] {
    require_action_by_text(window, QStringLiteral("Undo"))->trigger();
    QApplication::processEvents();
    check_trim_size(window, document, 200, 150);
  };

  // A first run starts on Transparent Pixels with every side checked.
  run_trim(window, {nullptr, {true, true, true, true}, true, "trimTransparentRadio", "ui_trim_dialog"});
  check_trim_size(window, document, 90, 80);
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Trimmed to 90 x 80"));
  {
    const auto composite = patchy::flatten_document_for_trim(document);
    CHECK(composite.pixel(0, 0)[3] == 255);
    CHECK(composite.pixel(0, 0)[0] == 0xc8);
    CHECK(composite.pixel(89, 79)[3] == 255);
    CHECK(composite.pixel(89, 79)[2] == 0xf0);
  }
  save_widget_artifact("ui_trim_result", window);
  undo();

  // Top only keeps the rows below the first visible one.
  run_trim(window, {nullptr, {true, false, false, false}});
  check_trim_size(window, document, 200, 120);
  undo();
  // Left and right only.
  run_trim(window, {nullptr, {false, false, true, true}, true, "trimTransparentRadio"});
  check_trim_size(window, document, 90, 150);

  // The choices persist.
  const auto settings = patchy::ui::app_settings();
  CHECK(settings.value(QStringLiteral("trim/basedOn")).toString() == QStringLiteral("transparent"));
  CHECK(!settings.value(QStringLiteral("trim/top")).toBool());
  CHECK(!settings.value(QStringLiteral("trim/bottom")).toBool());
  CHECK(settings.value(QStringLiteral("trim/left")).toBool());
  CHECK(settings.value(QStringLiteral("trim/right")).toBool());
  clear_trim_settings();
}

void ui_trim_corner_colors_and_nothing_to_trim() {
  clear_trim_settings();
  {
    // A remembered Transparent Pixels falls back to Top Left on an opaque image.
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("trim/basedOn"), QStringLiteral("transparent"));
  }
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_trim_script(window, QStringLiteral(R"JS(
    var d = app.newDocument(200, 150);
    d.activeLayer.fillRect(0, 0, 200, 150, '#ffffff');
    d.addLayer('Art').fillRect(50, 40, 30, 20, '#20a040');
    d.addLayer('Band').fillRect(0, 140, 200, 10, '#2040e0');
  )JS")));
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto undo = [&] {
    require_action_by_text(window, QStringLiteral("Undo"))->trigger();
    QApplication::processEvents();
    check_trim_size(window, document, 200, 150);
  };

  // White top left: everything white goes, the band keeps the full width.
  run_trim(window, {nullptr, {true, true, true, true}, false, "trimTopLeftRadio", "ui_trim_dialog_opaque"});
  check_trim_size(window, document, 200, 110);
  undo();
  // Blue bottom right, left and right only: no column is all blue, so nothing changes.
  run_trim(window, {"trimBottomRightRadio", {false, false, true, true}, false});
  check_trim_size(window, document, 200, 150);
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Nothing to trim on the chosen sides"));
  // Every side: only the band rows go.
  run_trim(window, {nullptr, {true, true, true, true}, false, "trimBottomRightRadio"});
  check_trim_size(window, document, 200, 140);
  undo();

  // A one-color image has nothing to keep.
  CHECK(run_trim_script(window, QStringLiteral(R"JS(
    var u = app.newDocument(64, 48);
    u.activeLayer.fillRect(0, 0, 64, 48, '#406080');
  )JS")));
  auto& uniform = patchy::ui::MainWindowTestAccess::document(window);
  run_trim(window, {"trimTopLeftRadio", {true, true, true, true}, false});
  check_trim_size(window, uniform, 64, 48);
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Nothing to trim: the whole image is one color"));
  clear_trim_settings();
}

// 16 and 32-bit documents trim on their deep composite and stay at their depth.
void ui_trim_deep_documents() {
  patchy::set_deep_editing_override(true);
  const auto restore = qScopeGuard([] { patchy::set_deep_editing_override(std::nullopt); });
  for (const int bits : {16, 32}) {
    clear_trim_settings();
    patchy::ui::MainWindow window;
    show_window(window);
    CHECK(run_trim_script(
        window, QString::fromLatin1(kTrimStickerSetup) + QStringLiteral("d.convertBitDepth(%1);").arg(bits)));
    auto& document = patchy::ui::MainWindowTestAccess::document(window);
    const auto depth = bits == 16 ? patchy::BitDepth::UInt16 : patchy::BitDepth::Float32;
    CHECK(document.color_state().bit_depth == depth);
    run_trim(window, {nullptr, {true, true, true, true}, true, "trimTransparentRadio"});
    check_trim_size(window, document, 90, 80);
    CHECK(document.color_state().bit_depth == depth);
    const auto* art = patchy::test::ui::scripting_support::layer_named(document, "Art");
    CHECK(art != nullptr && art->pixels().format().bit_depth == depth);
    require_action_by_text(window, QStringLiteral("Undo"))->trigger();
    QApplication::processEvents();
    check_trim_size(window, document, 200, 150);
  }
  clear_trim_settings();
}

// Tolerance (GitHub issue 297): near-white specks and a faint stripe keep a white image
// from trimming at 0; tolerance 8 cuts them, and the value is remembered.
void ui_trim_tolerance_cuts_near_matches() {
  clear_trim_settings();
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_trim_script(window, QStringLiteral(R"JS(
    var d = app.newDocument(200, 150);
    d.activeLayer.fillRect(0, 0, 200, 150, '#ffffff');
    d.activeLayer.fillRect(0, 1, 200, 1, '#f8f8f8');
    d.activeLayer.fillRect(5, 5, 1, 1, '#fbfcf9');
    d.activeLayer.fillRect(190, 140, 3, 3, '#f7f9fa');
    d.addLayer('Art').fillRect(50, 40, 30, 20, '#20a040');
  )JS")));
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  // A new user starts at 0, the exact match: only the speck-free rows above the stripe
  // and below the bottom speck go.
  {
    TrimDialogAnswer answer{"trimTopLeftRadio", {true, true, true, true}, false};
    answer.expected_tolerance = 0;
    run_trim(window, answer);
  }
  check_trim_size(window, document, 200, 142);
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  check_trim_size(window, document, 200, 150);
  {
    TrimDialogAnswer answer{"trimTopLeftRadio", {true, true, true, true}, false};
    answer.artifact = "ui_trim_dialog_tolerance";
    answer.tolerance = 8;
    run_trim(window, answer);
  }
  check_trim_size(window, document, 30, 20);
  CHECK(patchy::ui::app_settings().value(QStringLiteral("trim/tolerance")).toInt() == 8);
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  {
    TrimDialogAnswer answer{nullptr, {true, true, true, true}, false, "trimTopLeftRadio"};
    answer.expected_tolerance = 8;
    run_trim(window, answer);
  }
  check_trim_size(window, document, 30, 20);
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  check_trim_size(window, document, 200, 150);

  // A tolerance so high that every pixel matches leaves nothing, and says why.
  {
    TrimDialogAnswer answer{"trimTopLeftRadio", {true, true, true, true}, false};
    answer.tolerance = 255;
    run_trim(window, answer);
  }
  check_trim_size(window, document, 200, 150);
  CHECK(window.statusBar()->currentMessage() ==
        QStringLiteral("Nothing to trim: every pixel is within the tolerance of the trim color. Lower the Tolerance."));
  CHECK(run_trim_script(window, QString::fromLatin1(kTrimStickerSetup)));
  auto& stickers = patchy::ui::MainWindowTestAccess::document(window);
  {
    TrimDialogAnswer answer{"trimTransparentRadio", {true, true, true, true}, true};
    answer.tolerance = 255;
    run_trim(window, answer);
  }
  check_trim_size(window, stickers, 200, 150);
  CHECK(window.statusBar()->currentMessage() ==
        QStringLiteral("Nothing to trim: every pixel is transparent within the tolerance. Lower the Tolerance."));
  clear_trim_settings();
}

// doc.trim: the same analysis in one call, returning the kept rect or null.
void ui_script_trim() {
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_trim_script(window, QString::fromLatin1(kTrimStickerSetup) + QStringLiteral(R"JS(
    function throws(f) { try { f(); } catch (e) { return true; } return false; }
    if (throws(function () { d.trim({basedOn: 'middle'}); }) !== true) throw Error('unknown basedOn');
    var r = d.trim({bottom: false});
    if (!r || r.x !== 40 || r.y !== 30 || r.width !== 90 || r.height !== 120) throw Error('kept ' + JSON.stringify(r));
    if (d.width !== 90 || d.height !== 120) throw Error('size ' + d.width + 'x' + d.height);
    r = d.trim();
    if (!r || r.x !== 0 || r.y !== 0 || r.width !== 90 || r.height !== 80) throw Error('second ' + JSON.stringify(r));
    if (d.trim() !== null) throw Error('nothing left to trim');
    var o = app.newDocument(100, 80);
    o.activeLayer.fillRect(0, 0, 100, 80, '#ffffff');
    o.activeLayer.fillRect(10, 20, 30, 5, '#000000');
    if (o.trim() !== null) throw Error('opaque image has no transparent edge');
    var t = o.trim({basedOn: 'topLeft', left: false, right: false});
    if (!t || t.x !== 0 || t.y !== 20 || t.width !== 100 || t.height !== 5) throw Error('topLeft ' + JSON.stringify(t));
    if (o.trim({basedOn: 'bottomRight'}) === null) throw Error('bottomRight');
    if (o.width !== 30 || o.height !== 5) throw Error('final ' + o.width + 'x' + o.height);
    var n = app.newDocument(60, 40);
    n.activeLayer.fillRect(0, 0, 60, 40, '#ffffff');
    n.activeLayer.fillRect(0, 39, 60, 1, '#f6f6f6');
    n.activeLayer.fillRect(20, 10, 10, 10, '#000000');
    if (throws(function () { n.trim({basedOn: 'topLeft', tolerance: 256}); }) !== true) throw Error('tolerance 256');
    if (throws(function () { n.trim({basedOn: 'topLeft', tolerance: -1}); }) !== true) throw Error('tolerance -1');
    var e = n.trim({basedOn: 'topLeft', tolerance: 0});
    if (!e || e.height !== 30) throw Error('tolerance 0 ' + JSON.stringify(e));
    var k = n.trim({basedOn: 'topLeft', tolerance: 9});
    if (!k || k.width !== 10 || k.height !== 10) throw Error('tolerance 9 ' + JSON.stringify(k));
    console.log('trim-ok');
  )JS")));
  CHECK(patchy::test::ui::scripting_support::backlog_contains(window, QStringLiteral("trim-ok")));
}

}  // namespace

namespace {

using patchy::test::ui::scripting_support::run_script;

// Perspective Crop (GitHub issue 328): paints the document's first layer with a
// "photographed" chart, an 8 x 6 grid of colored cells seen through a perspective,
// so a session reads in screenshots and the commit has something to straighten.
const std::array<QPointF, 4> kPerspectiveChartQuad{QPointF(260.0, 150.0), QPointF(760.0, 210.0),
                                                   QPointF(820.0, 620.0), QPointF(180.0, 560.0)};

void paint_perspective_chart(patchy::ui::MainWindow& window) {
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(!document.layers().empty());
  auto& layer = document.layers().front();
  const auto format = std::as_const(layer).pixels().format();
  patchy::PixelBuffer pixels(document.width(), document.height(), format);
  const std::array<double, 8> quad{kPerspectiveChartQuad[0].x(), kPerspectiveChartQuad[0].y(),
                                   kPerspectiveChartQuad[1].x(), kPerspectiveChartQuad[1].y(),
                                   kPerspectiveChartQuad[2].x(), kPerspectiveChartQuad[2].y(),
                                   kPerspectiveChartQuad[3].x(), kPerspectiveChartQuad[3].y()};
  const auto forward = patchy::homography_from_rect_to_quad(0.0, 0.0, 8.0, 6.0, quad);
  CHECK(forward.has_value());
  const auto inverse = patchy::invert_homography(*forward);
  CHECK(inverse.has_value());
  const std::array<std::array<std::uint8_t, 3>, 6> palette{{{214, 69, 65},
                                                            {242, 177, 52},
                                                            {68, 160, 96},
                                                            {58, 120, 200},
                                                            {150, 90, 180},
                                                            {240, 240, 236}}};
  const auto channels = static_cast<std::size_t>(format.channels);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto uv = patchy::apply_homography(*inverse, x + 0.5, y + 0.5);
      std::array<std::uint8_t, 3> color{92, 98, 106};
      if (uv[0] >= 0.0 && uv[1] >= 0.0 && uv[0] < 8.0 && uv[1] < 6.0) {
        const auto cell_x = static_cast<int>(uv[0]);
        const auto cell_y = static_cast<int>(uv[1]);
        const bool border = uv[0] - cell_x < 0.06 || uv[1] - cell_y < 0.06;
        color = border ? std::array<std::uint8_t, 3>{30, 30, 34}
                       : palette[static_cast<std::size_t>((cell_x + cell_y * 3) % 6)];
      }
      auto* pixel = pixels.pixel(x, y);
      for (std::size_t c = 0; c < std::min<std::size_t>(channels, 3U); ++c) {
        pixel[c] = color[c];
      }
      if (channels >= 4U) {
        pixel[3] = 255;
      }
    }
  }
  layer.set_pixels(std::move(pixels));
  layer.set_bounds(patchy::Rect{0, 0, document.width(), document.height()});
  require_canvas(window)->document_changed();
  QApplication::processEvents();
}

QPoint perspective_widget_point(patchy::ui::CanvasWidget& canvas, QPointF document_point) {
  return canvas.widget_position_for_document_point(document_point.toPoint());
}

bool points_near(QPointF actual, QPointF expected, double tolerance = 3.0) {
  return std::abs(actual.x() - expected.x()) <= tolerance && std::abs(actual.y() - expected.y()) <= tolerance;
}

// The tool sits in the Crop flyout after Crop with its own permanent command id and no
// default key; Shift+C cycles the two, and C stays on Crop.
void ui_perspective_crop_tool_joins_crop_flyout() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* crop = require_action(window, "toolCropAction");
  auto* perspective = require_action(window, "toolPerspectiveCropAction");
  CHECK(perspective->text() == QStringLiteral("Perspective Crop"));
  CHECK(!perspective->icon().isNull());
  CHECK(perspective->shortcut().isEmpty());
  CHECK(crop->shortcut() == QKeySequence(Qt::Key_C));
  auto* button = window.findChild<QToolButton*>(QStringLiteral("cropToolButton"));
  CHECK(button != nullptr);
  if (button == nullptr) {
    return;
  }
  CHECK(button->menu() != nullptr);
  CHECK(button->menu()->actions().indexOf(perspective) == button->menu()->actions().indexOf(crop) + 1);
  const auto* command = window.hotkey_registry().find_command(QStringLiteral("tools.perspective_crop"));
  CHECK(command != nullptr);
  CHECK(command != nullptr && command->action == perspective);
  CHECK(command != nullptr && command->category == QStringLiteral("tools"));
  const auto* cycle = window.hotkey_registry().find_command(QStringLiteral("tools.cycle.crop"));
  CHECK(cycle != nullptr);

  crop->trigger();
  QApplication::processEvents();
  QTest::keyClick(canvas, Qt::Key_C, Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::PerspectiveCrop);
  CHECK(button->defaultAction() == perspective);
  CHECK(window.statusBar()->currentMessage().startsWith(QStringLiteral("Perspective Crop: drag a box")));
  QTest::keyClick(canvas, Qt::Key_C, Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Crop);
}

// A drag lays out a box whose corners then move one at a time, the inside moves it, Esc
// clears it; four clicks place the corners in any order and come out upright; a fourth
// click that would fold the quad is refused. Enter rectifies the document onto the
// options bar's W x H with its Resolution in one undo step, and Undo restores it all.
void ui_perspective_crop_session_commits_and_undoes() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_snap_enabled(false);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto original_ppi = document.print_settings().horizontal_ppi;
  require_action(window, "toolPerspectiveCropAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::PerspectiveCrop);
  CHECK(!canvas->perspective_crop_session_pending());
  auto* apply = window.findChild<QPushButton*>(QStringLiteral("perspectiveCropApplyButton"));
  auto* cancel = window.findChild<QPushButton*>(QStringLiteral("perspectiveCropCancelButton"));
  CHECK(apply != nullptr && cancel != nullptr);
  if (apply == nullptr || cancel == nullptr) {
    return;
  }
  CHECK(!apply->isEnabled());
  CHECK(!cancel->isEnabled());

  drag(*canvas, perspective_widget_point(*canvas, QPointF(100, 100)),
       perspective_widget_point(*canvas, QPointF(400, 300)));
  QApplication::processEvents();
  auto quad = canvas->perspective_crop_quad();
  CHECK(quad.has_value());
  if (!quad.has_value()) {
    return;
  }
  CHECK(points_near((*quad)[0], QPointF(100, 100)));
  CHECK(points_near((*quad)[2], QPointF(400, 300)));
  CHECK(apply->isEnabled());
  CHECK(cancel->isEnabled());

  // One corner moves alone.
  drag(*canvas, perspective_widget_point(*canvas, QPointF(400, 100)),
       perspective_widget_point(*canvas, QPointF(450, 80)));
  quad = canvas->perspective_crop_quad();
  CHECK(quad.has_value() && points_near((*quad)[1], QPointF(450, 80)));
  CHECK(quad.has_value() && points_near((*quad)[0], QPointF(100, 100)));
  CHECK(quad.has_value() && points_near((*quad)[2], QPointF(400, 300)));
  // Dragging that corner through the opposite side stops where the quad would fold.
  drag(*canvas, perspective_widget_point(*canvas, QPointF(450, 80)),
       perspective_widget_point(*canvas, QPointF(60, 400)));
  quad = canvas->perspective_crop_quad();
  CHECK(quad.has_value());
  if (quad.has_value()) {
    CHECK(patchy::perspective_crop_quad_is_convex({(*quad)[0].x(), (*quad)[0].y(), (*quad)[1].x(), (*quad)[1].y(),
                                                   (*quad)[2].x(), (*quad)[2].y(), (*quad)[3].x(), (*quad)[3].y()}));
  }
  // Esc clears everything.
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(!canvas->perspective_crop_session_pending());
  CHECK(!cancel->isEnabled());

  // Four clicks, counterclockwise and starting at the bottom right: the quad comes out
  // upright (top left first, clockwise). The inside then moves it whole.
  const std::array<QPointF, 4> clicks{QPointF(500, 400), QPointF(520, 150), QPointF(200, 120), QPointF(180, 380)};
  for (std::size_t i = 0; i < 3U; ++i) {
    const auto at = perspective_widget_point(*canvas, clicks[i]);
    send_mouse(*canvas, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton);
  }
  CHECK(canvas->perspective_crop_session_pending());
  CHECK(!canvas->perspective_crop_session_active());
  // A fourth click inside the triangle would fold the quad: refused, three corners stay.
  const auto folded = perspective_widget_point(*canvas, QPointF(420, 220));
  send_mouse(*canvas, QEvent::MouseButtonPress, folded, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, folded, Qt::LeftButton, Qt::NoButton);
  CHECK(!canvas->perspective_crop_session_active());
  const auto last = perspective_widget_point(*canvas, clicks[3]);
  send_mouse(*canvas, QEvent::MouseButtonPress, last, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, last, Qt::LeftButton, Qt::NoButton);
  quad = canvas->perspective_crop_quad();
  CHECK(quad.has_value());
  if (!quad.has_value()) {
    return;
  }
  CHECK(points_near((*quad)[0], clicks[2]));
  CHECK(points_near((*quad)[1], clicks[1]));
  CHECK(points_near((*quad)[2], clicks[0]));
  CHECK(points_near((*quad)[3], clicks[3]));
  drag(*canvas, perspective_widget_point(*canvas, QPointF(350, 260)),
       perspective_widget_point(*canvas, QPointF(360, 270)));
  quad = canvas->perspective_crop_quad();
  CHECK(quad.has_value() && points_near((*quad)[0], clicks[2] + QPointF(10, 10)));

  auto* width = window.findChild<QSpinBox*>(QStringLiteral("perspectiveCropWidthSpin"));
  auto* height = window.findChild<QSpinBox*>(QStringLiteral("perspectiveCropHeightSpin"));
  auto* resolution = window.findChild<QDoubleSpinBox*>(QStringLiteral("perspectiveCropResolutionSpin"));
  CHECK(width != nullptr && height != nullptr && resolution != nullptr);
  if (width == nullptr || height == nullptr || resolution == nullptr) {
    return;
  }
  CHECK(width->isVisible());
  width->setValue(300);
  height->setValue(200);
  resolution->setValue(150.0);
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(document.width() == 300);
  CHECK(document.height() == 200);
  CHECK(document.print_settings().horizontal_ppi == 150.0);
  CHECK(!canvas->perspective_crop_session_pending());
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Cropped and straightened"));

  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  auto& restored = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(restored.width() == 1024);
  CHECK(restored.height() == 768);
  CHECK(restored.print_settings().horizontal_ppi == original_ppi);

  // Blank fields take the corners' own spacing.
  width->setValue(0);
  height->setValue(0);
  resolution->setValue(0.0);
  CHECK(width->text().trimmed().isEmpty());
  CHECK(canvas->set_perspective_crop_quad({QPointF(100, 100), QPointF(300, 100), QPointF(300, 250), QPointF(100, 250)}));
  canvas->commit_perspective_crop_session();
  QApplication::processEvents();
  auto& rectified = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(rectified.width() == 200);
  CHECK(rectified.height() == 150);
  CHECK(rectified.print_settings().horizontal_ppi == original_ppi);
}

// Front Image fills the fields from the document, Clear empties them, Show Grid draws
// the grid inside the quad and persists. Also writes the session screenshot.
void ui_perspective_crop_options_bar_and_grid() {
  SettingsValueRestorer saved_grid(QStringLiteral("tools/perspectiveCropShowGrid"));
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  paint_perspective_chart(window);
  require_action(window, "toolPerspectiveCropAction")->trigger();
  QApplication::processEvents();
  auto* width = window.findChild<QSpinBox*>(QStringLiteral("perspectiveCropWidthSpin"));
  auto* height = window.findChild<QSpinBox*>(QStringLiteral("perspectiveCropHeightSpin"));
  auto* resolution = window.findChild<QDoubleSpinBox*>(QStringLiteral("perspectiveCropResolutionSpin"));
  auto* front = window.findChild<QPushButton*>(QStringLiteral("perspectiveCropFrontImageButton"));
  auto* clear = window.findChild<QPushButton*>(QStringLiteral("perspectiveCropClearButton"));
  auto* grid = window.findChild<QCheckBox*>(QStringLiteral("perspectiveCropShowGridCheck"));
  CHECK(width != nullptr && height != nullptr && resolution != nullptr && front != nullptr && clear != nullptr &&
        grid != nullptr);
  if (width == nullptr || height == nullptr || resolution == nullptr || front == nullptr || clear == nullptr ||
      grid == nullptr) {
    return;
  }
  CHECK(front->isVisible() && clear->isVisible() && grid->isVisible());
  front->click();
  QApplication::processEvents();
  const auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(width->value() == document.width());
  CHECK(height->value() == document.height());
  CHECK(std::abs(resolution->value() - document.print_settings().horizontal_ppi) < 0.01);
  clear->click();
  QApplication::processEvents();
  CHECK(width->value() == 0 && height->value() == 0 && resolution->value() == 0.0);
  CHECK(width->text().trimmed().isEmpty());
  CHECK(resolution->text().trimmed().isEmpty());

  // The whole quad on screen for the screenshot (and the pixel probes below).
  require_hotkey_action(window, QStringLiteral("view.fit_on_screen"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->set_perspective_crop_quad(kPerspectiveChartQuad));
  grid->setChecked(true);
  QApplication::processEvents();
  CHECK(canvas->perspective_crop_show_grid());
  const auto with_grid = render_widget_image(*canvas);
  save_widget_artifact("ui_perspective_crop_session", window);
  grid->setChecked(false);
  QApplication::processEvents();
  CHECK(!canvas->perspective_crop_show_grid());
  CHECK(!patchy::ui::app_settings().value(QStringLiteral("tools/perspectiveCropShowGrid")).toBool());
  const auto without_grid = render_widget_image(*canvas);
  // The grid only draws inside the quad: the middle differs, the pasteboard does not.
  const auto middle = canvas->widget_position_for_document_point(QPoint(500, 380));
  bool inside_changed = false;
  for (int dy = -40; dy <= 40 && !inside_changed; ++dy) {
    for (int dx = -40; dx <= 40 && !inside_changed; ++dx) {
      inside_changed = with_grid.pixelColor(middle + QPoint(dx, dy)) != without_grid.pixelColor(middle + QPoint(dx, dy));
    }
  }
  CHECK(inside_changed);
  const auto outside = canvas->widget_position_for_document_point(QPoint(40, 40));
  CHECK(with_grid.pixelColor(outside) == without_grid.pixelColor(outside));

  // Committing the chart's own corners straightens it: the cells come out upright.
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  const auto& straightened = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(straightened.width() > 400);
  const auto cell_w = straightened.width() / 8.0;
  const auto cell_h = straightened.height() / 6.0;
  const auto top_left = canvas_pixel_center(*canvas, QPoint(static_cast<int>(cell_w / 2), static_cast<int>(cell_h / 2)));
  const auto next_row = canvas_pixel_center(
      *canvas, QPoint(static_cast<int>(cell_w / 2), static_cast<int>(cell_h * 1.5)));
  CHECK(color_close(top_left, QColor(214, 69, 65), 6));
  CHECK(color_close(next_row, QColor(58, 120, 200), 6));
  save_widget_artifact("ui_perspective_crop_result", *canvas);
}

// doc.perspectiveCrop: the scripted twin of the commit, and its refusals.
// Paints the first layer from a per-pixel color function (document coordinates).
void paint_first_layer(patchy::ui::MainWindow& window, const std::function<QColor(int, int)>& color_at) {
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(!document.layers().empty());
  auto& layer = document.layers().front();
  const auto format = std::as_const(layer).pixels().format();
  patchy::PixelBuffer pixels(document.width(), document.height(), format);
  const auto channels = static_cast<std::size_t>(format.channels);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto color = color_at(x, y);
      auto* pixel = pixels.pixel(x, y);
      const std::array<std::uint8_t, 3> rgb{static_cast<std::uint8_t>(color.red()),
                                            static_cast<std::uint8_t>(color.green()),
                                            static_cast<std::uint8_t>(color.blue())};
      for (std::size_t c = 0; c < std::min<std::size_t>(channels, 3U); ++c) {
        pixel[c] = rgb[c];
      }
      if (channels >= 4U) {
        pixel[3] = 255;
      }
    }
  }
  layer.set_pixels(std::move(pixels));
  layer.set_bounds(patchy::Rect{0, 0, document.width(), document.height()});
  require_canvas(window)->document_changed();
  QApplication::processEvents();
}

// A white page with ruled lines, photographed in perspective on a light desk: the up
// marker has to read over light artwork too.
void paint_perspective_page(patchy::ui::MainWindow& window) {
  const std::array<double, 8> page{300.0, 140.0, 760.0, 190.0, 800.0, 640.0, 230.0, 600.0};
  const auto forward = patchy::homography_from_rect_to_quad(0.0, 0.0, 1.0, 1.0, page);
  CHECK(forward.has_value());
  const auto inverse = patchy::invert_homography(*forward);
  CHECK(inverse.has_value());
  paint_first_layer(window, [&inverse](int x, int y) {
    const auto uv = patchy::apply_homography(*inverse, x + 0.5, y + 0.5);
    if (uv[0] < 0.0 || uv[1] < 0.0 || uv[0] >= 1.0 || uv[1] >= 1.0) {
      return QColor(226, 222, 214);
    }
    const bool rule = uv[1] > 0.12 && std::fmod(uv[1] * 18.0, 1.0) < 0.08 && uv[0] > 0.08 && uv[0] < 0.92;
    const bool title = uv[1] > 0.04 && uv[1] < 0.09 && uv[0] > 0.08 && uv[0] < 0.55;
    return rule || title ? QColor(70, 74, 82) : QColor(252, 252, 250);
  });
}

// Quadrant colors about (400, 300): red top left, green top right, blue bottom left,
// yellow bottom right, so a commit's orientation reads from three probes.
QColor perspective_quadrant_color(int x, int y) {
  if (y < 300) {
    return x < 400 ? QColor(220, 40, 40) : QColor(40, 200, 60);
  }
  return x < 400 ? QColor(40, 80, 220) : QColor(240, 210, 30);
}

double degrees_between(QPointF from, QPointF to, QPointF center) {
  const auto a = std::atan2(from.y() - center.y(), from.x() - center.x());
  const auto b = std::atan2(to.y() - center.y(), to.x() - center.x());
  auto degrees = (b - a) * 180.0 / std::numbers::pi;
  while (degrees > 180.0) {
    degrees -= 360.0;
  }
  while (degrees < -180.0) {
    degrees += 360.0;
  }
  return degrees;
}

// Rotates the finished quad by dragging from just outside its top right corner around
// the center to `degrees` (on screen, clockwise positive).
void rotate_perspective_quad(patchy::ui::CanvasWidget& canvas, double degrees, Qt::KeyboardModifiers modifiers) {
  const auto quad = canvas.perspective_crop_quad();
  CHECK(quad.has_value());
  if (!quad.has_value()) {
    return;
  }
  const auto center = ((*quad)[0] + (*quad)[1] + (*quad)[2] + (*quad)[3]) / 4.0;
  const auto center_widget = canvas.widget_position_f(center);
  const auto corner_widget = canvas.widget_position_f((*quad)[1]);
  auto outward = corner_widget - center_widget;
  outward /= std::hypot(outward.x(), outward.y());
  const auto press = (corner_widget + outward * 14.0).toPoint();
  const auto release = (center_widget + QTransform().rotate(degrees).map(QPointF(press) - center_widget)).toPoint();
  const auto halfway =
      (center_widget + QTransform().rotate(degrees / 2.0).map(QPointF(press) - center_widget)).toPoint();
  send_mouse(canvas, QEvent::MouseButtonPress, press, Qt::LeftButton, Qt::LeftButton, modifiers);
  send_mouse(canvas, QEvent::MouseMove, halfway, Qt::NoButton, Qt::LeftButton, modifiers);
  send_mouse(canvas, QEvent::MouseMove, release, Qt::NoButton, Qt::LeftButton, modifiers);
  send_mouse(canvas, QEvent::MouseButtonRelease, release, Qt::LeftButton, Qt::NoButton, modifiers);
  QApplication::processEvents();
}

// The up marker: a light stroke over a dark one on the output's top edge, with an arrow
// outside its middle. True when the arrow's stem shows just outside that edge.
bool perspective_up_marker_at_top_edge(patchy::ui::CanvasWidget& canvas) {
  const auto quad = canvas.perspective_crop_quad();
  if (!quad.has_value()) {
    return false;
  }
  const auto top_left = canvas.widget_position_f((*quad)[0]);
  const auto top_right = canvas.widget_position_f((*quad)[1]);
  const auto center = canvas.widget_position_f(((*quad)[0] + (*quad)[1] + (*quad)[2] + (*quad)[3]) / 4.0);
  const auto middle = (top_left + top_right) / 2.0;
  const auto outward = middle - center;
  const auto along = top_right - top_left;
  QPointF normal(along.y(), -along.x());
  normal /= std::hypot(normal.x(), normal.y());
  if (normal.x() * outward.x() + normal.y() * outward.y() < 0.0) {
    normal = -normal;
  }
  const auto image = render_widget_image(canvas);
  int light = 0;
  const QPointF tangent = along / std::hypot(along.x(), along.y());
  for (double distance = 11.0; distance <= 15.0; distance += 1.0) {
    // The stem is 2 px of light ink: one of three pixels across it is fully covered.
    bool hit = false;
    for (const double across : {-1.0, 0.0, 1.0}) {
      const auto probe = (middle + normal * distance + tangent * across).toPoint();
      hit = hit || color_close(image.pixelColor(probe), QColor(245, 248, 252), 30);
    }
    light += hit ? 1 : 0;
  }
  return light >= 3;
}

// Seth (October 22, 2026): an edge drag moves its two corners, a drag just outside a
// corner rotates the quad (Shift snaps to 15 degrees), Turn moves the top to the next
// side, and an up marker on the output's top edge follows all of it. The commit's
// orientation matches the marker.
void ui_perspective_crop_edges_rotate_and_up_marker() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_snap_enabled(false);
  paint_first_layer(window, perspective_quadrant_color);
  require_hotkey_action(window, QStringLiteral("view.fit_on_screen"))->trigger();
  require_action(window, "toolPerspectiveCropAction")->trigger();
  QApplication::processEvents();
  const std::array<QPointF, 4> box{QPointF(200, 150), QPointF(600, 150), QPointF(600, 450), QPointF(200, 450)};
  CHECK(canvas->set_perspective_crop_quad(box));
  CHECK(perspective_up_marker_at_top_edge(*canvas));

  // The top edge's middle handle moves both top corners; the bottom stays.
  drag(*canvas, perspective_widget_point(*canvas, QPointF(400, 150)),
       perspective_widget_point(*canvas, QPointF(400, 110)));
  auto quad = canvas->perspective_crop_quad();
  CHECK(quad.has_value());
  if (!quad.has_value()) {
    return;
  }
  CHECK(points_near((*quad)[0], QPointF(200, 110), 4.0));
  CHECK(points_near((*quad)[1], QPointF(600, 110), 4.0));
  CHECK(points_near((*quad)[2], QPointF(600, 450), 0.01));
  CHECK(points_near((*quad)[3], QPointF(200, 450), 0.01));
  // The right edge's line itself (off its middle handle) grabs that side too.
  drag(*canvas, perspective_widget_point(*canvas, QPointF(600, 360)),
       perspective_widget_point(*canvas, QPointF(640, 360)));
  quad = canvas->perspective_crop_quad();
  CHECK(quad.has_value() && points_near((*quad)[1], QPointF(640, 110), 4.0));
  CHECK(quad.has_value() && points_near((*quad)[2], QPointF(640, 450), 4.0));
  CHECK(quad.has_value() && points_near((*quad)[0], QPointF(200, 110), 4.0));

  // A free rotate follows the pointer; Shift snaps the turn to 15 degrees.
  CHECK(canvas->set_perspective_crop_quad(box));
  rotate_perspective_quad(*canvas, 37.0, Qt::NoModifier);
  quad = canvas->perspective_crop_quad();
  CHECK(quad.has_value());
  const QPointF center(400, 300);
  if (quad.has_value()) {
    CHECK(std::abs(degrees_between(box[0], (*quad)[0], center) - 37.0) < 2.5);
  }
  CHECK(canvas->set_perspective_crop_quad(box));
  rotate_perspective_quad(*canvas, 92.0, Qt::ShiftModifier);
  quad = canvas->perspective_crop_quad();
  CHECK(quad.has_value());
  if (!quad.has_value()) {
    return;
  }
  // 90 degrees clockwise about (400, 300); the corners keep their output order.
  CHECK(points_near((*quad)[0], QPointF(550, 100), 0.05));
  CHECK(points_near((*quad)[1], QPointF(550, 500), 0.05));
  CHECK(points_near((*quad)[2], QPointF(250, 500), 0.05));
  CHECK(points_near((*quad)[3], QPointF(250, 100), 0.05));
  // The marker moved with the top edge, now the quad's right side.
  CHECK(perspective_up_marker_at_top_edge(*canvas));
  save_widget_artifact("ui_perspective_crop_rotated", *canvas);

  // Commit: the output's top is that right side, so the image comes out turned a
  // quarter counterclockwise: green top left, yellow top right, red bottom left.
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  const auto& rotated = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(rotated.width() == 400);
  CHECK(rotated.height() == 300);
  CHECK(color_close(canvas_pixel_center(*canvas, QPoint(50, 50)), QColor(40, 200, 60), 4));
  CHECK(color_close(canvas_pixel_center(*canvas, QPoint(350, 50)), QColor(240, 210, 30), 4));
  CHECK(color_close(canvas_pixel_center(*canvas, QPoint(50, 250)), QColor(220, 40, 40), 4));
  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  CHECK(patchy::ui::MainWindowTestAccess::document(window).width() == 1024);

  // Turn moves the top to the next side without moving a corner: the left side
  // becomes the top, so the result turns a quarter clockwise.
  require_action(window, "toolPerspectiveCropAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->set_perspective_crop_quad(box));
  auto* turn = window.findChild<QToolButton*>(QStringLiteral("perspectiveCropTurnButton"));
  CHECK(turn != nullptr);
  if (turn == nullptr) {
    return;
  }
  CHECK(turn->isVisible() && turn->isEnabled());
  turn->click();
  QApplication::processEvents();
  quad = canvas->perspective_crop_quad();
  CHECK(quad.has_value() && points_near((*quad)[0], box[3], 0.01));
  CHECK(quad.has_value() && points_near((*quad)[1], box[0], 0.01));
  CHECK(perspective_up_marker_at_top_edge(*canvas));
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  const auto& turned = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(turned.width() == 300);
  CHECK(turned.height() == 400);
  // A quarter clockwise: blue top left, red top right, yellow bottom left.
  CHECK(color_close(canvas_pixel_center(*canvas, QPoint(50, 50)), QColor(40, 80, 220), 4));
  CHECK(color_close(canvas_pixel_center(*canvas, QPoint(250, 50)), QColor(220, 40, 40), 4));
  CHECK(color_close(canvas_pixel_center(*canvas, QPoint(50, 350)), QColor(240, 210, 30), 4));
}

// The card's screenshot: a session over a light page, mid-edge handles and the up marker.
void ui_perspective_crop_up_marker_over_light_page() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  paint_perspective_page(window);
  require_hotkey_action(window, QStringLiteral("view.fit_on_screen"))->trigger();
  require_action(window, "toolPerspectiveCropAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->set_perspective_crop_quad(
      {QPointF(300, 140), QPointF(760, 190), QPointF(800, 640), QPointF(230, 600)}));
  CHECK(perspective_up_marker_at_top_edge(*canvas));
  save_widget_artifact("ui_perspective_crop_light_page", window);
}

void ui_script_document_perspective_crop() {
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral(R"JS(
    app.activeDocument.perspectiveCrop([[100, 100], [400, 120], [380, 330], [90, 300]],
                                       { width: 240, height: 160, resolution: 200 });
  )JS")));
  QApplication::processEvents();
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(document.width() == 240);
  CHECK(document.height() == 160);
  CHECK(document.print_settings().horizontal_ppi == 200.0);
  CHECK(run_script(window, QStringLiteral(R"JS(
    app.activeDocument.perspectiveCrop([{x: 0, y: 0}, {x: 100, y: 0}, {x: 100, y: 50}, {x: 0, y: 50}]);
  )JS")));
  QApplication::processEvents();
  CHECK(patchy::ui::MainWindowTestAccess::document(window).width() == 100);
  CHECK(patchy::ui::MainWindowTestAccess::document(window).height() == 50);
  CHECK(!run_script(window, QStringLiteral(
      "app.activeDocument.perspectiveCrop([[0, 0], [50, 50], [50, 0], [0, 50]]);")));
  CHECK(!run_script(window, QStringLiteral("app.activeDocument.perspectiveCrop([[0, 0], [50, 0], [50, 50]]);")));
  CHECK(!run_script(window, QStringLiteral(
      "app.activeDocument.perspectiveCrop([[0, 0], [50, 0], [50, 50], [0, 50]], { width: 0 });")));
  CHECK(patchy::ui::MainWindowTestAccess::document(window).width() == 100);
}

// The bar's strip and the open list of `combo` painted where it sits (the offscreen screen
// keeps Qt's list window apart from the window grab), cropped to the bar and the list.
void save_crop_list_artifact(const std::string& name, QWidget& window, QComboBox& combo) {
  combo.showPopup();
  QApplication::processEvents();
  process_events_for(30);
  auto* popup = combo.view()->window();
  ensure_capture_themed(window);
  ensure_capture_themed(*popup);
  const auto origin = window.mapToGlobal(QPoint(0, 0));
  const QRect popup_rect(popup->mapToGlobal(QPoint(0, 0)) - origin, popup->size());
  auto* bar = window.findChild<QToolBar*>(QStringLiteral("Options"));
  const int bar_top = bar != nullptr ? bar->mapTo(&window, QPoint(0, 0)).y() : 0;
  QImage shot(window.size().expandedTo(QSize(popup_rect.right() + 1, popup_rect.bottom() + 1)),
              QImage::Format_ARGB32_Premultiplied);
  shot.fill(QColor(96, 96, 104));
  {
    QPainter painter(&shot);
    painter.drawPixmap(0, 0, window.grab());
    painter.drawPixmap(popup_rect.topLeft(), popup->grab());
  }
  const int right = std::min(shot.width() - 1, std::max(popup_rect.right() + 420, 760));
  const int bottom = std::min(shot.height() - 1, popup_rect.bottom() + 16);
  save_image_artifact(name, shot.copy(QRect(QPoint(0, std::max(0, bar_top - 2)), QPoint(right, bottom))));
  combo.hidePopup();
  QApplication::processEvents();
}

// Picks the list's Save Preset row and answers its name prompt, typing `name` when it is not
// empty. Returns the name the prompt offered; empty when no prompt came up.
QString save_crop_preset_through_prompt(patchy::ui::MainWindow& window, QComboBox& presets, const QString& name) {
  QString offered;
  bool answered = false;
  bool stop = false;
  std::function<void()> answer;
  answer = guarded([&] {
    if (stop) {
      return;
    }
    auto* prompt = qobject_cast<QInputDialog*>(find_top_level_dialog(QStringLiteral("cropSavePresetDialog")));
    if (prompt == nullptr) {
      QTimer::singleShot(10, &window, answer);
      return;
    }
    offered = prompt->textValue();
    if (!name.isEmpty()) {
      prompt->setTextValue(name);
    }
    answered = true;
    prompt->accept();
  });
  presets.setCurrentIndex(crop_preset_index(presets, CropKind::Save));
  QTimer::singleShot(0, &window, answer);
  QElapsedTimer timer;
  timer.start();
  while (!answered && timer.elapsed() < 5000) {
    QApplication::processEvents(QEventLoop::AllEvents, 20);
  }
  stop = true;
  process_events_for(40);  // a retry still queued sees `stop` and ends
  return offered;
}

QJsonArray stored_crop_user_presets() {
  return QJsonDocument::fromJson(
             patchy::ui::app_settings().value(QStringLiteral("tools/cropUserPresets")).toString().toUtf8())
      .array();
}

// GitHub issue 696: one Preset list replaces the Style and Ratio lists. It holds the two free
// modes, Patchy's ratio presets, a few pixel sizes read from the New Document templates, and
// Save Preset / Delete Preset. A ratio preset shows the ratio fields and fills them; a size
// preset shows the Size fields and sets the box to that many pixels about its center without
// resampling (the commit extends or crops the canvas to it). The fields stay the source of
// truth: editing them shows the matching entry or the free mode.
void ui_crop_preset_list_offers_modes_ratios_and_sizes() {
  SettingsValueRestorer saved_style(QStringLiteral("tools/cropStyle"));
  SettingsValueRestorer saved_ratio_w(QStringLiteral("tools/cropRatioWidth"));
  SettingsValueRestorer saved_ratio_h(QStringLiteral("tools/cropRatioHeight"));
  SettingsValueRestorer saved_presets(QStringLiteral("tools/cropUserPresets"));
  for (const auto* key :
       {"tools/cropStyle", "tools/cropRatioWidth", "tools/cropRatioHeight", "tools/cropUserPresets"}) {
    patchy::ui::app_settings().remove(QLatin1String(key));
  }
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* info_label = window.findChild<QLabel*>(QStringLiteral("documentInfoLabel"));
  require_action(window, "toolCropAction")->trigger();
  QApplication::processEvents();
  canvas->set_snap_enabled(false);

  auto* presets = window.findChild<QComboBox*>(QStringLiteral("cropPresetCombo"));
  auto* ratio_w = window.findChild<QDoubleSpinBox*>(QStringLiteral("cropRatioWidthSpin"));
  auto* ratio_h = window.findChild<QDoubleSpinBox*>(QStringLiteral("cropRatioHeightSpin"));
  auto* width = window.findChild<QSpinBox*>(QStringLiteral("cropWidthSpin"));
  auto* height = window.findChild<QSpinBox*>(QStringLiteral("cropHeightSpin"));
  CHECK(presets != nullptr && ratio_w != nullptr && ratio_h != nullptr && width != nullptr && height != nullptr &&
        info_label != nullptr);
  if (presets == nullptr || ratio_w == nullptr || ratio_h == nullptr || width == nullptr || height == nullptr ||
      info_label == nullptr) {
    return;
  }
  // The two lists it replaces are gone.
  CHECK(window.findChild<QComboBox*>(QStringLiteral("cropStyleCombo")) == nullptr);
  CHECK(window.findChild<QComboBox*>(QStringLiteral("cropRatioPresetCombo")) == nullptr);
  CHECK(presets->isVisible());

  // The rows, top to bottom, with a separator above each group.
  std::vector<CropKind> kinds;
  int separators = 0;
  for (int index = 0; index < presets->count(); ++index) {
    const auto data = presets->itemData(index, patchy::ui::kCropPresetKindRole);
    if (data.isValid()) {
      kinds.push_back(static_cast<CropKind>(data.toInt()));
    } else {
      ++separators;
      CHECK(index + 1 < presets->count());
    }
  }
  const std::vector<CropKind> expected_kinds = {
      CropKind::FreeRatio, CropKind::FreeSize, CropKind::OriginalRatio, CropKind::Ratio, CropKind::Ratio,
      CropKind::Ratio,     CropKind::Ratio,    CropKind::Ratio,         CropKind::Size,  CropKind::Size,
      CropKind::Size,      CropKind::Size,     CropKind::Size,          CropKind::Size,  CropKind::Save,
      CropKind::Delete};
  CHECK(kinds == expected_kinds);
  CHECK(separators == 3);  // ratios, sizes, the two commands; saved presets add a fourth
  CHECK(presets->itemText(0) == QStringLiteral("Ratio"));
  CHECK(presets->itemText(1) == QStringLiteral("Size"));
  CHECK(presets->itemText(crop_preset_index(*presets, CropKind::OriginalRatio)) == QStringLiteral("Original Ratio"));
  CHECK(presets->itemText(crop_preset_index(*presets, CropKind::Save)) == QStringLiteral("Save Preset..."));
  CHECK(presets->itemText(crop_preset_index(*presets, CropKind::Delete)) == QStringLiteral("Delete Preset"));
  for (const auto ratio : {QSizeF(1.0, 1.0), QSizeF(4.0, 5.0), QSizeF(5.0, 7.0), QSizeF(2.0, 3.0), QSizeF(16.0, 9.0)}) {
    CHECK(crop_preset_index(*presets, CropKind::Ratio, ratio) >= 0);
  }
  // Every pixel size is a New Document template's, named after it.
  const auto templates = patchy::ui::artboard_size_presets();
  CHECK(patchy::ui::crop_size_template_ids().size() == 6U);
  for (const auto& id : patchy::ui::crop_size_template_ids()) {
    const auto found = std::find_if(templates.begin(), templates.end(),
                                    [&id](const patchy::ui::ArtboardSizePreset& preset) { return preset.id == id; });
    CHECK(found != templates.end());
    if (found == templates.end()) {
      continue;
    }
    const int row = crop_preset_index(*presets, CropKind::Size, QSizeF(found->size));
    CHECK(row >= 0);
    CHECK(presets->itemText(row) ==
          QStringLiteral("%1 x %2 px (%3)").arg(found->size.width()).arg(found->size.height()).arg(found->name));
  }

  // A fresh start: Ratio with nothing set, nothing to save or delete.
  CHECK(crop_preset_kind(*presets) == CropKind::FreeRatio);
  CHECK(ratio_w->isVisible() && !width->isVisible());
  CHECK(!crop_preset_row_enabled(*presets, crop_preset_index(*presets, CropKind::Save)));
  CHECK(!crop_preset_row_enabled(*presets, crop_preset_index(*presets, CropKind::Delete)));

  // A ratio preset picked from Size: one step shows the ratio fields and fills them.
  presets->setCurrentIndex(crop_preset_index(*presets, CropKind::FreeSize));
  QApplication::processEvents();
  CHECK(width->isVisible() && !ratio_w->isVisible());
  presets->setCurrentIndex(crop_preset_index(*presets, CropKind::Ratio, QSizeF(16.0, 9.0)));
  QApplication::processEvents();
  CHECK(ratio_w->isVisible() && !width->isVisible());
  CHECK(ratio_w->value() == 16.0 && ratio_h->value() == 9.0);
  CHECK(canvas->crop_ratio_width() == 16.0 && canvas->crop_ratio_height() == 9.0);
  CHECK(canvas->crop_session_rect() == QRect(0, 96, 1024, 576));
  CHECK(presets->currentText() == QStringLiteral("16 : 9"));
  CHECK(crop_preset_row_enabled(*presets, crop_preset_index(*presets, CropKind::Save)));
  save_crop_list_artifact("ui_crop_preset_list", window, *presets);

  // Editing a field shows the free mode; typing a preset's values shows the preset.
  ratio_h->setValue(10.0);
  QApplication::processEvents();
  CHECK(crop_preset_kind(*presets) == CropKind::FreeRatio);
  ratio_w->setValue(2.0);
  ratio_h->setValue(3.0);
  QApplication::processEvents();
  CHECK(presets->currentText() == QStringLiteral("2 : 3 (4 : 6)"));
  // Ratio picked over a preset clears the fields: what None was.
  presets->setCurrentIndex(crop_preset_index(*presets, CropKind::FreeRatio));
  QApplication::processEvents();
  CHECK(ratio_w->value() == 0.0 && ratio_h->value() == 0.0);
  CHECK(canvas->crop_ratio_width() == 0.0);
  CHECK(canvas->crop_session_rect() == QRect(0, 0, 1024, 768));

  // A size preset: the Size fields, and the box at that many pixels about its center.
  const int size_720 = crop_preset_index(*presets, CropKind::Size, QSizeF(1280.0, 720.0));
  presets->setCurrentIndex(size_720);
  QApplication::processEvents();
  CHECK(presets->currentIndex() == size_720);
  CHECK(width->isVisible() && !ratio_w->isVisible());
  CHECK(width->value() == 1280 && height->value() == 720);
  CHECK(canvas->crop_ratio_width() == 0.0);
  CHECK(canvas->crop_session_rect() == QRect(-128, 24, 1280, 720));
  save_widget_artifact("ui_crop_preset_size", window);
  // The fields stay the source of truth: another size shows Size, a preset's size the preset.
  width->setValue(500);
  QApplication::processEvents();
  CHECK(crop_preset_kind(*presets) == CropKind::FreeSize);
  width->setValue(1080);
  height->setValue(1080);
  QApplication::processEvents();
  CHECK(presets->currentIndex() == crop_preset_index(*presets, CropKind::Size, QSizeF(1080.0, 1080.0)));

  // Nothing is resampled: the commit makes a canvas of exactly the preset's pixels, by
  // extending and cropping the document, and the pixels keep their scale.
  presets->setCurrentIndex(size_720);
  QApplication::processEvents();
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(info_label->text().contains(QStringLiteral("1280 x 720 px")));
  {
    const auto& doc = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
    CHECK(doc.width() == 1280 && doc.height() == 720);
  }
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(info_label->text().contains(QStringLiteral("1024 x 768 px")));
}

// GitHub issue 696: Save Preset stores the current mode and its values under a name (a ratio,
// or a pixel size) in tools/cropUserPresets, offering "3 : 2" or "640 x 480 px" as the name;
// the saved entries join the list, come back in a new window and apply like the built-in
// ones; Delete Preset removes the saved preset the list shows. Built-in entries cannot be
// deleted, and malformed stored entries are skipped.
void ui_crop_presets_save_delete_and_persist() {
  SettingsValueRestorer saved_style(QStringLiteral("tools/cropStyle"));
  SettingsValueRestorer saved_ratio_w(QStringLiteral("tools/cropRatioWidth"));
  SettingsValueRestorer saved_ratio_h(QStringLiteral("tools/cropRatioHeight"));
  SettingsValueRestorer saved_presets(QStringLiteral("tools/cropUserPresets"));
  for (const auto* key :
       {"tools/cropStyle", "tools/cropRatioWidth", "tools/cropRatioHeight", "tools/cropUserPresets"}) {
    patchy::ui::app_settings().remove(QLatin1String(key));
  }
  {
    patchy::ui::MainWindow window;
    show_window(window);
    auto* canvas = require_canvas(window);
    require_action(window, "toolCropAction")->trigger();
    QApplication::processEvents();
    canvas->set_snap_enabled(false);
    auto* presets = window.findChild<QComboBox*>(QStringLiteral("cropPresetCombo"));
    auto* ratio_w = window.findChild<QDoubleSpinBox*>(QStringLiteral("cropRatioWidthSpin"));
    auto* ratio_h = window.findChild<QDoubleSpinBox*>(QStringLiteral("cropRatioHeightSpin"));
    auto* width = window.findChild<QSpinBox*>(QStringLiteral("cropWidthSpin"));
    auto* height = window.findChild<QSpinBox*>(QStringLiteral("cropHeightSpin"));
    CHECK(presets != nullptr && ratio_w != nullptr && ratio_h != nullptr && width != nullptr && height != nullptr);
    if (presets == nullptr || ratio_w == nullptr || ratio_h == nullptr || width == nullptr || height == nullptr) {
      return;
    }
    const auto save_enabled = [&] {
      return crop_preset_row_enabled(*presets, crop_preset_index(*presets, CropKind::Save));
    };
    const auto delete_enabled = [&] {
      return crop_preset_row_enabled(*presets, crop_preset_index(*presets, CropKind::Delete));
    };
    // No ratio: nothing to save, and no prompt comes up when the row is picked anyway.
    CHECK(!save_enabled() && !delete_enabled());

    // A ratio: saved under a typed name, the prompt offering the ratio itself.
    ratio_w->setValue(3.0);
    ratio_h->setValue(2.0);
    QApplication::processEvents();
    CHECK(save_enabled() && !delete_enabled());
    CHECK(save_crop_preset_through_prompt(window, *presets, QStringLiteral("Photo print")) == QStringLiteral("3 : 2"));
    CHECK(window.statusBar()->currentMessage() == QStringLiteral("Crop preset saved: Photo print"));
    const int photo_row = crop_preset_index(*presets, CropKind::UserRatio, QSizeF(3.0, 2.0));
    CHECK(photo_row >= 0);
    CHECK(presets->currentIndex() == photo_row);
    CHECK(presets->currentText() == QStringLiteral("Photo print"));
    CHECK(!save_enabled() && delete_enabled());  // saved already; it can be deleted
    {
      const auto stored = stored_crop_user_presets();
      CHECK(stored.size() == 1);
      const auto entry = stored.at(0).toObject();
      CHECK(entry.value(QStringLiteral("id")).toString() == QStringLiteral("user-1"));
      CHECK(entry.value(QStringLiteral("name")).toString() == QStringLiteral("Photo print"));
      CHECK(entry.value(QStringLiteral("kind")).toString() == QStringLiteral("ratio"));
      CHECK(entry.value(QStringLiteral("w")).toDouble() == 3.0);
      CHECK(entry.value(QStringLiteral("h")).toDouble() == 2.0);
    }

    // A pixel size: the Size mode's box, saved under the offered name.
    presets->setCurrentIndex(crop_preset_index(*presets, CropKind::FreeSize));
    width->setValue(640);
    height->setValue(480);
    QApplication::processEvents();
    CHECK(crop_preset_kind(*presets) == CropKind::FreeSize);
    CHECK(save_enabled());
    CHECK(save_crop_preset_through_prompt(window, *presets, QString()) == QStringLiteral("640 x 480 px"));
    const int size_row = crop_preset_index(*presets, CropKind::UserSize, QSizeF(640.0, 480.0));
    CHECK(size_row >= 0);
    CHECK(presets->currentIndex() == size_row);
    CHECK(presets->currentText() == QStringLiteral("640 x 480 px"));
    CHECK(stored_crop_user_presets().size() == 2);
    CHECK(stored_crop_user_presets().at(1).toObject().value(QStringLiteral("kind")).toString() ==
          QStringLiteral("size"));
    CHECK(stored_crop_user_presets().at(1).toObject().value(QStringLiteral("id")).toString() ==
          QStringLiteral("user-2"));
    // The saved presets sit in their own group above the two commands.
    CHECK(crop_preset_index(*presets, CropKind::UserRatio) < crop_preset_index(*presets, CropKind::UserSize));
    CHECK(crop_preset_index(*presets, CropKind::UserSize) < crop_preset_index(*presets, CropKind::Save));
    save_crop_list_artifact("ui_crop_preset_list_saved", window, *presets);
    send_key(*canvas, Qt::Key_Escape);
    window.close();
    QApplication::processEvents();
  }

  // A new window lists both and applies them like the built-in entries.
  patchy::ui::MainWindow second;
  show_window(second);
  auto* canvas = require_canvas(second);
  require_action(second, "toolCropAction")->trigger();
  QApplication::processEvents();
  canvas->set_snap_enabled(false);
  auto* presets = second.findChild<QComboBox*>(QStringLiteral("cropPresetCombo"));
  auto* ratio_w = second.findChild<QDoubleSpinBox*>(QStringLiteral("cropRatioWidthSpin"));
  auto* ratio_h = second.findChild<QDoubleSpinBox*>(QStringLiteral("cropRatioHeightSpin"));
  auto* width = second.findChild<QSpinBox*>(QStringLiteral("cropWidthSpin"));
  CHECK(presets != nullptr && ratio_w != nullptr && ratio_h != nullptr && width != nullptr);
  if (presets == nullptr || ratio_w == nullptr || ratio_h == nullptr || width == nullptr) {
    return;
  }
  const int photo_row = crop_preset_index(*presets, CropKind::UserRatio, QSizeF(3.0, 2.0));
  const int size_row = crop_preset_index(*presets, CropKind::UserSize, QSizeF(640.0, 480.0));
  CHECK(photo_row >= 0 && size_row >= 0);
  CHECK(presets->itemText(photo_row) == QStringLiteral("Photo print"));
  presets->setCurrentIndex(photo_row);
  QApplication::processEvents();
  CHECK(ratio_w->isVisible());
  CHECK(ratio_w->value() == 3.0 && ratio_h->value() == 2.0);
  CHECK(canvas->crop_session_rect() == QRect(0, 42, 1024, 683));
  presets->setCurrentIndex(size_row);
  QApplication::processEvents();
  CHECK(width->isVisible());
  CHECK(canvas->crop_session_rect() == QRect(192, 144, 640, 480));
  CHECK(presets->currentIndex() == size_row);

  // Delete Preset removes the saved preset the list shows and leaves the box as it is.
  presets->setCurrentIndex(crop_preset_index(*presets, CropKind::Delete));
  process_events_for(60);
  CHECK(crop_preset_index(*presets, CropKind::UserSize) < 0);
  CHECK(second.statusBar()->currentMessage() == QStringLiteral("Crop preset deleted: 640 x 480 px"));
  CHECK(crop_preset_kind(*presets) == CropKind::FreeSize);
  CHECK(canvas->crop_session_rect() == QRect(192, 144, 640, 480));
  CHECK(stored_crop_user_presets().size() == 1);
  // With a built-in entry shown, Delete is off and does nothing when picked anyway.
  presets->setCurrentIndex(crop_preset_index(*presets, CropKind::Ratio, QSizeF(1.0, 1.0)));
  QApplication::processEvents();
  CHECK(!crop_preset_row_enabled(*presets, crop_preset_index(*presets, CropKind::Delete)));
  presets->setCurrentIndex(crop_preset_index(*presets, CropKind::Delete));
  process_events_for(60);
  CHECK(presets->currentText() == QStringLiteral("1 : 1 (Square)"));
  CHECK(stored_crop_user_presets().size() == 1);
  // The last saved preset gone: the key is removed, and so is the group.
  presets->setCurrentIndex(crop_preset_index(*presets, CropKind::UserRatio));
  QApplication::processEvents();
  presets->setCurrentIndex(crop_preset_index(*presets, CropKind::Delete));
  process_events_for(60);
  CHECK(crop_preset_index(*presets, CropKind::UserRatio) < 0);
  CHECK(!patchy::ui::app_settings().contains(QStringLiteral("tools/cropUserPresets")));
  CHECK(ratio_w->value() == 3.0 && ratio_h->value() == 2.0);
  CHECK(crop_preset_kind(*presets) == CropKind::FreeRatio);
  presets->setCurrentIndex(crop_preset_index(*presets, CropKind::FreeRatio));

  // Stored entries without a usable id, name, kind or value are skipped, never shown.
  patchy::ui::app_settings().setValue(QStringLiteral("tools/cropUserPresets"), QString::fromUtf8(R"([
      {"id":"user-4","name":"Wide","kind":"ratio","w":21,"h":9},
      {"id":"x","name":"No id","kind":"ratio","w":1,"h":1},
      {"id":"user-5","name":"","kind":"size","w":10,"h":10},
      {"id":"user-6","name":"Bad kind","kind":"area","w":10,"h":10},
      {"id":"user-7","name":"Zero","kind":"size","w":0,"h":10},
      "junk"])"));
  const auto loaded = patchy::ui::load_crop_user_presets();
  CHECK(loaded.size() == 1U);
  if (loaded.size() == 1U) {
    CHECK(loaded.front().id == QStringLiteral("user-4") && loaded.front().name == QStringLiteral("Wide"));
    CHECK(!loaded.front().size && loaded.front().value == QSizeF(21.0, 9.0));
  }
  // A new preset takes an id above every stored one.
  CHECK(patchy::ui::add_crop_user_preset(QStringLiteral("Tall"), true, QSizeF(100.0, 400.0)) ==
        QStringLiteral("user-5"));
}

// The settings from before the Preset list keep their meaning (tools/cropStyle,
// tools/cropRatioWidth, tools/cropRatioHeight are permanent): a user who quit in Size style
// comes back to the Size fields, and one who quit with a ratio comes back to that ratio, the
// list showing its preset or Ratio for a ratio of their own.
void ui_crop_preset_list_restores_stored_style_and_ratio() {
  SettingsValueRestorer saved_style(QStringLiteral("tools/cropStyle"));
  SettingsValueRestorer saved_ratio_w(QStringLiteral("tools/cropRatioWidth"));
  SettingsValueRestorer saved_ratio_h(QStringLiteral("tools/cropRatioHeight"));
  SettingsValueRestorer saved_presets(QStringLiteral("tools/cropUserPresets"));
  patchy::ui::app_settings().remove(QStringLiteral("tools/cropUserPresets"));
  struct Stored {
    int style;
    double ratio_w;
    double ratio_h;
    CropKind shown;
    const char* text;
  };
  for (const auto& stored : {Stored{0, 16.0, 9.0, CropKind::Ratio, "16 : 9"},
                             Stored{0, 3.0, 1.0, CropKind::FreeRatio, "Ratio"},
                             Stored{0, 0.0, 0.0, CropKind::FreeRatio, "Ratio"},
                             Stored{1, 16.0, 9.0, CropKind::FreeSize, "Size"}}) {
    {
      auto settings = patchy::ui::app_settings();
      settings.setValue(QStringLiteral("tools/cropStyle"), stored.style);
      settings.setValue(QStringLiteral("tools/cropRatioWidth"), stored.ratio_w);
      settings.setValue(QStringLiteral("tools/cropRatioHeight"), stored.ratio_h);
      settings.sync();
    }
    patchy::ui::MainWindow window;
    show_window(window);
    auto* canvas = require_canvas(window);
    require_action(window, "toolCropAction")->trigger();
    QApplication::processEvents();
    auto* presets = window.findChild<QComboBox*>(QStringLiteral("cropPresetCombo"));
    auto* ratio_w = window.findChild<QDoubleSpinBox*>(QStringLiteral("cropRatioWidthSpin"));
    auto* ratio_h = window.findChild<QDoubleSpinBox*>(QStringLiteral("cropRatioHeightSpin"));
    auto* width = window.findChild<QSpinBox*>(QStringLiteral("cropWidthSpin"));
    CHECK(presets != nullptr && ratio_w != nullptr && ratio_h != nullptr && width != nullptr);
    if (presets == nullptr || ratio_w == nullptr || ratio_h == nullptr || width == nullptr) {
      return;
    }
    CHECK(crop_preset_kind(*presets) == stored.shown);
    CHECK(presets->currentText() == QLatin1String(stored.text));
    CHECK(width->isVisible() == (stored.style == 1));
    CHECK(ratio_w->isVisible() == (stored.style == 0));
    if (stored.style == 0) {
      CHECK(ratio_w->value() == stored.ratio_w && ratio_h->value() == stored.ratio_h);
      CHECK(canvas->crop_ratio_width() == stored.ratio_w && canvas->crop_ratio_height() == stored.ratio_h);
    } else {
      // Size runs unconstrained, and the stored ratio is written back unchanged.
      CHECK(canvas->crop_ratio_width() == 0.0);
      CHECK(canvas->crop_session_rect() == QRect(0, 0, 1024, 768));
    }
    window.close();
    QApplication::processEvents();
    const auto settings = patchy::ui::app_settings();
    CHECK(settings.value(QStringLiteral("tools/cropStyle")).toInt() == stored.style);
    CHECK(settings.value(QStringLiteral("tools/cropRatioWidth")).toDouble() == stored.ratio_w);
    CHECK(settings.value(QStringLiteral("tools/cropRatioHeight")).toDouble() == stored.ratio_h);
  }
}

// GitHub issue 697: the Crop bar's swap buttons. In the ratio fields the button stands where
// the ":" was: the two values trade places, the automatic frame re-fits and a custom box
// turns about its center; it is off until a ratio is set. In the Size fields it sits between
// Width and Height (the link button follows Height) and the pending box trades its width and
// height about its center.
void ui_crop_swap_buttons_turn_ratio_and_size() {
  SettingsValueRestorer saved_style(QStringLiteral("tools/cropStyle"));
  SettingsValueRestorer saved_ratio_w(QStringLiteral("tools/cropRatioWidth"));
  SettingsValueRestorer saved_ratio_h(QStringLiteral("tools/cropRatioHeight"));
  SettingsValueRestorer saved_presets(QStringLiteral("tools/cropUserPresets"));
  for (const auto* key :
       {"tools/cropStyle", "tools/cropRatioWidth", "tools/cropRatioHeight", "tools/cropUserPresets"}) {
    patchy::ui::app_settings().remove(QLatin1String(key));
  }
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action(window, "toolCropAction")->trigger();
  QApplication::processEvents();
  canvas->set_snap_enabled(false);
  auto* presets = window.findChild<QComboBox*>(QStringLiteral("cropPresetCombo"));
  auto* ratio_w = window.findChild<QDoubleSpinBox*>(QStringLiteral("cropRatioWidthSpin"));
  auto* ratio_h = window.findChild<QDoubleSpinBox*>(QStringLiteral("cropRatioHeightSpin"));
  auto* ratio_swap = window.findChild<QAbstractButton*>(QStringLiteral("cropRatioSwapButton"));
  auto* size_swap = window.findChild<QAbstractButton*>(QStringLiteral("cropSizeSwapButton"));
  auto* width = window.findChild<QSpinBox*>(QStringLiteral("cropWidthSpin"));
  auto* height = window.findChild<QSpinBox*>(QStringLiteral("cropHeightSpin"));
  auto* link = window.findChild<QPushButton*>(QStringLiteral("cropLinkSizeButton"));
  CHECK(presets != nullptr && ratio_w != nullptr && ratio_h != nullptr && ratio_swap != nullptr &&
        size_swap != nullptr && width != nullptr && height != nullptr && link != nullptr);
  if (presets == nullptr || ratio_w == nullptr || ratio_h == nullptr || ratio_swap == nullptr ||
      size_swap == nullptr || width == nullptr || height == nullptr || link == nullptr) {
    return;
  }
  // Ratio fields: the swap shows there, off while no ratio is set.
  CHECK(ratio_swap->isVisible() && !size_swap->isVisible());
  CHECK(!ratio_swap->isEnabled());
  CHECK(ratio_w->x() < ratio_swap->x() && ratio_swap->x() < ratio_h->x());

  // 16 : 9 becomes 9 : 16 with one click; the automatic frame re-fits to it.
  presets->setCurrentIndex(crop_preset_index(*presets, CropKind::Ratio, QSizeF(16.0, 9.0)));
  QApplication::processEvents();
  CHECK(ratio_swap->isEnabled());
  CHECK(canvas->crop_session_rect() == QRect(0, 96, 1024, 576));
  ratio_swap->click();
  QApplication::processEvents();
  CHECK(ratio_w->value() == 9.0 && ratio_h->value() == 16.0);
  CHECK(canvas->crop_ratio_width() == 9.0 && canvas->crop_ratio_height() == 16.0);
  CHECK(canvas->crop_session_rect() == QRect(296, 0, 432, 768));
  CHECK(!canvas->crop_session_can_reset());  // still the automatic frame
  CHECK(crop_preset_kind(*presets) == CropKind::FreeRatio);  // 9 : 16 is no preset
  ratio_swap->click();
  QApplication::processEvents();
  CHECK(ratio_w->value() == 16.0 && ratio_h->value() == 9.0);
  CHECK(presets->currentText() == QStringLiteral("16 : 9"));
  CHECK(canvas->crop_session_rect() == QRect(0, 96, 1024, 576));

  // A custom box turns in place: its width and height trade about its center.
  ratio_w->setValue(2.0);
  ratio_h->setValue(1.0);
  QApplication::processEvents();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(300, 300)),
       canvas->widget_position_for_document_point(QPoint(500, 380)));
  const auto landscape = canvas->crop_session_rect();
  CHECK(landscape.has_value() && landscape->width() > landscape->height());
  if (!landscape.has_value()) {
    return;
  }
  ratio_swap->click();
  QApplication::processEvents();
  CHECK(ratio_w->value() == 1.0 && ratio_h->value() == 2.0);
  CHECK(canvas->crop_session_rect() ==
        QRect(landscape->x() + (landscape->width() - landscape->height()) / 2,
              landscape->y() + (landscape->height() - landscape->width()) / 2, landscape->height(),
              landscape->width()));
  CHECK(canvas->crop_session_rect()->center() == landscape->center());
  send_key(*canvas, Qt::Key_Escape);

  // Original Ratio (4 : 3 for 1024 x 768) swaps to 3 : 4, a ratio of the user's own.
  presets->setCurrentIndex(crop_preset_index(*presets, CropKind::OriginalRatio));
  QApplication::processEvents();
  ratio_swap->click();
  QApplication::processEvents();
  CHECK(ratio_w->value() == 3.0 && ratio_h->value() == 4.0);
  CHECK(crop_preset_kind(*presets) == CropKind::FreeRatio);
  CHECK(canvas->crop_session_rect() == QRect(224, 0, 576, 768));
  // The swapped ratio is the one that is saved.
  QElapsedTimer timer;
  timer.start();
  while (patchy::ui::app_settings().value(QStringLiteral("tools/cropRatioHeight")).toDouble() != 4.0 &&
         timer.elapsed() < 3000) {
    QApplication::processEvents(QEventLoop::AllEvents, 20);
  }
  CHECK(patchy::ui::app_settings().value(QStringLiteral("tools/cropRatioWidth")).toDouble() == 3.0);
  CHECK(patchy::ui::app_settings().value(QStringLiteral("tools/cropRatioHeight")).toDouble() == 4.0);

  // Size fields: Width, swap, Height, then the link button.
  presets->setCurrentIndex(crop_preset_index(*presets, CropKind::FreeSize));
  QApplication::processEvents();
  CHECK(size_swap->isVisible() && !ratio_swap->isVisible());
  CHECK(width->x() < size_swap->x() && size_swap->x() < height->x() && height->x() < link->x());
  CHECK(size_swap->isEnabled());
  // On the untouched frame the swap lays a turned box over the canvas.
  size_swap->click();
  QApplication::processEvents();
  CHECK(canvas->crop_session_rect() == QRect(128, -128, 768, 1024));
  CHECK(width->value() == 768 && height->value() == 1024);
  CHECK(canvas->crop_session_has_changes());
  send_key(*canvas, Qt::Key_Escape);
  width->setValue(600);
  height->setValue(400);
  QApplication::processEvents();
  CHECK(canvas->crop_session_rect() == QRect(212, 184, 600, 400));
  save_widget_artifact("ui_crop_size_swap_before", window);
  size_swap->click();
  QApplication::processEvents();
  CHECK(canvas->crop_session_rect() == QRect(312, 84, 400, 600));
  CHECK(width->value() == 400 && height->value() == 600);
  save_widget_artifact("ui_crop_size_swap_after", window);
  size_swap->click();
  QApplication::processEvents();
  CHECK(canvas->crop_session_rect() == QRect(212, 184, 600, 400));
  send_key(*canvas, Qt::Key_Escape);
  presets->setCurrentIndex(crop_preset_index(*presets, CropKind::FreeRatio));
}

// GitHub issue 698: while a changed crop box is pending, O steps to the next overlay (No
// Overlay is not a stop), Shift+O turns the Triangle and the Golden Spiral (with another
// overlay the key is swallowed, never a tool switch) and H shows or hides the cropped area;
// the box survives each key. They work with the keyboard on the canvas or on a bar control,
// and a text field keeps its letters. With the untouched frame O and H switch tools as on any
// tool. The Crop options menu shows the keys and the Overlay list's tooltip names O.
void ui_crop_keys_step_overlay_and_cropped_area_while_box_pending() {
  SettingsGroupRestorer restore_hotkeys(QStringLiteral("hotkeys"));
  SettingsValueRestorer saved_overlay(QStringLiteral("tools/cropOverlay"));
  SettingsValueRestorer saved_orientation(QStringLiteral("tools/cropOverlayOrientation"));
  SettingsValueRestorer saved_show(QStringLiteral("tools/cropShowCroppedArea"));
  SettingsValueRestorer saved_style(QStringLiteral("tools/cropStyle"));
  SettingsValueRestorer saved_ratio_w(QStringLiteral("tools/cropRatioWidth"));
  SettingsValueRestorer saved_ratio_h(QStringLiteral("tools/cropRatioHeight"));
  for (const auto* key : {"tools/cropOverlay", "tools/cropOverlayOrientation", "tools/cropShowCroppedArea",
                          "tools/cropStyle", "tools/cropRatioWidth", "tools/cropRatioHeight"}) {
    patchy::ui::app_settings().remove(QLatin1String(key));
  }
  using Overlay = patchy::ui::CanvasWidget::CropOverlay;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* crop_tool = require_action(window, "toolCropAction");
  crop_tool->trigger();
  QApplication::processEvents();
  canvas->set_snap_enabled(false);
  auto* overlay = window.findChild<QComboBox*>(QStringLiteral("cropOverlayCombo"));
  auto* presets = window.findChild<QComboBox*>(QStringLiteral("cropPresetCombo"));
  auto* ratio_w = window.findChild<QDoubleSpinBox*>(QStringLiteral("cropRatioWidthSpin"));
  auto* options = window.findChild<QToolButton*>(QStringLiteral("cropOptionsButton"));
  auto* next = window.findChild<QAction*>(QStringLiteral("cropOverlayNextAction"));
  auto* turn = window.findChild<QAction*>(QStringLiteral("cropOverlayTurnAction"));
  auto* show_area = window.findChild<QAction*>(QStringLiteral("cropShowCroppedAreaAction"));
  CHECK(overlay != nullptr && presets != nullptr && ratio_w != nullptr && options != nullptr &&
        options->menu() != nullptr && next != nullptr && turn != nullptr && show_area != nullptr);
  if (overlay == nullptr || presets == nullptr || ratio_w == nullptr || options == nullptr ||
      options->menu() == nullptr || next == nullptr || turn == nullptr || show_area == nullptr) {
    return;
  }
  const auto press = [](QWidget* target, Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QTest::keyClick(target, key, modifiers);
    QApplication::processEvents();
  };
  const auto lay_out_box = [&] {
    drag(*canvas, canvas->widget_position_for_document_point(QPoint(200, 200)),
         canvas->widget_position_for_document_point(QPoint(500, 440)));
  };
  canvas->setFocus();
  QApplication::processEvents();

  // The untouched frame: the letters are tool keys, as on any tool.
  CHECK(!canvas->crop_session_can_reset());
  press(canvas, Qt::Key_O);
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Dodge);
  crop_tool->trigger();
  QApplication::processEvents();
  press(canvas, Qt::Key_H);
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Pan);
  crop_tool->trigger();
  QApplication::processEvents();
  CHECK(overlay->currentIndex() == 1);  // Rule of Thirds: no key touched it

  // A changed box: O walks the six overlays and wraps past Golden Spiral to Rule of Thirds.
  lay_out_box();
  const QRect box(200, 200, 301, 241);
  CHECK(canvas->crop_session_rect() == box);
  CHECK(canvas->crop_session_can_reset());
  canvas->setFocus();
  for (const auto expected : {Overlay::Grid, Overlay::Diagonal, Overlay::Triangle, Overlay::GoldenRatio,
                              Overlay::GoldenSpiral, Overlay::Thirds}) {
    press(canvas, Qt::Key_O);
    CHECK(canvas->tool() == patchy::ui::CanvasTool::Crop);
    CHECK(canvas->crop_overlay() == expected);
    CHECK(overlay->currentIndex() == static_cast<int>(expected));
    CHECK(canvas->crop_session_rect() == box);
  }
  // From No Overlay the step lands on Rule of Thirds.
  overlay->setCurrentIndex(0);
  QApplication::processEvents();
  CHECK(canvas->crop_overlay() == Overlay::None);
  press(canvas, Qt::Key_O);
  CHECK(canvas->crop_overlay() == Overlay::Thirds);

  // Shift+O: nothing to turn on Rule of Thirds, and still no tool switch.
  const int orientation = canvas->crop_overlay_orientation();
  press(canvas, Qt::Key_O, Qt::ShiftModifier);
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Crop);
  CHECK(canvas->crop_overlay() == Overlay::Thirds);
  CHECK(canvas->crop_overlay_orientation() == orientation);
  CHECK(canvas->crop_session_rect() == box);
  overlay->setCurrentIndex(static_cast<int>(Overlay::Triangle));
  QApplication::processEvents();
  press(canvas, Qt::Key_O, Qt::ShiftModifier);
  CHECK(canvas->crop_overlay() == Overlay::Triangle);
  CHECK(canvas->crop_overlay_orientation() == (orientation + 1) % 4);
  CHECK(canvas->crop_session_rect() == box);

  // H shows or hides the cropped area.
  CHECK(show_area->isChecked() && canvas->crop_overlay_options().show_cropped_area);
  press(canvas, Qt::Key_H);
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Crop);
  CHECK(!show_area->isChecked() && !canvas->crop_overlay_options().show_cropped_area);
  CHECK(canvas->crop_session_rect() == box);
  press(canvas, Qt::Key_H);
  CHECK(show_area->isChecked() && canvas->crop_overlay_options().show_cropped_area);

  // The keys reach the crop from a bar control too (a list would otherwise search for the
  // letter, and the tool key would throw the box away); a text field keeps its letters.
  presets->setFocus();
  QApplication::processEvents();
  const int preset_before = presets->currentIndex();
  press(presets, Qt::Key_O);
  CHECK(canvas->crop_overlay() == Overlay::GoldenRatio);
  CHECK(presets->currentIndex() == preset_before);
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Crop && canvas->crop_session_rect() == box);
  ratio_w->setFocus();
  QApplication::processEvents();
  press(ratio_w, Qt::Key_O);
  press(ratio_w, Qt::Key_H);
  CHECK(canvas->crop_overlay() == Overlay::GoldenRatio);
  CHECK(show_area->isChecked());
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Crop && canvas->crop_session_rect() == box);
  canvas->setFocus();
  QApplication::processEvents();

  // The menu shows the keys beside its entries, and the Overlay list's tooltip names O.
  overlay->setCurrentIndex(static_cast<int>(Overlay::GoldenSpiral));
  QApplication::processEvents();
  options->menu()->popup(options->mapToGlobal(QPoint(0, options->height())));
  QApplication::processEvents();
  CHECK(options->menu()->actions().value(0) == next);
  const auto shift_o = QKeySequence(Qt::SHIFT | Qt::Key_O).toString(QKeySequence::NativeText);
  CHECK(next->text() == QStringLiteral("Next Overlay\tO"));
  CHECK(turn->text() == QStringLiteral("Turn Overlay\t") + shift_o);
  CHECK(show_area->text() == QStringLiteral("Show Cropped Area\tH"));
  save_window_popup_artifact("ui_crop_options_keys", window, *options, *options->menu());
  {
    // The whole window with the open menu painted under its button, for the issue shot.
    auto shot = window.grab().toImage();
    auto* menu = options->menu();
    auto origin = options->mapTo(&window, QPoint(0, options->height()));
    origin.setX(std::min(origin.x(), window.width() - menu->width()));
    QPainter painter(&shot);
    painter.drawPixmap(origin, menu->grab());
    painter.end();
    save_image_artifact("ui_crop_options_keys_window", shot);
  }
  options->menu()->hide();
  QApplication::processEvents();
  CHECK(overlay->toolTip().contains(QStringLiteral("O picks the next overlay once the crop box has been changed")));

  // The menu entry does what the key does.
  next->trigger();
  CHECK(canvas->crop_overlay() == Overlay::Thirds);
  send_key(*canvas, Qt::Key_Escape);
}

// GitHub issue 698: the crop keys are built in, so they yield to the user's own keys. With O
// and H given to other commands (as the GIMP-style set does: Eyedropper and Healing Brush),
// the letters run those commands even while a changed crop box is pending, and the menu and
// the tooltip stop naming them.
void ui_crop_keys_yield_to_reassigned_letters() {
  SettingsGroupRestorer restore_hotkeys(QStringLiteral("hotkeys"));
  SettingsValueRestorer saved_overlay(QStringLiteral("tools/cropOverlay"));
  SettingsValueRestorer saved_show(QStringLiteral("tools/cropShowCroppedArea"));
  for (const auto* key : {"tools/cropOverlay", "tools/cropShowCroppedArea"}) {
    patchy::ui::app_settings().remove(QLatin1String(key));
  }
  {
    auto settings = patchy::ui::app_settings();
    settings.beginGroup(QStringLiteral("hotkeys"));
    settings.setValue(QStringLiteral("tools.eyedropper"), QStringLiteral("O"));
    settings.setValue(QStringLiteral("tools.dodge"), QStringLiteral("Shift+D"));
    settings.setValue(QStringLiteral("tools.healing"), QStringLiteral("H"));
    settings.setValue(QStringLiteral("tools.hand"), QString());
    settings.endGroup();
    settings.sync();
  }
  using Overlay = patchy::ui::CanvasWidget::CropOverlay;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* crop_tool = require_action(window, "toolCropAction");
  crop_tool->trigger();
  QApplication::processEvents();
  canvas->set_snap_enabled(false);
  canvas->set_crop_ratio(0.0, 0.0);
  auto* overlay = window.findChild<QComboBox*>(QStringLiteral("cropOverlayCombo"));
  auto* options = window.findChild<QToolButton*>(QStringLiteral("cropOptionsButton"));
  auto* next = window.findChild<QAction*>(QStringLiteral("cropOverlayNextAction"));
  auto* turn = window.findChild<QAction*>(QStringLiteral("cropOverlayTurnAction"));
  auto* show_area = window.findChild<QAction*>(QStringLiteral("cropShowCroppedAreaAction"));
  CHECK(overlay != nullptr && options != nullptr && options->menu() != nullptr && next != nullptr &&
        turn != nullptr && show_area != nullptr);
  if (overlay == nullptr || options == nullptr || options->menu() == nullptr || next == nullptr || turn == nullptr ||
      show_area == nullptr) {
    return;
  }
  const auto lay_out_box = [&] {
    drag(*canvas, canvas->widget_position_for_document_point(QPoint(200, 200)),
         canvas->widget_position_for_document_point(QPoint(500, 440)));
    canvas->setFocus();
    QApplication::processEvents();
  };
  lay_out_box();
  CHECK(canvas->crop_session_can_reset());
  QTest::keyClick(canvas, Qt::Key_O);
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Eyedropper);
  CHECK(canvas->crop_overlay() == Overlay::Thirds);
  crop_tool->trigger();
  QApplication::processEvents();
  lay_out_box();
  QTest::keyClick(canvas, Qt::Key_H);
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Healing);
  CHECK(show_area->isChecked());
  crop_tool->trigger();
  QApplication::processEvents();
  // Shift+O is still the Dodge group's cycle here, so the crop keeps taking it.
  lay_out_box();
  QTest::keyClick(canvas, Qt::Key_O, Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Crop);

  options->menu()->popup(options->mapToGlobal(QPoint(0, options->height())));
  QApplication::processEvents();
  CHECK(next->text() == QStringLiteral("Next Overlay"));
  CHECK(turn->text() ==
        QStringLiteral("Turn Overlay\t") + QKeySequence(Qt::SHIFT | Qt::Key_O).toString(QKeySequence::NativeText));
  CHECK(show_area->text() == QStringLiteral("Show Cropped Area"));
  options->menu()->hide();
  QApplication::processEvents();
  CHECK(!overlay->toolTip().contains(QStringLiteral("picks the next overlay")));
  send_key(*canvas, Qt::Key_Escape);
}

}  // namespace

std::vector<patchy::test::TestCase> crop_tool_tests() {
  return {
      {"ui_crop_tool_activates_with_c_hotkey", ui_crop_tool_activates_with_c_hotkey},
      {"ui_perspective_crop_tool_joins_crop_flyout", ui_perspective_crop_tool_joins_crop_flyout},
      {"ui_perspective_crop_session_commits_and_undoes", ui_perspective_crop_session_commits_and_undoes},
      {"ui_perspective_crop_options_bar_and_grid", ui_perspective_crop_options_bar_and_grid},
      {"ui_script_document_perspective_crop", ui_script_document_perspective_crop},
      {"ui_perspective_crop_edges_rotate_and_up_marker", ui_perspective_crop_edges_rotate_and_up_marker},
      {"ui_perspective_crop_up_marker_over_light_page", ui_perspective_crop_up_marker_over_light_page},
      {"ui_crop_ratio_fields_show_trimmed_numbers", ui_crop_ratio_fields_show_trimmed_numbers},
      {"ui_crop_drag_out_geometry", ui_crop_drag_out_geometry},
      {"ui_crop_tool_frames_canvas_and_fits_ratio", ui_crop_tool_frames_canvas_and_fits_ratio},
      {"ui_crop_tool_adopts_active_selection", ui_crop_tool_adopts_active_selection},
      {"ui_crop_size_style_fields_mirror_and_resize_box", ui_crop_size_style_fields_mirror_and_resize_box},
      {"ui_crop_preset_list_offers_modes_ratios_and_sizes", ui_crop_preset_list_offers_modes_ratios_and_sizes},
      {"ui_crop_presets_save_delete_and_persist", ui_crop_presets_save_delete_and_persist},
      {"ui_crop_preset_list_restores_stored_style_and_ratio", ui_crop_preset_list_restores_stored_style_and_ratio},
      {"ui_crop_swap_buttons_turn_ratio_and_size", ui_crop_swap_buttons_turn_ratio_and_size},
      {"ui_crop_keys_step_overlay_and_cropped_area_while_box_pending",
       ui_crop_keys_step_overlay_and_cropped_area_while_box_pending},
      {"ui_crop_keys_yield_to_reassigned_letters", ui_crop_keys_yield_to_reassigned_letters},
      {"ui_crop_handles_resize_move_and_nudge", ui_crop_handles_resize_move_and_nudge},
      {"ui_crop_size_link_keeps_proportion_on_handle_drags", ui_crop_size_link_keeps_proportion_on_handle_drags},
      {"ui_crop_alt_handle_drag_resizes_about_center", ui_crop_alt_handle_drag_resizes_about_center},
      {"ui_crop_alt_drag_out_draws_from_center", ui_crop_alt_drag_out_draws_from_center},
      {"ui_crop_handle_drag_space_slides_box_then_resumes", ui_crop_handle_drag_space_slides_box_then_resumes},
      {"ui_crop_rotated_commit_straightens_box", ui_crop_rotated_commit_straightens_box},
      {"ui_crop_enter_commits_expanding_document", ui_crop_enter_commits_expanding_document},
      {"ui_crop_escape_resets_and_tool_switch_cancels", ui_crop_escape_resets_and_tool_switch_cancels},
      {"ui_crop_apply_cancel_buttons_follow_session", ui_crop_apply_cancel_buttons_follow_session},
      {"ui_crop_cancel_button_off_for_ratio_frame", ui_crop_cancel_button_off_for_ratio_frame},
      {"ui_crop_overlay_renders_shield_and_thirds", ui_crop_overlay_renders_shield_and_thirds},
      {"ui_crop_overlay_choices_draw_their_guides", ui_crop_overlay_choices_draw_their_guides},
      {"ui_crop_menu_commands_use_crop_box", ui_crop_menu_commands_use_crop_box},
      {"ui_crop_delete_options_crop_and_delete_layers", ui_crop_delete_options_crop_and_delete_layers},
      {"ui_crop_keeping_pixels_turns_background_into_layer_0", ui_crop_keeping_pixels_turns_background_into_layer_0},
      {"ui_crop_options_popup_sets_shield_and_cropped_area", ui_crop_options_popup_sets_shield_and_cropped_area},
      {"ui_trim_transparent_pixels_trims_sides_and_undoes", ui_trim_transparent_pixels_trims_sides_and_undoes},
      {"ui_trim_corner_colors_and_nothing_to_trim", ui_trim_corner_colors_and_nothing_to_trim},
      {"ui_trim_deep_documents", ui_trim_deep_documents},
      {"ui_trim_tolerance_cuts_near_matches", ui_trim_tolerance_cuts_near_matches},
      {"ui_script_trim", ui_script_trim},
  };
}
