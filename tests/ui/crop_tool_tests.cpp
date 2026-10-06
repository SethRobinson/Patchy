// Crop tool: the canvas frame on activation, drag-out geometry, handle
// adjustment, ratio re-fits, Enter/Esc commit-reset, canvas expansion on
// commit, and the options-bar ratio/apply/reset row.

#include "ui_test_support.hpp"

#include "ui_test_groups.hpp"

#include <QCheckBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QStatusBar>
#include <QTimer>

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

// Image > Crop to Selection and Crop to Selection (Advanced) act on the Crop
// tool's pending box while the tool is active, with no marquee selection: the
// plain command commits like Enter, the advanced one prefills the Canvas Size
// dialog with the box, and the untouched canvas frame is refused with a message.
// The dialog's "Delete cropped pixels and layers too" box drives the two
// per-option checkboxes and mirrors them.
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

  // The advanced command opens the Canvas Size dialog prefilled with the box.
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
    auto* delete_cropped = dialog->findChild<QCheckBox*>(QStringLiteral("canvasSizeDeleteCroppedCheck"));
    auto* crop_layers = dialog->findChild<QCheckBox*>(QStringLiteral("canvasSizeCropLayersCheck"));
    auto* delete_off_canvas = dialog->findChild<QCheckBox*>(QStringLiteral("canvasSizeDeleteOffCanvasCheck"));
    CHECK(width != nullptr && height != nullptr && delete_cropped != nullptr && crop_layers != nullptr &&
          delete_off_canvas != nullptr);
    CHECK(dialog->windowTitle() == QStringLiteral("Crop to Selection (Advanced)"));
    CHECK(width->value() == static_cast<double>(box->width()));
    CHECK(height->value() == static_cast<double>(box->height()));
    CHECK(delete_cropped->text() == QStringLiteral("Delete cropped pixels and layers too"));
    // Off on every opening; the combined box sets both options and mirrors them.
    CHECK(!delete_cropped->isChecked() && !crop_layers->isChecked() && !delete_off_canvas->isChecked());
    delete_cropped->setChecked(true);
    CHECK(crop_layers->isChecked() && delete_off_canvas->isChecked());
    crop_layers->setChecked(false);
    CHECK(!delete_cropped->isChecked() && delete_off_canvas->isChecked());
    crop_layers->setChecked(true);
    CHECK(delete_cropped->isChecked());
    delete_cropped->setChecked(false);
    CHECK(!crop_layers->isChecked() && !delete_off_canvas->isChecked());
    save_widget_artifact("ui_crop_advanced_from_crop_box", *dialog);
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
}

}  // namespace

std::vector<patchy::test::TestCase> crop_tool_tests() {
  return {
      {"ui_crop_tool_activates_with_c_hotkey", ui_crop_tool_activates_with_c_hotkey},
      {"ui_crop_drag_out_geometry", ui_crop_drag_out_geometry},
      {"ui_crop_tool_frames_canvas_and_fits_ratio", ui_crop_tool_frames_canvas_and_fits_ratio},
      {"ui_crop_tool_adopts_active_selection", ui_crop_tool_adopts_active_selection},
      {"ui_crop_size_style_fields_mirror_and_resize_box", ui_crop_size_style_fields_mirror_and_resize_box},
      {"ui_crop_handles_resize_move_and_nudge", ui_crop_handles_resize_move_and_nudge},
      {"ui_crop_alt_handle_drag_resizes_about_center", ui_crop_alt_handle_drag_resizes_about_center},
      {"ui_crop_handle_drag_space_slides_box_then_resumes", ui_crop_handle_drag_space_slides_box_then_resumes},
      {"ui_crop_rotated_commit_straightens_box", ui_crop_rotated_commit_straightens_box},
      {"ui_crop_enter_commits_expanding_document", ui_crop_enter_commits_expanding_document},
      {"ui_crop_escape_resets_and_tool_switch_cancels", ui_crop_escape_resets_and_tool_switch_cancels},
      {"ui_crop_apply_cancel_buttons_follow_session", ui_crop_apply_cancel_buttons_follow_session},
      {"ui_crop_overlay_renders_shield_and_thirds", ui_crop_overlay_renders_shield_and_thirds},
      {"ui_crop_menu_commands_use_crop_box", ui_crop_menu_commands_use_crop_box},
  };
}
