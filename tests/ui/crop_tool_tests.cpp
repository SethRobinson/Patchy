// Crop tool: drag-out geometry, handle adjustment, Enter/Esc commit-cancel,
// canvas expansion on commit, and the options-bar ratio/apply/cancel row.

#include "ui_test_support.hpp"

#include "ui_test_groups.hpp"

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
  CHECK(!canvas->crop_session_active());
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

  // A plain click creates no session.
  const auto click_point = canvas->widget_position_for_document_point(QPoint(60, 60));
  send_mouse(*canvas, QEvent::MouseButtonPress, click_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, click_point, Qt::LeftButton, Qt::NoButton);
  CHECK(!canvas->crop_session_active());

  // A drag lays out the rect exactly (inclusive of both endpoints, the
  // marquee convention).
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(40, 40)),
       canvas->widget_position_for_document_point(QPoint(100, 80)));
  CHECK(canvas->crop_session_active());
  auto rect = canvas->crop_session_rect();
  CHECK(rect.has_value());
  CHECK(*rect == QRect(40, 40, 61, 41));

  send_key(*canvas, Qt::Key_Escape);
  CHECK(!canvas->crop_session_active());

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

  // Ratio fields constrain the drag-out; the dominant axis shrinks to fit.
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
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(120, 120)),
       canvas->widget_position_for_document_point(QPoint(240, 220)));
  rect = canvas->crop_session_rect();
  CHECK(rect.has_value());
  const auto ratio = static_cast<double>(rect->width()) / static_cast<double>(rect->height());
  CHECK(ratio > 1.9);
  CHECK(ratio < 2.1);
  send_key(*canvas, Qt::Key_Escape);

  // Clear zeroes both fields and lifts the constraint.
  auto* clear_button = window.findChild<QPushButton*>(QStringLiteral("cropRatioClearButton"));
  CHECK(clear_button != nullptr);
  clear_button->click();
  QApplication::processEvents();
  CHECK(canvas->crop_ratio_width() == 0.0);
  CHECK(canvas->crop_ratio_height() == 0.0);

  // The preset combo defaults to None, presets fill the fields, manual values
  // read back as Custom, and Original Ratio derives from the document.
  auto* preset_combo = window.findChild<QComboBox*>(QStringLiteral("cropRatioPresetCombo"));
  CHECK(preset_combo != nullptr);
  CHECK(preset_combo->currentText() == QStringLiteral("None"));
  preset_combo->setCurrentIndex(2);  // 1 : 1 (Square)
  QApplication::processEvents();
  CHECK(canvas->crop_ratio_width() == 1.0);
  CHECK(canvas->crop_ratio_height() == 1.0);
  ratio_w->setValue(3.0);
  ratio_h->setValue(7.0);
  QApplication::processEvents();
  CHECK(preset_combo->currentText() == QStringLiteral("Custom"));
  preset_combo->setCurrentIndex(1);  // Original Ratio: 1024 x 768 reduces to 4 : 3
  QApplication::processEvents();
  CHECK(canvas->crop_ratio_width() == 4.0);
  CHECK(canvas->crop_ratio_height() == 3.0);
  CHECK(preset_combo->currentIndex() == 1);
  clear_button->click();
  QApplication::processEvents();
  CHECK(preset_combo->currentText() == QStringLiteral("None"));
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
  CHECK(!canvas->crop_session_active());
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
  CHECK(!canvas->crop_session_active());
  CHECK(info_label->text().contains(QStringLiteral("121 x 81 px")));

  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(info_label->text().contains(QStringLiteral("1024 x 768 px")));
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
  CHECK(!canvas->crop_session_active());
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

void ui_crop_escape_and_tool_switch_cancel() {
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
  CHECK(canvas->crop_session_active());
  send_key(*canvas, Qt::Key_Escape);
  CHECK(!canvas->crop_session_active());
  CHECK(info_label->text().contains(QStringLiteral("1024 x 768 px")));

  // Re-picking the Crop tool keeps the session; a real switch cancels it
  // without committing.
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(50, 50)),
       canvas->widget_position_for_document_point(QPoint(150, 130)));
  CHECK(canvas->crop_session_active());
  require_action(window, "toolCropAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->crop_session_active());
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
  CHECK(!apply->isEnabled());
  CHECK(!cancel->isEnabled());

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(100, 100)),
       canvas->widget_position_for_document_point(QPoint(400, 300)));
  CHECK(canvas->crop_session_active());
  CHECK(apply->isEnabled());
  CHECK(cancel->isEnabled());

  cancel->click();
  QApplication::processEvents();
  CHECK(!canvas->crop_session_active());
  CHECK(!apply->isEnabled());

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(100, 100)),
       canvas->widget_position_for_document_point(QPoint(400, 300)));
  const auto committed = canvas->crop_session_rect();
  CHECK(committed.has_value());
  apply->click();
  QApplication::processEvents();
  CHECK(!canvas->crop_session_active());
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

}  // namespace

std::vector<patchy::test::TestCase> crop_tool_tests() {
  return {
      {"ui_crop_tool_activates_with_c_hotkey", ui_crop_tool_activates_with_c_hotkey},
      {"ui_crop_drag_out_geometry", ui_crop_drag_out_geometry},
      {"ui_crop_handles_resize_move_and_nudge", ui_crop_handles_resize_move_and_nudge},
      {"ui_crop_rotated_commit_straightens_box", ui_crop_rotated_commit_straightens_box},
      {"ui_crop_enter_commits_expanding_document", ui_crop_enter_commits_expanding_document},
      {"ui_crop_escape_and_tool_switch_cancel", ui_crop_escape_and_tool_switch_cancel},
      {"ui_crop_apply_cancel_buttons_follow_session", ui_crop_apply_cancel_buttons_follow_session},
      {"ui_crop_overlay_renders_shield_and_thirds", ui_crop_overlay_renders_shield_and_thirds},
  };
}
