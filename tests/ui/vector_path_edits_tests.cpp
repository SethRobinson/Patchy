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
#include <QToolButton>

#include <algorithm>
#include <iostream>
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


// Seth's report (#555 follow-up): Direct Select several points of a filled shape, Break Path at
// Point, and the shape grew spikes. Drive it as a user does (Rectangle tool with the toolbar's
// appearance, a corner dragged into a quad, a marquee over points, Layer > Shape) and check the
// layer and the canvas at 100% and zoomed in.
void check_break_renders_cleanly(bool with_stroke, patchy::VectorStrokeAlignment alignment) {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  canvas->set_view_zoom_centered(1.0);
  const auto id = make_rect_shape_layer(window, *canvas, QPoint(100, 100), QPoint(300, 260));
  canvas->set_tool(patchy::ui::CanvasTool::DirectSelect);
  QApplication::processEvents();
  shape_drag(*canvas, QPoint(100, 100), QPoint(220, 60));  // the top-left corner up and right
  if (with_stroke) {
    // A thick black stroke with miter joins, outside the edge, as the toolbar sets it.
    auto content = shape_of(document, id);
    content.stroke.enabled = true;
    content.stroke.width = 8.0;
    content.stroke.alignment = alignment;
    content.stroke.join = patchy::VectorStrokeJoin::Miter;
    content.stroke.content.kind = patchy::VectorFillKind::Solid;
    content.stroke.content.color = patchy::RgbColor{0, 0, 0};
    document.find_layer(id)->set_vector_shape(content);
    set_target_path(*canvas, content.path);
  }
  const auto hull = shape_of(document, id).path;
  const auto bounds = hull.bounds();
  CHECK(bounds.has_value());
  int run = 0;
  int total_stray = 0;
  for (const auto& marquee : std::vector<std::pair<QPoint, QPoint>>{{QPoint(280, 40), QPoint(320, 280)},
                                                                    {QPoint(60, 20), QPoint(340, 290)},
                                                                    {QPoint(200, 40), QPoint(240, 80)}}) {
    if (run > 0) {
      patchy::ui::MainWindowTestAccess::undo(window);  // back to the closed quad
      QApplication::processEvents();
    }
    shape_drag(*canvas, marquee.first, marquee.second);
    CHECK(canvas->path_edit_selected_anchor_count() >= 1);
    auto* break_action = require_action(window, "pathBreakAtPointAction");
    CHECK(break_action->isEnabled());
    break_action->trigger();
    QApplication::processEvents();
    const auto* layer = std::as_const(document).find_layer(id);
    const auto layer_bounds = layer->bounds();
    std::cerr << "break stroke " << with_stroke << " align " << static_cast<int>(alignment) << " run " << run << ": subpaths " << layer->vector_shape()->path.subpaths.size()
              << " layer bounds " << layer_bounds.x << "," << layer_bounds.y << " " << layer_bounds.width << "x"
              << layer_bounds.height << " path " << bounds->left << "," << bounds->top << " - " << bounds->right
              << "," << bounds->bottom << "\n";
    // Every point broken leaves two-point pieces: open lines with no area, so a fill-only shape
    // shows nothing (the pieces still stroke).
    // A miter at the quad's acute corner reaches a few widths out; the spikes reached 100 px and more.
    const int slack = with_stroke ? 36 : 1;
    if (!layer_bounds.empty()) {
      CHECK(layer_bounds.x >= std::floor(bounds->left) - slack && layer_bounds.y >= std::floor(bounds->top) - slack);
      CHECK(layer_bounds.x + layer_bounds.width <= std::ceil(bounds->right) + slack);
      CHECK(layer_bounds.y + layer_bounds.height <= std::ceil(bounds->bottom) + slack);
    }
    for (const double zoom : {1.0, 3.0}) {
      canvas->set_view_zoom_centered(zoom);
      QApplication::processEvents();
      process_events_for(200);
      const auto image = canvas->grab().toImage();
      save_image_artifact("ui_i555_break_" + std::string(with_stroke ? "stroke" : "fill") +
                              std::to_string(static_cast<int>(alignment)) + "_run" + std::to_string(run) + "_zoom" +
                              std::to_string(static_cast<int>(zoom)),
                          image);
      // Dark pixels outside the shape's box (grown by the overlay glyphs) are artifacts.
      const auto top_left = canvas->widget_position_for_document_point(
          QPoint(static_cast<int>(std::floor(bounds->left)), static_cast<int>(std::floor(bounds->top))));
      const auto bottom_right = canvas->widget_position_for_document_point(
          QPoint(static_cast<int>(std::ceil(bounds->right)), static_cast<int>(std::ceil(bounds->bottom))));
      const int reach = 12 + (with_stroke ? static_cast<int>(std::ceil(36.0 * zoom)) : 0);
      const QRect allowed = QRect(top_left, bottom_right).adjusted(-reach, -reach, reach, reach);
      int stray = 0;
      for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
          const auto color = image.pixelColor(x, y);
          // Pure black only (the scroll bars and frame are dark gray), inside the viewport.
          if (!allowed.contains(x, y) && color.red() < 16 && color.green() < 16 && color.blue() < 16 &&
              x > 2 && y > 2 && x < image.width() - 24 && y < image.height() - 24) {
            const auto doc_point = canvas->document_point_for_widget_position(QPointF(x, y));
            if (doc_point.x() >= 0 && doc_point.y() >= 0 && doc_point.x() < document.width() &&
                doc_point.y() < document.height()) {
              ++stray;
            }
          }
        }
      }
      std::cerr << "  zoom " << zoom << " stray dark pixels " << stray << "\n";
      total_stray += stray;
    }
    canvas->set_view_zoom_centered(1.0);
    QApplication::processEvents();
    ++run;
  }
  CHECK(total_stray == 0);
}
void ui_path_break_several_points_renders_cleanly() {
  check_break_renders_cleanly(false, patchy::VectorStrokeAlignment::Inside);
  for (const auto alignment : {patchy::VectorStrokeAlignment::Inside, patchy::VectorStrokeAlignment::Center,
                               patchy::VectorStrokeAlignment::Outside}) {
    check_break_renders_cleanly(true, alignment);
  }
}


// Merge Shapes (#555 part 1): the options-bar button bakes the live combine of every shape in the
// layer into one outline that renders the same, moves and point-edits as one shape, and survives
// a PSD round trip; two selected shape layers merge into one.
void ui_path_merge_shapes_bakes_the_combine() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  canvas->set_view_zoom_centered(1.0);
  const auto id = make_rect_shape_layer(window, *canvas, QPoint(100, 100), QPoint(260, 220));
  patchy::VectorPath path;
  path.subpaths = {square(100, 100, 120, 0), square(180, 160, 120, 1),
                   square(130, 130, 40, 2, patchy::PathCombineOp::Subtract)};
  set_target_path(*canvas, path);
  require_action(window, "toolPathSelectAction")->trigger();
  QApplication::processEvents();
  auto* button = window.findChild<QToolButton*>(QStringLiteral("vectorMergeShapesButton"));
  CHECK(button != nullptr && button->isVisible() && button->isEnabled());
  const std::vector<QPoint> probes{{110, 110}, {150, 150}, {250, 250}, {290, 290}, {200, 200}, {120, 280}, {280, 110}};
  std::vector<int> before;
  for (const auto& probe : probes) {
    before.push_back(layer_rgba_at(document, id, probe)[3]);
  }
  save_widget_artifact("ui_i555_merge_before", *canvas);
  const auto undo_before = undo_depth(window);
  button->click();
  QApplication::processEvents();
  CHECK(undo_depth(window) == undo_before + 1);
  const auto& merged = shape_of(document, id).path;
  std::vector<int> groups;
  for (const auto& subpath : merged.subpaths) {
    if (std::find(groups.begin(), groups.end(), subpath.shape_group) == groups.end()) {
      groups.push_back(subpath.shape_group);
    }
  }
  CHECK(groups.size() == 1);
  CHECK(merged.subpaths.size() == 2);  // the outline and the hole
  for (std::size_t i = 0; i < probes.size(); ++i) {
    CHECK(std::abs(layer_rgba_at(document, id, probes[i])[3] - before[i]) < 8);
  }
  CHECK(canvas->path_edit_selected_groups().size() == 1);  // the merged shape is selected
  save_widget_artifact("ui_i555_merge_after", *canvas);
  // It moves as one shape: a Path Select drag on its edge moves the outline and the hole.
  shape_drag(*canvas, QPoint(100, 160), QPoint(120, 160));
  CHECK(layer_rgba_at(document, id, QPoint(110, 110))[3] == 0);
  CHECK(layer_rgba_at(document, id, QPoint(160, 150))[3] == 0);  // the hole moved with it
  CHECK(layer_rgba_at(document, id, QPoint(130, 110))[3] > 200);
  // PSD keeps the merged outline.
  const auto reread = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document));
  bool found = false;
  for (const auto& layer : reread.layers()) {
    if (patchy::layer_is_vector_shape(layer)) {
      found = true;
      CHECK(layer.vector_shape()->path.subpaths.size() == 2);
    }
  }
  CHECK(found);

  // Two shape layers selected in the Layers panel merge into one layer and one outline.
  const auto second = make_rect_shape_layer(window, *canvas, QPoint(400, 100), QPoint(500, 200));
  const auto third = make_rect_shape_layer(window, *canvas, QPoint(450, 150), QPoint(550, 250));
  patchy::ui::MainWindowTestAccess::select_layers(window, {second, third}, third);
  require_action(window, "toolPathSelectAction")->trigger();
  QApplication::processEvents();
  auto* merge_action = require_action(window, "pathMergeShapesAction");
  CHECK(merge_action->isEnabled());
  const auto layers_before = document.layers().size();
  merge_action->trigger();
  QApplication::processEvents();
  CHECK(document.layers().size() == layers_before - 1);
  const auto survivor = *document.active_layer_id();
  CHECK(shape_of(document, survivor).path.subpaths.size() == 1);
  CHECK(shape_of(document, survivor).path.subpaths[0].anchors.size() == 8);
}

// Weld while editing: a Direct Select drag of an end point snaps onto the other line's end point
// (a ring shows it) and the release joins them in the drag's undo step; Alt places it freely.
void ui_path_weld_on_drag_joins_end_points() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  canvas->set_view_zoom_centered(1.0);
  const auto id = make_rect_shape_layer(window, *canvas, QPoint(100, 100), QPoint(200, 200));
  patchy::VectorPath lines;
  for (const auto& [x0, x1, group] : std::vector<std::tuple<double, double, int>>{{100, 200, 0}, {230, 330, 1}}) {
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
  {
    auto content = shape_of(document, id);
    content.fill.kind = patchy::VectorFillKind::None;
    content.stroke.enabled = true;
    content.stroke.width = 6.0;
    content.stroke.alignment = patchy::VectorStrokeAlignment::Center;
    content.stroke.content.kind = patchy::VectorFillKind::Solid;
    content.stroke.content.color = patchy::RgbColor{30, 90, 200};
    document.find_layer(id)->set_vector_shape(content);
    set_target_path(*canvas, lines);
  }
  require_action(window, "toolDirectSelectAction")->trigger();
  QApplication::processEvents();

  // Alt held: the point lands where it is dropped, no snap, no join.
  const auto from = canvas->widget_position_for_document_point(QPoint(200, 300));
  const auto near = canvas->widget_position_for_document_point(QPoint(225, 303));
  click(*canvas, QPoint(200, 300));
  CHECK(canvas->path_edit_selected_anchor_count() == 1);
  send_mouse(*canvas, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  send_mouse(*canvas, QEvent::MouseMove, near, Qt::NoButton, Qt::LeftButton, Qt::AltModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, near, Qt::LeftButton, Qt::NoButton, Qt::AltModifier);
  QApplication::processEvents();
  CHECK(shape_of(document, id).path.subpaths.size() == 2);
  CHECK(std::abs(shape_of(document, id).path.subpaths[0].anchors[1].anchor_x - 225.0) < 0.5);
  patchy::ui::MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(std::abs(shape_of(document, id).path.subpaths[0].anchors[1].anchor_x - 200.0) < 0.5);

  // Without Alt it snaps onto (230, 300), rings it, and the release joins the two lines.
  click(*canvas, QPoint(200, 300));
  const auto undo_before = undo_depth(window);
  send_mouse(*canvas, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  send_mouse(*canvas, QEvent::MouseMove, near, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
  QApplication::processEvents();
  CHECK(std::abs(shape_of(document, id).path.subpaths[0].anchors[1].anchor_x - 230.0) < 1e-6);
  std::cerr << "weld status: " << window.statusBar()->currentMessage().toStdString() << '\n';
  CHECK(window.statusBar()->currentMessage().startsWith(QStringLiteral("Release to join the end points")));
  save_widget_artifact("ui_i555_weld_snap", *canvas);
  send_mouse(*canvas, QEvent::MouseButtonRelease, near, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  QApplication::processEvents();
  const auto& joined = shape_of(document, id).path.subpaths;
  CHECK(joined.size() == 1 && joined[0].anchors.size() == 3);
  CHECK(undo_depth(window) == undo_before + 1);
  save_widget_artifact("ui_i555_weld_joined", *canvas);
  patchy::ui::MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(shape_of(document, id).path.subpaths.size() == 2);

  // Dragging the open end of a line onto its other end closes it.
  click(*canvas, QPoint(330, 300));
  send_mouse(*canvas, QEvent::MouseButtonPress, canvas->widget_position_for_document_point(QPoint(330, 300)),
             Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  send_mouse(*canvas, QEvent::MouseMove, canvas->widget_position_for_document_point(QPoint(280, 250)), Qt::NoButton,
             Qt::LeftButton, Qt::NoModifier);
  send_mouse(*canvas, QEvent::MouseMove, canvas->widget_position_for_document_point(QPoint(233, 297)), Qt::NoButton,
             Qt::LeftButton, Qt::NoModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, canvas->widget_position_for_document_point(QPoint(233, 297)),
             Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  QApplication::processEvents();
  // A two-point line cannot close on itself (one point), so it only snapped there.
  CHECK(shape_of(document, id).path.subpaths.size() == 2);
  CHECK(std::abs(shape_of(document, id).path.subpaths[1].anchors[1].anchor_x - 230.0) < 1e-6);
}


// Splitting a shape in two (#555 usage sweep): Shift+click two opposite corners with Direct
// Select, Break Path at Point, Join End Points: two closed halves, two undo steps.
void ui_path_split_shape_with_break_and_join() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  canvas->set_view_zoom_centered(1.0);
  const auto id = make_rect_shape_layer(window, *canvas, QPoint(100, 100), QPoint(300, 220));
  require_action(window, "toolDirectSelectAction")->trigger();
  QApplication::processEvents();
  click(*canvas, QPoint(100, 100));
  const auto corner = canvas->widget_position_for_document_point(QPoint(300, 220));
  send_mouse(*canvas, QEvent::MouseButtonPress, corner, Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, corner, Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(canvas->path_edit_selected_anchor_count() == 2);
  const auto undo_before = undo_depth(window);
  require_action(window, "pathBreakAtPointAction")->trigger();
  QApplication::processEvents();
  CHECK(shape_of(document, id).path.subpaths.size() == 2);
  auto* join = require_action(window, "pathJoinEndPointsAction");
  CHECK(join->isEnabled());
  join->trigger();
  QApplication::processEvents();
  const auto& halves = shape_of(document, id).path.subpaths;
  CHECK(halves.size() == 2 && halves[0].closed && halves[1].closed);
  CHECK(undo_depth(window) == undo_before + 2);
  // The fill is unchanged: both triangles together cover the rectangle.
  CHECK(layer_rgba_at(document, id, QPoint(150, 200))[3] > 200);
  CHECK(layer_rgba_at(document, id, QPoint(250, 120))[3] > 200);
}


// Shapes inside one layer align (#555 usage sweep): Path Select two of them, Layer > Arrange >
// Align Left Edges; the layer itself stays put and the shapes stay selected.
void ui_path_align_shapes_inside_a_layer() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  canvas->set_view_zoom_centered(1.0);
  const auto id = make_rect_shape_layer(window, *canvas, QPoint(100, 100), QPoint(200, 200));
  patchy::VectorPath path;
  path.subpaths = {square(100, 100, 60, 0), square(220, 220, 80, 1)};
  set_target_path(*canvas, path);
  require_action(window, "toolPathSelectAction")->trigger();
  QApplication::processEvents();
  click(*canvas, QPoint(100, 130));
  const auto second = canvas->widget_position_for_document_point(QPoint(220, 260));
  send_mouse(*canvas, QEvent::MouseButtonPress, second, Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, second, Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(canvas->path_edit_selected_groups().size() == 2);
  auto* align = require_action(window, "layerAlignLeftAction");
  CHECK(align->isEnabled());
  const auto undo_before = undo_depth(window);
  align->trigger();
  QApplication::processEvents();
  CHECK(undo_depth(window) == undo_before + 1);
  const auto& aligned = shape_of(document, id).path.subpaths;
  CHECK(std::abs(aligned[1].anchors[0].anchor_x - 100.0) < 1e-6);
  CHECK(std::abs(aligned[1].anchors[0].anchor_y - 220.0) < 1e-6);
  CHECK(canvas->path_edit_selected_groups().size() == 2);
}


// Seth's question (#555): dropping a point on its neighbour merges the two into one point on
// release (the segment between them shrank to nothing), in the drag's undo step; a point that is
// not a neighbour or an end point only snaps; Alt places it freely.
int anchor_index_at(const patchy::PathSubpath& subpath, double x, double y) {
  for (int i = 0; i < static_cast<int>(subpath.anchors.size()); ++i) {
    const auto& anchor = subpath.anchors[static_cast<std::size_t>(i)];
    if (std::abs(anchor.anchor_x - x) < 0.5 && std::abs(anchor.anchor_y - y) < 0.5) {
      return i;
    }
  }
  return -1;
}

// Presses on `from`, moves to `to` and releases there, with `modifiers` during the move and
// release; `before_release` runs while the drag is held.
template <typename Held>
void drag_point(patchy::ui::CanvasWidget& canvas, QPoint from, QPoint to, Qt::KeyboardModifiers modifiers,
                Held before_release) {
  const auto press = canvas.widget_position_for_document_point(from);
  const auto drop = canvas.widget_position_for_document_point(to);
  send_mouse(canvas, QEvent::MouseButtonPress, press, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  send_mouse(canvas, QEvent::MouseMove, drop, Qt::NoButton, Qt::LeftButton, modifiers);
  QApplication::processEvents();
  before_release();
  send_mouse(canvas, QEvent::MouseButtonRelease, drop, Qt::LeftButton, Qt::NoButton, modifiers);
  QApplication::processEvents();
}

// No two neighbouring points of any subpath sit at the same place (a zero-length segment).
bool has_zero_length_segment(const patchy::VectorPath& path) {
  for (const auto& subpath : path.subpaths) {
    const auto count = subpath.anchors.size();
    const auto segments = subpath.closed ? count : count - 1;
    for (std::size_t i = 0; i < segments; ++i) {
      const auto& a = subpath.anchors[i];
      const auto& b = subpath.anchors[(i + 1) % count];
      if (std::hypot(a.anchor_x - b.anchor_x, a.anchor_y - b.anchor_y) < 0.01) {
        return true;
      }
    }
  }
  return false;
}

void ui_path_weld_on_drag_merges_neighbours() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  canvas->set_view_zoom_centered(1.0);
  const auto id = make_rect_shape_layer(window, *canvas, QPoint(100, 100), QPoint(300, 220));
  require_action(window, "toolDirectSelectAction")->trigger();
  QApplication::processEvents();
  const auto outline = [&]() -> const patchy::PathSubpath& { return shape_of(document, id).path.subpaths[0]; };
  CHECK(outline().closed && outline().anchors.size() == 4);

  // Alt held: the corner lands on the next one and stays two points.
  click(*canvas, QPoint(100, 100));
  drag_point(*canvas, QPoint(100, 100), QPoint(296, 103), Qt::AltModifier, [] {});
  CHECK(outline().anchors.size() == 4);
  patchy::ui::MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(anchor_index_at(outline(), 100, 100) >= 0);

  // Onto its neighbour: a double ring and the hint while held, one point (a triangle) after.
  click(*canvas, QPoint(100, 100));
  const auto undo_before = undo_depth(window);
  drag_point(*canvas, QPoint(100, 100), QPoint(296, 103), Qt::NoModifier, [&] {
    CHECK(window.statusBar()->currentMessage().startsWith(QStringLiteral("Release to merge the points")));
    save_widget_artifact("ui_i555_weld_merge_snap", *canvas);
  });
  CHECK(outline().closed && outline().anchors.size() == 3);
  CHECK(anchor_index_at(outline(), 300, 100) >= 0 && anchor_index_at(outline(), 100, 100) < 0);
  CHECK(!has_zero_length_segment(shape_of(document, id).path));
  CHECK(undo_depth(window) == undo_before + 1);
  CHECK(canvas->path_edit_selected_anchor_count() == 1);
  // The triangle renders: filled inside, nothing beyond its corners.
  CHECK(layer_rgba_at(document, id, QPoint(280, 180))[3] > 200);
  CHECK(layer_rgba_at(document, id, QPoint(120, 120))[3] == 0);
  const auto bounds = std::as_const(document).find_layer(id)->bounds();
  CHECK(bounds.x >= 99 && bounds.y >= 99 && bounds.x + bounds.width <= 301 && bounds.y + bounds.height <= 221);
  save_widget_artifact("ui_i555_weld_merge_triangle", *canvas);

  // A triangle's corner onto the next only snaps: two points cannot make a shape.
  click(*canvas, QPoint(300, 100));
  drag_point(*canvas, QPoint(300, 100), QPoint(297, 217), Qt::NoModifier, [&] {
    CHECK(window.statusBar()->currentMessage().startsWith(QStringLiteral("Snapped to a point")));
  });
  CHECK(outline().anchors.size() == 3);
  patchy::ui::MainWindowTestAccess::undo(window);
  patchy::ui::MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(outline().anchors.size() == 4);

  // Across the closing segment: the last corner onto the first.
  const int last = static_cast<int>(outline().anchors.size()) - 1;
  const auto last_point = QPoint(static_cast<int>(outline().anchors[static_cast<std::size_t>(last)].anchor_x),
                                 static_cast<int>(outline().anchors[static_cast<std::size_t>(last)].anchor_y));
  const auto first_point = QPoint(static_cast<int>(outline().anchors[0].anchor_x),
                                  static_cast<int>(outline().anchors[0].anchor_y));
  click(*canvas, last_point);
  drag_point(*canvas, last_point, first_point + QPoint(3, 3), Qt::NoModifier, [] {});
  CHECK(outline().closed && outline().anchors.size() == 3);
  CHECK(!has_zero_length_segment(shape_of(document, id).path));
  patchy::ui::MainWindowTestAccess::undo(window);
  QApplication::processEvents();

  // Opposite corners are not neighbours: the point only snaps and two points stay there.
  click(*canvas, QPoint(100, 100));
  drag_point(*canvas, QPoint(100, 100), QPoint(297, 217), Qt::NoModifier, [&] {
    CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("Only end points or neighbouring points")));
  });
  CHECK(outline().anchors.size() == 4);
  patchy::ui::MainWindowTestAccess::undo(window);
  QApplication::processEvents();

  // An open path: the second point onto the third merges them; the ends stay.
  patchy::VectorPath line;
  patchy::PathSubpath open;
  open.closed = false;
  for (const double x : {100.0, 200.0, 300.0, 400.0}) {
    patchy::PathAnchor anchor;
    anchor.anchor_x = anchor.in_x = anchor.out_x = x;
    anchor.anchor_y = anchor.in_y = anchor.out_y = 300.0;
    open.anchors.push_back(anchor);
  }
  line.subpaths = {open};
  set_target_path(*canvas, line);
  click(*canvas, QPoint(200, 300));
  drag_point(*canvas, QPoint(200, 300), QPoint(296, 302), Qt::NoModifier, [] {});
  CHECK(!outline().closed && outline().anchors.size() == 3);
  CHECK(anchor_index_at(outline(), 300, 300) == 1);
  CHECK(!has_zero_length_segment(shape_of(document, id).path));
}

}  // namespace

std::vector<patchy::test::TestCase> vector_path_edits_tests() {
  return {
      {"ui_path_weld_on_drag_merges_neighbours", ui_path_weld_on_drag_merges_neighbours},
      {"ui_path_align_shapes_inside_a_layer", ui_path_align_shapes_inside_a_layer},
      {"ui_path_split_shape_with_break_and_join", ui_path_split_shape_with_break_and_join},
      {"ui_path_merge_shapes_bakes_the_combine", ui_path_merge_shapes_bakes_the_combine},
      {"ui_path_weld_on_drag_joins_end_points", ui_path_weld_on_drag_joins_end_points},
      {"ui_path_break_several_points_renders_cleanly", ui_path_break_several_points_renders_cleanly},
      {"ui_path_point_commands_edit_menus_and_undo", ui_path_point_commands_edit_menus_and_undo},
      {"ui_path_join_end_points_connects_two_open_paths", ui_path_join_end_points_connects_two_open_paths},
      {"ui_path_components_cut_copy_paste_round_trip", ui_path_components_cut_copy_paste_round_trip},
      {"ui_path_components_color_and_move_to_new_layer", ui_path_components_color_and_move_to_new_layer},
  };
}
