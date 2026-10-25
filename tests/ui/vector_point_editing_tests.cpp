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
#include <initializer_list>
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
#include "core/vector_raster.hpp"
#include "core/vector_shape.hpp"
#include "ui/app_settings.hpp"
#include "ui/modifier_names.hpp"
#include "ui/tool_cursors.hpp"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QImage>
#include <QPaintEvent>
#include <QPainter>
#include <QRegion>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <cmath>
#include <cstdio>
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

// Writes `path` into the active target through the canvas's own edit route
// (replace_path_edit_target, what every point and handle drag uses), so a shape
// layer re-renders its fill. Tests that set handles must use this, not
// set_vector_shape, or the fill stays the old shape in captures.
void set_target_path_through_edits(patchy::ui::CanvasWidget& canvas, patchy::VectorPath path) {
  std::vector<int> groups;
  for (const auto& subpath : path.subpaths) {
    if (std::find(groups.begin(), groups.end(), subpath.shape_group) == groups.end()) {
      groups.push_back(subpath.shape_group);
    }
  }
  canvas.replace_path_edit_target(std::move(path), groups);
  QApplication::processEvents();
}

// The alpha of the shape layer's rendered fill at a document point (0 outside it).
int layer_fill_alpha_at(patchy::Document& document, patchy::LayerId id, QPoint document_point) {
  const auto* layer = std::as_const(document).find_layer(id);
  CHECK(layer != nullptr);
  if (layer == nullptr) {
    return -1;
  }
  const auto bounds = layer->bounds();
  const QPoint local(document_point.x() - bounds.x, document_point.y() - bounds.y);
  const auto& pixels = layer->pixels();
  if (local.x() < 0 || local.y() < 0 || local.x() >= pixels.width() || local.y() >= pixels.height() ||
      pixels.format().channels < 4) {
    return 0;
  }
  return pixels.pixel(local.x(), local.y())[3];
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
  require_action(window, "toolGradientAction")->trigger();
  QApplication::processEvents();
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Gradient"));

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

// Tool roles (GitHub issue 248, Seth's follow-up): handle knobs belong to Direct
// Select, which draws and drags them; Path Select moves whole shapes and draws
// none, so a press where a knob would be is just empty canvas there.
void ui_path_tools_keep_handle_knobs_to_direct_select() {
  for (const auto tool : {patchy::ui::CanvasTool::PathSelect, patchy::ui::CanvasTool::DirectSelect}) {
    VectorSettingsGuard settings_guard;
    patchy::ui::MainWindow window;
    show_window(window);
    auto* canvas = require_canvas(window);
    auto& document = patchy::ui::MainWindowTestAccess::document(window);
    const auto layer_id = make_rect_shape_layer(window, *canvas);  // corners (100,100)..(300,220)
    {
      const auto* path = canvas->path_edit_target_path();
      CHECK(path != nullptr);
      if (path == nullptr) {
        return;
      }
      auto curved = *path;  // the top-left corner turns smooth: a fill the captures can trust
      auto& anchor = curved.subpaths[0].anchors[0];
      anchor.smooth = true;
      anchor.out_x = anchor.anchor_x + 60.0;
      anchor.out_y = anchor.anchor_y - 70.0;
      anchor.in_x = anchor.anchor_x - 60.0;
      anchor.in_y = anchor.anchor_y + 70.0;
      set_target_path_through_edits(*canvas, std::move(curved));
    }
    canvas->set_view_zoom_centered(1.0);
    canvas->set_tool(tool);
    const bool path_select = tool == patchy::ui::CanvasTool::PathSelect;
    // The fill follows the curve: above the old top edge, under the bulge.
    CHECK(layer_fill_alpha_at(document, layer_id, QPoint(150, 80)) > 200);
    pen_click(*canvas, QPoint(100, 100));  // select (the anchor, or the whole shape)
    CHECK(canvas->path_edit_has_selection());
    save_widget_artifact(path_select ? "ui_path_roles_path_select_selected_shape"
                                     : "ui_path_roles_direct_select_one_point",
                         *canvas);
    CHECK(accent_overlay_near(*canvas, QPoint(160, 30)) == !path_select);  // the out knob: Direct Select only

    const auto knob = canvas->widget_position_for_document_point(QPoint(160, 30));
    send_mouse(*canvas, QEvent::MouseButtonPress, knob, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseMove, knob + QPoint(10, 0), Qt::NoButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseMove, knob + QPoint(20, 0), Qt::NoButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseButtonRelease, knob + QPoint(20, 0), Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    const auto& dragged = anchor_at(document, layer_id, 0);
    CHECK(std::abs(dragged.out_x - (path_select ? 160.0 : 180.0)) < 1.5);
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

// GitHub #314: a Path Select drag applies once per batch of queued mouse moves, at
// the latest pointer position. Each apply re-bakes the shape (a gradient fill renders
// again in full) and recomposites its region; applying every queued move fell seconds
// behind a fast pointer on Wayland and the app stopped responding.
void ui_path_select_drag_coalesces_queued_moves() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = make_rect_shape_layer(window, *canvas);  // (100,100)-(300,220)
  canvas->set_view_zoom_centered(1.0);
  canvas->set_tool(patchy::ui::CanvasTool::PathSelect);
  const auto start = canvas->widget_position_for_document_point(QPoint(200, 160));
  const auto applies = [canvas] { return canvas->render_cache_diagnostics().path_drag_applies; };
  send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  const auto before = applies();
  for (int move = 1; move <= 30; ++move) {
    const auto position = start + QPoint(move, move / 2);
    QApplication::postEvent(canvas, new QMouseEvent(QEvent::MouseMove, QPointF(position),
                                                    QPointF(canvas->mapToGlobal(position)), Qt::NoButton,
                                                    Qt::LeftButton, Qt::NoModifier));
  }
  // The apply is queued behind the moves, so it runs on the event loop's next pass.
  QApplication::processEvents();
  QApplication::processEvents();
  CHECK(applies() - before == 1);
  CHECK(std::abs(anchor_at(document, layer_id, 0).anchor_x - 130.0) < 1.0);
  CHECK(std::abs(anchor_at(document, layer_id, 0).anchor_y - 115.0) < 1.0);
  // A move delivered right before the release still lands: the release applies it.
  const auto end = start + QPoint(40, 30);
  send_mouse(*canvas, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
  CHECK(applies() - before == 2);
  CHECK(std::abs(anchor_at(document, layer_id, 0).anchor_x - 140.0) < 1.0);
  CHECK(std::abs(anchor_at(document, layer_id, 0).anchor_y - 130.0) < 1.0);
  QApplication::processEvents();
  CHECK(applies() - before == 2);  // the queued apply found nothing left to do
  // The fill moved with the path, and the whole drag is one undo step.
  const auto* layer = std::as_const(document).find_layer(layer_id);
  CHECK(layer != nullptr && layer->bounds().x == 140 && layer->bounds().y == 130);
  patchy::ui::MainWindowTestAccess::undo(window);
  CHECK(std::abs(anchor_at(document, layer_id, 0).anchor_x - 100.0) < 0.5);
  CHECK(std::abs(anchor_at(document, layer_id, 0).anchor_y - 100.0) < 0.5);
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

// Tool roles (GitHub issue 248, Seth's follow-up): Path Select moves whole
// shapes. A press on an anchor, the outline or the fill moves the whole shape,
// selected or not, on this layer or a picked one, and the hover hint names the
// shape, never a point. Point drags are Direct Select's.
void ui_path_select_anchor_drag_moves_the_whole_shape() {
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

  // Unselected active shape: its corner moves the shape.
  hover(*canvas, QPoint(340, 60));
  CHECK(window.statusBar()->currentMessage() == shape_hint);
  shape_drag(*canvas, QPoint(340, 60), QPoint(350, 70));
  CHECK(std::abs(anchor_at(document, second_id, 0).anchor_x - 350.0) < 1.0);
  CHECK(std::abs(anchor_at(document, second_id, 2).anchor_x - 430.0) < 1.0);  // whole shape
  CHECK(canvas->path_edit_selected_anchor_count() == 4);

  // Selected: its corner still moves the whole shape, with the same hint.
  hover(*canvas, QPoint(600, 500));
  window.statusBar()->showMessage(QStringLiteral("marker"));
  hover(*canvas, QPoint(350, 70));
  CHECK(window.statusBar()->currentMessage() == shape_hint);
  shape_drag(*canvas, QPoint(350, 70), QPoint(330, 50));
  CHECK(std::abs(anchor_at(document, second_id, 0).anchor_x - 330.0) < 1.0);
  CHECK(std::abs(anchor_at(document, second_id, 0).anchor_y - 50.0) < 1.0);
  CHECK(std::abs(anchor_at(document, second_id, 2).anchor_x - 410.0) < 1.0);
  CHECK(std::abs(anchor_at(document, second_id, 2).anchor_y - 150.0) < 1.0);
  CHECK(canvas->path_edit_selected_anchor_count() == 4);
  // Its outline moves the whole shape too.
  shape_drag(*canvas, QPoint(410, 100), QPoint(420, 100));
  CHECK(std::abs(anchor_at(document, second_id, 0).anchor_x - 340.0) < 1.0);
  CHECK(std::abs(anchor_at(document, second_id, 2).anchor_x - 420.0) < 1.0);

  // The other layer's corner: the press picks that shape and moves it.
  shape_drag(*canvas, QPoint(100, 100), QPoint(110, 105));
  CHECK(*document.active_layer_id() == first_id);
  CHECK(std::abs(anchor_at(document, first_id, 0).anchor_x - 110.0) < 1.0);
  CHECK(std::abs(anchor_at(document, first_id, 2).anchor_x - 310.0) < 1.0);
  // A single point is Direct Select's.
  require_action(window, "toolDirectSelectAction")->trigger();
  QApplication::processEvents();
  shape_drag(*canvas, QPoint(310, 225), QPoint(330, 240));
  CHECK(std::abs(anchor_at(document, first_id, 2).anchor_x - 330.0) < 1.0);
  CHECK(std::abs(anchor_at(document, first_id, 0).anchor_x - 110.0) < 1.0);
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

// An open path's anchor: Direct Select drags it alone, Path Select (once the path
// is selected) moves the whole path (GitHub issue 248 tool roles).
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
    if (tool == patchy::ui::CanvasTool::PathSelect) {
      CHECK(std::abs(anchors[0].anchor_x - 110.0) < 1.0 && std::abs(anchors[2].anchor_x - 310.0) < 1.0);
    } else {
      CHECK(std::abs(anchors[0].anchor_x - 100.0) < 0.5 && std::abs(anchors[2].anchor_x - 300.0) < 0.5);
    }
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
  CHECK(hover(*canvas, QPoint(200, 160)).pixmap().toImage() == patchy::ui::path_select_cursor().pixmap().toImage());
}

// GitHub issue 248 follow-up: the path transform box takes Free Transform's
// distort gestures (GitHub issue 225) and maps every anchor and handle through
// the corner quad. A rect shape (100,100)-(300,220) is selected under Path Select
// and Ctrl+T starts the box; `gesture` drags from `from` to `to` with `modifiers`,
// commits, and returns the four corners (TL, TR, BR, BL).
struct PathTransformGestureScene {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  patchy::ui::CanvasWidget* canvas = nullptr;
  patchy::LayerId layer_id{};

  PathTransformGestureScene() {
    show_window(window);
    canvas = require_canvas(window);
    layer_id = make_rect_shape_layer(window, *canvas);
    canvas->set_view_zoom_centered(1.0);
    require_action(window, "toolPathSelectAction")->trigger();
    QApplication::processEvents();
    pen_click(*canvas, QPoint(200, 100));
    require_action(window, "editFreeTransformAction")->trigger();
    QApplication::processEvents();
    CHECK(canvas->path_transform_active());
  }

  void press_and_move(QPoint from, std::initializer_list<QPoint> path, Qt::KeyboardModifiers modifiers) {
    send_mouse(*canvas, QEvent::MouseButtonPress, canvas->widget_position_for_document_point(from), Qt::LeftButton,
               Qt::LeftButton, modifiers);
    for (const auto point : path) {
      send_mouse(*canvas, QEvent::MouseMove, canvas->widget_position_for_document_point(point), Qt::NoButton,
                 Qt::LeftButton, modifiers);
    }
    QApplication::processEvents();
  }

  std::array<QPointF, 4> finish(QPoint release, Qt::KeyboardModifiers modifiers) {
    send_mouse(*canvas, QEvent::MouseButtonRelease, canvas->widget_position_for_document_point(release),
               Qt::LeftButton, Qt::NoButton, modifiers);
    QApplication::processEvents();
    canvas->commit_path_transform();
    QApplication::processEvents();
    CHECK(!canvas->path_transform_active());
    auto& document = patchy::ui::MainWindowTestAccess::document(window);
    std::array<QPointF, 4> corners{};
    for (std::size_t i = 0; i < corners.size(); ++i) {
      const auto& anchor = anchor_at(document, layer_id, i);
      corners[i] = QPointF(anchor.anchor_x, anchor.anchor_y);
    }
    return corners;
  }

  std::array<QPointF, 4> gesture(QPoint from, QPoint to, Qt::KeyboardModifiers modifiers) {
    press_and_move(from, {from + (to - from) / 2, to}, modifiers);
    return finish(to, modifiers);
  }
};

bool corners_near(const std::array<QPointF, 4>& corners, const std::array<QPointF, 4>& expected) {
  for (std::size_t i = 0; i < corners.size(); ++i) {
    if (std::abs(corners[i].x() - expected[i].x()) > 0.6 || std::abs(corners[i].y() - expected[i].y()) > 0.6) {
      return false;
    }
  }
  return true;
}

// Ctrl+drag a corner distorts it alone; Ctrl+drag a side moves both its corners.
// One undo step restores the shape.
void ui_path_transform_ctrl_drag_distorts_corner_and_side() {
  {
    PathTransformGestureScene scene;
    save_widget_artifact("ui_path_transform_distort_before", *scene.canvas);
    scene.press_and_move(QPoint(100, 100), {QPoint(115, 110), QPoint(130, 120)}, Qt::ControlModifier);
    save_widget_artifact("ui_path_transform_distort_after", *scene.canvas);
    const auto corners = scene.finish(QPoint(130, 120), Qt::ControlModifier);
    CHECK(corners_near(corners, {QPointF(130, 120), QPointF(300, 100), QPointF(300, 220), QPointF(100, 220)}));
    require_action_by_text(scene.window, QStringLiteral("Undo"))->trigger();
    QApplication::processEvents();
    auto& document = patchy::ui::MainWindowTestAccess::document(scene.window);
    CHECK(std::abs(anchor_at(document, scene.layer_id, 0).anchor_x - 100.0) < 0.5);
  }
  {
    PathTransformGestureScene scene;
    const auto corners = scene.gesture(QPoint(300, 160), QPoint(320, 170), Qt::ControlModifier);
    CHECK(corners_near(corners, {QPointF(100, 100), QPointF(320, 110), QPointF(320, 230), QPointF(100, 220)}));
  }
}

// Ctrl+Alt mirrors the drag about the center: the opposite corner moves the other way.
void ui_path_transform_ctrl_alt_drag_is_symmetric() {
  PathTransformGestureScene scene;
  const auto corners = scene.gesture(QPoint(100, 100), QPoint(120, 110), Qt::ControlModifier | Qt::AltModifier);
  CHECK(corners_near(corners, {QPointF(120, 110), QPointF(300, 100), QPointF(280, 210), QPointF(100, 220)}));
}

// Ctrl+Shift+drag a side slides it along itself (a skew).
void ui_path_transform_ctrl_shift_drag_skews_side() {
  PathTransformGestureScene scene;
  const auto corners = scene.gesture(QPoint(200, 100), QPoint(230, 105), Qt::ControlModifier | Qt::ShiftModifier);
  CHECK(corners_near(corners, {QPointF(130, 100), QPointF(330, 100), QPointF(300, 220), QPointF(100, 220)}));
}

// Ctrl+Alt+Shift+drag a corner applies perspective: the corner sharing the side
// moves the opposite way (a symmetric trapezoid).
void ui_path_transform_ctrl_alt_shift_drag_is_perspective() {
  PathTransformGestureScene scene;
  const auto corners = scene.gesture(QPoint(100, 100), QPoint(130, 105),
                                     Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier);
  CHECK(corners_near(corners, {QPointF(130, 100), QPointF(270, 100), QPointF(300, 220), QPointF(100, 220)}));
}

// The #225 guard: a frame that would fold the quad keeps the last good shape.
void ui_path_transform_distort_never_folds_the_quad() {
  PathTransformGestureScene scene;
  scene.press_and_move(QPoint(100, 100), {QPoint(150, 130), QPoint(420, 300)}, Qt::ControlModifier);
  const auto corners = scene.finish(QPoint(420, 300), Qt::ControlModifier);
  CHECK(corners_near(corners, {QPointF(150, 130), QPointF(300, 100), QPointF(300, 220), QPointF(100, 220)}));
}


// GitHub issue 248 follow-up: the shape's fill follows the path transform box
// live (every drag frame re-rasterizes the one layer), not only on Enter: a
// pixel the new outline left is empty mid-drag, after release and after Enter.
// Esc restores the fill, and Undo during the session cancels it rather than
// reaching the history behind it. Plain scale drags follow too.
void ui_path_transform_fill_follows_the_box_live() {
  {
    PathTransformGestureScene scene;
    auto& document = patchy::ui::MainWindowTestAccess::document(scene.window);
    CHECK(layer_fill_alpha_at(document, scene.layer_id, QPoint(105, 105)) > 200);
    scene.press_and_move(QPoint(100, 100), {QPoint(130, 120), QPoint(160, 140)}, Qt::ControlModifier);
    save_widget_artifact("ui_path_transform_fill_mid_drag", *scene.canvas);
    CHECK(layer_fill_alpha_at(document, scene.layer_id, QPoint(105, 105)) == 0);  // left by the new outline
    CHECK(layer_fill_alpha_at(document, scene.layer_id, QPoint(200, 170)) > 200);  // still inside
    send_mouse(*scene.canvas, QEvent::MouseButtonRelease, scene.canvas->widget_position_for_document_point(QPoint(160, 140)),
               Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
    QApplication::processEvents();
    CHECK(layer_fill_alpha_at(document, scene.layer_id, QPoint(105, 105)) == 0);
    send_key(*scene.canvas, Qt::Key_Return);
    QApplication::processEvents();
    CHECK(!scene.canvas->path_transform_active());
    save_widget_artifact("ui_path_transform_fill_after_enter", *scene.canvas);
    CHECK(layer_fill_alpha_at(document, scene.layer_id, QPoint(105, 105)) == 0);
    CHECK(std::abs(anchor_at(document, scene.layer_id, 0).anchor_x - 160.0) < 0.6);
    // One undo entry brings the whole rectangle back.
    require_action_by_text(scene.window, QStringLiteral("Undo"))->trigger();
    QApplication::processEvents();
    CHECK(layer_fill_alpha_at(document, scene.layer_id, QPoint(105, 105)) > 200);
    CHECK(std::abs(anchor_at(document, scene.layer_id, 0).anchor_x - 100.0) < 0.5);
  }
  {
    PathTransformGestureScene scene;
    auto& document = patchy::ui::MainWindowTestAccess::document(scene.window);
    scene.press_and_move(QPoint(300, 160), {QPoint(270, 160), QPoint(250, 160)}, Qt::NoModifier);
    CHECK(layer_fill_alpha_at(document, scene.layer_id, QPoint(280, 160)) == 0);  // a plain scale follows too
    send_mouse(*scene.canvas, QEvent::MouseButtonRelease, scene.canvas->widget_position_for_document_point(QPoint(250, 160)),
               Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    send_key(*scene.canvas, Qt::Key_Escape);
    QApplication::processEvents();
    CHECK(!scene.canvas->path_transform_active());
    CHECK(layer_fill_alpha_at(document, scene.layer_id, QPoint(280, 160)) > 200);  // Esc restores the fill
    CHECK(std::abs(anchor_at(document, scene.layer_id, 2).anchor_x - 300.0) < 0.5);
  }
  {
    PathTransformGestureScene scene;
    auto& document = patchy::ui::MainWindowTestAccess::document(scene.window);
    scene.press_and_move(QPoint(300, 160), {QPoint(250, 160)}, Qt::NoModifier);
    send_mouse(*scene.canvas, QEvent::MouseButtonRelease, scene.canvas->widget_position_for_document_point(QPoint(250, 160)),
               Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    require_action_by_text(scene.window, QStringLiteral("Undo"))->trigger();
    QApplication::processEvents();
    CHECK(!scene.canvas->path_transform_active());
    CHECK(document.find_layer(scene.layer_id) != nullptr);  // the history behind the session is untouched
    CHECK(layer_fill_alpha_at(document, scene.layer_id, QPoint(280, 160)) > 200);
    CHECK(std::abs(anchor_at(document, scene.layer_id, 2).anchor_x - 300.0) < 0.5);
  }
}

// The held modifiers show Free Transform's gesture cursors over the box handles.
void ui_path_transform_modifiers_show_gesture_cursors() {
  PathTransformGestureScene scene;
  auto& canvas = *scene.canvas;
  const auto image_of = [](const QCursor& cursor) {
    return cursor.pixmap().toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
  };
  using patchy::ui::TransformGestureCursor;
  CHECK(hover(canvas, QPoint(100, 100)).shape() == Qt::SizeFDiagCursor);
  CHECK(image_of(hover(canvas, QPoint(100, 100), Qt::ControlModifier)) ==
        image_of(patchy::ui::transform_gesture_cursor(TransformGestureCursor::Distort)));
  CHECK(image_of(hover(canvas, QPoint(100, 100), Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier)) ==
        image_of(patchy::ui::transform_gesture_cursor(TransformGestureCursor::Perspective)));
  CHECK(image_of(hover(canvas, QPoint(200, 100), Qt::ControlModifier | Qt::ShiftModifier)) ==
        image_of(patchy::ui::transform_gesture_cursor(TransformGestureCursor::Skew)));
  CHECK(image_of(hover(canvas, QPoint(300, 160), Qt::ControlModifier)) ==
        image_of(patchy::ui::transform_gesture_cursor(TransformGestureCursor::Distort)));
  CHECK(hover(canvas, QPoint(200, 160), Qt::ControlModifier).shape() == Qt::SizeAllCursor);  // inside still moves
  CHECK(hover(canvas, QPoint(300, 160)).shape() == Qt::SizeHorCursor);
  // A distorted box's handles follow the true quad.
  scene.press_and_move(QPoint(100, 100), {QPoint(130, 120)}, Qt::ControlModifier);
  send_mouse(canvas, QEvent::MouseButtonRelease, canvas.widget_position_for_document_point(QPoint(130, 120)),
             Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
  QApplication::processEvents();
  CHECK(hover(canvas, QPoint(130, 120)).shape() == Qt::SizeFDiagCursor);
  CHECK(hover(canvas, QPoint(110, 110)).shape() != Qt::SizeFDiagCursor);  // the old corner is empty now
  save_widget_artifact("ui_path_transform_distorted_box", canvas);
}

// A rect shape layer whose path holds two shape groups: (100,100)-(300,220) and
// the same rectangle 250 px to the right, (350,100)-(550,220), in its own group.
patchy::LayerId make_two_group_shape_layer(patchy::ui::MainWindow& window, patchy::ui::CanvasWidget& canvas) {
  const auto layer_id = make_rect_shape_layer(window, canvas);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  auto* layer = document.find_layer(layer_id);
  CHECK(layer != nullptr && layer->vector_shape() != nullptr);
  if (layer == nullptr || layer->vector_shape() == nullptr) {
    return layer_id;
  }
  auto content = *layer->vector_shape();
  auto copy = content.path.subpaths[0];
  copy.shape_group = content.path.next_shape_group();
  for (auto& anchor : copy.anchors) {
    anchor.anchor_x += 250.0;
    anchor.in_x += 250.0;
    anchor.out_x += 250.0;
  }
  content.path.subpaths.push_back(copy);
  layer->set_vector_shape(std::move(content));
  canvas.document_changed();
  QApplication::processEvents();
  return layer_id;
}

const patchy::PathAnchor& group_anchor_at(patchy::Document& document, patchy::LayerId id, std::size_t subpath,
                                          std::size_t index) {
  const auto* layer = std::as_const(document).find_layer(id);
  CHECK(layer != nullptr);
  return layer->vector_shape()->path.subpaths[subpath].anchors[index];
}

// Tool roles (GitHub issue 248): switching to Path Select turns carried-over
// points into their whole shapes. The Pen keeps showing a selected point's knobs
// (its Ctrl latch, which is Direct Select, grabs them); Path Select shows none.
void ui_path_select_switch_expands_points_and_pen_keeps_selected_knobs() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = make_two_group_shape_layer(window, *canvas);
  {
    auto* layer = document.find_layer(layer_id);
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
  require_action(window, "toolDirectSelectAction")->trigger();
  QApplication::processEvents();
  pen_click(*canvas, QPoint(100, 100));
  CHECK(canvas->path_edit_selected_anchor_count() == 1);
  CHECK(accent_overlay_near(*canvas, QPoint(160, 30)));  // Direct Select draws the knob
  require_action(window, "toolPenAction")->trigger();
  QApplication::processEvents();
  CHECK(accent_overlay_near(*canvas, QPoint(160, 30)));  // the Pen keeps the selected point's knob
  require_action(window, "toolDirectSelectAction")->trigger();
  QApplication::processEvents();
  pen_click(*canvas, QPoint(100, 100));
  require_action(window, "toolPathSelectAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->path_edit_selected_anchor_count() == 4);  // its whole group, not the other one
  shape_drag(*canvas, QPoint(200, 220), QPoint(210, 220));
  CHECK(std::abs(group_anchor_at(document, layer_id, 0, 0).anchor_x - 110.0) < 1.0);
  CHECK(std::abs(group_anchor_at(document, layer_id, 1, 0).anchor_x - 350.0) < 0.5);
}

// The point selection is (subpath, anchor) indices into one path: when the
// active layer changes (a Layers-panel click), it is dropped, so Delete or a
// nudge never edits another layer's points (GitHub issue 248).
void ui_point_selection_drops_when_the_active_layer_changes() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto first_id = make_rect_shape_layer(window, *canvas);  // (100,100)-(300,220)
  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  shape_drag(*canvas, QPoint(400, 100), QPoint(600, 220));  // "Rectangle 2", active
  const auto second_id = *document.active_layer_id();
  canvas->set_view_zoom_centered(1.0);
  require_action(window, "toolDirectSelectAction")->trigger();
  QApplication::processEvents();
  shape_drag(*canvas, QPoint(380, 80), QPoint(620, 240));  // marquee around Rectangle 2
  CHECK(canvas->path_edit_selected_anchor_count() == 4);

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* item = require_layer_item(*layer_list, QStringLiteral("Rectangle 1"));
  const auto center = layer_list->visualItemRect(item).center();
  send_mouse(*layer_list->viewport(), QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*layer_list->viewport(), QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(document.active_layer_id() == first_id);
  CHECK(!canvas->path_edit_has_selection());
  CHECK(canvas->path_edit_selected_anchor_count() == 0);
  // Before the fix the four stale keys stayed and indexed Rectangle 1's corners, so
  // Delete or a nudge edited a layer the user never selected points on.
  CHECK(anchor_count(document, first_id) == 4);
  CHECK(anchor_count(document, second_id) == 4);
}

// Path Select: Shift+click on a shape's outline or fill adds it to the selection;
// Shift+click on a selected shape deselects it (GitHub issue 248).
void ui_path_select_shift_click_adds_and_removes_shapes() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  make_two_group_shape_layer(window, *canvas);
  canvas->set_view_zoom_centered(1.0);
  require_action(window, "toolPathSelectAction")->trigger();
  QApplication::processEvents();
  const auto shift_click = [&](QPoint point) {
    const auto widget_point = canvas->widget_position_for_document_point(point);
    send_mouse(*canvas, QEvent::MouseButtonPress, widget_point, Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
    send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point, Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
    QApplication::processEvents();
  };
  pen_click(*canvas, QPoint(200, 220));  // the first group's outline
  CHECK(canvas->path_edit_selected_anchor_count() == 4);
  shift_click(QPoint(450, 220));  // the second group's outline adds it
  CHECK(canvas->path_edit_selected_anchor_count() == 8);
  shift_click(QPoint(200, 160));  // Shift on the selected first group's fill deselects it
  CHECK(canvas->path_edit_selected_anchor_count() == 4);
  shift_click(QPoint(200, 160));  // and adds it back
  CHECK(canvas->path_edit_selected_anchor_count() == 8);
  shift_click(QPoint(450, 100));  // Shift on a selected outline deselects that one
  CHECK(canvas->path_edit_selected_anchor_count() == 4);
}

// Path Select Ctrl+T transforms only the selected shapes of the path.
void ui_path_select_transform_moves_only_selected_shapes() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = make_two_group_shape_layer(window, *canvas);
  canvas->set_view_zoom_centered(1.0);
  require_action(window, "toolPathSelectAction")->trigger();
  QApplication::processEvents();
  pen_click(*canvas, QPoint(450, 220));  // select the second group
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->path_transform_active());
  shape_drag(*canvas, QPoint(450, 160), QPoint(450, 200));  // move inside its box
  canvas->commit_path_transform();
  QApplication::processEvents();
  CHECK(std::abs(group_anchor_at(document, layer_id, 1, 0).anchor_y - 140.0) < 1.0);
  CHECK(std::abs(group_anchor_at(document, layer_id, 0, 0).anchor_y - 100.0) < 0.5);  // untouched
}

// Presses with modifiers on the canvas, document coordinates.
void modified_drag(patchy::ui::CanvasWidget& canvas, QPoint from, QPoint to, Qt::KeyboardModifiers modifiers) {
  const auto start = canvas.widget_position_for_document_point(from);
  const auto end = canvas.widget_position_for_document_point(to);
  send_mouse(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton, modifiers);
  send_mouse(canvas, QEvent::MouseMove, (start + end) / 2, Qt::NoButton, Qt::LeftButton, modifiers);
  send_mouse(canvas, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton, modifiers);
  send_mouse(canvas, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton, modifiers);
  QApplication::processEvents();
}

// Direct Select (GitHub issue 248): Alt+click on a point or segment selects its
// whole subpath (only that one, not the rest of the path).
void ui_direct_select_alt_click_selects_the_whole_subpath() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = make_two_group_shape_layer(window, *canvas);
  canvas->set_view_zoom_centered(1.0);
  require_action(window, "toolDirectSelectAction")->trigger();
  QApplication::processEvents();
  modified_drag(*canvas, QPoint(200, 220), QPoint(200, 220), Qt::AltModifier);  // a segment of the first
  CHECK(canvas->path_edit_selected_anchor_count() == 4);
  modified_drag(*canvas, QPoint(550, 220), QPoint(570, 230), Qt::AltModifier);  // a corner of the second
  CHECK(canvas->path_edit_selected_anchor_count() == 4);
  CHECK(std::abs(group_anchor_at(document, layer_id, 1, 0).anchor_x - 370.0) < 1.0);  // its whole subpath moved
  CHECK(std::abs(group_anchor_at(document, layer_id, 0, 0).anchor_x - 100.0) < 0.5);
}

// Direct Select handle drags (GitHub issue 248): a smooth pair stays colinear and
// the opposite handle keeps its own length (no forced mirror); Alt at the press
// breaks the pair and moves only the dragged handle.
void ui_direct_select_handle_drag_keeps_the_pair_colinear_and_alt_breaks_it() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = make_rect_shape_layer(window, *canvas);  // (100,100)-(300,220)
  {
    auto* layer = document.find_layer(layer_id);
    auto content = *layer->vector_shape();
    auto& anchor = content.path.subpaths[0].anchors[0];
    anchor.smooth = true;
    anchor.out_x = 160.0;  // 60, -70 from the anchor
    anchor.out_y = 30.0;
    anchor.in_x = 70.0;  // a shorter opposite handle: -30, 35
    anchor.in_y = 135.0;
    layer->set_vector_shape(std::move(content));
  }
  canvas->set_view_zoom_centered(1.0);
  require_action(window, "toolDirectSelectAction")->trigger();
  QApplication::processEvents();
  pen_click(*canvas, QPoint(100, 100));
  shape_drag(*canvas, QPoint(160, 30), QPoint(200, 30));  // the out knob, 40 px right
  {
    const auto& anchor = anchor_at(document, layer_id, 0);
    CHECK(std::abs(anchor.out_x - 200.0) < 1.0 && std::abs(anchor.out_y - 30.0) < 1.0);
    const double in_length = std::hypot(anchor.in_x - 100.0, anchor.in_y - 100.0);
    CHECK(std::abs(in_length - std::hypot(30.0, 35.0)) < 0.5);  // its own length, not mirrored
    const double cross = (anchor.out_x - 100.0) * (anchor.in_y - 100.0) - (anchor.out_y - 100.0) * (anchor.in_x - 100.0);
    CHECK(std::abs(cross) < 1.0);  // colinear
    CHECK((anchor.out_x - 100.0) * (anchor.in_x - 100.0) + (anchor.out_y - 100.0) * (anchor.in_y - 100.0) < 0.0);
    CHECK(anchor.smooth);
  }
  const QPointF in_before(anchor_at(document, layer_id, 0).in_x, anchor_at(document, layer_id, 0).in_y);
  const QPointF out_before(anchor_at(document, layer_id, 0).out_x, anchor_at(document, layer_id, 0).out_y);
  modified_drag(*canvas, in_before.toPoint(), in_before.toPoint() + QPoint(0, 30), Qt::AltModifier);
  {
    const auto& anchor = anchor_at(document, layer_id, 0);
    CHECK(!anchor.smooth);
    CHECK(std::abs(anchor.in_y - (in_before.y() + 30.0)) < 1.5);
    CHECK(std::abs(anchor.out_x - out_before.x()) < 0.01 && std::abs(anchor.out_y - out_before.y()) < 0.01);
  }
}

// Ctrl swaps the two arrows while held (GitHub issue 248): Path Select plus Ctrl
// drags one point; Direct Select plus Ctrl moves the whole shape. The cursor
// shows the arrow in effect.
void ui_ctrl_swaps_the_arrow_tools() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = make_rect_shape_layer(window, *canvas);  // (100,100)-(300,220)
  canvas->set_view_zoom_centered(1.0);
  const auto image_of = [](const QCursor& cursor) { return cursor.pixmap().toImage(); };
  require_action(window, "toolPathSelectAction")->trigger();
  QApplication::processEvents();
  CHECK(image_of(hover(*canvas, QPoint(500, 400))) == image_of(patchy::ui::path_select_cursor()));
  CHECK(image_of(hover(*canvas, QPoint(500, 400), Qt::ControlModifier)) ==
        image_of(patchy::ui::direct_select_cursor()));
  pen_click(*canvas, QPoint(200, 100));  // Path Select: the whole shape
  modified_drag(*canvas, QPoint(300, 220), QPoint(320, 230), Qt::ControlModifier);
  CHECK(std::abs(anchor_at(document, layer_id, 2).anchor_x - 320.0) < 1.0);
  CHECK(std::abs(anchor_at(document, layer_id, 0).anchor_x - 100.0) < 0.5);  // one point only
  CHECK(image_of(hover(*canvas, QPoint(500, 400))) == image_of(patchy::ui::path_select_cursor()));

  require_action(window, "toolDirectSelectAction")->trigger();
  QApplication::processEvents();
  CHECK(image_of(hover(*canvas, QPoint(500, 400))) == image_of(patchy::ui::direct_select_cursor()));
  pen_click(*canvas, QPoint(100, 100));  // Direct Select: one point
  CHECK(canvas->path_edit_selected_anchor_count() == 1);
  modified_drag(*canvas, QPoint(200, 220), QPoint(210, 220), Qt::ControlModifier);  // Ctrl: Path Select
  CHECK(std::abs(anchor_at(document, layer_id, 0).anchor_x - 110.0) < 1.0);
  CHECK(std::abs(anchor_at(document, layer_id, 1).anchor_x - 310.0) < 1.0);
  CHECK(std::abs(anchor_at(document, layer_id, 2).anchor_x - 330.0) < 1.0);  // the whole shape
}

// Path Select Alt-drag duplicates the shape (GitHub issue 248): the copy moves,
// the original stays, the copy paints with the layer's fill (GitHub issue 312),
// and one Undo removes the copy.
void ui_path_select_alt_drag_duplicates_the_shape() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = make_rect_shape_layer(window, *canvas);  // (100,100)-(300,220)
  canvas->set_view_zoom_centered(1.0);
  require_action(window, "toolPathSelectAction")->trigger();
  QApplication::processEvents();
  modified_drag(*canvas, QPoint(200, 220), QPoint(260, 300), Qt::AltModifier);
  const auto* layer = std::as_const(document).find_layer(layer_id);
  CHECK(layer != nullptr && layer->vector_shape() != nullptr);
  const auto& path = layer->vector_shape()->path;
  CHECK(path.subpaths.size() == 2);
  CHECK(path.subpaths[0].shape_group != path.subpaths[1].shape_group);
  CHECK(std::abs(path.subpaths[0].anchors[0].anchor_x - 100.0) < 0.5);  // the original stays
  CHECK(std::abs(path.subpaths[1].anchors[0].anchor_x - 160.0) < 1.0);  // the copy moved
  CHECK(std::abs(path.subpaths[1].anchors[0].anchor_y - 180.0) < 1.0);
  CHECK(canvas->path_edit_selected_anchor_count() == 4);  // the copy is selected
  // The copy (160,180)-(360,300) is filled, in the layer and on the canvas.
  const auto canvas_dark_at = [&](QPoint document_point) {
    return canvas->grab().toImage().pixelColor(canvas->widget_position_for_document_point(document_point)).lightness() <
           100;
  };
  CHECK(layer_fill_alpha_at(document, layer_id, QPoint(340, 290)) > 200);
  CHECK(layer_fill_alpha_at(document, layer_id, QPoint(120, 110)) > 200);  // the original too
  CHECK(canvas_dark_at(QPoint(340, 290)));
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(std::as_const(document).find_layer(layer_id)->vector_shape()->path.subpaths.size() == 1);
  CHECK(layer_fill_alpha_at(document, layer_id, QPoint(340, 290)) == 0);
  CHECK(!canvas_dark_at(QPoint(340, 290)));
}

// On a merged shape layer each shape paints from its own part, so an Alt-drag
// copy joins its original's part and paints in that part's color (GitHub issue 312).
void ui_path_select_alt_drag_duplicates_a_merged_shape() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = make_rect_shape_layer(window, *canvas);  // (100,100)-(300,220)
  canvas->set_view_zoom_centered(1.0);
  {
    // A merged layer: the black rectangle and a red (400,300)-(500,380) one, each its own part.
    auto* layer = document.find_layer(layer_id);
    auto content = *std::as_const(*layer).vector_shape();
    auto red = content.path.subpaths[0];
    red.shape_group = content.path.next_shape_group();
    red.anchors = {};
    for (const auto& [x, y] : std::initializer_list<std::pair<double, double>>{
             {400.0, 300.0}, {500.0, 300.0}, {500.0, 380.0}, {400.0, 380.0}}) {
      patchy::PathAnchor anchor;
      anchor.anchor_x = anchor.in_x = anchor.out_x = x;
      anchor.anchor_y = anchor.in_y = anchor.out_y = y;
      red.anchors.push_back(anchor);
    }
    patchy::VectorShapePart black_part;
    black_part.groups = {content.path.subpaths[0].shape_group};
    black_part.fill = content.fill;
    black_part.stroke = content.stroke;
    patchy::VectorShapePart red_part = black_part;
    red_part.groups = {red.shape_group};
    red_part.fill.kind = patchy::VectorFillKind::Solid;
    red_part.fill.color = {255, 0, 0};
    content.path.subpaths.push_back(std::move(red));
    content.parts = {black_part, red_part};
    layer->set_vector_shape(std::move(content));
    patchy::update_vector_shape_raster(*layer, patchy::Rect::from_size(document.width(), document.height()),
                                       &std::as_const(document).metadata().patterns);
  }
  const auto pixel_at = [&](QPoint point) {
    const auto* layer = std::as_const(document).find_layer(layer_id);
    const auto bounds = layer->bounds();
    const auto& pixels = layer->pixels();
    const QPoint local(point.x() - bounds.x, point.y() - bounds.y);
    if (local.x() < 0 || local.y() < 0 || local.x() >= pixels.width() || local.y() >= pixels.height()) {
      return std::array<int, 4>{0, 0, 0, 0};
    }
    const auto* p = pixels.pixel(local.x(), local.y());
    return std::array<int, 4>{p[0], p[1], p[2], p[3]};
  };
  CHECK(pixel_at(QPoint(450, 340))[0] > 200 && pixel_at(QPoint(450, 340))[3] > 200);  // red, painted
  require_action(window, "toolPathSelectAction")->trigger();
  QApplication::processEvents();
  modified_drag(*canvas, QPoint(450, 340), QPoint(450, 190), Qt::AltModifier);  // copy at (400,150)-(500,230)
  save_widget_artifact("ui_path_select_alt_drag_duplicates_a_merged_shape", *canvas);
  const auto& content = *std::as_const(document).find_layer(layer_id)->vector_shape();
  CHECK(content.path.subpaths.size() == 3);
  CHECK(content.parts.size() == 2);
  const auto copy = pixel_at(QPoint(450, 220));
  CHECK(copy[3] > 200 && copy[0] > 200 && copy[1] < 50);  // the copy paints red
  CHECK(pixel_at(QPoint(450, 340))[3] > 200);             // the original stays
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(std::as_const(document).find_layer(layer_id)->vector_shape()->path.subpaths.size() == 2);
  CHECK(pixel_at(QPoint(450, 220))[3] == 0);
}

// Pen (GitHub issue 248): Shift keeps a new point and a dragged handle on a
// 45-degree line; Ctrl+click away ends the open path.
void ui_pen_shift_keeps_45_degrees_and_ctrl_click_ends_the_path() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_view_zoom_centered(1.0);
  require_action(window, "toolPenAction")->trigger();
  QApplication::processEvents();
  pen_click(*canvas, QPoint(100, 100));
  modified_drag(*canvas, QPoint(200, 130), QPoint(200, 130), Qt::ShiftModifier);  // 17 degrees snaps to 0
  // A new point dragged with Shift: the handle snaps to 45 degrees.
  {
    const auto start = canvas->widget_position_for_document_point(QPoint(300, 100));
    send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseMove, canvas->widget_position_for_document_point(QPoint(350, 145)), Qt::NoButton,
               Qt::LeftButton, Qt::ShiftModifier);
    send_mouse(*canvas, QEvent::MouseButtonRelease, canvas->widget_position_for_document_point(QPoint(350, 145)),
               Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
    QApplication::processEvents();
  }
  CHECK(canvas->pen_session_active());
  modified_drag(*canvas, QPoint(500, 500), QPoint(500, 500), Qt::ControlModifier);  // Ctrl+click away
  CHECK(!canvas->pen_session_active());
  const auto* path = canvas->path_edit_target_path();
  CHECK(path != nullptr && !path->subpaths.empty());
  if (path == nullptr || path->subpaths.empty()) {
    return;
  }
  const auto& subpath = path->subpaths.back();
  CHECK(!subpath.closed && subpath.anchors.size() == 3);
  CHECK(std::abs(subpath.anchors[1].anchor_x - 200.0) < 1.0 && std::abs(subpath.anchors[1].anchor_y - 100.0) < 1.0);
  const auto& curve = subpath.anchors[2];
  const double dx = curve.out_x - curve.anchor_x;
  const double dy = curve.out_y - curve.anchor_y;
  CHECK(std::abs(std::abs(dx) - std::abs(dy)) < 1.0);  // the 45-degree diagonal
  CHECK(dx > 1.0);
}

// Pen (GitHub issue 248): a click on an open path's end point continues that path
// (it is not deleted), from either end, as one more subpath edit; the hover badge
// and hint say so.
void ui_pen_click_on_an_open_end_continues_the_path() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_view_zoom_centered(1.0);
  require_action(window, "toolPenAction")->trigger();
  QApplication::processEvents();
  pen_click(*canvas, QPoint(100, 300));
  pen_click(*canvas, QPoint(200, 200));
  pen_click(*canvas, QPoint(300, 300));
  send_key(*canvas, Qt::Key_Return);  // commit the open path
  QApplication::processEvents();
  const auto subpath_of = [&]() -> const patchy::PathSubpath& {
    const auto* path = canvas->path_edit_target_path();
    CHECK(path != nullptr && path->subpaths.size() == 1);
    return path->subpaths[0];
  };
  CHECK(subpath_of().anchors.size() == 3);
  window.statusBar()->showMessage(QStringLiteral("marker"));
  hover(*canvas, QPoint(500, 500));
  hover(*canvas, QPoint(300, 300));
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Click to continue the path from this end point"));
  pen_click(*canvas, QPoint(300, 300));  // the last end
  CHECK(canvas->pen_session_active());
  pen_click(*canvas, QPoint(400, 200));
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(subpath_of().anchors.size() == 4);
  CHECK(std::abs(subpath_of().anchors[3].anchor_x - 400.0) < 1.0);
  pen_click(*canvas, QPoint(100, 300));  // the first end
  pen_click(*canvas, QPoint(0, 200));
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(subpath_of().anchors.size() == 5);
  CHECK(std::abs(subpath_of().anchors[0].anchor_x - 0.0) < 1.0);  // grows at the front, same direction
  CHECK(std::abs(subpath_of().anchors[1].anchor_x - 100.0) < 1.0);
  CHECK(std::abs(subpath_of().anchors[4].anchor_x - 400.0) < 1.0);
  CHECK(!subpath_of().closed);
}

// The arrow tools' own cursors (GitHub issue 248): a dark Path Select arrow and a
// light Direct Select arrow with an anchor square, distinct from each other.
void ui_path_select_tools_show_their_own_arrow_cursors() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_view_zoom_centered(1.0);
  const auto image_of = [](const QCursor& cursor) { return cursor.pixmap().toImage(); };
  const auto path_arrow = image_of(patchy::ui::path_select_cursor());
  const auto direct_arrow = image_of(patchy::ui::direct_select_cursor());
  CHECK(!path_arrow.isNull() && !direct_arrow.isNull());
  CHECK(path_arrow != direct_arrow);
  CHECK(patchy::ui::path_select_cursor().hotSpot() == QPoint(3, 3));
  CHECK(patchy::ui::direct_select_cursor().hotSpot() == QPoint(3, 3));
  QImage sheet(QSize(path_arrow.width() * 2 + 8, path_arrow.height()), QImage::Format_ARGB32_Premultiplied);
  sheet.fill(QColor(128, 128, 128));
  {
    QPainter painter(&sheet);
    painter.drawImage(0, 0, path_arrow);
    painter.drawImage(path_arrow.width() + 8, 0, direct_arrow);
  }
  save_image_artifact("ui_path_roles_arrow_cursors", sheet.scaled(sheet.size() * 4));
  // Inside the arrow's body: dark for Path Select, light for Direct Select (device
  // pixels, so the probe scales with the cursor's pixel ratio).
  const auto body = [](const QImage& image) {
    const auto ratio = image.devicePixelRatio() > 0.0 ? image.devicePixelRatio() : 1.0;
    return image.pixelColor(static_cast<int>(5.0 * ratio), static_cast<int>(11.0 * ratio));
  };
  fprintf(stderr, "  arrow bodies: path %d (alpha %d), direct %d (alpha %d)\n", body(path_arrow).lightness(),
          body(path_arrow).alpha(), body(direct_arrow).lightness(), body(direct_arrow).alpha());
  CHECK(body(path_arrow).alpha() > 200 && body(path_arrow).lightness() < 90);
  CHECK(body(direct_arrow).alpha() > 200 && body(direct_arrow).lightness() > 200);
  require_action(window, "toolPathSelectAction")->trigger();
  QApplication::processEvents();
  CHECK(image_of(hover(*canvas, QPoint(500, 400))) == path_arrow);
  require_action(window, "toolDirectSelectAction")->trigger();
  QApplication::processEvents();
  CHECK(image_of(hover(*canvas, QPoint(500, 400))) == direct_arrow);
}

// A Direct Select anchor or handle drag re-renders the shape's fill on every move,
// not only on release (the live-fill rule the path transform box follows too):
// a pixel the new outline leaves is empty mid-drag.
void ui_direct_select_drags_redraw_the_fill_live() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = make_rect_shape_layer(window, *canvas);  // (100,100)-(300,220)
  canvas->set_view_zoom_centered(1.0);
  require_action(window, "toolDirectSelectAction")->trigger();
  QApplication::processEvents();
  // An anchor drag: the corner moves in, the corner's pixel empties mid-drag.
  CHECK(layer_fill_alpha_at(document, layer_id, QPoint(105, 105)) > 200);
  const auto corner = canvas->widget_position_for_document_point(QPoint(100, 100));
  send_mouse(*canvas, QEvent::MouseButtonPress, corner, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, canvas->widget_position_for_document_point(QPoint(150, 140)), Qt::NoButton,
             Qt::LeftButton);
  QApplication::processEvents();
  save_widget_artifact("ui_direct_select_anchor_drag_mid", *canvas);
  CHECK(layer_fill_alpha_at(document, layer_id, QPoint(105, 105)) == 0);
  CHECK(layer_fill_alpha_at(document, layer_id, QPoint(200, 170)) > 200);
  send_mouse(*canvas, QEvent::MouseButtonRelease, canvas->widget_position_for_document_point(QPoint(150, 140)),
             Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(layer_fill_alpha_at(document, layer_id, QPoint(105, 105)) > 200);

  // A handle drag: the top edge bulges up through a smooth corner, then a knob
  // drag flattens it, and the bulge's pixel empties mid-drag.
  {
    auto curved = *canvas->path_edit_target_path();
    auto& anchor = curved.subpaths[0].anchors[0];
    anchor.smooth = true;
    anchor.out_x = 160.0;
    anchor.out_y = 30.0;
    anchor.in_x = 40.0;
    anchor.in_y = 170.0;
    set_target_path_through_edits(*canvas, std::move(curved));
  }
  CHECK(layer_fill_alpha_at(document, layer_id, QPoint(150, 80)) > 200);
  pen_click(*canvas, QPoint(100, 100));
  const auto knob = canvas->widget_position_for_document_point(QPoint(160, 30));
  send_mouse(*canvas, QEvent::MouseButtonPress, knob, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, canvas->widget_position_for_document_point(QPoint(160, 100)), Qt::NoButton,
             Qt::LeftButton);
  QApplication::processEvents();
  CHECK(std::abs(anchor_at(document, layer_id, 0).out_y - 100.0) < 1.0);
  CHECK(layer_fill_alpha_at(document, layer_id, QPoint(150, 80)) == 0);
  send_mouse(*canvas, QEvent::MouseButtonRelease, canvas->widget_position_for_document_point(QPoint(160, 100)),
             Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
}

// Under Path Select the selection entries of the path menu name shapes, the
// point entries keep their names; Direct Select's name points. Delete Selected
// Shapes removes the selected shape (GitHub issue 248 tool roles).
void ui_path_select_context_menu_names_shapes() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = make_two_group_shape_layer(window, *canvas);
  canvas->set_view_zoom_centered(1.0);
  require_action(window, "toolPathSelectAction")->trigger();
  QApplication::processEvents();
  pen_click(*canvas, QPoint(450, 220));  // the second shape
  with_path_context_menu([](QMenu& menu) {
    CHECK(require_menu_action(menu, "pathMenuDeleteSelectedAction")->text() == QStringLiteral("Delete Selected Shapes"));
    CHECK(require_menu_action(menu, "pathMenuDeselectAction")->text() == QStringLiteral("Deselect Shapes"));
    CHECK(require_menu_action(menu, "pathMenuAddAnchorAction")->text() == QStringLiteral("Add Anchor Point"));
    CHECK(require_menu_action(menu, "pathMenuDeleteAnchorAction")->text() == QStringLiteral("Delete Anchor Point"));
    CHECK(require_menu_action(menu, "pathMenuConvertPointAction")->text() == QStringLiteral("Convert Point"));
    require_menu_action(menu, "pathMenuDeleteSelectedAction")->trigger();
  });
  open_path_menu_at(*canvas, QPoint(600, 400));
  const auto* layer = std::as_const(document).find_layer(layer_id);
  CHECK(layer != nullptr && layer->vector_shape() != nullptr && layer->vector_shape()->path.subpaths.size() == 1);
  require_action(window, "toolDirectSelectAction")->trigger();
  QApplication::processEvents();
  pen_click(*canvas, QPoint(100, 100));
  with_path_context_menu([](QMenu& menu) {
    CHECK(require_menu_action(menu, "pathMenuDeleteSelectedAction")->text() == QStringLiteral("Delete Selected Points"));
    CHECK(require_menu_action(menu, "pathMenuDeselectAction")->text() == QStringLiteral("Deselect Points"));
    menu.close();
  });
  open_path_menu_at(*canvas, QPoint(600, 400));
}

// GitHub issue 315: deleting the only shape of a shape layer under Path Select
// deletes the layer in the same undo step. An empty shape path means full
// coverage, so keeping the layer filled the whole canvas with nothing left to
// select. One Undo brings the layer back with its path.
void ui_path_select_delete_last_shape_removes_the_layer() {
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
  canvas->set_view_zoom_centered(1.0);
  require_action(window, "toolPathSelectAction")->trigger();
  QApplication::processEvents();
  const auto layers_before = document.layers().size();

  pen_click(*canvas, QPoint(200, 100));  // Rectangle 1's top edge picks it
  CHECK(document.active_layer_id() == first_id);
  CHECK(canvas->path_edit_selected_anchor_count() == 4);
  const auto undo_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  QKeyEvent override_event(QEvent::ShortcutOverride, Qt::Key_Delete, Qt::NoModifier);
  QApplication::sendEvent(canvas, &override_event);
  CHECK(override_event.isAccepted());  // layer.clear never fires
  send_key(*canvas, Qt::Key_Delete);
  QApplication::processEvents();
  CHECK(std::as_const(document).find_layer(first_id) == nullptr);
  CHECK(std::as_const(document).find_layer(second_id) != nullptr);
  CHECK(document.layers().size() == layers_before - 1);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before + 1);
  CHECK(canvas->path_edit_selected_anchor_count() == 0);

  // Path Select still picks the other shape.
  pen_click(*canvas, QPoint(380, 60));
  CHECK(document.active_layer_id() == second_id);
  CHECK(canvas->path_edit_selected_anchor_count() == 4);

  patchy::ui::MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(document.layers().size() == layers_before);
  CHECK(anchor_count(document, first_id) == 4);
  CHECK(std::abs(anchor_at(document, first_id, 0).anchor_x - 100.0) < 0.5);
}

// Deleting one of two shapes on a layer keeps the layer with the other
// (GitHub issue 315's other half).
void ui_path_select_delete_one_of_two_shapes_keeps_the_layer() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = make_two_group_shape_layer(window, *canvas);
  canvas->set_view_zoom_centered(1.0);
  require_action(window, "toolPathSelectAction")->trigger();
  QApplication::processEvents();
  const auto layers_before = document.layers().size();
  pen_click(*canvas, QPoint(450, 220));  // the second shape
  CHECK(canvas->path_edit_selected_anchor_count() == 4);
  send_key(*canvas, Qt::Key_Delete);
  QApplication::processEvents();
  CHECK(document.layers().size() == layers_before);
  const auto* layer = std::as_const(document).find_layer(layer_id);
  CHECK(layer != nullptr && layer->vector_shape() != nullptr);
  CHECK(layer->vector_shape()->path.subpaths.size() == 1);
  CHECK(std::abs(group_anchor_at(document, layer_id, 0, 0).anchor_x - 100.0) < 0.5);
  CHECK(layer_fill_alpha_at(document, layer_id, QPoint(200, 160)) > 0);  // the kept shape
  CHECK(layer_fill_alpha_at(document, layer_id, QPoint(450, 160)) == 0);  // the deleted one
  CHECK(layer_fill_alpha_at(document, layer_id, QPoint(40, 300)) == 0);   // no full coverage
}

// The path context menu's Delete Selected Shapes follows the same rule as the
// Delete key (GitHub issue 315).
void ui_path_menu_delete_last_shape_removes_the_layer() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = make_rect_shape_layer(window, *canvas);
  canvas->set_view_zoom_centered(1.0);
  require_action(window, "toolPathSelectAction")->trigger();
  QApplication::processEvents();
  const auto layers_before = document.layers().size();
  pen_click(*canvas, QPoint(200, 100));
  CHECK(canvas->path_edit_selected_anchor_count() == 4);
  with_path_context_menu([](QMenu& menu) {
    auto* action = require_menu_action(menu, "pathMenuDeleteSelectedAction");
    CHECK(action->isEnabled());
    action->trigger();
  });
  open_path_menu_at(*canvas, QPoint(600, 400));
  CHECK(std::as_const(document).find_layer(layer_id) == nullptr);
  CHECK(document.layers().size() == layers_before - 1);
  patchy::ui::MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(anchor_count(document, layer_id) == 4);
}

// GitHub issue 396: the path tools' right-click menu also offers the Paths
// panel's commands for the path (the panel's own QActions), and works when no
// panel row is targeted: it targets the path under the tools first, keeping its
// point selection.
void ui_path_context_menu_offers_paths_panel_commands() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  CHECK(mode_combo != nullptr);
  mode_combo->setCurrentIndex(1);  // Path
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  CHECK(radius_spin != nullptr);
  radius_spin->setValue(0);
  shape_drag(*canvas, QPoint(100, 100), QPoint(300, 220));
  CHECK(document.work_path() != nullptr);
  require_action(window, "toolDirectSelectAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->active_document_path().has_value());  // drawing targets the work path row

  static constexpr std::array<const char*, 6> kCommands{
      "pathMakeSelectionAction", "pathFillAction",      "pathStrokeAction",
      "pathSimplifyAction",      "pathDuplicateAction", "pathDeleteAction"};
  with_path_context_menu([&](QMenu& menu) {
    CHECK(menu.actions().front()->objectName() == QStringLiteral("pathMenuAddAnchorAction"));
    for (const auto* name : kCommands) {
      auto* action = require_menu_action(menu, name);
      CHECK(action != nullptr && action->isEnabled());
      CHECK(action == window.findChild<QAction*>(QLatin1String(name)));  // the panel's own action
    }
    save_window_popup_artifact("issue_396_path_menu", window, *canvas, menu);
    // Fill Path from the menu opens its dialog and paints the path.
    canvas->set_primary_color(QColor(30, 160, 40));
    QTimer::singleShot(0, [] {
      auto* dialog = find_top_level_dialog(QStringLiteral("fillPathDialog"));
      CHECK(dialog != nullptr);
      if (dialog == nullptr) {
        return;
      }
      dialog->findChild<QComboBox*>(QStringLiteral("fillPathContentsCombo"))->setCurrentIndex(0);
      dialog->findChild<QSpinBox*>(QStringLiteral("fillPathOpacitySpin"))->setValue(100);
      dialog->accept();
    });
    require_menu_action(menu, "pathFillAction")->trigger();
  });
  open_path_menu_at(*canvas, QPoint(200, 100));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(200, 160)), QColor(30, 160, 40), 8));

  // No row targeted: the tools still edit the work path, and the menu targets
  // its row so the commands apply, without dropping the point selection.
  auto* paths_list = window.findChild<QListWidget*>(QStringLiteral("pathsList"));
  CHECK(paths_list != nullptr);
  send_mouse(*paths_list->viewport(), QEvent::MouseButtonPress,
             QPoint(paths_list->viewport()->width() / 2, paths_list->viewport()->height() - 4),
             Qt::LeftButton, Qt::LeftButton);
  QApplication::processEvents();
  CHECK(!canvas->active_document_path().has_value());
  CHECK(!window.findChild<QAction*>(QStringLiteral("pathFillAction"))->isEnabled());
  pen_click(*canvas, QPoint(100, 220));
  CHECK(canvas->path_edit_has_selection());
  with_path_context_menu([](QMenu& menu) {
    for (const auto* name : kCommands) {
      CHECK(require_menu_action(menu, name)->isEnabled());
    }
    CHECK(require_menu_action(menu, "pathMenuDeleteSelectedAction")->isEnabled());
  });
  open_path_menu_at(*canvas, QPoint(500, 400));
  CHECK(canvas->active_document_path().has_value());
  CHECK(canvas->path_edit_has_selection());
}

// GitHub issue 396 follow-up: a command that would only refuse is greyed out,
// with its refusal as the disabled reason the menu's tooltip shows. On a shape
// layer, Fill Path and Stroke Path have no pixel layer to paint into, and its
// transient path row cannot be duplicated or deleted.
void ui_path_menu_greys_out_commands_that_would_refuse() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  make_rect_shape_layer(window, *canvas);
  require_action(window, "toolDirectSelectAction")->trigger();
  QApplication::processEvents();
  const auto reason = [](QAction* action) { return action->property("patchy.disabledReason").toString(); };
  with_path_context_menu([&](QMenu& menu) {
    for (const auto* name : {"pathFillAction", "pathStrokeAction"}) {
      auto* action = require_menu_action(menu, name);
      CHECK(!action->isEnabled());
      CHECK(reason(action) == QStringLiteral("Select an editable pixel layer first"));
    }
    CHECK(require_menu_action(menu, "pathMakeSelectionAction")->isEnabled());
    CHECK(require_menu_action(menu, "pathSimplifyAction")->isEnabled());
    CHECK(!require_menu_action(menu, "pathDuplicateAction")->isEnabled());
    CHECK(reason(require_menu_action(menu, "pathDeleteAction")) ==
          QStringLiteral("Select a saved path or the work path to delete"));
    save_window_popup_artifact("issue_396_shape_layer_menu", window, *canvas, menu);
  });
  open_path_menu_at(*canvas, QPoint(200, 100));

  // Locked pixels on the Background: the reason follows the lock, refreshed
  // when the menu opens.
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  std::optional<patchy::LayerId> background_id;
  for (const auto& layer : std::as_const(document).layers()) {
    if (layer.name() == "Background") {
      background_id = layer.id();
    }
  }
  CHECK(background_id.has_value());
  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  CHECK(mode_combo != nullptr);
  mode_combo->setCurrentIndex(1);  // Path: a work path to fill
  shape_drag(*canvas, QPoint(350, 250), QPoint(450, 330));
  document.set_active_layer(*background_id);
  patchy::ui::MainWindowTestAccess::refresh_paths_panel(window);
  require_action(window, "toolDirectSelectAction")->trigger();
  QApplication::processEvents();
  auto* fill = window.findChild<QAction*>(QStringLiteral("pathFillAction"));
  CHECK(fill != nullptr && fill->isEnabled() && reason(fill).isEmpty());
  document.find_layer(*background_id)->set_lock_flags(patchy::kLayerLockImagePixels);
  with_path_context_menu([&](QMenu& menu) {
    auto* action = require_menu_action(menu, "pathFillAction");
    CHECK(!action->isEnabled());
    CHECK(reason(action) == QStringLiteral("Layer pixels are locked."));
  });
  open_path_menu_at(*canvas, QPoint(400, 250));
}

// Pen Ctrl latch (Seth on GitHub issue 248): a point selected with Ctrl+click keeps
// its handle knobs on screen after the mouse button comes up, Ctrl still held, and
// after Ctrl comes up too, as long as it stays selected, so the user can see where
// to Ctrl-drag; a Ctrl-drag on a knob then moves that handle. Without Ctrl the
// knobs are only shown, never grabbed.
void ui_pen_ctrl_selected_point_keeps_its_knobs() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = make_rect_shape_layer(window, *canvas);  // (100,100)-(300,220)
  {
    auto curved = *canvas->path_edit_target_path();
    auto& anchor = curved.subpaths[0].anchors[0];
    anchor.smooth = true;
    anchor.out_x = 160.0;
    anchor.out_y = 30.0;
    anchor.in_x = 40.0;
    anchor.in_y = 170.0;
    set_target_path_through_edits(*canvas, std::move(curved));
  }
  canvas->set_view_zoom_centered(1.0);
  require_action(window, "toolPenAction")->trigger();
  QApplication::processEvents();
  CHECK(!accent_overlay_near(*canvas, QPoint(160, 30)));  // nothing selected: no knobs

  const auto corner = canvas->widget_position_for_document_point(QPoint(100, 100));
  send_mouse(*canvas, QEvent::MouseButtonPress, corner, Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
  QApplication::processEvents();
  CHECK(canvas->path_edit_selected_anchor_count() == 1);
  CHECK(accent_overlay_near(*canvas, QPoint(160, 30)));  // held: the knobs show
  send_mouse(*canvas, QEvent::MouseButtonRelease, corner, Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
  QApplication::processEvents();
  save_widget_artifact("ui_pen_ctrl_point_after_button_release", *canvas);
  CHECK(canvas->path_edit_selected_anchor_count() == 1);
  CHECK(accent_overlay_near(*canvas, QPoint(160, 30)));  // button up, Ctrl held: still shown
  CHECK(canvas->cursor().pixmap().toImage() == patchy::ui::direct_select_cursor().pixmap().toImage());
  send_key_release(*canvas, Qt::Key_Control, Qt::ControlModifier);
  QApplication::processEvents();
  CHECK(accent_overlay_near(*canvas, QPoint(160, 30)));  // Ctrl up: still shown while selected

  // A plain click on the knob is the Pen's own (it never grabs the handle).
  const auto& before = anchor_at(document, layer_id, 0);
  const QPointF out_before(before.out_x, before.out_y);
  // Ctrl-drag on the knob moves the handle.
  const auto knob = canvas->widget_position_for_document_point(QPoint(160, 30));
  send_mouse(*canvas, QEvent::MouseButtonPress, knob, Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
  send_mouse(*canvas, QEvent::MouseMove, knob + QPoint(20, 0), Qt::NoButton, Qt::LeftButton, Qt::ControlModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, knob + QPoint(20, 0), Qt::LeftButton, Qt::NoButton,
             Qt::ControlModifier);
  QApplication::processEvents();
  CHECK(std::abs(anchor_at(document, layer_id, 0).out_x - (out_before.x() + 20.0)) < 1.5);
  CHECK(std::abs(anchor_at(document, layer_id, 0).anchor_x - 100.0) < 0.5);
  CHECK(accent_overlay_near(*canvas, QPoint(180, 30)));  // the knob shows where it went
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
  // Both layers lost every subpath, so both go (GitHub issue 315).
  CHECK(std::as_const(document).find_layer(first_id) == nullptr);
  CHECK(std::as_const(document).find_layer(second_id) == nullptr);
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
      {"ui_path_context_menu_offers_paths_panel_commands", ui_path_context_menu_offers_paths_panel_commands},
      {"ui_path_menu_greys_out_commands_that_would_refuse", ui_path_menu_greys_out_commands_that_would_refuse},
      {"ui_anchor_tools_share_pen_flyout_and_edit_points",
       ui_anchor_tools_share_pen_flyout_and_edit_points},
      {"ui_shape_layer_activation_drops_stale_path_target",
       ui_shape_layer_activation_drops_stale_path_target},
      {"ui_layer_row_click_shows_anchors_for_shape_layer", ui_layer_row_click_shows_anchors_for_shape_layer},
      {"ui_path_overlay_outline_is_hairline", ui_path_overlay_outline_is_hairline},
      {"ui_path_handle_knobs_read_and_grab", ui_path_handle_knobs_read_and_grab},
      {"ui_path_tools_keep_handle_knobs_to_direct_select", ui_path_tools_keep_handle_knobs_to_direct_select},
      {"ui_path_select_blank_click_deselects_shape", ui_path_select_blank_click_deselects_shape},
      {"ui_path_select_click_picks_shape_under_pointer", ui_path_select_click_picks_shape_under_pointer},
      {"ui_path_select_marquee_touching_shape_selects_it", ui_path_select_marquee_touching_shape_selects_it},
      {"ui_path_select_drag_inside_fill_moves_shape", ui_path_select_drag_inside_fill_moves_shape},
      {"ui_path_select_drag_coalesces_queued_moves", ui_path_select_drag_coalesces_queued_moves},
      {"ui_direct_select_drags_points_of_unselected_shapes",
       ui_direct_select_drags_points_of_unselected_shapes},
      {"ui_path_select_anchor_drag_moves_the_whole_shape", ui_path_select_anchor_drag_moves_the_whole_shape},
      {"ui_direct_select_after_path_select_drags_one_point", ui_direct_select_after_path_select_drags_one_point},
      {"ui_path_select_tools_drag_open_path_anchor", ui_path_select_tools_drag_open_path_anchor},
      {"ui_path_transform_shows_free_transform_cursors", ui_path_transform_shows_free_transform_cursors},
      {"ui_path_transform_ctrl_drag_distorts_corner_and_side",
       ui_path_transform_ctrl_drag_distorts_corner_and_side},
      {"ui_path_transform_ctrl_alt_drag_is_symmetric", ui_path_transform_ctrl_alt_drag_is_symmetric},
      {"ui_path_transform_ctrl_shift_drag_skews_side", ui_path_transform_ctrl_shift_drag_skews_side},
      {"ui_path_transform_ctrl_alt_shift_drag_is_perspective", ui_path_transform_ctrl_alt_shift_drag_is_perspective},
      {"ui_path_transform_distort_never_folds_the_quad", ui_path_transform_distort_never_folds_the_quad},
      {"ui_path_transform_modifiers_show_gesture_cursors", ui_path_transform_modifiers_show_gesture_cursors},
      {"ui_path_transform_fill_follows_the_box_live", ui_path_transform_fill_follows_the_box_live},
      {"ui_path_select_switch_expands_points_and_pen_keeps_selected_knobs",
       ui_path_select_switch_expands_points_and_pen_keeps_selected_knobs},
      {"ui_point_selection_drops_when_the_active_layer_changes", ui_point_selection_drops_when_the_active_layer_changes},
      {"ui_path_select_shift_click_adds_and_removes_shapes", ui_path_select_shift_click_adds_and_removes_shapes},
      {"ui_path_select_transform_moves_only_selected_shapes", ui_path_select_transform_moves_only_selected_shapes},
      {"ui_direct_select_alt_click_selects_the_whole_subpath", ui_direct_select_alt_click_selects_the_whole_subpath},
      {"ui_direct_select_handle_drag_keeps_the_pair_colinear_and_alt_breaks_it",
       ui_direct_select_handle_drag_keeps_the_pair_colinear_and_alt_breaks_it},
      {"ui_ctrl_swaps_the_arrow_tools", ui_ctrl_swaps_the_arrow_tools},
      {"ui_path_select_alt_drag_duplicates_the_shape", ui_path_select_alt_drag_duplicates_the_shape},
      {"ui_path_select_alt_drag_duplicates_a_merged_shape", ui_path_select_alt_drag_duplicates_a_merged_shape},
      {"ui_pen_shift_keeps_45_degrees_and_ctrl_click_ends_the_path",
       ui_pen_shift_keeps_45_degrees_and_ctrl_click_ends_the_path},
      {"ui_pen_click_on_an_open_end_continues_the_path", ui_pen_click_on_an_open_end_continues_the_path},
      {"ui_path_select_tools_show_their_own_arrow_cursors", ui_path_select_tools_show_their_own_arrow_cursors},
      {"ui_direct_select_drags_redraw_the_fill_live", ui_direct_select_drags_redraw_the_fill_live},
      {"ui_path_select_context_menu_names_shapes", ui_path_select_context_menu_names_shapes},
      {"ui_path_select_delete_last_shape_removes_the_layer", ui_path_select_delete_last_shape_removes_the_layer},
      {"ui_path_select_delete_one_of_two_shapes_keeps_the_layer",
       ui_path_select_delete_one_of_two_shapes_keeps_the_layer},
      {"ui_path_menu_delete_last_shape_removes_the_layer", ui_path_menu_delete_last_shape_removes_the_layer},
      {"ui_pen_ctrl_selected_point_keeps_its_knobs", ui_pen_ctrl_selected_point_keeps_its_knobs},
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
