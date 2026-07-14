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

  style->setCurrentText(QStringLiteral("Fixed Ratio"));
  width->setValue(2);
  height->setValue(1);
  QApplication::processEvents();
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
}

void ui_shape_fill_and_corner_radius_apply_to_new_documents() {
  SettingsValueRestorer saved_shape_corner_radius(QStringLiteral("tools/shapeCornerRadius"));
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Rect"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Rectangle);

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
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Rect"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Rectangle);

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
    // (see the offscreen keyboardModifiers() note in AGENTS.md).
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
    auto* tabs = dialog->findChild<QTabWidget*>(QStringLiteral("preferencesTabWidget"));
    CHECK(tabs != nullptr);
    CHECK(tabs->count() == 5);
    CHECK(tabs->tabText(1) == QStringLiteral("Pen"));
    CHECK(tabs->tabText(2) == QStringLiteral("Grid and Guides"));
    CHECK(tabs->tabText(3) == QStringLiteral("Snapping"));
    CHECK(tabs->tabText(4) == QStringLiteral("Hotkeys"));
    auto* grid_color_button = dialog->findChild<QPushButton*>(QStringLiteral("preferencesGridColorButton"));
    CHECK(grid_color_button != nullptr);
    CHECK(grid_color_button->text().contains(QStringLiteral("#")));
    CHECK(grid_color_button->text().contains(QStringLiteral("%")));
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
    auto* tabs = dialog->findChild<QTabWidget*>(QStringLiteral("preferencesTabWidget"));
    CHECK(tabs != nullptr);
    CHECK(tabs->tabText(1) == QStringLiteral("Pen"));
    tabs->setCurrentIndex(1);
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
    dialog->findChild<QCheckBox*>(QStringLiteral("preferencesPenWheelZoomCheck"))->setChecked(false);
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
    auto* tabs = dialog->findChild<QTabWidget*>(QStringLiteral("preferencesTabWidget"));
    CHECK(tabs != nullptr);
    tabs->setCurrentIndex(1);
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

void ui_complex_selection_draws_region_outline() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(20, 20)),
       canvas->widget_position_for_document_point(QPoint(160, 90)));
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(80, 90)),
       canvas->widget_position_for_document_point(QPoint(160, 210)), Qt::ShiftModifier);
  QApplication::processEvents();

  const auto& selection = canvas->selected_document_region();
  CHECK(selection.contains(QPoint(30, 30)));
  CHECK(selection.contains(QPoint(120, 150)));
  CHECK(!selection.contains(QPoint(40, 150)));

  const auto image = canvas->grab().toImage().convertToFormat(QImage::Format_RGB32);
  int inner_outline_pixels = 0;
  for (int document_y = 96; document_y <= 204; ++document_y) {
    const auto boundary = canvas->widget_position_for_document_point(QPoint(80, document_y));
    bool found_dark_ant_pixel = false;
    for (int dx = -1; dx <= 1; ++dx) {
      const auto sample = image.pixelColor(boundary + QPoint(dx, 0));
      if (sample.red() < 70 && sample.green() < 70 && sample.blue() < 70) {
        found_dark_ant_pixel = true;
      }
    }
    if (found_dark_ant_pixel) {
      ++inner_outline_pixels;
    }
  }
  CHECK(inner_outline_pixels >= 8);
  save_widget_artifact("ui_complex_selection_outline", *canvas);
}

void ui_ctrl_h_hides_selection_edges_without_blue_tint() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(90, 80)),
       canvas->widget_position_for_document_point(QPoint(190, 155)));
  QApplication::processEvents();
  CHECK(canvas->has_selection());
  CHECK(canvas->selection_edges_visible());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(130, 115)), Qt::white, 2));

  const auto count_black_edge_pixels = [&canvas] {
    const auto image = canvas->grab().toImage();
    const auto left = canvas->widget_position_for_document_point(QPoint(90, 80));
    const auto right = canvas->widget_position_for_document_point(QPoint(190, 80));
    int pixels = 0;
    for (int y = left.y() - 2; y <= left.y() + 2; ++y) {
      for (int x = left.x(); x <= right.x(); ++x) {
        if (x < 0 || y < 0 || x >= image.width() || y >= image.height()) {
          continue;
        }
        const auto color = image.pixelColor(x, y);
        if (color.red() < 70 && color.green() < 70 && color.blue() < 70) {
          ++pixels;
        }
      }
    }
    return pixels;
  };

  CHECK(count_black_edge_pixels() > 4);
  save_widget_artifact("ui_selection_edges_visible_no_tint", *canvas);

  send_key(*canvas, Qt::Key_H, Qt::ControlModifier);
  QApplication::processEvents();
  CHECK(!canvas->selection_edges_visible());
  CHECK(canvas->has_selection());
  CHECK(count_black_edge_pixels() == 0);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(130, 115)), Qt::white, 2));
  save_widget_artifact("ui_selection_edges_hidden", *canvas);

  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();
  CHECK(!canvas->has_selection());
  CHECK(canvas->selection_edges_visible());

  send_key(*canvas, Qt::Key_H, Qt::ControlModifier);
  QApplication::processEvents();
  CHECK(!canvas->selection_edges_visible());
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(30, 30)),
       canvas->widget_position_for_document_point(QPoint(80, 70)));
  QApplication::processEvents();
  CHECK(canvas->has_selection());
  CHECK(canvas->selection_edges_visible());
}

void ui_select_inverse_and_extended_blend_modes_work() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  const QPoint inside(35, 35);
  const QPoint outside(150, 150);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(20, 20)),
       canvas->widget_position_for_document_point(QPoint(70, 70)));
  CHECK(canvas->selected_document_region().contains(inside));
  CHECK(!canvas->selected_document_region().contains(outside));
  require_action(window, "selectInverseAction")->trigger();
  QApplication::processEvents();
  CHECK(!canvas->selected_document_region().contains(inside));
  CHECK(canvas->selected_document_region().contains(outside));
  save_widget_artifact("ui_select_inverse", *canvas);

  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();
  CHECK(!canvas->has_selection());
  require_action(window, "selectReselectAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->selected_document_region().contains(outside));
  CHECK(!canvas->selected_document_region().contains(inside));
  save_widget_artifact("ui_select_reselect", *canvas);

  require_action(window, "editDeselectAction")->trigger();
  auto* blend_combo = window.findChild<QComboBox*>(QStringLiteral("layerBlendModeCombo"));
  CHECK(blend_combo != nullptr);
  canvas->set_primary_color(QColor(255, 0, 0));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  const auto difference_index = blend_combo->findText(QStringLiteral("Difference"));
  CHECK(difference_index >= 0);
  blend_combo->setCurrentIndex(difference_index);
  QApplication::processEvents();

  const auto sample = canvas_pixel(*canvas, QPoint(30, 30));
  CHECK(sample.red() < 40);
  CHECK(sample.green() > 220);
  CHECK(sample.blue() > 220);
  save_widget_artifact("ui_extended_blend_modes", window);
}

void ui_selection_expand_contract_and_layer_transparency_work() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(40, 40)),
       canvas->widget_position_for_document_point(QPoint(100, 100)));
  canvas->set_primary_color(QColor(40, 180, 255));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();

  require_action(window, "selectLayerTransparencyAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->selected_document_region().contains(QPoint(70, 70)));
  CHECK(canvas->selected_document_region().contains(QPoint(42, 50)));
  CHECK(!canvas->selected_document_region().contains(QPoint(34, 50)));

  canvas->contract_selection(8);
  QApplication::processEvents();
  CHECK(canvas->selected_document_region().contains(QPoint(70, 70)));
  CHECK(!canvas->selected_document_region().contains(QPoint(42, 50)));

  canvas->expand_selection(6);
  QApplication::processEvents();
  CHECK(canvas->selected_document_region().contains(QPoint(44, 50)));
  CHECK(!canvas->selected_document_region().contains(QPoint(30, 50)));

  require_action(window, "selectInverseAction")->trigger();
  QApplication::processEvents();
  CHECK(!canvas->selected_document_region().contains(QPoint(70, 70)));
  CHECK(canvas->selected_document_region().contains(QPoint(30, 50)));
  save_widget_artifact("ui_selection_expand_contract_transparency", *canvas);

  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(160, 40)),
       canvas->widget_position_for_document_point(QPoint(230, 110)));
  canvas->border_selection(7);
  QApplication::processEvents();
  CHECK(canvas->selected_document_region().contains(QPoint(162, 70)));
  CHECK(canvas->selected_document_region().contains(QPoint(226, 70)));
  CHECK(!canvas->selected_document_region().contains(QPoint(195, 75)));
  CHECK(!canvas->selected_document_region().contains(QPoint(148, 70)));
  save_widget_artifact("ui_selection_border", *canvas);
}

void ui_ctrl_click_layer_loads_layer_transparency() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(55, 45)),
       canvas->widget_position_for_document_point(QPoint(120, 95)));
  canvas->set_primary_color(QColor(20, 130, 230));
  require_action(window, "layerFillForegroundAction")->trigger();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();
  CHECK(!canvas->has_selection());

  QListWidgetItem* paint_layer_item = nullptr;
  for (int row = 0; row < layer_list->count(); ++row) {
    if (layer_list->item(row)->text() == QStringLiteral("Paint Layer")) {
      paint_layer_item = layer_list->item(row);
      break;
    }
  }
  CHECK(paint_layer_item != nullptr);
  auto* paint_layer_row = layer_list->itemWidget(paint_layer_item);
  CHECK(paint_layer_row != nullptr);
  auto* visibility = paint_layer_row->findChild<QToolButton*>(QStringLiteral("layerVisibilityCheck"));
  CHECK(visibility != nullptr);
  const auto check_state_before = paint_layer_item->checkState();
  send_mouse(*visibility, QEvent::MouseButtonPress, visibility->rect().center(), Qt::LeftButton, Qt::LeftButton,
             Qt::ControlModifier);
  send_mouse(*visibility, QEvent::MouseButtonRelease, visibility->rect().center(), Qt::LeftButton, Qt::NoButton,
             Qt::ControlModifier);
  QApplication::processEvents();

  CHECK(!canvas->has_selection());
  CHECK(paint_layer_item->checkState() == check_state_before);

  auto* thumbnail = paint_layer_row->findChild<QLabel*>(QStringLiteral("layerContentThumbnail"));
  CHECK(thumbnail != nullptr);
  send_mouse(*thumbnail, QEvent::MouseButtonPress, thumbnail->rect().center(), Qt::LeftButton, Qt::LeftButton,
             Qt::ControlModifier);
  send_mouse(*thumbnail, QEvent::MouseButtonRelease, thumbnail->rect().center(), Qt::LeftButton, Qt::NoButton,
             Qt::ControlModifier);
  QApplication::processEvents();

  CHECK(canvas->selected_document_region().contains(QPoint(70, 60)));
  CHECK(!canvas->selected_document_region().contains(QPoint(30, 60)));
  CHECK(paint_layer_item->checkState() == check_state_before);
  save_widget_artifact("ui_ctrl_click_layer_transparency", window);
}

void ui_ctrl_click_layer_and_mask_preserve_soft_coverage() {
  patchy::Document document(72, 60, patchy::PixelFormat::rgb8());
  patchy::PixelBuffer pixels(72, 60, patchy::PixelFormat::rgba8());
  const auto coverage_for_coordinate = [](int coordinate) -> std::uint8_t {
    if (coordinate < 4) {
      return 0;
    }
    if (coordinate < 16) {
      return 1;
    }
    if (coordinate < 28) {
      return 60;
    }
    if (coordinate < 40) {
      return 127;
    }
    if (coordinate < 52) {
      return 128;
    }
    return 255;
  };
  for (int y = 0; y < pixels.height(); ++y) {
    for (int x = 0; x < pixels.width(); ++x) {
      auto* pixel = pixels.pixel(x, y);
      pixel[0] = 210;
      pixel[1] = 120;
      pixel[2] = 40;
      pixel[3] = coverage_for_coordinate(x);
    }
  }
  auto& layer = document.add_pixel_layer("Soft Pixels", std::move(pixels));
  patchy::PixelBuffer mask_pixels(72, 60, patchy::PixelFormat::gray8());
  for (int y = 0; y < mask_pixels.height(); ++y) {
    auto row = mask_pixels.row(y);
    std::fill(row.begin(), row.end(), coverage_for_coordinate(y));
  }
  layer.set_mask(patchy::LayerMask{patchy::Rect{0, 0, 72, 60}, std::move(mask_pixels), 0, false});

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Soft Transparency"));
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layers = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layers != nullptr);

  click_layer_row_thumbnail(*layers, QStringLiteral("Soft Pixels"), QStringLiteral("layerContentThumbnail"),
                            Qt::ControlModifier);
  CHECK(canvas->selection_has_partial_alpha());
  for (const auto [x, expected] : std::array<std::pair<int, std::uint8_t>, 6>{
           std::pair{2, std::uint8_t{0}}, std::pair{8, std::uint8_t{1}},
           std::pair{20, std::uint8_t{60}}, std::pair{32, std::uint8_t{127}},
           std::pair{44, std::uint8_t{128}}, std::pair{60, std::uint8_t{255}}}) {
    CHECK(canvas->selection_alpha_at(QPoint(x, 20)) == expected);
  }

  auto& active_document = patchy::ui::MainWindowTestAccess::document(window);
  require_action(window, "channelSaveSelectionAction")->trigger();
  QApplication::processEvents();
  CHECK(active_document.channels().size() == 1);
  const auto& saved = active_document.channels().front().pixels();
  CHECK(*saved.pixel(8, 20) == 1);
  CHECK(*saved.pixel(20, 20) == 60);
  CHECK(*saved.pixel(32, 20) == 127);
  CHECK(*saved.pixel(44, 20) == 128);
  CHECK(*saved.pixel(60, 20) == 255);
  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  CHECK(active_document.channels().empty());

  click_layer_row_thumbnail(*layers, QStringLiteral("Soft Pixels"), QStringLiteral("layerMaskThumbnail"),
                            Qt::ControlModifier);
  CHECK(canvas->selection_has_partial_alpha());
  for (const auto [y, expected] : std::array<std::pair<int, std::uint8_t>, 6>{
           std::pair{2, std::uint8_t{0}}, std::pair{8, std::uint8_t{1}},
           std::pair{20, std::uint8_t{60}}, std::pair{32, std::uint8_t{127}},
           std::pair{44, std::uint8_t{128}}, std::pair{56, std::uint8_t{255}}}) {
    CHECK(canvas->selection_alpha_at(QPoint(20, y)) == expected);
  }
}

void ui_deko_layer_transparency_selection_and_channel_save_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("deko_test.psd");
  if (!std::filesystem::exists(path)) {
    return;
  }

  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(window, QString::fromStdWString(path.wstring()));
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  auto* layers = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layers != nullptr);
  auto* layer_item = require_layer_item(*layers, QStringLiteral("Layer 2"));
  const auto layer_id =
      static_cast<patchy::LayerId>(layer_item->data(patchy::ui::kLayerIdRole).toULongLong());
  auto& active_document = patchy::ui::MainWindowTestAccess::document(window);
  const auto* layer = static_cast<const patchy::Document&>(active_document).find_layer(layer_id);
  CHECK(layer != nullptr);
  CHECK(layer->pixels().format() == patchy::PixelFormat::rgba8());

  std::optional<QPoint> faint_point;
  std::optional<QPoint> strong_point;
  std::uint8_t faint_alpha = 0;
  std::uint8_t strong_alpha = 0;
  for (int y = 0; y < layer->pixels().height() && (!faint_point.has_value() || !strong_point.has_value()); ++y) {
    for (int x = 0; x < layer->pixels().width(); ++x) {
      const auto alpha = layer->pixels().pixel(x, y)[3];
      const QPoint document_point(layer->bounds().x + x, layer->bounds().y + y);
      if (!faint_point.has_value() && alpha > 0 && alpha < 16) {
        faint_point = document_point;
        faint_alpha = alpha;
      }
      if (!strong_point.has_value() && alpha >= 128 && alpha < 255) {
        strong_point = document_point;
        strong_alpha = alpha;
      }
      if (faint_point.has_value() && strong_point.has_value()) {
        break;
      }
    }
  }
  CHECK(faint_point.has_value());
  CHECK(strong_point.has_value());

  click_layer_row_thumbnail(*layers, QStringLiteral("Layer 2"), QStringLiteral("layerContentThumbnail"),
                            Qt::ControlModifier);
  CHECK(canvas->selection_has_partial_alpha());
  CHECK(canvas->selection_alpha_at(*faint_point) == faint_alpha);
  CHECK(canvas->selection_alpha_at(*strong_point) == strong_alpha);

  const auto channel_count_before = active_document.channels().size();
  QElapsedTimer timer;
  timer.start();
  require_action(window, "channelSaveSelectionAction")->trigger();
  const auto elapsed_ms = timer.elapsed();
  QApplication::processEvents();
  CHECK(elapsed_ms < 5000);
  CHECK(active_document.channels().size() == channel_count_before + 1);
  const auto& saved = active_document.channels().back().pixels();
  CHECK(*saved.pixel(faint_point->x(), faint_point->y()) == faint_alpha);
  CHECK(*saved.pixel(strong_point->x(), strong_point->y()) == strong_alpha);
  save_widget_artifact("ui_deko_soft_layer_selection", window);
}

void ui_select_grow_and_similar_use_magic_wand_tolerance() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  canvas->set_snap_enabled(false);
  canvas->set_wand_tolerance(8);

  canvas->set_primary_color(QColor(220, 20, 40));
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(20, 20)),
       canvas->widget_position_for_document_point(QPoint(70, 70)));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(140, 20)),
       canvas->widget_position_for_document_point(QPoint(190, 70)));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();

  canvas->set_primary_color(QColor(30, 80, 230));
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(20, 140)),
       canvas->widget_position_for_document_point(QPoint(70, 190)));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(34, 34)),
       canvas->widget_position_for_document_point(QPoint(40, 40)));
  CHECK(canvas->selected_document_region().contains(QPoint(36, 36)));
  CHECK(!canvas->selected_document_region().contains(QPoint(66, 66)));

  require_action(window, "selectGrowAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->selected_document_region().contains(QPoint(66, 66)));
  CHECK(!canvas->selected_document_region().contains(QPoint(150, 40)));
  CHECK(!canvas->selected_document_region().contains(QPoint(40, 150)));
  save_widget_artifact("ui_select_grow", *canvas);

  require_action(window, "selectSimilarAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->selected_document_region().contains(QPoint(66, 66)));
  CHECK(canvas->selected_document_region().contains(QPoint(150, 40)));
  CHECK(!canvas->selected_document_region().contains(QPoint(40, 150)));
  CHECK(!canvas->selected_document_region().contains(QPoint(240, 40)));
  save_widget_artifact("ui_select_similar", *canvas);
}

void ui_complex_selection_stroke_uses_region_outline() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(24, 24)),
       canvas->widget_position_for_document_point(QPoint(72, 72)));
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(132, 132)),
       canvas->widget_position_for_document_point(QPoint(180, 180)), Qt::ShiftModifier);
  CHECK(canvas->selected_document_region().contains(QPoint(40, 40)));
  CHECK(canvas->selected_document_region().contains(QPoint(150, 150)));
  CHECK(!canvas->selected_document_region().contains(QPoint(98, 98)));

  canvas->set_primary_color(QColor(20, 230, 90));
  canvas->set_brush_size(7);
  require_action(window, "editStrokeSelectionAction")->trigger();
  QApplication::processEvents();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(24, 45)), QColor(20, 230, 90), 55));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(132, 150)), QColor(20, 230, 90), 55));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(98, 98)), QColor(255, 255, 255), 8));
  save_widget_artifact("ui_complex_stroke_selection", *canvas);
}

void ui_layer_lock_transparency_and_keyboard_nudge_work() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* lock = window.findChild<QToolButton*>(QStringLiteral("layerLockTransparentButton"));
  CHECK(lock != nullptr);

  canvas->set_primary_color(QColor(220, 20, 40));
  use_solid_fill_settings(canvas);
  lock->click();
  QApplication::processEvents();
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(30, 30)), QColor(255, 255, 255), 8));

  lock->click();
  QApplication::processEvents();
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(30, 30)), QColor(220, 20, 40), 8));

  canvas->set_primary_color(QColor(20, 90, 220));
  lock->click();
  QApplication::processEvents();
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(30, 30)), QColor(20, 90, 220), 8));

  require_action(window, "layerClearAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(30, 30)), QColor(20, 90, 220), 8));
  save_widget_artifact("ui_layer_lock_transparency", window);

  canvas->setFocus();
  send_key(*canvas, Qt::Key_Right, Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(5, 30)), QColor(255, 255, 255), 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(20, 30)), QColor(20, 90, 220), 8));
  save_widget_artifact("ui_keyboard_nudge_layer", window);
}

void ui_layer_full_lock_row_control_blocks_edits_and_move() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  canvas->set_primary_color(QColor(220, 30, 40));
  use_solid_fill_settings(canvas);
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(30, 30)), QColor(220, 30, 40), 8));

  auto* paint_item = require_layer_item(*layer_list, QStringLiteral("Paint Layer"));
  auto* paint_row = layer_list->itemWidget(paint_item);
  CHECK(paint_row != nullptr);
  auto* lock_all = window.findChild<QToolButton*>(QStringLiteral("layerLockAllButton"));
  CHECK(lock_all != nullptr);
  CHECK(!lock_all->isChecked());
  lock_all->click();
  QApplication::processEvents();
  CHECK(lock_all->isChecked());

  paint_item = require_layer_item(*layer_list, QStringLiteral("Paint Layer"));
  paint_row = layer_list->itemWidget(paint_item);
  CHECK(paint_row != nullptr);
  const auto badges = paint_row->findChildren<QLabel*>(QStringLiteral("layerLockBadge"));
  CHECK(badges.size() == 3);

  canvas->set_primary_color(QColor(20, 90, 220));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(30, 30)), QColor(220, 30, 40), 8));
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("pixels are locked")));

  const auto before = canvas->active_layer_document_rect();
  CHECK(before.has_value());
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_auto_select_layer(false);
  canvas->set_show_transform_controls(false);
  const auto start = canvas->widget_position_for_document_point(QPoint(30, 30));
  send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, start + QPoint(40, 0), Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, start + QPoint(40, 0), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(canvas->active_layer_document_rect() == before);

  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  save_widget_artifact("ui_layer_full_lock_controls", window);
}

void ui_folder_lock_inherits_to_child_layers() {
  patchy::Document document(80, 80, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(80, 80, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::Layer folder(document.allocate_layer_id(), "Folder", patchy::LayerKind::Group);
  auto child_pixels = solid_pixels(20, 20, patchy::PixelFormat::rgba8(), QColor(230, 40, 40, 255));
  patchy::Layer child(document.allocate_layer_id(), "Child", std::move(child_pixels));
  const auto child_id = child.id();
  child.set_bounds(patchy::Rect{10, 10, 20, 20});
  folder.add_child(std::move(child));
  document.add_layer(std::move(folder));
  document.set_active_layer(child_id);

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Folder Lock"));
  QApplication::processEvents();

  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* folder_item = require_layer_item(*layer_list, QStringLiteral("Folder"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(folder_item);
  folder_item->setSelected(true);
  QApplication::processEvents();
  auto* folder_row = layer_list->itemWidget(folder_item);
  CHECK(folder_row != nullptr);
  auto* lock_all = window.findChild<QToolButton*>(QStringLiteral("layerLockAllButton"));
  CHECK(lock_all != nullptr);
  lock_all->click();
  QApplication::processEvents();

  auto* child_item = require_layer_item(*layer_list, QStringLiteral("Child"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(child_item);
  child_item->setSelected(true);
  QApplication::processEvents();
  auto* child_row = layer_list->itemWidget(child_item);
  CHECK(child_row != nullptr);
  const auto child_badges = child_row->findChildren<QLabel*>(QStringLiteral("layerLockBadge"));
  CHECK(child_badges.size() == 3);
  CHECK(std::all_of(child_badges.begin(), child_badges.end(), [](QLabel* badge) {
    return badge != nullptr && badge->property("inherited").toBool() &&
           badge->toolTip().contains(QStringLiteral("folder"));
  }));

  canvas->set_primary_color(QColor(20, 80, 220));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(15, 15)), QColor(230, 40, 40), 8));
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("pixels are locked")));
  save_widget_artifact("ui_folder_lock_inheritance", window);
}

void ui_move_auto_select_ignores_locked_layers() {
  patchy::Document document(80, 80, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(80, 80, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  auto bottom_pixels = solid_pixels(24, 24, patchy::PixelFormat::rgba8(), QColor(30, 90, 220, 255));
  patchy::Layer bottom(document.allocate_layer_id(), "Unlocked", std::move(bottom_pixels));
  bottom.set_bounds(patchy::Rect{16, 16, 24, 24});
  document.add_layer(std::move(bottom));
  auto top_pixels = solid_pixels(24, 24, patchy::PixelFormat::rgba8(), QColor(230, 40, 40, 255));
  patchy::Layer top(document.allocate_layer_id(), "Locked", std::move(top_pixels));
  top.set_bounds(patchy::Rect{16, 16, 24, 24});
  patchy::set_layer_locks_position(top, true);
  document.add_layer(std::move(top));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Locked Auto Select"));
  QApplication::processEvents();

  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_auto_select_layer(true);
  canvas->set_show_transform_controls(false);
  const auto click = canvas->widget_position_for_document_point(QPoint(20, 20));
  send_mouse(*canvas, QEvent::MouseButtonPress, click, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, click, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  auto* unlocked_item = require_layer_item(*layer_list, QStringLiteral("Unlocked"));
  CHECK(unlocked_item->isSelected());
  CHECK(layer_list->currentItem() == unlocked_item);
  save_widget_artifact("ui_move_auto_select_locked_layer", window);
}

void ui_lasso_selection_draws_freeform_region() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Lasso);

  const auto a = canvas->widget_position_for_document_point(QPoint(40, 40));
  const auto b = canvas->widget_position_for_document_point(QPoint(115, 42));
  const auto c = canvas->widget_position_for_document_point(QPoint(96, 105));
  const auto d = canvas->widget_position_for_document_point(QPoint(48, 112));
  send_mouse(*canvas, QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, b, Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, c, Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, d, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(canvas->selected_document_region().contains(QPoint(70, 70)));
  CHECK(!canvas->selected_document_region().contains(QPoint(25, 25)));
  save_widget_artifact("ui_lasso_selection", *canvas);
}

void ui_lasso_click_deselects() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Lasso);

  const auto a = canvas->widget_position_for_document_point(QPoint(40, 40));
  const auto b = canvas->widget_position_for_document_point(QPoint(115, 42));
  const auto c = canvas->widget_position_for_document_point(QPoint(96, 105));
  const auto d = canvas->widget_position_for_document_point(QPoint(48, 112));
  send_mouse(*canvas, QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, b, Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, c, Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, d, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(canvas->has_selection());

  // A plain click (no drag) inside the canvas deselects in Replace mode.
  const auto inside = canvas->widget_position_for_document_point(QPoint(70, 70));
  send_mouse(*canvas, QEvent::MouseButtonPress, inside, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, inside, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(!canvas->has_selection());

  // Re-select, then verify a plain click in the grey area also deselects.
  send_mouse(*canvas, QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, b, Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, c, Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, d, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(canvas->has_selection());

  const auto grey = canvas->widget_position_for_document_point(QPoint(-40, -40));
  CHECK(canvas->rect().contains(grey));
  send_mouse(*canvas, QEvent::MouseButtonPress, grey, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, grey, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(!canvas->has_selection());
}

void ui_marquee_drag_moves_selection() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(50, 50)),
       canvas->widget_position_for_document_point(QPoint(130, 130)));
  const auto before = canvas->selected_document_rect();
  CHECK(before.has_value());
  CHECK(canvas->selected_document_region().contains(QPoint(60, 60)));

  // Grab inside the selection and drag it down-right: the outline moves and the
  // size is preserved.
  const auto grab = canvas->widget_position_for_document_point(before->center());
  const auto drop = canvas->widget_position_for_document_point(before->center() + QPoint(40, 30));
  drag(*canvas, grab, drop);

  const auto after = canvas->selected_document_rect();
  CHECK(after.has_value());
  CHECK(after->width() == before->width());
  CHECK(after->height() == before->height());
  CHECK(after->left() >= before->left() + 39);
  CHECK(after->left() <= before->left() + 41);
  CHECK(after->top() >= before->top() + 29);
  CHECK(after->top() <= before->top() + 31);
  // The area it moved off is no longer selected; the new area is.
  CHECK(!canvas->selected_document_region().contains(QPoint(60, 60)));
  CHECK(canvas->selected_document_region().contains(after->center()));

  // A plain click inside the selection (no drag) still deselects.
  const auto click = canvas->widget_position_for_document_point(after->center());
  send_mouse(*canvas, QEvent::MouseButtonPress, click, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, click, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(!canvas->has_selection());
}

void ui_lasso_drag_moves_selection() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Lasso);

  send_mouse(*canvas, QEvent::MouseButtonPress, canvas->widget_position_for_document_point(QPoint(50, 50)),
             Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, canvas->widget_position_for_document_point(QPoint(120, 50)),
             Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, canvas->widget_position_for_document_point(QPoint(120, 120)),
             Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, canvas->widget_position_for_document_point(QPoint(50, 120)),
             Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  const auto before = canvas->selected_document_rect();
  CHECK(before.has_value());

  // Grabbing inside the lasso selection drags the outline (does not start a new lasso).
  const auto grab = canvas->widget_position_for_document_point(before->center());
  const auto drop = canvas->widget_position_for_document_point(before->center() + QPoint(35, 25));
  drag(*canvas, grab, drop);

  const auto after = canvas->selected_document_rect();
  CHECK(after.has_value());
  CHECK(after->width() == before->width());
  CHECK(after->height() == before->height());
  CHECK(after->left() >= before->left() + 34);
  CHECK(after->left() <= before->left() + 36);
}

void ui_selection_arrow_keys_nudge() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(50, 50)),
       canvas->widget_position_for_document_point(QPoint(120, 110)));
  const auto before = canvas->selected_document_rect();
  CHECK(before.has_value());

  send_key(*canvas, Qt::Key_Right);
  send_key(*canvas, Qt::Key_Down);
  auto after = canvas->selected_document_rect();
  CHECK(after.has_value());
  CHECK(after->left() == before->left() + 1);
  CHECK(after->top() == before->top() + 1);
  CHECK(after->width() == before->width());
  CHECK(after->height() == before->height());

  // Shift nudges by 10px.
  send_key(*canvas, Qt::Key_Right, Qt::ShiftModifier);
  after = canvas->selected_document_rect();
  CHECK(after->left() == before->left() + 11);

  // Holding an arrow (auto-repeat key presses) keeps nudging the selection.
  const auto left_before_hold = canvas->selected_document_rect()->left();
  for (int i = 0; i < 3; ++i) {
    QKeyEvent autorep(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier, QString(), true);
    QApplication::sendEvent(canvas, &autorep);
    QApplication::processEvents();
  }
  CHECK(canvas->selected_document_rect()->left() == left_before_hold + 3);

  // With the Move tool active, arrow keys move the layer, not the selection.
  canvas->set_tool(patchy::ui::CanvasTool::Move);
  const auto sel_before = canvas->selected_document_rect();
  send_key(*canvas, Qt::Key_Right);
  const auto sel_after = canvas->selected_document_rect();
  CHECK(sel_after.has_value());
  CHECK(sel_after->left() == sel_before->left());
  CHECK(sel_after->top() == sel_before->top());
}

void ui_selection_moves_coalesce_into_one_undo_step() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* undo_action = require_action_by_text(window, QStringLiteral("Undo"));
  auto* redo_action = require_action_by_text(window, QStringLiteral("Redo"));
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(50, 50)),
       canvas->widget_position_for_document_point(QPoint(120, 110)));
  CHECK(canvas->has_selection());
  const auto origin = canvas->selected_document_rect()->topLeft();

  // A run of nudges (including key auto-repeat) moves the selection but collapses
  // into a single undo step.
  send_key(*canvas, Qt::Key_Right);
  send_key(*canvas, Qt::Key_Right);
  send_key(*canvas, Qt::Key_Down);
  for (int i = 0; i < 3; ++i) {
    QKeyEvent autorep(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier, QString(), true);
    QApplication::sendEvent(canvas, &autorep);
    QApplication::processEvents();
  }
  CHECK(canvas->selected_document_rect()->topLeft() == origin + QPoint(5, 1));

  // One undo returns to the pre-move position (not just one nudge back); one redo
  // restores the final moved position.
  undo_action->trigger();
  QApplication::processEvents();
  CHECK(canvas->selected_document_rect()->topLeft() == origin);
  redo_action->trigger();
  QApplication::processEvents();
  CHECK(canvas->selected_document_rect()->topLeft() == origin + QPoint(5, 1));

  // The whole run is one entry sitting on top of the marquee: undoing twice
  // removes the move, then the selection itself.
  undo_action->trigger();
  QApplication::processEvents();
  CHECK(canvas->selected_document_rect()->topLeft() == origin);
  undo_action->trigger();
  QApplication::processEvents();
  CHECK(!canvas->has_selection());
}

void ui_selection_cursor_shows_combine_mode_badge() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  canvas->setFocus(Qt::OtherFocusReason);
  QApplication::processEvents();

  // The marquee uses the same drawn crosshair (BitmapCursor) in every mode, so
  // toggling a modifier never shifts or recolours it; Replace just omits the
  // badge.
  send_mouse(*canvas, QEvent::MouseMove, canvas->widget_position_for_document_point(QPoint(40, 40)),
             Qt::NoButton, Qt::NoButton);
  CHECK(canvas->cursor().shape() == Qt::BitmapCursor);
  CHECK(canvas->cursor().hotSpot() == QPoint(10, 10));
  const auto replace_image = canvas->cursor().pixmap().toImage();
  CHECK(!replace_image.isNull());

  // The toolbar combine mode badges the crosshair with no modifier held.
  // (Checked before any synthetic key events, which would leave the global
  // modifier state dirty for set_selection_mode's lookup.)
  canvas->set_selection_mode(patchy::ui::CanvasWidget::SelectionMode::Intersect);
  CHECK(canvas->cursor().hotSpot() == QPoint(10, 10));
  const auto intersect_image = canvas->cursor().pixmap().toImage();
  CHECK(intersect_image != replace_image);
  canvas->set_selection_mode(patchy::ui::CanvasWidget::SelectionMode::Replace);
  CHECK(canvas->cursor().pixmap().toImage() == replace_image);

  // With no active selection there is nothing to add to / subtract from, so
  // Shift and Alt do NOT switch the combine mode (they act as geometry
  // constraints instead). The badge therefore stays on the tool's own mode.
  send_key_press(window, Qt::Key_Shift, Qt::NoModifier);
  CHECK(canvas->cursor().pixmap().toImage() == replace_image);
  send_key_release(window, Qt::Key_Shift, Qt::ShiftModifier);
  send_key_press(window, Qt::Key_Alt, Qt::NoModifier);
  CHECK(canvas->cursor().pixmap().toImage() == replace_image);
  send_key_release(window, Qt::Key_Alt, Qt::AltModifier);

  // Once there is a selection to combine with, the same modifiers badge the
  // crosshair. The app-level event filter catches a Shift press delivered to the
  // window even with the pointer stationary. (The press event reports no
  // modifier yet, so the cursor logic folds the pressed key in.)
  canvas->select_all();
  QApplication::processEvents();
  send_key_press(window, Qt::Key_Shift, Qt::NoModifier);
  const auto add_image = canvas->cursor().pixmap().toImage();
  CHECK(add_image != replace_image);
  CHECK(add_image != intersect_image);

  // The release event still reports Shift; the handler must clear it to return
  // to the plain crosshair.
  send_key_release(window, Qt::Key_Shift, Qt::ShiftModifier);
  CHECK(canvas->cursor().pixmap().toImage() == replace_image);

  // Alt shows a distinct "-" badge.
  send_key_press(window, Qt::Key_Alt, Qt::NoModifier);
  const auto subtract_image = canvas->cursor().pixmap().toImage();
  CHECK(subtract_image != replace_image);
  CHECK(subtract_image != add_image);
  CHECK(subtract_image != intersect_image);
  send_key_release(window, Qt::Key_Alt, Qt::AltModifier);
}

void ui_brush_alt_shows_eyedropper_cursor() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Brush);
  canvas->set_brush_size(1);  // small footprint: a real OS cursor, not the overlay path
  canvas->setFocus(Qt::OtherFocusReason);
  QApplication::processEvents();

  // Establish the normal brush cursor through the Alt handler: an Alt-up with no
  // Alt held rebuilds the tool cursor from the folded modifier state, so this is
  // independent of any stale global keyboard-modifier state a prior test may have
  // left behind (the offscreen platform never clears a synthetic Alt press).
  send_key_release(window, Qt::Key_Alt, Qt::AltModifier);
  const auto brush_hotspot = canvas->cursor().hotSpot();
  const auto brush_image = canvas->cursor().pixmap().toImage();
  CHECK(!brush_image.isNull());
  CHECK(brush_hotspot != QPoint(5, 27));

  // Holding Alt turns the brush into a temporary colour picker; the cursor swaps
  // to the eyedropper immediately (pointer stationary), with its hotspot on the
  // lower-left sampling tip.
  send_key_press(window, Qt::Key_Alt, Qt::NoModifier);
  CHECK(canvas->cursor().hotSpot() == QPoint(5, 27));
  const auto eyedropper_image = canvas->cursor().pixmap().toImage();
  CHECK(!eyedropper_image.isNull());
  CHECK(eyedropper_image != brush_image);

  // Releasing Alt restores the exact brush cursor. The release event carries the
  // authoritative modifier state (Alt cleared) folded in by the event filter, so
  // the swap-back does not depend on the global keyboard-modifier state being
  // refreshed yet.
  send_key_release(window, Qt::Key_Alt, Qt::AltModifier);
  CHECK(canvas->cursor().hotSpot() == brush_hotspot);
  CHECK(canvas->cursor().pixmap().toImage() == brush_image);

  // The standalone Eyedropper tool uses the same cursor with no modifier held.
  canvas->set_tool(patchy::ui::CanvasTool::Eyedropper);
  send_mouse(*canvas, QEvent::MouseMove, canvas->widget_position_for_document_point(QPoint(41, 41)),
             Qt::NoButton, Qt::NoButton);
  CHECK(canvas->cursor().hotSpot() == QPoint(5, 27));
  CHECK(canvas->cursor().pixmap().toImage() == eyedropper_image);
}

}  // namespace

std::vector<patchy::test::TestCase> selection_marquee_lasso_tests() {
  return {
      {"ui_marquee_selection_modifiers_work", ui_marquee_selection_modifiers_work},
      {"ui_marquee_click_outside_canvas_deselects", ui_marquee_click_outside_canvas_deselects},
      {"ui_marquee_shift_drag_constrains_to_square", ui_marquee_shift_drag_constrains_to_square},
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
      {"ui_shape_fill_and_corner_radius_apply_to_new_documents",
       ui_shape_fill_and_corner_radius_apply_to_new_documents},
      {"ui_shape_tool_fixed_size_and_ratio_options_work", ui_shape_tool_fixed_size_and_ratio_options_work},
      {"ui_elliptical_marquee_selects_oval_region", ui_elliptical_marquee_selects_oval_region},
      {"ui_marquee_space_drag_repositions_active_rect", ui_marquee_space_drag_repositions_active_rect},
      {"ui_info_panel_shows_selection_rect", ui_info_panel_shows_selection_rect},
      {"ui_rulers_grid_guides_render_and_edit", ui_rulers_grid_guides_render_and_edit},
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
      {"ui_complex_selection_draws_region_outline", ui_complex_selection_draws_region_outline},
      {"ui_ctrl_h_hides_selection_edges_without_blue_tint", ui_ctrl_h_hides_selection_edges_without_blue_tint},
      {"ui_select_inverse_and_extended_blend_modes_work", ui_select_inverse_and_extended_blend_modes_work},
      {"ui_selection_expand_contract_and_layer_transparency_work",
       ui_selection_expand_contract_and_layer_transparency_work},
      {"ui_ctrl_click_layer_loads_layer_transparency", ui_ctrl_click_layer_loads_layer_transparency},
      {"ui_ctrl_click_layer_and_mask_preserve_soft_coverage",
       ui_ctrl_click_layer_and_mask_preserve_soft_coverage},
      {"ui_deko_layer_transparency_selection_and_channel_save_if_available",
       ui_deko_layer_transparency_selection_and_channel_save_if_available},
      {"ui_select_grow_and_similar_use_magic_wand_tolerance",
       ui_select_grow_and_similar_use_magic_wand_tolerance},
      {"ui_complex_selection_stroke_uses_region_outline", ui_complex_selection_stroke_uses_region_outline},
      {"ui_layer_lock_transparency_and_keyboard_nudge_work", ui_layer_lock_transparency_and_keyboard_nudge_work},
      {"ui_layer_full_lock_row_control_blocks_edits_and_move",
       ui_layer_full_lock_row_control_blocks_edits_and_move},
      {"ui_folder_lock_inherits_to_child_layers", ui_folder_lock_inherits_to_child_layers},
      {"ui_move_auto_select_ignores_locked_layers", ui_move_auto_select_ignores_locked_layers},
      {"ui_lasso_selection_draws_freeform_region", ui_lasso_selection_draws_freeform_region},
      {"ui_lasso_click_deselects", ui_lasso_click_deselects},
      {"ui_marquee_drag_moves_selection", ui_marquee_drag_moves_selection},
      {"ui_lasso_drag_moves_selection", ui_lasso_drag_moves_selection},
      {"ui_selection_arrow_keys_nudge", ui_selection_arrow_keys_nudge},
      {"ui_selection_moves_coalesce_into_one_undo_step", ui_selection_moves_coalesce_into_one_undo_step},
      {"ui_selection_cursor_shows_combine_mode_badge", ui_selection_cursor_shows_combine_mode_badge},
      {"ui_brush_alt_shows_eyedropper_cursor", ui_brush_alt_shows_eyedropper_cursor},
  };
}
