// GitHub #555: point and segment commands (Break Path at Point, Join End Points, Merge into One
// Point, Delete Segment) from Layer > Shape and the path context menu, and the component commands:
// Edit > Cut/Copy/Paste of selected shapes, Color Selected Shapes, Move/Copy Selected Shapes to
// New Layer (docs/vector-commands.md).
#include "ui/canvas_widget.hpp"
#include "ui/main_window.hpp"
#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_support.hpp"
#include "core/vector_compound.hpp"
#include "core/vector_shape.hpp"
#include "psd/psd_document_io.hpp"

#include <QAction>
#include <QApplication>
#include <QColor>
#include <QListWidget>
#include <QMenu>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <tuple>
#include <string>
#include <utility>
#include <vector>

using namespace patchy::test::ui;

namespace {

void shape_drag(patchy::ui::CanvasWidget& canvas, QPoint document_from, QPoint document_to) {
  drag(canvas, canvas.widget_position_for_document_point(document_from),
       canvas.widget_position_for_document_point(document_to));
  QApplication::processEvents();
}

void click(patchy::ui::CanvasWidget& canvas, QPoint document_point) {
  shape_drag(canvas, document_point, document_point);
}

patchy::LayerId make_rect_shape_layer(patchy::ui::MainWindow& window, patchy::ui::CanvasWidget& canvas, QPoint from,
                                      QPoint to) {
  canvas.set_tool(patchy::ui::CanvasTool::Rectangle);
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  CHECK(radius_spin != nullptr);
  radius_spin->setValue(0);
  shape_drag(canvas, from, to);
  const auto active = patchy::ui::MainWindowTestAccess::document(window).active_layer_id();
  CHECK(active.has_value());
  return *active;
}

const patchy::VectorShapeContent& shape_of(patchy::Document& document, patchy::LayerId id) {
  const auto* layer = std::as_const(document).find_layer(id);
  CHECK(layer != nullptr && layer->vector_shape() != nullptr);
  return *layer->vector_shape();
}

patchy::PathSubpath square(double left, double top, double size, int group,
                           patchy::PathCombineOp op = patchy::PathCombineOp::Add) {
  patchy::PathSubpath subpath;
  subpath.shape_group = group;
  subpath.op = op;
  for (const auto& [x, y] : std::vector<std::pair<double, double>>{
           {left, top}, {left + size, top}, {left + size, top + size}, {left, top + size}}) {
    patchy::PathAnchor anchor;
    anchor.anchor_x = anchor.in_x = anchor.out_x = x;
    anchor.anchor_y = anchor.in_y = anchor.out_y = y;
    subpath.anchors.push_back(anchor);
  }
  return subpath;
}

// Writes `path` into the target through the canvas's edit route (a shape layer re-renders).
void set_target_path(patchy::ui::CanvasWidget& canvas, patchy::VectorPath path) {
  std::vector<int> groups;
  for (const auto& subpath : path.subpaths) {
    groups.push_back(subpath.shape_group);
  }
  canvas.replace_path_edit_target(std::move(path), groups);
  QApplication::processEvents();
}

// The layer's rendered RGBA at a document point (all zero outside it).
std::array<int, 4> layer_rgba_at(patchy::Document& document, patchy::LayerId id, QPoint point) {
  const auto* layer = std::as_const(document).find_layer(id);
  CHECK(layer != nullptr);
  const auto bounds = layer->bounds();
  const int x = point.x() - bounds.x;
  const int y = point.y() - bounds.y;
  const auto& pixels = layer->pixels();
  if (x < 0 || y < 0 || x >= pixels.width() || y >= pixels.height() || pixels.format().channels < 4) {
    return {0, 0, 0, 0};
  }
  const auto* px = pixels.pixel(x, y);
  return {px[0], px[1], px[2], px[3]};
}

// Runs `act` on the canvas path menu once it opens (menu.exec blocks until it closes).
template <typename Act>
void with_path_menu_at(patchy::ui::CanvasWidget& canvas, QPoint document_point, Act act) {
  bool opened = false;
  QTimer::singleShot(0, [act, &opened] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* menu = qobject_cast<QMenu*>(widget);
      if (menu == nullptr || menu->objectName() != QStringLiteral("canvasPathContextMenu")) {
        continue;
      }
      opened = true;
      act(*menu);
      menu->close();
      return;
    }
  });
  const auto widget_point = canvas.widget_position_for_document_point(document_point);
  CHECK(canvas.show_path_context_menu(QPointF(widget_point), canvas.mapToGlobal(widget_point)));
  QApplication::processEvents();
  CHECK(opened);
}

QAction* menu_action(QMenu& menu, const char* object_name) {
  for (auto* action : menu.actions()) {
    if (action->objectName() == QLatin1String(object_name)) {
      return action;
    }
  }
  CHECK(false);
  return nullptr;
}

std::size_t undo_depth(patchy::ui::MainWindow& window) {
  return patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
}

// A closed rectangle: Delete Segment opens it, Break Path at Point (menu) opens it at a corner,
// Join End Points closes it again, Merge into One Point (menu, over a segment) collapses an edge.
// Each is one undo step, and the open path survives a PSD round trip.
void ui_path_point_commands_edit_menus_and_undo() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  canvas->set_view_zoom_centered(1.0);
  const auto id = make_rect_shape_layer(window, *canvas, QPoint(100, 100), QPoint(300, 220));
  // An outline only, so the open edge shows.
  {
    auto content = shape_of(document, id);
    content.fill.kind = patchy::VectorFillKind::None;
    content.stroke.enabled = true;
    content.stroke.width = 8.0;
    content.stroke.alignment = patchy::VectorStrokeAlignment::Center;
    content.stroke.content.kind = patchy::VectorFillKind::Solid;
    content.stroke.content.color = patchy::RgbColor{30, 90, 200};
    document.find_layer(id)->set_vector_shape(content);
    set_target_path(*canvas, content.path);
  }
  canvas->set_tool(patchy::ui::CanvasTool::DirectSelect);
  QApplication::processEvents();
  auto* break_action = require_action(window, "pathBreakAtPointAction");
  auto* join_action = require_action(window, "pathJoinEndPointsAction");
  auto* merge_action = require_action(window, "pathMergePointsAction");
  auto* delete_action = require_action(window, "pathDeleteSegmentAction");
  CHECK(!break_action->isEnabled() && !join_action->isEnabled() && !delete_action->isEnabled());

  // A segment click selects its two points: the segment commands apply, Join does not.
  click(*canvas, QPoint(200, 100));
  CHECK(canvas->path_edit_selected_anchor_count() == 2);
  CHECK(delete_action->isEnabled() && merge_action->isEnabled() && break_action->isEnabled());
  CHECK(!join_action->isEnabled());
  save_widget_artifact("ui_i555_delete_segment_before", *canvas);
  auto undo_before = undo_depth(window);
  delete_action->trigger();
  QApplication::processEvents();
  CHECK(shape_of(document, id).path.subpaths.size() == 1);
  CHECK(!shape_of(document, id).path.subpaths[0].closed);
  CHECK(shape_of(document, id).path.subpaths[0].anchors.size() == 4);
  CHECK(undo_depth(window) == undo_before + 1);
  // The top edge's stroke is gone; the left edge's stays.
  CHECK(layer_rgba_at(document, id, QPoint(200, 100))[3] == 0);
  CHECK(layer_rgba_at(document, id, QPoint(100, 160))[3] > 200);
  save_widget_artifact("ui_i555_delete_segment_after", *canvas);
  // PSD keeps the open path.
  const auto reread = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document));
  const patchy::Layer* reread_layer = nullptr;
  for (const auto& layer : reread.layers()) {
    if (patchy::layer_is_vector_shape(layer)) {
      reread_layer = &layer;
    }
  }
  CHECK(reread_layer != nullptr && reread_layer->vector_shape()->path.subpaths.size() == 1);
  CHECK(!reread_layer->vector_shape()->path.subpaths[0].closed);
  CHECK(reread_layer->vector_shape()->path.subpaths[0].anchors.size() == 4);
  patchy::ui::MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(shape_of(document, id).path.subpaths[0].closed);

  // The menu lists the four commands; over a corner Break applies.
  with_path_menu_at(*canvas, QPoint(100, 100), [&](QMenu& menu) {
    CHECK(menu_action(menu, "pathMenuBreakAtPointAction")->isEnabled());
    CHECK(!menu_action(menu, "pathMenuJoinEndPointsAction")->isEnabled());
    CHECK(menu_action(menu, "pathMenuMergePointsAction") != nullptr);
    CHECK(menu_action(menu, "pathMenuDeleteSegmentAction") != nullptr);
    save_widget_artifact("ui_i555_path_menu", menu);
    menu_action(menu, "pathMenuBreakAtPointAction")->trigger();
  });
  const auto& opened = shape_of(document, id).path.subpaths[0];
  CHECK(!opened.closed && opened.anchors.size() == 5);
  // Both new end points are selected, so Join closes the path again (they coincide: one point).
  CHECK(canvas->path_edit_selected_anchor_count() == 2);
  CHECK(join_action->isEnabled());
  undo_before = undo_depth(window);
  join_action->trigger();
  QApplication::processEvents();
  CHECK(shape_of(document, id).path.subpaths[0].closed);
  CHECK(shape_of(document, id).path.subpaths[0].anchors.size() == 4);
  CHECK(undo_depth(window) == undo_before + 1);

  // Merge over the right edge collapses it into one point at its middle.
  with_path_menu_at(*canvas, QPoint(300, 160), [&](QMenu& menu) {
    auto* merge = menu_action(menu, "pathMenuMergePointsAction");
    CHECK(merge->isEnabled());
    merge->trigger();
  });
  const auto& merged = shape_of(document, id).path.subpaths[0];
  CHECK(merged.closed && merged.anchors.size() == 3);
  CHECK(std::any_of(merged.anchors.begin(), merged.anchors.end(), [](const patchy::PathAnchor& anchor) {
    return std::abs(anchor.anchor_x - 300.0) < 0.5 && std::abs(anchor.anchor_y - 160.0) < 0.5;
  }));
}

// Two open lines: a marquee over their near ends, then Join connects them into one path.
void ui_path_join_end_points_connects_two_open_paths() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  canvas->set_view_zoom_centered(1.0);
  const auto id = make_rect_shape_layer(window, *canvas, QPoint(100, 100), QPoint(300, 220));
  patchy::VectorPath lines;
  for (const auto& [x0, x1, group] : std::vector<std::tuple<double, double, int>>{{100, 200, 0}, {220, 320, 1}}) {
    patchy::PathSubpath line;
    line.closed = false;
    line.shape_group = group;
    for (const double x : {x0, x1}) {
      patchy::PathAnchor anchor;
      anchor.anchor_x = anchor.in_x = anchor.out_x = x;
      anchor.anchor_y = anchor.in_y = anchor.out_y = 300.0;
      line.anchors.push_back(anchor);
    }
    lines.subpaths.push_back(line);
  }
  set_target_path(*canvas, lines);
  canvas->set_tool(patchy::ui::CanvasTool::DirectSelect);
  shape_drag(*canvas, QPoint(190, 280), QPoint(230, 320));
  CHECK(canvas->path_edit_selected_anchor_count() == 2);
  auto* join_action = require_action(window, "pathJoinEndPointsAction");
  CHECK(join_action->isEnabled());
  join_action->trigger();
  QApplication::processEvents();
  CHECK(shape_of(document, id).path.subpaths.size() == 1);
  CHECK(shape_of(document, id).path.subpaths[0].anchors.size() == 4);
  patchy::ui::MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(shape_of(document, id).path.subpaths.size() == 2);
  // Merge into One Point makes the two ends one point at (210, 300).
  shape_drag(*canvas, QPoint(190, 280), QPoint(230, 320));
  require_action(window, "pathMergePointsAction")->trigger();
  QApplication::processEvents();
  const auto& one = shape_of(document, id).path.subpaths;
  CHECK(one.size() == 1 && one[0].anchors.size() == 3);
  CHECK(std::abs(one[0].anchors[1].anchor_x - 210.0) < 1e-6);
}

// Path Select picks the donut's hole; Edit > Copy and Paste put it into another shape layer with
// its Subtract op; Edit > Cut takes it out again; Paste with no path target makes a new layer.
void ui_path_components_cut_copy_paste_round_trip() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  canvas->set_view_zoom_centered(1.0);
  const auto pixel_layer = *document.active_layer_id();
  const auto donut = make_rect_shape_layer(window, *canvas, QPoint(100, 100), QPoint(300, 300));
  patchy::VectorPath path;
  path.subpaths = {square(100, 100, 200, 0), square(150, 150, 100, 1, patchy::PathCombineOp::Subtract)};
  set_target_path(*canvas, path);
  const auto other = make_rect_shape_layer(window, *canvas, QPoint(400, 100), QPoint(600, 300));

  patchy::ui::MainWindowTestAccess::select_layers(window, {donut}, donut);
  QApplication::processEvents();
  canvas->set_tool(patchy::ui::CanvasTool::PathSelect);
  click(*canvas, QPoint(150, 200));  // the hole's left edge
  CHECK((canvas->path_edit_selected_groups() == std::vector<int>{1}));
  require_action(window, "editCopyAction")->trigger();
  QApplication::processEvents();
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Copied 1 shape(s)"));
  CHECK(shape_of(document, donut).path.subpaths.size() == 2);  // a copy leaves the source

  patchy::ui::MainWindowTestAccess::select_layers(window, {other}, other);
  QApplication::processEvents();
  auto undo_before = undo_depth(window);
  require_action(window, "editPasteAction")->trigger();
  QApplication::processEvents();
  const auto& pasted = shape_of(document, other).path.subpaths;
  CHECK(pasted.size() == 2);
  CHECK(pasted[1].op == patchy::PathCombineOp::Subtract && pasted[1].shape_group != pasted[0].shape_group);
  CHECK(std::abs(pasted[1].anchors[0].anchor_x - 150.0) < 1e-6);  // in place
  CHECK(undo_depth(window) == undo_before + 1);
  CHECK(canvas->path_edit_selected_groups().size() == 1);  // the pasted shape is selected
  CHECK(layer_rgba_at(document, other, QPoint(200, 200))[3] == 0);  // it cuts nothing there
  CHECK(layer_rgba_at(document, other, QPoint(500, 200))[3] > 200);

  // Cut takes the selected shape out again, one undo step.
  undo_before = undo_depth(window);
  require_action(window, "editCutAction")->trigger();
  QApplication::processEvents();
  CHECK(shape_of(document, other).path.subpaths.size() == 1);
  CHECK(undo_depth(window) == undo_before + 1);

  // With a pixel layer active there is no path target: Paste makes a new shape layer, and the
  // lone hole paints its own square there instead of the canvas around it.
  patchy::ui::MainWindowTestAccess::select_layers(window, {pixel_layer}, pixel_layer);
  canvas->set_tool(patchy::ui::CanvasTool::Move);
  QApplication::processEvents();
  const auto layers_before = document.layers().size();
  require_action(window, "editPasteAction")->trigger();
  QApplication::processEvents();
  CHECK(document.layers().size() == layers_before + 1);
  const auto created = *document.active_layer_id();
  CHECK(created != pixel_layer && patchy::layer_is_vector_shape(*std::as_const(document).find_layer(created)));
  CHECK(shape_of(document, created).path.subpaths.size() == 1);
  CHECK(shape_of(document, created).path.subpaths[0].op == patchy::PathCombineOp::Add);
  CHECK(layer_rgba_at(document, created, QPoint(200, 200))[3] > 200);
  CHECK(layer_rgba_at(document, created, QPoint(120, 120))[3] == 0);
}

// Color Selected Shapes gives one of two squares its own paint part (it survives a PSD round
// trip); Move Selected Shapes to New Layer takes it to a new layer above, Copy (from the path
// menu) leaves the source whole.
void ui_path_components_color_and_move_to_new_layer() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  canvas->set_view_zoom_centered(1.0);
  const auto id = make_rect_shape_layer(window, *canvas, QPoint(100, 100), QPoint(200, 200));
  patchy::VectorPath path;
  path.subpaths = {square(100, 100, 100, 0), square(300, 100, 100, 1)};
  set_target_path(*canvas, path);
  {
    auto content = shape_of(document, id);
    content.fill.kind = patchy::VectorFillKind::Solid;
    content.fill.color = patchy::RgbColor{200, 30, 30};
    document.find_layer(id)->set_vector_shape(content);
    set_target_path(*canvas, content.path);
  }
  canvas->set_tool(patchy::ui::CanvasTool::PathSelect);
  click(*canvas, QPoint(300, 150));  // the right square's left edge
  CHECK((canvas->path_edit_selected_groups() == std::vector<int>{1}));
  CHECK(require_action(window, "pathShapesColorAction")->isEnabled());
  auto undo_before = undo_depth(window);
  CHECK(patchy::ui::MainWindowTestAccess::set_selected_path_components_color(window, QColor(20, 40, 220)));
  QApplication::processEvents();
  CHECK(undo_depth(window) == undo_before + 1);
  CHECK(shape_of(document, id).parts.size() == 2);
  const auto right = layer_rgba_at(document, id, QPoint(350, 150));
  const auto left = layer_rgba_at(document, id, QPoint(150, 150));
  CHECK(right[2] > 180 && right[0] < 60 && right[3] > 200);
  CHECK(left[0] > 180 && left[2] < 60 && left[3] > 200);
  CHECK(canvas->path_edit_selected_groups().size() == 1);  // the selection stays
  save_widget_artifact("ui_i555_shape_colors", *canvas);
  // PSD writes the two paints and reads one layer back.
  const auto reread = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document));
  bool found = false;
  for (const auto& layer : reread.layers()) {
    if (patchy::layer_is_vector_shape(layer) && layer.vector_shape()->parts.size() == 2) {
      found = true;
      CHECK(layer.vector_shape()->parts[1].fill.color == (patchy::RgbColor{20, 40, 220}));
    }
  }
  CHECK(found);

  // Copy Selected Shapes to New Layer from the path menu leaves the source whole.
  const auto layers_before = document.layers().size();
  with_path_menu_at(*canvas, QPoint(300, 150), [&](QMenu& menu) {
    CHECK(menu_action(menu, "pathShapesCutAction")->isEnabled());
    CHECK(menu_action(menu, "pathShapesColorAction")->isEnabled());
    auto* copy = menu_action(menu, "pathShapesCopyToNewLayerAction");
    CHECK(copy->isEnabled());
    save_widget_artifact("ui_i555_shapes_menu", menu);
    copy->trigger();
  });
  CHECK(document.layers().size() == layers_before + 1);
  CHECK(shape_of(document, id).path.subpaths.size() == 2);
  const auto copy_id = *document.active_layer_id();
  CHECK(shape_of(document, copy_id).path.subpaths.size() == 1);
  CHECK(layer_rgba_at(document, copy_id, QPoint(350, 150))[2] > 180);  // its blue paint came along
  patchy::ui::MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(document.layers().size() == layers_before);

  // Move takes it off the source, one undo step; the moved shape stays selected on its new layer.
  patchy::ui::MainWindowTestAccess::select_layers(window, {id}, id);
  QApplication::processEvents();
  click(*canvas, QPoint(300, 150));
  CHECK((canvas->path_edit_selected_groups() == std::vector<int>{1}));
  undo_before = undo_depth(window);
  require_action(window, "pathShapesToNewLayerAction")->trigger();
  QApplication::processEvents();
  CHECK(undo_depth(window) == undo_before + 1);
  CHECK(document.layers().size() == layers_before + 1);
  CHECK(shape_of(document, id).path.subpaths.size() == 1);
  CHECK(layer_rgba_at(document, id, QPoint(350, 150))[3] == 0);
  const auto moved = *document.active_layer_id();
  CHECK(moved != id && shape_of(document, moved).path.subpaths.size() == 1);
  CHECK(canvas->path_edit_selected_groups().size() == 1);
  patchy::ui::MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(document.layers().size() == layers_before);
  CHECK(shape_of(document, id).path.subpaths.size() == 2);
}

}  // namespace

std::vector<patchy::test::TestCase> vector_path_edits_tests() {
  return {
      {"ui_path_point_commands_edit_menus_and_undo", ui_path_point_commands_edit_menus_and_undo},
      {"ui_path_join_end_points_connects_two_open_paths", ui_path_join_end_points_connects_two_open_paths},
      {"ui_path_components_cut_copy_paste_round_trip", ui_path_components_cut_copy_paste_round_trip},
      {"ui_path_components_color_and_move_to_new_layer", ui_path_components_color_and_move_to_new_layer},
  };
}
