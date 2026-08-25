// Point-editing discoverability and targeting: tool activation and hover
// hints, the status-bar point-count chip, the Pen's Auto Add/Delete
// option, Delete with a Ctrl-selected anchor under the Pen, the right-click
// path menu, the dedicated anchor tools in the Pen flyout, and the Paths
// panel retargeting when a layer with its own path becomes active.
#include "ui_test_support.hpp"

#include "core/vector_shape.hpp"
#include "ui/app_settings.hpp"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>
#include <QToolButton>

#include <cmath>
#include <cstddef>

using namespace patchy::test::ui;

namespace {

void shape_drag(patchy::ui::CanvasWidget& canvas, QPoint document_from, QPoint document_to) {
  drag(canvas, canvas.widget_position_for_document_point(document_from),
       canvas.widget_position_for_document_point(document_to));
  QApplication::processEvents();
}

void pen_click(patchy::ui::CanvasWidget& canvas, QPoint document_point) {
  const auto widget_point = canvas.widget_position_for_document_point(document_point);
  drag(canvas, widget_point, widget_point);
  QApplication::processEvents();
}

// A 200x120 rectangle shape layer with corner anchors at (100,100), (300,100),
// (300,220), (100,220).
patchy::LayerId make_rect_shape_layer(patchy::ui::MainWindow& window,
                                      patchy::ui::CanvasWidget& canvas) {
  canvas.set_tool(patchy::ui::CanvasTool::Rectangle);
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  CHECK(radius_spin != nullptr);
  radius_spin->setValue(0);
  shape_drag(canvas, QPoint(100, 100), QPoint(300, 220));
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto active = document.active_layer_id();
  CHECK(active.has_value());
  return *active;
}

// Hover moves carry the event's own modifiers: the offscreen platform never
// clears the global keyboard state.
QCursor hover(patchy::ui::CanvasWidget& canvas, QPoint document_point,
              Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  send_mouse(canvas, QEvent::MouseMove, canvas.widget_position_for_document_point(document_point),
             Qt::NoButton, Qt::NoButton, modifiers);
  QApplication::processEvents();
  return canvas.cursor();
}

std::size_t anchor_count(patchy::Document& document, patchy::LayerId id) {
  const auto* layer = std::as_const(document).find_layer(id);
  CHECK(layer != nullptr);
  CHECK(layer->vector_shape() != nullptr);
  CHECK(!layer->vector_shape()->path.subpaths.empty());
  return layer->vector_shape()->path.subpaths[0].anchors.size();
}

const patchy::PathAnchor& anchor_at(patchy::Document& document, patchy::LayerId id,
                                    std::size_t index) {
  const auto* layer = std::as_const(document).find_layer(id);
  CHECK(layer != nullptr);
  return layer->vector_shape()->path.subpaths[0].anchors[index];
}

// True when a pixel near the document point reads as the path-overlay accent
// (116, 192, 255) in a canvas grab.
bool accent_overlay_near(patchy::ui::CanvasWidget& canvas, QPoint document_point) {
  const auto image = canvas.grab().toImage();
  const auto center = canvas.widget_position_for_document_point(document_point);
  for (int dy = -3; dy <= 3; ++dy) {
    for (int dx = -3; dx <= 3; ++dx) {
      const QPoint probe(center.x() + dx, center.y() + dy);
      if (!image.rect().contains(probe)) {
        continue;
      }
      const auto color = image.pixelColor(probe);
      if (color.blue() >= 200 && color.blue() - color.red() >= 60 && color.green() > color.red()) {
        return true;
      }
    }
  }
  return false;
}

// Counts paint events: grab() repaints on its own, so a stale on-screen frame
// (the bug: no repaint after a layer activation) needs a direct witness.
class PaintCounter final : public QObject {
public:
  int paints{0};

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() == QEvent::Paint) {
      ++paints;
    }
    return QObject::eventFilter(watched, event);
  }
};

// Runs `act` on the canvas path menu once it opens; menu.exec blocks until
// the callback closes it.
template <typename Act>
void with_path_context_menu(Act act) {
  QTimer::singleShot(0, [act] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* menu = qobject_cast<QMenu*>(widget);
      if (menu == nullptr || menu->objectName() != QStringLiteral("canvasPathContextMenu")) {
        continue;
      }
      act(*menu);
      menu->close();
      return;
    }
    CHECK(false);
  });
}

void open_path_menu_at(patchy::ui::CanvasWidget& canvas, QPoint document_point) {
  const auto widget_point = canvas.widget_position_for_document_point(document_point);
  CHECK(canvas.show_path_context_menu(QPointF(widget_point), canvas.mapToGlobal(widget_point)));
  QApplication::processEvents();
}

QAction* require_menu_action(QMenu& menu, const char* object_name) {
  for (auto* action : menu.actions()) {
    if (action->objectName() == QLatin1String(object_name)) {
      return action;
    }
  }
  CHECK(false);
  return nullptr;
}

void ui_path_tools_show_activation_hints_and_tooltips() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);

  require_action(window, "toolPenAction")->trigger();
  QApplication::processEvents();
  CHECK(window.statusBar()->currentMessage().startsWith(QStringLiteral("Pen: click to add points")));
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("Alt+click converts")));
  require_action(window, "toolPathSelectAction")->trigger();
  QApplication::processEvents();
  CHECK(window.statusBar()->currentMessage().startsWith(
      QStringLiteral("Path Select: click a shape to select it")));
  require_action(window, "toolDirectSelectAction")->trigger();
  QApplication::processEvents();
  CHECK(window.statusBar()->currentMessage().startsWith(
      QStringLiteral("Direct Select: click or marquee points")));
  require_action(window, "toolAddAnchorAction")->trigger();
  QApplication::processEvents();
  CHECK(window.statusBar()->currentMessage() ==
        QStringLiteral("Add Anchor Point: click a path segment to insert a point."));
  // Tools without a hint keep showing their name.
  require_action(window, "toolBrushAction")->trigger();
  QApplication::processEvents();
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Brush"));

  // Tooltips name the gestures; flyout buttons copy their default action's.
  CHECK(require_action(window, "toolPenAction")->toolTip().contains(QStringLiteral("Alt+click")));
  CHECK(require_action(window, "toolDirectSelectAction")->toolTip().startsWith(
      QStringLiteral("Direct Select (Shift+A)")));
  auto* path_button = window.findChild<QToolButton*>(QStringLiteral("pathSelectToolButton"));
  CHECK(path_button != nullptr);
  // The flyout button shows its current default action's tooltip (Direct
  // Select was picked last); re-picking Path Select swaps it back.
  CHECK(path_button->toolTip().startsWith(QStringLiteral("Direct Select (Shift+A)")));
  require_action(window, "toolPathSelectAction")->trigger();
  QApplication::processEvents();
  CHECK(path_button->toolTip().startsWith(QStringLiteral("Path Select (A)")));
  CHECK(path_button->toolTip().contains(QStringLiteral("Ctrl+T")));
}

void ui_pen_hover_over_path_shows_edit_hints() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  make_rect_shape_layer(window, *canvas);
  canvas->set_tool(patchy::ui::CanvasTool::Pen);
  canvas->setFocus();

  // Plain hovering never touches the status bar.
  window.statusBar()->showMessage(QStringLiteral("marker"));
  hover(*canvas, QPoint(500, 400));
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("marker"));

  hover(*canvas, QPoint(200, 100));  // top-edge segment
  CHECK(window.statusBar()->currentMessage() ==
        QStringLiteral("Click to add a point here. Ctrl-drag moves the segment."));
  hover(*canvas, QPoint(100, 100));  // anchor
  CHECK(window.statusBar()->currentMessage().startsWith(
      QStringLiteral("Click to delete this point. Ctrl-drag moves it. Alt+click converts it")));
  hover(*canvas, QPoint(100, 100), Qt::AltModifier);
  CHECK(window.statusBar()->currentMessage() ==
        QStringLiteral("Click to convert this point between corner and smooth"));

  // Leaving the path keeps whatever the status bar showed last...
  window.statusBar()->showMessage(QStringLiteral("confirmation"));
  hover(*canvas, QPoint(500, 400));
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("confirmation"));
  // ...and re-entering advertises the action again.
  hover(*canvas, QPoint(200, 100));
  CHECK(window.statusBar()->currentMessage() ==
        QStringLiteral("Click to add a point here. Ctrl-drag moves the segment."));

  // Direct Select hovers describe the drag targets.
  require_action(window, "toolDirectSelectAction")->trigger();
  QApplication::processEvents();
  hover(*canvas, QPoint(500, 400));
  hover(*canvas, QPoint(100, 100));
  CHECK(window.statusBar()->currentMessage().startsWith(QStringLiteral("Drag to move the point")));
  hover(*canvas, QPoint(200, 100));
  CHECK(window.statusBar()->currentMessage().startsWith(QStringLiteral("Drag to move the segment")));
  require_action(window, "toolPathSelectAction")->trigger();
  QApplication::processEvents();
  hover(*canvas, QPoint(500, 400));
  hover(*canvas, QPoint(200, 100));
  CHECK(window.statusBar()->currentMessage().startsWith(
      QStringLiteral("Click to select the shape, drag to move it")));
}

void ui_path_tools_options_bar_shows_tool_options() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);

  // Only the raster Background exists: the appearance controls hide. The old
  // per-tool gesture label is gone (hints live in the status bar now).
  require_action(window, "toolDirectSelectAction")->trigger();
  QApplication::processEvents();
  CHECK(window.findChild<QLabel*>(QStringLiteral("pathToolHintLabel")) == nullptr);
  auto* fill_swatch = window.findChild<QToolButton*>(QStringLiteral("vectorFillSwatchButton"));
  CHECK(fill_swatch != nullptr);
  CHECK(!fill_swatch->isVisible());

  require_action(window, "toolPenAction")->trigger();
  QApplication::processEvents();
  auto* auto_add_delete = window.findChild<QCheckBox*>(QStringLiteral("penAutoAddDeleteCheck"));
  CHECK(auto_add_delete != nullptr);
  CHECK(auto_add_delete->isVisible());

  require_action(window, "toolBrushAction")->trigger();
  QApplication::processEvents();
  CHECK(!auto_add_delete->isVisible());
}

void ui_pen_auto_add_delete_off_starts_new_path_over_segment() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = make_rect_shape_layer(window, *canvas);
  const auto layers_before = document.layers().size();

  require_action(window, "toolPenAction")->trigger();
  QApplication::processEvents();
  auto* auto_add_delete = window.findChild<QCheckBox*>(QStringLiteral("penAutoAddDeleteCheck"));
  CHECK(auto_add_delete != nullptr);
  CHECK(auto_add_delete->isChecked());
  CHECK(canvas->pen_auto_add_delete());

  auto_add_delete->setChecked(false);
  QApplication::processEvents();
  CHECK(!canvas->pen_auto_add_delete());
  // The segment no longer advertises an edit: plain crosshair, and a click
  // starts a new path instead of inserting.
  const auto plain = hover(*canvas, QPoint(500, 400)).pixmap().toImage();
  CHECK(hover(*canvas, QPoint(200, 100)).pixmap().toImage() == plain);
  pen_click(*canvas, QPoint(200, 100));
  CHECK(canvas->pen_session_active());
  CHECK(anchor_count(document, layer_id) == 4);
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(!canvas->pen_session_active());

  // Back on: the same click inserts a point.
  auto_add_delete->setChecked(true);
  QApplication::processEvents();
  CHECK(hover(*canvas, QPoint(200, 100)).pixmap().toImage() != plain);
  pen_click(*canvas, QPoint(200, 100));
  CHECK(!canvas->pen_session_active());
  CHECK(anchor_count(document, layer_id) == 5);
  CHECK(document.layers().size() == layers_before);

  // The setting persists and seeds new sessions.
  auto_add_delete->setChecked(false);
  QApplication::processEvents();
  const auto key = QStringLiteral("tools/penAutoAddDelete");
  CHECK(process_events_until(
      [&] { return patchy::ui::app_settings().contains(key) && !patchy::ui::app_settings().value(key).toBool(); },
      3000));
  window.add_document_session(patchy::Document(300, 200, patchy::PixelFormat::rgb8()),
                              QStringLiteral("Second"));
  QApplication::processEvents();
  auto* second = require_canvas(window);
  CHECK(second != canvas);
  CHECK(!second->pen_auto_add_delete());
}

void ui_pen_deletes_ctrl_selected_anchors_with_delete_key() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = make_rect_shape_layer(window, *canvas);

  canvas->set_tool(patchy::ui::CanvasTool::Pen);
  canvas->setFocus();
  const auto corner = canvas->widget_position_for_document_point(QPoint(100, 100));
  drag(*canvas, corner, corner, Qt::ControlModifier);
  QApplication::processEvents();
  CHECK(canvas->path_edit_has_selection());
  CHECK(anchor_count(document, layer_id) == 4);

  // The canvas claims Delete from the app-level layer.clear shortcut while
  // anchors are selected, then removes them.
  QKeyEvent override_event(QEvent::ShortcutOverride, Qt::Key_Delete, Qt::NoModifier);
  QApplication::sendEvent(canvas, &override_event);
  CHECK(override_event.isAccepted());
  send_key(*canvas, Qt::Key_Delete);
  QApplication::processEvents();
  CHECK(anchor_count(document, layer_id) == 3);
  CHECK(!canvas->path_edit_has_selection());
  CHECK(!canvas->pen_session_active());

  // Escape clears a fresh Ctrl selection without touching the path.
  const auto other = canvas->widget_position_for_document_point(QPoint(300, 220));
  drag(*canvas, other, other, Qt::ControlModifier);
  QApplication::processEvents();
  CHECK(canvas->path_edit_has_selection());
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(!canvas->path_edit_has_selection());
  CHECK(anchor_count(document, layer_id) == 3);
}

void ui_path_context_menu_edits_anchors() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = make_rect_shape_layer(window, *canvas);
  canvas->set_tool(patchy::ui::CanvasTool::Pen);
  CHECK(canvas->path_edit_target_path() != nullptr);

  // Over a segment: only Add applies.
  with_path_context_menu([](QMenu& menu) {
    CHECK(require_menu_action(menu, "pathMenuAddAnchorAction")->isEnabled());
    CHECK(!require_menu_action(menu, "pathMenuDeleteAnchorAction")->isEnabled());
    CHECK(!require_menu_action(menu, "pathMenuConvertPointAction")->isEnabled());
    CHECK(!require_menu_action(menu, "pathMenuDeleteSelectedAction")->isEnabled());
    CHECK(!require_menu_action(menu, "pathMenuFreeTransformAction")->isEnabled());  // Pen
    require_menu_action(menu, "pathMenuAddAnchorAction")->trigger();
  });
  open_path_menu_at(*canvas, QPoint(200, 100));
  CHECK(anchor_count(document, layer_id) == 5);
  CHECK(!canvas->pen_session_active());

  // Over an anchor: Delete and Convert apply.
  with_path_context_menu([](QMenu& menu) {
    CHECK(!require_menu_action(menu, "pathMenuAddAnchorAction")->isEnabled());
    CHECK(require_menu_action(menu, "pathMenuDeleteAnchorAction")->isEnabled());
    CHECK(require_menu_action(menu, "pathMenuConvertPointAction")->isEnabled());
    require_menu_action(menu, "pathMenuDeleteAnchorAction")->trigger();
  });
  open_path_menu_at(*canvas, QPoint(100, 100));
  CHECK(anchor_count(document, layer_id) == 4);
  with_path_context_menu([](QMenu& menu) {
    require_menu_action(menu, "pathMenuConvertPointAction")->trigger();
  });
  open_path_menu_at(*canvas, QPoint(300, 220));
  bool converted = false;
  for (std::size_t i = 0; i < anchor_count(document, layer_id); ++i) {
    const auto& anchor = anchor_at(document, layer_id, i);
    if (std::abs(anchor.anchor_x - 300.0) < 1.0 && std::abs(anchor.anchor_y - 220.0) < 1.0) {
      converted = anchor.smooth;
    }
  }
  CHECK(converted);

  // Direct Select with a selection: the selection entries and Free Transform
  // Points are live; the transform starts a path session.
  canvas->set_tool(patchy::ui::CanvasTool::DirectSelect);
  const auto corner = canvas->widget_position_for_document_point(QPoint(100, 220));
  drag(*canvas, corner, corner);
  QApplication::processEvents();
  CHECK(canvas->path_edit_has_selection());
  with_path_context_menu([](QMenu& menu) {
    CHECK(require_menu_action(menu, "pathMenuDeleteSelectedAction")->isEnabled());
    CHECK(require_menu_action(menu, "pathMenuDeselectAction")->isEnabled());
    auto* transform = require_menu_action(menu, "pathMenuFreeTransformAction");
    CHECK(transform->isEnabled());
    CHECK(transform->text() == QStringLiteral("Free Transform Points"));
    transform->trigger();
  });
  open_path_menu_at(*canvas, QPoint(500, 400));
  CHECK(canvas->path_transform_active());
  canvas->cancel_path_transform();
  QApplication::processEvents();
  CHECK(!canvas->path_transform_active());

  // The gesture: a right click without a drag opens the menu, a right drag
  // pans instead.
  bool menu_seen = false;
  const auto empty = canvas->widget_position_for_document_point(QPoint(500, 400));
  send_mouse(*canvas, QEvent::MouseButtonPress, empty, Qt::RightButton, Qt::RightButton);
  // Armed between press and release: send_mouse pumps events after the press,
  // which would fire the finder before the release opens the menu.
  with_path_context_menu([&menu_seen](QMenu&) { menu_seen = true; });
  send_mouse(*canvas, QEvent::MouseButtonRelease, empty, Qt::RightButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(menu_seen);
  send_mouse(*canvas, QEvent::MouseButtonPress, empty, Qt::RightButton, Qt::RightButton);
  send_mouse(*canvas, QEvent::MouseMove, empty + QPoint(40, 30), Qt::NoButton, Qt::RightButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, empty + QPoint(40, 30), Qt::RightButton,
             Qt::NoButton);
  QApplication::processEvents();
  for (auto* widget : QApplication::topLevelWidgets()) {
    auto* menu = qobject_cast<QMenu*>(widget);
    CHECK(menu == nullptr || menu->objectName() != QStringLiteral("canvasPathContextMenu") ||
          !menu->isVisible());
  }
}

void ui_anchor_tools_share_pen_flyout_and_edit_points() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = make_rect_shape_layer(window, *canvas);

  auto* pen_button = window.findChild<QToolButton*>(QStringLiteral("penToolButton"));
  CHECK(pen_button != nullptr);
  CHECK(pen_button->menu() != nullptr);
  CHECK(pen_button->menu()->actions().size() == 4);
  CHECK(pen_button->defaultAction() == require_action(window, "toolPenAction"));
  CHECK(pen_button->property("toolFlyout").toBool());

  // Add Anchor Point: a segment click inserts, an anchor click does nothing.
  require_action(window, "toolAddAnchorAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::AddAnchor);
  CHECK(pen_button->defaultAction() == require_action(window, "toolAddAnchorAction"));
  pen_click(*canvas, QPoint(200, 100));
  CHECK(anchor_count(document, layer_id) == 5);
  CHECK(!canvas->pen_session_active());
  pen_click(*canvas, QPoint(100, 100));
  CHECK(anchor_count(document, layer_id) == 5);
  CHECK(!canvas->pen_session_active());
  // A miss never starts a path either.
  pen_click(*canvas, QPoint(500, 400));
  CHECK(!canvas->pen_session_active());

  // Delete Anchor Point: a segment click does nothing, an anchor click removes.
  require_action(window, "toolDeleteAnchorAction")->trigger();
  QApplication::processEvents();
  pen_click(*canvas, QPoint(100, 160));  // left edge segment
  CHECK(anchor_count(document, layer_id) == 5);
  pen_click(*canvas, QPoint(100, 100));
  CHECK(anchor_count(document, layer_id) == 4);

  // Convert Point toggles corner/smooth without Alt; its badge is fixed.
  require_action(window, "toolConvertPointAction")->trigger();
  QApplication::processEvents();
  const auto away = hover(*canvas, QPoint(500, 400)).pixmap().toImage();
  CHECK(hover(*canvas, QPoint(100, 160)).pixmap().toImage() == away);
  pen_click(*canvas, QPoint(300, 100));
  bool converted = false;
  for (std::size_t i = 0; i < anchor_count(document, layer_id); ++i) {
    const auto& anchor = anchor_at(document, layer_id, i);
    if (std::abs(anchor.anchor_x - 300.0) < 1.0 && std::abs(anchor.anchor_y - 100.0) < 1.0) {
      converted = anchor.smooth;
    }
  }
  CHECK(converted);

  // Ctrl still acts as Direct Select under the anchor tools.
  require_action(window, "toolAddAnchorAction")->trigger();
  QApplication::processEvents();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(300, 220)),
       canvas->widget_position_for_document_point(QPoint(320, 240)), Qt::ControlModifier);
  QApplication::processEvents();
  bool moved = false;
  for (std::size_t i = 0; i < anchor_count(document, layer_id); ++i) {
    const auto& anchor = anchor_at(document, layer_id, i);
    if (std::abs(anchor.anchor_x - 320.0) < 1.0 && std::abs(anchor.anchor_y - 240.0) < 1.0) {
      moved = true;
    }
  }
  CHECK(moved);

  // Switching from a live Pen session to an anchor tool commits the path.
  require_action(window, "toolPenAction")->trigger();
  QApplication::processEvents();
  const auto layers_before = document.layers().size();
  pen_click(*canvas, QPoint(500, 300));
  pen_click(*canvas, QPoint(650, 300));
  pen_click(*canvas, QPoint(575, 420));
  CHECK(canvas->pen_session_active());
  require_action(window, "toolDeleteAnchorAction")->trigger();
  QApplication::processEvents();
  CHECK(!canvas->pen_session_active());
  CHECK(document.layers().size() == layers_before + 1);
  // P brings the Pen back onto the flyout button.
  require_action(window, "toolPenAction")->trigger();
  QApplication::processEvents();
  CHECK(pen_button->defaultAction() == require_action(window, "toolPenAction"));
}

void ui_shape_layer_activation_drops_stale_path_target() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  CHECK(mode_combo != nullptr);
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  CHECK(radius_spin != nullptr);
  radius_spin->setValue(0);
  std::optional<patchy::LayerId> background_id;
  for (const auto& layer : std::as_const(document).layers()) {
    if (layer.name() == "Background") {
      background_id = layer.id();
    }
  }
  CHECK(background_id.has_value());

  // A Path-mode drag targets the work path row.
  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  mode_combo->setCurrentIndex(1);
  shape_drag(*canvas, QPoint(100, 100), QPoint(200, 160));
  CHECK(document.work_path() != nullptr);
  CHECK(canvas->active_document_path().has_value());
  const auto* work_path = &document.work_path()->path();
  CHECK(canvas->path_edit_target_path() == work_path);

  // A Shape-mode drag activates the new shape layer, which now wins.
  mode_combo->setCurrentIndex(0);
  shape_drag(*canvas, QPoint(300, 100), QPoint(400, 160));
  const auto shape_id = document.active_layer_id();
  CHECK(shape_id.has_value());
  CHECK(patchy::layer_is_vector_shape(*std::as_const(document).find_layer(*shape_id)));
  CHECK(!canvas->active_document_path().has_value());
  CHECK(canvas->path_edit_target_path() ==
        &std::as_const(document).find_layer(*shape_id)->vector_shape()->path);

  // An explicit work-path row click while the shape layer stays active
  // sticks across refreshes and across activating a layer without a path.
  auto* paths_list = window.findChild<QListWidget*>(QStringLiteral("pathsList"));
  CHECK(paths_list != nullptr);
  CHECK(paths_list->count() == 2);  // the layer row, then the work path
  paths_list->setCurrentRow(1);
  QApplication::processEvents();
  CHECK(canvas->active_document_path().has_value());
  patchy::ui::MainWindowTestAccess::refresh_paths_panel(window);
  QApplication::processEvents();
  CHECK(canvas->active_document_path().has_value());
  document.set_active_layer(*background_id);
  patchy::ui::MainWindowTestAccess::refresh_paths_panel(window);
  QApplication::processEvents();
  CHECK(canvas->active_document_path().has_value());
  CHECK(canvas->path_edit_target_path() == &document.work_path()->path());

  // Re-activating the shape layer retargets to its own path.
  document.set_active_layer(*shape_id);
  patchy::ui::MainWindowTestAccess::refresh_paths_panel(window);
  QApplication::processEvents();
  CHECK(!canvas->active_document_path().has_value());
  CHECK(canvas->path_edit_target_path() ==
        &std::as_const(document).find_layer(*shape_id)->vector_shape()->path);
  CHECK(!paths_list->selectedItems().isEmpty());
  CHECK(paths_list->currentRow() == 0);
}

// Seth's report (August 2026): with the Pen active, clicking a shape layer's
// row in the Layers panel showed no anchors until an appearance edit forced a
// repaint. The overlay must follow layer activation immediately.
void ui_layer_row_click_shows_anchors_for_shape_layer() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto first_id = make_rect_shape_layer(window, *canvas);  // (100,100)-(300,220)
  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  shape_drag(*canvas, QPoint(400, 100), QPoint(600, 220));
  const auto second_id = document.active_layer_id();
  CHECK(second_id.has_value() && *second_id != first_id);

  require_action(window, "toolPenAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->path_edit_target_path() ==
        &std::as_const(document).find_layer(*second_id)->vector_shape()->path);
  CHECK(accent_overlay_near(*canvas, QPoint(400, 100)));
  CHECK(!accent_overlay_near(*canvas, QPoint(100, 100)));

  // Click the first rectangle's row like a user; the canvas must repaint on
  // its own (grab() below would otherwise mask a stale frame).
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* item = require_layer_item(*layer_list, QStringLiteral("Rectangle 1"));
  const auto center = layer_list->visualItemRect(item).center();
  QApplication::processEvents();
  PaintCounter paints;
  canvas->installEventFilter(&paints);
  send_mouse(*layer_list->viewport(), QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*layer_list->viewport(), QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  QApplication::processEvents();
  canvas->removeEventFilter(&paints);
  CHECK(paints.paints >= 1);
  CHECK(document.active_layer_id() == first_id);
  CHECK(canvas->path_edit_target_path() ==
        &std::as_const(document).find_layer(first_id)->vector_shape()->path);
  CHECK(!canvas->active_document_path().has_value());
  CHECK(accent_overlay_near(*canvas, QPoint(100, 100)));
  CHECK(!accent_overlay_near(*canvas, QPoint(400, 100)));
  // The badge follows too: the first rectangle's corner offers Delete.
  const auto plain = hover(*canvas, QPoint(700, 500)).pixmap().toImage();
  CHECK(hover(*canvas, QPoint(100, 100)).pixmap().toImage() != plain);
}

// Ctrl+H (Show Selection Edges, Photoshop's Extras toggle) hides the path
// anchors and outline together with the marching ants and leaves the target
// alone; a second press shows them again.
void ui_ctrl_h_hides_path_points_with_selection_edges() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  make_rect_shape_layer(window, *canvas);
  canvas->set_tool(patchy::ui::CanvasTool::DirectSelect);
  QApplication::processEvents();
  CHECK(canvas->selection_edges_visible());
  CHECK(canvas->path_edit_target_path() != nullptr);
  CHECK(accent_overlay_near(*canvas, QPoint(100, 100)));  // corner anchor
  CHECK(accent_overlay_near(*canvas, QPoint(200, 100)));  // top edge outline

  send_key(*canvas, Qt::Key_H, Qt::ControlModifier);
  QApplication::processEvents();
  CHECK(!canvas->selection_edges_visible());
  CHECK(canvas->path_edit_target_path() != nullptr);
  CHECK(!accent_overlay_near(*canvas, QPoint(100, 100)));
  CHECK(!accent_overlay_near(*canvas, QPoint(200, 100)));

  send_key(*canvas, Qt::Key_H, Qt::ControlModifier);
  QApplication::processEvents();
  CHECK(canvas->selection_edges_visible());
  CHECK(accent_overlay_near(*canvas, QPoint(100, 100)));
  CHECK(accent_overlay_near(*canvas, QPoint(200, 100)));
}

// A marquee selection moves as one group when any of its segments is dragged
// (Direct Select, and the Pen's Ctrl latch); an unselected segment replaces it.
void ui_direct_select_drags_marquee_selection_by_segment() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = make_rect_shape_layer(window, *canvas);  // corners (100,100)..(300,220)
  const auto corner_xy = [&](std::size_t index) {
    const auto& anchor = anchor_at(document, layer_id, index);
    return QPointF(anchor.anchor_x, anchor.anchor_y);
  };

  canvas->set_tool(patchy::ui::CanvasTool::DirectSelect);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(60, 60)),
       canvas->widget_position_for_document_point(QPoint(340, 260)));  // marquee around all four
  QApplication::processEvents();
  CHECK(canvas->path_edit_has_selection());
  const auto before_0 = corner_xy(0);
  const auto before_2 = corner_xy(2);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(200, 100)),
       canvas->widget_position_for_document_point(QPoint(220, 130)));  // top edge segment
  QApplication::processEvents();
  CHECK(std::abs(corner_xy(0).x() - before_0.x() - 20.0) < 1.0);
  CHECK(std::abs(corner_xy(0).y() - before_0.y() - 30.0) < 1.0);
  CHECK(std::abs(corner_xy(2).x() - before_2.x() - 20.0) < 1.0);
  CHECK(std::abs(corner_xy(2).y() - before_2.y() - 30.0) < 1.0);

  // A segment outside the selection replaces it: only its two ends move.
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(!canvas->path_edit_has_selection());
  const auto tl = corner_xy(0);
  const auto br = corner_xy(2);
  const QPoint top_mid(static_cast<int>((corner_xy(0).x() + corner_xy(1).x()) / 2.0),
                       static_cast<int>(corner_xy(0).y()));
  drag(*canvas, canvas->widget_position_for_document_point(top_mid),
       canvas->widget_position_for_document_point(top_mid + QPoint(0, -10)));
  QApplication::processEvents();
  CHECK(std::abs(corner_xy(0).y() - (tl.y() - 10.0)) < 1.0);
  CHECK(std::abs(corner_xy(2).y() - br.y()) < 1.0);

  // The Pen's Ctrl latch behaves the same: Ctrl+marquee, then Ctrl+drag a segment.
  canvas->set_tool(patchy::ui::CanvasTool::Pen);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(60, 40)),
       canvas->widget_position_for_document_point(QPoint(340, 260)), Qt::ControlModifier);
  QApplication::processEvents();
  CHECK(canvas->path_edit_has_selection());
  CHECK(!canvas->pen_session_active());
  const auto pen_before_3 = corner_xy(3);
  const auto pen_before_2 = corner_xy(2);
  const QPoint left_mid(static_cast<int>(corner_xy(0).x()),
                        static_cast<int>((corner_xy(0).y() + corner_xy(3).y()) / 2.0));
  drag(*canvas, canvas->widget_position_for_document_point(left_mid),
       canvas->widget_position_for_document_point(left_mid + QPoint(15, 0)), Qt::ControlModifier);
  QApplication::processEvents();
  CHECK(std::abs(corner_xy(3).x() - pen_before_3.x() - 15.0) < 1.0);
  CHECK(std::abs(corner_xy(2).x() - pen_before_2.x() - 15.0) < 1.0);  // the far side moved too
  CHECK(!canvas->pen_session_active());
}

// Shift during an anchor drag constrains movement to the nearest horizontal,
// vertical, or 45-degree axis, re-evaluated per move against the total delta
// from the press point (Photoshop's vertex constraint, never latched).
void ui_direct_select_shift_drag_constrains_axis() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = make_rect_shape_layer(window, *canvas);
  const auto widget_point = [&](QPoint document_point) {
    return canvas->widget_position_for_document_point(document_point);
  };

  canvas->set_tool(patchy::ui::CanvasTool::DirectSelect);
  // Press without Shift (plain replace-select), then move with Shift held: a
  // mostly-horizontal delta snaps onto the horizontal axis (dy exactly 0).
  send_mouse(*canvas, QEvent::MouseButtonPress, widget_point(QPoint(100, 100)), Qt::LeftButton,
             Qt::LeftButton, Qt::NoModifier);
  send_mouse(*canvas, QEvent::MouseMove, widget_point(QPoint(140, 110)), Qt::NoButton,
             Qt::LeftButton, Qt::ShiftModifier);
  CHECK(std::abs(anchor_at(document, layer_id, 0).anchor_x - 140.0) < 1.0);
  CHECK(std::abs(anchor_at(document, layer_id, 0).anchor_y - 100.0) < 1.0);
  // The constraint re-evaluates against the TOTAL delta: a near-diagonal total
  // of (30,26) snaps to 45 degrees, projecting onto (28,28).
  send_mouse(*canvas, QEvent::MouseMove, widget_point(QPoint(130, 126)), Qt::NoButton,
             Qt::LeftButton, Qt::ShiftModifier);
  CHECK(std::abs(anchor_at(document, layer_id, 0).anchor_x - 128.0) < 1.0);
  CHECK(std::abs(anchor_at(document, layer_id, 0).anchor_y - 128.0) < 1.0);
  send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point(QPoint(130, 126)), Qt::LeftButton,
             Qt::NoButton, Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(std::abs(anchor_at(document, layer_id, 0).anchor_x - 128.0) < 1.0);
  CHECK(std::abs(anchor_at(document, layer_id, 0).anchor_y - 128.0) < 1.0);

  // Without Shift the drag stays raw.
  drag(*canvas, widget_point(QPoint(128, 128)), widget_point(QPoint(150, 143)));
  QApplication::processEvents();
  CHECK(std::abs(anchor_at(document, layer_id, 0).anchor_x - 150.0) < 1.0);
  CHECK(std::abs(anchor_at(document, layer_id, 0).anchor_y - 143.0) < 1.0);
}

// Pressing or releasing Shift mid-drag re-applies the constraint immediately,
// with no mouse motion, and the whole drag stays one undo entry. A marquee
// multi-selection moves as one constrained group.
void ui_direct_select_shift_toggle_mid_drag_reapplies_without_motion() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = make_rect_shape_layer(window, *canvas);
  const auto widget_point = [&](QPoint document_point) {
    return canvas->widget_position_for_document_point(document_point);
  };

  canvas->set_tool(patchy::ui::CanvasTool::DirectSelect);
  send_mouse(*canvas, QEvent::MouseButtonPress, widget_point(QPoint(100, 100)), Qt::LeftButton,
             Qt::LeftButton, Qt::NoModifier);
  send_mouse(*canvas, QEvent::MouseMove, widget_point(QPoint(140, 110)), Qt::NoButton,
             Qt::LeftButton, Qt::NoModifier);
  CHECK(std::abs(anchor_at(document, layer_id, 0).anchor_y - 110.0) < 1.0);
  send_key_press(*canvas, Qt::Key_Shift, Qt::ShiftModifier);  // snaps without motion
  CHECK(std::abs(anchor_at(document, layer_id, 0).anchor_x - 140.0) < 1.0);
  CHECK(std::abs(anchor_at(document, layer_id, 0).anchor_y - 100.0) < 1.0);
  send_key_release(*canvas, Qt::Key_Shift);  // back to the raw pointer position
  CHECK(std::abs(anchor_at(document, layer_id, 0).anchor_x - 140.0) < 1.0);
  CHECK(std::abs(anchor_at(document, layer_id, 0).anchor_y - 110.0) < 1.0);
  send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point(QPoint(140, 110)), Qt::LeftButton,
             Qt::NoButton, Qt::NoModifier);
  QApplication::processEvents();

  // The toggles coalesced into the drag's single history entry.
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(std::abs(anchor_at(document, layer_id, 0).anchor_x - 100.0) < 0.5);
  CHECK(std::abs(anchor_at(document, layer_id, 0).anchor_y - 100.0) < 0.5);

  // A marquee selection moves as one group under the constraint.
  drag(*canvas, widget_point(QPoint(60, 60)), widget_point(QPoint(340, 260)));
  QApplication::processEvents();
  CHECK(canvas->path_edit_has_selection());
  const auto far_corner_before = anchor_at(document, layer_id, 2).anchor_x;
  send_mouse(*canvas, QEvent::MouseButtonPress, widget_point(QPoint(100, 100)), Qt::LeftButton,
             Qt::LeftButton, Qt::NoModifier);
  send_mouse(*canvas, QEvent::MouseMove, widget_point(QPoint(140, 110)), Qt::NoButton,
             Qt::LeftButton, Qt::ShiftModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point(QPoint(140, 110)), Qt::LeftButton,
             Qt::NoButton, Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(std::abs(anchor_at(document, layer_id, 0).anchor_y - 100.0) < 1.0);
  CHECK(std::abs(anchor_at(document, layer_id, 2).anchor_x - far_corner_before - 40.0) < 1.0);
  CHECK(std::abs(anchor_at(document, layer_id, 2).anchor_y - 220.0) < 1.0);
}

// The status-bar chip shows the selected-point count once two or more anchors
// are selected (Direct Select and the Pen's Ctrl latch), and hides on deselect
// or a single-point selection.
void ui_direct_select_status_chip_shows_selected_count() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  make_rect_shape_layer(window, *canvas);
  const auto widget_point = [&](QPoint document_point) {
    return canvas->widget_position_for_document_point(document_point);
  };

  require_action(window, "toolDirectSelectAction")->trigger();
  QApplication::processEvents();
  auto* chip = window.findChild<QLabel*>(QStringLiteral("pathPointCountChip"));
  CHECK(chip != nullptr);
  CHECK(!chip->isVisible());

  drag(*canvas, widget_point(QPoint(60, 60)), widget_point(QPoint(340, 260)));  // marquee all four
  QApplication::processEvents();
  CHECK(canvas->path_edit_selected_anchor_count() == 4);
  CHECK(chip->isVisible());
  CHECK(chip->text() == QStringLiteral("4 points selected"));

  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(!canvas->path_edit_has_selection());
  CHECK(!chip->isVisible());

  // A single-point selection keeps the chip hidden.
  const auto corner = widget_point(QPoint(100, 100));
  drag(*canvas, corner, corner);
  QApplication::processEvents();
  CHECK(canvas->path_edit_selected_anchor_count() == 1);
  CHECK(!chip->isVisible());

  // The Pen's Ctrl-latch marquee shows the same count.
  send_key(*canvas, Qt::Key_Escape);
  require_action(window, "toolPenAction")->trigger();
  QApplication::processEvents();
  CHECK(!chip->isVisible());
  drag(*canvas, widget_point(QPoint(60, 60)), widget_point(QPoint(340, 260)), Qt::ControlModifier);
  QApplication::processEvents();
  CHECK(canvas->path_edit_selected_anchor_count() == 4);
  CHECK(chip->isVisible());
  CHECK(chip->text() == QStringLiteral("4 points selected"));

  // Path Select's whole-shape selection never shows an anchor count.
  require_action(window, "toolPathSelectAction")->trigger();
  QApplication::processEvents();
  CHECK(!chip->isVisible());
}

// A Layers-panel multi-selection outlines every selected shape layer's path
// under a path tool, not just the active editing target.
void ui_path_overlay_shows_all_selected_shape_layers() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  make_rect_shape_layer(window, *canvas);  // (100,100)-(300,220), "Rectangle 1"
  // Second rectangle away from the first; it becomes the active target.
  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  shape_drag(*canvas, QPoint(340, 60), QPoint(420, 160));
  require_action(window, "toolDirectSelectAction")->trigger();
  QApplication::processEvents();
  CHECK(accent_overlay_near(*canvas, QPoint(340, 60)));    // active target draws
  CHECK(!accent_overlay_near(*canvas, QPoint(100, 100)));  // unselected layer does not

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  require_layer_item(*layer_list, QStringLiteral("Rectangle 1"))->setSelected(true);
  require_layer_item(*layer_list, QStringLiteral("Rectangle 2"))->setSelected(true);
  QApplication::processEvents();
  CHECK(accent_overlay_near(*canvas, QPoint(100, 100)));  // both selected shapes draw
  CHECK(accent_overlay_near(*canvas, QPoint(340, 60)));
}

// With several shape layers selected in the Layers panel, Direct Select
// marquees, drags, nudges, and deletes span all of them in one undo entry,
// and the status chip totals the cross-layer count.
void ui_direct_select_edits_points_across_selected_layers() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto first_id = make_rect_shape_layer(window, *canvas);  // (100,100)-(300,220)
  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  shape_drag(*canvas, QPoint(340, 60), QPoint(420, 160));  // "Rectangle 2", stays active
  const auto second_id = *document.active_layer_id();
  CHECK(second_id != first_id);

  require_action(window, "toolDirectSelectAction")->trigger();
  QApplication::processEvents();
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  require_layer_item(*layer_list, QStringLiteral("Rectangle 1"))->setSelected(true);
  require_layer_item(*layer_list, QStringLiteral("Rectangle 2"))->setSelected(true);
  QApplication::processEvents();

  // Marquee around both rectangles selects all eight corners.
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(40, 40)),
       canvas->widget_position_for_document_point(QPoint(440, 280)));
  QApplication::processEvents();
  CHECK(canvas->path_edit_selected_anchor_count() == 8);
  auto* chip = window.findChild<QLabel*>(QStringLiteral("pathPointCountChip"));
  CHECK(chip != nullptr);
  CHECK(chip->text() == QStringLiteral("8 points selected"));

  // Dragging one corner moves the selection on BOTH layers, as one undo entry.
  const auto undo_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(340, 60)),
       canvas->widget_position_for_document_point(QPoint(360, 70)));
  QApplication::processEvents();
  CHECK(std::abs(anchor_at(document, second_id, 0).anchor_x - 360.0) < 1.0);
  CHECK(std::abs(anchor_at(document, second_id, 0).anchor_y - 70.0) < 1.0);
  CHECK(std::abs(anchor_at(document, first_id, 0).anchor_x - 120.0) < 1.0);
  CHECK(std::abs(anchor_at(document, first_id, 0).anchor_y - 110.0) < 1.0);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before + 1);

  patchy::ui::MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(std::abs(anchor_at(document, first_id, 0).anchor_x - 100.0) < 0.5);
  CHECK(std::abs(anchor_at(document, second_id, 0).anchor_x - 340.0) < 0.5);

  // The undo's history restore rebuilds the layer list (active row only), so
  // re-select both rows before the next cross-layer gesture.
  require_layer_item(*layer_list, QStringLiteral("Rectangle 1"))->setSelected(true);
  require_layer_item(*layer_list, QStringLiteral("Rectangle 2"))->setSelected(true);
  QApplication::processEvents();

  // Delete removes the selected points from both layers in one entry.
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(40, 40)),
       canvas->widget_position_for_document_point(QPoint(440, 280)));
  QApplication::processEvents();
  CHECK(canvas->path_edit_selected_anchor_count() == 8);
  const auto delete_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  send_key(*canvas, Qt::Key_Delete);
  QApplication::processEvents();
  CHECK(std::as_const(document).find_layer(first_id)->vector_shape()->path.subpaths.empty());
  CHECK(std::as_const(document).find_layer(second_id)->vector_shape()->path.subpaths.empty());
  CHECK(canvas->path_edit_selected_anchor_count() == 0);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == delete_before + 1);
  patchy::ui::MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(anchor_count(document, first_id) == 4);
  CHECK(anchor_count(document, second_id) == 4);
}

}  // namespace

std::vector<patchy::test::TestCase> vector_point_editing_tests() {
  return {
      {"ui_path_tools_show_activation_hints_and_tooltips",
       ui_path_tools_show_activation_hints_and_tooltips},
      {"ui_pen_hover_over_path_shows_edit_hints", ui_pen_hover_over_path_shows_edit_hints},
      {"ui_path_tools_options_bar_shows_tool_options", ui_path_tools_options_bar_shows_tool_options},
      {"ui_pen_auto_add_delete_off_starts_new_path_over_segment",
       ui_pen_auto_add_delete_off_starts_new_path_over_segment},
      {"ui_pen_deletes_ctrl_selected_anchors_with_delete_key",
       ui_pen_deletes_ctrl_selected_anchors_with_delete_key},
      {"ui_path_context_menu_edits_anchors", ui_path_context_menu_edits_anchors},
      {"ui_anchor_tools_share_pen_flyout_and_edit_points",
       ui_anchor_tools_share_pen_flyout_and_edit_points},
      {"ui_shape_layer_activation_drops_stale_path_target",
       ui_shape_layer_activation_drops_stale_path_target},
      {"ui_layer_row_click_shows_anchors_for_shape_layer", ui_layer_row_click_shows_anchors_for_shape_layer},
      {"ui_ctrl_h_hides_path_points_with_selection_edges",
       ui_ctrl_h_hides_path_points_with_selection_edges},
      {"ui_direct_select_drags_marquee_selection_by_segment",
       ui_direct_select_drags_marquee_selection_by_segment},
      {"ui_direct_select_shift_drag_constrains_axis", ui_direct_select_shift_drag_constrains_axis},
      {"ui_direct_select_shift_toggle_mid_drag_reapplies_without_motion",
       ui_direct_select_shift_toggle_mid_drag_reapplies_without_motion},
      {"ui_direct_select_status_chip_shows_selected_count",
       ui_direct_select_status_chip_shows_selected_count},
      {"ui_path_overlay_shows_all_selected_shape_layers",
       ui_path_overlay_shows_all_selected_shape_layers},
      {"ui_direct_select_edits_points_across_selected_layers",
       ui_direct_select_edits_points_across_selected_layers},
  };
}
