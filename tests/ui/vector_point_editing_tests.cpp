#include <QStatusBar>
#include "ui/canvas_widget.hpp"
#include "ui/main_window.hpp"
#include "test_harness.hpp"
#include <QApplication>
#include <QWidget>
#include <algorithm>
#include <atomic>
#include <array>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
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
// Point-editing discoverability and targeting: tool activation and hover
// hints, the status-bar point-count chip, the Pen's Auto Add/Delete
// option, Delete with a Ctrl-selected anchor under the Pen, the right-click
// path menu, the dedicated anchor tools in the Pen flyout, and the Paths
// panel retargeting when a layer with its own path becomes active.
#include "ui_test_support.hpp"
#include "core/vector_shape.hpp"
#include "ui/app_settings.hpp"
#include "ui/modifier_names.hpp"
#include "ui/tool_cursors.hpp"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QPaintEvent>
#include <QRegion>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <cmath>
#include <iostream>
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
  CHECK(window.statusBar()->currentMessage().contains(
      patchy::ui::resolve_modifier_names(QStringLiteral("%ALT%+click converts"))));
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
  CHECK(require_action(window, "toolPenAction")
            ->toolTip()
            .contains(patchy::ui::resolve_modifier_names(QStringLiteral("%ALT%+click"))));

  // refresh_action_tooltip renders the shortcut chip with QKeySequence::NativeText, so the
  // expected text has to be built the same way: macOS spells Shift+A as the glyph "⇧A", and
  // hardcoding the Windows/Linux spelling made this a macOS-only failure. The binding itself
  // is pinned separately in PortableText, which reads the same on every platform.
  const auto binding = [](const QAction* action) {
    return action->shortcuts().isEmpty() ? QString()
                                         : action->shortcuts().first().toString(QKeySequence::PortableText);
  };
  const auto tooltip_prefix = [](const QAction* action, const QString& label) {
    return action->shortcuts().isEmpty()
               ? label
               : QStringLiteral("%1 (%2)").arg(
                     label, action->shortcuts().first().toString(QKeySequence::NativeText));
  };
  auto* direct_select_action = require_action(window, "toolDirectSelectAction");
  auto* path_select_action = require_action(window, "toolPathSelectAction");
  // Shift+A now walks the flyout (GitHub issue 45); Direct Select itself ships unbound.
  CHECK(binding(direct_select_action).isEmpty());
  CHECK(binding(require_action(window, "toolCyclePathAction")) == QStringLiteral("Shift+A"));
  CHECK(binding(path_select_action) == QStringLiteral("A"));
  const auto direct_select_tip = tooltip_prefix(direct_select_action, QStringLiteral("Direct Select"));
  const auto path_select_tip = tooltip_prefix(path_select_action, QStringLiteral("Path Select"));
  CHECK(direct_select_action->toolTip().startsWith(direct_select_tip));

  auto* path_button = window.findChild<QToolButton*>(QStringLiteral("pathSelectToolButton"));
  CHECK(path_button != nullptr);
  // The flyout button shows its current default action's tooltip (Direct
  // Select was picked last); re-picking Path Select swaps it back.
  CHECK(path_button->toolTip().startsWith(direct_select_tip));
  path_select_action->trigger();
  QApplication::processEvents();
  CHECK(path_button->toolTip().startsWith(path_select_tip));
  // The detail sentence names its modifier keys for the platform too (macOS says Command
  // and Option), so the expectation resolves through the same helper the app uses.
  CHECK(path_button->toolTip().contains(
      patchy::ui::resolve_modifier_names(QStringLiteral("%CTRL%+T"))));
}

// The modifier tokens (%CTRL%, %ALT%) are an implementation detail of the platform key
// names in ui/modifier_names.hpp: a sentence that carries one must resolve it before a user
// sees it. A tokenized string that skipped resolve_modifier_names shipped a literal "%CTRL%"
// to the status bar once (September 2026), so this walks every action and widget text plus
// each tool's activation hint and fails on any surviving token.
void ui_user_facing_text_never_shows_modifier_tokens() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);

  const auto has_token = [](const QString& text) {
    return text.contains(QLatin1String(patchy::ui::kCtrlModifierToken)) ||
           text.contains(QLatin1String(patchy::ui::kAltModifierToken));
  };
  int checked = 0;
  for (auto* action : window.findChildren<QAction*>()) {
    if (has_token(action->text()) || has_token(action->toolTip()) || has_token(action->statusTip())) {
      std::cout << "token in action " << action->objectName().toStdString() << ": "
                << action->toolTip().toStdString() << std::endl;
      CHECK(false);
    }
    ++checked;
  }
  for (auto* widget : window.findChildren<QWidget*>()) {
    if (has_token(widget->toolTip()) || has_token(widget->statusTip()) ||
        has_token(widget->accessibleDescription())) {
      std::cout << "token in widget " << widget->objectName().toStdString() << ": "
                << widget->toolTip().toStdString() << std::endl;
      CHECK(false);
    }
    ++checked;
  }
  CHECK(checked > 100);

  // Tool activation hints are translated at their own choke point; trigger every tool.
  for (auto* action : window.findChildren<QAction*>()) {
    if (!action->objectName().startsWith(QStringLiteral("tool")) || !action->isCheckable()) {
      continue;
    }
    action->trigger();
    QApplication::processEvents();
    if (has_token(window.statusBar()->currentMessage())) {
      std::cout << "token in status hint for " << action->objectName().toStdString() << ": "
                << window.statusBar()->currentMessage().toStdString() << std::endl;
      CHECK(false);
    }
  }

  // And the platform spelling itself: a sentence with a token resolves to the key name the
  // platform actually uses, never to the token.
  const auto resolved = patchy::ui::resolve_modifier_names(QStringLiteral("%CTRL%+T, %ALT%-click"));
  CHECK(!has_token(resolved));
  CHECK(resolved.startsWith(patchy::ui::ctrl_key_name()));
  CHECK(resolved.contains(patchy::ui::alt_key_name()));
#ifdef Q_OS_MACOS
  CHECK(patchy::ui::ctrl_key_name() == QStringLiteral("Command"));
  CHECK(patchy::ui::alt_key_name() == QStringLiteral("Option"));
#else
  CHECK(patchy::ui::ctrl_key_name() == QStringLiteral("Ctrl"));
  CHECK(patchy::ui::alt_key_name() == QStringLiteral("Alt"));
#endif
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
        patchy::ui::resolve_modifier_names(
            QStringLiteral("Click to add a point here. %CTRL%-drag moves the segment.")));
  hover(*canvas, QPoint(100, 100));  // anchor
  CHECK(window.statusBar()->currentMessage().startsWith(patchy::ui::resolve_modifier_names(
      QStringLiteral("Click to delete this point. %CTRL%-drag moves it. %ALT%+click converts it"))));
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
        patchy::ui::resolve_modifier_names(
            QStringLiteral("Click to add a point here. %CTRL%-drag moves the segment.")));

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
  // opens nothing (and moves nothing: the right button no longer pans).
  bool menu_seen = false;
  const auto empty = canvas->widget_position_for_document_point(QPoint(500, 400));
  const auto origin_before_drag = canvas->widget_position_for_document_point(QPoint());
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
  CHECK(canvas->widget_position_for_document_point(QPoint()) == origin_before_drag);
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
  CHECK(pen_button->menu()->actions().size() == 6);  // four tools, separator, Cycle Pen Tools
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

// The path outline is a 1-device-pixel accent hairline with no halo (GitHub issue
// 238: the 2 px haloed outline read as part of the artwork), and the anchors stay
// 8 px, Photoshop's scale.
void ui_path_overlay_outline_is_hairline() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  make_rect_shape_layer(window, *canvas);  // (100,100)-(300,220)
  canvas->set_view_zoom_centered(1.0);
  require_action(window, "toolPenAction")->trigger();
  QApplication::processEvents();
  const auto image = canvas->grab().toImage();
  save_widget_artifact("ui_path_overlay_hairline", *canvas);
  const auto is_accent = [](QColor color) {
    return color.blue() >= 200 && color.blue() - color.red() >= 60 && color.green() > color.red();
  };

  // Midway along the top edge: one accent row on the shape's first row, plain
  // white document right above it (no halo).
  const auto edge = canvas->widget_position_for_document_point(QPoint(200, 100));
  int accent_rows = 0;
  for (int dy = -3; dy <= 3; ++dy) {
    accent_rows += is_accent(image.pixelColor(edge.x(), edge.y() + dy)) ? 1 : 0;
  }
  CHECK(accent_rows == 1);
  CHECK(is_accent(image.pixelColor(edge.x(), edge.y())));
  for (int dy = 1; dy <= 3; ++dy) {
    const auto above = image.pixelColor(edge.x(), edge.y() - dy);
    CHECK(above.red() > 240 && above.green() > 240 && above.blue() > 240);
  }

  // The corner anchor's dark edge sits 4 px out; 6 px above it is plain document.
  const auto corner = canvas->widget_position_for_document_point(QPoint(100, 100));
  CHECK(std::min(image.pixelColor(corner.x(), corner.y() - 4).lightness(),
                 image.pixelColor(corner.x(), corner.y() - 5).lightness()) < 200);
  CHECK(image.pixelColor(corner.x(), corner.y() - 7).lightness() > 240);
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

// Collects the regions the canvas is asked to paint; an offscreen grab repaints
// everything and would hide a repaint that missed part of the overlay.
class PaintRegionRecorder final : public QObject {
public:
  QRegion painted;

protected:
  bool eventFilter(QObject* /*watched*/, QEvent* event) override {
    if (event->type() == QEvent::Paint) {
      painted += static_cast<QPaintEvent*>(event)->region();
    }
    return false;
  }
};

// A live anchor drag repaints where the overlay's handle knobs WERE, not only
// the layer's bounds: a handle pulled above the shape used to leave a trail of
// stale knobs along the drag (GitHub issue 105).
void ui_anchor_drag_repaints_the_old_handle_knobs() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = make_rect_shape_layer(window, *canvas);  // corners (100,100)..(300,220)
  {
    // The top-left corner gets a smooth handle reaching 70 px above the shape.
    auto* layer = document.find_layer(layer_id);
    CHECK(layer != nullptr && layer->vector_shape() != nullptr);
    if (layer == nullptr || layer->vector_shape() == nullptr) {
      return;
    }
    auto content = *layer->vector_shape();
    auto& anchor = content.path.subpaths[0].anchors[0];
    anchor.smooth = true;
    anchor.out_x = anchor.anchor_x + 60.0;
    anchor.out_y = anchor.anchor_y - 70.0;
    anchor.in_x = anchor.anchor_x - 60.0;
    anchor.in_y = anchor.anchor_y + 70.0;
    layer->set_vector_shape(std::move(content));
  }
  canvas->set_tool(patchy::ui::CanvasTool::DirectSelect);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(60, 60)),
       canvas->widget_position_for_document_point(QPoint(340, 260)));  // marquee around all four
  QApplication::processEvents();
  CHECK(canvas->path_edit_has_selection());

  PaintRegionRecorder recorder;
  canvas->installEventFilter(&recorder);
  const auto press = canvas->widget_position_for_document_point(QPoint(100, 100));
  send_mouse(*canvas, QEvent::MouseButtonPress, press, Qt::LeftButton, Qt::LeftButton);
  QApplication::processEvents();
  int knob_y = 30;  // the out handle's knob, document space
  for (const int step : {20, 40, 60}) {
    recorder.painted = QRegion();
    const auto old_knob = canvas->widget_position_for_document_point(QPoint(160, knob_y));
    send_mouse(*canvas, QEvent::MouseMove, canvas->widget_position_for_document_point(QPoint(100, 100 + step)),
               Qt::NoButton, Qt::LeftButton);
    QApplication::processEvents();
    QApplication::processEvents();
    const auto& anchor = anchor_at(document, layer_id, 0);
    CHECK(std::abs(anchor.anchor_y - (100.0 + step)) < 1.0);
    // The knob that was 70 px above the shape, outside the layer's bounds, is
    // inside this move's repaint.
    CHECK(recorder.painted.contains(old_knob));
    knob_y = 30 + step;
  }
  send_mouse(*canvas, QEvent::MouseButtonRelease, canvas->widget_position_for_document_point(QPoint(100, 160)),
             Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  canvas->removeEventFilter(&recorder);
}

// Curve handle knobs grab at 100% (GitHub issue 105, second pass): a selected
// smooth anchor shows its 8 px knobs, and a press 8 px off a knob still grabs it
// (the radius was 7, too small to hit).
void ui_path_handle_knobs_read_and_grab() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = make_rect_shape_layer(window, *canvas);  // corners (100,100)..(300,220)
  {
    auto* layer = document.find_layer(layer_id);
    CHECK(layer != nullptr && layer->vector_shape() != nullptr);
    if (layer == nullptr || layer->vector_shape() == nullptr) {
      return;
    }
    auto content = *layer->vector_shape();
    auto& anchor = content.path.subpaths[0].anchors[0];
    anchor.smooth = true;
    anchor.out_x = anchor.anchor_x + 60.0;
    anchor.out_y = anchor.anchor_y - 70.0;
    anchor.in_x = anchor.anchor_x - 60.0;
    anchor.in_y = anchor.anchor_y + 70.0;
    layer->set_vector_shape(std::move(content));
  }
  canvas->set_view_zoom_centered(1.0);
  canvas->set_tool(patchy::ui::CanvasTool::DirectSelect);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(20, 20)),
       canvas->widget_position_for_document_point(QPoint(340, 260)));  // marquee around every point
  QApplication::processEvents();
  CHECK(canvas->path_edit_has_selection());
  const auto image = canvas->grab().toImage();
  save_widget_artifact("ui_path_handle_knobs", *canvas);

  // The out knob at (160, 30) sits on plain document: its dark rim spans the
  // 8 px disc (the white band inside the rim counts as part of it).
  const auto knob = canvas->widget_position_for_document_point(QPoint(160, 30));
  int top = 0;
  int bottom = 0;
  for (int dy = -8; dy <= 8; ++dy) {
    if (image.pixelColor(knob.x(), knob.y() + dy).lightness() < 245) {
      top = std::min(top, dy);
      bottom = std::max(bottom, dy);
    }
  }
  CHECK(bottom - top + 1 >= 9 && bottom - top + 1 <= 11);

  // A press 8 px right of the knob grabs it and drags the handle.
  const auto press = knob + QPoint(8, 0);
  send_mouse(*canvas, QEvent::MouseButtonPress, press, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, press + QPoint(20, 0), Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, press + QPoint(20, 0), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  const auto& dragged = anchor_at(document, layer_id, 0);
  CHECK(std::abs(dragged.out_x - 180.0) < 1.5);
  CHECK(std::abs(dragged.anchor_x - 100.0) < 0.5 && std::abs(dragged.anchor_y - 100.0) < 0.5);
}

// A drawn handle knob drags its handle under both arrow tools. Path Select
// selects the whole shape, so every anchor's knobs show; a press on one used to
// fall through to the anchor/segment hit and move the whole shape instead.
void ui_path_select_tools_drag_drawn_handle_knobs() {
  for (const auto tool : {patchy::ui::CanvasTool::PathSelect, patchy::ui::CanvasTool::DirectSelect}) {
    VectorSettingsGuard settings_guard;
    patchy::ui::MainWindow window;
    show_window(window);
    auto* canvas = require_canvas(window);
    auto& document = patchy::ui::MainWindowTestAccess::document(window);
    const auto layer_id = make_rect_shape_layer(window, *canvas);  // corners (100,100)..(300,220)
    {
      auto* layer = document.find_layer(layer_id);
      CHECK(layer != nullptr && layer->vector_shape() != nullptr);
      if (layer == nullptr || layer->vector_shape() == nullptr) {
        return;
      }
      auto content = *layer->vector_shape();
      auto& anchor = content.path.subpaths[0].anchors[0];
      anchor.smooth = true;
      anchor.out_x = anchor.anchor_x + 60.0;
      anchor.out_y = anchor.anchor_y - 70.0;
      anchor.in_x = anchor.anchor_x - 60.0;
      anchor.in_y = anchor.anchor_y + 70.0;
      layer->set_vector_shape(std::move(content));
    }
    canvas->set_view_zoom_centered(1.0);
    canvas->set_tool(tool);
    const bool path_select = tool == patchy::ui::CanvasTool::PathSelect;
    pen_click(*canvas, QPoint(100, 100));  // select (the anchor, or the whole shape)
    CHECK(canvas->path_edit_has_selection());
    CHECK(accent_overlay_near(*canvas, QPoint(160, 30)));  // the out knob is drawn
    if (path_select) {
      save_widget_artifact("ui_path_select_handle_knob_before", *canvas);
    }

    const auto knob = canvas->widget_position_for_document_point(QPoint(160, 30));
    send_mouse(*canvas, QEvent::MouseButtonPress, knob, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseMove, knob + QPoint(10, 0), Qt::NoButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseMove, knob + QPoint(20, 0), Qt::NoButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseButtonRelease, knob + QPoint(20, 0), Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    if (path_select) {
      save_widget_artifact("ui_path_select_handle_knob_after", *canvas);
    }
    const auto& dragged = anchor_at(document, layer_id, 0);
    CHECK(std::abs(dragged.out_x - 180.0) < 1.5);
    CHECK(std::abs(dragged.anchor_x - 100.0) < 0.5 && std::abs(dragged.anchor_y - 100.0) < 0.5);
    const auto& far_corner = anchor_at(document, layer_id, 2);  // the shape did not move
    CHECK(std::abs(far_corner.anchor_x - 300.0) < 0.5 && std::abs(far_corner.anchor_y - 220.0) < 0.5);
  }
}

// GitHub issue 248, point 1: a click on blank canvas, or past the canvas edge,
// deselects the shape Path Select picked, and the deselected shape shows only
// its outline (no anchor squares). Direct Select keeps the hollow anchors.
void ui_path_select_blank_click_deselects_shape() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  make_rect_shape_layer(window, *canvas);  // (100,100)-(300,220), black fill
  canvas->set_view_zoom_centered(0.5);
  canvas->set_tool(patchy::ui::CanvasTool::PathSelect);
  // An anchor square draws light pixels 3 px inside the corner, over the fill.
  const auto corner_glyph_drawn = [&] {
    const auto image = canvas->grab().toImage();
    const auto probe = canvas->widget_position_for_document_point(QPoint(300, 220)) - QPoint(3, 3);
    return image.pixelColor(probe).lightness() > 100;
  };
  pen_click(*canvas, QPoint(200, 100));  // the top edge
  CHECK(canvas->path_edit_has_selection());
  CHECK(corner_glyph_drawn());
  save_widget_artifact("ui_path_select_blank_click_selected", *canvas);
  pen_click(*canvas, QPoint(500, 400));  // blank canvas
  save_widget_artifact("ui_path_select_blank_click_deselected", *canvas);
  CHECK(!canvas->path_edit_has_selection());
  CHECK(!corner_glyph_drawn());
  pen_click(*canvas, QPoint(200, 100));
  CHECK(canvas->path_edit_has_selection());
  pen_click(*canvas, QPoint(-60, -60));  // the pasteboard
  CHECK(!canvas->path_edit_has_selection());
  CHECK(!corner_glyph_drawn());
  canvas->set_tool(patchy::ui::CanvasTool::DirectSelect);
  QApplication::processEvents();
  CHECK(corner_glyph_drawn());  // hollow anchors on the active path
}

// GitHub issue 248, point 2: a Path Select click picks the shape under the
// pointer even when another shape layer is active.
void ui_path_select_click_picks_shape_under_pointer() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto first_id = make_rect_shape_layer(window, *canvas);  // (100,100)-(300,220)
  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  shape_drag(*canvas, QPoint(340, 60), QPoint(420, 160));  // "Rectangle 2", active
  const auto second_id = *document.active_layer_id();
  CHECK(second_id != first_id);
  canvas->set_view_zoom_centered(1.0);
  canvas->set_tool(patchy::ui::CanvasTool::PathSelect);
  pen_click(*canvas, QPoint(200, 220));  // the first rectangle's bottom edge
  save_widget_artifact("ui_path_select_pick_click", *canvas);
  CHECK(*document.active_layer_id() == first_id);
  CHECK(canvas->path_edit_has_selection());
  // A drag inside the second rectangle's fill picks it and moves it in one press.
  save_widget_artifact("ui_path_select_pick_before", *canvas);
  shape_drag(*canvas, QPoint(380, 110), QPoint(400, 130));
  save_widget_artifact("ui_path_select_pick_after", *canvas);
  CHECK(*document.active_layer_id() == second_id);
  CHECK(std::abs(anchor_at(document, second_id, 0).anchor_x - 360.0) < 1.0);
  CHECK(std::abs(anchor_at(document, second_id, 0).anchor_y - 80.0) < 1.0);
  CHECK(std::abs(anchor_at(document, first_id, 0).anchor_x - 100.0) < 0.5);  // untouched
}

// GitHub issue 248, point 2: a Path Select marquee that crosses a shape's edge
// selects it, anchors inside the rect or not.
void ui_path_select_marquee_touching_shape_selects_it() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  make_rect_shape_layer(window, *canvas);  // (100,100)-(300,220)
  canvas->set_view_zoom_centered(1.0);
  canvas->set_tool(patchy::ui::CanvasTool::PathSelect);
  shape_drag(*canvas, QPoint(150, 50), QPoint(250, 140));  // crosses the top edge only
  save_widget_artifact("ui_path_select_marquee_touch_after", *canvas);
  CHECK(canvas->path_edit_has_selection());
  CHECK(canvas->path_edit_selected_anchor_count() == 4);
  shape_drag(*canvas, QPoint(320, 50), QPoint(400, 90));  // misses the shape
  CHECK(!canvas->path_edit_has_selection());
}

// GitHub issue 248, point 3: a Path Select drag inside the shape's fill moves
// the shape.
void ui_path_select_drag_inside_fill_moves_shape() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = make_rect_shape_layer(window, *canvas);  // (100,100)-(300,220)
  canvas->set_view_zoom_centered(1.0);
  canvas->set_tool(patchy::ui::CanvasTool::PathSelect);
  pen_click(*canvas, QPoint(200, 100));  // select it
  CHECK(canvas->path_edit_has_selection());
  save_widget_artifact("ui_path_select_fill_drag_before", *canvas);
  shape_drag(*canvas, QPoint(200, 160), QPoint(230, 180));
  save_widget_artifact("ui_path_select_fill_drag_after", *canvas);
  CHECK(std::abs(anchor_at(document, layer_id, 0).anchor_x - 130.0) < 1.0);
  CHECK(std::abs(anchor_at(document, layer_id, 0).anchor_y - 120.0) < 1.0);
  CHECK(canvas->path_edit_has_selection());
}

// GitHub issue 248, point 4: a Direct Select press on a point of a shape that
// is not selected yet drags that point at once, on the active shape and on
// another shape layer.
void ui_direct_select_drags_points_of_unselected_shapes() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto first_id = make_rect_shape_layer(window, *canvas);  // (100,100)-(300,220)
  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  shape_drag(*canvas, QPoint(340, 60), QPoint(420, 160));  // "Rectangle 2", active
  const auto second_id = *document.active_layer_id();
  canvas->set_view_zoom_centered(1.0);
  canvas->set_tool(patchy::ui::CanvasTool::DirectSelect);
  CHECK(!canvas->path_edit_has_selection());
  shape_drag(*canvas, QPoint(340, 60), QPoint(350, 70));  // the active shape's corner
  CHECK(std::abs(anchor_at(document, second_id, 0).anchor_x - 350.0) < 1.0);
  shape_drag(*canvas, QPoint(100, 100), QPoint(120, 110));  // the other shape's corner
  CHECK(*document.active_layer_id() == first_id);
  CHECK(std::abs(anchor_at(document, first_id, 0).anchor_x - 120.0) < 1.0);
  CHECK(std::abs(anchor_at(document, first_id, 0).anchor_y - 110.0) < 1.0);
  CHECK(std::abs(anchor_at(document, first_id, 1).anchor_x - 300.0) < 0.5);  // only that point
  // The pick and the drag are one undo step.
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(std::abs(anchor_at(document, first_id, 0).anchor_x - 100.0) < 0.5);
  CHECK(std::abs(anchor_at(document, second_id, 0).anchor_x - 350.0) < 1.0);  // the earlier drag stays
}

// GitHub issue 248 follow-up: under Path Select a drawn anchor (one of the
// selected shape's) drags alone, like its knobs, and the shape stays selected;
// an undrawn anchor (shape not selected, on this or another layer) is part of
// the outline and moves the whole shape. The hover hint says which.
void ui_path_select_drags_drawn_anchor_alone() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto first_id = make_rect_shape_layer(window, *canvas);  // (100,100)-(300,220)
  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  shape_drag(*canvas, QPoint(340, 60), QPoint(420, 160));  // "Rectangle 2", active
  const auto second_id = *document.active_layer_id();
  canvas->set_view_zoom_centered(1.0);
  require_action(window, "toolPathSelectAction")->trigger();
  QApplication::processEvents();
  const auto shape_hint = patchy::ui::resolve_modifier_names(
      QStringLiteral("Click to select the shape, drag to move it. %CTRL%+T transforms it."));
  const auto point_hint = QStringLiteral("Drag to move the point. Drag the outline or fill to move the shape.");

  // Unselected active shape: its corner is outline, the press moves the shape.
  hover(*canvas, QPoint(340, 60));
  CHECK(window.statusBar()->currentMessage() == shape_hint);
  shape_drag(*canvas, QPoint(340, 60), QPoint(350, 70));
  CHECK(std::abs(anchor_at(document, second_id, 0).anchor_x - 350.0) < 1.0);
  CHECK(std::abs(anchor_at(document, second_id, 2).anchor_x - 430.0) < 1.0);  // whole shape
  CHECK(canvas->path_edit_selected_anchor_count() == 4);

  // Now selected: a press on its drawn corner moves that corner alone.
  hover(*canvas, QPoint(600, 500));
  hover(*canvas, QPoint(350, 70));
  const auto hint_shown = window.statusBar()->currentMessage();
  save_widget_artifact("ui_path_select_anchor_drag_before", *canvas);
  shape_drag(*canvas, QPoint(350, 70), QPoint(330, 50));
  save_widget_artifact("ui_path_select_anchor_drag_after", *canvas);
  CHECK(hint_shown == point_hint);
  CHECK(std::abs(anchor_at(document, second_id, 0).anchor_x - 330.0) < 1.0);
  CHECK(std::abs(anchor_at(document, second_id, 0).anchor_y - 50.0) < 1.0);
  CHECK(std::abs(anchor_at(document, second_id, 2).anchor_x - 430.0) < 1.0);  // stayed
  CHECK(std::abs(anchor_at(document, second_id, 1).anchor_x - 430.0) < 1.0);
  CHECK(canvas->path_edit_selected_anchor_count() == 4);  // the shape stays selected
  // Its outline (a segment, not an anchor) still moves the whole shape.
  shape_drag(*canvas, QPoint(430, 120), QPoint(440, 120));
  CHECK(std::abs(anchor_at(document, second_id, 0).anchor_x - 340.0) < 1.0);
  CHECK(std::abs(anchor_at(document, second_id, 2).anchor_x - 440.0) < 1.0);

  // The other layer's corner (not drawn): the press picks that shape and moves it.
  shape_drag(*canvas, QPoint(100, 100), QPoint(110, 105));
  CHECK(*document.active_layer_id() == first_id);
  CHECK(std::abs(anchor_at(document, first_id, 0).anchor_x - 110.0) < 1.0);
  CHECK(std::abs(anchor_at(document, first_id, 2).anchor_x - 310.0) < 1.0);
  // Then its drawn corner drags alone.
  shape_drag(*canvas, QPoint(310, 225), QPoint(330, 240));
  CHECK(std::abs(anchor_at(document, first_id, 2).anchor_x - 330.0) < 1.0);
  CHECK(std::abs(anchor_at(document, first_id, 0).anchor_x - 110.0) < 1.0);
  // One drag, one undo step.
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(std::abs(anchor_at(document, first_id, 2).anchor_x - 310.0) < 1.0);
  CHECK(std::abs(anchor_at(document, first_id, 0).anchor_x - 110.0) < 1.0);
}

// GitHub issue 248 follow-up: Direct Select drags the pressed point alone, also
// right after Path Select selected the whole shape (the switch starts a fresh
// point selection).
void ui_direct_select_after_path_select_drags_one_point() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = make_rect_shape_layer(window, *canvas);  // (100,100)-(300,220)
  canvas->set_view_zoom_centered(1.0);
  require_action(window, "toolPathSelectAction")->trigger();
  QApplication::processEvents();
  pen_click(*canvas, QPoint(200, 100));
  CHECK(canvas->path_edit_selected_anchor_count() == 4);
  require_action(window, "toolDirectSelectAction")->trigger();
  QApplication::processEvents();
  CHECK(!canvas->path_edit_has_selection());
  shape_drag(*canvas, QPoint(300, 220), QPoint(320, 230));
  CHECK(std::abs(anchor_at(document, layer_id, 2).anchor_x - 320.0) < 1.0);
  CHECK(std::abs(anchor_at(document, layer_id, 2).anchor_y - 230.0) < 1.0);
  CHECK(std::abs(anchor_at(document, layer_id, 0).anchor_x - 100.0) < 0.5);
  CHECK(std::abs(anchor_at(document, layer_id, 1).anchor_x - 300.0) < 0.5);
  CHECK(canvas->path_edit_selected_anchor_count() == 1);
}

// GitHub issue 248 follow-up: an open path's anchors drag alone under both
// arrows (Path Select once the path is selected).
void ui_path_select_tools_drag_open_path_anchor() {
  for (const auto tool : {patchy::ui::CanvasTool::PathSelect, patchy::ui::CanvasTool::DirectSelect}) {
    VectorSettingsGuard settings_guard;
    patchy::ui::MainWindow window;
    show_window(window);
    auto* canvas = require_canvas(window);
    auto& document = patchy::ui::MainWindowTestAccess::document(window);
    canvas->set_view_zoom_centered(1.0);
    canvas->set_tool(patchy::ui::CanvasTool::Pen);
    pen_click(*canvas, QPoint(100, 300));
    pen_click(*canvas, QPoint(200, 200));
    pen_click(*canvas, QPoint(300, 300));
    canvas->set_tool(tool);  // commits the open path
    QApplication::processEvents();
    (void)document;
    // The work path or a shape layer, whichever the Pen's mode made.
    const auto open_path = [&]() -> const patchy::VectorPath& {
      const auto* path = canvas->path_edit_target_path();
      CHECK(path != nullptr);
      return *path;
    };
    CHECK(!open_path().subpaths.empty() && !open_path().subpaths[0].closed &&
          open_path().subpaths[0].anchors.size() == 3);
    if (tool == patchy::ui::CanvasTool::PathSelect) {
      pen_click(*canvas, QPoint(150, 250));  // the first segment selects the path
      CHECK(canvas->path_edit_selected_anchor_count() == 3);
    }
    shape_drag(*canvas, QPoint(200, 200), QPoint(210, 180));
    const auto& anchors = open_path().subpaths[0].anchors;
    CHECK(std::abs(anchors[1].anchor_x - 210.0) < 1.0 && std::abs(anchors[1].anchor_y - 180.0) < 1.0);
    CHECK(std::abs(anchors[0].anchor_x - 100.0) < 0.5 && std::abs(anchors[2].anchor_x - 300.0) < 0.5);
  }
}

// GitHub issue 248 follow-up: the path transform box (Ctrl+T under a path
// tool) shows Free Transform's cursors: resize on handles and sides, move
// inside, and each corner's own rotate cursor outside; a drag keeps the
// grabbed zone's cursor.
void ui_path_transform_shows_free_transform_cursors() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  make_rect_shape_layer(window, *canvas);  // (100,100)-(300,220)
  canvas->set_view_zoom_centered(1.0);
  require_action(window, "toolPathSelectAction")->trigger();
  QApplication::processEvents();
  pen_click(*canvas, QPoint(200, 100));
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->path_transform_active());
  const auto image_of = [](const QCursor& cursor) {
    return cursor.pixmap().toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
  };
  CHECK(hover(*canvas, QPoint(200, 160)).shape() == Qt::SizeAllCursor);
  CHECK(hover(*canvas, QPoint(100, 100)).shape() == Qt::SizeFDiagCursor);
  CHECK(hover(*canvas, QPoint(300, 100)).shape() == Qt::SizeBDiagCursor);
  CHECK(hover(*canvas, QPoint(300, 160)).shape() == Qt::SizeHorCursor);
  CHECK(hover(*canvas, QPoint(200, 220)).shape() == Qt::SizeVerCursor);
  const std::array<std::pair<QPoint, double>, 4> outside = {{{QPoint(320, 240), 45.0},
                                                            {QPoint(80, 240), 135.0},
                                                            {QPoint(80, 80), 225.0},
                                                            {QPoint(320, 80), 315.0}}};
  for (const auto& [point, direction] : outside) {
    const auto cursor = hover(*canvas, point);
    CHECK(cursor.shape() == Qt::BitmapCursor);
    CHECK(image_of(cursor) == image_of(patchy::ui::corner_rotate_cursor(direction)));
  }
  // Mid-drag the grabbed side keeps its resize cursor off the box.
  const auto side = canvas->widget_position_for_document_point(QPoint(300, 160));
  send_mouse(*canvas, QEvent::MouseButtonPress, side, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, side + QPoint(60, 40), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  CHECK(canvas->cursor().shape() == Qt::SizeHorCursor);
  send_mouse(*canvas, QEvent::MouseButtonRelease, side + QPoint(60, 40), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  canvas->cancel_path_transform();
  QApplication::processEvents();
  CHECK(!canvas->path_transform_active());
  CHECK(hover(*canvas, QPoint(200, 160)).shape() == Qt::ArrowCursor);
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

// The path-edit marquee repositions with Space held and squares with Shift,
// like the selection marquee - including under the Pen's Ctrl latch, where
// the Space release must not drop the latch.
void ui_path_marquee_space_repositions_and_shift_squares() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  make_rect_shape_layer(window, *canvas);  // corners (100,100)..(300,220)
  const auto widget_point = [&](QPoint document_point) {
    return canvas->widget_position_for_document_point(document_point);
  };

  canvas->set_tool(patchy::ui::CanvasTool::DirectSelect);
  // Space mid-marquee slides the whole rect: (40,40)-(270,160) shifts by
  // (+70,+30) to (110,70)-(340,190), which contains only the (300,100) corner.
  send_mouse(*canvas, QEvent::MouseButtonPress, widget_point(QPoint(40, 40)), Qt::LeftButton,
             Qt::LeftButton, Qt::NoModifier);
  send_mouse(*canvas, QEvent::MouseMove, widget_point(QPoint(270, 160)), Qt::NoButton,
             Qt::LeftButton, Qt::NoModifier);
  send_key_press(*canvas, Qt::Key_Space);
  send_mouse(*canvas, QEvent::MouseMove, widget_point(QPoint(340, 190)), Qt::NoButton,
             Qt::LeftButton, Qt::NoModifier);
  send_key_release(*canvas, Qt::Key_Space);
  send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point(QPoint(340, 190)), Qt::LeftButton,
             Qt::NoButton, Qt::NoModifier);
  QApplication::processEvents();
  CHECK(canvas->path_edit_selected_anchor_count() == 1);

  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();

  // Shift pressed mid-marquee squares the rect immediately: (60,60)-(340,180)
  // becomes (60,60)-(180,180), which contains only the (100,100) corner.
  send_mouse(*canvas, QEvent::MouseButtonPress, widget_point(QPoint(60, 60)), Qt::LeftButton,
             Qt::LeftButton, Qt::NoModifier);
  send_mouse(*canvas, QEvent::MouseMove, widget_point(QPoint(340, 180)), Qt::NoButton,
             Qt::LeftButton, Qt::NoModifier);
  send_key_press(*canvas, Qt::Key_Shift, Qt::ShiftModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point(QPoint(340, 180)), Qt::LeftButton,
             Qt::NoButton, Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(canvas->path_edit_selected_anchor_count() == 1);

  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();

  // The Pen's Ctrl-latch marquee survives a Space press AND release mid-drag
  // (the release must not run set_tool, which clears the latch).
  canvas->set_tool(patchy::ui::CanvasTool::Pen);
  send_mouse(*canvas, QEvent::MouseButtonPress, widget_point(QPoint(40, 40)), Qt::LeftButton,
             Qt::LeftButton, Qt::ControlModifier);
  send_mouse(*canvas, QEvent::MouseMove, widget_point(QPoint(270, 160)), Qt::NoButton,
             Qt::LeftButton, Qt::ControlModifier);
  send_key_press(*canvas, Qt::Key_Space);
  send_mouse(*canvas, QEvent::MouseMove, widget_point(QPoint(340, 190)), Qt::NoButton,
             Qt::LeftButton, Qt::ControlModifier);
  send_key_release(*canvas, Qt::Key_Space);
  send_mouse(*canvas, QEvent::MouseMove, widget_point(QPoint(345, 195)), Qt::NoButton,
             Qt::LeftButton, Qt::ControlModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point(QPoint(345, 195)), Qt::LeftButton,
             Qt::NoButton, Qt::ControlModifier);
  QApplication::processEvents();
  CHECK(canvas->path_edit_selected_anchor_count() == 1);
  CHECK(!canvas->pen_session_active());
}

}  // namespace

std::vector<patchy::test::TestCase> vector_point_editing_tests() {
  return {
      {"ui_user_facing_text_never_shows_modifier_tokens", ui_user_facing_text_never_shows_modifier_tokens},
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
      {"ui_path_overlay_outline_is_hairline", ui_path_overlay_outline_is_hairline},
      {"ui_path_handle_knobs_read_and_grab", ui_path_handle_knobs_read_and_grab},
      {"ui_path_select_tools_drag_drawn_handle_knobs", ui_path_select_tools_drag_drawn_handle_knobs},
      {"ui_path_select_blank_click_deselects_shape", ui_path_select_blank_click_deselects_shape},
      {"ui_path_select_click_picks_shape_under_pointer", ui_path_select_click_picks_shape_under_pointer},
      {"ui_path_select_marquee_touching_shape_selects_it", ui_path_select_marquee_touching_shape_selects_it},
      {"ui_path_select_drag_inside_fill_moves_shape", ui_path_select_drag_inside_fill_moves_shape},
      {"ui_direct_select_drags_points_of_unselected_shapes",
       ui_direct_select_drags_points_of_unselected_shapes},
      {"ui_path_select_drags_drawn_anchor_alone", ui_path_select_drags_drawn_anchor_alone},
      {"ui_direct_select_after_path_select_drags_one_point", ui_direct_select_after_path_select_drags_one_point},
      {"ui_path_select_tools_drag_open_path_anchor", ui_path_select_tools_drag_open_path_anchor},
      {"ui_path_transform_shows_free_transform_cursors", ui_path_transform_shows_free_transform_cursors},
      {"ui_ctrl_h_hides_path_points_with_selection_edges",
       ui_ctrl_h_hides_path_points_with_selection_edges},
      {"ui_direct_select_drags_marquee_selection_by_segment",
       ui_direct_select_drags_marquee_selection_by_segment},
      {"ui_anchor_drag_repaints_the_old_handle_knobs", ui_anchor_drag_repaints_the_old_handle_knobs},
      {"ui_direct_select_shift_drag_constrains_axis", ui_direct_select_shift_drag_constrains_axis},
      {"ui_direct_select_shift_toggle_mid_drag_reapplies_without_motion",
       ui_direct_select_shift_toggle_mid_drag_reapplies_without_motion},
      {"ui_direct_select_status_chip_shows_selected_count",
       ui_direct_select_status_chip_shows_selected_count},
      {"ui_path_overlay_shows_all_selected_shape_layers",
       ui_path_overlay_shows_all_selected_shape_layers},
      {"ui_direct_select_edits_points_across_selected_layers",
       ui_direct_select_edits_points_across_selected_layers},
      {"ui_path_marquee_space_repositions_and_shift_squares",
       ui_path_marquee_space_repositions_and_shift_squares},
  };
}
