// Move-tool alignment guides (snap targets, the magenta overlay, the Snap
// checkbox) and the Layer > Arrange > Align / Distribute commands with their
// options-bar buttons. See docs/alignment.md.

#include "ui_test_support.hpp"

#include "ui_test_groups.hpp"

#include "core/layer_alignment.hpp"
#include "ui/theme_palette.hpp"
#include "ui/ui_test_access.hpp"

#include <QListWidget>
#include <QMenu>
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

  // The "..." menu on the Move tool's options bar carries the same actions.
  require_action_by_text(scene.window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  auto* more_button = scene.window.findChild<QToolButton*>(QStringLiteral("moveAlignMoreButton"));
  CHECK(more_button != nullptr && more_button->isVisible() && more_button->menu() != nullptr);
  if (more_button != nullptr && more_button->menu() != nullptr) {
    const auto actions = more_button->menu()->actions();
    CHECK(actions.contains(require_action(scene.window, "layerDistributeVSpacingAction")));
    CHECK(actions.contains(require_action(scene.window, "layerAlignToCanvasAction")));
  }
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
  CHECK(layer_menu->actions().size() <= 23);
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
  // or grow past it with the Snap check, the six align buttons, and "...".
  require_action_by_text(window, QStringLiteral("Marquee"))->trigger();
  QApplication::processEvents();
  const int marquee_height = options_bar->height();
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  CHECK(options_bar->height() == marquee_height);
  for (const auto* name : {"moveAlignLeftButton", "moveAlignHCenterButton", "moveAlignRightButton",
                           "moveAlignTopButton", "moveAlignVCenterButton", "moveAlignBottomButton",
                           "moveAlignMoreButton"}) {
    auto* button = window.findChild<QToolButton*>(QLatin1String(name));
    CHECK(button != nullptr && button->isVisible());
  }
  for (std::size_t i = 0; i < 6; ++i) {
    CHECK(!require_action(window, align_names[i])->icon().isNull());
  }
}

}  // namespace

std::vector<patchy::test::TestCase> alignment_tests() {
  return {
      {"ui_move_snap_targets_use_opaque_bounds_and_skip_moving_layers",
       ui_move_snap_targets_use_opaque_bounds_and_skip_moving_layers},
      {"ui_move_snap_draws_alignment_guide_and_clears_on_release",
       ui_move_snap_draws_alignment_guide_and_clears_on_release},
      {"ui_move_snap_shift_constraint_drops_pinned_axis_guide", ui_move_snap_shift_constraint_drops_pinned_axis_guide},
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
