#include <QStatusBar>
#include "ui/canvas_widget.hpp"
#include "ui/main_window.hpp"
#include "ui/app_settings.hpp"
#include "test_harness.hpp"
#include <QApplication>
#include <QByteArray>
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QIcon>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <algorithm>
#include <atomic>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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
// Move-tool alignment guides (snap targets, the magenta overlay, the Snap
// checkbox) and the Layer > Arrange > Align / Distribute commands with their
// options-bar buttons. See docs/alignment.md.
#include "ui_test_support.hpp"
#include "ui_test_groups.hpp"
#include "core/layer_alignment.hpp"
#include "ui/ui_test_access.hpp"
#include <QListWidget>
#include <QMenu>
#include <QSpinBox>
#include <QToolBar>
#include <QToolButton>

namespace {

using namespace patchy::test::ui;

// A pixel layer whose buffer carries a transparent margin around an opaque
// core, so render bounds and opaque bounds differ.
patchy::PixelBuffer framed_pixels(int width, int height, int margin, QColor color) {
  auto pixels = solid_pixels(width, height, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  for (int y = margin; y < height - margin; ++y) {
    for (int x = margin; x < width - margin; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(color.red());
      px[1] = static_cast<std::uint8_t>(color.green());
      px[2] = static_cast<std::uint8_t>(color.blue());
      px[3] = 255;
    }
  }
  return pixels;
}

patchy::Layer solid_layer(patchy::Document& document, const char* name, patchy::Rect bounds, QColor color) {
  patchy::Layer layer(document.allocate_layer_id(), name,
                      solid_pixels(bounds.width, bounds.height, patchy::PixelFormat::rgba8(), color));
  layer.set_bounds(bounds);
  return layer;
}

const patchy::Layer* find_layer(patchy::ui::MainWindow& window, const char* name) {
  const auto& document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  for (const auto& layer : document.layers()) {
    if (layer.name() == name) {
      return &layer;
    }
    if (layer.kind() == patchy::LayerKind::Group) {
      for (const auto& child : layer.children()) {
        if (child.name() == name) {
          return &child;
        }
      }
    }
  }
  return nullptr;
}

void select_layers(QListWidget& list, const std::vector<QString>& names) {
  list.clearSelection();
  QListWidgetItem* first = nullptr;
  for (const auto& name : names) {
    auto* item = require_layer_item(list, name);
    if (first == nullptr) {
      first = item;
      list.setCurrentItem(item);
    }
    item->setSelected(true);
  }
  QApplication::processEvents();
}

// Two layers: a framed target (opaque core at 90,40 40x30 inside a 60x50
// buffer at 80,30) and a 24x24 mover at the far left. Canvas: 240x150.
struct SnapScene {
  patchy::ui::MainWindow window;
  patchy::ui::CanvasWidget* canvas{nullptr};
  QListWidget* layer_list{nullptr};

  explicit SnapScene(patchy::Rect mover_bounds = patchy::Rect{10, 100, 24, 24}) {
    patchy::Document document(240, 150, patchy::PixelFormat::rgba8());
    patchy::Layer target(document.allocate_layer_id(), "Target", framed_pixels(60, 50, 10, QColor(40, 180, 90)));
    target.set_bounds(patchy::Rect{80, 30, 60, 50});
    document.add_layer(std::move(target));
    document.add_layer(solid_layer(document, "Mover", mover_bounds, QColor(220, 40, 40)));
    show_window(window);
    window.add_document_session(std::move(document), QStringLiteral("Snap Scene"));
    QApplication::processEvents();
    canvas = require_canvas(window);
    layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
    CHECK(layer_list != nullptr);
    select_layers(*layer_list, {QStringLiteral("Mover")});
    require_action_by_text(window, QStringLiteral("Move"))->trigger();
    canvas->set_show_transform_controls(false);
    canvas->set_auto_select_layer(false);
    canvas->set_snap_enabled(true);
    canvas->set_snap_to_layers(true);
    canvas->set_snap_to_document(true);
    canvas->set_zoom(1.0);
    QApplication::processEvents();
  }

  void drag_mover(QPoint from_document, QPoint widget_delta, Qt::KeyboardModifiers modifiers = Qt::NoModifier,
                  bool release = true) {
    const auto start = canvas->widget_position_for_document_point(from_document);
    send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton, modifiers);
    send_mouse(*canvas, QEvent::MouseMove, start + widget_delta / 2, Qt::NoButton, Qt::LeftButton, modifiers);
    send_mouse(*canvas, QEvent::MouseMove, start + widget_delta, Qt::NoButton, Qt::LeftButton, modifiers);
    QApplication::processEvents();
    if (release) {
      send_mouse(*canvas, QEvent::MouseButtonRelease, start + widget_delta, Qt::LeftButton, Qt::NoButton,
                 modifiers);
      QApplication::processEvents();
    }
  }
};

void ui_move_snap_targets_use_opaque_bounds_and_skip_moving_layers() {
  SnapScene scene;
  // A layer no longer snaps back to its own start: +14 px used to be pulled to
  // +12 because the mover's own center sat 2 px from its shifted left edge.
  scene.drag_mover(QPoint(22, 112), QPoint(14, 0));
  const auto* mover = find_layer(scene.window, "Mover");
  CHECK(mover != nullptr && mover->bounds().x == 24 && mover->bounds().y == 100);

  // Dragging the left edge to 92 snaps it to the target's OPAQUE left edge (90),
  // not the buffer edge (80) or, through the center, the render center (110).
  scene.drag_mover(QPoint(36, 112), QPoint(68, 0));
  mover = find_layer(scene.window, "Mover");
  CHECK(mover != nullptr && mover->bounds().x == 90 && mover->bounds().y == 100);
  CHECK(!scene.canvas->move_snap_match_x().has_value());
  CHECK(!scene.canvas->move_snap_match_y().has_value());
}

void ui_move_snap_draws_alignment_guide_and_clears_on_release() {
  SnapScene scene;
  PaintRegionRecorder recorder(scene.canvas);
  scene.canvas->installEventFilter(&recorder);
  // Left edge lands at 92: snapped to 90, a Layer match with a vertical line.
  scene.drag_mover(QPoint(22, 112), QPoint(82, 0), Qt::NoModifier, /*release=*/false);
  const auto& match_x = scene.canvas->move_snap_match_x();
  CHECK(match_x.has_value());
  if (match_x.has_value()) {
    CHECK(match_x->kind == patchy::ui::CanvasWidget::SnapMatch::Kind::Layer);
    CHECK(match_x->position == 90.0);
    // The guide bridges the target core (top 40) and the moving rect (bottom 124).
    CHECK(match_x->target_span.top() == 40.0 && match_x->target_span.bottom() == 70.0);
    CHECK(match_x->source_span.left() == 90.0 && match_x->source_span.bottom() == 124.0);
  }
  CHECK(!scene.canvas->move_snap_match_y().has_value());

  const auto frame = scene.canvas->grab().toImage();
  const auto guide_point = scene.canvas->widget_position_for_document_point(QPoint(90, 85));
  const auto expected = patchy::ui::theme().canvas_snap_guide;
  bool guide_drawn = false;
  for (int dx = -1; dx <= 1 && !guide_drawn; ++dx) {
    guide_drawn = color_close(frame.pixelColor(guide_point + QPoint(dx, 0)), expected, 40);
  }
  CHECK(guide_drawn);
  // Outside the bridged extent (above the target's top) nothing is drawn.
  CHECK(!color_close(frame.pixelColor(scene.canvas->widget_position_for_document_point(QPoint(90, 20))), expected,
                     40));
  save_widget_artifact("ui_move_snap_alignment_guide", scene.window);

  recorder.reset();
  const auto end = scene.canvas->widget_position_for_document_point(QPoint(22, 112)) + QPoint(82, 0);
  send_mouse(*scene.canvas, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(!scene.canvas->move_snap_match_x().has_value());
  // The release repaint covers the line so it cannot linger.
  CHECK(recorder.region().contains(guide_point));
  const auto* mover = find_layer(scene.window, "Mover");
  CHECK(mover != nullptr && mover->bounds().x == 90);

  // A snapped drag interrupted by a tool change drops the guide too.
  scene.drag_mover(QPoint(102, 112), QPoint(-80, 0), Qt::NoModifier, /*release=*/false);
  require_action_by_text(scene.window, QStringLiteral("Brush"))->trigger();
  QApplication::processEvents();
  CHECK(!scene.canvas->move_snap_match_x().has_value());
  CHECK(!scene.canvas->move_snap_match_y().has_value());
}

void ui_move_snap_shift_constraint_drops_pinned_axis_guide() {
  // Mover at x 6: its left edge is 6 px from the canvas edge, so without the
  // constraint the x axis would snap to 0 while the y axis snaps to the
  // target's bottom (70). Shift pins x (the drag is mostly vertical).
  SnapScene scene(patchy::Rect{6, 100, 24, 24});
  scene.drag_mover(QPoint(18, 112), QPoint(2, -30), Qt::ShiftModifier, /*release=*/false);
  CHECK(!scene.canvas->move_snap_match_x().has_value());
  const auto& match_y = scene.canvas->move_snap_match_y();
  CHECK(match_y.has_value());
  if (match_y.has_value()) {
    CHECK(match_y->kind == patchy::ui::CanvasWidget::SnapMatch::Kind::Layer);
    CHECK(match_y->position == 70.0);
  }
  const auto end = scene.canvas->widget_position_for_document_point(QPoint(18, 112)) + QPoint(2, -30);
  send_mouse(*scene.canvas, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
  QApplication::processEvents();
  const auto* mover = find_layer(scene.window, "Mover");
  CHECK(mover != nullptr && mover->bounds().x == 6 && mover->bounds().y == 70);

  // Without Shift the x axis snaps to the canvas edge on the way back down.
  scene.drag_mover(QPoint(18, 82), QPoint(0, 30));
  mover = find_layer(scene.window, "Mover");
  CHECK(mover != nullptr && mover->bounds().x == 0 && mover->bounds().y == 100);
}

// Whole-shape drags (GitHub issue 695): Path Select, and Ctrl in a shape tool,
// snap the moved shape's bounds like the Move tool snaps a layer.
//
// Canvas 420x300 (center 210,150). A solid pixel target at 200,60 80x60, and a
// rectangle shape layer drawn with the Rectangle tool, 20,160 to 60,190 unless a
// test asks for another. The Rectangle tool stays active. With `hexagon` the shape is a
// live hexagon drawn with the Polygon tool from its center (`shape_from`) to its first
// vertex (`shape_to`), and the Polygon tool stays active.
struct ShapeSnapScene {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  patchy::ui::CanvasWidget* canvas{nullptr};
  patchy::LayerId shape_id{};

  explicit ShapeSnapScene(QPoint shape_from = QPoint(20, 160), QPoint shape_to = QPoint(60, 190),
                          bool hexagon = false) {
    patchy::Document document(420, 300, patchy::PixelFormat::rgba8());
    document.add_layer(solid_layer(document, "Target", patchy::Rect{200, 60, 80, 60}, QColor(40, 180, 90)));
    show_window(window);
    window.add_document_session(std::move(document), QStringLiteral("Shape Snap Scene"));
    QApplication::processEvents();
    canvas = require_canvas(window);
    // Snapping stays off while the shape is drawn, so it sits exactly where asked.
    canvas->set_snap_enabled(false);
    canvas->set_snap_to_layers(true);
    canvas->set_snap_to_document(true);
    canvas->set_zoom(1.0);
    // Through the tool's own action, so the toolbox and the options bar show it too.
    require_hotkey_action(window, QStringLiteral("tools.rect"))->trigger();
    QApplication::processEvents();
    CHECK(canvas->tool() == patchy::ui::CanvasTool::Rectangle);
    auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
    CHECK(radius_spin != nullptr);
    if (radius_spin != nullptr) {
      radius_spin->setValue(0);
    }
    QApplication::processEvents();
    if (hexagon) {
      require_hotkey_action(window, QStringLiteral("tools.polygon"))->trigger();
      QApplication::processEvents();
      CHECK(canvas->tool() == patchy::ui::CanvasTool::Polygon);
      auto* sides_spin = window.findChild<QSpinBox*>(QStringLiteral("polygonSidesSpin"));
      auto* inset_spin = window.findChild<QSpinBox*>(QStringLiteral("polygonStarInsetSpin"));
      CHECK(sides_spin != nullptr && inset_spin != nullptr);
      if (sides_spin != nullptr && inset_spin != nullptr) {
        sides_spin->setValue(6);
        inset_spin->setValue(0);
      }
      QApplication::processEvents();
    }
    drag(*canvas, canvas->widget_position_for_document_point(shape_from),
         canvas->widget_position_for_document_point(shape_to));
    QApplication::processEvents();
    const auto active = std::as_const(patchy::ui::MainWindowTestAccess::document(window)).active_layer_id();
    CHECK(active.has_value());
    shape_id = active.value_or(0);
    canvas->set_snap_enabled(true);
    canvas->setFocus();
  }

  [[nodiscard]] const patchy::VectorPath& path() {
    const auto* layer = std::as_const(patchy::ui::MainWindowTestAccess::document(window)).find_layer(shape_id);
    static const patchy::VectorPath empty;
    return layer != nullptr && layer->vector_shape() != nullptr ? layer->vector_shape()->path : empty;
  }

  // The anchor bounds of one subpath (a plain rectangle: its tight bounds).
  [[nodiscard]] QRectF bounds(std::size_t subpath = 0) {
    const auto& shape_path = path();
    CHECK(subpath < shape_path.subpaths.size());
    if (subpath >= shape_path.subpaths.size() || shape_path.subpaths[subpath].anchors.empty()) {
      return {};
    }
    const auto& anchors = shape_path.subpaths[subpath].anchors;
    QRectF rect(QPointF(anchors[0].anchor_x, anchors[0].anchor_y), QSizeF(0.0, 0.0));
    for (const auto& anchor : anchors) {
      rect.setLeft(std::min(rect.left(), anchor.anchor_x));
      rect.setRight(std::max(rect.right(), anchor.anchor_x));
      rect.setTop(std::min(rect.top(), anchor.anchor_y));
      rect.setBottom(std::max(rect.bottom(), anchor.anchor_y));
    }
    return rect;
  }

  // `press_modifiers` differ from the drag's when a key goes down after the press (Shift at a
  // Path Select press on a selected shape deselects it instead of dragging).
  void drag_shape(QPoint from_document, QPoint widget_delta, Qt::KeyboardModifiers modifiers = Qt::NoModifier,
                  bool release = true, std::optional<Qt::KeyboardModifiers> press_modifiers = std::nullopt) {
    const auto start = canvas->widget_position_for_document_point(from_document);
    send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton,
               press_modifiers.value_or(modifiers));
    send_mouse(*canvas, QEvent::MouseMove, start + widget_delta / 2, Qt::NoButton, Qt::LeftButton, modifiers);
    send_mouse(*canvas, QEvent::MouseMove, start + widget_delta, Qt::NoButton, Qt::LeftButton, modifiers);
    QApplication::processEvents();
    if (release) {
      this->release(from_document, widget_delta, modifiers);
    }
  }

  void release(QPoint from_document, QPoint widget_delta, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    send_mouse(*canvas, QEvent::MouseButtonRelease,
               canvas->widget_position_for_document_point(from_document) + widget_delta, Qt::LeftButton,
               Qt::NoButton, modifiers);
    QApplication::processEvents();
  }

  void use_path_select() {
    require_action(window, "toolPathSelectAction")->trigger();
    QApplication::processEvents();
  }
};

bool rect_is(const QRectF& rect, double left, double top, double right, double bottom, double tolerance = 1e-6) {
  return std::abs(rect.left() - left) <= tolerance && std::abs(rect.top() - top) <= tolerance &&
         std::abs(rect.right() - right) <= tolerance && std::abs(rect.bottom() - bottom) <= tolerance;
}

void ui_shape_drag_snap_path_select_lands_on_layer_edge_and_clears_guide() {
  ShapeSnapScene scene;
  scene.use_path_select();
  PaintRegionRecorder recorder(scene.canvas);
  scene.canvas->installEventFilter(&recorder);
  // +137: the right edge would stop at 197, 3 px short of the target's left edge (200).
  scene.drag_shape(QPoint(40, 175), QPoint(137, 0), Qt::NoModifier, /*release=*/false);
  CHECK(rect_is(scene.bounds(), 160.0, 160.0, 200.0, 190.0));
  const auto& match_x = scene.canvas->move_snap_match_x();
  CHECK(match_x.has_value());
  if (match_x.has_value()) {
    CHECK(match_x->kind == patchy::ui::CanvasWidget::SnapMatch::Kind::Layer);
    CHECK(match_x->position == 200.0);
    // The guide bridges the target (top 60) and the shape where it landed (bottom 190).
    CHECK(match_x->target_span.top() == 60.0 && match_x->target_span.bottom() == 120.0);
    CHECK(rect_is(match_x->source_span, 160.0, 160.0, 200.0, 190.0));
  }
  CHECK(!scene.canvas->move_snap_match_y().has_value());

  const auto frame = scene.canvas->grab().toImage();
  const auto guide_point = scene.canvas->widget_position_for_document_point(QPoint(200, 140));
  const auto expected = patchy::ui::theme().canvas_snap_guide;
  bool guide_drawn = false;
  for (int dx = -1; dx <= 1 && !guide_drawn; ++dx) {
    guide_drawn = color_close(frame.pixelColor(guide_point + QPoint(dx, 0)), expected, 40);
  }
  CHECK(guide_drawn);
  // Past the bridged extent (above the target's top) nothing is drawn.
  CHECK(!color_close(frame.pixelColor(scene.canvas->widget_position_for_document_point(QPoint(200, 30))), expected,
                     40));

  recorder.reset();
  scene.release(QPoint(40, 175), QPoint(137, 0));
  CHECK(!scene.canvas->move_snap_match_x().has_value());
  CHECK(!scene.canvas->move_snap_match_y().has_value());
  // The release repaint covers the line so it cannot linger.
  CHECK(recorder.region().contains(guide_point));
  CHECK(rect_is(scene.bounds(), 160.0, 160.0, 200.0, 190.0));
  CHECK(!color_close(scene.canvas->grab().toImage().pixelColor(guide_point), expected, 40));

  // One undo step takes the whole snapped move back.
  require_action_by_text(scene.window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(rect_is(scene.bounds(), 20.0, 160.0, 60.0, 190.0));

  // A snapped drag interrupted by a tool change drops the guide too.
  scene.drag_shape(QPoint(40, 175), QPoint(137, 0), Qt::NoModifier, /*release=*/false);
  CHECK(scene.canvas->move_snap_match_x().has_value());
  require_action_by_text(scene.window, QStringLiteral("Brush"))->trigger();
  QApplication::processEvents();
  CHECK(!scene.canvas->move_snap_match_x().has_value());
  CHECK(!scene.canvas->move_snap_match_y().has_value());
}

void ui_shape_drag_snap_ctrl_in_shape_tool_lands_on_layer_edge() {
  ShapeSnapScene scene;
  CHECK(scene.canvas->tool() == patchy::ui::CanvasTool::Rectangle);
  const auto layer_count = std::as_const(patchy::ui::MainWindowTestAccess::document(scene.window)).layers().size();
  // Ctrl is the temporary Path Select: the same drag, the same landing.
  scene.drag_shape(QPoint(40, 175), QPoint(137, 0), Qt::ControlModifier, /*release=*/false);
  CHECK(rect_is(scene.bounds(), 160.0, 160.0, 200.0, 190.0));
  const auto& match_x = scene.canvas->move_snap_match_x();
  CHECK(match_x.has_value());
  if (match_x.has_value()) {
    CHECK(match_x->kind == patchy::ui::CanvasWidget::SnapMatch::Kind::Layer);
    CHECK(match_x->position == 200.0);
  }
  save_widget_artifact("ui_shape_drag_snap_ctrl_in_shape_tool", *scene.canvas);
  save_widget_artifact("ui_shape_drag_snap_ctrl_in_shape_tool_window", scene.window);
  scene.release(QPoint(40, 175), QPoint(137, 0), Qt::ControlModifier);
  CHECK(!scene.canvas->move_snap_match_x().has_value());
  CHECK(rect_is(scene.bounds(), 160.0, 160.0, 200.0, 190.0));
  // Nothing was drawn, and the tool is still the Rectangle tool.
  CHECK(std::as_const(patchy::ui::MainWindowTestAccess::document(scene.window)).layers().size() == layer_count);
  CHECK(scene.canvas->tool() == patchy::ui::CanvasTool::Rectangle);
  CHECK(scene.path().subpaths.size() == 1);
}

void ui_shape_drag_snap_center_lands_on_document_center() {
  ShapeSnapScene scene;
  scene.use_path_select();
  // The shape's center (40,175) would stop at (207,153): 3 px from the canvas center on both axes.
  scene.drag_shape(QPoint(40, 175), QPoint(167, -22), Qt::NoModifier, /*release=*/false);
  CHECK(rect_is(scene.bounds(), 190.0, 135.0, 230.0, 165.0));
  const auto& match_x = scene.canvas->move_snap_match_x();
  const auto& match_y = scene.canvas->move_snap_match_y();
  CHECK(match_x.has_value() && match_y.has_value());
  if (match_x.has_value() && match_y.has_value()) {
    CHECK(match_x->kind == patchy::ui::CanvasWidget::SnapMatch::Kind::Document);
    CHECK(match_y->kind == patchy::ui::CanvasWidget::SnapMatch::Kind::Document);
    CHECK(match_x->position == 210.0);
    CHECK(match_y->position == 150.0);
  }
  scene.release(QPoint(40, 175), QPoint(167, -22));
  CHECK(rect_is(scene.bounds(), 190.0, 135.0, 230.0, 165.0));
}

void ui_shape_drag_snap_follows_view_snap() {
  // View > Snap off: the shape goes exactly where the pointer took it.
  ShapeSnapScene scene;
  scene.canvas->set_snap_enabled(false);
  scene.drag_shape(QPoint(40, 175), QPoint(137, 0), Qt::ControlModifier, /*release=*/false);
  CHECK(rect_is(scene.bounds(), 157.0, 160.0, 197.0, 190.0, 0.01));
  CHECK(!scene.canvas->move_snap_match_x().has_value());
  CHECK(!scene.canvas->move_snap_match_y().has_value());
  scene.release(QPoint(40, 175), QPoint(137, 0), Qt::ControlModifier);
  CHECK(rect_is(scene.bounds(), 157.0, 160.0, 197.0, 190.0, 0.01));

  scene.drag_shape(QPoint(177, 175), QPoint(-137, 0), Qt::ControlModifier);
  CHECK(rect_is(scene.bounds(), 20.0, 160.0, 60.0, 190.0, 0.01));

  // Snap on with the layer targets off (View > Snap To > Layers): still no pull to the layer.
  scene.canvas->set_snap_enabled(true);
  scene.canvas->set_snap_to_layers(false);
  scene.drag_shape(QPoint(40, 175), QPoint(137, 0), Qt::ControlModifier);
  CHECK(rect_is(scene.bounds(), 157.0, 160.0, 197.0, 190.0, 0.01));

  // A click that wobbles a pixel is not a drag: the edge 2 px away does not pull the shape.
  scene.canvas->set_snap_to_layers(true);
  scene.drag_shape(QPoint(177, 175), QPoint(1, 0), Qt::ControlModifier);
  CHECK(rect_is(scene.bounds(), 158.0, 160.0, 198.0, 190.0, 0.01));
}

void ui_shape_drag_snap_shift_drops_pinned_axis_guide() {
  // The shape's top (156) sits 6 px under the canvas center line (150), so the y
  // axis would snap there. Shift pins y (the drag is mostly horizontal).
  ShapeSnapScene scene(QPoint(20, 156), QPoint(60, 186));
  scene.use_path_select();
  scene.drag_shape(QPoint(40, 171), QPoint(137, 2), Qt::ShiftModifier, /*release=*/false);
  CHECK(rect_is(scene.bounds(), 160.0, 156.0, 200.0, 186.0));
  const auto& match_x = scene.canvas->move_snap_match_x();
  CHECK(match_x.has_value());
  if (match_x.has_value()) {
    CHECK(match_x->kind == patchy::ui::CanvasWidget::SnapMatch::Kind::Layer);
    CHECK(match_x->position == 200.0);
    // The guide spans the shape where it is, not where the dropped y snap would have put it.
    CHECK(rect_is(match_x->source_span, 160.0, 156.0, 200.0, 186.0));
  }
  CHECK(!scene.canvas->move_snap_match_y().has_value());
  scene.release(QPoint(40, 171), QPoint(137, 2), Qt::ShiftModifier);
  CHECK(rect_is(scene.bounds(), 160.0, 156.0, 200.0, 186.0));

  // Without Shift the y axis snaps to the canvas center line; x stays on the edge.
  scene.drag_shape(QPoint(180, 171), QPoint(0, -12), Qt::NoModifier, /*release=*/false);
  CHECK(rect_is(scene.bounds(), 160.0, 150.0, 200.0, 180.0));
  CHECK(scene.canvas->move_snap_match_x().has_value());
  const auto& match_y = scene.canvas->move_snap_match_y();
  CHECK(match_y.has_value());
  if (match_y.has_value()) {
    CHECK(match_y->kind == patchy::ui::CanvasWidget::SnapMatch::Kind::Document);
    CHECK(match_y->position == 150.0);
  }
  scene.release(QPoint(180, 171), QPoint(0, -12));

  // Shift on a diagonal holds both axes to each other: no snap and no guide, though the
  // shape's bottom stops 3 px from the canvas edge (300).
  scene.drag_shape(QPoint(180, 165), QPoint(-118, 116), Qt::ShiftModifier, /*release=*/false, Qt::NoModifier);
  CHECK(!scene.canvas->move_snap_match_x().has_value());
  CHECK(!scene.canvas->move_snap_match_y().has_value());
  CHECK(rect_is(scene.bounds(), 43.0, 267.0, 83.0, 297.0, 0.01));
  scene.release(QPoint(180, 165), QPoint(-118, 116), Qt::ShiftModifier);
}

void ui_shape_drag_snap_alt_copy_lands_on_layer_edge() {
  // Ctrl+Alt in a shape tool drags a copy; the copy snaps, the original stays.
  ShapeSnapScene scene;
  const auto modifiers = Qt::ControlModifier | Qt::AltModifier;
  scene.drag_shape(QPoint(40, 175), QPoint(137, 0), modifiers, /*release=*/false);
  CHECK(scene.path().subpaths.size() == 2);
  if (scene.path().subpaths.size() == 2) {
    CHECK(rect_is(scene.bounds(0), 20.0, 160.0, 60.0, 190.0));
    CHECK(rect_is(scene.bounds(1), 160.0, 160.0, 200.0, 190.0));
  }
  CHECK(scene.canvas->move_snap_match_x().has_value());
  scene.release(QPoint(40, 175), QPoint(137, 0), modifiers);
  CHECK(!scene.canvas->move_snap_match_x().has_value());

  // Path Select's own Alt-drag does the same.
  ShapeSnapScene arrow_scene;
  arrow_scene.use_path_select();
  arrow_scene.drag_shape(QPoint(40, 175), QPoint(137, 0), Qt::AltModifier);
  CHECK(arrow_scene.path().subpaths.size() == 2);
  if (arrow_scene.path().subpaths.size() == 2) {
    CHECK(rect_is(arrow_scene.bounds(0), 20.0, 160.0, 60.0, 190.0));
    CHECK(rect_is(arrow_scene.bounds(1), 160.0, 160.0, 200.0, 190.0));
  }
}

void ui_shape_drag_snap_leaves_direct_select_point_drags_alone() {
  ShapeSnapScene scene;
  scene.canvas->set_tool(patchy::ui::CanvasTool::DirectSelect);
  QApplication::processEvents();
  // The top-right point dragged to 197,160: 3 px from the target's edge, and it stays there.
  scene.drag_shape(QPoint(60, 160), QPoint(137, 0), Qt::NoModifier, /*release=*/false);
  CHECK(!scene.canvas->move_snap_match_x().has_value());
  CHECK(!scene.canvas->move_snap_match_y().has_value());
  scene.release(QPoint(60, 160), QPoint(137, 0));
  CHECK(rect_is(scene.bounds(), 20.0, 160.0, 197.0, 190.0, 0.01));
  bool moved_point = false;
  for (const auto& anchor : scene.path().subpaths[0].anchors) {
    moved_point = moved_point || (std::abs(anchor.anchor_x - 197.0) < 0.01 && std::abs(anchor.anchor_y - 160.0) < 0.01);
  }
  CHECK(moved_point);
}

void ui_shape_drag_snap_ignores_the_grab_point() {
  // The same shape dragged the same distance by its far corner point and by its fill near
  // the other corner lands the same: its right edge on the target's edge, whichever was held.
  ShapeSnapScene by_corner;
  by_corner.use_path_select();
  by_corner.drag_shape(QPoint(20, 160), QPoint(137, 0), Qt::NoModifier, /*release=*/false);
  const auto corner_match = by_corner.canvas->move_snap_match_x();
  by_corner.release(QPoint(20, 160), QPoint(137, 0));

  ShapeSnapScene by_fill;
  by_fill.use_path_select();
  by_fill.drag_shape(QPoint(50, 180), QPoint(137, 0), Qt::NoModifier, /*release=*/false);
  const auto fill_match = by_fill.canvas->move_snap_match_x();
  by_fill.release(QPoint(50, 180), QPoint(137, 0));

  CHECK(rect_is(by_corner.bounds(), 160.0, 160.0, 200.0, 190.0));
  CHECK(rect_is(by_fill.bounds(), 160.0, 160.0, 200.0, 190.0));
  CHECK(corner_match.has_value() && fill_match.has_value());
  if (corner_match.has_value() && fill_match.has_value()) {
    CHECK(corner_match->position == 200.0 && fill_match->position == 200.0);
    CHECK(corner_match->source_span == fill_match->source_span);
  }
}

// GitHub issue 695 follow-up: a live polygon dragged by its center point is a whole-shape
// drag too. The hexagon has radius 30, its center at 40,200 and its first vertex straight
// up: its bounds are 40 +/- 30 * cos(30 deg) by 170..230, so its edges sit off the pixel grid.
constexpr double kHexagonHalfWidth = 25.980762113533160;

bool hexagon_points_at(const patchy::ui::CanvasWidget& canvas, QPointF center) {
  const auto points = canvas.shape_live_points();
  return points.has_value() && points->kind == patchy::ui::CanvasWidget::ShapePointsKind::Polygon &&
         std::abs(points->first.x() - center.x()) < 1e-6 && std::abs(points->first.y() - center.y()) < 1e-6 &&
         std::abs(points->second.x() - center.x()) < 1e-6 && std::abs(points->second.y() - (center.y() - 30.0)) < 1e-6;
}

void ui_shape_drag_snap_polygon_center_point_lands_on_layer_edge_and_clears_guide() {
  SettingsValueRestorer saved_sides(QStringLiteral("tools/polygonSides"));
  SettingsValueRestorer saved_inset(QStringLiteral("tools/polygonStarInset"));
  ShapeSnapScene scene(QPoint(40, 200), QPoint(40, 170), /*hexagon=*/true);
  const auto& document = std::as_const(patchy::ui::MainWindowTestAccess::document(scene.window));
  const auto layer_count = document.layers().size();
  const double half = kHexagonHalfWidth;
  CHECK(rect_is(scene.bounds(), 40.0 - half, 170.0, 40.0 + half, 230.0));
  CHECK(hexagon_points_at(*scene.canvas, QPointF(40.0, 200.0)));

  // +131 by the center point: the right edge would stop at 196.98, 3.02 px short of the
  // target's left edge (200). The center is what is held and an edge is what lands: every
  // edge and the center of the bounds is a source, as in a Ctrl drag of the same shape.
  scene.drag_shape(QPoint(40, 200), QPoint(131, 0), Qt::NoModifier, /*release=*/false);
  CHECK(rect_is(scene.bounds(), 200.0 - 2.0 * half, 170.0, 200.0, 230.0));
  const auto& match_x = scene.canvas->move_snap_match_x();
  CHECK(match_x.has_value());
  if (match_x.has_value()) {
    CHECK(match_x->kind == patchy::ui::CanvasWidget::SnapMatch::Kind::Layer);
    CHECK(match_x->position == 200.0);
    // The guide bridges the target (top 60) and the hexagon where it landed (bottom 230).
    CHECK(match_x->target_span.top() == 60.0 && match_x->target_span.bottom() == 120.0);
    CHECK(rect_is(match_x->source_span, 200.0 - 2.0 * half, 170.0, 200.0, 230.0));
  }
  CHECK(!scene.canvas->move_snap_match_y().has_value());
  // Mid-drag the two points ride the snapped shape.
  CHECK(hexagon_points_at(*scene.canvas, QPointF(200.0 - half, 200.0)));

  const auto guide_point = scene.canvas->widget_position_for_document_point(QPoint(200, 140));
  const auto expected = patchy::ui::theme().canvas_snap_guide;
  const auto guide_drawn = [&] {
    const auto frame = scene.canvas->grab().toImage();
    for (int dx = -1; dx <= 1; ++dx) {
      if (color_close(frame.pixelColor(guide_point + QPoint(dx, 0)), expected, 40)) {
        return true;
      }
    }
    return false;
  };
  CHECK(guide_drawn());
  save_widget_artifact("ui_shape_drag_snap_polygon_center_point", *scene.canvas);

  scene.release(QPoint(40, 200), QPoint(131, 0));
  CHECK(!scene.canvas->move_snap_match_x().has_value());
  CHECK(!scene.canvas->move_snap_match_y().has_value());
  CHECK(!guide_drawn());
  CHECK(rect_is(scene.bounds(), 200.0 - 2.0 * half, 170.0, 200.0, 230.0));
  // Still a live hexagon on the same layer, with nothing drawn.
  CHECK(hexagon_points_at(*scene.canvas, QPointF(200.0 - half, 200.0)));
  CHECK(document.layers().size() == layer_count);
  CHECK(scene.canvas->tool() == patchy::ui::CanvasTool::Polygon);

  // One undo step takes the whole snapped move back.
  require_action_by_text(scene.window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(rect_is(scene.bounds(), 40.0 - half, 170.0, 40.0 + half, 230.0));
  CHECK(hexagon_points_at(*scene.canvas, QPointF(40.0, 200.0)));

  // Esc mid-drag drops the guide and puts the hexagon back.
  scene.drag_shape(QPoint(40, 200), QPoint(131, 0), Qt::NoModifier, /*release=*/false);
  CHECK(scene.canvas->move_snap_match_x().has_value());
  send_key(*scene.canvas, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(!scene.canvas->move_snap_match_x().has_value());
  CHECK(!guide_drawn());
  scene.release(QPoint(40, 200), QPoint(131, 0));
  CHECK(rect_is(scene.bounds(), 40.0 - half, 170.0, 40.0 + half, 230.0));

  // The vertex point is not a whole-shape drag: it shows no alignment guide.
  scene.drag_shape(QPoint(40, 170), QPoint(0, -15), Qt::NoModifier, /*release=*/false);
  CHECK(!scene.canvas->move_snap_match_x().has_value());
  CHECK(!scene.canvas->move_snap_match_y().has_value());
  scene.release(QPoint(40, 170), QPoint(0, -15));
  CHECK(document.layers().size() == layer_count);
}

void ui_shape_drag_snap_polygon_center_point_follows_guides_and_view_snap() {
  SettingsValueRestorer saved_sides(QStringLiteral("tools/polygonSides"));
  SettingsValueRestorer saved_inset(QStringLiteral("tools/polygonStarInset"));
  ShapeSnapScene scene(QPoint(40, 200), QPoint(40, 170), /*hexagon=*/true);
  const double half = kHexagonHalfWidth;
  scene.canvas->set_snap_to_guides(true);
  scene.canvas->add_guide(patchy::GuideOrientation::Vertical, 120 * 32);
  QApplication::processEvents();

  // +77: the center would stop at 117, 3 px short of the guide at 120, and lands on it.
  scene.drag_shape(QPoint(40, 200), QPoint(77, 0), Qt::NoModifier, /*release=*/false);
  CHECK(rect_is(scene.bounds(), 120.0 - half, 170.0, 120.0 + half, 230.0));
  const auto& match_x = scene.canvas->move_snap_match_x();
  CHECK(match_x.has_value());
  if (match_x.has_value()) {
    CHECK(match_x->kind == patchy::ui::CanvasWidget::SnapMatch::Kind::Guide);
    CHECK(match_x->position == 120.0);
  }
  CHECK(!scene.canvas->move_snap_match_y().has_value());
  scene.release(QPoint(40, 200), QPoint(77, 0));
  CHECK(rect_is(scene.bounds(), 120.0 - half, 170.0, 120.0 + half, 230.0));
  CHECK(!scene.canvas->move_snap_match_x().has_value());

  // With nothing in reach the hexagon moves by the pointer's travel on whole pixels.
  scene.drag_shape(QPoint(120, 200), QPoint(-50, 3), Qt::NoModifier, /*release=*/false);
  CHECK(!scene.canvas->move_snap_match_x().has_value());
  CHECK(!scene.canvas->move_snap_match_y().has_value());
  scene.release(QPoint(120, 200), QPoint(-50, 3));
  CHECK(rect_is(scene.bounds(), 70.0 - half, 173.0, 70.0 + half, 233.0));

  // View > Snap off: the center stops 3 px short of the guide, where the pointer took it.
  scene.canvas->set_snap_enabled(false);
  scene.drag_shape(QPoint(70, 203), QPoint(47, 0), Qt::NoModifier, /*release=*/false);
  CHECK(rect_is(scene.bounds(), 117.0 - half, 173.0, 117.0 + half, 233.0));
  CHECK(!scene.canvas->move_snap_match_x().has_value());
  CHECK(!scene.canvas->move_snap_match_y().has_value());
  scene.release(QPoint(70, 203), QPoint(47, 0));
  CHECK(rect_is(scene.bounds(), 117.0 - half, 173.0, 117.0 + half, 233.0));

  // Snap back on with the guide targets off (View > Snap To > Guides): away and back to
  // 3 px short of the guide, and still no pull.
  scene.canvas->set_snap_enabled(true);
  scene.canvas->set_snap_to_guides(false);
  scene.drag_shape(QPoint(117, 203), QPoint(-40, 0));
  CHECK(rect_is(scene.bounds(), 77.0 - half, 173.0, 77.0 + half, 233.0));
  scene.drag_shape(QPoint(77, 203), QPoint(40, 0), Qt::NoModifier, /*release=*/false);
  CHECK(!scene.canvas->move_snap_match_x().has_value());
  scene.release(QPoint(77, 203), QPoint(40, 0));
  CHECK(rect_is(scene.bounds(), 117.0 - half, 173.0, 117.0 + half, 233.0));

  // The guide targets back on: the same approach lands on the guide again.
  scene.canvas->set_snap_to_guides(true);
  scene.drag_shape(QPoint(117, 203), QPoint(-40, 0));
  scene.drag_shape(QPoint(77, 203), QPoint(40, 0));
  CHECK(rect_is(scene.bounds(), 120.0 - half, 173.0, 120.0 + half, 233.0));
}

void ui_shape_drag_snap_moves_a_work_path_too() {
  // A path with no layer (the work path a shape tool draws in Path mode) snaps the same way.
  ShapeSnapScene scene;
  auto* mode_combo = scene.window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  CHECK(mode_combo != nullptr);
  if (mode_combo == nullptr) {
    return;
  }
  mode_combo->setCurrentIndex(1);  // Path
  QApplication::processEvents();
  drag(*scene.canvas, scene.canvas->widget_position_for_document_point(QPoint(20, 220)),
       scene.canvas->widget_position_for_document_point(QPoint(60, 250)));
  QApplication::processEvents();
  const auto& document = std::as_const(patchy::ui::MainWindowTestAccess::document(scene.window));
  CHECK(document.work_path() != nullptr);
  if (document.work_path() == nullptr) {
    return;
  }
  scene.use_path_select();
  // Pressed on its outline, which picks the work path over the shape layer.
  scene.drag_shape(QPoint(40, 220), QPoint(137, 0), Qt::NoModifier, /*release=*/false);
  CHECK(scene.canvas->move_snap_match_x().has_value());
  scene.release(QPoint(40, 220), QPoint(137, 0));
  CHECK(document.work_path() != nullptr);
  if (document.work_path() == nullptr || document.work_path()->path().subpaths.empty()) {
    return;
  }
  double left = 1e9;
  double right = -1e9;
  for (const auto& anchor : document.work_path()->path().subpaths[0].anchors) {
    left = std::min(left, anchor.anchor_x);
    right = std::max(right, anchor.anchor_x);
  }
  CHECK(std::abs(left - 160.0) < 1e-6 && std::abs(right - 200.0) < 1e-6);
}

void ui_move_snap_checkbox_mirrors_view_snap_action() {
  SettingsValueRestorer saved_snap(QStringLiteral("view/snapEnabled"));
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* snap_check = window.findChild<QCheckBox*>(QStringLiteral("moveSnapCheck"));
  auto* snap_action = require_action(window, "viewToggleSnapAction");
  CHECK(snap_check != nullptr);
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  CHECK(snap_check->isVisible());
  CHECK(snap_check->isChecked() == snap_action->isChecked());

  snap_check->setChecked(!snap_action->isChecked());
  QApplication::processEvents();
  CHECK(snap_action->isChecked() == snap_check->isChecked());
  CHECK(canvas->snap_enabled() == snap_check->isChecked());
  CHECK(patchy::ui::app_settings().value(QStringLiteral("view/snapEnabled")).toBool() == snap_check->isChecked());

  snap_action->toggle();
  QApplication::processEvents();
  CHECK(snap_check->isChecked() == snap_action->isChecked());
  CHECK(canvas->snap_enabled() == snap_action->isChecked());

  // Hidden for other tools, like the rest of the Move row.
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  QApplication::processEvents();
  CHECK(!snap_check->isVisible());
}

// Three separate layers: A 20x20 at (10,10), B 30x10 at (60,40), C 20x30 at
// (100,90). Canvas 200x150.
struct AlignScene {
  patchy::ui::MainWindow window;
  patchy::ui::CanvasWidget* canvas{nullptr};
  QListWidget* layer_list{nullptr};

  AlignScene() {
    patchy::Document document(200, 150, patchy::PixelFormat::rgba8());
    document.add_layer(solid_layer(document, "A", patchy::Rect{10, 10, 20, 20}, QColor(220, 40, 40)));
    document.add_layer(solid_layer(document, "B", patchy::Rect{60, 40, 30, 10}, QColor(40, 90, 220)));
    document.add_layer(solid_layer(document, "C", patchy::Rect{100, 90, 20, 30}, QColor(40, 180, 90)));
    show_window(window);
    window.add_document_session(std::move(document), QStringLiteral("Align Scene"));
    QApplication::processEvents();
    canvas = require_canvas(window);
    layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
    CHECK(layer_list != nullptr);
  }

  [[nodiscard]] patchy::Rect bounds(const char* name) {
    const auto* layer = find_layer(window, name);
    return layer != nullptr ? layer->bounds() : patchy::Rect{};
  }
};

void ui_layer_align_buttons_align_selected_layers_with_one_undo() {
  AlignScene scene;
  select_layers(*scene.layer_list, {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")});
  require_action_by_text(scene.window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  const auto depth = patchy::ui::MainWindowTestAccess::active_session_undo_depth(scene.window);

  auto* left_button = scene.window.findChild<QToolButton*>(QStringLiteral("moveAlignLeftButton"));
  CHECK(left_button != nullptr && left_button->isVisible());
  CHECK(left_button->isEnabled());
  left_button->click();
  QApplication::processEvents();
  CHECK(scene.bounds("A").x == 10 && scene.bounds("B").x == 10 && scene.bounds("C").x == 10);
  CHECK(scene.bounds("A").y == 10 && scene.bounds("B").y == 40 && scene.bounds("C").y == 90);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(scene.window) == depth + 1);

  // Vertical centers: union 10..120 -> center 65.
  require_action(scene.window, "layerAlignVCenterAction")->trigger();
  QApplication::processEvents();
  CHECK(scene.bounds("A").y == 55 && scene.bounds("B").y == 60 && scene.bounds("C").y == 50);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(scene.window) == depth + 2);

  // Already aligned: no history entry, an informational status line.
  require_action(scene.window, "layerAlignLeftAction")->trigger();
  QApplication::processEvents();
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(scene.window) == depth + 2);
  CHECK(scene.window.statusBar()->currentMessage() == QStringLiteral("The selected layers are already aligned"));

  patchy::ui::MainWindowTestAccess::undo(scene.window);
  patchy::ui::MainWindowTestAccess::undo(scene.window);
  QApplication::processEvents();
  CHECK(scene.bounds("A").x == 10 && scene.bounds("B").x == 60 && scene.bounds("C").x == 100);
  CHECK(scene.bounds("A").y == 10 && scene.bounds("B").y == 40 && scene.bounds("C").y == 90);
  save_widget_artifact("ui_layer_align_options_bar", scene.window);
}

void ui_layer_align_single_layer_uses_canvas_and_selection_wins() {
  SettingsValueRestorer saved_align_to(QStringLiteral("tools/alignTo"));
  AlignScene scene;
  // One unit lines up with the canvas: right edge to 200, bottom to 150.
  select_layers(*scene.layer_list, {QStringLiteral("B")});
  require_action(scene.window, "layerAlignRightAction")->trigger();
  require_action(scene.window, "layerAlignBottomAction")->trigger();
  QApplication::processEvents();
  CHECK(scene.bounds("B").x == 170 && scene.bounds("B").y == 140);
  CHECK(scene.bounds("A").x == 10 && scene.bounds("C").x == 100);

  // A marquee selection is the reference for several units.
  select_layers(*scene.layer_list, {QStringLiteral("A"), QStringLiteral("C")});
  {
    patchy::ui::CanvasWidget::SelectionSnapshot snapshot;
    snapshot.selection = QRegion(QRect(30, 30, 100, 100));
    snapshot.display_region = snapshot.selection;
    scene.canvas->apply_selection_snapshot(snapshot);
    QApplication::processEvents();
  }
  require_action(scene.window, "layerAlignLeftAction")->trigger();
  require_action(scene.window, "layerAlignTopAction")->trigger();
  QApplication::processEvents();
  CHECK(scene.bounds("A").x == 30 && scene.bounds("C").x == 30);
  CHECK(scene.bounds("A").y == 30 && scene.bounds("C").y == 30);

  // Align To: Canvas ignores the selection.
  auto* to_canvas = require_action(scene.window, "layerAlignToCanvasAction");
  to_canvas->setChecked(true);
  QApplication::processEvents();
  CHECK(patchy::ui::MainWindowTestAccess::align_to_canvas(scene.window));
  CHECK(!require_action(scene.window, "layerAlignToSelectionAction")->isChecked());
  require_action(scene.window, "layerAlignHCenterAction")->trigger();
  QApplication::processEvents();
  CHECK(scene.bounds("A").x == 90 && scene.bounds("C").x == 90);
  require_action(scene.window, "layerAlignToSelectionAction")->setChecked(true);
  QApplication::processEvents();
  CHECK(!patchy::ui::MainWindowTestAccess::align_to_canvas(scene.window));
}

void ui_layer_align_to_is_exclusive_and_starts_at_selection() {
  // A settings file from a build that persisted Align To must not carry Canvas
  // into a new launch: every window starts at Selection. Loading the old value
  // with the actions' signals blocked is also what left the exclusive group
  // stale, so a later click on Selection showed both entries checked.
  SettingsValueRestorer saved_align_to(QStringLiteral("tools/alignTo"));
  patchy::ui::app_settings().setValue(QStringLiteral("tools/alignTo"), QStringLiteral("canvas"));
  AlignScene scene;
  auto* to_selection = require_action(scene.window, "layerAlignToSelectionAction");
  auto* to_canvas = require_action(scene.window, "layerAlignToCanvasAction");
  CHECK(to_selection->isChecked());
  CHECK(!to_canvas->isChecked());
  CHECK(!patchy::ui::MainWindowTestAccess::align_to_canvas(scene.window));

  // Menu clicks flip between the two, never leaving both (or neither) checked.
  for (int round = 0; round < 2; ++round) {
    to_canvas->trigger();
    QApplication::processEvents();
    CHECK(to_canvas->isChecked() && !to_selection->isChecked());
    CHECK(patchy::ui::MainWindowTestAccess::align_to_canvas(scene.window));
    to_selection->trigger();
    QApplication::processEvents();
    CHECK(to_selection->isChecked() && !to_canvas->isChecked());
    CHECK(!patchy::ui::MainWindowTestAccess::align_to_canvas(scene.window));
  }

  // The choice is session-only: saving tool settings does not write it back.
  to_canvas->trigger();
  QApplication::processEvents();
  patchy::ui::app_settings().remove(QStringLiteral("tools/alignTo"));
  patchy::ui::MainWindowTestAccess::save_tool_settings(scene.window);
  CHECK(!patchy::ui::app_settings().contains(QStringLiteral("tools/alignTo")));
}

void ui_layer_align_treats_folder_as_one_unit() {
  patchy::Document document(200, 150, patchy::PixelFormat::rgba8());
  patchy::Layer folder(document.allocate_layer_id(), "Folder", patchy::LayerKind::Group);
  folder.add_child(solid_layer(document, "A", patchy::Rect{10, 10, 20, 20}, QColor(220, 40, 40)));
  folder.add_child(solid_layer(document, "B", patchy::Rect{60, 40, 30, 10}, QColor(40, 90, 220)));
  document.add_layer(std::move(folder));
  document.add_layer(solid_layer(document, "C", patchy::Rect{100, 90, 20, 30}, QColor(40, 180, 90)));
  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Folder Align"));
  QApplication::processEvents();
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  select_layers(*layer_list, {QStringLiteral("Folder"), QStringLiteral("C")});

  // Two units: the folder block (10,10)-(90,50) and C. Distribute needs three.
  CHECK(require_action(window, "layerAlignBottomAction")->isEnabled());
  CHECK(!require_action(window, "layerDistributeLeftAction")->isEnabled());
  require_action(window, "layerAlignBottomAction")->trigger();
  QApplication::processEvents();
  // Union bottom = 120 (C). The folder moves as a block by +70; A and B keep
  // their relative positions; C stays.
  CHECK(find_layer(window, "A")->bounds().y == 80);
  CHECK(find_layer(window, "B")->bounds().y == 110);
  CHECK(find_layer(window, "C")->bounds().y == 90);
  CHECK(find_layer(window, "A")->bounds().x == 10 && find_layer(window, "B")->bounds().x == 60);
}

void ui_layer_distribute_requires_three_units_and_spaces_evenly() {
  AlignScene scene;
  select_layers(*scene.layer_list, {QStringLiteral("A"), QStringLiteral("B")});
  CHECK(!require_action(scene.window, "layerDistributeHCenterAction")->isEnabled());
  CHECK(require_action(scene.window, "layerAlignLeftAction")->isEnabled());
  const auto depth = patchy::ui::MainWindowTestAccess::active_session_undo_depth(scene.window);
  // The command itself refuses too (a hotkey could still reach a disabled state).
  patchy::ui::MainWindowTestAccess::distribute_selected_layers(scene.window, patchy::DistributeMode::Left);
  QApplication::processEvents();
  CHECK(scene.window.statusBar()->currentMessage() ==
        QStringLiteral("Select at least three layers to distribute"));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(scene.window) == depth);

  select_layers(*scene.layer_list, {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")});
  CHECK(require_action(scene.window, "layerDistributeHCenterAction")->isEnabled());
  // Left edges 10, 60, 100 -> B to 55.
  require_action(scene.window, "layerDistributeLeftAction")->trigger();
  QApplication::processEvents();
  CHECK(scene.bounds("A").x == 10 && scene.bounds("B").x == 55 && scene.bounds("C").x == 100);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(scene.window) == depth + 1);
  // Horizontal spacing: widths 20, 30, 20 in 10..120 -> gap 20 -> B at 50.
  require_action(scene.window, "layerDistributeHSpacingAction")->trigger();
  QApplication::processEvents();
  CHECK(scene.bounds("B").x == 50);
  // Vertical centers: 20, 45, 105 -> B center to 62.5 -> 63 (top 58).
  require_action(scene.window, "layerDistributeVCenterAction")->trigger();
  QApplication::processEvents();
  CHECK(scene.bounds("B").y == 58);

  // The six feature modes are buttons on the Move tool's options bar (GitHub
  // issue 172) that run the same one-shot commands; the spacing pair and Align
  // To stay in the gear menu.
  require_action_by_text(scene.window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  auto* distribute_left = scene.window.findChild<QToolButton*>(QStringLiteral("moveDistributeLeftButton"));
  CHECK(distribute_left != nullptr && distribute_left->isVisible() && distribute_left->isEnabled());
  CHECK(distribute_left != nullptr && !distribute_left->icon().isNull());
  CHECK(distribute_left != nullptr &&
        distribute_left->toolTip().startsWith(require_action(scene.window, "layerDistributeLeftAction")->toolTip()));
  // Left edges 10, 50, 100 -> B to 55, one undo entry.
  const auto button_depth = patchy::ui::MainWindowTestAccess::active_session_undo_depth(scene.window);
  if (distribute_left != nullptr) {
    distribute_left->click();
  }
  QApplication::processEvents();
  CHECK(scene.bounds("A").x == 10 && scene.bounds("B").x == 55 && scene.bounds("C").x == 100);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(scene.window) == button_depth + 1);
  auto* more_button = scene.window.findChild<QToolButton*>(QStringLiteral("moveAlignMoreButton"));
  CHECK(more_button != nullptr && more_button->isVisible() && more_button->menu() != nullptr);
  if (more_button != nullptr && more_button->menu() != nullptr) {
    const auto actions = more_button->menu()->actions();
    CHECK(actions.contains(require_action(scene.window, "layerDistributeHSpacingAction")));
    CHECK(actions.contains(require_action(scene.window, "layerDistributeVSpacingAction")));
    CHECK(actions.contains(require_action(scene.window, "layerAlignToCanvasAction")));
    CHECK(!actions.contains(require_action(scene.window, "layerDistributeLeftAction")));
  }
  // GitHub issue 405: the enabled Align and Distribute buttons in both schemes, cropped
  // from the first Align button to the gear menu button.
  auto* options_bar = scene.window.findChild<QToolBar*>(QStringLiteral("Options"));
  auto* first_align = scene.window.findChild<QToolButton*>(QStringLiteral("moveAlignLeftButton"));
  CHECK(options_bar != nullptr && first_align != nullptr && more_button != nullptr);
  if (options_bar != nullptr && first_align != nullptr && more_button != nullptr) {
    const auto align_distribute_strip = [&] {
      QApplication::processEvents();
      const QPoint top_left = first_align->mapTo(options_bar, QPoint(0, 0));
      const QPoint bottom_right =
          more_button->mapTo(options_bar, QPoint(more_button->width(), more_button->height()));
      const QRect crop = QRect(top_left, bottom_right).adjusted(-6, -3, 6, 3).intersected(options_bar->rect());
      return options_bar->grab(crop).toImage();
    };
    const QImage dark_strip = align_distribute_strip();
    ColorSchemeRestorer restore_scheme(patchy::ui::ColorSchemePreference::Light);
    const QImage light_strip = align_distribute_strip();
    save_stacked_artifact("ui_issue_405_align_distribute", {dark_strip, light_strip});
  }
  // The buttons follow their action's enabled state: two units cannot distribute.
  select_layers(*scene.layer_list, {QStringLiteral("A"), QStringLiteral("B")});
  QApplication::processEvents();
  CHECK(distribute_left != nullptr && !distribute_left->isEnabled());
}

void ui_layer_align_refuses_during_transform_lock_and_gesture() {
  AlignScene scene;
  select_layers(*scene.layer_list, {QStringLiteral("A"), QStringLiteral("B")});
  const auto depth = patchy::ui::MainWindowTestAccess::active_session_undo_depth(scene.window);

  require_action(scene.window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(scene.canvas->free_transform_active());
  require_action(scene.window, "layerAlignLeftAction")->trigger();
  QApplication::processEvents();
  CHECK(scene.window.statusBar()->currentMessage() ==
        QStringLiteral("Finish the transform first: press Enter to apply it or Esc to cancel it"));
  CHECK(scene.bounds("B").x == 60);
  send_key(*scene.canvas, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(!scene.canvas->free_transform_active());

  // A live Move drag refuses too.
  require_action_by_text(scene.window, QStringLiteral("Move"))->trigger();
  scene.canvas->set_auto_select_layer(false);
  scene.canvas->set_show_transform_controls(false);
  QApplication::processEvents();
  const auto start = scene.canvas->widget_position_for_document_point(QPoint(70, 45));
  send_mouse(*scene.canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*scene.canvas, QEvent::MouseMove, start + QPoint(30, 0), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  CHECK(scene.canvas->pointer_gesture_active());
  patchy::ui::MainWindowTestAccess::align_selected_layers(scene.window, patchy::AlignEdge::Left);
  QApplication::processEvents();
  CHECK(scene.window.statusBar()->currentMessage() == QStringLiteral("Finish the current drag before aligning layers"));
  send_mouse(*scene.canvas, QEvent::MouseButtonRelease, start + QPoint(30, 0), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  // The drag moved B by 30 (one entry); the refused align added nothing.
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(scene.window) == depth + 1);
  CHECK(scene.bounds("A").x == 40 && scene.bounds("B").x == 90);
}

void ui_layer_arrange_menu_hosts_align_and_distribute() {
  patchy::ui::MainWindow window;
  show_window_empty(window);
  auto* layer_menu = window.findChild<QMenu*>(QStringLiteral("layerMenu"));
  auto* arrange_menu = window.findChild<QMenu*>(QStringLiteral("layerArrangeMenu"));
  auto* align_menu = window.findChild<QMenu*>(QStringLiteral("layerAlignMenu"));
  auto* distribute_menu = window.findChild<QMenu*>(QStringLiteral("layerDistributeMenu"));
  CHECK(layer_menu != nullptr && arrange_menu != nullptr && align_menu != nullptr && distribute_menu != nullptr);
  const auto layer_menu_rows = layer_menu->actions();
  CHECK(std::count_if(layer_menu_rows.begin(), layer_menu_rows.end(),
                      [](const QAction* action) { return !action->isSeparator(); }) <= 22);
  CHECK(arrange_menu->actions().contains(align_menu->menuAction()));
  CHECK(arrange_menu->actions().contains(distribute_menu->menuAction()));
  const char* const align_names[] = {"layerAlignLeftAction",   "layerAlignHCenterAction", "layerAlignRightAction",
                                     "layerAlignTopAction",    "layerAlignVCenterAction", "layerAlignBottomAction",
                                     "layerAlignToSelectionAction", "layerAlignToCanvasAction"};
  for (const auto* name : align_names) {
    auto* action = require_action(window, name);
    CHECK(align_menu->actions().contains(action));
    CHECK(action->shortcut().isEmpty());
  }
  const char* const distribute_names[] = {"layerDistributeLeftAction",     "layerDistributeHCenterAction",
                                          "layerDistributeRightAction",    "layerDistributeTopAction",
                                          "layerDistributeVCenterAction",  "layerDistributeBottomAction",
                                          "layerDistributeHSpacingAction", "layerDistributeVSpacingAction"};
  for (const auto* name : distribute_names) {
    CHECK(distribute_menu->actions().contains(require_action(window, name)));
  }
  CHECK(require_action(window, "layerAlignToSelectionAction")->isChecked());
  // Without a document every command is disabled; the default one-layer
  // document enables Align (one unit lines up with the canvas) but not Distribute.
  CHECK(!require_action(window, "layerAlignLeftAction")->isEnabled());
  CHECK(!require_action(window, "layerDistributeLeftAction")->isEnabled());
  patchy::ui::MainWindowTestAccess::create_default_document(window);
  QApplication::processEvents();
  CHECK(require_action(window, "layerAlignLeftAction")->isEnabled());
  CHECK(!require_action(window, "layerDistributeLeftAction")->isEnabled());

  // The options-bar row keeps its height with the Move controls shown.
  auto* options_bar = window.findChild<QToolBar*>(QStringLiteral("Options"));
  CHECK(options_bar != nullptr);
  // The Marquee row is a single 26 px control row; the Move row must not wrap
  // or grow past it with the Snap check, the align and distribute buttons, and the gear menu button.
  require_action_by_text(window, QStringLiteral("Marquee"))->trigger();
  QApplication::processEvents();
  const int marquee_height = options_bar->height();
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  CHECK(options_bar->height() == marquee_height);
  for (const auto* name : {"moveAlignLeftButton", "moveAlignHCenterButton", "moveAlignRightButton",
                           "moveAlignTopButton", "moveAlignVCenterButton", "moveAlignBottomButton",
                           "moveDistributeLeftButton", "moveDistributeHCenterButton", "moveDistributeRightButton",
                           "moveDistributeTopButton", "moveDistributeVCenterButton", "moveDistributeBottomButton",
                           "moveAlignMoreButton"}) {
    auto* button = window.findChild<QToolButton*>(QLatin1String(name));
    CHECK(button != nullptr && button->isVisible());
    if (button != nullptr && QByteArray(name) != "moveAlignMoreButton") {
      CHECK(button->height() <= options_bar->height());
    }
  }
  for (std::size_t i = 0; i < 6; ++i) {
    CHECK(!require_action(window, align_names[i])->icon().isNull());
  }
  // Every Distribute glyph renders and the twelve Align/Distribute glyphs differ.
  std::vector<QImage> glyphs;
  for (const auto* name : align_names) {
    const auto icon = require_action(window, name)->icon();
    if (!icon.isNull()) {
      glyphs.push_back(icon.pixmap(QSize(18, 18)).toImage());
    }
  }
  for (const auto* name : distribute_names) {
    const auto icon = require_action(window, name)->icon();
    CHECK(!icon.isNull());
    glyphs.push_back(icon.pixmap(QSize(18, 18)).toImage());
  }
  CHECK(glyphs.size() == 14U);
  for (std::size_t i = 0; i < glyphs.size(); ++i) {
    for (std::size_t j = i + 1; j < glyphs.size(); ++j) {
      CHECK(glyphs[i] != glyphs[j]);
    }
  }
  // GitHub issue 405: the fourteen glyphs side by side at the options bar's 18 px size,
  // rendered for 100%, 150% and 200% display scaling, one row per scale and scheme.
  {
    std::vector<QAction*> glyph_actions;
    for (const auto* name : align_names) {
      if (auto* action = require_action(window, name); !action->icon().isNull()) {
        glyph_actions.push_back(action);
      }
    }
    for (const auto* name : distribute_names) {
      glyph_actions.push_back(require_action(window, name));
    }
    CHECK(glyph_actions.size() == 14U);
    // The glyphs sit on whole device pixels at every scale: each pixel is fully
    // inked or fully clear, never a half-covered antialiased edge.
    for (const qreal ratio : {1.0, 1.25, 1.5, 2.0}) {
      for (auto* action : glyph_actions) {
        const QImage glyph =
            action->icon().pixmap(QSize(18, 18), ratio).toImage().convertToFormat(QImage::Format_ARGB32);
        int partial = 0;
        int inked = 0;
        for (int y = 0; y < glyph.height(); ++y) {
          for (int x = 0; x < glyph.width(); ++x) {
            const int alpha = qAlpha(glyph.pixel(x, y));
            partial += alpha != 0 && alpha != 255 ? 1 : 0;
            inked += alpha == 255 ? 1 : 0;
          }
        }
        if (partial != 0 || inked == 0) {
          std::fprintf(stderr, "  %s at %.2fx: %d partial, %d inked pixels\n",
                       qPrintable(action->objectName()), ratio, partial, inked);
        }
        CHECK(glyph.width() == qRound(18 * ratio));
        CHECK(partial == 0 && inked > 0);
      }
    }
    const auto glyph_rows = [&] {
      QList<QImage> rows;
      for (const qreal ratio : {1.0, 1.5, 2.0}) {
        const int cell = qRound(18 * ratio) + 8;
        QImage row(static_cast<int>(glyph_actions.size()) * cell, cell, QImage::Format_ARGB32_Premultiplied);
        row.fill(patchy::ui::theme().options_bar_bg);
        QPainter painter(&row);
        for (std::size_t i = 0; i < glyph_actions.size(); ++i) {
          QImage glyph = glyph_actions[i]->icon().pixmap(QSize(18, 18), ratio).toImage();
          glyph.setDevicePixelRatio(1.0);
          painter.drawImage(QPoint(static_cast<int>(i) * cell + 4, 4), glyph);
        }
        painter.end();
        rows.push_back(row);
      }
      return rows;
    };
    QList<QImage> sheet = glyph_rows();
    {
      ColorSchemeRestorer restore_scheme(patchy::ui::ColorSchemePreference::Light);
      sheet.append(glyph_rows());
    }
    save_stacked_artifact("ui_issue_405_align_distribute_glyphs", sheet);
  }
  // GitHub issue 211: a separator sets Auto-Nest Layers apart from the buttons before it,
  // on the Move bar (the gear menu button) and on the Artboard bar (Add Artboard).
  const auto separated = [&](const char* before_name) {
    auto* before = window.findChild<QWidget*>(QLatin1String(before_name));
    auto* nest = window.findChild<QWidget*>(QStringLiteral("artboardAutoNestCheck"));
    CHECK(before != nullptr && before->isVisible() && nest != nullptr && nest->isVisible());
    if (before == nullptr || nest == nullptr) {
      return false;
    }
    const QPoint before_right = before->mapTo(options_bar, QPoint(before->width(), 0));
    const QPoint nest_left = nest->mapTo(options_bar, QPoint(0, 0));
    for (auto* line : options_bar->findChildren<QFrame*>(QStringLiteral("optionSeparator"))) {
      const QPoint at = line->mapTo(options_bar, QPoint(0, 0));
      if (line->isVisible() && at.x() >= before_right.x() && at.x() + line->width() <= nest_left.x() &&
          std::abs(at.y() - nest_left.y()) < options_bar->height()) {
        return true;
      }
    }
    return false;
  };
  CHECK(separated("moveAlignMoreButton"));
  save_widget_artifact("ui_move_options_bar_align_distribute", *options_bar);
  require_hotkey_action(window, QStringLiteral("tools.artboard"))->trigger();
  QApplication::processEvents();
  CHECK(separated("artboardAddButton"));
  save_widget_artifact("ui_artboard_options_bar_auto_nest", *options_bar);
}

}  // namespace

std::vector<patchy::test::TestCase> alignment_tests() {
  return {
      {"ui_move_snap_targets_use_opaque_bounds_and_skip_moving_layers",
       ui_move_snap_targets_use_opaque_bounds_and_skip_moving_layers},
      {"ui_move_snap_draws_alignment_guide_and_clears_on_release",
       ui_move_snap_draws_alignment_guide_and_clears_on_release},
      {"ui_move_snap_shift_constraint_drops_pinned_axis_guide", ui_move_snap_shift_constraint_drops_pinned_axis_guide},
      {"ui_shape_drag_snap_path_select_lands_on_layer_edge_and_clears_guide",
       ui_shape_drag_snap_path_select_lands_on_layer_edge_and_clears_guide},
      {"ui_shape_drag_snap_ctrl_in_shape_tool_lands_on_layer_edge",
       ui_shape_drag_snap_ctrl_in_shape_tool_lands_on_layer_edge},
      {"ui_shape_drag_snap_center_lands_on_document_center", ui_shape_drag_snap_center_lands_on_document_center},
      {"ui_shape_drag_snap_follows_view_snap", ui_shape_drag_snap_follows_view_snap},
      {"ui_shape_drag_snap_shift_drops_pinned_axis_guide", ui_shape_drag_snap_shift_drops_pinned_axis_guide},
      {"ui_shape_drag_snap_alt_copy_lands_on_layer_edge", ui_shape_drag_snap_alt_copy_lands_on_layer_edge},
      {"ui_shape_drag_snap_leaves_direct_select_point_drags_alone",
       ui_shape_drag_snap_leaves_direct_select_point_drags_alone},
      {"ui_shape_drag_snap_ignores_the_grab_point", ui_shape_drag_snap_ignores_the_grab_point},
      {"ui_shape_drag_snap_polygon_center_point_lands_on_layer_edge_and_clears_guide",
       ui_shape_drag_snap_polygon_center_point_lands_on_layer_edge_and_clears_guide},
      {"ui_shape_drag_snap_polygon_center_point_follows_guides_and_view_snap",
       ui_shape_drag_snap_polygon_center_point_follows_guides_and_view_snap},
      {"ui_shape_drag_snap_moves_a_work_path_too", ui_shape_drag_snap_moves_a_work_path_too},
      {"ui_move_snap_checkbox_mirrors_view_snap_action", ui_move_snap_checkbox_mirrors_view_snap_action},
      {"ui_layer_align_buttons_align_selected_layers_with_one_undo",
       ui_layer_align_buttons_align_selected_layers_with_one_undo},
      {"ui_layer_align_single_layer_uses_canvas_and_selection_wins",
       ui_layer_align_single_layer_uses_canvas_and_selection_wins},
      {"ui_layer_align_to_is_exclusive_and_starts_at_selection",
       ui_layer_align_to_is_exclusive_and_starts_at_selection},
      {"ui_layer_align_treats_folder_as_one_unit", ui_layer_align_treats_folder_as_one_unit},
      {"ui_layer_distribute_requires_three_units_and_spaces_evenly",
       ui_layer_distribute_requires_three_units_and_spaces_evenly},
      {"ui_layer_align_refuses_during_transform_lock_and_gesture",
       ui_layer_align_refuses_during_transform_lock_and_gesture},
      {"ui_layer_arrange_menu_hosts_align_and_distribute", ui_layer_arrange_menu_hosts_align_and_distribute},
  };
}
