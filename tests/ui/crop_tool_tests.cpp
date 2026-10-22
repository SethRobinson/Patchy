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
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "ui_test_access.hpp"
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
#include <QAbstractItemView>
#include <QMenu>
#include <QPainter>
#include <QTimer>
#include <QToolButton>
#include "ui/color_panel.hpp"
#include <cstdlib>

namespace {

using namespace patchy::test::ui;

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

  // The preset combo defaults to None, presets fill the fields, manual values
  // read back as Custom, and Original Ratio derives from the document.
  auto* preset_combo = window.findChild<QComboBox*>(QStringLiteral("cropRatioPresetCombo"));
  CHECK(preset_combo != nullptr);
  CHECK(preset_combo->currentText() == QStringLiteral("None"));
  preset_combo->setCurrentIndex(2);  // 1 : 1 (Square)
  QApplication::processEvents();
  CHECK(canvas->crop_ratio_width() == 1.0);
  CHECK(canvas->crop_ratio_height() == 1.0);
  CHECK(canvas->crop_session_rect() == QRect(128, 0, 768, 768));
  ratio_w->setValue(3.0);
  ratio_h->setValue(7.0);
  QApplication::processEvents();
  CHECK(preset_combo->currentText() == QStringLiteral("Custom"));
  preset_combo->setCurrentIndex(1);  // Original Ratio: 1024 x 768 reduces to 4 : 3
  QApplication::processEvents();
  CHECK(canvas->crop_ratio_width() == 4.0);
  CHECK(canvas->crop_ratio_height() == 3.0);
  CHECK(preset_combo->currentIndex() == 1);
  CHECK(canvas->crop_session_rect() == QRect(0, 0, 1024, 768));
  clear_button->click();
  QApplication::processEvents();
  CHECK(preset_combo->currentText() == QStringLiteral("None"));
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

// The Style combo: Ratio shows the preset/ratio row, Size shows unit Width /
// Height fields that mirror the box and resize it about its center (linked
// when the chain button is down), handle drags flow back into the fields, and
// the remembered ratio returns with Ratio mode. tools/cropStyle persists.
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

  auto* style = window.findChild<QComboBox*>(QStringLiteral("cropStyleCombo"));
  auto* preset = window.findChild<QComboBox*>(QStringLiteral("cropRatioPresetCombo"));
  auto* ratio_w = window.findChild<QDoubleSpinBox*>(QStringLiteral("cropRatioWidthSpin"));
  auto* ratio_h = window.findChild<QDoubleSpinBox*>(QStringLiteral("cropRatioHeightSpin"));
  auto* width = window.findChild<QSpinBox*>(QStringLiteral("cropWidthSpin"));
  auto* height = window.findChild<QSpinBox*>(QStringLiteral("cropHeightSpin"));
  auto* link = window.findChild<QPushButton*>(QStringLiteral("cropLinkSizeButton"));
  CHECK(style != nullptr);
  CHECK(preset != nullptr);
  CHECK(ratio_w != nullptr);
  CHECK(ratio_h != nullptr);
  CHECK(width != nullptr);
  CHECK(height != nullptr);
  CHECK(link != nullptr);
  if (style == nullptr || preset == nullptr || ratio_w == nullptr || ratio_h == nullptr || width == nullptr ||
      height == nullptr || link == nullptr) {
    return;
  }
  style->setCurrentIndex(0);
  ratio_w->setValue(2.0);
  ratio_h->setValue(1.0);
  QApplication::processEvents();
  CHECK(style->currentText() == QStringLiteral("Ratio"));
  CHECK(preset->isVisible());
  CHECK(ratio_w->isVisible());
  CHECK(!width->isVisible());
  CHECK(!height->isVisible());
  CHECK(canvas->crop_session_rect() == QRect(0, 128, 1024, 512));

  // Size: the ratio row hides, the constraint lifts (the automatic frame grows
  // back to the canvas; a custom box would keep its shape), and the fields
  // mirror the box.
  style->setCurrentIndex(1);
  QApplication::processEvents();
  CHECK(!preset->isVisible());
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

  // Back to Ratio: the remembered 2 : 1 returns and re-fits the frame.
  style->setCurrentIndex(0);
  QApplication::processEvents();
  CHECK(canvas->crop_ratio_width() == 2.0);
  CHECK(ratio_w->value() == 2.0);
  CHECK(canvas->crop_session_rect() == QRect(0, 128, 1024, 512));
  CHECK(preset->isVisible());
  CHECK(!width->isVisible());

  // The style persists across windows.
  style->setCurrentIndex(1);
  QApplication::processEvents();
  window.close();
  QApplication::processEvents();
  patchy::ui::MainWindow second;
  show_window(second);
  require_action(second, "toolCropAction")->trigger();
  QApplication::processEvents();
  auto* second_style = second.findChild<QComboBox*>(QStringLiteral("cropStyleCombo"));
  CHECK(second_style != nullptr);
  if (second_style != nullptr) {
    CHECK(second_style->currentIndex() == 1);
  }
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
  auto* canvas = require_canvas(window);
  require_action(window, "toolCropAction")->trigger();
  QApplication::processEvents();
  canvas->set_snap_enabled(false);

  auto* style = window.findChild<QComboBox*>(QStringLiteral("cropStyleCombo"));
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
  style->setCurrentIndex(1);
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

  auto* style = window.findChild<QComboBox*>(QStringLiteral("cropStyleCombo"));
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
  style->setCurrentIndex(0);
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
  style->setCurrentIndex(1);
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
  pick_when_open = [&] {
    auto* picker = patchy::ui::open_color_picker_dialog();
    if (picker == nullptr) {
      QTimer::singleShot(10, &window, pick_when_open);
      return;
    }
    CHECK(patchy::ui::apply_color_to_open_color_picker(QColor(200, 0, 0)));
    picked = true;
    picker->accept();
  };
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
  QTimer::singleShot(0, [&] {
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
  });
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

}  // namespace

std::vector<patchy::test::TestCase> crop_tool_tests() {
  return {
      {"ui_crop_tool_activates_with_c_hotkey", ui_crop_tool_activates_with_c_hotkey},
      {"ui_crop_ratio_fields_show_trimmed_numbers", ui_crop_ratio_fields_show_trimmed_numbers},
      {"ui_crop_drag_out_geometry", ui_crop_drag_out_geometry},
      {"ui_crop_tool_frames_canvas_and_fits_ratio", ui_crop_tool_frames_canvas_and_fits_ratio},
      {"ui_crop_tool_adopts_active_selection", ui_crop_tool_adopts_active_selection},
      {"ui_crop_size_style_fields_mirror_and_resize_box", ui_crop_size_style_fields_mirror_and_resize_box},
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
      {"ui_crop_options_popup_sets_shield_and_cropped_area", ui_crop_options_popup_sets_shield_and_cropped_area},
  };
}
