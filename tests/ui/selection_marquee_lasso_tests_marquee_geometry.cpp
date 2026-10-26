#include "psd/psd_document_io.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/measurement_units.hpp"
#include "ui/main_window.hpp"
#include "ui/app_settings.hpp"
#include "test_harness.hpp"
#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QStyle>
#include <QStyleOptionSpinBox>
#include <QTabWidget>
#include <QTest>
#include <QTimer>
#include <QToolBar>
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
#include "local_psd_fixtures.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

namespace {

using namespace patchy::test::ui;

void ui_marquee_selection_modifiers_work() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);

  drag(*canvas, QPoint(60, 60), QPoint(100, 100));
  auto selection = canvas->selected_document_rect();
  CHECK(selection.has_value());
  CHECK(selection->contains(QPoint(25, 25)));

  drag(*canvas, QPoint(130, 130), QPoint(170, 170), Qt::ShiftModifier);
  selection = canvas->selected_document_rect();
  CHECK(selection.has_value());
  CHECK(selection->contains(QPoint(25, 25)));
  CHECK(selection->contains(QPoint(125, 125)));
  CHECK(canvas->selected_document_region().contains(QPoint(25, 25)));
  CHECK(canvas->selected_document_region().contains(QPoint(125, 125)));
  CHECK(!canvas->selected_document_region().contains(QPoint(75, 75)));
  const auto added_width = selection->width();

  drag(*canvas, QPoint(120, 50), QPoint(180, 180), Qt::AltModifier);
  selection = canvas->selected_document_rect();
  CHECK(selection.has_value());
  CHECK(selection->width() < added_width);
  save_widget_artifact("ui_selection_modifiers", *canvas);

  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();
  CHECK(!canvas->has_selection());
}

void ui_marquee_click_outside_canvas_deselects() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);

  // A widget point mapping to a document coordinate outside the canvas.
  const auto grey = canvas->widget_position_for_document_point(QPoint(-40, -40));
  CHECK(canvas->rect().contains(grey));

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(20, 20)),
       canvas->widget_position_for_document_point(QPoint(80, 80)));
  CHECK(canvas->has_selection());

  // A plain click in the grey area clears the selection.
  send_mouse(*canvas, QEvent::MouseButtonPress, grey, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, grey, Qt::LeftButton, Qt::NoButton);
  CHECK(!canvas->has_selection());

  // A drag that starts in the grey area still selects, clamped to the canvas.
  drag(*canvas, grey, canvas->widget_position_for_document_point(QPoint(80, 80)));
  const auto sel = canvas->selected_document_rect();
  CHECK(sel.has_value());
  CHECK(sel->left() >= 0);
  CHECK(sel->top() >= 0);
  CHECK(sel->contains(QPoint(10, 10)));
}

// GitHub issue 142: the W x H readout is a HUD pill right beside the pointer,
// not bare text 24 px away that vanished on busy artwork.
void ui_marquee_drag_readout_draws_a_pill_by_the_pointer() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  const auto from = canvas->widget_position_for_document_point(QPoint(40, 40));
  const auto to = canvas->widget_position_for_document_point(QPoint(240, 160));
  send_mouse(*canvas, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  const auto image = canvas->grab().toImage();
  const auto hud = patchy::ui::theme().canvas_hud_bg;  // drawn at alpha 236 over the white canvas
  const QColor expected((hud.red() * 236 + 255 * 19) / 255, (hud.green() * 236 + 255 * 19) / 255,
                        (hud.blue() * 236 + 255 * 19) / 255);
  CHECK(color_close(image.pixelColor(to + QPoint(15, 17)), expected, 24));
  // Below the pill there is only canvas.
  CHECK(color_close(image.pixelColor(to + QPoint(15, 60)), Qt::white, 8));
  send_mouse(*canvas, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
}

void ui_marquee_shift_drag_constrains_to_square() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);

  const auto start = canvas->widget_position_for_document_point(QPoint(40, 40));
  const auto wide_end = canvas->widget_position_for_document_point(QPoint(200, 100));  // 160x60 drag

  // No active selection: Shift from the press constrains to a square (side = 60).
  send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
  send_mouse(*canvas, QEvent::MouseMove, wide_end, Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, wide_end, Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
  auto sel = canvas->selected_document_rect();
  CHECK(sel.has_value());
  CHECK(sel->width() == sel->height());
  CHECK(sel->width() > 40);
  CHECK(sel->width() < 120);

  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();

  // Also constrains when Shift is pressed only after the drag starts.
  send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  send_mouse(*canvas, QEvent::MouseMove, wide_end, Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, wide_end, Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
  sel = canvas->selected_document_rect();
  CHECK(sel.has_value());
  CHECK(sel->width() == sel->height());

  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();

  // With an existing selection, Shift at press adds a free shape (not a square):
  // the far corner, outside a 60x60 square, stays selected.
  const auto add_start = canvas->widget_position_for_document_point(QPoint(200, 40));
  const auto add_end = canvas->widget_position_for_document_point(QPoint(360, 100));
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(40, 40)),
       canvas->widget_position_for_document_point(QPoint(80, 80)));
  CHECK(canvas->has_selection());
  send_mouse(*canvas, QEvent::MouseButtonPress, add_start, Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
  send_mouse(*canvas, QEvent::MouseMove, add_end, Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, add_end, Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
  CHECK(canvas->selected_document_region().contains(QPoint(350, 50)));

  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();

  // In add-mode, releasing and re-pressing Shift mid-drag constrains the added
  // shape to a square (via key events, with no intervening mouse move).
  const auto add_mid = canvas->widget_position_for_document_point(QPoint(280, 70));
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(40, 40)),
       canvas->widget_position_for_document_point(QPoint(80, 80)));
  CHECK(canvas->has_selection());
  send_mouse(*canvas, QEvent::MouseButtonPress, add_start, Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
  send_mouse(*canvas, QEvent::MouseMove, add_mid, Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
  send_key_release(*canvas, Qt::Key_Shift);
  send_key_press(*canvas, Qt::Key_Shift);
  send_mouse(*canvas, QEvent::MouseMove, add_end, Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, add_end, Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
  CHECK(canvas->selected_document_region().contains(QPoint(210, 50)));
  CHECK(!canvas->selected_document_region().contains(QPoint(350, 50)));

  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();
  canvas->set_snap_enabled(false);
  canvas->zoom_to_document_rect(QRect(90, 90, 20, 20));

  // Tiny mixed-axis drags must still resolve to an exact square (2x5 -> 2x2).
  const auto tiny_start = canvas->widget_position_for_document_point(QPoint(100, 100));
  const auto tiny_end = canvas->widget_position_for_document_point(QPoint(102, 95));  // 2x5 drag
  send_mouse(*canvas, QEvent::MouseButtonPress, tiny_start, Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
  send_mouse(*canvas, QEvent::MouseMove, tiny_end, Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, tiny_end, Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
  sel = canvas->selected_document_rect();
  CHECK(sel.has_value());
  CHECK(sel->size() == QSize(2, 2));

  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();

  // A constrained drag begun in the grey area anchors the square at the edge it
  // enters, not at the off-canvas press point.
  const auto document_rect = canvas->active_layer_document_rect();
  CHECK(document_rect.has_value());
  const auto right_edge = document_rect->right() + 1;
  const auto edge_y = document_rect->top() + 100;
  canvas->zoom_to_document_rect(QRect(right_edge - 30, edge_y - 10, 50, 30));

  const auto outside_start = canvas->widget_position_for_document_point(QPoint(right_edge + 20, edge_y));
  const auto inside_left = canvas->widget_position_for_document_point(QPoint(right_edge - 20, edge_y));
  const auto inside_left_down = canvas->widget_position_for_document_point(QPoint(right_edge - 20, edge_y + 2));
  send_mouse(*canvas, QEvent::MouseButtonPress, outside_start, Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
  send_mouse(*canvas, QEvent::MouseMove, inside_left, Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
  send_mouse(*canvas, QEvent::MouseMove, inside_left_down, Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, inside_left_down, Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
  sel = canvas->selected_document_rect();
  CHECK(sel.has_value());
  CHECK(*sel == QRect(right_edge - 2, edge_y, 2, 2));
}

void ui_marquee_drag_keeps_minimum_one_pixel() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);

  const auto anchor = canvas->widget_position_for_document_point(QPoint(100, 100));

  // Cursor 1px left of the anchor, dragged far vertically: the width axis used to
  // collapse to 0 (QRect::normalized); the big vertical motion keeps it a drag.
  drag(*canvas, anchor, canvas->widget_position_for_document_point(QPoint(99, 300)));
  auto sel = canvas->selected_document_rect();
  CHECK(sel.has_value());
  CHECK(sel->width() >= 1);
  CHECK(sel->height() >= 1);

  // Symmetric case: cursor 1px above the anchor, dragged far horizontally.
  drag(*canvas, anchor, canvas->widget_position_for_document_point(QPoint(300, 99)));
  sel = canvas->selected_document_rect();
  CHECK(sel.has_value());
  CHECK(sel->width() >= 1);
  CHECK(sel->height() >= 1);
}

void ui_selection_toolbar_modes_work() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Marquee"))->trigger();

  auto* new_mode = require_action(window, "selectionNewModeAction");
  auto* add_mode = require_action(window, "selectionAddModeAction");
  auto* subtract_mode = require_action(window, "selectionSubtractModeAction");
  auto* intersect_mode = require_action(window, "selectionIntersectModeAction");
  CHECK(new_mode->isChecked());

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(20, 20)),
       canvas->widget_position_for_document_point(QPoint(70, 70)));
  CHECK(canvas->selected_document_region().contains(QPoint(35, 35)));

  add_mode->trigger();
  QApplication::processEvents();
  CHECK(add_mode->isChecked());
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(120, 120)),
       canvas->widget_position_for_document_point(QPoint(170, 170)));
  CHECK(canvas->selected_document_region().contains(QPoint(35, 35)));
  CHECK(canvas->selected_document_region().contains(QPoint(145, 145)));
  CHECK(!canvas->selected_document_region().contains(QPoint(95, 95)));

  subtract_mode->trigger();
  QApplication::processEvents();
  CHECK(subtract_mode->isChecked());
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(34, 34)),
       canvas->widget_position_for_document_point(QPoint(56, 56)));
  CHECK(!canvas->selected_document_region().contains(QPoint(45, 45)));
  CHECK(canvas->selected_document_region().contains(QPoint(25, 25)));
  CHECK(canvas->selected_document_region().contains(QPoint(145, 145)));

  intersect_mode->trigger();
  QApplication::processEvents();
  CHECK(intersect_mode->isChecked());
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(132, 132)),
       canvas->widget_position_for_document_point(QPoint(158, 158)));
  CHECK(!canvas->selected_document_region().contains(QPoint(25, 25)));
  CHECK(canvas->selected_document_region().contains(QPoint(145, 145)));
  CHECK(!canvas->selected_document_region().contains(QPoint(125, 125)));

  new_mode->trigger();
  QApplication::processEvents();
  CHECK(new_mode->isChecked());
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(210, 30)),
       canvas->widget_position_for_document_point(QPoint(250, 70)));
  CHECK(canvas->selected_document_region().contains(QPoint(225, 45)));
  CHECK(!canvas->selected_document_region().contains(QPoint(145, 145)));
  save_widget_artifact("ui_selection_toolbar_modes", *canvas);
}

void ui_ctrl_a_selects_entire_canvas() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  const auto document_rect = canvas->active_layer_document_rect();
  CHECK(document_rect.has_value());
  CHECK(!canvas->has_selection());

  send_key(*canvas, Qt::Key_A, Qt::ControlModifier);
  QApplication::processEvents();

  const auto selection = canvas->selected_document_rect();
  CHECK(selection.has_value());
  CHECK(selection->topLeft() == QPoint(0, 0));
  CHECK(selection->size() == document_rect->size());
  CHECK(canvas->selected_document_region().contains(QPoint(0, 0)));
  CHECK(canvas->selected_document_region().contains(document_rect->bottomRight()));

  canvas->contract_selection(10);
  QApplication::processEvents();
  auto contracted_selection = canvas->selected_document_rect();
  CHECK(contracted_selection.has_value());
  CHECK(contracted_selection->topLeft() == QPoint(10, 10));
  CHECK(contracted_selection->bottomRight() == document_rect->bottomRight() - QPoint(10, 10));
  CHECK(!canvas->selected_document_region().contains(QPoint(9, 9)));
  CHECK(canvas->selected_document_region().contains(QPoint(10, 10)));

  require_action(window, "editSelectAllAction")->trigger();
  QApplication::processEvents();
  accept_integer_dialog(QStringLiteral("patchyContractSelectionDialog"), 123);
  require_action(window, "selectContractAction")->trigger();
  QApplication::processEvents();
  contracted_selection = canvas->selected_document_rect();
  CHECK(contracted_selection.has_value());
  CHECK(contracted_selection->topLeft() == QPoint(123, 123));
  CHECK(contracted_selection->bottomRight() == document_rect->bottomRight() - QPoint(123, 123));
  CHECK(!canvas->selected_document_region().contains(QPoint(122, 122)));
  CHECK(canvas->selected_document_region().contains(QPoint(123, 123)));
  save_widget_artifact("ui_ctrl_a_select_all", *canvas);
}

void ui_alt_backspace_fills_selection_with_foreground() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_primary_color(QColor(20, 140, 230));

  require_action_by_text(window, QStringLiteral("Marquee"))->trigger();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(80, 70)),
       canvas->widget_position_for_document_point(QPoint(180, 150)));
  QApplication::processEvents();
  CHECK(canvas->has_selection());

  use_solid_fill_settings(canvas);
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(120, 100)), QColor(20, 140, 230), 12));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(40, 40)), Qt::white, 8));
  save_widget_artifact("ui_alt_backspace_fill_selection", *canvas);
}

void ui_feathered_marquee_fill_uses_soft_selection_alpha() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_primary_color(QColor(25, 90, 230));

  require_action_by_text(window, QStringLiteral("Marquee"))->trigger();
  auto* feather = window.findChild<QSpinBox*>(QStringLiteral("selectionFeatherSpin"));
  auto* anti_alias = window.findChild<QCheckBox*>(QStringLiteral("selectionAntiAliasCheck"));
  CHECK(feather != nullptr);
  CHECK(anti_alias != nullptr);
  feather->setValue(20);
  anti_alias->setChecked(true);
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Marquee);
  CHECK(canvas->selection_feather_radius() == 20);
  CHECK(canvas->selection_antialias());

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(120, 90)),
       canvas->widget_position_for_document_point(QPoint(260, 210)));
  QApplication::processEvents();
  CHECK(canvas->has_selection());
  const auto selection_rect = canvas->selected_document_rect();
  CHECK(selection_rect.has_value());
  std::optional<QPoint> solid_point;
  std::optional<QPoint> feather_point;
  for (int y = selection_rect->top(); y <= selection_rect->bottom(); ++y) {
    for (int x = selection_rect->left(); x <= selection_rect->right(); ++x) {
      const auto alpha = canvas->selection_alpha_at(QPoint(x, y));
      if (!solid_point.has_value() && alpha > 240) {
        solid_point = QPoint(x, y);
      }
      if (!feather_point.has_value() && alpha > 40 && alpha < 220) {
        feather_point = QPoint(x, y);
      }
    }
  }
  CHECK(solid_point.has_value());
  CHECK(feather_point.has_value());
  const QPoint hard_corner(120, 90);
  const QPoint hard_top_edge(190, 90);
  const auto corner_alpha = canvas->selection_alpha_at(hard_corner);
  const auto top_edge_alpha = canvas->selection_alpha_at(hard_top_edge);
  const auto center_alpha = canvas->selection_alpha_at(QPoint(190, 150));
  CHECK(corner_alpha > 0);
  CHECK(top_edge_alpha > corner_alpha);
  CHECK(center_alpha > top_edge_alpha);
  CHECK(canvas->selection_alpha_at(QPoint(95, 150)) > 0);
  CHECK(canvas->selection_alpha_at(QPoint(82, 150)) < canvas->selection_alpha_at(QPoint(95, 150)));

  use_solid_fill_settings(canvas);
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  canvas->set_selection_edges_visible(false);
  QApplication::processEvents();
  const auto inside = canvas_pixel(*canvas, *solid_point);
  CHECK(inside.blue() > 180);
  CHECK(inside.blue() > inside.green());
  const auto feathered = canvas_pixel(*canvas, *feather_point);
  CHECK(feathered.blue() > feathered.green());
  CHECK(feathered.green() > 110);
  CHECK(feathered.green() < 245);
  const auto corner_fill = canvas_pixel(*canvas, hard_corner);
  const auto top_edge_fill = canvas_pixel(*canvas, hard_top_edge);
  CHECK(corner_fill.green() > top_edge_fill.green());
  CHECK(corner_fill.red() > top_edge_fill.red());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(70, 150)), Qt::white, 8));
  canvas->set_selection_edges_visible(true);
  QApplication::processEvents();
  save_widget_artifact("ui_feathered_marquee_fill", *canvas);
}

void ui_feathered_selection_add_keeps_existing_selection() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Marquee"))->trigger();
  auto* feather = window.findChild<QSpinBox*>(QStringLiteral("selectionFeatherSpin"));
  CHECK(feather != nullptr);
  feather->setValue(15);
  QApplication::processEvents();
  CHECK(canvas->selection_feather_radius() == 15);

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(40, 40)),
       canvas->widget_position_for_document_point(QPoint(120, 120)));
  QApplication::processEvents();
  CHECK(canvas->has_selection());
  CHECK(canvas->selection_alpha_at(QPoint(80, 80)) > 200);

  // Shift+drag a disjoint rectangle: Add must keep the first feathered blob.
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(200, 60)),
       canvas->widget_position_for_document_point(QPoint(280, 140)), Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(canvas->has_selection());
  CHECK(canvas->selection_alpha_at(QPoint(80, 80)) > 200);
  CHECK(canvas->selection_alpha_at(QPoint(240, 100)) > 200);

  // The Options-bar Add mode (no modifier) must behave the same.
  require_action(window, "selectionAddModeAction")->trigger();
  QApplication::processEvents();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(60, 200)),
       canvas->widget_position_for_document_point(QPoint(140, 280)));
  QApplication::processEvents();
  CHECK(canvas->selection_alpha_at(QPoint(80, 80)) > 200);
  CHECK(canvas->selection_alpha_at(QPoint(240, 100)) > 200);
  CHECK(canvas->selection_alpha_at(QPoint(100, 240)) > 200);

  // Feathered lasso add: trace a triangle well away from the existing blobs.
  require_action_by_text(window, QStringLiteral("Lasso"))->trigger();
  QApplication::processEvents();
  const auto lasso_a = canvas->widget_position_for_document_point(QPoint(220, 200));
  const auto lasso_b = canvas->widget_position_for_document_point(QPoint(300, 200));
  const auto lasso_c = canvas->widget_position_for_document_point(QPoint(260, 290));
  send_mouse(*canvas, QEvent::MouseButtonPress, lasso_a, Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
  send_mouse(*canvas, QEvent::MouseMove, lasso_b, Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
  send_mouse(*canvas, QEvent::MouseMove, lasso_c, Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, lasso_c, Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(canvas->selection_alpha_at(QPoint(80, 80)) > 200);
  CHECK(canvas->selection_alpha_at(QPoint(240, 100)) > 200);
  CHECK(canvas->selection_alpha_at(QPoint(100, 240)) > 200);
  CHECK(canvas->selection_alpha_at(QPoint(258, 225)) > 100);
  save_widget_artifact("ui_feathered_selection_add", *canvas);
}

void ui_marquee_corner_radius_rounds_selection() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Marquee"))->trigger();
  QApplication::processEvents();
  auto* radius = window.findChild<QSpinBox*>(QStringLiteral("selectionCornerRadiusSpin"));
  CHECK(radius != nullptr);
  CHECK(radius->isVisible());
  // Feather is a plain label and field like Radius: the same gap after its
  // label (GitHub issue 106) and the same full-height field, never clipped by
  // a container (GitHub issue 160).
  auto* feather_field = window.findChild<QSpinBox*>(QStringLiteral("selectionFeatherSpin"));
  auto* feather_label = window.findChild<QLabel*>(QStringLiteral("selectionFeatherLabel"));
  QLabel* radius_label = nullptr;
  for (auto* label : window.findChildren<QLabel*>()) {
    if (label->isVisible() && label->property("optionLabel").toBool() && label->text() == QStringLiteral("Radius")) {
      radius_label = label;
    }
  }
  CHECK(feather_field != nullptr && feather_label != nullptr && radius_label != nullptr);
  CHECK(feather_label->isVisible() && feather_field->isVisible());
  CHECK(feather_label->text() == QStringLiteral("Feather"));
  CHECK(feather_field->parentWidget() == radius->parentWidget());
  CHECK(feather_field->x() - (feather_label->x() + feather_label->width()) ==
        radius->x() - (radius_label->x() + radius_label->width()));
  CHECK(feather_field->height() == radius->height());
  CHECK(feather_field->y() == radius->y());
  CHECK(feather_field->parentWidget()->rect().contains(feather_field->geometry()));
  if (auto* anti_alias = window.findChild<QWidget*>(QStringLiteral("selectionAntiAliasCheck"));
      anti_alias != nullptr && anti_alias->isVisible()) {
    auto* bar = feather_field->parentWidget();
    const QRect strip(feather_label->x() - 8, 0, anti_alias->geometry().right() + 16 - feather_label->x(),
                      bar->height());
    save_image_artifact("issue160_feather_field", bar->grab(strip).toImage());
  }
  radius->setValue(30);
  QApplication::processEvents();
  CHECK(canvas->marquee_corner_radius() == 30);

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(60, 60)),
       canvas->widget_position_for_document_point(QPoint(220, 180)));
  QApplication::processEvents();
  CHECK(canvas->has_selection());
  const auto& region = canvas->selected_document_region();
  CHECK(region.contains(QPoint(140, 120)));  // center
  CHECK(region.contains(QPoint(140, 60)));   // top edge midpoint stays flat
  CHECK(region.contains(QPoint(60, 120)));   // left edge midpoint stays flat
  CHECK(!region.contains(QPoint(61, 61)));   // all four corners are rounded away
  CHECK(!region.contains(QPoint(218, 61)));
  CHECK(!region.contains(QPoint(61, 178)));
  CHECK(!region.contains(QPoint(218, 178)));
  save_widget_artifact("ui_marquee_corner_radius", *canvas);

  // Radius zero restores sharp corners. (Deselect first: a drag that starts
  // inside the current selection would move its outline instead.)
  require_action(window, "editDeselectAction")->trigger();
  radius->setValue(0);
  QApplication::processEvents();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(60, 60)),
       canvas->widget_position_for_document_point(QPoint(220, 180)));
  QApplication::processEvents();
  CHECK(canvas->selected_document_region().contains(QPoint(61, 61)));

  // Rounded corners compose with feather: solid center, soft corner falloff.
  require_action(window, "editDeselectAction")->trigger();
  radius->setValue(40);
  auto* feather = window.findChild<QSpinBox*>(QStringLiteral("selectionFeatherSpin"));
  CHECK(feather != nullptr);
  feather->setValue(10);
  QApplication::processEvents();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(60, 60)),
       canvas->widget_position_for_document_point(QPoint(220, 180)));
  QApplication::processEvents();
  CHECK(canvas->has_selection());
  CHECK(canvas->selection_alpha_at(QPoint(140, 120)) > 240);
  CHECK(canvas->selection_alpha_at(QPoint(61, 61)) < canvas->selection_alpha_at(QPoint(140, 120)));
  feather->setValue(0);
  QApplication::processEvents();

  // The radius control is a rectangular-marquee option only.
  require_action_by_text(window, QStringLiteral("Elliptical Marquee"))->trigger();
  QApplication::processEvents();
  CHECK(!radius->isVisible());
}

void ui_marquee_fixed_size_and_ratio_options_work() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Marquee"))->trigger();

  auto* style = window.findChild<QComboBox*>(QStringLiteral("selectionStyleCombo"));
  auto* width = window.findChild<QSpinBox*>(QStringLiteral("selectionFixedWidthSpin"));
  auto* height = window.findChild<QSpinBox*>(QStringLiteral("selectionFixedHeightSpin"));
  CHECK(style != nullptr);
  CHECK(width != nullptr);
  CHECK(height != nullptr);
  CHECK(style->currentText() == QStringLiteral("Normal"));

  width->setValue(80);
  height->setValue(50);
  style->setCurrentText(QStringLiteral("Fixed Size"));
  QApplication::processEvents();
  CHECK(canvas->marquee_style() == patchy::ui::CanvasWidget::MarqueeStyle::FixedSize);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(30, 30)),
       canvas->widget_position_for_document_point(QPoint(55, 55)));
  auto selection = canvas->selected_document_rect();
  CHECK(selection.has_value());
  CHECK(selection->topLeft() == QPoint(30, 30));
  CHECK(selection->width() == 80);
  CHECK(selection->height() == 50);
  CHECK(canvas->selected_document_region().contains(QPoint(105, 75)));
  CHECK(!canvas->selected_document_region().contains(QPoint(112, 84)));

  // Fixed Ratio swaps in its own unitless pair, 1 : 1 by default, and hides the pixel
  // fields; Fixed Size keeps its pixels (GitHub issue 201).
  CHECK(width->isVisible() && height->isVisible());
  style->setCurrentText(QStringLiteral("Fixed Ratio"));
  QApplication::processEvents();
  auto* ratio_width = window.findChild<QDoubleSpinBox*>(QStringLiteral("selectionRatioWidthSpin"));
  auto* ratio_height = window.findChild<QDoubleSpinBox*>(QStringLiteral("selectionRatioHeightSpin"));
  CHECK(ratio_width != nullptr && ratio_height != nullptr);
  if (ratio_width == nullptr || ratio_height == nullptr) {
    return;
  }
  CHECK(ratio_width->isVisible() && ratio_height->isVisible());
  CHECK(!width->isVisible() && !height->isVisible());
  CHECK(ratio_width->value() == 1.0 && ratio_height->value() == 1.0);
  CHECK(ratio_width->suffix().isEmpty() && !ratio_width->text().contains(QStringLiteral("px")));
  // Whole ratio parts read like the pixel fields, without ".00".
  CHECK(ratio_width->text() == QStringLiteral("1") && ratio_height->text() == QStringLiteral("1"));
  save_widget_artifact("ui_marquee_fixed_ratio_options_bar", *window.findChild<QToolBar*>(QStringLiteral("Options")));
  ratio_width->setValue(2);
  ratio_height->setValue(1);
  QApplication::processEvents();
  CHECK(ratio_width->text() == QStringLiteral("2") && ratio_height->text() == QStringLiteral("1"));
  save_widget_artifact("ui_marquee_fixed_ratio_options_bar_2_1", *window.findChild<QToolBar*>(QStringLiteral("Options")));
  CHECK(width->value() == 80 && height->value() == 50);
  CHECK(canvas->marquee_style() == patchy::ui::CanvasWidget::MarqueeStyle::FixedRatio);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(180, 40)),
       canvas->widget_position_for_document_point(QPoint(300, 140)));
  selection = canvas->selected_document_rect();
  CHECK(selection.has_value());
  const auto ratio = static_cast<double>(selection->width()) / static_cast<double>(selection->height());
  CHECK(ratio > 1.85);
  CHECK(ratio < 2.15);
  CHECK(canvas->selected_document_region().contains(QPoint(230, 64)));
  CHECK(!canvas->selected_document_region().contains(QPoint(230, 130)));
  save_widget_artifact("ui_marquee_fixed_size_ratio", *canvas);

  // Back to Fixed Size: the pixel pair returns with its values, the ratio pair hides.
  style->setCurrentText(QStringLiteral("Fixed Size"));
  QApplication::processEvents();
  CHECK(width->isVisible() && height->isVisible() && width->value() == 80);
  CHECK(!ratio_width->isVisible() && !ratio_height->isVisible());

  // A fractional part keeps its significant decimals only: typed "1.50" is 1.5 and
  // reads "1.5"; 0.25 keeps both decimals.
  auto* ratio_editor = ratio_width->findChild<QLineEdit*>();
  CHECK(ratio_editor != nullptr);
  if (ratio_editor != nullptr) {
    ratio_editor->setText(QStringLiteral("1.50"));
    ratio_width->interpretText();
    CHECK(ratio_width->value() == 1.5);
    CHECK(ratio_width->text() == QStringLiteral("1.5"));
  }
  ratio_height->setValue(0.25);
  CHECK(ratio_height->text() == QStringLiteral("0.25"));
}

// GitHub issue 286: Fixed Ratio and Fixed Size each carry a swap button between Width and
// Height, as the Artboard bar does; Normal, whose fields are greyed, shows neither.
void ui_marquee_swap_button_swaps_width_and_height() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action(window, "toolMarqueeAction")->trigger();
  QApplication::processEvents();
  auto* style = window.findChild<QComboBox*>(QStringLiteral("selectionStyleCombo"));
  auto* width = window.findChild<QSpinBox*>(QStringLiteral("selectionFixedWidthSpin"));
  auto* height = window.findChild<QSpinBox*>(QStringLiteral("selectionFixedHeightSpin"));
  auto* ratio_width = window.findChild<QDoubleSpinBox*>(QStringLiteral("selectionRatioWidthSpin"));
  auto* ratio_height = window.findChild<QDoubleSpinBox*>(QStringLiteral("selectionRatioHeightSpin"));
  auto* size_swap = window.findChild<QAbstractButton*>(QStringLiteral("selectionFixedSizeSwapButton"));
  auto* ratio_swap = window.findChild<QAbstractButton*>(QStringLiteral("selectionRatioSwapButton"));
  CHECK(style != nullptr && width != nullptr && height != nullptr);
  CHECK(ratio_width != nullptr && ratio_height != nullptr);
  CHECK(size_swap != nullptr && ratio_swap != nullptr);
  if (style == nullptr || width == nullptr || height == nullptr || ratio_width == nullptr ||
      ratio_height == nullptr || size_swap == nullptr || ratio_swap == nullptr) {
    return;
  }

  style->setCurrentText(QStringLiteral("Normal"));
  QApplication::processEvents();
  CHECK(!size_swap->isVisible() && !ratio_swap->isVisible());

  style->setCurrentText(QStringLiteral("Fixed Size"));
  width->setValue(80);
  height->setValue(50);
  QApplication::processEvents();
  CHECK(size_swap->isVisible() && !ratio_swap->isVisible());
  CHECK(!size_swap->toolTip().isEmpty());
  QTest::mouseClick(size_swap, Qt::LeftButton);
  QApplication::processEvents();
  CHECK(width->value() == 50 && height->value() == 80);
  CHECK(canvas->marquee_fixed_size() == QSize(50, 80));
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(30, 30)),
       canvas->widget_position_for_document_point(QPoint(40, 40)));
  auto selection = canvas->selected_document_rect();
  CHECK(selection.has_value());
  CHECK(selection.has_value() && selection->size() == QSize(50, 80));

  style->setCurrentText(QStringLiteral("Fixed Ratio"));
  ratio_width->setValue(2);
  ratio_height->setValue(1);
  QApplication::processEvents();
  CHECK(ratio_swap->isVisible() && !size_swap->isVisible());
  QTest::mouseClick(ratio_swap, Qt::LeftButton);
  QApplication::processEvents();
  CHECK(ratio_width->value() == 1.0 && ratio_height->value() == 2.0);
  // The pixel pair keeps its own values.
  CHECK(width->value() == 50 && height->value() == 80);
  canvas->clear_selection();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(180, 40)),
       canvas->widget_position_for_document_point(QPoint(300, 140)));
  selection = canvas->selected_document_rect();
  CHECK(selection.has_value());
  if (selection.has_value()) {
    const auto ratio = static_cast<double>(selection->width()) / static_cast<double>(selection->height());
    CHECK(ratio > 0.45 && ratio < 0.55);
  }
}

// Style: Normal greys out the marquee's Width and Height, and a click on a greyed field's
// chevron must not open its value slider (GitHub issue 194); Fixed Size enables both again.
// The artifact draws the options bar with any open value popup where it sits.
void ui_marquee_disabled_size_fields_open_no_value_popup() {
  patchy::ui::MainWindow window;
  show_window(window);
  require_action_by_text(window, QStringLiteral("Marquee"))->trigger();
  QApplication::processEvents();
  auto* style = window.findChild<QComboBox*>(QStringLiteral("selectionStyleCombo"));
  auto* bar = window.findChild<QToolBar*>(QStringLiteral("Options"));
  CHECK(style != nullptr);
  CHECK(bar != nullptr);
  style->setCurrentText(QStringLiteral("Normal"));
  QApplication::processEvents();
  const auto click_chevron = [](QSpinBox& spin) {
    auto* editor = spin.findChild<QLineEdit*>();
    CHECK(editor != nullptr);
    QTest::mouseClick(editor, Qt::LeftButton, Qt::NoModifier, QPoint(editor->width() - 2, editor->height() / 2));
    QApplication::processEvents();
  };
  const auto popup_of = [&window](const char* base) {
    return window.findChild<QFrame*>(QString::fromLatin1(base) + QStringLiteral("Popup"));
  };
  const auto save_bar_with_popup = [bar](const std::string& name, QWidget* popup) {
    const auto bar_image = bar->grab().toImage();
    QImage image(bar_image.width(), bar_image.height() + 120, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.drawImage(0, 0, bar_image);
    if (popup != nullptr && popup->isVisible()) {
      painter.drawImage(popup->mapToGlobal(QPoint()) - bar->mapToGlobal(QPoint()), popup->grab().toImage());
    }
    painter.end();
    save_image_artifact(name, image);
  };

  for (const char* base : {"selectionFixedWidth", "selectionFixedHeight"}) {
    auto* spin = window.findChild<QSpinBox*>(QString::fromLatin1(base) + QStringLiteral("Spin"));
    CHECK(spin != nullptr);
    CHECK(!spin->isEnabled());
    const auto value = spin->value();
    click_chevron(*spin);
    auto* popup = popup_of(base);
    if (std::string(base) == "selectionFixedWidth") {
      save_bar_with_popup("ui_marquee_disabled_size_fields_open_no_value_popup", popup);
    }
    CHECK(popup == nullptr || !popup->isVisible());
    if (popup != nullptr) {
      popup->close();
      QApplication::processEvents();
    }
    CHECK(spin->value() == value);
  }

  auto* width = window.findChild<QSpinBox*>(QStringLiteral("selectionFixedWidthSpin"));
  auto* height = window.findChild<QSpinBox*>(QStringLiteral("selectionFixedHeightSpin"));
  const auto disabled_widths = std::make_pair(width->width(), height->width());
  style->setCurrentText(QStringLiteral("Fixed Size"));
  QApplication::processEvents();
  CHECK(width->isEnabled());
  // Greying out keeps each field at its enabled width.
  CHECK(std::make_pair(width->width(), height->width()) == disabled_widths);
  click_chevron(*width);
  auto* popup = popup_of("selectionFixedWidth");
  CHECK(popup != nullptr && popup->isVisible());
  // Greying the field out while its popup is open closes the popup.
  style->setCurrentText(QStringLiteral("Normal"));
  QApplication::processEvents();
  popup = popup_of("selectionFixedWidth");
  CHECK(popup == nullptr || !popup->isVisible());
  if (popup != nullptr) {
    popup->close();
  }
  style->setCurrentText(QStringLiteral("Normal"));
  QApplication::processEvents();
}

void ui_shape_fill_and_corner_radius_apply_to_new_documents() {
  SettingsValueRestorer saved_shape_corner_radius(QStringLiteral("tools/shapeCornerRadius"));
  SettingsValueRestorer saved_vector_mode(QStringLiteral("tools/vectorToolMode"));
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Rect"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Rectangle);
  // This test exercises the legacy raster commit (fill/softness/radius are
  // Pixels-mode options); Shape mode would create a shape layer instead.
  auto* vector_mode = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  CHECK(vector_mode != nullptr);
  vector_mode->setCurrentIndex(2);

  auto* fill_check = window.findChild<QCheckBox*>(QStringLiteral("shapeFillCheck"));
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  CHECK(fill_check != nullptr);
  CHECK(radius_spin != nullptr);
  fill_check->setChecked(true);
  radius_spin->setValue(7);
  QApplication::processEvents();
  CHECK(canvas->fill_shapes());
  CHECK(canvas->shape_corner_radius() == 7);

  accept_new_document_dialog(320, 240);
  require_action_by_text(window, QStringLiteral("New"))->trigger();
  QApplication::processEvents();
  auto* new_canvas = require_canvas(window);
  CHECK(new_canvas != canvas);
  CHECK(new_canvas->tool() == patchy::ui::CanvasTool::Rectangle);
  CHECK(new_canvas->fill_shapes());
  CHECK(new_canvas->shape_corner_radius() == 7);

  new_canvas->set_primary_color(Qt::black);
  new_canvas->set_brush_opacity(100);
  new_canvas->set_brush_softness(0);
  drag(*new_canvas, new_canvas->widget_position_for_document_point(QPoint(40, 40)),
       new_canvas->widget_position_for_document_point(QPoint(200, 160)));
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*new_canvas, QPoint(120, 100)), Qt::black, 10));
  save_widget_artifact("ui_shape_fill_new_document", *new_canvas);
}

void ui_shape_tool_fixed_size_and_ratio_options_work() {
  SettingsValueRestorer saved_vector_mode(QStringLiteral("tools/vectorToolMode"));
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Rect"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Rectangle);
  // Raster stamping is Pixels-mode behavior (Shape mode would add layers).
  auto* vector_mode = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  CHECK(vector_mode != nullptr);
  vector_mode->setCurrentIndex(2);

  auto* style = window.findChild<QComboBox*>(QStringLiteral("shapeStyleCombo"));
  auto* width = window.findChild<QSpinBox*>(QStringLiteral("shapeFixedWidthSpin"));
  auto* height = window.findChild<QSpinBox*>(QStringLiteral("shapeFixedHeightSpin"));
  CHECK(style != nullptr);
  CHECK(width != nullptr);
  CHECK(height != nullptr);
  CHECK(style->isVisible());
  CHECK(style->currentText() == QStringLiteral("Normal"));

  auto* fill_check = window.findChild<QCheckBox*>(QStringLiteral("shapeFillCheck"));
  CHECK(fill_check != nullptr);
  fill_check->setChecked(true);
  canvas->set_primary_color(Qt::black);
  canvas->set_brush_opacity(100);
  canvas->set_brush_softness(0);

  // Fixed Size: a short drag stamps exactly W x H extending down-right from the press point.
  width->setValue(80);
  height->setValue(50);
  style->setCurrentText(QStringLiteral("Fixed Size"));
  QApplication::processEvents();
  CHECK(canvas->shape_style() == patchy::ui::CanvasWidget::MarqueeStyle::FixedSize);
  CHECK(canvas->shape_fixed_size() == QSize(80, 50));
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(30, 30)),
       canvas->widget_position_for_document_point(QPoint(55, 55)));
  QApplication::processEvents();
  // Scan region covering every shape this test stamps (default document is 1024x768).
  const auto document_rect = QRect(0, 0, 320, 240);
  auto bounds = dark_document_bounds(*canvas, document_rect);
  CHECK(bounds.has_value());
  CHECK(bounds->topLeft() == QPoint(30, 30));
  CHECK(bounds->width() == 80);
  CHECK(bounds->height() == 50);

  // A plain click (zero-length drag) stamps the same W x H shape.
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  const auto click_point = canvas->widget_position_for_document_point(QPoint(120, 90));
  drag(*canvas, click_point, click_point);
  QApplication::processEvents();
  bounds = dark_document_bounds(*canvas, document_rect);
  CHECK(bounds.has_value());
  CHECK(bounds->topLeft() == QPoint(120, 90));
  CHECK(bounds->width() == 80);
  CHECK(bounds->height() == 50);

  // Fixed Ratio: the drag follows the cursor but keeps W:H proportions.
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  style->setCurrentText(QStringLiteral("Fixed Ratio"));
  width->setValue(2);
  height->setValue(1);
  QApplication::processEvents();
  CHECK(canvas->shape_style() == patchy::ui::CanvasWidget::MarqueeStyle::FixedRatio);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(40, 40)),
       canvas->widget_position_for_document_point(QPoint(160, 140)));
  QApplication::processEvents();
  bounds = dark_document_bounds(*canvas, document_rect);
  CHECK(bounds.has_value());
  const auto ratio = static_cast<double>(bounds->width()) / static_cast<double>(bounds->height());
  CHECK(ratio > 1.85);
  CHECK(ratio < 2.15);
  save_widget_artifact("ui_shape_fixed_size_ratio", *canvas);

  // The ellipse tool shares the options; Fixed Size bounds its W x H bounding box.
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  require_action_by_text(window, QStringLiteral("Ellipse"))->trigger();
  QApplication::processEvents();
  CHECK(style->isVisible());
  style->setCurrentText(QStringLiteral("Fixed Size"));
  width->setValue(80);
  height->setValue(50);
  QApplication::processEvents();
  const auto ellipse_click = canvas->widget_position_for_document_point(QPoint(60, 60));
  drag(*canvas, ellipse_click, ellipse_click);
  QApplication::processEvents();
  bounds = dark_document_bounds(*canvas, document_rect);
  CHECK(bounds.has_value());
  CHECK(std::abs(bounds->width() - 80) <= 2);
  CHECK(std::abs(bounds->height() - 50) <= 2);

  // The shape style widgets are shape-tool options only; the marquee keeps its own set.
  require_action_by_text(window, QStringLiteral("Marquee"))->trigger();
  QApplication::processEvents();
  CHECK(!style->isVisible());
  CHECK(!width->isVisible());
  CHECK(!height->isVisible());
  CHECK(window.findChild<QComboBox*>(QStringLiteral("selectionStyleCombo"))->isVisible());
}

void ui_elliptical_marquee_selects_oval_region() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Elliptical Marquee"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::EllipticalMarquee);

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(80, 60)),
       canvas->widget_position_for_document_point(QPoint(180, 140)));
  const auto selection = canvas->selected_document_rect();
  CHECK(selection.has_value());
  CHECK(selection->contains(QPoint(130, 100)));
  CHECK(canvas->selected_document_region().contains(QPoint(130, 100)));
  CHECK(!canvas->selected_document_region().contains(QPoint(82, 62)));
  CHECK(!canvas->selected_document_region().contains(QPoint(178, 62)));
  save_widget_artifact("ui_elliptical_marquee", *canvas);
}

void ui_marquee_space_drag_repositions_active_rect() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  canvas->set_snap_enabled(false);

  const auto start = canvas->widget_position_for_document_point(QPoint(40, 40));
  const auto first_corner = canvas->widget_position_for_document_point(QPoint(100, 80));
  const auto moved_corner = canvas->widget_position_for_document_point(QPoint(130, 110));
  const auto resized_corner = canvas->widget_position_for_document_point(QPoint(150, 140));

  send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, first_corner, Qt::NoButton, Qt::LeftButton);
  const auto original = canvas->selected_document_rect();
  CHECK(original.has_value());

  send_key_press(*canvas, Qt::Key_Space);
  send_mouse(*canvas, QEvent::MouseMove, moved_corner, Qt::NoButton, Qt::LeftButton);
  const auto moved = canvas->selected_document_rect();
  CHECK(moved.has_value());
  CHECK(moved->size() == original->size());
  CHECK(moved->topLeft() == original->topLeft() + QPoint(30, 30));
  CHECK(canvas->selected_document_region().contains(QPoint(80, 75)));
  CHECK(!canvas->selected_document_region().contains(QPoint(50, 45)));

  send_key_release(*canvas, Qt::Key_Space);
  send_mouse(*canvas, QEvent::MouseMove, resized_corner, Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, resized_corner, Qt::LeftButton, Qt::NoButton);
  const auto resized = canvas->selected_document_rect();
  CHECK(resized.has_value());
  CHECK(resized->topLeft() == moved->topLeft());
  CHECK(resized->width() > moved->width());
  CHECK(resized->height() > moved->height());
  save_widget_artifact("ui_marquee_space_drag_reposition", *canvas);
}

void ui_info_panel_shows_selection_rect() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* info_label = window.findChild<QLabel*>(QStringLiteral("canvasInfoLabel"));
  CHECK(info_label != nullptr);
  CHECK(info_label->textInteractionFlags().testFlag(Qt::TextSelectableByMouse));

  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  canvas->set_snap_enabled(false);

  const auto start = canvas->widget_position_for_document_point(QPoint(40, 40));
  const auto end = canvas->widget_position_for_document_point(QPoint(100, 80));
  send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);

  const auto committed = canvas->selected_document_rect();
  CHECK(committed.has_value());
  const auto expected_rect_line = QStringLiteral("Selection: %1 x %2  at %3, %4")
                                      .arg(committed->width())
                                      .arg(committed->height())
                                      .arg(committed->x())
                                      .arg(committed->y());
  CHECK(info_label->text().contains(expected_rect_line));

  // The committed rect stays readable after the cursor leaves the document.
  send_mouse(*canvas, QEvent::MouseMove, canvas->widget_position_for_document_point(QPoint(-30, -30)), Qt::NoButton,
             Qt::NoButton);
  CHECK(info_label->text().contains(QStringLiteral("X: -")));
  CHECK(info_label->text().contains(expected_rect_line));

  // A non-rectangular selection reports its bounding box under a distinct label.
  canvas->invert_selection();
  CHECK(canvas->selected_document_region().rectCount() > 1);
  CHECK(info_label->text().contains(QStringLiteral("Selection bounds: ")));

  // Selection edits made without mouse motion still refresh the panel.
  canvas->clear_selection();
  CHECK(info_label->text().contains(QStringLiteral("Rect: -")));
}

void ui_rulers_grid_guides_render_and_edit() {
  patchy::Document document(96, 64, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_pixels(96, 64, patchy::PixelFormat::rgb8(), Qt::white));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(360, 260);
  canvas.set_document(&document);
  canvas.set_zoom(2.0);
  canvas.set_rulers_visible(true);
  canvas.set_grid_visible(true);
  canvas.set_guides_visible(true);
  canvas.set_grid_subdivisions(4);
  canvas.set_snap_enabled(false);
  CHECK(canvas.guide_color() == QColor(255, 70, 180, 230));
  CHECK(!color_close(canvas.guide_color(), canvas.grid_color(), 48));
  canvas.add_guide(patchy::GuideOrientation::Vertical, 20 * 32);
  canvas.add_guide(patchy::GuideOrientation::Horizontal, 30 * 32);
  canvas.show();
  QApplication::processEvents();
  CHECK(document.guides().size() == 2);
  save_widget_artifact("ui_rulers_grid_guides", canvas);

  const QPoint ruler_start(canvas.widget_position_for_document_point(QPoint(42, 0)).x(), 12);
  const auto new_guide_target = canvas.widget_position_for_document_point(QPoint(42, 18));
  send_mouse(canvas, QEvent::MouseButtonPress, ruler_start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, new_guide_target, Qt::NoButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, new_guide_target, Qt::LeftButton, Qt::NoButton);
  CHECK(document.guides().size() == 3);
  CHECK(document.guides().back().orientation == patchy::GuideOrientation::Horizontal);
  CHECK(std::abs(document.guides().back().position_32 - 18 * 32) <= 1);

  const QPoint left_ruler_start(12, canvas.widget_position_for_document_point(QPoint(0, 22)).y());
  const auto vertical_guide_target = canvas.widget_position_for_document_point(QPoint(42, 22));
  send_mouse(canvas, QEvent::MouseButtonPress, left_ruler_start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, vertical_guide_target, Qt::NoButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, vertical_guide_target, Qt::LeftButton, Qt::NoButton);
  CHECK(document.guides().size() == 4);
  CHECK(document.guides().back().orientation == patchy::GuideOrientation::Vertical);
  CHECK(std::abs(document.guides().back().position_32 - 42 * 32) <= 1);
  CHECK(canvas.has_selected_guides());

  canvas.set_tool(patchy::ui::CanvasTool::Marquee);
  drag(canvas, canvas.widget_position_for_document_point(QPoint(55, 8)),
       canvas.widget_position_for_document_point(QPoint(70, 18)));
  CHECK(!canvas.has_selected_guides());
  canvas.clear_selection();

  canvas.set_tool(patchy::ui::CanvasTool::Move);
  const auto move_start = canvas.widget_position_for_document_point(QPoint(20, 50));
  const auto move_end = canvas.widget_position_for_document_point(QPoint(25, 50));
  send_mouse(canvas, QEvent::MouseButtonPress, move_start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, move_end, Qt::NoButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, move_end, Qt::LeftButton, Qt::NoButton);
  CHECK(std::abs(document.guides()[0].position_32 - 25 * 32) <= 1);

  canvas.set_guides_locked(true);
  canvas.clear_selected_guides();
  CHECK(document.guides().size() == 4);

  canvas.set_guides_locked(false);
  canvas.clear_selected_guides();
  CHECK(document.guides().size() == 3);
  save_widget_artifact("ui_guides_editing", canvas);
}

// GitHub issue 197: at 1000% one screen pixel is a tenth of a document pixel, and a
// dragged guide used to land wherever the pointer was (Y: 10.41 px). A free drag now
// lands on the nearest whole pixel, halves up like the rest of the pixel grid, in the
// readout and in the stored position. A guide loaded at a fraction keeps it until it is
// actually dragged, and grabbing it off-center does not make it jump to the pointer.
// The 40 x 30 document fits the canvas at 1000%, so the capture shows the guide.
void ui_guide_drag_lands_on_whole_pixels() {
  patchy::Document document(40, 30, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_pixels(40, 30, patchy::PixelFormat::rgb8(), Qt::white));
  // A guide at 20.53125 px, the kind a PSD made elsewhere can carry.
  document.guides().push_back(patchy::DocumentGuide{patchy::GuideOrientation::Horizontal, 20 * 32 + 17});

  patchy::ui::CanvasWidget canvas;
  canvas.resize(460, 340);
  canvas.set_document(&document);
  canvas.set_zoom(10.0);
  canvas.set_rulers_visible(true);
  canvas.set_guides_visible(true);
  canvas.set_snap_enabled(false);
  canvas.show();
  QApplication::processEvents();

  // A horizontal guide from the top ruler, released 4 and then 6 screen pixels (0.4 and
  // 0.6 document pixels) below the top of row 10.
  const auto row_10 = canvas.widget_position_for_document_point(QPoint(12, 10));
  const QPoint top_ruler(row_10.x(), 12);
  send_mouse(canvas, QEvent::MouseButtonPress, top_ruler, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, row_10 + QPoint(0, 4), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  save_widget_artifact("issue197_guide_drag_readout_1000pct", canvas);
  auto readout = canvas.transform_drag_readout();
  CHECK(readout.has_value());
  if (readout.has_value() && !readout->canvas_lines.empty()) {
    std::cout << "guide readout at 1000%: " << readout->canvas_lines[0].toStdString() << std::endl;
    CHECK(readout->canvas_lines[0] == QStringLiteral("Y: 10 px"));
  }
  send_mouse(canvas, QEvent::MouseMove, row_10 + QPoint(0, 6), Qt::NoButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, row_10 + QPoint(0, 6), Qt::LeftButton, Qt::NoButton);
  CHECK(document.guides().size() == 2);
  if (document.guides().size() == 2) {
    std::cout << "new guide position_32: " << document.guides().back().position_32 << std::endl;
    CHECK(document.guides().back().position_32 == 11 * 32);
  }

  // A press on the fractional guide that never moves leaves it where it was.
  canvas.set_tool(patchy::ui::CanvasTool::Move);
  const auto on_fraction = canvas.widget_position_for_document_point(QPoint(12, 20)) + QPoint(0, 5);
  send_mouse(canvas, QEvent::MouseButtonPress, on_fraction, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, on_fraction, Qt::LeftButton, Qt::NoButton);
  CHECK(document.guides().front().position_32 == 20 * 32 + 17);

  // Dragging it 30 screen pixels (3 document pixels) keeps the grab point and rounds:
  // 20.53 + 3 = 23.53 lands on 24.
  send_mouse(canvas, QEvent::MouseButtonPress, on_fraction, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, on_fraction + QPoint(0, 30), Qt::NoButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, on_fraction + QPoint(0, 30), Qt::LeftButton, Qt::NoButton);
  std::cout << "moved guide position_32: " << document.guides().front().position_32 << std::endl;
  CHECK(document.guides().front().position_32 == 24 * 32);
}

// Dragging a guide shows its position beside the pointer in the ruler unit
// (GitHub issue 36), mirrors it to the status bar, hides it while the drop
// would remove the guide, and clears it on release.
void ui_guide_drag_shows_position_readout() {
  patchy::Document document(300, 200, patchy::PixelFormat::rgb8());
  document.print_settings().horizontal_ppi = 100.0;
  document.print_settings().vertical_ppi = 100.0;
  document.add_pixel_layer("Background", solid_pixels(300, 200, patchy::PixelFormat::rgb8(), Qt::white));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(460, 340);
  canvas.set_document(&document);
  canvas.set_zoom(1.0);
  canvas.set_rulers_visible(true);
  canvas.set_guides_visible(true);
  canvas.set_snap_enabled(false);
  QString status_text;
  canvas.set_status_callback([&status_text](QString text) { status_text = std::move(text); });
  canvas.show();
  QApplication::processEvents();
  CHECK(canvas.show_transform_drag_values());
  CHECK(!canvas.transform_drag_readout().has_value());

  // Pixels: a horizontal guide from the top ruler reads its Y position.
  const QPoint top_ruler(canvas.widget_position_for_document_point(QPoint(42, 0)).x(), 12);
  const auto pixel_target = canvas.widget_position_for_document_point(QPoint(42, 60));
  send_mouse(canvas, QEvent::MouseButtonPress, top_ruler, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, pixel_target, Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  auto readout = canvas.transform_drag_readout();
  CHECK(readout.has_value());
  CHECK(readout->canvas_lines.size() == 1);
  CHECK(readout->canvas_lines[0] == QStringLiteral("Y: 60 px"));
  CHECK(readout->lines.size() == 1);
  CHECK(readout->lines[0].contains(QStringLiteral("60 px")));
  CHECK(status_text == readout->lines[0]);
  const auto panel = canvas.drag_readout_widget_rect();
  CHECK(!panel.isEmpty());
  CHECK(panel.contains(pixel_target + QPoint(20, 30)));
  const auto frame = canvas.grab().toImage();
  CHECK(color_close(frame.pixelColor(panel.left() + 3, panel.top() + 2), patchy::ui::theme().canvas_hud_bg, 24));
  save_widget_artifact("ui_guide_drag_position_readout", canvas);

  // Back over the ruler the drop would discard the guide: no readout.
  send_mouse(canvas, QEvent::MouseMove, top_ruler, Qt::NoButton, Qt::LeftButton);
  CHECK(!canvas.transform_drag_readout().has_value());
  send_mouse(canvas, QEvent::MouseMove, pixel_target, Qt::NoButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, pixel_target, Qt::LeftButton, Qt::NoButton);
  CHECK(document.guides().size() == 1);
  CHECK(!canvas.transform_drag_readout().has_value());
  CHECK(canvas.drag_readout_widget_rect().isEmpty());

  // Millimeters: moving that guide reads in mm through the document PPI.
  canvas.set_ruler_unit(patchy::ui::MeasurementUnit::Millimeters);
  canvas.set_tool(patchy::ui::CanvasTool::Move);
  const auto mm_target = canvas.widget_position_for_document_point(QPoint(42, 100));
  send_mouse(canvas, QEvent::MouseButtonPress, pixel_target, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, mm_target, Qt::NoButton, Qt::LeftButton);
  readout = canvas.transform_drag_readout();
  send_mouse(canvas, QEvent::MouseButtonRelease, mm_target, Qt::LeftButton, Qt::NoButton);
  CHECK(readout.has_value());
  const auto moved_pixels = document.guides().front().position_32 / 32.0;
  CHECK(std::abs(moved_pixels - 100.0) <= 1.0 / 32.0);
  CHECK(readout->canvas_lines[0] ==
        QStringLiteral("Y: ") +
            patchy::ui::format_measurement(moved_pixels * 25.4 / 100.0, patchy::ui::MeasurementUnit::Millimeters, 1));
  CHECK(readout->canvas_lines[0].endsWith(QStringLiteral(" mm")));

  // Inches: a vertical guide from the left ruler reads its X position.
  canvas.set_ruler_unit(patchy::ui::MeasurementUnit::Inches);
  const QPoint left_ruler(12, canvas.widget_position_for_document_point(QPoint(0, 150)).y());
  const auto inch_target = canvas.widget_position_for_document_point(QPoint(150, 150));
  send_mouse(canvas, QEvent::MouseButtonPress, left_ruler, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, inch_target, Qt::NoButton, Qt::LeftButton);
  readout = canvas.transform_drag_readout();
  CHECK(readout.has_value());
  CHECK(readout->canvas_lines[0].startsWith(QStringLiteral("X: ")));
  CHECK(readout->canvas_lines[0].endsWith(QStringLiteral(" in")));

  // The Show Transformation Values preference governs guide drags too.
  canvas.set_show_transform_drag_values(false);
  CHECK(!canvas.transform_drag_readout().has_value());
  canvas.set_show_transform_drag_values(true);
  send_mouse(canvas, QEvent::MouseButtonRelease, inch_target, Qt::LeftButton, Qt::NoButton);
  CHECK(document.guides().size() == 2);
  CHECK(document.guides().back().orientation == patchy::GuideOrientation::Vertical);
  CHECK(!canvas.transform_drag_readout().has_value());
}

// GitHub issue 231: a guide is a line across the whole view, pasteboard included, as in
// Photoshop. Guides used to stop at the canvas edges, so one placed off the canvas showed
// as a short stub beside it, or not at all.
bool guide_drawn_near(const QImage& frame, QPoint point, bool vertical, QColor guide) {
  guide.setAlpha(255);
  for (int offset = -1; offset <= 1; ++offset) {
    const auto probe = point + (vertical ? QPoint(offset, 0) : QPoint(0, offset));
    if (frame.rect().contains(probe) && color_close(frame.pixelColor(probe), guide, 40)) {
      return true;
    }
  }
  return false;
}

void ui_guides_draw_across_the_pasteboard() {
  patchy::Document document(40, 30, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_pixels(40, 30, patchy::PixelFormat::rgb8(), Qt::white));
  // One guide inside the canvas per axis, and the rest on the pasteboard: before it,
  // and past its far edge.
  document.guides() = {
      patchy::DocumentGuide{patchy::GuideOrientation::Vertical, 10 * 32},
      patchy::DocumentGuide{patchy::GuideOrientation::Vertical, -20 * 32},
      patchy::DocumentGuide{patchy::GuideOrientation::Vertical, 60 * 32},
      patchy::DocumentGuide{patchy::GuideOrientation::Horizontal, 12 * 32},
      patchy::DocumentGuide{patchy::GuideOrientation::Horizontal, -15 * 32},
      patchy::DocumentGuide{patchy::GuideOrientation::Horizontal, 45 * 32},
  };

  patchy::ui::CanvasWidget canvas;
  canvas.resize(360, 260);
  canvas.set_document(&document);
  canvas.set_rulers_visible(true);
  canvas.set_guides_visible(true);
  canvas.show();
  QApplication::processEvents();
  // Centered, with pasteboard on every side of the canvas.
  canvas.set_zoom_centered(2.0);
  QApplication::processEvents();
  const auto frame = canvas.grab().toImage();
  save_widget_artifact("issue231_guides_across_pasteboard", canvas);

  const auto top_left = canvas.widget_position_for_document_point(QPoint(0, 0));
  const auto bottom_right = canvas.widget_position_for_document_point(QPoint(40, 30));
  // Pasteboard rows above and below the canvas, columns left and right of it.
  const int above = top_left.y() - 20;
  const int below = bottom_right.y() + 20;
  const int left = top_left.x() - 30;
  const int right = bottom_right.x() + 50;
  CHECK(above > 24 && below < canvas.height() && left > 32 && right < canvas.width());
  const auto guide = canvas.guide_color();
  for (const int x_pixels : {10, -20, 60}) {
    const int x = canvas.widget_position_for_document_point(QPoint(x_pixels, 0)).x();
    CHECK(guide_drawn_near(frame, QPoint(x, above), true, guide));
    CHECK(guide_drawn_near(frame, QPoint(x, below), true, guide));
  }
  for (const int y_pixels : {12, -15, 45}) {
    const int y = canvas.widget_position_for_document_point(QPoint(0, y_pixels)).y();
    CHECK(guide_drawn_near(frame, QPoint(left, y), false, guide));
    CHECK(guide_drawn_near(frame, QPoint(right, y), false, guide));
  }
  // Hidden guides draw nowhere.
  canvas.set_guides_visible(false);
  const auto hidden = canvas.grab().toImage();
  const int x = canvas.widget_position_for_document_point(QPoint(-20, 0)).x();
  CHECK(!guide_drawn_near(hidden, QPoint(x, above), true, guide));
}

// A guide dropped on the pasteboard stays there (GitHub issue 231); a drop on a ruler or
// outside the view still removes it, and a guide on the pasteboard can be grabbed again.
void ui_guide_dragged_onto_pasteboard_stays() {
  patchy::Document document(40, 30, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_pixels(40, 30, patchy::PixelFormat::rgb8(), Qt::white));
  document.guides().push_back(patchy::DocumentGuide{patchy::GuideOrientation::Vertical, 10 * 32});

  patchy::ui::CanvasWidget canvas;
  canvas.resize(360, 260);
  canvas.set_document(&document);
  canvas.set_rulers_visible(true);
  canvas.set_guides_visible(true);
  canvas.set_snap_enabled(false);
  canvas.set_tool(patchy::ui::CanvasTool::Move);
  canvas.show();
  QApplication::processEvents();
  canvas.set_zoom_centered(2.0);
  QApplication::processEvents();

  // Inside the canvas, dragged 15 px past its left edge.
  const auto on_guide = canvas.widget_position_for_document_point(QPoint(10, 15));
  const auto on_pasteboard = canvas.widget_position_for_document_point(QPoint(-15, 15));
  send_mouse(canvas, QEvent::MouseButtonPress, on_guide, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, on_pasteboard, Qt::NoButton, Qt::LeftButton);
  CHECK(canvas.transform_drag_readout().has_value());
  send_mouse(canvas, QEvent::MouseButtonRelease, on_pasteboard, Qt::LeftButton, Qt::NoButton);
  CHECK(document.guides().size() == 1);
  if (document.guides().size() == 1) {
    std::cout << "guide dropped on the pasteboard at position_32: " << document.guides().front().position_32
              << std::endl;
    CHECK(document.guides().front().position_32 == -15 * 32);
  }
  save_widget_artifact("issue231_guide_dropped_on_pasteboard", canvas);

  // Grabbed again on the pasteboard above the canvas and moved further out.
  const auto above_canvas = canvas.widget_position_for_document_point(QPoint(-15, -10));
  const auto further_left = canvas.widget_position_for_document_point(QPoint(-25, -10));
  send_mouse(canvas, QEvent::MouseButtonPress, above_canvas, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, further_left, Qt::NoButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, further_left, Qt::LeftButton, Qt::NoButton);
  CHECK(document.guides().size() == 1);
  CHECK(!document.guides().empty() && document.guides().front().position_32 == -25 * 32);

  // Onto the left ruler: removed, with no readout while it hovers there.
  const auto on_moved = canvas.widget_position_for_document_point(QPoint(-25, 5));
  const QPoint left_ruler(12, on_moved.y());
  send_mouse(canvas, QEvent::MouseButtonPress, on_moved, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, left_ruler, Qt::NoButton, Qt::LeftButton);
  CHECK(!canvas.transform_drag_readout().has_value());
  send_mouse(canvas, QEvent::MouseButtonRelease, left_ruler, Qt::LeftButton, Qt::NoButton);
  CHECK(document.guides().empty());

  // A new guide from the top ruler kept below the canvas, past its far edge.
  const auto below_canvas = canvas.widget_position_for_document_point(QPoint(20, 50));
  const QPoint top_ruler(below_canvas.x(), 12);
  CHECK(below_canvas.y() < canvas.height());
  send_mouse(canvas, QEvent::MouseButtonPress, top_ruler, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, below_canvas, Qt::NoButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, below_canvas, Qt::LeftButton, Qt::NoButton);
  CHECK(document.guides().size() == 1);
  CHECK(!document.guides().empty() && document.guides().front().orientation == patchy::GuideOrientation::Horizontal &&
        document.guides().front().position_32 == 50 * 32);

  // Released outside the view: no guide.
  const QPoint outside(below_canvas.x(), canvas.height() + 40);
  send_mouse(canvas, QEvent::MouseButtonPress, top_ruler, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, outside, Qt::NoButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, outside, Qt::LeftButton, Qt::NoButton);
  CHECK(document.guides().size() == 1);

  // New Guide places a typed position off the canvas too.
  canvas.add_guide(patchy::GuideOrientation::Vertical, -7 * 32);
  CHECK(document.guides().size() == 2);
  CHECK(document.guides().size() == 2 && document.guides().back().position_32 == -7 * 32);
}

// The reporter's file (ag-psd's test/read/psb-test/src.psb, GitHub issue 231), a 549 x 864
// PSB whose guides sit at x -154.2 and 1617.0 and y -1637.3 and 864.3, all on the
// pasteboard. At 12% every one of them shows across the view.
void ui_issue231_sample_guides_outside_document_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("ag-psd-psb-test-src.psb");
  patchy::test::skip_unless_exists(path);
  auto document = patchy::psd::DocumentIo::read_file(path);
  CHECK(document.guides().size() == 5);

  patchy::ui::CanvasWidget canvas;
  canvas.resize(600, 600);
  canvas.set_document(&document);
  canvas.set_rulers_visible(true);
  canvas.set_guides_visible(true);
  canvas.show();
  QApplication::processEvents();
  canvas.set_zoom_centered(0.12);
  CHECK(process_events_until([&canvas] { return canvas.render_settled(); }, 10000));
  QApplication::processEvents();
  const auto frame = canvas.grab().toImage();
  save_widget_artifact("issue231_sample_pasteboard_guides", canvas);

  const auto bottom_right = canvas.widget_position_for_document_point(QPoint(549, 864));
  const int below = bottom_right.y() + 40;
  const int right = bottom_right.x() + 40;
  CHECK(below < canvas.height() && right < canvas.width());
  const auto guide = canvas.guide_color();
  for (const auto& sample : document.guides()) {
    const auto pixels = static_cast<int>(std::lround(sample.position_32 / 32.0));
    if (sample.orientation == patchy::GuideOrientation::Vertical) {
      const int x = canvas.widget_position_for_document_point(QPoint(pixels, 0)).x();
      CHECK(x > 32 && x < canvas.width());
      CHECK(guide_drawn_near(frame, QPoint(x, below), true, guide));
    } else {
      const int y = canvas.widget_position_for_document_point(QPoint(0, pixels)).y();
      CHECK(y > 24 && y < canvas.height());
      CHECK(guide_drawn_near(frame, QPoint(right, y), false, guide));
    }
  }
}

void ui_deep_zoom_pixel_grid_matches_rendered_pixels() {
  patchy::Document document(24, 12, patchy::PixelFormat::rgb8());
  auto pixels = solid_pixels(24, 12, patchy::PixelFormat::rgb8(), Qt::white);
  fill_pixel_rect(pixels, QRect(3, 3, 1, 1), QColor(230, 20, 45));
  fill_pixel_rect(pixels, QRect(13, 3, 1, 1), QColor(230, 20, 45));
  document.add_pixel_layer("Background", std::move(pixels));
  document.grid_settings().horizontal_cycle_32 = 32;
  document.grid_settings().vertical_cycle_32 = 32;

  patchy::ui::CanvasWidget canvas;
  canvas.resize(900, 360);
  canvas.set_document(&document);
  canvas.set_zoom(32.0);
  canvas.zoom_at_widget_point(QPointF(231.4, 181.7), 1.85);
  CHECK(canvas.zoom() > 32.0);
  CHECK(canvas.zoom() < 64.0);
  canvas.set_grid_visible(true);
  canvas.set_grid_subdivisions(1);
  canvas.set_grid_style(0);
  canvas.set_grid_color(QColor(78, 154, 255, 180));
  canvas.show();
  QApplication::processEvents();

  const auto cell_center = [&canvas](QPoint document_point) {
    const auto a = canvas.widget_position_for_document_point(document_point);
    const auto b = canvas.widget_position_for_document_point(document_point + QPoint(1, 1));
    return QPoint((a.x() + b.x()) / 2, (a.y() + b.y()) / 2);
  };
  const auto image = canvas.grab().toImage();
  CHECK(color_close(image.pixelColor(cell_center(QPoint(3, 3))), QColor(230, 20, 45), 24));
  CHECK(color_close(image.pixelColor(cell_center(QPoint(13, 3))), QColor(230, 20, 45), 24));
  CHECK(color_close(image.pixelColor(cell_center(QPoint(4, 3))), Qt::white, 18));

  auto strongest_grid_column = [&image](int expected_x, int sample_y) {
    int strongest_x = expected_x;
    int strongest_delta = std::numeric_limits<int>::min();
    for (int x = expected_x - 1; x <= expected_x + 1; ++x) {
      if (!image.rect().contains(QPoint(x, sample_y))) {
        continue;
      }
      const auto color = image.pixelColor(x, sample_y);
      const auto delta = color.blue() - ((color.red() + color.green()) / 2);
      if (delta > strongest_delta) {
        strongest_delta = delta;
        strongest_x = x;
      }
    }
    return std::pair<int, int>{strongest_x, strongest_delta};
  };

  const auto assert_red_cell_matches_grid = [&](QPoint document_point) {
    const auto top_left = canvas.widget_position_for_document_point(document_point);
    const auto bottom_right = canvas.widget_position_for_document_point(document_point + QPoint(1, 1));
    const QRect expected_cell(top_left, QSize(bottom_right.x() - top_left.x(), bottom_right.y() - top_left.y()));
    CHECK(expected_cell.width() >= 58);
    CHECK(expected_cell.height() >= 58);

    const auto sample_y = cell_center(QPoint(document_point.x(), 5)).y();
    const auto [left_grid_x, left_grid_delta] = strongest_grid_column(expected_cell.left(), sample_y);
    const auto [right_grid_x, right_grid_delta] = strongest_grid_column(bottom_right.x(), sample_y);
    CHECK(std::abs(left_grid_x - expected_cell.left()) <= 1);
    CHECK(std::abs(right_grid_x - bottom_right.x()) <= 1);
    CHECK(left_grid_delta > 20);
    CHECK(right_grid_delta > 20);

    int min_x = image.width();
    int min_y = image.height();
    int max_x = -1;
    int max_y = -1;
    const auto search_rect = expected_cell.adjusted(-3, -3, 3, 3).intersected(image.rect());
    for (int y = search_rect.top(); y <= search_rect.bottom(); ++y) {
      for (int x = search_rect.left(); x <= search_rect.right(); ++x) {
        const auto color = image.pixelColor(x, y);
        if (color.red() > 170 && color.green() < 80 && color.blue() < 100) {
          min_x = std::min(min_x, x);
          min_y = std::min(min_y, y);
          max_x = std::max(max_x, x);
          max_y = std::max(max_y, y);
        }
      }
    }
    CHECK(min_x >= expected_cell.left());
    CHECK(min_x <= expected_cell.left() + 2);
    CHECK(min_y >= expected_cell.top());
    CHECK(min_y <= expected_cell.top() + 2);
    CHECK(max_x <= expected_cell.right());
    CHECK(max_x >= expected_cell.right() - 2);
    CHECK(max_y <= expected_cell.bottom());
    CHECK(max_y >= expected_cell.bottom() - 2);
  };
  assert_red_cell_matches_grid(QPoint(3, 3));
  assert_red_cell_matches_grid(QPoint(13, 3));
}

void ui_deep_zoom_one_pixel_brush_marks_match_pixel_grid() {
  patchy::Document document(32, 16, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_pixels(32, 16, patchy::PixelFormat::rgb8(), Qt::white));
  auto& paint_layer = document.add_pixel_layer("Paint",
                                               solid_pixels(32, 16, patchy::PixelFormat::rgba8(), Qt::transparent));
  document.set_active_layer(paint_layer.id());
  document.grid_settings().horizontal_cycle_32 = 16 * 32;
  document.grid_settings().vertical_cycle_32 = 16 * 32;

  patchy::ui::CanvasWidget canvas;
  canvas.resize(900, 420);
  canvas.set_document(&document);
  canvas.set_zoom(32.0);
  canvas.zoom_at_widget_point(QPointF(286.35, 190.65), 1.25);
  canvas.set_grid_visible(true);
  canvas.set_grid_subdivisions(16);
  canvas.set_grid_style(0);
  canvas.set_grid_color(QColor(78, 154, 255, 180));
  canvas.set_tool(patchy::ui::CanvasTool::Brush);
  canvas.set_primary_color(Qt::black);
  canvas.set_brush_size(1);
  canvas.set_brush_opacity(100);
  canvas.set_brush_softness(75);
  canvas.show();
  QApplication::processEvents();

  const auto cell_center = [&canvas](QPoint document_point) {
    const auto a = canvas.widget_position_for_document_point(document_point);
    const auto b = canvas.widget_position_for_document_point(document_point + QPoint(1, 1));
    return QPoint((a.x() + b.x()) / 2, (a.y() + b.y()) / 2);
  };

  const std::array<QPoint, 3> marks{QPoint(3, 4), QPoint(9, 4), QPoint(17, 4)};
  for (const auto mark : marks) {
    const auto point = cell_center(mark);
    send_mouse(canvas, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
    send_mouse(canvas, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
  }
  QApplication::processEvents();

  for (const auto mark : marks) {
    CHECK(paint_layer.pixels().pixel(mark.x(), mark.y())[3] == 255);
    CHECK(paint_layer.pixels().pixel(mark.x() + 1, mark.y())[3] == 0);
  }

  const auto image = canvas.grab().toImage();
  const auto grid_strength = [&image](int x, int y) {
    if (!image.rect().contains(QPoint(x, y))) {
      return std::numeric_limits<int>::min();
    }
    const auto color = image.pixelColor(x, y);
    return color.blue() - ((color.red() + color.green()) / 2);
  };
  const auto strongest_grid_column = [&](int expected_x, int sample_y) {
    int strongest_x = expected_x;
    int strongest_delta = std::numeric_limits<int>::min();
    for (int x = expected_x - 1; x <= expected_x + 1; ++x) {
      const auto delta = grid_strength(x, sample_y);
      if (delta > strongest_delta) {
        strongest_delta = delta;
        strongest_x = x;
      }
    }
    return std::pair<int, int>{strongest_x, strongest_delta};
  };

  const auto sample_y = cell_center(QPoint(6, 8)).y();
  for (const auto mark : marks) {
    const auto top_left = canvas.widget_position_for_document_point(mark);
    const auto bottom_right = canvas.widget_position_for_document_point(mark + QPoint(1, 1));
    const QRect expected_cell(top_left, QSize(bottom_right.x() - top_left.x(), bottom_right.y() - top_left.y()));
    CHECK(expected_cell.width() >= 38);
    CHECK(expected_cell.height() >= 38);

    const auto [left_grid_x, left_grid_delta] = strongest_grid_column(expected_cell.left(), sample_y);
    const auto [right_grid_x, right_grid_delta] = strongest_grid_column(bottom_right.x(), sample_y);
    CHECK(std::abs(left_grid_x - expected_cell.left()) <= 1);
    CHECK(std::abs(right_grid_x - bottom_right.x()) <= 1);
    CHECK(left_grid_delta > 20);
    CHECK(right_grid_delta > 20);

    int min_x = image.width();
    int min_y = image.height();
    int max_x = -1;
    int max_y = -1;
    const auto search_rect = expected_cell.adjusted(-2, -2, 2, 2).intersected(image.rect());
    for (int y = search_rect.top(); y <= search_rect.bottom(); ++y) {
      for (int x = search_rect.left(); x <= search_rect.right(); ++x) {
        const auto color = image.pixelColor(x, y);
        if (color.red() < 40 && color.green() < 40 && color.blue() < 40) {
          min_x = std::min(min_x, x);
          min_y = std::min(min_y, y);
          max_x = std::max(max_x, x);
          max_y = std::max(max_y, y);
        }
      }
    }
    CHECK(min_x >= expected_cell.left());
    CHECK(min_x <= expected_cell.left() + 2);
    CHECK(min_y >= expected_cell.top());
    CHECK(min_y <= expected_cell.top() + 2);
    CHECK(max_x <= expected_cell.right());
    CHECK(max_x >= expected_cell.right() - 2);
    CHECK(max_y <= expected_cell.bottom());
    CHECK(max_y >= expected_cell.bottom() - 2);
  }
}

void ui_deep_zoom_subpixel_subdivisions_do_not_draw_inside_pixels() {
  patchy::Document document(24, 12, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_pixels(24, 12, patchy::PixelFormat::rgb8(), Qt::white));
  document.grid_settings().horizontal_cycle_32 = 4 * 32;
  document.grid_settings().vertical_cycle_32 = 4 * 32;

  patchy::ui::CanvasWidget canvas;
  canvas.resize(700, 360);
  canvas.set_document(&document);
  canvas.set_zoom(40.0);
  canvas.set_grid_visible(true);
  canvas.set_grid_subdivisions(16);
  canvas.set_grid_style(0);
  canvas.set_grid_color(QColor(78, 154, 255, 220));
  canvas.show();
  QApplication::processEvents();

  const auto cell_center = [&canvas](QPoint document_point) {
    const auto a = canvas.widget_position_for_document_point(document_point);
    const auto b = canvas.widget_position_for_document_point(document_point + QPoint(1, 1));
    return QPoint((a.x() + b.x()) / 2, (a.y() + b.y()) / 2);
  };
  const auto image = canvas.grab().toImage();
  const auto grid_strength = [&image](int x, int y) {
    if (!image.rect().contains(QPoint(x, y))) {
      return std::numeric_limits<int>::min();
    }
    const auto color = image.pixelColor(x, y);
    return color.blue() - ((color.red() + color.green()) / 2);
  };

  const auto sample_y = cell_center(QPoint(6, 5)).y();
  for (int document_x = 4; document_x <= 8; ++document_x) {
    const auto left = canvas.widget_position_for_document_point(QPoint(document_x, 0)).x();
    const auto right = canvas.widget_position_for_document_point(QPoint(document_x + 1, 0)).x();
    CHECK(grid_strength(left, sample_y) > 20);
    CHECK(grid_strength(right, sample_y) > 20);
    for (int x = left + 3; x <= right - 3; ++x) {
      CHECK(grid_strength(x, sample_y) < 12);
    }
  }
}

void ui_deep_zoom_fractional_subdivision_spacing_stays_on_pixel_edges() {
  patchy::Document document(32, 16, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_pixels(32, 16, patchy::PixelFormat::rgb8(), Qt::white));
  document.grid_settings().horizontal_cycle_32 = 18 * 32;
  document.grid_settings().vertical_cycle_32 = 18 * 32;

  patchy::ui::CanvasWidget canvas;
  canvas.resize(900, 420);
  canvas.set_document(&document);
  canvas.set_zoom(40.0);
  canvas.set_grid_visible(true);
  canvas.set_grid_subdivisions(16);
  canvas.set_grid_style(0);
  canvas.set_grid_color(QColor(78, 154, 255, 220));
  canvas.show();
  QApplication::processEvents();

  const auto image = canvas.grab().toImage();
  const auto grid_strength = [&image](int x, int y) {
    if (!image.rect().contains(QPoint(x, y))) {
      return std::numeric_limits<int>::min();
    }
    const auto color = image.pixelColor(x, y);
    return color.blue() - ((color.red() + color.green()) / 2);
  };
  const auto sample_y = (canvas.widget_position_for_document_point(QPoint(0, 4)).y() +
                         canvas.widget_position_for_document_point(QPoint(0, 5)).y()) /
                        2;

  for (int document_x = 1; document_x <= 20; ++document_x) {
    const auto left = canvas.widget_position_for_document_point(QPoint(document_x, 0)).x();
    const auto right = canvas.widget_position_for_document_point(QPoint(document_x + 1, 0)).x();
    for (int x = left + 3; x <= right - 3; ++x) {
      CHECK(grid_strength(x, sample_y) < 12);
    }
  }
}

void ui_deep_zoom_grid_subdivision_counts_change_spacing() {
  patchy::Document document(32, 16, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_pixels(32, 16, patchy::PixelFormat::rgb8(), Qt::white));
  document.grid_settings().horizontal_cycle_32 = 16 * 32;
  document.grid_settings().vertical_cycle_32 = 16 * 32;

  patchy::ui::CanvasWidget canvas;
  canvas.resize(900, 420);
  canvas.set_document(&document);
  canvas.set_zoom(40.0);
  canvas.set_grid_visible(true);
  canvas.set_grid_style(0);
  canvas.set_grid_color(QColor(78, 154, 255, 220));
  canvas.show();
  QApplication::processEvents();

  const auto grid_strength = [](const QImage& image, int x, int y) {
    if (!image.rect().contains(QPoint(x, y))) {
      return std::numeric_limits<int>::min();
    }
    const auto color = image.pixelColor(x, y);
    return color.blue() - ((color.red() + color.green()) / 2);
  };
  const auto vertical_line_score = [&](const QImage& image, int document_x) {
    const auto x = canvas.widget_position_for_document_point(QPoint(document_x, 0)).x();
    const auto top = canvas.widget_position_for_document_point(QPoint(0, 2)).y();
    const auto bottom = canvas.widget_position_for_document_point(QPoint(0, 10)).y();
    int score = 0;
    for (int y = top; y <= bottom; y += 2) {
      const auto center = grid_strength(image, x, y);
      const auto side = std::max(grid_strength(image, x - 3, y), grid_strength(image, x + 3, y));
      if (center > 12 && center > side + 8) {
        ++score;
      }
    }
    return score;
  };
  const auto grab_for_subdivisions = [&](int subdivisions) {
    canvas.set_grid_subdivisions(subdivisions);
    QApplication::processEvents();
    return canvas.grab().toImage();
  };

  const auto one = grab_for_subdivisions(1);
  CHECK(vertical_line_score(one, 4) <= 2);
  CHECK(vertical_line_score(one, 8) <= 2);
  CHECK(vertical_line_score(one, 16) > 8);

  const auto two = grab_for_subdivisions(2);
  CHECK(vertical_line_score(two, 4) <= 2);
  CHECK(vertical_line_score(two, 8) > 4);

  const auto four = grab_for_subdivisions(4);
  CHECK(vertical_line_score(four, 4) > 4);

  const auto sixteen = grab_for_subdivisions(16);
  CHECK(vertical_line_score(sixteen, 1) > 8);
}

void ui_snap_marquee_uses_screen_pixel_tolerance_and_target_toggles() {
  patchy::Document document(96, 64, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_pixels(96, 64, patchy::PixelFormat::rgb8(), Qt::white));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(420, 300);
  canvas.set_document(&document);
  canvas.set_zoom(1.0);
  canvas.set_tool(patchy::ui::CanvasTool::Marquee);
  canvas.set_guides_visible(false);
  canvas.set_snap_enabled(true);
  canvas.set_snap_to_guides(true);
  canvas.set_snap_to_grid(false);
  canvas.set_snap_to_document(false);
  canvas.set_snap_to_layers(false);
  canvas.set_snap_to_selection(false);
  canvas.add_guide(patchy::GuideOrientation::Vertical, 20 * 32);
  canvas.add_guide(patchy::GuideOrientation::Horizontal, 30 * 32);
  canvas.show();
  QApplication::processEvents();

  drag(canvas, canvas.widget_position_for_document_point(QPoint(13, 10)),
       canvas.widget_position_for_document_point(QPoint(36, 28)));
  auto selection = canvas.selected_document_rect();
  CHECK(selection.has_value());
  CHECK(selection->left() == 20);

  canvas.clear_selection();
  drag(canvas, canvas.widget_position_for_document_point(QPoint(5, 10)),
       canvas.widget_position_for_document_point(QPoint(18, 28)));
  selection = canvas.selected_document_rect();
  CHECK(selection.has_value());
  CHECK(selection->left() == 5);
  CHECK(selection->right() + 1 == 20);

  canvas.clear_selection();
  drag(canvas, canvas.widget_position_for_document_point(QPoint(20, 5)),
       canvas.widget_position_for_document_point(QPoint(36, 18)));
  selection = canvas.selected_document_rect();
  CHECK(selection.has_value());
  CHECK(selection->left() == 20);
  CHECK(document.guides().front().position_32 == 20 * 32);

  canvas.clear_selection();
  const auto space_start = canvas.widget_position_for_document_point(QPoint(5, 45));
  const auto space_initial = canvas.widget_position_for_document_point(QPoint(10, 55));
  const auto space_moved = canvas.widget_position_for_document_point(QPoint(19, 55));
  const auto space_released = canvas.widget_position_for_document_point(QPoint(29, 55));
  send_mouse(canvas, QEvent::MouseButtonPress, space_start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, space_initial, Qt::NoButton, Qt::LeftButton);
  send_key_press(canvas, Qt::Key_Space);
  send_mouse(canvas, QEvent::MouseMove, space_moved, Qt::NoButton, Qt::LeftButton);
  selection = canvas.selected_document_rect();
  CHECK(selection.has_value());
  CHECK(selection->left() == 14);
  CHECK(selection->right() + 1 == 20);
  send_mouse(canvas, QEvent::MouseMove, canvas.widget_position_for_document_point(QPoint(21, 55)), Qt::NoButton,
             Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, canvas.widget_position_for_document_point(QPoint(23, 55)), Qt::NoButton,
             Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, canvas.widget_position_for_document_point(QPoint(25, 55)), Qt::NoButton,
             Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, canvas.widget_position_for_document_point(QPoint(27, 55)), Qt::NoButton,
             Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, space_released, Qt::NoButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, space_released, Qt::LeftButton, Qt::NoButton);
  send_key_release(canvas, Qt::Key_Space);
  selection = canvas.selected_document_rect();
  CHECK(selection.has_value());
  CHECK(selection->width() == 6);
  CHECK(selection->left() > 14);
  CHECK(selection->right() + 1 > 20);

  canvas.clear_selection();
  canvas.set_zoom(4.0);
  drag(canvas, canvas.widget_position_for_document_point(QPoint(17, 10)),
       canvas.widget_position_for_document_point(QPoint(36, 28)));
  selection = canvas.selected_document_rect();
  CHECK(selection.has_value());
  CHECK(selection->left() == 17);

  canvas.clear_selection();
  drag(canvas, canvas.widget_position_for_document_point(QPoint(18, 10)),
       canvas.widget_position_for_document_point(QPoint(36, 28)));
  selection = canvas.selected_document_rect();
  CHECK(selection.has_value());
  CHECK(selection->left() == 20);

  canvas.clear_selection();
  canvas.set_snap_to_guides(false);
  drag(canvas, canvas.widget_position_for_document_point(QPoint(19, 10)),
       canvas.widget_position_for_document_point(QPoint(36, 28)));
  selection = canvas.selected_document_rect();
  CHECK(selection.has_value());
  CHECK(selection->left() == 19);

  canvas.clear_selection();
  canvas.set_snap_to_guides(true);
  canvas.set_zoom(1.0);
  drag(canvas, canvas.widget_position_for_document_point(QPoint(10, 23)),
       canvas.widget_position_for_document_point(QPoint(36, 50)));
  selection = canvas.selected_document_rect();
  CHECK(selection.has_value());
  CHECK(selection->top() == 30);
  save_widget_artifact("ui_snapped_marquee_guides", canvas);
}

void ui_shift_constrains_shape_drag_to_square() {
  patchy::Document document(220, 180, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_pixels(220, 180, patchy::PixelFormat::rgb8(), Qt::white));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(480, 380);
  canvas.set_document(&document);
  canvas.set_zoom(2.0);
  canvas.set_primary_color(Qt::black);
  canvas.set_fill_shapes(true);
  canvas.set_brush_opacity(100);
  canvas.set_brush_softness(0);
  canvas.show();
  QApplication::processEvents();

  // A wide, short drag (160x60) with Shift collapses to a 1:1 square sized to the smaller axis.
  canvas.set_tool(patchy::ui::CanvasTool::Rectangle);
  drag(canvas, canvas.widget_position_for_document_point(QPoint(30, 30)),
       canvas.widget_position_for_document_point(QPoint(190, 90)), Qt::ShiftModifier);
  QApplication::processEvents();
  const auto square = dark_document_bounds(canvas, QRect(0, 0, 220, 180));
  CHECK(square.has_value());
  CHECK(std::abs(square->width() - square->height()) <= 3);
  CHECK(square->width() < 90);  // ~60 (smaller axis), not the 160px-wide drag

  // Without Shift the same drag stays a wide rectangle.
  patchy::Document free_document(220, 180, patchy::PixelFormat::rgb8());
  free_document.add_pixel_layer("Background", solid_pixels(220, 180, patchy::PixelFormat::rgb8(), Qt::white));
  patchy::ui::CanvasWidget free_canvas;
  free_canvas.resize(480, 380);
  free_canvas.set_document(&free_document);
  free_canvas.set_zoom(2.0);
  free_canvas.set_primary_color(Qt::black);
  free_canvas.set_fill_shapes(true);
  free_canvas.set_brush_opacity(100);
  free_canvas.set_brush_softness(0);
  free_canvas.set_tool(patchy::ui::CanvasTool::Rectangle);
  free_canvas.show();
  QApplication::processEvents();
  drag(free_canvas, free_canvas.widget_position_for_document_point(QPoint(30, 30)),
       free_canvas.widget_position_for_document_point(QPoint(190, 90)));
  QApplication::processEvents();
  const auto wide = dark_document_bounds(free_canvas, QRect(0, 0, 220, 180));
  CHECK(wide.has_value());
  CHECK(wide->width() > wide->height() + 40);  // free drag keeps the 160x60 aspect
  save_widget_artifact("ui_shift_square_shape", canvas);
}

void ui_shape_tool_alt_draws_from_center() {
  const auto make_document = [] {
    patchy::Document document(240, 200, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Background", solid_pixels(240, 200, patchy::PixelFormat::rgb8(), Qt::white));
    return document;
  };
  const auto make_canvas = [](patchy::Document& document) {
    auto canvas = std::make_unique<patchy::ui::CanvasWidget>();
    canvas->resize(520, 440);
    canvas->set_document(&document);
    canvas->set_zoom(2.0);
    canvas->set_primary_color(Qt::black);
    canvas->set_fill_shapes(true);
    canvas->set_brush_opacity(100);
    canvas->set_brush_softness(0);
    canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
    canvas->show();
    QApplication::processEvents();
    return canvas;
  };
  const auto document_rect = QRect(0, 0, 240, 200);

  // Alt+press starts the drag from center directly (the shape tools are exempt from the
  // Alt temporary-eyedropper, which would otherwise swallow the press and fight the cursor).
  {
    auto document = make_document();
    auto canvas = make_canvas(document);
    send_mouse(*canvas, QEvent::MouseButtonPress, canvas->widget_position_for_document_point(QPoint(100, 80)),
               Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
    send_mouse(*canvas, QEvent::MouseMove, canvas->widget_position_for_document_point(QPoint(140, 110)),
               Qt::NoButton, Qt::LeftButton, Qt::AltModifier);
    send_mouse(*canvas, QEvent::MouseButtonRelease, canvas->widget_position_for_document_point(QPoint(140, 110)),
               Qt::LeftButton, Qt::NoButton, Qt::AltModifier);
    QApplication::processEvents();
    // No color pick happened (the white background would have replaced the black primary)...
    CHECK(canvas->primary_color() == QColor(Qt::black));
    // ...and the shape grew symmetrically around the press point.
    const auto bounds = dark_document_bounds(*canvas, document_rect);
    CHECK(bounds.has_value());
    CHECK(std::abs(bounds->width() - 80) <= 2);
    CHECK(std::abs(bounds->height() - 60) <= 2);
    CHECK(std::abs(bounds->center().x() - 100) <= 2);
    CHECK(std::abs(bounds->center().y() - 80) <= 2);
  }

  // Alt engaged mid-drag re-anchors growth symmetrically around the press point too.
  {
    auto document = make_document();
    auto canvas = make_canvas(document);
    send_mouse(*canvas, QEvent::MouseButtonPress, canvas->widget_position_for_document_point(QPoint(100, 80)),
               Qt::LeftButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseMove, canvas->widget_position_for_document_point(QPoint(140, 110)),
               Qt::NoButton, Qt::LeftButton, Qt::AltModifier);
    send_mouse(*canvas, QEvent::MouseButtonRelease, canvas->widget_position_for_document_point(QPoint(140, 110)),
               Qt::LeftButton, Qt::NoButton, Qt::AltModifier);
    QApplication::processEvents();
    const auto bounds = dark_document_bounds(*canvas, document_rect);
    CHECK(bounds.has_value());
    // 40x30 drag extent doubles to 80x60 centered on the press point.
    CHECK(std::abs(bounds->width() - 80) <= 2);
    CHECK(std::abs(bounds->height() - 60) <= 2);
    CHECK(std::abs(bounds->center().x() - 100) <= 2);
    CHECK(std::abs(bounds->center().y() - 80) <= 2);
    save_widget_artifact("ui_shape_alt_from_center", *canvas);
  }

  // Dropping Alt mid-drag reverts to the corner-anchored rectangle.
  {
    auto document = make_document();
    auto canvas = make_canvas(document);
    send_mouse(*canvas, QEvent::MouseButtonPress, canvas->widget_position_for_document_point(QPoint(100, 80)),
               Qt::LeftButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseMove, canvas->widget_position_for_document_point(QPoint(140, 110)),
               Qt::NoButton, Qt::LeftButton, Qt::AltModifier);
    send_mouse(*canvas, QEvent::MouseButtonRelease, canvas->widget_position_for_document_point(QPoint(140, 110)),
               Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    const auto bounds = dark_document_bounds(*canvas, document_rect);
    CHECK(bounds.has_value());
    CHECK(bounds->topLeft() == QPoint(100, 80));
    CHECK(std::abs(bounds->width() - 41) <= 2);
    CHECK(std::abs(bounds->height() - 31) <= 2);
  }

  // Alt composes with Fixed Size: the exact W x H rect is centered on the press point.
  {
    auto document = make_document();
    auto canvas = make_canvas(document);
    canvas->set_shape_style(patchy::ui::CanvasWidget::MarqueeStyle::FixedSize);
    canvas->set_shape_fixed_size(60, 40);
    send_mouse(*canvas, QEvent::MouseButtonPress, canvas->widget_position_for_document_point(QPoint(100, 80)),
               Qt::LeftButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseMove, canvas->widget_position_for_document_point(QPoint(120, 95)),
               Qt::NoButton, Qt::LeftButton, Qt::AltModifier);
    send_mouse(*canvas, QEvent::MouseButtonRelease, canvas->widget_position_for_document_point(QPoint(120, 95)),
               Qt::LeftButton, Qt::NoButton, Qt::AltModifier);
    QApplication::processEvents();
    const auto bounds = dark_document_bounds(*canvas, document_rect);
    CHECK(bounds.has_value());
    CHECK(bounds->topLeft() == QPoint(70, 60));
    CHECK(bounds->width() == 60);
    CHECK(bounds->height() == 40);
  }

  // A stationary cursor picks up an Alt keypress too (key event, not a mouse move).
  {
    auto document = make_document();
    auto canvas = make_canvas(document);
    send_mouse(*canvas, QEvent::MouseButtonPress, canvas->widget_position_for_document_point(QPoint(100, 80)),
               Qt::LeftButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseMove, canvas->widget_position_for_document_point(QPoint(130, 100)),
               Qt::NoButton, Qt::LeftButton);
    send_key_press(*canvas, Qt::Key_Alt, Qt::AltModifier);
    send_mouse(*canvas, QEvent::MouseButtonRelease, canvas->widget_position_for_document_point(QPoint(130, 100)),
               Qt::LeftButton, Qt::NoButton, Qt::AltModifier);
    // Balance the synthetic Alt press so no modifier state leaks into later tests
    // (see the offscreen keyboardModifiers() note in docs/testing.md).
    send_key_release(*canvas, Qt::Key_Alt, Qt::NoModifier);
    QApplication::processEvents();
    const auto bounds = dark_document_bounds(*canvas, document_rect);
    CHECK(bounds.has_value());
    CHECK(std::abs(bounds->center().x() - 100) <= 2);
    CHECK(std::abs(bounds->center().y() - 80) <= 2);
    CHECK(std::abs(bounds->width() - 60) <= 2);
  }
}

void ui_drag_size_readout_shows_dimensions() {
  // The badge is drawn offset below-right of the drag corner; scan that widget-space
  // zone for the dark text outline (the zone is clear of the shape/selection itself).
  const auto readout_visible = [](patchy::ui::CanvasWidget& canvas, QPoint document_corner) {
    const auto image = canvas.grab().toImage();
    const auto anchor = canvas.widget_position_for_document_point(document_corner);
    for (int y = anchor.y() + 8; y <= anchor.y() + 48; ++y) {
      for (int x = anchor.x() + 10; x <= anchor.x() + 160; ++x) {
        if (!image.rect().contains(x, y)) {
          continue;
        }
        const auto color = image.pixelColor(x, y);
        if (color.red() < 100 && color.green() < 100 && color.blue() < 100) {
          return true;
        }
      }
    }
    return false;
  };

  patchy::Document document(240, 200, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_pixels(240, 200, patchy::PixelFormat::rgb8(), Qt::white));
  patchy::ui::CanvasWidget canvas;
  canvas.resize(520, 440);
  canvas.set_document(&document);
  canvas.set_zoom(2.0);
  canvas.set_primary_color(Qt::black);
  canvas.set_fill_shapes(false);
  canvas.set_brush_size(1);
  canvas.set_brush_opacity(100);
  canvas.set_brush_softness(0);
  canvas.show();
  QApplication::processEvents();

  // Rectangle draw tool: readout appears mid-drag and disappears after the commit.
  canvas.set_tool(patchy::ui::CanvasTool::Rectangle);
  send_mouse(canvas, QEvent::MouseButtonPress, canvas.widget_position_for_document_point(QPoint(30, 30)),
             Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, canvas.widget_position_for_document_point(QPoint(100, 80)),
             Qt::NoButton, Qt::LeftButton);
  CHECK(readout_visible(canvas, QPoint(100, 80)));
  save_widget_artifact("ui_drag_size_readout_shape", canvas);
  // The commit repaint must cover the readout zone: it sits outside the shape's dirty
  // margin, so a bounded repaint would leave the readout stuck on screen. grab() cannot
  // catch that (it re-renders the whole widget), so record the real paint regions.
  PaintRegionRecorder recorder(&canvas);
  canvas.installEventFilter(&recorder);
  recorder.reset();
  send_mouse(canvas, QEvent::MouseButtonRelease, canvas.widget_position_for_document_point(QPoint(100, 80)),
             Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  canvas.removeEventFilter(&recorder);
  const auto shape_anchor = canvas.widget_position_for_document_point(QPoint(100, 80));
  const auto readout_zone = QRect(shape_anchor + QPoint(10, 8), QSize(150, 40)).intersected(canvas.rect());
  // QRegion::contains(QRect) tests overlap, not coverage; subtract to assert full coverage.
  CHECK(QRegion(readout_zone).subtracted(recorder.region()).isEmpty());
  CHECK(!readout_visible(canvas, QPoint(100, 80)));

  // Rectangular marquee: same readout while dragging out a selection.
  canvas.set_tool(patchy::ui::CanvasTool::Marquee);
  send_mouse(canvas, QEvent::MouseButtonPress, canvas.widget_position_for_document_point(QPoint(120, 100)),
             Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, canvas.widget_position_for_document_point(QPoint(190, 150)),
             Qt::NoButton, Qt::LeftButton);
  CHECK(readout_visible(canvas, QPoint(190, 150)));
  save_widget_artifact("ui_drag_size_readout_marquee", canvas);
  send_mouse(canvas, QEvent::MouseButtonRelease, canvas.widget_position_for_document_point(QPoint(190, 150)),
             Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(!readout_visible(canvas, QPoint(190, 150)));
}

void ui_snap_applies_to_shape_text_and_move_tools() {
  patchy::Document document(96, 64, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_pixels(96, 64, patchy::PixelFormat::rgb8(), Qt::white));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(420, 300);
  canvas.set_document(&document);
  canvas.set_zoom(2.0);
  canvas.set_snap_enabled(true);
  canvas.set_guides_visible(false);
  canvas.set_snap_to_guides(true);
  canvas.set_snap_to_grid(false);
  canvas.set_snap_to_document(false);
  canvas.set_snap_to_layers(false);
  canvas.set_snap_to_selection(false);
  canvas.add_guide(patchy::GuideOrientation::Vertical, 20 * 32);
  canvas.set_tool(patchy::ui::CanvasTool::Rectangle);
  canvas.set_primary_color(Qt::black);
  canvas.set_brush_size(1);
  canvas.show();
  QApplication::processEvents();

  drag(canvas, canvas.widget_position_for_document_point(QPoint(19, 10)),
       canvas.widget_position_for_document_point(QPoint(44, 28)));
  const auto shape_bounds = dark_document_bounds(canvas, QRect(0, 0, 96, 64));
  CHECK(shape_bounds.has_value());
  CHECK(shape_bounds->left() == 20);

  QPoint requested_point;
  QRect requested_box;
  canvas.set_text_requested_callback([&](QPoint point, QRect box) {
    requested_point = point;
    requested_box = box;
  });
  canvas.set_tool(patchy::ui::CanvasTool::Text);
  drag(canvas, canvas.widget_position_for_document_point(QPoint(19, 36)),
       canvas.widget_position_for_document_point(QPoint(48, 56)));
  CHECK(requested_box.isValid());
  CHECK(requested_box.left() == 20);
  CHECK(requested_point.x() == 20);

  patchy::Document move_document(96, 64, patchy::PixelFormat::rgba8());
  auto move_pixels = solid_pixels(8, 8, patchy::PixelFormat::rgba8(), Qt::black);
  patchy::Layer move_layer(move_document.allocate_layer_id(), "Move", std::move(move_pixels));
  const auto move_id = move_layer.id();
  move_layer.set_bounds(patchy::Rect{10, 10, 8, 8});
  move_document.add_layer(std::move(move_layer));

  patchy::ui::CanvasWidget move_canvas;
  move_canvas.resize(420, 300);
  move_canvas.set_document(&move_document);
  move_canvas.set_zoom(2.0);
  move_canvas.set_tool(patchy::ui::CanvasTool::Move);
  move_canvas.set_show_transform_controls(false);
  move_canvas.set_auto_select_layer(false);
  move_canvas.set_selected_layer_ids({move_id});
  move_canvas.set_snap_enabled(true);
  move_canvas.set_snap_to_guides(true);
  move_canvas.set_snap_to_grid(false);
  move_canvas.set_snap_to_document(false);
  move_canvas.set_snap_to_layers(false);
  move_canvas.set_snap_to_selection(false);
  move_canvas.add_guide(patchy::GuideOrientation::Vertical, 25 * 32);
  move_canvas.show();
  QApplication::processEvents();
  drag(move_canvas, move_canvas.widget_position_for_document_point(QPoint(12, 12)),
       move_canvas.widget_position_for_document_point(QPoint(26, 12)));
  const auto* moved = move_document.find_layer(move_id);
  CHECK(moved != nullptr);
  CHECK(moved->bounds().x == 25);
  save_widget_artifact("ui_snap_shape_text_move", canvas);
}

void ui_canvas_aid_preferences_and_guide_dialogs_work() {
  SettingsValueRestorer restore_rulers(QStringLiteral("view/rulersVisible"));
  SettingsValueRestorer restore_grid(QStringLiteral("view/gridVisible"));
  SettingsValueRestorer restore_guides(QStringLiteral("view/guidesVisible"));
  SettingsValueRestorer restore_lock(QStringLiteral("view/guidesLocked"));
  SettingsValueRestorer restore_snap(QStringLiteral("view/snapEnabled"));
  SettingsValueRestorer restore_snap_guides(QStringLiteral("view/snapToGuides"));
  SettingsValueRestorer restore_snap_grid(QStringLiteral("view/snapToGrid"));
  SettingsValueRestorer restore_snap_document(QStringLiteral("view/snapToDocument"));
  SettingsValueRestorer restore_snap_layers(QStringLiteral("view/snapToLayers"));
  SettingsValueRestorer restore_snap_selection(QStringLiteral("view/snapToSelection"));
  SettingsValueRestorer restore_spacing(QStringLiteral("view/gridSpacing32"));
  SettingsValueRestorer restore_subdivisions(QStringLiteral("view/gridSubdivisions"));
  SettingsValueRestorer restore_style(QStringLiteral("view/gridStyle"));
  SettingsValueRestorer restore_grid_color(QStringLiteral("view/gridColor"));
  SettingsValueRestorer restore_guide_color(QStringLiteral("view/guideColor"));
  SettingsValueRestorer restore_guide_color_migration(QStringLiteral("view/guideColorDefaultMigrated"));
  SettingsValueRestorer restore_units(QStringLiteral("view/rulerUnits"));

  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  bool saw_preferences = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    auto* pages = preferences_page_list(*dialog);
    CHECK(pages != nullptr);
    // Windows appends a Plug-ins page for the legacy 8BF host (docs/plugins.md).
#ifdef Q_OS_WIN
    CHECK(pages->count() == 8);
    CHECK(pages->item(7)->text() == QStringLiteral("Plug-ins"));
#else
    CHECK(pages->count() == 7);
#endif
    CHECK(pages->item(0)->text() == QStringLiteral("Application"));
    CHECK(pages->item(1)->text() == QStringLiteral("Tools"));
    CHECK(pages->item(2)->text() == QStringLiteral("Metadata and Watermarks"));
    CHECK(pages->item(3)->text() == QStringLiteral("Pen"));
    CHECK(pages->item(4)->text() == QStringLiteral("Units & Grids"));
    CHECK(pages->item(5)->text() == QStringLiteral("Snapping"));
    CHECK(pages->item(6)->text() == QStringLiteral("Hotkeys"));
    // Tool and canvas-input behavior lives on the Tools tab, not on Application or Pen.
    auto* tools_group = dialog->findChild<QWidget*>(QStringLiteral("preferencesToolsGroup"));
    CHECK(tools_group != nullptr);
    if (tools_group != nullptr) {
      for (const auto* name : {"preferencesWheelZoomCheck", "preferencesTransformShiftAspectCheck",
                               "preferencesShowTransformValuesCheck",
                               "preferencesTransformSnapToPixelGridCheck"}) {
        CHECK(tools_group->findChild<QCheckBox*>(QString::fromLatin1(name)) != nullptr);
      }
    }
    auto* grid_color_button = dialog->findChild<QPushButton*>(QStringLiteral("preferencesGridColorButton"));
    CHECK(grid_color_button != nullptr);
    CHECK(grid_color_button->text().contains(QStringLiteral("#")));
    // The picker chooses opaque colors, so the alpha lives in an opacity spin beside each
    // button, prefilled from the current color.
    auto* grid_opacity = dialog->findChild<QSpinBox*>(QStringLiteral("preferencesGridOpacitySpin"));
    CHECK(grid_opacity != nullptr);
    CHECK(dialog->findChild<QSpinBox*>(QStringLiteral("preferencesGuideOpacitySpin")) != nullptr);
    CHECK(grid_opacity->value() == qRound(canvas->grid_color().alphaF() * 100.0));
    grid_opacity->setValue(60);
    auto* overlay_preview = dialog->findChild<QLabel*>(QStringLiteral("preferencesGridOverlayPreview"));
    CHECK(overlay_preview != nullptr);
    CHECK(overlay_preview->width() >= 200);
    dialog->findChild<QCheckBox*>(QStringLiteral("preferencesShowRulersCheck"))->setChecked(true);
    dialog->findChild<QCheckBox*>(QStringLiteral("preferencesShowGridCheck"))->setChecked(true);
    dialog->findChild<QCheckBox*>(QStringLiteral("preferencesShowGuidesCheck"))->setChecked(true);
    dialog->findChild<QCheckBox*>(QStringLiteral("preferencesLockGuidesCheck"))->setChecked(false);
    dialog->findChild<QCheckBox*>(QStringLiteral("preferencesSnapCheck"))->setChecked(false);
    dialog->findChild<QDoubleSpinBox*>(QStringLiteral("preferencesGridSpacingSpin"))->setValue(32.0);
    dialog->findChild<QSpinBox*>(QStringLiteral("preferencesGridSubdivisionsSpin"))->setValue(8);
    dialog->findChild<QComboBox*>(QStringLiteral("preferencesGridStyleCombo"))->setCurrentIndex(1);
    dialog->findChild<QCheckBox*>(QStringLiteral("preferencesSnapGridCheck"))->setChecked(false);
    saw_preferences = true;
    dialog->accept();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_preferences);
  CHECK(canvas->rulers_visible());
  CHECK(canvas->grid_visible());
  CHECK(canvas->guides_visible());
  CHECK(!canvas->snap_enabled());
  CHECK(canvas->grid_subdivisions() == 8);
  CHECK(canvas->grid_style() == 1);
  CHECK(canvas->grid_color().alpha() == 153);  // the 60% opacity spin
  CHECK(require_action(window, "viewToggleRulersAction")->isChecked());
  CHECK(require_action(window, "viewToggleGridAction")->isChecked());
  CHECK(!require_action(window, "viewToggleSnapAction")->isChecked());
  auto settings = patchy::ui::app_settings();
  CHECK(settings.value(QStringLiteral("view/gridSpacing32")).toInt() == 1024);
  CHECK(settings.value(QStringLiteral("view/gridSubdivisions")).toInt() == 8);

  bool saw_new_guide = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("newGuideDialog"));
    CHECK(dialog != nullptr);
    dialog->findChild<QComboBox*>(QStringLiteral("newGuideOrientationCombo"))->setCurrentIndex(0);
    dialog->findChild<QDoubleSpinBox*>(QStringLiteral("newGuidePositionSpin"))->setValue(24.0);
    saw_new_guide = true;
    dialog->accept();
  });
  require_action(window, "viewNewGuideAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_new_guide);
  CHECK(canvas->has_selected_guides());

  bool saw_layout = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("newGuideLayoutDialog"));
    CHECK(dialog != nullptr);
    dialog->findChild<QSpinBox*>(QStringLiteral("newGuideLayoutColumnsSpin"))->setValue(3);
    dialog->findChild<QSpinBox*>(QStringLiteral("newGuideLayoutRowsSpin"))->setValue(2);
    saw_layout = true;
    dialog->accept();
  });
  require_action(window, "viewNewGuideLayoutAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_layout);
  save_widget_artifact("ui_grid_guides_preferences_dialogs", window);
}

void ui_pen_preferences_persist_and_apply() {
  SettingsValueRestorer restore_enabled(QStringLiteral("input/pen/enabled"));
  SettingsValueRestorer restore_pressure_size(QStringLiteral("input/pen/pressureSize"));
  SettingsValueRestorer restore_pressure_size_min(QStringLiteral("input/pen/pressureSizeMinPercent"));
  SettingsValueRestorer restore_pressure_opacity(QStringLiteral("input/pen/pressureOpacity"));
  SettingsValueRestorer restore_pressure_opacity_min(QStringLiteral("input/pen/pressureOpacityMinPercent"));
  SettingsValueRestorer restore_eraser(QStringLiteral("input/pen/useEraserTip"));
  SettingsValueRestorer restore_primary_button(QStringLiteral("input/pen/primaryButtonAction"));
  SettingsValueRestorer restore_secondary_button(QStringLiteral("input/pen/secondaryButtonAction"));
  SettingsValueRestorer restore_wheel_zoom(QStringLiteral("input/wheelZooms"));
  SettingsValueRestorer restore_tilt(QStringLiteral("input/pen/tiltShape"));
  SettingsValueRestorer restore_tilt_roundness(QStringLiteral("input/pen/tiltMinRoundnessPercent"));

  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  bool saw_preferences = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    CHECK(select_preferences_page(*dialog, QStringLiteral("Pen")));
    dialog->findChild<QCheckBox*>(QStringLiteral("preferencesPenEnabledCheck"))->setChecked(true);
    dialog->findChild<QCheckBox*>(QStringLiteral("preferencesPenPressureSizeCheck"))->setChecked(false);
    dialog->findChild<QSpinBox*>(QStringLiteral("preferencesPenPressureSizeMinSpin"))->setValue(27);
    dialog->findChild<QCheckBox*>(QStringLiteral("preferencesPenPressureOpacityCheck"))->setChecked(true);
    dialog->findChild<QSpinBox*>(QStringLiteral("preferencesPenPressureOpacityMinSpin"))->setValue(33);
    dialog->findChild<QCheckBox*>(QStringLiteral("preferencesPenEraserTipCheck"))->setChecked(false);
    auto* primary_combo = dialog->findChild<QComboBox*>(QStringLiteral("preferencesPenPrimaryButtonCombo"));
    CHECK(primary_combo != nullptr);
    primary_combo->setCurrentIndex(primary_combo->findData(static_cast<int>(patchy::ui::PenButtonAction::Undo)));
    auto* secondary_combo = dialog->findChild<QComboBox*>(QStringLiteral("preferencesPenSecondaryButtonCombo"));
    CHECK(secondary_combo != nullptr);
    secondary_combo->setCurrentIndex(
        secondary_combo->findData(static_cast<int>(patchy::ui::PenButtonAction::ToggleEraser)));
    dialog->findChild<QCheckBox*>(QStringLiteral("preferencesWheelZoomCheck"))->setChecked(false);
    dialog->findChild<QCheckBox*>(QStringLiteral("preferencesPenTiltShapeCheck"))->setChecked(true);
    dialog->findChild<QSpinBox*>(QStringLiteral("preferencesPenTiltMinRoundnessSpin"))->setValue(44);
    saw_preferences = true;
    dialog->accept();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_preferences);

  auto settings = patchy::ui::app_settings();
  CHECK(settings.value(QStringLiteral("input/pen/enabled")).toBool());
  CHECK(!settings.value(QStringLiteral("input/pen/pressureSize")).toBool());
  CHECK(settings.value(QStringLiteral("input/pen/pressureSizeMinPercent")).toInt() == 27);
  CHECK(settings.value(QStringLiteral("input/pen/pressureOpacity")).toBool());
  CHECK(settings.value(QStringLiteral("input/pen/pressureOpacityMinPercent")).toInt() == 33);
  CHECK(!settings.value(QStringLiteral("input/pen/useEraserTip")).toBool());
  CHECK(settings.value(QStringLiteral("input/pen/primaryButtonAction")).toString() == QStringLiteral("undo"));
  CHECK(settings.value(QStringLiteral("input/pen/secondaryButtonAction")).toString() ==
        QStringLiteral("toggleEraser"));
  CHECK(!settings.value(QStringLiteral("input/wheelZooms")).toBool());
  CHECK(settings.value(QStringLiteral("input/pen/tiltShape")).toBool());
  CHECK(settings.value(QStringLiteral("input/pen/tiltMinRoundnessPercent")).toInt() == 44);

  const auto& pen = canvas->pen_input_settings();
  CHECK(pen.enabled);
  CHECK(!pen.pressure_size);
  CHECK(pen.pressure_size_min_percent == 27);
  CHECK(pen.pressure_opacity);
  CHECK(pen.pressure_opacity_min_percent == 33);
  CHECK(!pen.use_eraser_tip);
  CHECK(pen.primary_button_action == patchy::ui::PenButtonAction::Undo);
  CHECK(pen.secondary_button_action == patchy::ui::PenButtonAction::ToggleEraser);
  CHECK(!canvas->wheel_zooms());
  CHECK(pen.tilt_shape);
  CHECK(pen.tilt_min_roundness_percent == 44);
}

void ui_pen_preferences_spin_buttons_visible_and_increment_on_right() {
  patchy::ui::MainWindow window;
  show_window(window);

  bool saw_preferences = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    CHECK(select_preferences_page(*dialog, QStringLiteral("Pen")));
    dialog->findChild<QCheckBox*>(QStringLiteral("preferencesPenEnabledCheck"))->setChecked(true);
    dialog->findChild<QCheckBox*>(QStringLiteral("preferencesPenPressureSizeCheck"))->setChecked(true);
    QApplication::processEvents();

    auto* spin = dialog->findChild<QSpinBox*>(QStringLiteral("preferencesPenPressureSizeMinSpin"));
    CHECK(spin != nullptr);
    CHECK(spin->isEnabled());

    QStyleOptionSpinBox option;
    option.initFrom(spin);
    option.subControls = QStyle::SC_All;
    const auto up_rect = spin->style()->subControlRect(QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxUp, spin);
    const auto down_rect =
        spin->style()->subControlRect(QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxDown, spin);
    CHECK(up_rect.width() >= 20 && up_rect.height() >= 20);
    CHECK(down_rect.width() >= 20 && down_rect.height() >= 20);
    CHECK(down_rect.right() < up_rect.left());

    spin->setValue(50);
    send_mouse(*spin, QEvent::MouseButtonPress, up_rect.center(), Qt::LeftButton, Qt::LeftButton);
    send_mouse(*spin, QEvent::MouseButtonRelease, up_rect.center(), Qt::LeftButton, Qt::NoButton);
    CHECK(spin->value() == 51);
    send_mouse(*spin, QEvent::MouseButtonPress, down_rect.center(), Qt::LeftButton, Qt::LeftButton);
    send_mouse(*spin, QEvent::MouseButtonRelease, down_rect.center(), Qt::LeftButton, Qt::NoButton);
    CHECK(spin->value() == 50);

    save_widget_artifact("ui_pen_preferences_spin_buttons", *dialog);
    saw_preferences = true;
    dialog->reject();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_preferences);
}

// New Guide Layout (GitHub issue 403): every field shows its -/+ buttons in both
// schemes, the presets fill the fields, and gutters and margins place the guides.
void ui_new_guide_layout_dialog_presets_gutters_and_margins() {
  const QStringList keys{QStringLiteral("view/rulerUnits"),
                         QStringLiteral("view/guideLayoutColumns"),
                         QStringLiteral("view/guideLayoutRows"),
                         QStringLiteral("view/guideLayoutColumnGutter"),
                         QStringLiteral("view/guideLayoutRowGutter"),
                         QStringLiteral("view/guideLayoutMarginTop"),
                         QStringLiteral("view/guideLayoutMarginLeft"),
                         QStringLiteral("view/guideLayoutMarginBottom"),
                         QStringLiteral("view/guideLayoutMarginRight"),
                         QStringLiteral("view/guideLayoutClearExisting")};
  std::vector<std::unique_ptr<SettingsValueRestorer>> restorers;
  {
    auto settings = patchy::ui::app_settings();
    for (const auto& key : keys) {
      restorers.push_back(std::make_unique<SettingsValueRestorer>(key));
      if (key != QStringLiteral("view/rulerUnits")) {
        settings.remove(key);
      }
    }
  }
  ColorSchemeRestorer restore_scheme;

  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::create_document(window, 600, 300);
  patchy::ui::MainWindowTestAccess::set_ruler_unit(window, patchy::ui::MeasurementUnit::Pixels);
  (void)require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(document.width() == 600 && document.height() == 300);

  for (const auto preference :
       {patchy::ui::ColorSchemePreference::Dark, patchy::ui::ColorSchemePreference::Light}) {
    ColorSchemeRestorer::apply(preference);
    const std::string scheme = preference == patchy::ui::ColorSchemePreference::Light ? "light" : "dark";
    bool saw_dialog = false;
    QTimer::singleShot(0, [&] {
      auto* dialog = find_top_level_dialog(QStringLiteral("newGuideLayoutDialog"));
      CHECK(dialog != nullptr);
      if (dialog == nullptr) {
        return;
      }
      QApplication::processEvents();
      (void)capture_widget_artifact("ui_new_guide_layout_dialog_" + scheme, *dialog);
      const auto spins = dialog->findChildren<QAbstractSpinBox*>();
      CHECK(spins.size() >= 2);
      for (auto* spin : spins) {
        QStyleOptionSpinBox option;
        option.initFrom(spin);
        option.subControls = QStyle::SC_All;
        const auto up_rect =
            spin->style()->subControlRect(QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxUp, spin);
        const auto down_rect =
            spin->style()->subControlRect(QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxDown, spin);
        CHECK(spin->buttonSymbols() != QAbstractSpinBox::NoButtons);
        CHECK(up_rect.width() >= 20 && up_rect.height() >= 20);
        CHECK(down_rect.right() < up_rect.left());
      }
      saw_dialog = true;
      dialog->reject();
    });
    require_action(window, "viewNewGuideLayoutAction")->trigger();
    QApplication::processEvents();
    CHECK(saw_dialog);
  }
  CHECK(document.guides().empty());

  const auto positions = [&](patchy::GuideOrientation orientation) {
    std::vector<double> out;
    for (const auto& guide : document.guides()) {
      if (guide.orientation == orientation) {
        out.push_back(static_cast<double>(guide.position_32) / 32.0);
      }
    }
    std::sort(out.begin(), out.end());
    return out;
  };
  const auto guides_match = [](const std::vector<double>& actual, const std::vector<double>& expected) {
    if (actual.size() != expected.size()) {
      return false;
    }
    for (std::size_t index = 0; index < actual.size(); ++index) {
      if (std::abs(actual[index] - expected[index]) > 1.0 / 32.0) {
        return false;
      }
    }
    return true;
  };

  // The 3 x 3 preset fills the fields.
  bool saw_preset = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("newGuideLayoutDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* preset = dialog->findChild<QComboBox*>(QStringLiteral("newGuideLayoutPresetCombo"));
    auto* columns = dialog->findChild<QSpinBox*>(QStringLiteral("newGuideLayoutColumnsSpin"));
    auto* rows = dialog->findChild<QSpinBox*>(QStringLiteral("newGuideLayoutRowsSpin"));
    CHECK(preset != nullptr && columns != nullptr && rows != nullptr);
    if (preset == nullptr || columns == nullptr || rows == nullptr) {
      dialog->reject();
      return;
    }
    // The 2 x 2 default matches its preset.
    CHECK(preset->currentData().toString() == QStringLiteral("guideLayout.2x2"));
    const auto three = preset->findData(QStringLiteral("guideLayout.3x3"));
    CHECK(three > 0);
    CHECK(preset->findData(QStringLiteral("guideLayout.12columnsGutter")) > 0);
    preset->setCurrentIndex(three);
    CHECK(columns->value() == 3);
    CHECK(rows->value() == 3);
    saw_preset = true;
    dialog->accept();
  });
  require_action(window, "viewNewGuideLayoutAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_preset);
  CHECK(guides_match(positions(patchy::GuideOrientation::Vertical), {200.0, 400.0}));
  CHECK(guides_match(positions(patchy::GuideOrientation::Horizontal), {100.0, 200.0}));

  // Reopened, the dialog remembers the layout; editing a field turns the preset to Custom.
  bool saw_custom = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("newGuideLayoutDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* preset = dialog->findChild<QComboBox*>(QStringLiteral("newGuideLayoutPresetCombo"));
    auto* columns = dialog->findChild<QSpinBox*>(QStringLiteral("newGuideLayoutColumnsSpin"));
    auto* column_gutter = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("newGuideLayoutColumnGutterSpin"));
    auto* left = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("newGuideLayoutMarginLeftSpin"));
    auto* right = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("newGuideLayoutMarginRightSpin"));
    auto* top = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("newGuideLayoutMarginTopSpin"));
    auto* bottom = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("newGuideLayoutMarginBottomSpin"));
    auto* row_gutter = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("newGuideLayoutRowGutterSpin"));
    auto* clear = dialog->findChild<QCheckBox*>(QStringLiteral("newGuideLayoutClearExistingCheck"));
    CHECK(preset != nullptr && columns != nullptr && column_gutter != nullptr && left != nullptr &&
          right != nullptr && top != nullptr && bottom != nullptr && row_gutter != nullptr && clear != nullptr);
    if (preset == nullptr || columns == nullptr || column_gutter == nullptr || left == nullptr ||
        right == nullptr || top == nullptr || bottom == nullptr || row_gutter == nullptr || clear == nullptr) {
      dialog->reject();
      return;
    }
    CHECK(preset->currentData().toString() == QStringLiteral("guideLayout.3x3"));
    CHECK(columns->value() == 3);
    column_gutter->setValue(20.0);
    CHECK(preset->currentData().toString().isEmpty());  // Custom
    left->setValue(30.0);
    right->setValue(30.0);
    top->setValue(10.0);
    bottom->setValue(0.0);
    row_gutter->setValue(0.0);
    clear->setChecked(true);
    QApplication::processEvents();
    save_widget_artifact("ui_new_guide_layout_dialog_custom", *dialog);
    saw_custom = true;
    dialog->accept();
  });
  require_action(window, "viewNewGuideLayoutAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_custom);
  // Columns: content 30..570, three cells of (540 - 40) / 3 px with 20 px gutters.
  CHECK(guides_match(positions(patchy::GuideOrientation::Vertical),
             {30.0, 30.0 + 500.0 / 3.0, 50.0 + 500.0 / 3.0, 50.0 + 1000.0 / 3.0, 70.0 + 1000.0 / 3.0, 570.0}));
  // Rows: a 10 px top margin, then three cells over the remaining 290 px.
  CHECK(guides_match(positions(patchy::GuideOrientation::Horizontal), {10.0, 10.0 + 290.0 / 3.0, 10.0 + 580.0 / 3.0}));
  CHECK(patchy::ui::app_settings().value(QStringLiteral("view/guideLayoutColumnGutter")).toDouble() == 20.0);
  CHECK(patchy::ui::app_settings().value(QStringLiteral("view/guideLayoutClearExisting")).toBool());
  save_widget_artifact("ui_new_guide_layout_canvas", window);
}

// Saved New Guide Layout presets (GitHub issue 403): Save Preset... stores the fields under a
// name, the preset comes back after the dialog reopens and applies its values, saving under
// the same name asks before replacing it, built-in names are refused, and Delete Preset
// (only for saved presets) asks before removing it.
void ui_new_guide_layout_user_presets_save_apply_delete() {
  const QStringList keys{QStringLiteral("view/rulerUnits"),
                         QStringLiteral("view/guideLayoutColumns"),
                         QStringLiteral("view/guideLayoutRows"),
                         QStringLiteral("view/guideLayoutColumnGutter"),
                         QStringLiteral("view/guideLayoutRowGutter"),
                         QStringLiteral("view/guideLayoutMarginTop"),
                         QStringLiteral("view/guideLayoutMarginLeft"),
                         QStringLiteral("view/guideLayoutMarginBottom"),
                         QStringLiteral("view/guideLayoutMarginRight"),
                         QStringLiteral("view/guideLayoutClearExisting"),
                         QStringLiteral("view/guideLayoutUserPresets")};
  std::vector<std::unique_ptr<SettingsValueRestorer>> restorers;
  {
    auto settings = patchy::ui::app_settings();
    for (const auto& key : keys) {
      restorers.push_back(std::make_unique<SettingsValueRestorer>(key));
      if (key != QStringLiteral("view/rulerUnits")) {
        settings.remove(key);
      }
    }
  }
  const auto user_presets_json = [] {
    return patchy::ui::app_settings().value(QStringLiteral("view/guideLayoutUserPresets")).toByteArray();
  };

  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::create_document(window, 600, 300);
  patchy::ui::MainWindowTestAccess::set_ruler_unit(window, patchy::ui::MeasurementUnit::Pixels);
  (void)require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  // Arms a one-shot answer for a dialog that opens inside the next blocking call.
  const auto when_open = [](const QString& name, std::function<void(QDialog&)> act) {
    QTimer::singleShot(0, [name, act] {
      auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(name));
      CHECK(dialog != nullptr);
      if (dialog != nullptr) {
        act(*dialog);
      }
    });
  };
  const auto name_prompt = [when_open](const QString& text, std::function<void()> after = {}) {
    when_open(QStringLiteral("newGuideLayoutSavePresetNameDialog"), [text, after](QDialog& prompt) {
      auto* edit = prompt.findChild<QLineEdit*>();
      CHECK(edit != nullptr);
      if (edit != nullptr) {
        edit->setText(text);
      }
      if (after) {
        after();  // arms the box that opens once the prompt closes
      }
      prompt.accept();
    });
  };
  const auto answer_box = [when_open](const QString& name, QMessageBox::StandardButton button) {
    when_open(name, [button](QDialog& dialog) {
      auto* box = qobject_cast<QMessageBox*>(&dialog);
      CHECK(box != nullptr && box->button(button) != nullptr);
      if (box != nullptr && box->button(button) != nullptr) {
        box->button(button)->click();
      } else {
        dialog.reject();
      }
    });
  };
  struct Fields {
    QComboBox* preset{nullptr};
    QAbstractButton* save{nullptr};
    QAbstractButton* remove{nullptr};
    QSpinBox* columns{nullptr};
    QSpinBox* rows{nullptr};
    QDoubleSpinBox* column_gutter{nullptr};
    QDoubleSpinBox* left{nullptr};
    QCheckBox* clear{nullptr};
    bool ok() const {
      return preset != nullptr && save != nullptr && remove != nullptr && columns != nullptr && rows != nullptr &&
             column_gutter != nullptr && left != nullptr && clear != nullptr;
    }
  };
  const auto fields_of = [](QWidget& dialog) {
    Fields fields;
    fields.preset = dialog.findChild<QComboBox*>(QStringLiteral("newGuideLayoutPresetCombo"));
    fields.save = dialog.findChild<QAbstractButton*>(QStringLiteral("newGuideLayoutSavePresetButton"));
    fields.remove = dialog.findChild<QAbstractButton*>(QStringLiteral("newGuideLayoutDeletePresetButton"));
    fields.columns = dialog.findChild<QSpinBox*>(QStringLiteral("newGuideLayoutColumnsSpin"));
    fields.rows = dialog.findChild<QSpinBox*>(QStringLiteral("newGuideLayoutRowsSpin"));
    fields.column_gutter = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("newGuideLayoutColumnGutterSpin"));
    fields.left = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("newGuideLayoutMarginLeftSpin"));
    fields.clear = dialog.findChild<QCheckBox*>(QStringLiteral("newGuideLayoutClearExistingCheck"));
    CHECK(fields.ok());
    return fields;
  };
  const auto run_dialog = [&](const std::function<void(QDialog&, Fields&)>& act) {
    bool saw = false;
    QTimer::singleShot(0, [&] {
      auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("newGuideLayoutDialog")));
      CHECK(dialog != nullptr);
      if (dialog == nullptr) {
        return;
      }
      auto fields = fields_of(*dialog);
      if (!fields.ok()) {
        dialog->reject();
        return;
      }
      act(*dialog, fields);
      saw = true;
      if (dialog->isVisible()) {
        dialog->reject();
      }
    });
    require_action(window, "viewNewGuideLayoutAction")->trigger();
    QApplication::processEvents();
    CHECK(saw);
  };

  // Save a layout under a name; a built-in name is refused.
  run_dialog([&](QDialog& dialog, Fields& f) {
    CHECK(!f.remove->isEnabled());  // the 2 x 2 default is built in
    f.columns->setValue(5);
    f.rows->setValue(0);
    f.column_gutter->setValue(10.0);
    f.left->setValue(20.0);
    f.clear->setChecked(true);
    CHECK(f.preset->currentData().toString().isEmpty());  // Custom
    CHECK(!f.remove->isEnabled());
    name_prompt(QStringLiteral("3 x 3 Grid"), [answer_box] {
      answer_box(QStringLiteral("newGuideLayoutPresetNameMessageBox"), QMessageBox::Ok);
    });
    f.save->click();
    QApplication::processEvents();
    CHECK(user_presets_json().isEmpty());
    name_prompt(QStringLiteral("Five Columns"));
    f.save->click();
    QApplication::processEvents();
    CHECK(f.preset->currentText() == QStringLiteral("Five Columns"));
    CHECK(f.remove->isEnabled());
    QApplication::processEvents();
    save_widget_artifact("ui_new_guide_layout_user_preset", dialog);
    // Cancel: the preset stays saved, the last-used fields do not change.
  });
  const auto saved = QJsonDocument::fromJson(user_presets_json()).array();
  CHECK(saved.size() == 1);
  if (saved.size() == 1) {
    const auto object = saved.first().toObject();
    CHECK(object.value(QStringLiteral("name")).toString() == QStringLiteral("Five Columns"));
    CHECK(object.value(QStringLiteral("unit")).toString() == QStringLiteral("px"));
    CHECK(object.value(QStringLiteral("columns")).toInt() == 5);
    CHECK(object.value(QStringLiteral("columnGutter")).toDouble() == 10.0);
    CHECK(object.value(QStringLiteral("marginLeft")).toDouble() == 20.0);
    CHECK(object.value(QStringLiteral("clearExisting")).toBool());
  }

  // Reopened: the saved preset is listed after the built-ins and applies its values; saving
  // under its name (any case) asks before replacing it.
  run_dialog([&](QDialog& dialog, Fields& f) {
    CHECK(f.preset->currentData().toString() == QStringLiteral("guideLayout.2x2"));
    CHECK(!f.clear->isChecked());
    const auto row = f.preset->findText(QStringLiteral("Five Columns"));
    CHECK(row > f.preset->findData(QStringLiteral("guideLayout.12columnsGutter")));
    f.preset->setCurrentIndex(row);
    CHECK(f.columns->value() == 5 && f.rows->value() == 0);
    CHECK(f.column_gutter->value() == 10.0 && f.left->value() == 20.0);
    CHECK(f.clear->isChecked());
    CHECK(f.remove->isEnabled());
    f.columns->setValue(6);
    CHECK(f.preset->currentData().toString().isEmpty());
    // Declining keeps the old values.
    name_prompt(QStringLiteral("five columns"), [answer_box] {
      answer_box(QStringLiteral("newGuideLayoutReplacePresetMessageBox"), QMessageBox::No);
    });
    f.save->click();
    QApplication::processEvents();
    CHECK(QJsonDocument::fromJson(user_presets_json()).array().first().toObject().value(QStringLiteral("columns"))
              .toInt() == 5);
    name_prompt(QStringLiteral("Five Columns"), [answer_box] {
      answer_box(QStringLiteral("newGuideLayoutReplacePresetMessageBox"), QMessageBox::Yes);
    });
    f.save->click();
    QApplication::processEvents();
    CHECK(QJsonDocument::fromJson(user_presets_json()).array().size() == 1);
    CHECK(QJsonDocument::fromJson(user_presets_json()).array().first().toObject().value(QStringLiteral("columns"))
              .toInt() == 6);
    CHECK(f.preset->currentText() == QStringLiteral("Five Columns"));
    dialog.accept();
  });
  // Six columns with 10 px gutters after a 20 px left margin: the margin guide and a pair per division.
  std::size_t vertical = 0;
  std::size_t horizontal = 0;
  for (const auto& guide : document.guides()) {
    (guide.orientation == patchy::GuideOrientation::Vertical ? vertical : horizontal) += 1;
  }
  CHECK(vertical == 11);
  CHECK(horizontal == 0);

  // Delete Preset: only for saved presets, and it asks first.
  run_dialog([&](QDialog&, Fields& f) {
    CHECK(f.preset->currentText() == QStringLiteral("Five Columns"));  // the remembered fields match it
    CHECK(f.remove->isEnabled());
    f.preset->setCurrentIndex(f.preset->findData(QStringLiteral("guideLayout.3x3")));
    CHECK(!f.remove->isEnabled());
    f.preset->setCurrentIndex(f.preset->findText(QStringLiteral("Five Columns")));
    CHECK(f.remove->isEnabled());
    answer_box(QStringLiteral("newGuideLayoutDeletePresetMessageBox"), QMessageBox::No);
    f.remove->click();
    QApplication::processEvents();
    CHECK(f.preset->findText(QStringLiteral("Five Columns")) > 0);
    answer_box(QStringLiteral("newGuideLayoutDeletePresetMessageBox"), QMessageBox::Yes);
    f.remove->click();
    QApplication::processEvents();
    CHECK(f.preset->findText(QStringLiteral("Five Columns")) < 0);
    CHECK(f.preset->currentData().toString().isEmpty());  // the fields stay, now Custom
    CHECK(f.columns->value() == 6);
    CHECK(!f.remove->isEnabled());
  });
  CHECK(!patchy::ui::app_settings().contains(QStringLiteral("view/guideLayoutUserPresets")));
}

}  // namespace

// A typed physical unit in a pixel-only field (Feather, the Rectangle tool's
// corner radius) converts at the document PPI, not a fixed 300 (issue 53).
void ui_feather_field_typed_unit_uses_document_ppi() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  document.print_settings().horizontal_ppi = 72.0;
  document.print_settings().vertical_ppi = 72.0;
  const auto commit_text = [](QAbstractSpinBox& spin, const QString& text) {
    auto* editor = spin.findChild<QLineEdit*>();
    CHECK(editor != nullptr);
    editor->setText(text);
    send_key(spin, Qt::Key_Return);
    QApplication::processEvents();
  };

  require_action_by_text(window, QStringLiteral("Marquee"))->trigger();
  auto* feather = window.findChild<QSpinBox*>(QStringLiteral("selectionFeatherSpin"));
  CHECK(feather != nullptr);
  CHECK(feather->suffix() == patchy::ui::pixel_suffix());  // stays a px field
  commit_text(*feather, QStringLiteral("1 in"));
  CHECK(feather->value() == 72);
  CHECK(canvas->selection_feather_radius() == 72);
  CHECK(feather->text() == QStringLiteral("72") + patchy::ui::pixel_suffix());

  document.print_settings().horizontal_ppi = 300.0;
  document.print_settings().vertical_ppi = 300.0;
  commit_text(*feather, QStringLiteral("1 in"));
  CHECK(feather->value() == 300);
  commit_text(*feather, QStringLiteral("0"));
  CHECK(feather->value() == 0);

  require_action(window, "toolRectAction")->trigger();
  QApplication::processEvents();
  auto* radius = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  CHECK(radius != nullptr);
  commit_text(*radius, QStringLiteral("10 mm"));
  CHECK(radius->value() == 118);  // 300 / 25.4 * 10, rounded
  commit_text(*radius, QStringLiteral("0"));
}

std::vector<patchy::test::TestCase> selection_marquee_lasso_tests_part1() {
  return {
      {"ui_feather_field_typed_unit_uses_document_ppi", ui_feather_field_typed_unit_uses_document_ppi},
      {"ui_marquee_selection_modifiers_work", ui_marquee_selection_modifiers_work},
      {"ui_marquee_click_outside_canvas_deselects", ui_marquee_click_outside_canvas_deselects},
      {"ui_marquee_shift_drag_constrains_to_square", ui_marquee_shift_drag_constrains_to_square},
      {"ui_marquee_drag_readout_draws_a_pill_by_the_pointer", ui_marquee_drag_readout_draws_a_pill_by_the_pointer},
      {"ui_marquee_drag_keeps_minimum_one_pixel", ui_marquee_drag_keeps_minimum_one_pixel},
      {"ui_selection_toolbar_modes_work", ui_selection_toolbar_modes_work},
      {"ui_ctrl_a_selects_entire_canvas", ui_ctrl_a_selects_entire_canvas},
      {"ui_alt_backspace_fills_selection_with_foreground", ui_alt_backspace_fills_selection_with_foreground},
      {"ui_feathered_marquee_fill_uses_soft_selection_alpha",
       ui_feathered_marquee_fill_uses_soft_selection_alpha},
      {"ui_feathered_selection_add_keeps_existing_selection",
       ui_feathered_selection_add_keeps_existing_selection},
      {"ui_marquee_corner_radius_rounds_selection", ui_marquee_corner_radius_rounds_selection},
      {"ui_marquee_fixed_size_and_ratio_options_work", ui_marquee_fixed_size_and_ratio_options_work},
      {"ui_marquee_swap_button_swaps_width_and_height", ui_marquee_swap_button_swaps_width_and_height},
      {"ui_marquee_disabled_size_fields_open_no_value_popup", ui_marquee_disabled_size_fields_open_no_value_popup},
      {"ui_shape_fill_and_corner_radius_apply_to_new_documents",
       ui_shape_fill_and_corner_radius_apply_to_new_documents},
      {"ui_shape_tool_fixed_size_and_ratio_options_work", ui_shape_tool_fixed_size_and_ratio_options_work},
      {"ui_elliptical_marquee_selects_oval_region", ui_elliptical_marquee_selects_oval_region},
      {"ui_marquee_space_drag_repositions_active_rect", ui_marquee_space_drag_repositions_active_rect},
      {"ui_info_panel_shows_selection_rect", ui_info_panel_shows_selection_rect},
      {"ui_rulers_grid_guides_render_and_edit", ui_rulers_grid_guides_render_and_edit},
      {"ui_guide_drag_shows_position_readout", ui_guide_drag_shows_position_readout},
      {"ui_guide_drag_lands_on_whole_pixels", ui_guide_drag_lands_on_whole_pixels},
      {"ui_guides_draw_across_the_pasteboard", ui_guides_draw_across_the_pasteboard},
      {"ui_guide_dragged_onto_pasteboard_stays", ui_guide_dragged_onto_pasteboard_stays},
      {"ui_issue231_sample_guides_outside_document_if_available",
       ui_issue231_sample_guides_outside_document_if_available},
      {"ui_deep_zoom_pixel_grid_matches_rendered_pixels",
       ui_deep_zoom_pixel_grid_matches_rendered_pixels},
      {"ui_deep_zoom_one_pixel_brush_marks_match_pixel_grid",
       ui_deep_zoom_one_pixel_brush_marks_match_pixel_grid},
      {"ui_deep_zoom_subpixel_subdivisions_do_not_draw_inside_pixels",
       ui_deep_zoom_subpixel_subdivisions_do_not_draw_inside_pixels},
      {"ui_deep_zoom_fractional_subdivision_spacing_stays_on_pixel_edges",
       ui_deep_zoom_fractional_subdivision_spacing_stays_on_pixel_edges},
      {"ui_deep_zoom_grid_subdivision_counts_change_spacing",
       ui_deep_zoom_grid_subdivision_counts_change_spacing},
      {"ui_snap_marquee_uses_screen_pixel_tolerance_and_target_toggles",
       ui_snap_marquee_uses_screen_pixel_tolerance_and_target_toggles},
      {"ui_shift_constrains_shape_drag_to_square", ui_shift_constrains_shape_drag_to_square},
      {"ui_shape_tool_alt_draws_from_center", ui_shape_tool_alt_draws_from_center},
      {"ui_drag_size_readout_shows_dimensions", ui_drag_size_readout_shows_dimensions},
      {"ui_snap_applies_to_shape_text_and_move_tools", ui_snap_applies_to_shape_text_and_move_tools},
      {"ui_canvas_aid_preferences_and_guide_dialogs_work", ui_canvas_aid_preferences_and_guide_dialogs_work},
      {"ui_pen_preferences_persist_and_apply", ui_pen_preferences_persist_and_apply},
      {"ui_pen_preferences_spin_buttons_visible_and_increment_on_right",
       ui_pen_preferences_spin_buttons_visible_and_increment_on_right},
      {"ui_new_guide_layout_dialog_presets_gutters_and_margins",
       ui_new_guide_layout_dialog_presets_gutters_and_margins},
      {"ui_new_guide_layout_user_presets_save_apply_delete",
       ui_new_guide_layout_user_presets_save_apply_delete},
  };
}
