#include <QClipboard>
#include <QStatusBar>
#include "ui/canvas_widget.hpp"
#include "ai/segmentation_engine.hpp"
#include "ui/main_window_shared.hpp"
#include "core/adjustment_layer.hpp"
#include "core/layer_metadata.hpp"
#include "core/pixel_depth.hpp"
#include "ui/color_panel.hpp"
#include "ui/new_document_dialog.hpp"
#include "ui/gradient_stops_editor.hpp"
#include "formats/acv_curves_io.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/ico_document_io.hpp"
#include "formats/tga_document_io.hpp"
#include "ui/main_window.hpp"
#include "ui/app_settings.hpp"
#include "psd/psd_document_io.hpp"
#include "test_harness.hpp"
#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFocusEvent>
#include <QFormLayout>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QListWidget>
#include <QMetaObject>
#include <QMouseEvent>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QStringList>
#include <QSlider>
#include <QTabBar>
#include <QTabWidget>
#include <QTest>
#include <QTextEdit>
#include <QTimer>
#include <QPainter>
#include <QScreen>
#include <QScrollBar>
#include <QSet>
#include <QToolButton>
#include <QWidget>
#include <algorithm>
#include <atomic>
#include <array>
#include <cstdint>
#include <cstdio>
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

// The canvas context menu (one builder for the Move tool's layer section and
// the selection section; docs/ui-conventions.md).
QMenu* move_layer_menu(patchy::ui::CanvasWidget& canvas) {
  for (auto* menu : canvas.findChildren<QMenu*>(QStringLiteral("canvasContextMenu"))) {
    if (menu->isVisible()) {
      return menu;
    }
  }
  return nullptr;
}

QMenu* right_click_move_canvas(patchy::ui::CanvasWidget& canvas, QPoint document_point) {
  const auto point = canvas.widget_position_for_document_point(document_point);
  send_mouse(canvas, QEvent::MouseButtonPress, point, Qt::RightButton, Qt::RightButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, point, Qt::RightButton, Qt::NoButton);
  QApplication::processEvents();
  return move_layer_menu(canvas);
}

void click_move_menu_action(QMenu& menu, QAction* action) {
  CHECK(action != nullptr);
  const auto point = menu.actionGeometry(action).center();
  send_mouse(menu, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(menu, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
}

void ui_move_layer_menu_selects_overlapping_layers_and_reveals_rows() {
  patchy::Document document(160, 120, patchy::PixelFormat::rgba8());
  patchy::Layer bottom(document.allocate_layer_id(), "Bottom",
      solid_pixels(80, 80, patchy::PixelFormat::rgba8(), QColor(Qt::red)));
  const auto bottom_id = bottom.id();
  patchy::set_layer_locks_position(bottom, true);
  document.add_layer(std::move(bottom));
  patchy::Layer group(document.allocate_layer_id(), "Folder", patchy::LayerKind::Group);
  group.metadata()[patchy::kLayerMetadataGroupExpanded] = "false";
  patchy::Layer child(document.allocate_layer_id(), "Ink & Paper",
      solid_pixels(60, 60, patchy::PixelFormat::rgba8(), QColor(Qt::blue)));
  const auto child_id = child.id();
  group.add_child(std::move(child));
  document.add_layer(std::move(group));
  patchy::Layer top(document.allocate_layer_id(), "Ink & Paper",
      solid_pixels(40, 40, patchy::PixelFormat::rgba8(), QColor(Qt::green)));
  const auto top_id = top.id();
  document.add_layer(std::move(top));
  document.set_active_layer(bottom_id);

  patchy::ui::MainWindow window;
  show_window_empty(window);
  window.add_document_session(std::move(document), QStringLiteral("Move Layer Menu"));
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  auto* list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* filter = window.findChild<QLineEdit*>(QStringLiteral("layerNameFilterEdit"));
  CHECK(list != nullptr && filter != nullptr);
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_auto_select_layer(false);
  canvas->set_show_transform_controls(true);
  canvas->set_rulers_visible(false);
  filter->setText(QStringLiteral("Bottom"));
  QApplication::processEvents();
  CHECK(list->count() == 1);
  const auto undo_depth = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  const auto modified = patchy::ui::MainWindowTestAccess::active_session_is_modified(window);

  auto* menu = right_click_move_canvas(*canvas, QPoint(20, 20));
  CHECK(menu != nullptr);
  const auto actions = menu->actions();
  CHECK(actions.size() == 5); // Three layers, separator, Select All.
  CHECK(actions[0]->data().toULongLong() == top_id);
  CHECK(actions[0]->text() == QStringLiteral("Ink && Paper"));
  CHECK(actions[1]->data().toULongLong() == child_id);
  CHECK(actions[1]->text() == QStringLiteral("Folder / Ink && Paper"));
  CHECK(actions[2]->data().toULongLong() == bottom_id);
  CHECK(actions[2]->isChecked());
  CHECK(actions[4]->text() == QStringLiteral("Select All Layers Here"));
  save_widget_artifact("ui_move_layer_menu", *menu);
  click_move_menu_action(*menu, actions[1]);
  CHECK(filter->text().isEmpty());
  CHECK(list->selectedItems().size() == 1);
  CHECK(list->currentItem()->data(Qt::UserRole).toULongLong() == child_id);
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("1 layer selected"));

  menu = right_click_move_canvas(*canvas, QPoint(20, 20));
  CHECK(menu != nullptr);
  CHECK(menu->actions()[1]->isChecked());
  click_move_menu_action(*menu, menu->findChild<QAction*>(QStringLiteral("moveMenuSelectAllLayersAction")));
  CHECK(list->selectedItems().size() == 3);
  CHECK(list->currentItem()->data(Qt::UserRole).toULongLong() == top_id);
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("3 layers selected"));

  menu = right_click_move_canvas(*canvas, QPoint(70, 70));
  CHECK(menu != nullptr);
  CHECK(menu->actions().size() == 1);
  CHECK(menu->actions().front()->data().toULongLong() == bottom_id);
  click_move_menu_action(*menu, menu->actions().front());
  CHECK(list->selectedItems().size() == 1);
  CHECK(list->currentItem()->data(Qt::UserRole).toULongLong() == bottom_id);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_depth);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_is_modified(window) == modified);
}

void ui_move_layer_menu_respects_pixels_masks_and_visibility() {
  patchy::Document document(120, 100, patchy::PixelFormat::rgba8());
  const auto make_layer = [&](const char* name) {
    patchy::Layer layer(document.allocate_layer_id(), name,
        solid_pixels(40, 40, patchy::PixelFormat::rgba8(), QColor(Qt::blue)));
    layer.set_bounds({20, 20, 40, 40});
    return layer;
  };
  auto bottom = make_layer("Bottom");
  const auto bottom_id = bottom.id();
  document.add_layer(std::move(bottom));
  auto hidden = make_layer("Hidden");
  hidden.set_visible(false);
  document.add_layer(std::move(hidden));
  auto zero = make_layer("Zero opacity");
  zero.set_opacity(0.0F);
  document.add_layer(std::move(zero));
  auto hole = make_layer("Transparent hole");
  hole.pixels().pixel(5, 5)[3] = 0;
  document.add_layer(std::move(hole));
  auto masked = make_layer("Mask hole");
  patchy::PixelBuffer mask(40, 40, patchy::PixelFormat::gray8());
  masked.set_mask(patchy::LayerMask{{20, 20, 40, 40}, std::move(mask), 0, false});
  document.add_layer(std::move(masked));
  for (int i = 0; i < 3; ++i) {
    patchy::Layer group(document.allocate_layer_id(), "Excluded folder", patchy::LayerKind::Group);
    group.add_child(make_layer("Excluded child"));
    if (i == 0) {
      group.set_visible(false);
    } else if (i == 1) {
      group.set_opacity(0.0F);
    } else {
      patchy::PixelBuffer group_mask(40, 40, patchy::PixelFormat::gray8());
      group.set_mask(patchy::LayerMask{{20, 20, 40, 40}, std::move(group_mask), 0, false});
    }
    document.add_layer(std::move(group));
  }
  patchy::Layer locked_group(document.allocate_layer_id(), "Locked folder", patchy::LayerKind::Group);
  patchy::set_layer_locks_position(locked_group, true);
  auto locked_child = make_layer("Locked child");
  const auto locked_child_id = locked_child.id();
  locked_group.add_child(std::move(locked_child));
  document.add_layer(std::move(locked_group));
  document.set_active_layer(bottom_id);
  std::vector<std::uint64_t> revisions;
  for (const auto& layer : std::as_const(document).layers()) {
    revisions.push_back(layer.content_revision());
  }
  patchy::ui::CanvasWidget canvas;
  canvas.resize(560, 420);
  canvas.set_document(&document);
  canvas.set_zoom(2.0);
  canvas.set_tool(patchy::ui::CanvasTool::Move);
  canvas.set_rulers_visible(false);
  canvas.set_show_transform_controls(false);
  canvas.show();
  QApplication::processEvents();
  auto* menu = right_click_move_canvas(canvas, QPoint(25, 25));
  CHECK(menu != nullptr);
  CHECK(menu->actions().size() == 4);
  CHECK(menu->actions()[0]->data().toULongLong() == locked_child_id);
  CHECK(menu->actions()[1]->data().toULongLong() == bottom_id);
  send_key(*menu, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(document.active_layer_id() == bottom_id);
  CHECK(move_layer_menu(canvas) == nullptr);
  CHECK(right_click_move_canvas(canvas, QPoint(90, 80)) == nullptr);
  // Outside the document the menu offers only the pasteboard color (issue 47),
  // never a layer entry.
  auto* pasteboard_menu = right_click_move_canvas(canvas, QPoint(-10, 40));
  CHECK(pasteboard_menu != nullptr);
  CHECK(std::none_of(pasteboard_menu->actions().cbegin(), pasteboard_menu->actions().cend(),
                     [](const QAction* action) { return action->data().toULongLong() != 0; }));
  CHECK(pasteboard_menu->findChild<QAction*>(QStringLiteral("canvasBackdropDefaultAction")) != nullptr);
  send_key(*pasteboard_menu, Qt::Key_Escape);
  QApplication::processEvents();
  std::size_t index = 0;
  for (const auto& layer : std::as_const(document).layers()) {
    CHECK(layer.content_revision() == revisions[index++]);
  }
}

// A right drag under a painting tool moves nothing: no pan, no paint, no menu.
void ui_right_drag_does_not_pan_canvas() {
  patchy::Document document(400, 300, patchy::PixelFormat::rgba8());
  auto& layer = document.add_pixel_layer(
      "Paint", solid_pixels(400, 300, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0)));
  const auto layer_id = layer.id();
  patchy::ui::CanvasWidget canvas;
  canvas.resize(300, 220);
  canvas.set_document(&document);
  canvas.set_zoom(1.0);
  canvas.set_tool(patchy::ui::CanvasTool::Brush);
  canvas.set_primary_color(Qt::black);
  canvas.set_brush_size(20);
  canvas.show();
  QApplication::processEvents();
  const auto origin = canvas.widget_position_for_document_point(QPoint());
  const auto start = canvas.widget_position_for_document_point(QPoint(200, 150));
  send_mouse(canvas, QEvent::MouseButtonPress, start, Qt::RightButton, Qt::RightButton);
  send_mouse(canvas, QEvent::MouseMove, start + QPoint(40, 30), Qt::NoButton, Qt::RightButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, start + QPoint(40, 30), Qt::RightButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(canvas.widget_position_for_document_point(QPoint()) == origin);
  CHECK(move_layer_menu(canvas) == nullptr);
  int painted = 0;
  const auto& pixels = document.find_layer(layer_id)->pixels();
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      painted += pixels.pixel(x, y)[3] > 0U ? 1 : 0;
    }
  }
  CHECK(painted == 0);
  // The middle button still pans.
  send_mouse(canvas, QEvent::MouseButtonPress, start, Qt::MiddleButton, Qt::MiddleButton);
  send_mouse(canvas, QEvent::MouseMove, start + QPoint(40, 30), Qt::NoButton, Qt::MiddleButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, start + QPoint(40, 30), Qt::MiddleButton, Qt::NoButton);
  CHECK(canvas.widget_position_for_document_point(QPoint()) == origin + QPoint(40, 30));
}

// A right-click on the selection offers the host's selection commands, led by
// Remove Object; off the selection (and without a Move-tool layer hit) there
// is nothing to offer; with the Move tool the layer section comes first.
void ui_selection_context_menu_offers_remove_object() {
  auto&& [window, canvas] = ShownWindowFixture{};
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(20, 20)),
       canvas->widget_position_for_document_point(QPoint(80, 80)));
  QApplication::processEvents();
  CHECK(canvas->has_selection());

  const auto menu_has_remove = [](QMenu& menu) {
    for (auto* action : menu.actions()) {
      if (action->objectName() == QStringLiteral("editRemoveObjectAction")) {
        return true;
      }
    }
    return false;
  };

  auto* menu = right_click_move_canvas(*canvas, QPoint(50, 50));
  CHECK(menu != nullptr);
  if (menu != nullptr) {
    CHECK(menu_has_remove(*menu));
    CHECK(!menu->actions().isEmpty() &&
          menu->actions().front()->objectName() == QStringLiteral("editRemoveObjectAction"));
    menu->close();
    QApplication::processEvents();
  }

  // Off the selection a selection tool offers the subject commands (where the build has the
  // model, docs/ai-selection.md) and then the selection's own commands less the ones that act on
  // its content: Remove Object stays inside the marching ants, Deselect works from anywhere
  // (GitHub issue 105).
  patchy::ui::configure_automatic_selection_models();
  auto* outside = right_click_move_canvas(*canvas, QPoint(300, 300));
  CHECK(outside != nullptr);
  if (outside != nullptr) {
    CHECK(!menu_has_remove(*outside));
    const auto names = [&] {
      QStringList list;
      for (auto* action : outside->actions()) {
        list.push_back(action->objectName());
      }
      return list;
    }();
    CHECK(names.contains(QStringLiteral("editDeselectAction")));
    if (patchy::ai::subject_selection_available()) {
      CHECK(outside->actions().front()->objectName() == QStringLiteral("selectSubjectAction"));
    } else {
      CHECK(!names.contains(QStringLiteral("selectSubjectAction")));
    }
    outside->close();
    QApplication::processEvents();
  }

  canvas->set_tool(patchy::ui::CanvasTool::Move);
  menu = right_click_move_canvas(*canvas, QPoint(50, 50));
  CHECK(menu != nullptr);
  if (menu != nullptr) {
    CHECK(menu_has_remove(*menu));
    const auto* front = menu->actions().front();
    CHECK(front->isCheckable() || front->objectName() == QStringLiteral("editRemoveObjectAction"));
    menu->close();
    QApplication::processEvents();
  }

  // Picking Remove Object from the menu opens its dialog while the first fill
  // runs on a worker; accepting it once the result is in commits one history
  // entry. The driver re-arms until then and gives up after five seconds.
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  const auto depth = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  bool accepted_dialog = false;
  int driver_tries = 0;
  auto poll = std::make_shared<std::function<void()>>();
  *poll = [&, poll] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* dialog = qobject_cast<QDialog*>(widget);
      if (dialog == nullptr || dialog->objectName() != QStringLiteral("patchyRemoveObjectDialog") ||
          !dialog->isVisible()) {
        continue;
      }
      // The fill runs on a worker; OK enables once its result is in.
      auto* buttons = dialog->findChild<QDialogButtonBox*>();
      auto* ok = buttons != nullptr ? buttons->button(QDialogButtonBox::Ok) : nullptr;
      if (ok == nullptr || !ok->isEnabled()) {
        break;
      }
      accepted_dialog = true;
      dialog->accept();
      return;
    }
    if (++driver_tries < 500) {
      QTimer::singleShot(10, *poll);
    }
  };
  QTimer::singleShot(10, *poll);
  menu = right_click_move_canvas(*canvas, QPoint(50, 50));
  CHECK(menu != nullptr);
  if (menu != nullptr) {
    for (auto* action : menu->actions()) {
      if (action->objectName() == QStringLiteral("editRemoveObjectAction")) {
        action->trigger();
      }
    }
    menu->close();
    QApplication::processEvents();
  }
  CHECK(accepted_dialog);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == depth + 1);
}

// Regression (September 2026 crash): Stroke Selection picked from the canvas context
// menu opens a modal dialog from inside the popup menu's own mouse release. The menu
// must survive that nested loop, and ordinary mouse input afterwards must not touch a
// dead widget.
void ui_stroke_selection_from_context_menu_survives_following_mouse_input() {
  auto&& [window, canvas] = ShownWindowFixture{};
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  canvas->set_primary_color(Qt::black);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(20, 20)),
       canvas->widget_position_for_document_point(QPoint(80, 80)));
  QApplication::processEvents();
  CHECK(canvas->has_selection());
  const auto selection = canvas->selected_document_region().boundingRect();

  auto* menu = right_click_move_canvas(*canvas, QPoint(50, 50));
  CHECK(menu != nullptr);
  if (menu == nullptr) {
    return;
  }
  QAction* stroke = nullptr;
  for (auto* action : menu->actions()) {
    if (action->objectName() == QStringLiteral("editStrokeSelectionAction")) {
      stroke = action;
    }
  }
  CHECK(stroke != nullptr);
  const QPointer<QMenu> menu_guard(menu);
  accept_stroke_selection_dialog(2, QStringLiteral("center"));
  // Through the window handle, not sendEvent on the QMenu: the crash lived in the
  // window-level mouse dispatcher that keeps running after the menu's release handler.
  CHECK(menu->windowHandle() != nullptr);
  QTest::mouseClick(menu->windowHandle(), Qt::LeftButton, Qt::NoModifier, menu->actionGeometry(stroke).center());
  QApplication::processEvents();
  process_events_for(50);

  // The band landed on the selection edge, and the menu was released only after the pick
  // finished dispatching (deferred delete processed once control returned to the loop).
  {
    const auto& doc = patchy::ui::MainWindowTestAccess::document(window);
    const auto* layer = doc.find_layer(*doc.active_layer_id());
    CHECK(layer != nullptr);
    CHECK(layer->pixels().pixel(selection.left(), selection.center().y())[3] == 255);
  }
  CHECK(menu_guard.isNull());

  // What the user does next: move over the canvas, click, drag a little.
  const auto inside = canvas->widget_position_for_document_point(QPoint(50, 50));
  const auto outside = canvas->widget_position_for_document_point(QPoint(120, 100));
  send_mouse(*canvas, QEvent::MouseMove, inside, Qt::NoButton, Qt::NoButton);
  send_mouse(*canvas, QEvent::MouseMove, outside, Qt::NoButton, Qt::NoButton);
  QApplication::processEvents();
  send_mouse(*canvas, QEvent::MouseButtonPress, outside, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, outside + QPoint(10, 10), Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, outside + QPoint(10, 10), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  process_events_for(50);
  auto* second = right_click_move_canvas(*canvas, QPoint(125, 105));
  if (second != nullptr) {
    second->close();
    QApplication::processEvents();
  }
  CHECK(window.isVisible());
}

// A Move-tool right-click inside the active raster layer's Move outline ends
// the menu with Free Transform, as the shape section does for shape layers;
// off the layer, with another tool, or on a position-locked layer it does not.
void ui_move_context_menu_offers_free_transform_on_raster_layer() {
  patchy::Document document(160, 120, patchy::PixelFormat::rgba8());
  patchy::Layer photo(document.allocate_layer_id(), "Photo",
      solid_pixels(40, 40, patchy::PixelFormat::rgba8(), QColor(Qt::red)));
  photo.set_bounds({20, 20, 40, 40});
  const auto photo_id = photo.id();
  document.add_layer(std::move(photo));
  document.set_active_layer(photo_id);

  patchy::ui::MainWindow window;
  show_window_empty(window);
  window.add_document_session(std::move(document), QStringLiteral("Raster Free Transform"));
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_rulers_visible(false);
  QApplication::processEvents();

  const auto free_transform_in = [](QMenu& menu) -> QAction* {
    for (auto* action : menu.actions()) {
      if (action->objectName() == QStringLiteral("editFreeTransformAction")) {
        return action;
      }
    }
    return nullptr;
  };

  auto* menu = right_click_move_canvas(*canvas, QPoint(30, 30));
  CHECK(menu != nullptr);
  if (menu != nullptr) {
    // The layer entry first, then the separator and Free Transform.
    CHECK(menu->actions().size() == 3);
    CHECK(menu->actions().front()->data().toULongLong() == photo_id);
    auto* transform = free_transform_in(*menu);
    CHECK(transform != nullptr && transform == menu->actions().back());
    save_widget_artifact("ui_move_context_menu_free_transform", *menu);
    if (transform != nullptr) {
      transform->trigger();
    }
    menu->close();
    QApplication::processEvents();
  }
  CHECK(canvas->free_transform_active());
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());

  // Off the layer there is nothing under the pointer and no menu.
  CHECK(right_click_move_canvas(*canvas, QPoint(120, 100)) == nullptr);

  // Other tools keep a right-click on a raster layer menu-free.
  canvas->set_tool(patchy::ui::CanvasTool::Brush);
  CHECK(right_click_move_canvas(*canvas, QPoint(30, 30)) == nullptr);

  // A position-locked layer would refuse Free Transform, so it is not offered.
  canvas->set_tool(patchy::ui::CanvasTool::Move);
  if (auto* layer = patchy::ui::MainWindowTestAccess::document(window).find_layer(photo_id)) {
    patchy::set_layer_locks_position(*layer, true);
  }
  menu = right_click_move_canvas(*canvas, QPoint(30, 30));
  CHECK(menu != nullptr);
  if (menu != nullptr) {
    CHECK(free_transform_in(*menu) == nullptr);
    menu->close();
    QApplication::processEvents();
  }
}

void ui_move_layer_menu_preserves_pan_and_cancels_stale_clicks() {
  patchy::Document document(1000, 800, patchy::PixelFormat::rgba8());
  const auto bottom_id = document.add_pixel_layer("Bottom",
      solid_pixels(1000, 800, patchy::PixelFormat::rgba8(), QColor(Qt::red))).id();
  document.add_pixel_layer("Top",
      solid_pixels(1000, 800, patchy::PixelFormat::rgba8(), QColor(Qt::blue)));
  document.set_active_layer(bottom_id);
  patchy::ui::CanvasWidget canvas;
  canvas.resize(560, 420);
  canvas.set_document(&document);
  canvas.set_zoom(1.0);
  canvas.set_tool(patchy::ui::CanvasTool::Move);
  canvas.set_rulers_visible(false);
  canvas.set_show_transform_controls(false);
  canvas.show();
  QApplication::processEvents();
  const auto start = canvas.widget_position_for_document_point(QPoint(500, 400));
  const auto origin = canvas.widget_position_for_document_point(QPoint());
  send_mouse(canvas, QEvent::MouseButtonPress, start, Qt::RightButton, Qt::RightButton);
  CHECK(move_layer_menu(canvas) == nullptr);
  send_mouse(canvas, QEvent::MouseMove, start + QPoint(1, 0), Qt::NoButton, Qt::RightButton);
  CHECK(canvas.widget_position_for_document_point(QPoint()) == origin);
  send_mouse(canvas, QEvent::MouseButtonRelease, start + QPoint(1, 0), Qt::RightButton, Qt::NoButton);
  CHECK(move_layer_menu(canvas) != nullptr);
  move_layer_menu(canvas)->close();
  QApplication::processEvents();

  // A right drag neither pans (the right button is the context-menu button;
  // panning is the middle button) nor opens the menu, even when the pointer
  // returns to its starting point before release.
  send_mouse(canvas, QEvent::MouseButtonPress, start, Qt::RightButton, Qt::RightButton);
  send_mouse(canvas, QEvent::MouseMove, start + QPoint(40, 30), Qt::NoButton, Qt::RightButton);
  CHECK(canvas.widget_position_for_document_point(QPoint()) == origin);
  send_mouse(canvas, QEvent::MouseMove, start, Qt::NoButton, Qt::RightButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, start, Qt::RightButton, Qt::NoButton);
  CHECK(move_layer_menu(canvas) == nullptr);
  send_mouse(canvas, QEvent::MouseButtonPress, start, Qt::MiddleButton, Qt::MiddleButton);
  send_mouse(canvas, QEvent::MouseMove, start + QPoint(40, 30), Qt::NoButton, Qt::MiddleButton);
  CHECK(canvas.widget_position_for_document_point(QPoint()) == origin + QPoint(40, 30));
  send_mouse(canvas, QEvent::MouseButtonRelease, start + QPoint(40, 30), Qt::MiddleButton, Qt::NoButton);
  send_mouse(canvas, QEvent::MouseButtonPress, start + QPoint(40, 30), Qt::MiddleButton, Qt::MiddleButton);
  send_mouse(canvas, QEvent::MouseMove, start, Qt::NoButton, Qt::MiddleButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, start, Qt::MiddleButton, Qt::NoButton);
  CHECK(canvas.widget_position_for_document_point(QPoint()) == origin);

  canvas.set_tool(patchy::ui::CanvasTool::Pan);
  CHECK(right_click_move_canvas(canvas, QPoint(500, 400)) == nullptr);
  canvas.set_tool(patchy::ui::CanvasTool::Move);
  send_key_press(canvas, Qt::Key_Space);
  CHECK(right_click_move_canvas(canvas, QPoint(500, 400)) == nullptr);
  send_key_release(canvas, Qt::Key_Space);
  CHECK(canvas.begin_free_transform());
  CHECK(right_click_move_canvas(canvas, QPoint(500, 400)) == nullptr);
  CHECK(canvas.free_transform_active());
  canvas.cancel_free_transform();

  // Context clicks are cancelled by focus/tool/lock/document changes, and an
  // already-open menu must not select a stale target after those changes.
  for (int scenario = 0; scenario < 4; ++scenario) {
    canvas.set_document(&document);
    canvas.set_tool(patchy::ui::CanvasTool::Move);
    canvas.set_edit_locked(false);
    canvas.set_zoom(1.0);
    const auto point = canvas.widget_position_for_document_point(QPoint(500, 400));
    send_mouse(canvas, QEvent::MouseButtonPress, point, Qt::RightButton, Qt::RightButton);
    if (scenario == 0) {
      QFocusEvent focus(QEvent::FocusOut);
      QApplication::sendEvent(&canvas, &focus);
    } else if (scenario == 1) {
      canvas.set_tool(patchy::ui::CanvasTool::Brush);
    } else if (scenario == 2) {
      canvas.set_edit_locked(true);
    } else {
      canvas.set_document(nullptr);
    }
    send_mouse(canvas, QEvent::MouseButtonRelease, point, Qt::RightButton, Qt::NoButton);
    CHECK(move_layer_menu(canvas) == nullptr);
  }
  for (int scenario = 0; scenario < 3; ++scenario) {
    canvas.set_document(&document);
    canvas.set_tool(patchy::ui::CanvasTool::Move);
    canvas.set_edit_locked(false);
    auto* menu = right_click_move_canvas(canvas, QPoint(500, 400));
    CHECK(menu != nullptr);
    auto* stale_action = menu->actions().front();
    if (scenario == 0) {
      canvas.set_tool(patchy::ui::CanvasTool::Brush);
    } else if (scenario == 1) {
      canvas.set_edit_locked(true);
    } else {
      canvas.set_document(nullptr);
    }
    stale_action->trigger();
    CHECK(move_layer_menu(canvas) == nullptr);
    CHECK(document.active_layer_id() == bottom_id);
    QApplication::processEvents();
  }
}

void ui_layer_style_color_overlay_patch_double_click_opens_picker() {
  // The Color Overlay page's color patch is a passive QLabel next to the Choose
  // Color... button; double-clicking the patch must open the same picker.
  auto&& [window, canvas] = ShownWindowFixture{};
  auto* blending_options = require_action(window, "layerBlendingOptionsAction");

  bool saw_dialog = false;
  QTimer::singleShot(0, [&saw_dialog] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyLayerStyleDialog")) {
        continue;
      }
      saw_dialog = true;
      auto* dialog = qobject_cast<QDialog*>(widget);
      auto* patch = dialog->findChild<QLabel*>(QStringLiteral("layerStyleColorOverlayColorPreview"));
      auto* red = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleColorOverlayRedSpin"));
      auto* green = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleColorOverlayGreenSpin"));
      auto* blue = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleColorOverlayBlueSpin"));
      CHECK(patch != nullptr);
      CHECK(red != nullptr && green != nullptr && blue != nullptr);

      bool saw_picker = false;
      QTimer::singleShot(0, [&saw_picker] {
        for (auto* picker_widget : QApplication::topLevelWidgets()) {
          if (picker_widget->objectName() != QStringLiteral("patchyColorDialog") || !picker_widget->isVisible()) {
            continue;
          }
          auto* picker =
              picker_widget->findChild<patchy::ui::PatchyColorPicker*>(QStringLiteral("patchyAdvancedColorPicker"));
          CHECK(picker != nullptr);
          picker->setCurrentColor(QColor(12, 200, 99));
          saw_picker = true;
          qobject_cast<QDialog*>(picker_widget)->accept();
          return;
        }
        CHECK(false);
      });
      const auto patch_center = patch->rect().center();
      QMouseEvent double_click(QEvent::MouseButtonDblClick, QPointF(patch_center),
                               patch->mapToGlobal(patch_center), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
      QApplication::sendEvent(patch, &double_click);
      QApplication::processEvents();
      CHECK(saw_picker);
      CHECK(red->value() == 12);
      CHECK(green->value() == 200);
      CHECK(blue->value() == 99);
      dialog->reject();
      return;
    }
    CHECK(false);
  });
  blending_options->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);
}

// A right-clicked group gets group wording for the commands that act on it (Duplicate,
// Duplicate to Document, Rename, Delete), as Photoshop does; a plain layer keeps the
// layer wording (GitHub issue 203).
void ui_layer_context_menu_names_groups_as_groups() {
  auto&& [window, canvas] = ShownWindowFixture{};
  patchy::Document seed(120, 80, patchy::PixelFormat::rgba8());
  const auto paper =
      seed.add_pixel_layer("Paper", solid_pixels(120, 80, patchy::PixelFormat::rgba8(), QColor(255, 255, 255))).id();
  patchy::Layer group(seed.allocate_layer_id(), "Shapes", patchy::LayerKind::Group);
  const auto group_id = group.id();
  patchy::Layer child(seed.allocate_layer_id(), "Dot", solid_pixels(10, 10, patchy::PixelFormat::rgba8(), QColor(200, 30, 30)));
  group.add_child(std::move(child));
  seed.add_layer(std::move(group));
  window.add_document_session(std::move(seed), QStringLiteral("Groups"));
  QApplication::processEvents();
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  if (layer_list == nullptr) {
    return;
  }
  const auto menu_texts = [layer_list]() {
    QStringList texts;
    QTimer::singleShot(0, [&texts] {
      for (auto* widget : QApplication::topLevelWidgets()) {
        auto* menu = qobject_cast<QMenu*>(widget);
        if (menu == nullptr || menu->objectName() != QStringLiteral("layerContextMenu")) {
          continue;
        }
        for (auto* action : menu->actions()) {
          texts << action->text().remove('&');
        }
        menu->close();
        return;
      }
    });
    auto* item = layer_list->currentItem();
    CHECK(item != nullptr);
    if (item == nullptr) {
      return texts;
    }
    const auto point = layer_list->visualItemRect(item).center();
    QContextMenuEvent event(QContextMenuEvent::Mouse, point, layer_list->viewport()->mapToGlobal(point));
    QApplication::sendEvent(layer_list->viewport(), &event);
    QApplication::processEvents();
    return texts;
  };

  patchy::ui::MainWindowTestAccess::select_layers(window, {group_id}, group_id);
  QApplication::processEvents();
  const auto group_texts = menu_texts();
  for (const auto* expected : {"Duplicate Group", "Duplicate Group to Document...", "Rename Group...", "Delete Group"}) {
    CHECK(group_texts.contains(QLatin1String(expected)));
  }
  CHECK(!group_texts.contains(QStringLiteral("Delete Layer")));

  patchy::ui::MainWindowTestAccess::select_layers(window, {paper}, paper);
  QApplication::processEvents();
  const auto layer_texts = menu_texts();
  for (const auto* expected : {"Duplicate Layer", "Duplicate Layer to Document...", "Rename Layer...", "Delete Layer"}) {
    CHECK(layer_texts.contains(QLatin1String(expected)));
  }
  CHECK(!layer_texts.contains(QStringLiteral("Delete Group")));
}

void ui_layer_context_menu_exposes_blending_options_dialog() {
  auto&& [window, canvas] = ShownWindowFixture{};
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* blending_options = require_action(window, "layerBlendingOptionsAction");
  CHECK(layer_list != nullptr);
  CHECK(blending_options->text().remove('&') == QStringLiteral("Edit Layer Styles..."));

  bool saw_context_action = false;
  bool saw_rasterize_action = false;
  bool saw_rasterize_style_action = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* menu = qobject_cast<QMenu*>(widget);
      if (menu == nullptr || menu->objectName() != QStringLiteral("layerContextMenu")) {
        continue;
      }
      for (auto* action : menu->actions()) {
        auto text = action->text();
        text.remove('&');
        if (text == QStringLiteral("Edit Layer Styles...")) {
          saw_context_action = true;
        } else if (text == QStringLiteral("Rasterize")) {
          saw_rasterize_action = true;
        } else if (text == QStringLiteral("Rasterize (including layer style)")) {
          saw_rasterize_style_action = true;
        }
      }
      menu->close();
      return;
    }
  });
  const auto context_point = layer_list->visualItemRect(layer_list->item(0)).center();
  QContextMenuEvent context_event(QContextMenuEvent::Mouse, context_point,
                                  layer_list->viewport()->mapToGlobal(context_point));
  QApplication::sendEvent(layer_list->viewport(), &context_event);
  QApplication::processEvents();
  CHECK(saw_context_action);
  CHECK(saw_rasterize_action);
  CHECK(saw_rasterize_style_action);

  canvas->set_primary_color(QColor(230, 40, 40));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  const auto before = canvas_pixel(*canvas, QPoint(80, 80));
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_brush_size(24);
  canvas->set_primary_color(QColor(20, 220, 60));

  bool saw_live_style_preview = false;
  bool saw_non_modal_dialog = false;
  bool saw_layer_style_edit_lock = false;
  bool saw_shared_color_picker = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyLayerStyleDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      CHECK(dialog != nullptr);
      auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
      auto* gradient_check = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleGradientOverlayCategoryCheck"));
      auto* gradient_angle_slider = dialog->findChild<QSlider*>(QStringLiteral("layerStyleGradientAngleSlider"));
      auto* gradient_editor =
          dialog->findChild<patchy::ui::GradientStopsEditorWidget*>(QStringLiteral("layerStyleGradientStopsEditor"));
      auto* gradient_blend = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleGradientBlendModeCombo"));
      auto* gradient_reverse = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleGradientReverseCheck"));
      auto* gradient_style_combo = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleGradientStyleCombo"));
      auto* gradient_stop_location = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleGradientStopLocationSpin"));
      auto* gradient_stop_hex = dialog->findChild<QLineEdit*>(QStringLiteral("layerStyleGradientStopHexEdit"));
      auto* gradient_stop_swatch = dialog->findChild<QPushButton*>(QStringLiteral("layerStyleGradientStopSwatchButton"));
      auto* stroke_color = dialog->findChild<QPushButton*>(QStringLiteral("layerStyleStrokeColorPreview"));
      auto* stroke_red = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleStrokeRedSpin"));
      auto* stroke_green = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleStrokeGreenSpin"));
      auto* stroke_blue = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleStrokeBlueSpin"));
      auto* outer_glow_color = dialog->findChild<QPushButton*>(QStringLiteral("layerStyleOuterGlowColorPreview"));
      auto* outer_glow_red = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleOuterGlowRedSpin"));
      auto* outer_glow_green = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleOuterGlowGreenSpin"));
      auto* outer_glow_blue = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleOuterGlowBlueSpin"));
      auto* shadow_blend = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleDropShadowBlendModeCombo"));
      auto* shadow_color = dialog->findChild<QPushButton*>(QStringLiteral("layerStyleDropShadowColorPreview"));
      auto* shadow_red = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleDropShadowRedSpin"));
      auto* shadow_green = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleDropShadowGreenSpin"));
      auto* shadow_blue = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleDropShadowBlueSpin"));
      auto* preview = dialog->findChild<QCheckBox*>(QStringLiteral("layerStylePreviewCheck"));
      CHECK(categories != nullptr);
      CHECK(gradient_check != nullptr);
      CHECK(gradient_angle_slider != nullptr);
      CHECK(gradient_editor != nullptr);
      CHECK(gradient_blend != nullptr);
      CHECK(gradient_reverse != nullptr);
      CHECK(gradient_style_combo != nullptr);
      CHECK(gradient_stop_location != nullptr);
      CHECK(gradient_stop_hex != nullptr);
      CHECK(gradient_stop_swatch != nullptr);
      CHECK(stroke_color != nullptr);
      CHECK(stroke_red != nullptr);
      CHECK(stroke_green != nullptr);
      CHECK(stroke_blue != nullptr);
      CHECK(outer_glow_color != nullptr);
      CHECK(outer_glow_red != nullptr);
      CHECK(outer_glow_green != nullptr);
      CHECK(outer_glow_blue != nullptr);
      CHECK(shadow_blend != nullptr);
      CHECK(shadow_color != nullptr);
      CHECK(shadow_red != nullptr);
      CHECK(shadow_green != nullptr);
      CHECK(shadow_blue != nullptr);
      CHECK(preview != nullptr);
      CHECK(preview->isChecked());
      CHECK(shadow_blend->findText(QStringLiteral("Normal")) >= 0);
      CHECK(stroke_red->value() == 255);
      CHECK(stroke_green->value() == 0);
      CHECK(stroke_blue->value() == 0);
      saw_non_modal_dialog = !dialog->isModal() && dialog->windowModality() == Qt::NonModal &&
                             dialog->windowFlags().testFlag(Qt::FramelessWindowHint) &&
                             dialog->findChild<QWidget*>(QStringLiteral("dialogChromeTitleBar")) != nullptr &&
                             dialog->findChild<QToolButton*>(QStringLiteral("dialogChromeCloseButton")) != nullptr;
      CHECK(canvas->edit_locked());
      CHECK(!layer_list->isEnabled());
      CHECK(!require_action(window, "layerNewAction")->isEnabled());
      CHECK(!require_action(window, "layerFillForegroundAction")->isEnabled());
      CHECK(require_action(window, "viewZoomInAction")->isEnabled());
      const auto locked_pixel_before_edit = canvas_pixel(*canvas, QPoint(80, 80));
      drag(*canvas, canvas->widget_position_for_document_point(QPoint(80, 80)),
           canvas->widget_position_for_document_point(QPoint(98, 98)));
      const auto locked_pixel_after_edit = canvas_pixel(*canvas, QPoint(80, 80));
      saw_layer_style_edit_lock = color_close(locked_pixel_after_edit, locked_pixel_before_edit, 2);
      gradient_check->setChecked(true);
      gradient_angle_slider->setValue(0);
      QApplication::processEvents();
      QElapsedTimer live_preview_wait;
      live_preview_wait.start();
      while (live_preview_wait.elapsed() < 2000) {
        QApplication::processEvents(QEventLoop::AllEvents, 16);
        const auto layer_id = patchy::ui::MainWindowTestAccess::document(window).active_layer_id();
        const auto* live_layer = layer_id.has_value()
                                     ? patchy::ui::MainWindowTestAccess::document(window).find_layer(*layer_id)
                                     : nullptr;
        saw_live_style_preview =
            live_layer != nullptr && !live_layer->layer_style().gradient_fills.empty() &&
            live_layer->layer_style().gradient_fills.front().enabled;
        if (saw_live_style_preview) {
          break;
        }
      }
      const auto gradient_items = categories->findItems(QStringLiteral("Gradient Overlay"), Qt::MatchExactly);
      CHECK(!gradient_items.empty());
      categories->setCurrentItem(gradient_items.front());
      QApplication::processEvents();
      CHECK(gradient_editor->isVisible());
      CHECK(gradient_stop_hex->isVisible());
      CHECK(gradient_stop_location->value() == 0);
      CHECK(gradient_stop_hex->text() == QStringLiteral("#FFFFFF"));
      QTimer::singleShot(0, [&saw_shared_color_picker] {
        for (auto* widget : QApplication::topLevelWidgets()) {
          if (widget->objectName() != QStringLiteral("patchyColorDialog") || !widget->isVisible()) {
            continue;
          }
          auto* color_dialog = qobject_cast<QDialog*>(widget);
          CHECK(color_dialog != nullptr);
          auto* picker =
              color_dialog->findChild<patchy::ui::PatchyColorPicker*>(QStringLiteral("patchyAdvancedColorPicker"));
          CHECK(picker != nullptr);
          picker->setCurrentColor(QColor(64, 128, 192));
          saw_shared_color_picker = true;
          color_dialog->accept();
          return;
        }
        CHECK(false);
      });
      gradient_stop_swatch->click();
      CHECK(gradient_stop_hex->text() == QStringLiteral("#4080C0"));
      // Click the 100% color tag under the bar to select the second stop.
      const auto second_stop_point = QPoint(gradient_editor->width() - 11, gradient_editor->height() - 18);
      send_mouse(*gradient_editor, QEvent::MouseButtonPress, second_stop_point, Qt::LeftButton, Qt::LeftButton);
      send_mouse(*gradient_editor, QEvent::MouseButtonRelease, second_stop_point, Qt::LeftButton, Qt::NoButton);
      QApplication::processEvents();
      CHECK(gradient_stop_location->value() == 100);
      CHECK(gradient_stop_hex->text() == QStringLiteral("#202020"));
      CHECK(gradient_stop_swatch->styleSheet().contains(QStringLiteral("rgb(32, 32, 32)")));
      bool saw_gradient_swatch_picker = false;
      QTimer::singleShot(0, [&saw_gradient_swatch_picker] {
        for (auto* widget : QApplication::topLevelWidgets()) {
          if (widget->objectName() != QStringLiteral("patchyColorDialog") || !widget->isVisible()) {
            continue;
          }
          auto* picker = widget->findChild<patchy::ui::PatchyColorPicker*>(QStringLiteral("patchyAdvancedColorPicker"));
          CHECK(picker != nullptr);
          picker->setCurrentColor(QColor(220, 40, 96));
          saw_gradient_swatch_picker = true;
          qobject_cast<QDialog*>(widget)->accept();
          return;
        }
        CHECK(false);
      });
      gradient_stop_swatch->click();
      CHECK(saw_gradient_swatch_picker);
      CHECK(gradient_stop_hex->text() == QStringLiteral("#DC2860"));
      bool saw_stroke_color_picker = false;
      QTimer::singleShot(0, [&saw_stroke_color_picker] {
        for (auto* widget : QApplication::topLevelWidgets()) {
          if (widget->objectName() != QStringLiteral("patchyColorDialog") || !widget->isVisible()) {
            continue;
          }
          auto* picker = widget->findChild<patchy::ui::PatchyColorPicker*>(QStringLiteral("patchyAdvancedColorPicker"));
          CHECK(picker != nullptr);
          picker->setCurrentColor(QColor(24, 48, 72));
          saw_stroke_color_picker = true;
          qobject_cast<QDialog*>(widget)->accept();
          return;
        }
        CHECK(false);
      });
      stroke_color->click();
      CHECK(saw_stroke_color_picker);
      CHECK(stroke_red->value() == 24);
      CHECK(stroke_green->value() == 48);
      CHECK(stroke_blue->value() == 72);
      bool saw_outer_glow_color_picker = false;
      QTimer::singleShot(0, [&saw_outer_glow_color_picker] {
        for (auto* widget : QApplication::topLevelWidgets()) {
          if (widget->objectName() != QStringLiteral("patchyColorDialog") || !widget->isVisible()) {
            continue;
          }
          auto* picker = widget->findChild<patchy::ui::PatchyColorPicker*>(QStringLiteral("patchyAdvancedColorPicker"));
          CHECK(picker != nullptr);
          picker->setCurrentColor(QColor(190, 170, 64));
          saw_outer_glow_color_picker = true;
          qobject_cast<QDialog*>(widget)->accept();
          return;
        }
        CHECK(false);
      });
      outer_glow_color->click();
      CHECK(saw_outer_glow_color_picker);
      CHECK(outer_glow_red->value() == 190);
      CHECK(outer_glow_green->value() == 170);
      CHECK(outer_glow_blue->value() == 64);
      bool saw_shadow_color_picker = false;
      QTimer::singleShot(0, [&saw_shadow_color_picker] {
        for (auto* widget : QApplication::topLevelWidgets()) {
          if (widget->objectName() != QStringLiteral("patchyColorDialog") || !widget->isVisible()) {
            continue;
          }
          auto* picker = widget->findChild<patchy::ui::PatchyColorPicker*>(QStringLiteral("patchyAdvancedColorPicker"));
          CHECK(picker != nullptr);
          picker->setCurrentColor(QColor(250, 250, 250));
          saw_shadow_color_picker = true;
          qobject_cast<QDialog*>(widget)->accept();
          return;
        }
        CHECK(false);
      });
      shadow_color->click();
      CHECK(saw_shadow_color_picker);
      CHECK(shadow_red->value() == 250);
      CHECK(shadow_green->value() == 250);
      CHECK(shadow_blue->value() == 250);
      dialog->reject();
      return;
    }
    CHECK(false);
  });
  blending_options->trigger();
  QApplication::processEvents();
  CHECK(saw_non_modal_dialog);
  CHECK(saw_live_style_preview);
  CHECK(saw_layer_style_edit_lock);
  CHECK(saw_shared_color_picker);
  CHECK(!canvas->edit_locked());
  CHECK(layer_list->isEnabled());
  // Cancel restores the layers at once; the canvas re-renders them asynchronously (a slower
  // machine still shows the last preview frame for a moment).
  (void)process_events_until([&] { return color_close(canvas_pixel(*canvas, QPoint(80, 80)), before, 8); }, 5000);
  if (const auto after_cancel = canvas_pixel(*canvas, QPoint(80, 80)); !color_close(after_cancel, before, 8)) {
    std::printf("blending options cancel: before %s, after %s\n", qPrintable(before.name()),
                qPrintable(after_cancel.name()));
    save_widget_artifact("ui_blending_options_after_cancel", *canvas);
    CHECK(false);
  }

  accept_layer_style_dialog(false, true, false);
  blending_options->trigger();
  QApplication::processEvents();
  const auto after = canvas_pixel(*canvas, QPoint(80, 80));
  CHECK(!color_close(before, after, 20));

  auto* styled_item = layer_list->item(0);
  CHECK(styled_item != nullptr);
  styled_item->setCheckState(Qt::Unchecked);
  QApplication::processEvents();
  styled_item->setCheckState(Qt::Checked);
  QApplication::processEvents();
  const auto after_visibility_toggle = canvas_pixel(*canvas, QPoint(80, 80));
  CHECK(color_close(after, after_visibility_toggle, 8));

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_show_transform_controls(false);
  QApplication::processEvents();
  const auto before_move_click = canvas->grab().toImage();
  const auto move_point = canvas->widget_position_for_document_point(QPoint(80, 80));
  send_mouse(*canvas, QEvent::MouseButtonPress, move_point, Qt::LeftButton, Qt::LeftButton);
  QApplication::processEvents();
  const auto during_move_click = canvas->grab().toImage();
  CHECK(color_close(during_move_click.pixelColor(move_point), before_move_click.pixelColor(move_point), 0));
  send_mouse(*canvas, QEvent::MouseButtonRelease, move_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  const auto after_move_click = canvas->grab().toImage();
  CHECK(color_close(after_move_click.pixelColor(move_point), before_move_click.pixelColor(move_point), 0));

  auto* fx_badge =
      layer_list->itemWidget(layer_list->item(0))->findChild<QToolButton*>(QStringLiteral("layerFxBadgeButton"));
  CHECK(fx_badge != nullptr);
  save_widget_artifact("ui_layer_style_result", window);
}

void ui_layer_row_double_click_opens_blending_options_dialog() {
  auto&& [window, canvas] = ShownWindowFixture{};
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* item = layer_list->item(0);
  CHECK(item != nullptr);
  auto* row_widget = layer_list->itemWidget(item);
  CHECK(row_widget != nullptr);
  auto* row_name = row_widget->findChild<QLabel*>(QStringLiteral("layerRowDetails"));
  CHECK(row_name != nullptr);

  bool saw_blending_options = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog"));
    CHECK(dialog != nullptr);
    auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
    auto* blend = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleBlendModeCombo"));
    CHECK(categories != nullptr);
    CHECK(blend != nullptr);
    CHECK(categories->currentItem() != nullptr);
    CHECK(categories->currentItem()->text() == QStringLiteral("Blending Options"));
    saw_blending_options = true;
    dialog->reject();
  });

  send_double_click(*row_name, row_name->rect().center());
  QApplication::processEvents();
  CHECK(saw_blending_options);
}

// The fx badge is a clickable icon button on the row's details line (it replaced
// the old "fx" text): clicking it activates the badge's own layer and opens the
// layer styles dialog. Styleless rows show no badge.
void ui_layer_fx_badge_button_opens_layer_style_dialog() {
  patchy::Document document(48, 48, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background",
                           solid_pixels(48, 48, patchy::PixelFormat::rgba8(), QColor(60, 60, 70, 255)));
  document.add_pixel_layer("Styled", solid_pixels(48, 48, patchy::PixelFormat::rgba8(), QColor(40, 110, 230, 255)));
  auto& styled = document.layers().back();
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.opacity = 0.75F;
  shadow.distance = 4.0F;
  shadow.size = 3.0F;
  styled.layer_style().drop_shadows.push_back(shadow);
  // The click must activate the styled layer itself, so start elsewhere.
  document.set_active_layer(document.layers().front().id());

  auto&& [window, canvas] = ShownWindowFixture{};
  window.add_document_session(std::move(document), QStringLiteral("Fx Badge"));
  QApplication::processEvents();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* background_row = layer_list->itemWidget(require_layer_item(*layer_list, QStringLiteral("Background")));
  CHECK(background_row != nullptr);
  CHECK(background_row->findChild<QToolButton*>(QStringLiteral("layerFxBadgeButton")) == nullptr);
  auto* styled_row = layer_list->itemWidget(require_layer_item(*layer_list, QStringLiteral("Styled")));
  CHECK(styled_row != nullptr);
  auto* fx_badge = styled_row->findChild<QToolButton*>(QStringLiteral("layerFxBadgeButton"));
  CHECK(fx_badge != nullptr);
  CHECK(!fx_badge->icon().isNull());
  // The details line dropped the old "fx" text in favor of the button.
  auto* details = styled_row->findChild<QLabel*>(QStringLiteral("layerRowDetails"));
  CHECK(details != nullptr);
  CHECK(!details->text().contains(QStringLiteral("fx")));

  // A plain mouse click must reach the badge itself instead of the list's row
  // select/drag handling. send_mouse processes events internally, so the badge's
  // deferred action (and its modal style dialog) runs INSIDE the release call:
  // arm a re-arming closer beforehand that rejects the dialog once it appears.
  bool saw_dialog = false;
  int close_attempts = 0;
  std::function<void()> close_style_dialog = [&] {
    if (auto* dialog = find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")); dialog != nullptr) {
      saw_dialog = true;
      dialog->reject();
      return;
    }
    if (++close_attempts < 300) {
      QTimer::singleShot(0, &window, close_style_dialog);
    }
  };
  QTimer::singleShot(0, &window, close_style_dialog);
  send_mouse(*fx_badge, QEvent::MouseButtonPress, fx_badge->rect().center(), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*fx_badge, QEvent::MouseButtonRelease, fx_badge->rect().center(), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(saw_dialog);
  auto& doc = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(doc.active_layer_id().has_value());
  const auto* active_layer = doc.find_layer(*doc.active_layer_id());
  CHECK(active_layer != nullptr);
  CHECK(active_layer->name() == "Styled");
}

void ui_layer_style_stroke_blend_mode_round_trips() {
  // The Stroke page's Blend Mode combo (added July 2026 for Photoshop parity) must
  // save into LayerStroke::blend_mode and load back when the dialog reopens.
  // The Overprint checkbox (July 2026, PS's stroke knockout toggle) rides the
  // same round trip: unchecked by default, saved into LayerStroke::overprint.
  // The Drop Shadow page's "Layer Knocks Out Drop Shadow" checkbox (PS default
  // ON, LayerDropShadow::layer_conceals) round-trips the same way.
  auto&& [window, canvas] = ShownWindowFixture{};
  auto* blending_options = require_action(window, "layerBlendingOptionsAction");

  bool edited = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog"));
    CHECK(dialog != nullptr);
    auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
    auto* stroke_blend = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleStrokeBlendModeCombo"));
    auto* stroke_size = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleStrokeSizeSpin"));
    auto* stroke_overprint = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleStrokeOverprintCheck"));
    auto* shadow_conceals = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleDropShadowConcealsCheck"));
    CHECK(categories != nullptr);
    CHECK(stroke_blend != nullptr);
    CHECK(stroke_size != nullptr);
    CHECK(stroke_overprint != nullptr);
    CHECK(!stroke_overprint->isChecked());  // Photoshop's default
    CHECK(shadow_conceals != nullptr);
    CHECK(shadow_conceals->isChecked());  // Photoshop's default is ON
    // Page values commit when the category selection moves away from the
    // page (or on accept for the current page), so edit the Drop Shadow page
    // first and switch to the Stroke page before accepting.
    const auto shadow_items = categories->findItems(QStringLiteral("Drop Shadow"), Qt::MatchExactly);
    CHECK(!shadow_items.empty());
    shadow_items.front()->setCheckState(Qt::Checked);
    categories->setCurrentItem(shadow_items.front());
    shadow_conceals->setChecked(false);
    const auto stroke_items = categories->findItems(QStringLiteral("Stroke"), Qt::MatchExactly);
    CHECK(!stroke_items.empty());
    stroke_items.front()->setCheckState(Qt::Checked);
    categories->setCurrentItem(stroke_items.front());
    stroke_size->setValue(6);
    stroke_blend->setCurrentIndex(
        std::max(0, stroke_blend->findData(static_cast<int>(patchy::BlendMode::Multiply))));
    CHECK(stroke_blend->currentData().toInt() == static_cast<int>(patchy::BlendMode::Multiply));
    stroke_overprint->setChecked(true);
    edited = true;
    qobject_cast<QDialog*>(dialog)->accept();
  });
  blending_options->trigger();
  QApplication::processEvents();
  CHECK(edited);

  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = document.active_layer_id();
  CHECK(layer_id.has_value());
  const auto* layer = document.find_layer(*layer_id);
  CHECK(layer != nullptr);
  CHECK(!layer->layer_style().strokes.empty());
  CHECK(layer->layer_style().strokes.front().enabled);
  CHECK(layer->layer_style().strokes.front().blend_mode == patchy::BlendMode::Multiply);
  CHECK(layer->layer_style().strokes.front().size == 6.0F);
  CHECK(layer->layer_style().strokes.front().overprint);
  CHECK(!layer->layer_style().drop_shadows.empty());
  CHECK(!layer->layer_style().drop_shadows.front().layer_conceals);

  bool reloaded = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog"));
    CHECK(dialog != nullptr);
    auto* stroke_blend = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleStrokeBlendModeCombo"));
    auto* stroke_overprint = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleStrokeOverprintCheck"));
    auto* shadow_conceals = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleDropShadowConcealsCheck"));
    CHECK(stroke_blend != nullptr);
    CHECK(stroke_overprint != nullptr);
    CHECK(shadow_conceals != nullptr);
    CHECK(stroke_blend->currentData().toInt() == static_cast<int>(patchy::BlendMode::Multiply));
    CHECK(stroke_overprint->isChecked());
    CHECK(!shadow_conceals->isChecked());
    reloaded = true;
    qobject_cast<QDialog*>(dialog)->reject();
  });
  blending_options->trigger();
  QApplication::processEvents();
  CHECK(reloaded);
}

void ui_layer_row_double_click_opens_folder_styles_and_edits_adjustments() {
  patchy::Document document(80, 60, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background",
                           solid_pixels(80, 60, patchy::PixelFormat::rgba8(), QColor(245, 245, 245)));
  patchy::Layer folder(document.allocate_layer_id(), "Folder 1", patchy::LayerKind::Group);
  folder.add_child(patchy::Layer(document.allocate_layer_id(), "Paint Layer",
                                 solid_pixels(20, 20, patchy::PixelFormat::rgba8(), QColor(40, 90, 220))));
  document.add_layer(std::move(folder));
  patchy::Layer levels(document.allocate_layer_id(), "Levels", patchy::LayerKind::Adjustment);
  patchy::AdjustmentSettings settings;
  settings.kind = patchy::AdjustmentKind::Levels;
  patchy::configure_adjustment_layer(levels, settings);
  document.add_layer(std::move(levels));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Double Click Layer Kinds"));
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  const auto row_name_for = [layer_list](const QString& layer_name) {
    auto* item = require_layer_item(*layer_list, layer_name);
    auto* row_widget = layer_list->itemWidget(item);
    CHECK(row_widget != nullptr);
    auto* row_name = row_widget->findChild<QLabel*>(QStringLiteral("layerRowDetails"));
    CHECK(row_name != nullptr);
    return row_name;
  };

  // Double-clicking a folder opens the layer styles dialog like any other
  // layer: group layer effects render since July 2026.
  bool folder_opened_style_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* style_dialog = find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog"));
    CHECK(find_top_level_dialog(QStringLiteral("patchyLevelsDialog")) == nullptr);
    if (style_dialog != nullptr) {
      folder_opened_style_dialog = true;
      style_dialog->reject();
    }
  });
  auto* folder_name = row_name_for(QStringLiteral("Folder 1"));
  send_double_click(*folder_name, folder_name->rect().center());
  QApplication::processEvents();
  CHECK(folder_opened_style_dialog);

  // Double-clicking an adjustment layer opens its settings dialog (Levels
  // here) instead of the blending dialog.
  bool saw_levels_dialog = false;
  QTimer::singleShot(0, [&] {
    CHECK(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")) == nullptr);
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyLevelsDialog"));
    CHECK(dialog != nullptr);
    saw_levels_dialog = true;
    dialog->reject();
  });
  auto* levels_name = row_name_for(QStringLiteral("Levels"));
  send_double_click(*levels_name, levels_name->rect().center());
  QApplication::processEvents();
  CHECK(saw_levels_dialog);
}

void ui_layer_context_menu_rasterizes_text_and_layer_styles() {
  {
    patchy::Document document(140, 96, patchy::PixelFormat::rgba8());
    patchy::Layer text_layer(document.allocate_layer_id(), "Text: Raster", patchy::LayerKind::Text);
    text_layer.set_bounds(patchy::Rect{24, 24, 96, 36});
    text_layer.metadata()[patchy::kLayerMetadataText] = "Raster";
    text_layer.metadata()[patchy::kLayerMetadataTextSize] = "28";
    text_layer.metadata()[patchy::kLayerMetadataTextColor] = "#000000";
    patchy::LayerDropShadow text_shadow;
    text_shadow.enabled = true;
    text_shadow.opacity = 1.0F;
    text_shadow.distance = 6.0F;
    text_shadow.size = 6.0F;
    text_layer.layer_style().drop_shadows.push_back(text_shadow);
    document.add_layer(std::move(text_layer));

    patchy::ui::MainWindow window;
    window.add_document_session(std::move(document), QStringLiteral("Rasterize Text"));
    show_window(window);
    auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
    auto* layer_info = window.findChild<QLabel*>(QStringLiteral("activeLayerInfoLabel"));
    auto* geometry = window.findChild<QLabel*>(QStringLiteral("activeLayerGeometryLabel"));
    auto* text_info = window.findChild<QLabel*>(QStringLiteral("activeLayerTextLabel"));
    CHECK(layer_list != nullptr);
    CHECK(layer_info != nullptr);
    CHECK(geometry != nullptr);
    CHECK(text_info != nullptr);
    auto* thumbnail = layer_list->itemWidget(layer_list->item(0))->findChild<QLabel*>(QStringLiteral("layerContentThumbnail"));
    CHECK(thumbnail != nullptr);
    CHECK(thumbnail->toolTip() == QStringLiteral("Text layer"));
    const auto thumbnail_image = thumbnail->pixmap(Qt::ReturnByValue).toImage();
    int bright_thumbnail_pixels = 0;
    for (int y = 0; y < thumbnail_image.height(); ++y) {
      for (int x = 0; x < thumbnail_image.width(); ++x) {
        const auto color = thumbnail_image.pixelColor(x, y);
        if (color.red() + color.green() + color.blue() > 600) {
          ++bright_thumbnail_pixels;
        }
      }
    }
    CHECK(bright_thumbnail_pixels > 20);
    CHECK(layer_info->text().contains(QStringLiteral("Text Layer")));
    CHECK(geometry->text().contains(QStringLiteral("Drop Shadow")));
    CHECK(!text_info->isHidden());

    require_action(window, "layerRasterizeAction")->trigger();
    QApplication::processEvents();

    CHECK(layer_info->text().contains(QStringLiteral("Pixel Layer")));
    CHECK(geometry->text().contains(QStringLiteral("Drop Shadow")));
    CHECK(text_info->isHidden());
    auto* name = layer_list->itemWidget(layer_list->item(0))->findChild<QLabel*>(QStringLiteral("layerRowName"));
    CHECK(name != nullptr);
    CHECK(name->text() == QStringLiteral("Raster"));
    CHECK(!require_action(window, "layerRasterizeAction")->isEnabled());
    CHECK(require_action(window, "layerRasterizeLayerStyleAction")->isEnabled());
  }

  {
    patchy::Document document(140, 96, patchy::PixelFormat::rgba8());
    auto pixels = solid_pixels(36, 28, patchy::PixelFormat::rgba8(), QColor(40, 90, 220, 255));
    patchy::Layer styled_layer(document.allocate_layer_id(), "Styled Pixel", std::move(pixels));
    styled_layer.set_bounds(patchy::Rect{34, 28, 36, 28});
    patchy::LayerDropShadow shadow;
    shadow.enabled = true;
    shadow.opacity = 1.0F;
    shadow.distance = 8.0F;
    shadow.size = 8.0F;
    shadow.color = patchy::RgbColor{0, 0, 0};
    styled_layer.layer_style().drop_shadows.push_back(shadow);
    document.add_layer(std::move(styled_layer));

    // Reimport once so this exercises the Photoshop raw-style preservation
    // path as well as the modeled style. Rasterizing must remove both or the
    // next PSD save would resurrect the original effect over baked pixels.
    auto imported_document =
        patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document));
    CHECK(std::any_of(imported_document.layers().front().unknown_psd_blocks().begin(),
                      imported_document.layers().front().unknown_psd_blocks().end(),
                      [](const patchy::UnknownPsdBlock& block) {
                        return block.key == "lfx2" || block.key == "lrFX";
                      }));

    patchy::ui::MainWindow window;
    window.add_document_session(std::move(imported_document), QStringLiteral("Rasterize Style"));
    show_window(window);
    auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
    auto* geometry = window.findChild<QLabel*>(QStringLiteral("activeLayerGeometryLabel"));
    CHECK(layer_list != nullptr);
    CHECK(geometry != nullptr);
    CHECK(geometry->text().contains(QStringLiteral("Drop Shadow")));
    CHECK(!require_action(window, "layerRasterizeAction")->isEnabled());
    CHECK(require_action(window, "layerRasterizeLayerStyleAction")->isEnabled());

    require_action(window, "layerRasterizeLayerStyleAction")->trigger();
    QApplication::processEvents();

    CHECK(geometry->text().contains(QStringLiteral("Effects: none")));
    CHECK(!require_action(window, "layerRasterizeLayerStyleAction")->isEnabled());
    const auto& rasterized_document = patchy::ui::MainWindowTestAccess::document(window);
    const auto& rasterized_layer = rasterized_document.layers().front();
    CHECK(std::none_of(rasterized_layer.unknown_psd_blocks().begin(),
                       rasterized_layer.unknown_psd_blocks().end(),
                       [](const patchy::UnknownPsdBlock& block) {
                         return block.key == "lfx2" || block.key == "lrFX";
                       }));
    const auto resaved = patchy::psd::DocumentIo::read(
        patchy::psd::DocumentIo::write_layered_rgb8(rasterized_document));
    CHECK(resaved.layers().front().layer_style().empty());
    CHECK(std::none_of(resaved.layers().front().unknown_psd_blocks().begin(),
                       resaved.layers().front().unknown_psd_blocks().end(),
                       [](const patchy::UnknownPsdBlock& block) {
                         return block.key == "lfx2" || block.key == "lrFX";
                       }));
  }
}

void ui_layer_rasterize_recurses_into_selected_folders() {
  patchy::Document document(140, 96, patchy::PixelFormat::rgba8());
  patchy::Layer folder(document.allocate_layer_id(), "Folder", patchy::LayerKind::Group);
  // Collapsed on purpose: children of a collapsed folder have no panel rows,
  // so rasterize has to find them through the document tree.
  folder.metadata()[patchy::kLayerMetadataGroupExpanded] = "false";

  patchy::Layer first_text(document.allocate_layer_id(), "Text: One", patchy::LayerKind::Text);
  const auto first_text_id = first_text.id();
  first_text.set_bounds(patchy::Rect{24, 12, 96, 30});
  first_text.metadata()[patchy::kLayerMetadataText] = "One";
  first_text.metadata()[patchy::kLayerMetadataTextSize] = "24";
  first_text.metadata()[patchy::kLayerMetadataTextColor] = "#000000";
  folder.add_child(std::move(first_text));

  patchy::Layer nested(document.allocate_layer_id(), "Nested", patchy::LayerKind::Group);
  patchy::Layer second_text(document.allocate_layer_id(), "Text: Two", patchy::LayerKind::Text);
  const auto second_text_id = second_text.id();
  second_text.set_bounds(patchy::Rect{24, 50, 96, 30});
  second_text.metadata()[patchy::kLayerMetadataText] = "Two";
  second_text.metadata()[patchy::kLayerMetadataTextSize] = "24";
  second_text.metadata()[patchy::kLayerMetadataTextColor] = "#000000";
  nested.add_child(std::move(second_text));
  folder.add_child(std::move(nested));
  document.add_layer(std::move(folder));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Rasterize Folder"));
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  CHECK(layer_list->count() == 1);
  CHECK(require_action(window, "layerRasterizeAction")->isEnabled());

  require_action(window, "layerRasterizeAction")->trigger();
  QApplication::processEvents();

  const auto& rasterized_document = patchy::ui::MainWindowTestAccess::document(window);
  const auto* first = rasterized_document.find_layer(first_text_id);
  const auto* second = rasterized_document.find_layer(second_text_id);
  CHECK(first != nullptr);
  CHECK(second != nullptr);
  CHECK(first->kind() == patchy::LayerKind::Pixel);
  CHECK(second->kind() == patchy::LayerKind::Pixel);
  CHECK(!patchy::layer_is_text(*first));
  CHECK(!patchy::layer_is_text(*second));
  CHECK(first->name() == "One");
  CHECK(second->name() == "Two");
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Rasterized layers"));
  CHECK(!require_action(window, "layerRasterizeAction")->isEnabled());
}

void ui_layer_context_menu_layer_style_actions_follow_selection_state() {
  patchy::Document document(160, 120, patchy::PixelFormat::rgba8());
  patchy::Layer styled_layer(document.allocate_layer_id(), "Styled Source",
                             solid_pixels(24, 24, patchy::PixelFormat::rgba8(), QColor(220, 40, 40, 255)));
  styled_layer.set_bounds(patchy::Rect{16, 16, 24, 24});
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.opacity = 1.0F;
  shadow.distance = 8.0F;
  shadow.size = 8.0F;
  styled_layer.layer_style().drop_shadows.push_back(shadow);
  document.add_layer(std::move(styled_layer));

  patchy::Layer target_a_layer(document.allocate_layer_id(), "Plain Target A",
                               solid_pixels(24, 24, patchy::PixelFormat::rgba8(), QColor(40, 180, 80, 255)));
  target_a_layer.set_bounds(patchy::Rect{56, 16, 24, 24});
  document.add_layer(std::move(target_a_layer));

  patchy::Layer target_b_layer(document.allocate_layer_id(), "Plain Target B",
                               solid_pixels(24, 24, patchy::PixelFormat::rgba8(), QColor(40, 80, 220, 255)));
  target_b_layer.set_bounds(patchy::Rect{96, 16, 24, 24});
  document.add_layer(std::move(target_b_layer));

  auto&& [window, canvas] = ShownWindowFixture{};
  window.add_document_session(std::move(document), QStringLiteral("Layer Style Menu State"));
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  auto* styled_item = require_layer_item(*layer_list, QStringLiteral("Styled Source"));
  auto* target_a_item = require_layer_item(*layer_list, QStringLiteral("Plain Target A"));
  auto* target_b_item = require_layer_item(*layer_list, QStringLiteral("Plain Target B"));

  layer_list->clearSelection();
  layer_list->setCurrentItem(styled_item);
  styled_item->setSelected(true);
  QApplication::processEvents();
  auto state = layer_style_context_menu_state(*layer_list, *styled_item);
  CHECK(state.saw_copy);
  CHECK(state.saw_paste);
  CHECK(state.saw_delete);
  CHECK(state.copy_enabled);
  CHECK(!state.paste_enabled);
  CHECK(state.delete_enabled);

  layer_list->clearSelection();
  target_a_item->setSelected(true);
  target_b_item->setSelected(true);
  QApplication::processEvents();
  state = layer_style_context_menu_state(*layer_list, *target_a_item);
  CHECK(state.saw_copy);
  CHECK(state.saw_paste);
  CHECK(state.saw_delete);
  CHECK(!state.copy_enabled);
  CHECK(!state.paste_enabled);
  CHECK(!state.delete_enabled);

  layer_list->clearSelection();
  styled_item->setSelected(true);
  target_a_item->setSelected(true);
  QApplication::processEvents();
  state = layer_style_context_menu_state(*layer_list, *target_a_item);
  CHECK(!state.copy_enabled);
  CHECK(!state.paste_enabled);
  CHECK(state.delete_enabled);
}

void ui_layer_style_copy_paste_delete_applies_to_selected_layers() {
  patchy::Document document(180, 120, patchy::PixelFormat::rgba8());
  patchy::Layer source_layer(document.allocate_layer_id(), "Style Source",
                             solid_pixels(26, 26, patchy::PixelFormat::rgba8(), QColor(230, 60, 40, 255)));
  source_layer.set_bounds(patchy::Rect{16, 22, 26, 26});
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.opacity = 0.9F;
  shadow.distance = 7.0F;
  shadow.size = 7.0F;
  source_layer.layer_style().drop_shadows.push_back(shadow);
  patchy::LayerSatin custom_satin;
  custom_satin.enabled = false;
  custom_satin.unsupported_contour_options = true;
  source_layer.layer_style().satins.push_back(custom_satin);
  document.add_layer(std::move(source_layer));

  patchy::Layer target_a_layer(document.allocate_layer_id(), "Target Multiply",
                               solid_pixels(26, 26, patchy::PixelFormat::rgba8(), QColor(40, 180, 80, 255)));
  target_a_layer.set_bounds(patchy::Rect{66, 22, 26, 26});
  target_a_layer.set_blend_mode(patchy::BlendMode::Multiply);
  target_a_layer.set_opacity(0.42F);
  document.add_layer(std::move(target_a_layer));

  patchy::Layer target_b_layer(document.allocate_layer_id(), "Target Screen",
                               solid_pixels(26, 26, patchy::PixelFormat::rgba8(), QColor(40, 80, 220, 255)));
  target_b_layer.set_bounds(patchy::Rect{116, 22, 26, 26});
  target_b_layer.set_blend_mode(patchy::BlendMode::Screen);
  target_b_layer.set_opacity(0.77F);
  document.add_layer(std::move(target_b_layer));

  auto&& [window, canvas] = ShownWindowFixture{};
  window.add_document_session(std::move(document), QStringLiteral("Layer Style Copy Paste Delete"));
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* layer_info = window.findChild<QLabel*>(QStringLiteral("activeLayerInfoLabel"));
  auto* geometry = window.findChild<QLabel*>(QStringLiteral("activeLayerGeometryLabel"));
  CHECK(layer_list != nullptr);
  CHECK(layer_info != nullptr);
  CHECK(geometry != nullptr);

  auto select_single_layer = [&](const QString& name) {
    auto* item = require_layer_item(*layer_list, name);
    layer_list->clearSelection();
    layer_list->setCurrentItem(item);
    item->setSelected(true);
    QApplication::processEvents();
    return item;
  };

  select_single_layer(QStringLiteral("Style Source"));
  auto* copy_style = require_action(window, "layerCopyStyleAction");
  auto* paste_style = require_action(window, "layerPasteStyleAction");
  auto* delete_style = require_action(window, "layerDeleteStyleAction");
  CHECK(copy_style->isEnabled());
  CHECK(!paste_style->isEnabled());
  copy_style->trigger();
  QApplication::processEvents();
  CHECK(paste_style->isEnabled());

  auto* target_a_item = require_layer_item(*layer_list, QStringLiteral("Target Multiply"));
  auto* target_b_item = require_layer_item(*layer_list, QStringLiteral("Target Screen"));
  layer_list->clearSelection();
  target_a_item->setSelected(true);
  target_b_item->setSelected(true);
  QApplication::processEvents();
  CHECK(!copy_style->isEnabled());
  CHECK(paste_style->isEnabled());
  paste_style->trigger();
  QApplication::processEvents();

  const auto& pasted_document = patchy::ui::MainWindowTestAccess::document(window);
  const auto find_named_layer = [&](std::string_view name) -> const patchy::Layer* {
    const auto found = std::find_if(pasted_document.layers().begin(), pasted_document.layers().end(),
                                    [name](const patchy::Layer& layer) { return layer.name() == name; });
    return found == pasted_document.layers().end() ? nullptr : &*found;
  };
  const auto* source_after_copy = find_named_layer("Style Source");
  const auto* target_a_after_paste = find_named_layer("Target Multiply");
  const auto* target_b_after_paste = find_named_layer("Target Screen");
  CHECK(source_after_copy != nullptr);
  CHECK(target_a_after_paste != nullptr);
  CHECK(target_b_after_paste != nullptr);
  CHECK(source_after_copy->layer_style().satins.front().unsupported_contour_options);
  CHECK(!target_a_after_paste->layer_style().satins.front().unsupported_contour_options);
  CHECK(!target_b_after_paste->layer_style().satins.front().unsupported_contour_options);

  select_single_layer(QStringLiteral("Target Multiply"));
  CHECK(geometry->text().contains(QStringLiteral("Drop Shadow")));
  CHECK(layer_info->text().contains(QStringLiteral("Mode: Multiply")));
  CHECK(layer_info->text().contains(QStringLiteral("Opacity: 42%")));

  select_single_layer(QStringLiteral("Target Screen"));
  CHECK(geometry->text().contains(QStringLiteral("Drop Shadow")));
  CHECK(layer_info->text().contains(QStringLiteral("Mode: Screen")));
  CHECK(layer_info->text().contains(QStringLiteral("Opacity: 77%")));

  target_a_item = require_layer_item(*layer_list, QStringLiteral("Target Multiply"));
  target_b_item = require_layer_item(*layer_list, QStringLiteral("Target Screen"));
  layer_list->clearSelection();
  target_a_item->setSelected(true);
  target_b_item->setSelected(true);
  QApplication::processEvents();
  CHECK(delete_style->isEnabled());
  delete_style->trigger();
  QApplication::processEvents();

  select_single_layer(QStringLiteral("Target Multiply"));
  CHECK(geometry->text().contains(QStringLiteral("Effects: none")));
  CHECK(layer_info->text().contains(QStringLiteral("Mode: Multiply")));
  CHECK(layer_info->text().contains(QStringLiteral("Opacity: 42%")));

  select_single_layer(QStringLiteral("Target Screen"));
  CHECK(geometry->text().contains(QStringLiteral("Effects: none")));
  CHECK(layer_info->text().contains(QStringLiteral("Mode: Screen")));
  CHECK(layer_info->text().contains(QStringLiteral("Opacity: 77%")));

  select_single_layer(QStringLiteral("Style Source"));
  CHECK(geometry->text().contains(QStringLiteral("Drop Shadow")));
}

void inspect_new_document_dialog_without_clipboard() {
  QTimer::singleShot(0, [] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyNewDocumentDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      auto* presets = dialog->findChild<QListWidget*>(QStringLiteral("newDocumentPresetList"));
      auto* screen_chip = dialog->findChild<QToolButton*>(QStringLiteral("newDocumentScreenChip"));
      auto* print_chip = dialog->findChild<QToolButton*>(QStringLiteral("newDocumentPrintChip"));
      auto* width = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("newDocumentWidthSpin"));
      auto* height = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("newDocumentHeightSpin"));
      auto* resolution = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("newDocumentResolutionSpin"));
      auto* unit = dialog->findChild<QComboBox*>(QStringLiteral("newDocumentUnitCombo"));
      auto* summary = dialog->findChild<QLabel*>(QStringLiteral("newDocumentSummaryLabel"));
      auto* swap_dimensions =
          dialog->findChild<QToolButton*>(QStringLiteral("newDocumentSwapDimensionsButton"));
      CHECK(dialog != nullptr);
      CHECK(presets != nullptr);
      CHECK(screen_chip != nullptr);
      CHECK(print_chip != nullptr);
      CHECK(width != nullptr);
      CHECK(height != nullptr);
      CHECK(resolution != nullptr);
      CHECK(unit != nullptr);
      CHECK(summary != nullptr);
      CHECK(swap_dimensions != nullptr);
      CHECK(width->buttonSymbols() == QAbstractSpinBox::NoButtons);
      CHECK(width->width() == height->width());
      CHECK(width->width() == resolution->width());

      const auto card_ids = [presets] {
        QStringList ids;
        for (int row = 0; row < presets->count(); ++row) {
          ids << presets->item(row)->data(patchy::ui::kNewDocumentPresetIdRole).toString();
        }
        return ids;
      };

      // Fresh settings: the Screen category shows with the default card selected
      // at Photoshop's 72 PPI screen convention.
      CHECK(screen_chip->isChecked());
      const QStringList screen_ids = {
          QStringLiteral("screen-1024x768"),
          QStringLiteral("screen-720p"),        QStringLiteral("screen-1080p"),
          QStringLiteral("screen-1440p"),       QStringLiteral("screen-4k"),
          QStringLiteral("screen-8k"),          QStringLiteral("screen-ultrawide-3440"),
          QStringLiteral("screen-dci-4k"),      QStringLiteral("screen-laptop-1366"),
          QStringLiteral("screen-square-2048")};
      CHECK(card_ids() == screen_ids);
      // Without an image on the clipboard and before any create, the Recent row is empty.
      auto* recent = dialog->findChild<QListWidget*>(QStringLiteral("newDocumentRecentList"));
      CHECK(recent != nullptr && recent->count() == 0);
      CHECK(presets->currentItem() != nullptr);
      CHECK(presets->currentItem()->data(patchy::ui::kNewDocumentPresetIdRole).toString() ==
            QStringLiteral("screen-1024x768"));
      CHECK(width->value() == 1024);
      CHECK(height->value() == 768);
      CHECK(std::abs(resolution->value() - 72.0) < 0.01);
      CHECK(unit->currentText() == QStringLiteral("Pixels"));

      // The Print category lists the paper cards; switching categories keeps the
      // pending size until a card is clicked.
      print_chip->click();
      QApplication::processEvents();
      const QStringList print_ids = {
          QStringLiteral("print-a6"),         QStringLiteral("print-a5"),
          QStringLiteral("print-a4"),         QStringLiteral("print-a3"),
          QStringLiteral("print-a2"),         QStringLiteral("print-us-letter"),
          QStringLiteral("print-us-legal"),   QStringLiteral("print-tabloid"),
          QStringLiteral("print-business-card-us"), QStringLiteral("print-business-card-eu"),
          QStringLiteral("print-poster-18x24")};
      CHECK(card_ids() == print_ids);
      CHECK(width->value() == 1024);
      screen_chip->click();
      QApplication::processEvents();
      CHECK(card_ids() == screen_ids);
      CHECK(presets->currentItem() != nullptr);
      CHECK(presets->currentItem()->data(patchy::ui::kNewDocumentPresetIdRole).toString() ==
            QStringLiteral("screen-1024x768"));

      // A physical unit converts the display through the resolution: 1024 px at
      // 72 ppi is 14.22 in.
      unit->setCurrentIndex(unit->findText(QStringLiteral("Inches")));
      QApplication::processEvents();
      CHECK(std::abs(width->value() - 1024.0 / 72.0) < 0.01);
      unit->setCurrentIndex(unit->findText(QStringLiteral("Pixels")));
      QApplication::processEvents();
      CHECK(width->value() == 1024);

      // Swapping the dimensions makes the size custom: the card selection clears
      // and the summary reports it.
      swap_dimensions->click();
      QApplication::processEvents();
      CHECK(presets->currentItem() == nullptr);
      CHECK(width->value() == 768);
      CHECK(height->value() == 1024);
      CHECK(summary->text().contains(QStringLiteral("Custom")));
      CHECK(summary->text().contains(QStringLiteral("768 x 1024 px")));

      widget->grab().save(QStringLiteral("test-artifacts/ui_new_document_dialog_cards.png"));
      dialog->reject();
      return;
    }
    CHECK(false);
  });
}

void ui_closing_last_document_leaves_empty_workspace() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  auto* info = window.findChild<QLabel*>(QStringLiteral("documentInfoLabel"));
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* foreground = window.findChild<QPushButton*>(QStringLiteral("foregroundColorButton"));
  CHECK(tabs != nullptr);
  CHECK(info != nullptr);
  CHECK(layer_list != nullptr);
  CHECK(foreground != nullptr);
  CHECK(tabs->count() == 1);

  CHECK(QMetaObject::invokeMethod(tabs, "tabCloseRequested", Qt::DirectConnection, Q_ARG(int, 0)));
  QApplication::processEvents();

  CHECK(tabs->count() == 0);
  CHECK(info->text() == QStringLiteral("No document"));
  CHECK(layer_list->count() == 0);
  CHECK(!layer_list->isEnabled());
  CHECK(!foreground->isEnabled());
  CHECK(!require_action(window, "fileSaveAction")->isEnabled());
  CHECK(!require_action(window, "layerNewAction")->isEnabled());
  CHECK(!require_action(window, "brushLargerAction")->isEnabled());
  CHECK(require_action(window, "fileNewAction")->isEnabled());
  CHECK(require_action(window, "fileOpenAction")->isEnabled());

  accept_new_document_dialog(320, 180);
  require_action(window, "fileNewAction")->trigger();
  QApplication::processEvents();

  CHECK(tabs->count() == 1);
  CHECK(info->text().contains(QStringLiteral("320 x 180 px")));
  CHECK(layer_list->isEnabled());
  CHECK(foreground->isEnabled());
  CHECK(require_action(window, "fileSaveAction")->isEnabled());
  auto* canvas = require_canvas(window);
  auto* brush_preset = window.findChild<QComboBox*>(QStringLiteral("brushPresetCombo"));
  CHECK(brush_preset != nullptr);
  CHECK(brush_preset->currentData().toString() == QStringLiteral("round"));
  CHECK(canvas->brush_size() == 25);
  CHECK(canvas->brush_opacity() == 100);
  CHECK(canvas->brush_softness() == 0);
}

// Regression: closing the last tab while an inline text edit is open must
// commit the edit before the save prompt.  An editor that survived into
// removeTab() auto-committed on the focus change mid teardown, after
// activate_document_tab() had already cleared the active canvas pointer, and
// crashed dereferencing it (the June 10 2026 WER dumps for patchy.exe).
void ui_close_last_tab_with_active_text_edit_commits_editor_first() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  CHECK(tabs != nullptr);
  CHECK(tabs->count() == 1);
  auto* canvas = require_canvas(window);

  // Dirty the document so confirm_close_session shows the save prompt.
  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  QApplication::processEvents();
  const auto center = canvas->rect().center();
  send_mouse(*canvas, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  editor->setPlainText(QStringLiteral("Patchy"));
  QApplication::processEvents();

  // The prompt opens a nested event loop, so dismiss it from a polling timer.
  // The editor must already be finished by prompt time: that is the fix.
  bool prompt_seen = false;
  bool editor_gone_at_prompt = false;
  auto* dismiss_timer = new QTimer(&window);
  dismiss_timer->setInterval(10);
  QObject::connect(dismiss_timer, &QTimer::timeout, &window, [&] {
    auto* dialog = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("saveChangesMessageBox")));
    if (dialog == nullptr) {
      return;
    }
    prompt_seen = true;
    editor_gone_at_prompt = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr;
    dismiss_timer->stop();
    dialog->button(QMessageBox::Discard)->click();
  });
  dismiss_timer->start();

  CHECK(QMetaObject::invokeMethod(tabs, "tabCloseRequested", Qt::DirectConnection, Q_ARG(int, 0)));
  QApplication::processEvents();
  dismiss_timer->stop();

  CHECK(prompt_seen);
  CHECK(editor_gone_at_prompt);
  CHECK(tabs->count() == 0);
}

// The close-document save prompt asks a yes/no question with Yes / No / Cancel, Y, N and
// C underlined and Yes the default (GitHub issues 70 and 240). The buttons keep the Save
// and Discard roles. Bare key presses (no Alt) answer it like native Windows message
// boxes: Y, N and C, plus S and D as aliases from the Save / Don't Save days. Qt itself
// only wires the Alt+mnemonic; show_warning_message adds the plain letters, so both the
// button set and the accelerators are pinned here.
void ui_save_prompt_uses_yes_no_cancel_with_letter_hotkeys() {
  std::filesystem::create_directories("test-artifacts");
  const auto path = QFileInfo(QDir(QStringLiteral("test-artifacts"))
                                  .filePath(QStringLiteral("ui_save_prompt_yes_no.tga")))
                        .absoluteFilePath();
  const QColor left_color(200, 40, 40);
  const QColor right_color(40, 80, 200);
  {
    patchy::Document source(8, 6, patchy::PixelFormat::rgb8());
    patchy::PixelBuffer pixels(8, 6, patchy::PixelFormat::rgb8());
    for (std::int32_t y = 0; y < 6; ++y) {
      for (std::int32_t x = 0; x < 8; ++x) {
        const auto color = x < 4 ? left_color : right_color;
        auto* px = pixels.pixel(x, y);
        px[0] = static_cast<std::uint8_t>(color.red());
        px[1] = static_cast<std::uint8_t>(color.green());
        px[2] = static_cast<std::uint8_t>(color.blue());
      }
    }
    source.add_pixel_layer("Background", std::move(pixels));
    patchy::tga::DocumentIo::write_file(source, std::filesystem::path(path.toStdWString()));
  }

  auto&& [window, canvas] = ShownWindowFixture{};
  CHECK(window.screen() != nullptr);
  // Closing a partly offscreen main window must leave every prompt button
  // reachable, including the native frame below the message-box content.
  window.move(window.screen()->availableGeometry().bottomRight() - QPoint(80, 80));
  QApplication::processEvents();
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  CHECK(tabs != nullptr);

  const auto corner_color = [&window] {
    auto& document = patchy::ui::MainWindowTestAccess::document(window);
    const auto* px = document.layers().front().pixels().pixel(0, 0);
    return QColor(px[0], px[1], px[2]);
  };

  // Dismisses the save prompt with a bare letter key once it appears, recording
  // the button layout. The prompt runs a nested event loop, hence the timer.
  bool prompt_seen = false;
  bool buttons_are_yes_no_cancel = false;
  bool prompt_is_inside_screen = false;
  const auto dismiss_prompt_with_key = [&](int key) {
    prompt_seen = false;
    buttons_are_yes_no_cancel = false;
    prompt_is_inside_screen = false;
    auto* dismiss_timer = new QTimer(&window);
    dismiss_timer->setInterval(10);
    // The placement filter re-clamps the box one event-loop turn after each Show and
    // Resize (QMessageBox settles its size late), so judge the position only once the
    // frame has held still for three ticks; earlier reads caught the box mid-placement.
    auto last_frame = std::make_shared<QRect>();
    auto steady_ticks = std::make_shared<int>(0);
    QObject::connect(dismiss_timer, &QTimer::timeout, &window, [&, key, dismiss_timer, last_frame, steady_ticks] {
      auto* dialog = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("saveChangesMessageBox")));
      if (dialog == nullptr) {
        return;
      }
      if (dialog->frameGeometry() != *last_frame) {
        *last_frame = dialog->frameGeometry();
        *steady_ticks = 0;
        return;
      }
      if (++*steady_ticks < 3) {
        return;
      }
      prompt_seen = true;
      prompt_is_inside_screen = dialog->screen() != nullptr &&
                                dialog->screen()->availableGeometry().contains(dialog->frameGeometry());
      if (!prompt_is_inside_screen) {
        // Fails only after other tests in a full run: report what grew or moved.
        const auto f = dialog->frameGeometry();
        const auto s = dialog->screen() != nullptr ? dialog->screen()->availableGeometry() : QRect();
        const auto w = window.frameGeometry();
        std::cerr << "  save prompt frame " << f.x() << ',' << f.y() << ' ' << f.width() << 'x' << f.height()
                  << " screen " << s.x() << ',' << s.y() << ' ' << s.width() << 'x' << s.height() << " window "
                  << w.x() << ',' << w.y() << ' ' << w.width() << 'x' << w.height() << " font pt "
                  << dialog->font().pointSizeF() << " dpr " << dialog->devicePixelRatioF() << '\n';
      }
      auto* save = dialog->button(QMessageBox::Save);
      auto* discard = dialog->button(QMessageBox::Discard);
      auto* cancel = dialog->button(QMessageBox::Cancel);
      buttons_are_yes_no_cancel =
          save != nullptr && discard != nullptr && cancel != nullptr &&
          dialog->button(QMessageBox::Yes) == nullptr && dialog->button(QMessageBox::No) == nullptr &&
          dialog->defaultButton() == save && save->text() == QStringLiteral("&Yes") &&
          discard->text() == QStringLiteral("&No") && cancel->text() == QStringLiteral("&Cancel") &&
          dialog->text().endsWith(QLatin1Char('?'));
      dismiss_timer->stop();
      dismiss_timer->deleteLater();
      if (key == Qt::Key_D) {
        save_widget_artifact("ui_save_prompt", *dialog);  // Yes carries the default outline
      }
      // Send to the focused button when there is one: the bare letter must reach
      // the box by propagating up from the child, the interactive path.
      auto* target = dialog->focusWidget() != nullptr ? dialog->focusWidget() : dialog;
      send_key(*target, key);
    });
    dismiss_timer->start();
  };

  // Flips the open document, closes its tab answering the prompt with `key`,
  // and reports whether the prompt appeared with the expected buttons.
  const auto flip_and_close_with_key = [&](int key) {
    require_action_by_text(window, QStringLiteral("Flip Layer Horizontal"))->trigger();
    QApplication::processEvents();
    const int tabs_before_close = tabs->count();
    dismiss_prompt_with_key(key);
    const bool closed = patchy::ui::MainWindowTestAccess::close_document_tab(window, tabs->currentIndex());
    QApplication::processEvents();
    CHECK(prompt_seen);
    CHECK(buttons_are_yes_no_cancel);
    CHECK(prompt_is_inside_screen);
    // C cancels: the tab stays open with its changes.
    CHECK(closed == (key != Qt::Key_C));
    CHECK(tabs->count() == tabs_before_close - (key == Qt::Key_C ? 0 : 1));
  };
  const auto reopen = [&] {
    patchy::ui::MainWindowTestAccess::open_document_path(window, path);
    QApplication::processEvents();
  };

  // N answers No: the document closes and the file keeps its pixels.
  reopen();
  CHECK(corner_color() == left_color);
  flip_and_close_with_key(Qt::Key_N);
  reopen();
  CHECK(corner_color() == left_color);

  // D still means No.
  flip_and_close_with_key(Qt::Key_D);
  reopen();
  CHECK(corner_color() == left_color);

  // Y answers Yes: the flipped pixels reach disk before the tab closes.
  flip_and_close_with_key(Qt::Key_Y);
  reopen();
  CHECK(corner_color() == right_color);

  // S still means Yes.
  flip_and_close_with_key(Qt::Key_S);
  reopen();
  CHECK(corner_color() == left_color);

  // C answers Cancel: the flipped document stays open, unsaved; N then closes it.
  flip_and_close_with_key(Qt::Key_C);
  CHECK(corner_color() == right_color);
  dismiss_prompt_with_key(Qt::Key_N);
  CHECK(patchy::ui::MainWindowTestAccess::close_document_tab(window, tabs->currentIndex()));
  QApplication::processEvents();
  reopen();
  CHECK(corner_color() == left_color);
}

void ui_document_tab_context_menu_closes_tabs_and_file_menu_closes_all() {
  const auto make_document = [](QColor color) {
    patchy::Document document(32, 24, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Background", solid_pixels(32, 24, patchy::PixelFormat::rgb8(), color));
    return document;
  };

  auto&& [window, canvas] = ShownWindowFixture{};
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  CHECK(tabs != nullptr);
  auto* tab_bar = tabs->findChild<QTabBar*>();
  CHECK(tab_bar != nullptr);
  CHECK(tab_bar->contextMenuPolicy() == Qt::CustomContextMenu);

  window.add_document_session(make_document(QColor(50, 80, 140)), QStringLiteral("Zulu"));
  window.add_document_session(make_document(QColor(140, 80, 50)), QStringLiteral("Alpha"));
  QApplication::processEvents();
  CHECK(tabs->count() == 3);

  CHECK(window.findChild<QAction*>(QStringLiteral("windowArrangeWindowsAction")) == nullptr);
  CHECK(window.findChild<QAction*>(QStringLiteral("windowCloseAllWindowsAction")) == nullptr);

  const auto target_index = tabs->indexOf(tabs->widget(1));
  CHECK(target_index == 1);
  bool saw_context_menu = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* menu = qobject_cast<QMenu*>(widget);
      if (menu == nullptr || menu->objectName() != QStringLiteral("documentTabContextMenu")) {
        continue;
      }
      auto* close_action = find_menu_action_by_text(*menu, QStringLiteral("Close"));
      auto* close_others_action = find_menu_action_by_text(*menu, QStringLiteral("Close Others"));
      auto* close_all_action = find_menu_action_by_text(*menu, QStringLiteral("Close All"));
      CHECK(close_action != nullptr);
      CHECK(close_others_action != nullptr);
      CHECK(close_all_action != nullptr);
      CHECK(close_action->objectName() == QStringLiteral("documentTabCloseAction"));
      CHECK(close_others_action->objectName() == QStringLiteral("documentTabCloseOthersAction"));
      CHECK(close_all_action->objectName() == QStringLiteral("documentTabCloseAllAction"));
      CHECK(close_others_action->isEnabled());
      close_others_action->trigger();
      menu->close();
      saw_context_menu = true;
      return;
    }
    CHECK(false);
  });

  const auto context_point = tab_bar->tabRect(target_index).center();
  QContextMenuEvent context_event(QContextMenuEvent::Mouse, context_point, tab_bar->mapToGlobal(context_point));
  QApplication::sendEvent(tab_bar, &context_event);
  QApplication::processEvents();

  CHECK(saw_context_menu);
  CHECK(tabs->count() == 1);
  CHECK(tabs->tabText(0) == QStringLiteral("Zulu"));
  CHECK(require_action(window, "fileCloseAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_W));
  auto* file_close_all_action = require_action(window, "fileCloseAllAction");
  CHECK(file_close_all_action->shortcut() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_W));
  file_close_all_action->trigger();
  QApplication::processEvents();
  CHECK(tabs->count() == 0);
  CHECK(!file_close_all_action->isEnabled());
}

void ui_canvas_size_preserves_layers_and_crop_option_resets() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  document = patchy::Document(8, 8, patchy::PixelFormat::rgb8());
  patchy::Layer paint(document.allocate_layer_id(), "Paint",
                      solid_pixels(12, 12, patchy::PixelFormat::rgba8(), QColor(17, 29, 41)));
  const auto id = paint.id();
  paint.set_bounds({-2, -2, 12, 12});
  document.add_layer(std::move(paint));
  auto* canvas = patchy::ui::MainWindowTestAccess::canvas(window);
  canvas->set_document(&document);
  const auto resize = [&](int size, bool crop, bool cancel = false) {
    bool saw_dialog = false;
    std::exception_ptr failure;
    QTimer::singleShot(0, &window, [&] {
      auto* dialog = window.findChild<QDialog*>(QStringLiteral("patchyCanvasSizeDialog"));
      try {
        CHECK(dialog != nullptr);
        auto* checkbox = dialog->findChild<QCheckBox*>(QStringLiteral("canvasSizeCropLayersCheck"));
        CHECK(checkbox != nullptr);
        CHECK(!checkbox->isChecked());
        CHECK(checkbox->text() == QStringLiteral("Also crop each actual layer to the canvas area"));
        checkbox->setChecked(crop);
        auto* width = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("canvasSizeWidthSpin"));
        auto* height = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("canvasSizeHeightSpin"));
        CHECK(width != nullptr && height != nullptr);
        width->setValue(size);
        height->setValue(size);
        save_widget_artifact("ui_canvas_size_crop_option", *dialog);
        saw_dialog = true;
      } catch (...) {
        failure = std::current_exception();
      }
      if (dialog != nullptr) {
        if (cancel || failure) {
          dialog->reject();
        } else {
          dialog->accept();
        }
      }
    });
    require_action(window, "imageCanvasSizeAction")->trigger();
    if (failure) {
      std::rethrow_exception(failure);
    }
    CHECK(saw_dialog);
  };
  const auto layer = [&]() -> const patchy::Layer& { return *std::as_const(document).find_layer(id); };
  resize(4, false);
  CHECK(document.width() == 4);
  CHECK(layer().bounds().x == -4 && layer().bounds().y == -4);
  CHECK(layer().pixels().width() == 12 && layer().pixels().height() == 12);
  CHECK(layer().pixels().pixel(11, 11)[0] == 17);
  patchy::ui::MainWindowTestAccess::undo(window);
  CHECK(document.width() == 8 && layer().bounds().x == -2);
  patchy::ui::MainWindowTestAccess::redo(window);
  CHECK(document.width() == 4 && layer().pixels().width() == 12);
  // Explicit crop also works without changing canvas dimensions.
  resize(4, true);
  CHECK(layer().bounds().x == 0 && layer().bounds().y == 0);
  CHECK(layer().pixels().width() == 4 && layer().pixels().height() == 4);
  patchy::ui::MainWindowTestAccess::undo(window);
  CHECK(layer().bounds().x == -4 && layer().pixels().width() == 12);
  // Both acceptance and cancellation forget the destructive option.
  resize(6, true, true);
  CHECK(document.width() == 4 && layer().pixels().width() == 12);
  resize(8, false);
  CHECK(document.width() == 8 && layer().bounds().x == -2);
  CHECK(layer().pixels().width() == 12 && layer().pixels().pixel(11, 11)[0] == 17);
}

void ui_new_document_and_canvas_size_dialogs_work() {
  auto&& [window, canvas] = ShownWindowFixture{};
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  CHECK(tabs != nullptr);
  CHECK(tabs->count() == 1);

  accept_new_document_dialog(640, 360);
  require_action_by_text(window, QStringLiteral("New"))->trigger();
  QApplication::processEvents();
  CHECK(tabs->count() == 2);
  auto* info = window.findChild<QLabel*>(QStringLiteral("documentInfoLabel"));
  CHECK(info != nullptr);
  CHECK(info->text().contains(QStringLiteral("640 x 360 px")));
  save_widget_artifact("ui_new_document_result", window);

  tabs->setCurrentIndex(0);
  QApplication::processEvents();
  CHECK(info->text().contains(QStringLiteral("1024 x 768 px")));
  tabs->setCurrentIndex(1);
  QApplication::processEvents();
  CHECK(info->text().contains(QStringLiteral("640 x 360 px")));
  save_widget_artifact("ui_multiple_documents", window);

  accept_image_size_dialog(800, 450);
  require_action(window, "imageSizeAction")->trigger();
  QApplication::processEvents();
  CHECK(info->text().contains(QStringLiteral("800 x 450 px")));
  CHECK(info->text().contains(QStringLiteral("72 ppi")));
  save_widget_artifact("ui_image_size_result", window);

  accept_image_size_resolution_dialog(144);
  require_action(window, "imageSizeAction")->trigger();
  QApplication::processEvents();
  CHECK(info->text().contains(QStringLiteral("800 x 450 px")));
  CHECK(info->text().contains(QStringLiteral("144 ppi")));

  accept_canvas_size_dialog(720, 405);
  require_action(window, "imageCanvasSizeAction")->trigger();
  QApplication::processEvents();
  CHECK(info->text().contains(QStringLiteral("720 x 405 px")));
  save_widget_artifact("ui_canvas_size_result", window);
}

// Image > Rotate Arbitrary...: the dialog's angle and direction rotate the whole canvas and
// enlarge it to the rotated bounding box. The quarter-turn commands keep their persisted
// action ids under their new Rotate Right / Rotate Left labels.
void ui_rotate_canvas_arbitrary_dialog_rotates_and_expands_canvas() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* info = window.findChild<QLabel*>(QStringLiteral("documentInfoLabel"));
  CHECK(info != nullptr);
  if (info == nullptr) {
    return;
  }
  CHECK(info->text().contains(QStringLiteral("1024 x 768 px")));
  // GitHub issue 139: the half turn and the whole-image flips sit beside them, and
  // the Layer > Arrange menu turns the selected layers.
  // The mnemonic sits inside the number ("Rotate 1&80"), so compare without it.
  const auto menu_text = [&](const char* name) {
    return require_action(window, name)->text().remove(QLatin1Char('&'));
  };
  CHECK(menu_text("imageRotate180Action").contains(QStringLiteral("180")));
  CHECK(require_action(window, "imageFlipHorizontalAction")->text().contains(QStringLiteral("Horizontal")));
  CHECK(require_action(window, "imageFlipVerticalAction")->text().contains(QStringLiteral("Vertical")));
  CHECK(require_action(window, "layerRotateClockwiseAction")->text().contains(QStringLiteral("Right")));
  CHECK(require_action(window, "layerRotateCounterclockwiseAction")->text().contains(QStringLiteral("Left")));
  CHECK(menu_text("layerRotate180Action").contains(QStringLiteral("180")));
  CHECK(require_action(window, "imageRotateClockwiseAction")->text().contains(QStringLiteral("Right")));
  CHECK(require_action(window, "imageRotateCounterclockwiseAction")->text().contains(QStringLiteral("Left")));

  accept_rotate_canvas_dialog(90.0, true);
  require_action(window, "imageRotateArbitraryAction")->trigger();
  QApplication::processEvents();
  CHECK(info->text().contains(QStringLiteral("768 x 1024 px")));

  // 45 degrees either way: (768 + 1024) * cos 45 = 1267 on both axes.
  accept_rotate_canvas_dialog(45.0, false);
  require_action(window, "imageRotateArbitraryAction")->trigger();
  QApplication::processEvents();
  CHECK(info->text().contains(QStringLiteral("1267 x 1267 px")));
  save_widget_artifact("ui_rotate_canvas_result", window);
}

void ui_new_document_presets_and_clipboard_work() {
  QApplication::clipboard()->clear();
  // Earlier tests in the suite accept the dialog (settings leak by construction);
  // the default-state checks need a clean remembered-settings slate.
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("newDocument"));
  }

  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  auto* info = window.findChild<QLabel*>(QStringLiteral("documentInfoLabel"));
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(tabs != nullptr);
  CHECK(info != nullptr);
  CHECK(layer_list != nullptr);
  CHECK(tabs->count() == 1);

  inspect_new_document_dialog_without_clipboard();
  require_action_by_text(window, QStringLiteral("New"))->trigger();
  QApplication::processEvents();
  CHECK(tabs->count() == 1);

  QImage clipboard_image(123, 45, QImage::Format_RGBA8888);
  clipboard_image.fill(QColor(30, 160, 220, 180));
  QApplication::clipboard()->setImage(clipboard_image);
  QApplication::processEvents();

  accept_clipboard_new_document_dialog(clipboard_image.size());
  require_action_by_text(window, QStringLiteral("New"))->trigger();
  QApplication::processEvents();

  CHECK(tabs->count() == 2);
  CHECK(info->text().contains(QStringLiteral("123 x 45 px")));
  CHECK(layer_list->count() == 1);
  CHECK(require_layer_item(*layer_list, QStringLiteral("Clipboard Image")) != nullptr);
  auto* canvas = require_canvas(window);
  const auto pasted_rect = canvas->active_layer_document_rect();
  CHECK(pasted_rect.has_value());
  CHECK(pasted_rect->topLeft() == QPoint(0, 0));
  CHECK(pasted_rect->size() == clipboard_image.size());

  // The Clipboard create became Last Used at the clipboard's size: reopening (image
  // still on the clipboard) shows the card after Clipboard, first on Print, and
  // selects it with editable fields.
  bool reopened = false;
  QTimer::singleShot(0, [&reopened] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyNewDocumentDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* presets = dialog->findChild<QListWidget*>(QStringLiteral("newDocumentPresetList"));
    auto* recent = dialog->findChild<QListWidget*>(QStringLiteral("newDocumentRecentList"));
    auto* print_chip = dialog->findChild<QToolButton*>(QStringLiteral("newDocumentPrintChip"));
    auto* width = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("newDocumentWidthSpin"));
    auto* height = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("newDocumentHeightSpin"));
    CHECK(presets != nullptr && recent != nullptr && print_chip != nullptr && width != nullptr && height != nullptr);
    if (presets == nullptr || recent == nullptr || print_chip == nullptr || width == nullptr || height == nullptr) {
      dialog->reject();
      return;
    }
    const auto id_at = [recent](int row) {
      return recent->item(row)->data(patchy::ui::kNewDocumentPresetIdRole).toString();
    };
    // The Recent row: Clipboard, then the create, selected with editable fields.
    CHECK(recent->count() == 2);
    CHECK(id_at(0) == QStringLiteral("clipboard"));
    CHECK(id_at(1) == QStringLiteral("last-used"));
    CHECK(recent->item(1)->text() == QStringLiteral("Last Used"));
    CHECK(recent->item(1)->data(patchy::ui::kNewDocumentPresetSizeRole).toSize() == QSize(123, 45));
    CHECK(recent->currentItem() == recent->item(1));
    CHECK(presets->currentItem() == nullptr);
    CHECK(width->isEnabled());
    CHECK(width->value() == 123);
    CHECK(height->value() == 45);
    // All ten Screen templates fit without a scroll bar.
    CHECK(presets->count() == 10);
    CHECK(!presets->verticalScrollBar()->isVisible());
    dialog->grab().save(QStringLiteral("test-artifacts/ui_new_document_dialog_last_used.png"));
    // Another group keeps the Recent row and its selection.
    print_chip->click();
    QApplication::processEvents();
    CHECK(recent->count() == 2);
    CHECK(recent->currentItem() == recent->item(1));
    reopened = true;
    dialog->reject();
  });
  require_action_by_text(window, QStringLiteral("New"))->trigger();
  QApplication::processEvents();
  CHECK(reopened);
  CHECK(tabs->count() == 2);
  QApplication::clipboard()->clear();
}

// Double-clicking a preset card creates the document without a trip to Create.
void ui_new_document_preset_double_click_creates_document() {
  QApplication::clipboard()->clear();
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("newDocument"));
  }
  auto&& [window, canvas] = ShownWindowFixture{};
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  auto* info = window.findChild<QLabel*>(QStringLiteral("documentInfoLabel"));
  CHECK(tabs != nullptr && info != nullptr);
  if (tabs == nullptr || info == nullptr) {
    return;
  }
  CHECK(tabs->count() == 1);

  bool card_seen = false;
  QTimer::singleShot(0, [&card_seen] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyNewDocumentDialog")) {
        continue;
      }
      auto* presets = widget->findChild<QListWidget*>(QStringLiteral("newDocumentPresetList"));
      CHECK(presets != nullptr);
      if (presets == nullptr) {
        return;
      }
      for (int row = 0; row < presets->count(); ++row) {
        auto* item = presets->item(row);
        if (item->data(patchy::ui::kNewDocumentPresetIdRole).toString() != QStringLiteral("screen-720p")) {
          continue;
        }
        card_seen = true;
        const auto center = presets->visualItemRect(item).center();
        auto* viewport = presets->viewport();
        send_mouse(*viewport, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
        send_mouse(*viewport, QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton);
        send_mouse(*viewport, QEvent::MouseButtonDblClick, center, Qt::LeftButton, Qt::LeftButton);
        send_mouse(*viewport, QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton);
        return;
      }
    }
  });
  require_action_by_text(window, QStringLiteral("New"))->trigger();
  QApplication::processEvents();
  CHECK(card_seen);
  CHECK(tabs->count() == 2);
  CHECK(info->text().contains(QStringLiteral("1280 x 720 px")));
}

void ui_new_document_dialog_remembers_last_settings() {
  QApplication::clipboard()->clear();
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("newDocument"));
  }

  auto&& [window, canvas] = ShownWindowFixture{};
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  auto* info = window.findChild<QLabel*>(QStringLiteral("documentInfoLabel"));
  CHECK(tabs != nullptr);
  CHECK(info != nullptr);

  const auto find_card = [](QListWidget* presets, const QString& id) -> QListWidgetItem* {
    for (int row = 0; row < presets->count(); ++row) {
      if (presets->item(row)->data(patchy::ui::kNewDocumentPresetIdRole).toString() == id) {
        return presets->item(row);
      }
    }
    return nullptr;
  };

  // Accept the 1080p card with a black background.
  QTimer::singleShot(0, [find_card] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyNewDocumentDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      auto* presets = dialog->findChild<QListWidget*>(QStringLiteral("newDocumentPresetList"));
      auto* background = dialog->findChild<QComboBox*>(QStringLiteral("newDocumentBackgroundCombo"));
      CHECK(presets != nullptr);
      CHECK(background != nullptr);
      auto* card = find_card(presets, QStringLiteral("screen-1080p"));
      CHECK(card != nullptr);
      presets->setCurrentItem(card);
      QApplication::processEvents();
      const auto black_index = background->findText(QStringLiteral("Black"));
      CHECK(black_index > 0);
      background->setCurrentIndex(black_index);
      QApplication::processEvents();
      dialog->accept();
      return;
    }
    CHECK(false);
  });
  require_action(window, "fileNewAction")->trigger();
  QApplication::processEvents();
  CHECK(tabs->count() == 2);
  CHECK(info->text().contains(QStringLiteral("1920 x 1080 px")));
  CHECK(info->text().contains(QStringLiteral("72 ppi")));

  // Reopening, even with an image on the clipboard, selects the Last Used card (not
  // the 1080p card its values match) and restores the remembered background. Clicking
  // it after other edits brings everything back.
  QImage clipboard_image(64, 32, QImage::Format_RGBA8888);
  clipboard_image.fill(QColor(200, 40, 90));
  QApplication::clipboard()->setImage(clipboard_image);
  QApplication::processEvents();
  QTimer::singleShot(0, [find_card] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyNewDocumentDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      auto* presets = dialog->findChild<QListWidget*>(QStringLiteral("newDocumentPresetList"));
      auto* recent = dialog->findChild<QListWidget*>(QStringLiteral("newDocumentRecentList"));
      auto* background = dialog->findChild<QComboBox*>(QStringLiteral("newDocumentBackgroundCombo"));
      auto* width = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("newDocumentWidthSpin"));
      auto* resolution = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("newDocumentResolutionSpin"));
      auto* artboards = dialog->findChild<QCheckBox*>(QStringLiteral("newDocumentArtboardsCheck"));
      CHECK(presets != nullptr);
      CHECK(recent != nullptr);
      CHECK(background != nullptr);
      CHECK(width != nullptr);
      CHECK(resolution != nullptr);
      CHECK(artboards != nullptr);
      CHECK(recent->currentItem() != nullptr);
      CHECK(recent->currentItem()->data(patchy::ui::kNewDocumentPresetIdRole).toString() ==
            QStringLiteral("last-used"));
      CHECK(recent->currentItem() == recent->item(1));
      CHECK(width->value() == 1920);
      CHECK(std::abs(resolution->value() - 72.0) < 0.01);
      CHECK(background->currentText() == QStringLiteral("Black"));
      const bool remembered_artboards = artboards->isChecked();

      auto* default_card = find_card(presets, QStringLiteral("screen-1024x768"));
      CHECK(default_card != nullptr);
      presets->setCurrentItem(default_card);
      background->setCurrentIndex(background->findText(QStringLiteral("White")));
      artboards->setChecked(!remembered_artboards);
      QApplication::processEvents();
      CHECK(width->value() == 1024);
      recent->setCurrentItem(find_card(recent, QStringLiteral("last-used")));
      QApplication::processEvents();
      CHECK(width->value() == 1920);
      CHECK(background->currentText() == QStringLiteral("Black"));
      CHECK(artboards->isChecked() == remembered_artboards);
      dialog->reject();
      return;
    }
    CHECK(false);
  });
  require_action(window, "fileNewAction")->trigger();
  QApplication::processEvents();
  CHECK(tabs->count() == 2);
  QApplication::clipboard()->clear();
}

// Issue 53: the W/H unit and the resolution unit persist with the other New
// Document settings; a first run (no stored unit) seeds the W/H unit from the
// ruler unit, and a stored token the combo cannot show falls back to Pixels.
namespace new_document_templates {

QStringList card_ids(const QListWidget& presets) {
  QStringList ids;
  for (int row = 0; row < presets.count(); ++row) {
    ids << presets.item(row)->data(patchy::ui::kNewDocumentPresetIdRole).toString();
  }
  return ids;
}

QListWidgetItem* card(const QListWidget& presets, const QString& id) {
  for (int row = 0; row < presets.count(); ++row) {
    if (presets.item(row)->data(patchy::ui::kNewDocumentPresetIdRole).toString() == id) {
      return presets.item(row);
    }
  }
  return nullptr;
}

void clear_remembered_settings() {
  auto settings = patchy::ui::app_settings();
  settings.remove(QStringLiteral("newDocument"));
}

}  // namespace new_document_templates

// The template groups (GitHub issue 222): every group's cards, in order, with the first
// templates keeping their ids in their new groups; each group fits the grid without a
// scroll bar, and the artboard size list carries the same templates.
void ui_new_document_template_groups_hold_every_template() {
  using new_document_templates::card;
  using new_document_templates::card_ids;
  SettingsValueRestorer last_category(QStringLiteral("newDocument/lastCategory"));
  QApplication::clipboard()->clear();
  new_document_templates::clear_remembered_settings();
  auto&& [window, canvas] = ShownWindowFixture{};

  struct Group {
    const char* chip;
    const char* label;
    QStringList ids;
  };
  const std::vector<Group> groups = {
      {"newDocumentScreenChip", "Screen",
       {"screen-1024x768", "screen-720p", "screen-1080p", "screen-1440p", "screen-4k", "screen-8k",
        "screen-ultrawide-3440", "screen-dci-4k", "screen-laptop-1366", "screen-square-2048"}},
      {"newDocumentMobileChip", "Mobile",
       {"mobile-iphone-16", "mobile-iphone-16-pro", "mobile-iphone-16-pro-max", "mobile-iphone-se",
        "mobile-android-hd-720x1280", "mobile-android-fhd-1080x1920", "mobile-android-1080x2400",
        "mobile-android-wqhd-1440x2560",
        "mobile-android-1440x3120", "mobile-ipad-11", "mobile-ipad-pro-13", "mobile-android-tablet-1600x2560"}},
      {"newDocumentSocialChip", "Social",
       {"social-square-1080", "social-portrait-1080x1350", "phone-story-1080x1920", "social-youtube-thumbnail",
        "social-youtube-banner", "social-facebook-cover", "social-x-header", "social-x-post",
        "social-linkedin-banner", "social-pinterest-pin", "social-profile-picture"}},
      {"newDocumentWebChip", "Web",
       {"web-page-1440x900", "web-link-preview", "web-email-header", "web-ad-medium-rectangle",
        "web-ad-large-rectangle", "web-ad-leaderboard", "web-ad-billboard", "web-ad-half-page",
        "web-ad-wide-skyscraper", "web-ad-mobile-banner"}},
      {"newDocumentPrintChip", "Print",
       {"print-a6", "print-a5", "print-a4", "print-a3", "print-a2", "print-us-letter", "print-us-legal",
        "print-tabloid", "print-business-card-us", "print-business-card-eu", "print-poster-18x24"}},
      {"newDocumentPhotoChip", "Photo",
       {"photo-print-4x6", "print-5x7", "print-8x10", "photo-print-11x14", "photo-print-8x8",
        "photo-print-10x15cm", "photo-print-13x18cm", "photo-3x2-3000"}},
      {"newDocumentIconsChip", "Icons & Games",
       {"icon-app-1024", "icon-store-512", "icon-desktop-256", "icon-favicon-32", "art-pixel-16", "art-pixel-32",
        "art-pixel-64", "art-texture-1024", "art-texture-2048", "art-texture-4096", "game-store-header"}},
  };
  struct Spot {
    const char* id;
    QSize size;
    double ppi;
  };
  const std::vector<Spot> spots = {
      {"screen-1440p", QSize(2560, 1440), 72.0},       {"mobile-iphone-16", QSize(1179, 2556), 72.0},
      {"social-youtube-thumbnail", QSize(1280, 720), 72.0}, {"web-ad-medium-rectangle", QSize(300, 250), 72.0},
      {"print-a6", QSize(1240, 1748), 300.0},           {"print-business-card-us", QSize(1050, 600), 300.0},
      {"photo-print-4x6", QSize(1200, 1800), 300.0},    {"icon-favicon-32", QSize(32, 32), 72.0},
  };

  bool inspected = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyNewDocumentDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    // Review capture: the dialog as it opens.
    save_widget_artifact("issue_222_new_document_dialog", *dialog);
    auto* presets = dialog->findChild<QListWidget*>(QStringLiteral("newDocumentPresetList"));
    auto* width = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("newDocumentWidthSpin"));
    auto* height = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("newDocumentHeightSpin"));
    auto* resolution = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("newDocumentResolutionSpin"));
    CHECK(presets != nullptr && width != nullptr && height != nullptr && resolution != nullptr);
    if (presets == nullptr || width == nullptr || height == nullptr || resolution == nullptr) {
      dialog->reject();
      return;
    }
    QSet<QString> seen;
    for (const auto& group : groups) {
      auto* chip = dialog->findChild<QToolButton*>(QLatin1String(group.chip));
      CHECK(chip != nullptr);
      if (chip == nullptr) {
        continue;
      }
      CHECK(QString(chip->text()).replace(QStringLiteral("&&"), QStringLiteral("&")) == QLatin1String(group.label));
      chip->click();
      QApplication::processEvents();
      CHECK(chip->isChecked());
      CHECK(card_ids(*presets) == group.ids);
      CHECK(presets->count() <= 12);
      CHECK(!presets->verticalScrollBar()->isVisible());
      for (const auto& id : group.ids) {
        CHECK(!seen.contains(id));
        seen.insert(id);
      }
      for (const auto& spot : spots) {
        if (auto* item = card(*presets, QLatin1String(spot.id)); item != nullptr) {
          presets->setCurrentItem(item);
          QApplication::processEvents();
          CHECK(width->value() == spot.size.width());
          CHECK(height->value() == spot.size.height());
          CHECK(std::abs(resolution->value() - spot.ppi) < 0.01);
        }
      }
      if (QLatin1String(group.chip) == QLatin1String("newDocumentSocialChip")) {
        save_widget_artifact("issue_222_social_group", *dialog);
      }
    }
    inspected = true;
    dialog->reject();
  });
  require_action_by_text(window, QStringLiteral("New"))->trigger();
  QApplication::processEvents();
  CHECK(inspected);

  // The artboard sizes are every template but Clipboard, named and grouped like the cards.
  const auto artboard_presets = patchy::ui::artboard_size_presets();
  int template_count = 0;
  for (const auto& group : groups) {
    template_count += static_cast<int>(group.ids.size()) - (group.ids.contains(QStringLiteral("clipboard")) ? 1 : 0);
  }
  CHECK(static_cast<int>(artboard_presets.size()) == template_count);
  CHECK(std::any_of(artboard_presets.begin(), artboard_presets.end(), [](const auto& preset) {
    return preset.name == QStringLiteral("YouTube Thumbnail") && preset.size == QSize(1280, 720) &&
           preset.group == QStringLiteral("Social");
  }));
}

// The search field (GitHub issue 223) searches every group at once: typing hides the
// group chips' state, Clipboard and Last Used, lists only the matches, selects the first
// one so Return creates it, and Escape or a chip brings back the groups.
void ui_new_document_search_filters_every_group() {
  using new_document_templates::card_ids;
  SettingsValueRestorer last_category(QStringLiteral("newDocument/lastCategory"));
  new_document_templates::clear_remembered_settings();
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("newDocument/lastWidth"), 640);
    settings.setValue(QStringLiteral("newDocument/lastHeight"), 480);
  }
  QImage clipboard_image(40, 30, QImage::Format_RGBA8888);
  clipboard_image.fill(QColor(200, 60, 40));
  QApplication::clipboard()->setImage(clipboard_image);
  QApplication::processEvents();

  auto&& [window, canvas] = ShownWindowFixture{};
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  CHECK(tabs != nullptr);
  const auto tabs_before = tabs != nullptr ? tabs->count() : 0;

  bool inspected = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyNewDocumentDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    // Review capture: the dialog as it opens, before any search.
    save_widget_artifact("issue_223_new_document_dialog", *dialog);
    auto* search = dialog->findChild<QLineEdit*>(QStringLiteral("newDocumentSearchEdit"));
    auto* presets = dialog->findChild<QListWidget*>(QStringLiteral("newDocumentPresetList"));
    auto* no_matches = dialog->findChild<QLabel*>(QStringLiteral("newDocumentNoMatchesLabel"));
    auto* width = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("newDocumentWidthSpin"));
    auto* screen_chip = dialog->findChild<QToolButton*>(QStringLiteral("newDocumentScreenChip"));
    auto* photo_chip = dialog->findChild<QToolButton*>(QStringLiteral("newDocumentPhotoChip"));
    CHECK(search != nullptr && presets != nullptr && no_matches != nullptr && width != nullptr &&
          screen_chip != nullptr && photo_chip != nullptr);
    if (search == nullptr || presets == nullptr || no_matches == nullptr || width == nullptr ||
        screen_chip == nullptr || photo_chip == nullptr) {
      dialog->reject();
      return;
    }
    const auto any_chip_checked = [dialog] {
      for (auto* chip : dialog->findChildren<QToolButton*>()) {
        if (chip->property("newDocumentChip").toBool() && chip->isChecked()) {
          return true;
        }
      }
      return false;
    };
    const auto current_id = [presets] {
      return presets->currentItem() == nullptr
                 ? QString()
                 : presets->currentItem()->data(patchy::ui::kNewDocumentPresetIdRole).toString();
    };
    auto* recent = dialog->findChild<QListWidget*>(QStringLiteral("newDocumentRecentList"));
    CHECK(recent != nullptr);
    if (recent == nullptr) {
      dialog->reject();
      return;
    }
    const QStringList recent_ids = {QStringLiteral("clipboard"), QStringLiteral("last-used")};
    CHECK(card_ids(*recent) == recent_ids);
    CHECK(!card_ids(*presets).contains(QStringLiteral("clipboard")));

    // One word, matches in one group: no chip is current, the grid holds only the matches,
    // and the Recent row stays.
    search->setText(QStringLiteral("youtube"));
    QApplication::processEvents();
    CHECK(!any_chip_checked());
    CHECK(card_ids(*presets) ==
          QStringList({QStringLiteral("social-youtube-thumbnail"), QStringLiteral("social-youtube-banner")}));
    CHECK(current_id() == QStringLiteral("social-youtube-thumbnail"));
    CHECK(width->value() == 1280);
    CHECK(!no_matches->isVisible());

    // A size matches across groups, in group order.
    search->setText(QStringLiteral("1080"));
    QApplication::processEvents();
    const auto size_matches = card_ids(*presets);
    CHECK(!size_matches.contains(QStringLiteral("clipboard")));
    CHECK(!size_matches.contains(QStringLiteral("last-used")));
    CHECK(size_matches.indexOf(QStringLiteral("screen-1080p")) >= 0);
    CHECK(size_matches.indexOf(QStringLiteral("mobile-android-1080x2400")) >
          size_matches.indexOf(QStringLiteral("screen-1080p")));
    CHECK(size_matches.indexOf(QStringLiteral("phone-story-1080x1920")) >
          size_matches.indexOf(QStringLiteral("mobile-android-1080x2400")));
    save_widget_artifact("issue_223_search_results", *dialog);

    // Every word has to match; a group name counts.
    search->setText(QStringLiteral("print a4"));
    QApplication::processEvents();
    CHECK(card_ids(*presets) == QStringList({QStringLiteral("print-a4")}));
    search->setText(QStringLiteral("story 1080"));
    QApplication::processEvents();
    CHECK(card_ids(*presets) == QStringList({QStringLiteral("phone-story-1080x1920")}));
    // A word without a digit matches names and groups only: a bare "x" finds the X templates
    // and names holding an x, never every size (Seth, October 20, 2026).
    search->setText(QStringLiteral("x"));
    QApplication::processEvents();
    const auto x_matches = card_ids(*presets);
    CHECK(x_matches.contains(QStringLiteral("social-x-header")));
    CHECK(x_matches.contains(QStringLiteral("art-pixel-16")));
    for (const auto* unrelated : {"screen-1080p", "print-a4", "social-youtube-thumbnail", "mobile-iphone-16"}) {
      CHECK(!x_matches.contains(QLatin1String(unrelated)));
    }
    // A word with a digit also matches the size forms, either side of the x.
    search->setText(QStringLiteral("1920x"));
    QApplication::processEvents();
    CHECK(card_ids(*presets).contains(QStringLiteral("screen-1080p")));
    CHECK(!card_ids(*presets).contains(QStringLiteral("phone-story-1080x1920")));
    search->setText(QStringLiteral("x1080"));
    QApplication::processEvents();
    CHECK(card_ids(*presets).contains(QStringLiteral("screen-1080p")));
    CHECK(!card_ids(*presets).contains(QStringLiteral("phone-story-1080x1920")));
    search->setText(QStringLiteral("zzzz"));
    QApplication::processEvents();
    CHECK(presets->count() == 0);
    CHECK(no_matches->isVisible());

    // Up and Down move through the matches while the field keeps focus.
    search->setText(QStringLiteral("icon"));
    QApplication::processEvents();
    CHECK(presets->count() == 11);
    CHECK(presets->currentRow() == 0);
    send_key(*search, Qt::Key_Down);
    QApplication::processEvents();
    CHECK(presets->currentRow() > 0);
    send_key(*search, Qt::Key_Up);
    QApplication::processEvents();
    CHECK(presets->currentRow() == 0);

    // Escape clears the search, not the dialog: the Screen group is back, whole.
    send_key(*search, Qt::Key_Escape);
    QApplication::processEvents();
    CHECK(dialog->isVisible());
    CHECK(search->text().isEmpty());
    CHECK(screen_chip->isChecked());
    CHECK(card_ids(*recent) == recent_ids);
    CHECK(!no_matches->isVisible());

    // A chip picked during a search leaves the search for that group.
    search->setText(QStringLiteral("poster"));
    QApplication::processEvents();
    photo_chip->click();
    QApplication::processEvents();
    CHECK(search->text().isEmpty());
    CHECK(photo_chip->isChecked());
    CHECK(!screen_chip->isChecked());
    CHECK(card_ids(*presets).first() == QStringLiteral("photo-print-4x6"));
    CHECK(card_ids(*recent) == recent_ids);

    // Return creates the selected match.
    search->setFocus();
    search->setText(QStringLiteral("favicon"));
    QApplication::processEvents();
    CHECK(current_id() == QStringLiteral("icon-favicon-32"));
    inspected = true;
    send_key(*search, Qt::Key_Return);
  });
  require_action_by_text(window, QStringLiteral("New"))->trigger();
  QApplication::processEvents();
  CHECK(inspected);
  CHECK(tabs != nullptr && tabs->count() == tabs_before + 1);
  const auto& document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  CHECK(document.width() == 32 && document.height() == 32);
  QApplication::clipboard()->clear();
}

// The dialog opens on the group the user picked last (newDocument/lastCategory), after a
// cancel or a create alike; an unknown saved group falls back to the selected card's group.
void ui_new_document_reopens_on_the_last_picked_group() {
  SettingsValueRestorer last_category(QStringLiteral("newDocument/lastCategory"));
  QApplication::clipboard()->clear();
  new_document_templates::clear_remembered_settings();
  auto&& [window, canvas] = ShownWindowFixture{};
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  CHECK(tabs != nullptr);

  // Opens the dialog, hands it to `drive`, and returns the chip that was checked on open.
  const auto run_dialog = [&window](const std::function<void(QDialog&)>& drive) {
    QString opened_on;
    QTimer::singleShot(0, [&] {
      auto* dialog = find_top_level_dialog(QStringLiteral("patchyNewDocumentDialog"));
      CHECK(dialog != nullptr);
      if (dialog == nullptr) {
        return;
      }
      for (auto* chip : dialog->findChildren<QToolButton*>()) {
        if (chip->property("newDocumentChip").toBool() && chip->isChecked()) {
          opened_on = chip->objectName();
        }
      }
      drive(*dialog);
    });
    require_action_by_text(window, QStringLiteral("New"))->trigger();
    QApplication::processEvents();
    return opened_on;
  };
  const auto pick = [](QDialog& dialog, const char* chip_name) {
    auto* chip = dialog.findChild<QToolButton*>(QLatin1String(chip_name));
    CHECK(chip != nullptr);
    if (chip != nullptr) {
      chip->click();
      QApplication::processEvents();
    }
  };

  // Fresh settings open on Screen. Pick Print and cancel: Print opens next.
  CHECK(run_dialog([&](QDialog& dialog) {
          pick(dialog, "newDocumentPrintChip");
          dialog.reject();
        }) == QStringLiteral("newDocumentScreenChip"));
  CHECK(patchy::ui::app_settings().value(QStringLiteral("newDocument/lastCategory")).toString() ==
        QStringLiteral("print"));
  const auto tabs_before = tabs != nullptr ? tabs->count() : 0;
  CHECK(run_dialog([&](QDialog& dialog) {
          auto* presets = dialog.findChild<QListWidget*>(QStringLiteral("newDocumentPresetList"));
          CHECK(presets != nullptr && new_document_templates::card(*presets, QStringLiteral("print-a4")) != nullptr);
          // Pick Web and create: Web opens next, on the Last Used card the create made.
          pick(dialog, "newDocumentWebChip");
          dialog.accept();
        }) == QStringLiteral("newDocumentPrintChip"));
  CHECK(tabs != nullptr && tabs->count() == tabs_before + 1);
  CHECK(run_dialog([&](QDialog& dialog) {
          auto* recent = dialog.findChild<QListWidget*>(QStringLiteral("newDocumentRecentList"));
          CHECK(recent != nullptr && recent->currentItem() != nullptr &&
                recent->currentItem()->data(patchy::ui::kNewDocumentPresetIdRole).toString() ==
                    QStringLiteral("last-used"));
          dialog.reject();
        }) == QStringLiteral("newDocumentWebChip"));

  // A saved group this build does not know falls back to the Last Used card's group.
  patchy::ui::app_settings().setValue(QStringLiteral("newDocument/lastCategory"), QStringLiteral("no-such-group"));
  CHECK(run_dialog([](QDialog& dialog) { dialog.reject(); }) == QStringLiteral("newDocumentScreenChip"));
}

// Review captures of the New Document groups, search and remembered group (GitHub issue
// 222), each checked for what it is meant to show: Print picked, "x" matching names and
// groups only, "1080" matching sizes, and a reopen on the group picked before a cancel.
void ui_new_document_review_captures() {
  using new_document_templates::card_ids;
  SettingsValueRestorer last_category(QStringLiteral("newDocument/lastCategory"));
  QApplication::clipboard()->clear();
  new_document_templates::clear_remembered_settings();
  auto&& [window, canvas] = ShownWindowFixture{};

  bool first_done = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyNewDocumentDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* presets = dialog->findChild<QListWidget*>(QStringLiteral("newDocumentPresetList"));
    auto* search = dialog->findChild<QLineEdit*>(QStringLiteral("newDocumentSearchEdit"));
    auto* print_chip = dialog->findChild<QToolButton*>(QStringLiteral("newDocumentPrintChip"));
    auto* web_chip = dialog->findChild<QToolButton*>(QStringLiteral("newDocumentWebChip"));
    CHECK(presets != nullptr && search != nullptr && print_chip != nullptr && web_chip != nullptr);
    if (presets == nullptr || search == nullptr || print_chip == nullptr || web_chip == nullptr) {
      dialog->reject();
      return;
    }
    print_chip->click();
    QApplication::processEvents();
    CHECK(print_chip->isChecked());
    CHECK(card_ids(*presets).contains(QStringLiteral("print-a4")));
    save_widget_artifact("issue_222_after_groups", *dialog);

    search->setText(QStringLiteral("x"));
    QApplication::processEvents();
    const auto x_matches = card_ids(*presets);
    CHECK(x_matches.contains(QStringLiteral("social-x-header")));
    CHECK(!x_matches.contains(QStringLiteral("screen-1080p")));
    save_widget_artifact("issue_222_after_search_x", *dialog);

    search->setText(QStringLiteral("1080"));
    QApplication::processEvents();
    CHECK(card_ids(*presets).contains(QStringLiteral("screen-1080p")));
    CHECK(card_ids(*presets).contains(QStringLiteral("phone-story-1080x1920")));
    save_widget_artifact("issue_222_after_search_1080", *dialog);

    search->clear();
    QApplication::processEvents();
    web_chip->click();
    QApplication::processEvents();
    first_done = true;
    dialog->reject();
  });
  require_action_by_text(window, QStringLiteral("New"))->trigger();
  QApplication::processEvents();
  CHECK(first_done);

  bool reopened = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyNewDocumentDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* web_chip = dialog->findChild<QToolButton*>(QStringLiteral("newDocumentWebChip"));
    CHECK(web_chip != nullptr && web_chip->isChecked());
    save_widget_artifact("issue_222_after_last_category", *dialog);
    reopened = true;
    dialog->reject();
  });
  require_action_by_text(window, QStringLiteral("New"))->trigger();
  QApplication::processEvents();
  CHECK(reopened);
}

// New Document opens centered on the main window every time (Seth, October 2026): a spot
// it was dragged to is neither saved nor restored, so it never reopens in a far corner.
void ui_new_document_dialog_always_opens_centered() {
  new_document_templates::clear_remembered_settings();
  const auto group = QStringLiteral("dialogPositions/patchyNewDocumentDialog");
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(group);
  }
  auto&& [window, canvas] = ShownWindowFixture{};
  const auto* screen = window.screen();
  CHECK(screen != nullptr);
  const auto available = screen != nullptr ? screen->availableGeometry() : QRect(0, 0, 1280, 800);
  const auto corner = available.topLeft() + QPoint(3, 4);

  // Where a centered dialog of this frame size lands: over the window's center, kept inside
  // the screen's available area.
  const auto centered_position = [&window, available](const QDialog& dialog) {
    const auto frame = dialog.frameGeometry().size();
    auto x = window.frameGeometry().center().x() - frame.width() / 2;
    auto y = window.frameGeometry().center().y() - frame.height() / 2;
    x = std::clamp(x, available.left(), std::max(available.left(), available.right() + 1 - frame.width()));
    y = std::clamp(y, available.top(), std::max(available.top(), available.bottom() + 1 - frame.height()));
    return QPoint(x, y);
  };

  // First open: centered; drag it to the corner and close it.
  bool moved = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyNewDocumentDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    QApplication::processEvents();
    CHECK((dialog->frameGeometry().topLeft() - centered_position(*dialog)).manhattanLength() <= 12);
    dialog->move(corner);
    QApplication::processEvents();
    CHECK((dialog->pos() - corner).manhattanLength() <= 12);
    moved = true;
    dialog->reject();
  });
  require_action_by_text(window, QStringLiteral("New"))->trigger();
  QApplication::processEvents();
  CHECK(moved);
  {
    auto settings = patchy::ui::app_settings();
    CHECK(!settings.value(group + QStringLiteral("/moved"), false).toBool());
  }

  // Reopened: centered on the window again, not in the corner.
  bool reopened = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyNewDocumentDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    QApplication::processEvents();
    CHECK((dialog->pos() - corner).manhattanLength() > 40);
    CHECK((dialog->frameGeometry().topLeft() - centered_position(*dialog)).manhattanLength() <= 12);
    // Review capture: the window with the dialog drawn where it opened.
    auto shot = window.grab().toImage();
    QPainter painter(&shot);
    painter.drawImage(dialog->pos() - window.pos(), dialog->grab().toImage());
    painter.end();
    save_image_artifact("newdoc_centered_after_reopen", shot);
    reopened = true;
    dialog->reject();
  });
  require_action_by_text(window, QStringLiteral("New"))->trigger();
  QApplication::processEvents();
  CHECK(reopened);
}

namespace new_document_recent {

// Opens New Document, hands the dialog to `drive` (which accepts or rejects it).
void run_dialog(patchy::ui::MainWindow& window, const std::function<void(QDialog&)>& drive) {
  bool driven = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyNewDocumentDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    QApplication::processEvents();
    drive(*dialog);
    driven = true;
    // A dialog `drive` failed to close would block the suite: fail and close it.
    QTimer::singleShot(2000, [guarded = QPointer<QDialog>(dialog)] {
      if (guarded != nullptr && guarded->isVisible()) {
        CHECK(false);
        guarded->reject();
      }
    });
  });
  require_action_by_text(window, QStringLiteral("New"))->trigger();
  QApplication::processEvents();
  CHECK(driven);
}

QListWidget* list(QDialog& dialog, const char* name) {
  auto* found = dialog.findChild<QListWidget*>(QLatin1String(name));
  CHECK(found != nullptr);
  return found;
}

// Creates a document from the template `id` (a group's card), on the group `chip`.
void create_from_template(patchy::ui::MainWindow& window, const char* chip, const char* id) {
  run_dialog(window, [chip, id](QDialog& dialog) {
    if (auto* group = dialog.findChild<QToolButton*>(QLatin1String(chip)); group != nullptr) {
      group->click();
      QApplication::processEvents();
    }
    auto* presets = list(dialog, "newDocumentPresetList");
    auto* card = presets != nullptr ? new_document_templates::card(*presets, QLatin1String(id)) : nullptr;
    CHECK(card != nullptr);
    if (card == nullptr) {
      dialog.reject();
      return;
    }
    presets->setCurrentItem(card);
    QApplication::processEvents();
    dialog.accept();
  });
}

QSize card_size(const QListWidget& cards, int row) {
  return cards.item(row)->data(patchy::ui::kNewDocumentPresetSizeRole).toSize();
}

}  // namespace new_document_recent

// The Recent row (Seth, October 2026): the documents created here, newest first and
// without repeats, pinned above every group's grid and through a search; a Recent card
// restores its create and creates like a template.
void ui_new_document_recent_row_lists_creates_newest_first() {
  using new_document_recent::card_size;
  using new_document_recent::list;
  using new_document_templates::card_ids;
  SettingsValueRestorer last_category(QStringLiteral("newDocument/lastCategory"));
  QApplication::clipboard()->clear();
  new_document_templates::clear_remembered_settings();
  auto&& [window, canvas] = ShownWindowFixture{};

  new_document_recent::create_from_template(window, "newDocumentScreenChip", "screen-1080p");
  new_document_recent::create_from_template(window, "newDocumentScreenChip", "screen-720p");
  new_document_recent::create_from_template(window, "newDocumentScreenChip", "screen-1080p");
  new_document_recent::create_from_template(window, "newDocumentPrintChip", "print-a4");
  const auto stored =
      QJsonDocument::fromJson(patchy::ui::app_settings().value(QStringLiteral("newDocument/recent")).toString().toUtf8())
          .array();
  CHECK(stored.size() == 3);

  new_document_recent::run_dialog(window, [&](QDialog& dialog) {
    auto* recent = list(dialog, "newDocumentRecentList");
    auto* presets = list(dialog, "newDocumentPresetList");
    auto* label = dialog.findChild<QLabel*>(QStringLiteral("newDocumentRecentLabel"));
    auto* search = dialog.findChild<QLineEdit*>(QStringLiteral("newDocumentSearchEdit"));
    auto* width = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("newDocumentWidthSpin"));
    CHECK(recent != nullptr && presets != nullptr && label != nullptr && search != nullptr && width != nullptr);
    if (recent == nullptr || presets == nullptr || label == nullptr || search == nullptr || width == nullptr) {
      dialog.reject();
      return;
    }
    CHECK(label->isVisible() && label->text() == QStringLiteral("Recent"));
    const QStringList ids = {QStringLiteral("last-used"), QStringLiteral("recent-1"), QStringLiteral("recent-2")};
    CHECK(card_ids(*recent) == ids);
    CHECK(card_size(*recent, 0) == QSize(2480, 3508));
    CHECK(card_size(*recent, 1) == QSize(1920, 1080));
    CHECK(card_size(*recent, 2) == QSize(1280, 720));
    CHECK(recent->item(0)->text() == QStringLiteral("Last Used"));
    CHECK(recent->item(1)->text() == QStringLiteral("1080p"));
    CHECK(recent->currentItem() == recent->item(0));
    CHECK(std::abs(width->value() - 2480.0) < 0.5);
    // Every group, and a search, keep the row; the grids never hold Recent cards.
    for (auto* chip : dialog.findChildren<QToolButton*>()) {
      if (!chip->property("newDocumentChip").toBool()) {
        continue;
      }
      chip->click();
      QApplication::processEvents();
      CHECK(recent->isVisible());
      CHECK(card_ids(*recent) == ids);
      CHECK(!card_ids(*presets).contains(QStringLiteral("last-used")));
    }
    search->setText(QStringLiteral("youtube"));
    QApplication::processEvents();
    CHECK(recent->isVisible());
    CHECK(card_ids(*recent) == ids);
    // Picking a Recent card moves the selection out of the search results.
    recent->setCurrentItem(recent->item(2));
    QApplication::processEvents();
    CHECK(presets->selectedItems().isEmpty());
    CHECK(std::abs(width->value() - 1280.0) < 0.5);
    // A double-click creates it, like a template.
    // A view acts on a double-click only after a press on the same card.
    const auto center = recent->visualItemRect(recent->item(2)).center();
    send_mouse(*recent->viewport(), QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*recent->viewport(), QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton);
    send_mouse(*recent->viewport(), QEvent::MouseButtonDblClick, center, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*recent->viewport(), QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton);
  });
  CHECK(patchy::ui::MainWindowTestAccess::document(window).width() == 1280);

  // The create moved 720p to the front.
  new_document_recent::run_dialog(window, [&](QDialog& dialog) {
    auto* recent = list(dialog, "newDocumentRecentList");
    CHECK(recent != nullptr && recent->count() == 3);
    if (recent != nullptr && recent->count() == 3) {
      CHECK(card_size(*recent, 0) == QSize(1280, 720));
      CHECK(card_size(*recent, 1) == QSize(2480, 3508));
      CHECK(card_size(*recent, 2) == QSize(1920, 1080));
    }
    dialog.reject();
  });
}

// The Clipboard card leads the Recent row while the clipboard holds an image, appearing
// and disappearing as the clipboard changes, without being preselected.
void ui_new_document_recent_row_follows_the_clipboard() {
  using new_document_recent::list;
  using new_document_templates::card_ids;
  SettingsValueRestorer last_category(QStringLiteral("newDocument/lastCategory"));
  QApplication::clipboard()->clear();
  new_document_templates::clear_remembered_settings();
  auto&& [window, canvas] = ShownWindowFixture{};
  new_document_recent::create_from_template(window, "newDocumentScreenChip", "screen-1080p");

  new_document_recent::run_dialog(window, [&](QDialog& dialog) {
    auto* recent = list(dialog, "newDocumentRecentList");
    auto* width = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("newDocumentWidthSpin"));
    CHECK(recent != nullptr && width != nullptr);
    if (recent == nullptr || width == nullptr) {
      dialog.reject();
      return;
    }
    CHECK(card_ids(*recent) == QStringList({QStringLiteral("last-used")}));
    QImage image(90, 40, QImage::Format_RGBA8888);
    image.fill(QColor(30, 160, 220));
    QApplication::clipboard()->setImage(image);
    QApplication::processEvents();
    CHECK(card_ids(*recent) == QStringList({QStringLiteral("clipboard"), QStringLiteral("last-used")}));
    CHECK(recent->currentItem() == recent->item(1));
    recent->setCurrentItem(recent->item(0));
    QApplication::processEvents();
    CHECK(width->value() == 90);
    CHECK(!width->isEnabled());
    QApplication::clipboard()->clear();
    QApplication::processEvents();
    CHECK(card_ids(*recent) == QStringList({QStringLiteral("last-used")}));
    CHECK(width->isEnabled());
    dialog.reject();
  });
}

// The Recent row shows as many cards as one row holds at the dialog's width, recomputed
// as the dialog resizes, never wrapping or scrolling.
void ui_new_document_recent_row_fits_the_dialog_width() {
  using new_document_recent::list;
  SettingsValueRestorer last_category(QStringLiteral("newDocument/lastCategory"));
  QApplication::clipboard()->clear();
  new_document_templates::clear_remembered_settings();
  QJsonArray entries;
  for (int index = 0; index < 8; ++index) {
    QJsonObject entry;
    entry.insert(QStringLiteral("w"), 100 + index);
    entry.insert(QStringLiteral("h"), 80);
    entry.insert(QStringLiteral("ppi"), 72.0);
    entries.append(entry);
  }
  patchy::ui::app_settings().setValue(QStringLiteral("newDocument/recent"),
                                      QString::fromUtf8(QJsonDocument(entries).toJson(QJsonDocument::Compact)));
  auto&& [window, canvas] = ShownWindowFixture{};
  new_document_recent::run_dialog(window, [&](QDialog& dialog) {
    auto* recent = list(dialog, "newDocumentRecentList");
    CHECK(recent != nullptr);
    if (recent == nullptr) {
      dialog.reject();
      return;
    }
    const auto expected_count = [recent] {
      return std::clamp(recent->viewport()->width() / recent->gridSize().width(), 1, 8);
    };
    CHECK(recent->count() == expected_count());
    CHECK(!recent->horizontalScrollBar()->isVisible());
    const auto narrow = recent->count();
    dialog.resize(dialog.width() + 3 * recent->gridSize().width(), dialog.height());
    QApplication::processEvents();
    QApplication::processEvents();
    CHECK(recent->count() == expected_count());
    CHECK(recent->count() > narrow);
    CHECK(recent->item(0)->data(patchy::ui::kNewDocumentPresetSizeRole).toSize() == QSize(100, 80));
    dialog.reject();
  });
}

// Before the Recent row's key exists, the old Last Used settings seed its first card.
void ui_new_document_recent_row_seeds_from_old_last_used_settings() {
  using new_document_recent::list;
  SettingsValueRestorer last_category(QStringLiteral("newDocument/lastCategory"));
  QApplication::clipboard()->clear();
  new_document_templates::clear_remembered_settings();
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("newDocument/lastWidth"), 640);
    settings.setValue(QStringLiteral("newDocument/lastHeight"), 480);
    settings.setValue(QStringLiteral("newDocument/lastPpi"), 150.0);
    settings.setValue(QStringLiteral("newDocument/lastBackground"), QColor(Qt::black));
  }
  auto&& [window, canvas] = ShownWindowFixture{};
  new_document_recent::run_dialog(window, [&](QDialog& dialog) {
    auto* recent = list(dialog, "newDocumentRecentList");
    auto* width = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("newDocumentWidthSpin"));
    auto* resolution = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("newDocumentResolutionSpin"));
    auto* background = dialog.findChild<QComboBox*>(QStringLiteral("newDocumentBackgroundCombo"));
    CHECK(recent != nullptr && width != nullptr && resolution != nullptr && background != nullptr);
    if (recent == nullptr || width == nullptr || resolution == nullptr || background == nullptr) {
      dialog.reject();
      return;
    }
    CHECK(new_document_templates::card_ids(*recent) == QStringList({QStringLiteral("last-used")}));
    CHECK(recent->currentItem() == recent->item(0));
    CHECK(width->value() == 640);
    CHECK(std::abs(resolution->value() - 150.0) < 0.01);
    CHECK(background->currentText() == QStringLiteral("Black"));
    dialog.reject();
  });
  CHECK(!patchy::ui::app_settings().contains(QStringLiteral("newDocument/recent")));
}

// Review captures (dark theme): the Recent row on Print with an image on the clipboard,
// and through a search.
void ui_new_document_recent_row_review_captures() {
  using new_document_recent::list;
  SettingsValueRestorer last_category(QStringLiteral("newDocument/lastCategory"));
  QApplication::clipboard()->clear();
  new_document_templates::clear_remembered_settings();
  auto&& [window, canvas] = ShownWindowFixture{};
  new_document_recent::create_from_template(window, "newDocumentSocialChip", "social-square-1080");
  new_document_recent::create_from_template(window, "newDocumentScreenChip", "screen-1080p");
  QImage image(1200, 800, QImage::Format_RGBA8888);
  image.fill(QColor(220, 120, 40));
  QApplication::clipboard()->setImage(image);
  QApplication::processEvents();
  new_document_recent::run_dialog(window, [&](QDialog& dialog) {
    auto* print_chip = dialog.findChild<QToolButton*>(QStringLiteral("newDocumentPrintChip"));
    auto* search = dialog.findChild<QLineEdit*>(QStringLiteral("newDocumentSearchEdit"));
    auto* recent = list(dialog, "newDocumentRecentList");
    CHECK(print_chip != nullptr && search != nullptr && recent != nullptr);
    if (print_chip == nullptr || search == nullptr || recent == nullptr) {
      dialog.reject();
      return;
    }
    print_chip->click();
    QApplication::processEvents();
    CHECK(new_document_templates::card_ids(*recent).first() == QStringLiteral("clipboard"));
    save_widget_artifact("newdoc_recent_print_clipboard", dialog);
    search->setText(QStringLiteral("1080"));
    QApplication::processEvents();
    CHECK(recent->count() >= 3);
    save_widget_artifact("newdoc_recent_search", dialog);
    dialog.reject();
  });
  QApplication::clipboard()->clear();
}

// New Document fits short screens (GitHub issue 342): five card columns, the Recent row as
// the top section of the template list under one frame, a caption naming the group (or the
// search), and every group unscrolled (Mobile has the most, 12), in a dialog no taller than
// about 620 px at the default font.
void ui_new_document_dialog_fits_short_screens() {
  using new_document_recent::list;
  using new_document_templates::card_ids;
  SettingsValueRestorer last_category(QStringLiteral("newDocument/lastCategory"));
  QApplication::clipboard()->clear();
  new_document_templates::clear_remembered_settings();
  auto&& [window, canvas] = ShownWindowFixture{};
  new_document_recent::create_from_template(window, "newDocumentScreenChip", "screen-720p");
  QImage image(800, 600, QImage::Format_RGBA8888);
  image.fill(QColor(70, 120, 190));
  QApplication::clipboard()->setImage(image);
  QApplication::processEvents();
  new_document_recent::run_dialog(window, [&](QDialog& dialog) {
    auto* recent = list(dialog, "newDocumentRecentList");
    auto* presets = list(dialog, "newDocumentPresetList");
    auto* mobile_chip = dialog.findChild<QToolButton*>(QStringLiteral("newDocumentMobileChip"));
    auto* search = dialog.findChild<QLineEdit*>(QStringLiteral("newDocumentSearchEdit"));
    CHECK(recent != nullptr && presets != nullptr && mobile_chip != nullptr && search != nullptr);
    if (recent == nullptr || presets == nullptr || mobile_chip == nullptr || search == nullptr) {
      dialog.reject();
      return;
    }
    mobile_chip->click();
    QApplication::processEvents();
    save_widget_artifact("issue_342_new_document_mobile", dialog);
    CHECK(dialog.height() <= 620);
    CHECK(presets->viewport()->height() >= 3 * presets->gridSize().height());
    CHECK(presets->count() == 12);
    CHECK(!presets->verticalScrollBar()->isVisible());
    QSet<int> columns;
    for (int row = 0; row < presets->count(); ++row) {
      columns.insert(presets->visualItemRect(presets->item(row)).left());
    }
    CHECK(columns.size() == 5);
    CHECK(card_ids(*recent) == QStringList({QStringLiteral("clipboard"), QStringLiteral("last-used")}));

    // One frame holds Recent, the group caption and the group's cards.
    auto* frame = dialog.findChild<QWidget*>(QStringLiteral("newDocumentListFrame"));
    auto* group_label = dialog.findChild<QLabel*>(QStringLiteral("newDocumentGroupLabel"));
    CHECK(frame != nullptr && group_label != nullptr);
    if (frame == nullptr || group_label == nullptr) {
      dialog.reject();
      return;
    }
    CHECK(recent->parentWidget() == frame && presets->parentWidget() == frame);
    CHECK(group_label->isVisible() && group_label->text() == QStringLiteral("Mobile"));
    CHECK(recent->frameShape() == QFrame::NoFrame && presets->frameShape() == QFrame::NoFrame);

    // The chips sit on one row, and every group fits without a scroll bar.
    int chip_top = -1;
    bool one_row = true;
    for (auto* chip : dialog.findChildren<QToolButton*>()) {
      if (!chip->property("newDocumentChip").toBool()) {
        continue;
      }
      one_row = one_row && (chip_top < 0 || chip->y() == chip_top);
      chip_top = chip->y();
      chip->click();
      QApplication::processEvents();
      CHECK(!presets->verticalScrollBar()->isVisible());
      CHECK(group_label->text() == QString(chip->text()).replace(QStringLiteral("&&"), QStringLiteral("&")));
    }
    CHECK(one_row);

    // A search names its results; many matches scroll and keep five columns.
    search->setText(QStringLiteral("a"));
    QApplication::processEvents();
    CHECK(group_label->text() == QStringLiteral("Search results"));
    CHECK(presets->count() > 15);
    columns.clear();
    for (int row = 0; row < presets->count(); ++row) {
      columns.insert(presets->visualItemRect(presets->item(row)).left());
    }
    CHECK(columns.size() == 5);
    dialog.reject();
  });
  QApplication::clipboard()->clear();
}

void ui_new_document_dialog_remembers_unit() {
  QApplication::clipboard()->clear();
  SettingsValueRestorer restore_units(QStringLiteral("view/rulerUnits"));
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("newDocument"));
    settings.setValue(QStringLiteral("view/rulerUnits"), QStringLiteral("cm"));
  }

  auto&& [window, canvas] = ShownWindowFixture{};
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  CHECK(tabs != nullptr);

  struct DialogFields {
    QDialog* dialog{nullptr};
    QComboBox* unit{nullptr};
    QComboBox* resolution_unit{nullptr};
    QDoubleSpinBox* width{nullptr};
    QDoubleSpinBox* resolution{nullptr};
  };
  const auto with_dialog = [](std::function<void(const DialogFields&)> body) {
    QTimer::singleShot(0, [body = std::move(body)] {
      for (auto* widget : QApplication::topLevelWidgets()) {
        if (widget->objectName() != QStringLiteral("patchyNewDocumentDialog")) {
          continue;
        }
        DialogFields fields;
        fields.dialog = qobject_cast<QDialog*>(widget);
        fields.unit = fields.dialog->findChild<QComboBox*>(QStringLiteral("newDocumentUnitCombo"));
        fields.resolution_unit =
            fields.dialog->findChild<QComboBox*>(QStringLiteral("newDocumentResolutionUnitCombo"));
        fields.width = fields.dialog->findChild<QDoubleSpinBox*>(QStringLiteral("newDocumentWidthSpin"));
        fields.resolution =
            fields.dialog->findChild<QDoubleSpinBox*>(QStringLiteral("newDocumentResolutionSpin"));
        CHECK(fields.unit != nullptr);
        CHECK(fields.resolution_unit != nullptr);
        CHECK(fields.width != nullptr);
        CHECK(fields.resolution != nullptr);
        body(fields);
        return;
      }
      CHECK(false);
    });
  };

  // First run: the ruler unit (cm) seeds the combo and the width already reads
  // in it. Pick mm and Pixels/Centimeter, then accept.
  with_dialog([](const DialogFields& fields) {
    CHECK(fields.unit->currentText() == QStringLiteral("Centimeters"));
    CHECK(fields.resolution_unit->currentIndex() == 0);
    const auto ppi = fields.resolution->value();
    CHECK(ppi > 0.0);
    CHECK(std::abs(fields.width->value() - 1024.0 / ppi * 2.54) < 0.01);
    fields.unit->setCurrentIndex(fields.unit->findText(QStringLiteral("Millimeters")));
    fields.resolution_unit->setCurrentIndex(1);
    QApplication::processEvents();
    CHECK(std::abs(fields.width->value() - 1024.0 / ppi * 25.4) < 0.1);  // mm shows one decimal
    fields.dialog->accept();
  });
  require_action(window, "fileNewAction")->trigger();
  QApplication::processEvents();
  CHECK(tabs->count() == 2);
  CHECK(patchy::ui::MainWindowTestAccess::document(window).width() == 1024);
  {
    auto settings = patchy::ui::app_settings();
    CHECK(settings.value(QStringLiteral("newDocument/lastUnit")).toString() == QStringLiteral("mm"));
    CHECK(settings.value(QStringLiteral("newDocument/lastResolutionUnit")).toString() == QStringLiteral("cm"));
  }

  // Reopening restores both units, whatever the ruler unit says now.
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("view/rulerUnits"), QStringLiteral("in"));
  }
  with_dialog([](const DialogFields& fields) {
    CHECK(fields.unit->currentText() == QStringLiteral("Millimeters"));
    CHECK(fields.resolution_unit->currentIndex() == 1);
    const auto ppi = fields.resolution->value() * 2.54;  // shown as pixels/cm
    CHECK(std::abs(fields.width->value() - 1024.0 / ppi * 25.4) < 0.1);
    fields.dialog->reject();
  });
  require_action(window, "fileNewAction")->trigger();
  QApplication::processEvents();
  CHECK(tabs->count() == 2);

  // Points is a ruler unit but not a New Document unit: Pixels instead. (Without the Recent
  // row's own record the old keys seed it, unit included; each Recent card keeps its unit.)
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("newDocument/lastUnit"), QStringLiteral("pt"));
    settings.remove(QStringLiteral("newDocument/recent"));
  }
  with_dialog([](const DialogFields& fields) {
    CHECK(fields.unit->currentText() == QStringLiteral("Pixels"));
    CHECK(fields.width->value() == 1024);
    fields.dialog->reject();
  });
  require_action(window, "fileNewAction")->trigger();
  QApplication::processEvents();
  CHECK(tabs->count() == 2);
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("newDocument"));
  }
}

void ui_new_document_opens_fit_to_view() {
  patchy::ui::MainWindow window;
  show_window(window);

  // Each session owns its canvas: refetch after every create.
  accept_new_document_dialog(4000, 3000);
  require_action(window, "fileNewAction")->trigger();
  QApplication::processEvents();
  auto* large_canvas = require_canvas(window);
  // A canvas larger than the view opens zoomed out so it is fully visible.
  CHECK(large_canvas->zoom() < 1.0);
  CHECK(4000.0 * large_canvas->zoom() <= static_cast<double>(large_canvas->width()));
  CHECK(3000.0 * large_canvas->zoom() <= static_cast<double>(large_canvas->height()));

  // A canvas that already fits opens at 100%, never zoomed past it.
  accept_new_document_dialog(320, 200);
  require_action(window, "fileNewAction")->trigger();
  QApplication::processEvents();
  auto* small_canvas = require_canvas(window);
  CHECK(std::abs(small_canvas->zoom() - 1.0) < 0.0001);
}

void ui_new_document_background_starts_locked() {
  auto&& [window, canvas] = ShownWindowFixture{};
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(tabs != nullptr);
  CHECK(layer_list != nullptr);

  const auto require_locked_background = [layer_list] {
    auto* background_item = require_layer_item(*layer_list, QStringLiteral("Background"));
    auto* background_row = layer_list->itemWidget(background_item);
    CHECK(background_row != nullptr);
    // Position and transparency, as in Photoshop (GitHub issue 97): a Background, marked by
    // one padlock (GitHub issue 274).
    const auto badges = background_row->findChildren<QLabel*>(QStringLiteral("layerLockBadge"));
    CHECK(badges.size() == 1);
    CHECK(badges.size() == 1 && badges.front()->property("layerBackgroundBadge").toBool() &&
          badges.front()->toolTip().startsWith(QStringLiteral("Background")));
    CHECK(require_layer_item(*layer_list, QStringLiteral("Paint Layer"))->isSelected());
  };

  require_locked_background();

  accept_new_document_dialog(360, 240);
  require_action_by_text(window, QStringLiteral("New"))->trigger();
  QApplication::processEvents();
  CHECK(tabs->count() == 2);
  require_locked_background();
}

void ui_merge_down_into_position_locked_background_works() {
  auto&& [window, canvas] = ShownWindowFixture{};
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  CHECK(require_layer_item(*layer_list, QStringLiteral("Paint Layer"))->isSelected());

  canvas->set_primary_color(QColor(220, 30, 40));
  use_solid_fill_settings(canvas);
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(30, 30)), QColor(220, 30, 40), 8));

  require_action(window, "layerMergeDownAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->count() == 1);
  auto* background = require_layer_item(*layer_list, QStringLiteral("Background"));
  CHECK(background->isSelected());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(30, 30)), QColor(220, 30, 40), 8));
  auto* background_row = layer_list->itemWidget(background);
  CHECK(background_row != nullptr);
  // The merge keeps the Background's position and transparency locks (it is still the
  // Background, one padlock); neither blocks it.
  const auto badges = background_row->findChildren<QLabel*>(QStringLiteral("layerLockBadge"));
  CHECK(badges.size() == 1 && badges.front()->property("layerBackgroundBadge").toBool());
  save_widget_artifact("ui_merge_down_position_locked_background", window);
}

// GitHub issue 274: Photoshop's Background rules. The File > New Background (core
// background_layer) never leaves the bottom of the stack, and takes no layer styles.
void ui_background_layer_stays_at_bottom_and_takes_no_styles() {
  auto&& [window, canvas] = ShownWindowFixture{};
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto& doc = patchy::ui::MainWindowTestAccess::document(window);
  const auto bottom_id = [&] { return std::as_const(doc).layers().front().id(); };
  const auto background_id = bottom_id();
  CHECK(patchy::background_layer(std::as_const(doc).layers(), doc.width(), doc.height()) != nullptr);
  CHECK(std::as_const(doc).layers().size() == 2U);
  const auto paint_id = std::as_const(doc).layers().back().id();
  const auto undo_depth = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  const auto select_row = [&](const char* name) {
    auto* item = require_layer_item(*layer_list, QString::fromLatin1(name));
    layer_list->clearSelection();
    layer_list->setCurrentItem(item);
    item->setSelected(true);
    QApplication::processEvents();
  };

  // Moving another layer under it, or it up, is refused with a status message.
  select_row("Paint Layer");
  require_action_by_text(window, QStringLiteral("Move Layer Down"))->trigger();
  QApplication::processEvents();
  CHECK(bottom_id() == background_id);
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("stays at the bottom")));
  const auto background_rect =
      layer_list->visualItemRect(require_layer_item(*layer_list, QStringLiteral("Background")));
  send_layer_drop(*layer_list, QPoint(background_rect.center().x(), background_rect.bottom() - 2), {paint_id});
  CHECK(bottom_id() == background_id);
  select_row("Background");
  require_action_by_text(window, QStringLiteral("Move Layer Up"))->trigger();
  QApplication::processEvents();
  CHECK(bottom_id() == background_id);
  require_action(window, "layerNewFolderAction")->trigger();
  QApplication::processEvents();
  CHECK(bottom_id() == background_id);
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("cannot go inside a folder")));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_depth);

  // No layer styles: the footer button is off, Edit Layer Styles... refuses, and Paste
  // Layer Style skips it.
  auto* styles_button = window.findChild<QWidget*>(QStringLiteral("layerStylesButton"));
  CHECK(styles_button != nullptr && !styles_button->isEnabled());
  require_action(window, "layerBlendingOptionsAction")->trigger();
  QApplication::processEvents();
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("cannot have layer styles")));
  CHECK(std::as_const(doc).find_layer(background_id)->layer_style().empty());
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  doc.find_layer(paint_id)->layer_style().drop_shadows.push_back(shadow);
  select_row("Paint Layer");
  CHECK(styles_button->isEnabled());
  require_action(window, "layerCopyStyleAction")->trigger();
  select_row("Background");
  patchy::ui::MainWindowTestAccess::refresh_layer_ui(window);
  CHECK(!require_action(window, "layerPasteStyleAction")->isEnabled());
  CHECK(std::as_const(doc).find_layer(background_id)->layer_style().empty());
}

// GitHub issue 274: the Background's row shows one padlock that names it (Seth: no italic
// name), and its shorter right-click menu offers Layer from Background, which makes it an
// ordinary "Layer 0" in one undo step. Delete Layer and Merge Visible need another layer.
void ui_background_layer_marker_and_short_menu() {
  auto&& [window, canvas] = ShownWindowFixture{};
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* layers_dock = window.findChild<QWidget*>(QStringLiteral("layersDock"));
  CHECK(layer_list != nullptr && layers_dock != nullptr);
  for (const auto scheme : {patchy::ui::ColorSchemePreference::Dark, patchy::ui::ColorSchemePreference::Light}) {
    ColorSchemeRestorer restore(scheme);
    save_widget_artifact(scheme == patchy::ui::ColorSchemePreference::Dark ? "issue274_background_row_dark"
                                                                           : "issue274_background_row_light",
                         *layers_dock);
  }
  const auto background_badges = [&] {
    auto* row = layer_list->itemWidget(layer_list->item(layer_list->count() - 1));
    return row != nullptr ? row->findChildren<QLabel*>(QStringLiteral("layerLockBadge")) : QList<QLabel*>{};
  };
  auto badges = background_badges();
  CHECK(badges.size() == 1);
  CHECK(badges.size() == 1 && badges.front()->property("layerBackgroundBadge").toBool() &&
        badges.front()->toolTip().startsWith(QStringLiteral("Background")));

  // Right-clicks the bottom row and picks `pick`; returns the menu's entries, "-" for a
  // separator and "!" before a disabled one.
  const auto right_click_background = [&](const char* pick) {
    QStringList seen;
    QTimer::singleShot(0, [&seen, pick] {
      for (auto* widget : QApplication::topLevelWidgets()) {
        auto* menu = qobject_cast<QMenu*>(widget);
        if (menu == nullptr || !menu->isVisible() || menu->objectName() != QStringLiteral("layerContextMenu")) {
          continue;
        }
        QAction* chosen = nullptr;
        for (auto* action : menu->actions()) {
          seen.push_back(action->isSeparator() ? QStringLiteral("-")
                                               : (action->isEnabled() ? QString() : QStringLiteral("!")) +
                                                     action->text().remove(QLatin1Char('&')));
          if (pick != nullptr && action->objectName() == QLatin1String(pick)) {
            chosen = action;
          }
        }
        save_widget_artifact(pick != nullptr ? "issue274_background_menu" : "issue274_lone_background_menu", *menu);
        if (chosen != nullptr) {
          menu->setActiveAction(chosen);
          QTest::keyClick(menu, Qt::Key_Return);
        } else {
          menu->close();
        }
        return;
      }
    });
    const auto point = layer_list->visualItemRect(layer_list->item(layer_list->count() - 1)).center();
    QContextMenuEvent context_event(QContextMenuEvent::Mouse, point, layer_list->viewport()->mapToGlobal(point));
    QApplication::sendEvent(layer_list->viewport(), &context_event);
    QApplication::processEvents();
    return seen;
  };

  auto& doc = patchy::ui::MainWindowTestAccess::document(window);
  const auto undo_depth = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  const auto menu = right_click_background("layerFromBackgroundAction");
  CHECK(menu == (QStringList{QStringLiteral("Layer from Background"), QStringLiteral("-"),
                             QStringLiteral("Duplicate Layer"), QStringLiteral("Duplicate Layer to Document..."),
                             QStringLiteral("Delete Layer"), QStringLiteral("-"),
                             QStringLiteral("Convert to Smart Object"), QStringLiteral("-"),
                             QStringLiteral("Merge Visible"), QStringLiteral("Merge Visible to New Layer (Copy)"),
                             QStringLiteral("Flatten Image"), QStringLiteral("-"),
                             QStringLiteral("Thumbnail Size")}));
  const auto& bottom = std::as_const(doc).layers().front();
  CHECK(bottom.name() == "Layer 0");
  CHECK(patchy::background_layer(std::as_const(doc).layers(), doc.width(), doc.height()) == nullptr);
  CHECK((patchy::layer_lock_flags(bottom) & (patchy::kLayerLockPosition | patchy::kLayerLockTransparentPixels)) ==
        0U);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_depth + 1);
  CHECK(background_badges().isEmpty());
  patchy::ui::MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(std::as_const(doc).layers().front().name() == "Background");
  CHECK(patchy::background_layer(std::as_const(doc).layers(), doc.width(), doc.height()) != nullptr);
  CHECK(background_badges().size() == 1);

  // Alone in its document, Delete Layer and Merge Visible are off.
  patchy::Document lone(64, 48, patchy::PixelFormat::rgb8());
  auto& lone_background =
      lone.add_pixel_layer("Background", solid_pixels(64, 48, patchy::PixelFormat::rgb8(), QColor(Qt::white)));
  patchy::set_layer_locks_position(lone_background, true);
  patchy::set_layer_locks_transparent_pixels(lone_background, true);
  window.add_document_session(std::move(lone), QStringLiteral("Lone Background"));
  QApplication::processEvents();
  const auto lone_menu = right_click_background(nullptr);
  CHECK(lone_menu.contains(QStringLiteral("!Delete Layer")));
  CHECK(lone_menu.contains(QStringLiteral("!Merge Visible to New Layer (Copy)")));
  CHECK(lone_menu.contains(QStringLiteral("!Merge Visible")));
}

// Drives the New Layer dialog once it is up (exec_dialog nests an event loop).
void drive_new_layer_dialog(const std::function<void(QDialog&)>& driver, bool& seen) {
  QTimer::singleShot(0, [driver, &seen] {
    auto* dialog = find_top_level_dialog(QStringLiteral("newLayerDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    seen = true;
    driver(*dialog);
  });
}

void click_dialog_ok(QDialog& dialog) {
  auto* buttons = dialog.findChild<QDialogButtonBox*>();
  CHECK(buttons != nullptr);
  if (buttons != nullptr) {
    buttons->button(QDialogButtonBox::Ok)->click();
  }
}

// GitHub issue 275: Layer > New > New Layer... (Ctrl+Shift+N) asks for the name, clipping,
// color label, mode and opacity first; Cancel adds nothing. New Layer (no default key since
// then) still adds one at once, no dialog.
void ui_new_layer_dialog_applies_each_field_and_cancel_adds_nothing() {
  using Access = patchy::ui::MainWindowTestAccess;
  patchy::ui::MainWindow window;
  show_window(window);
  auto& doc = Access::document(window);
  doc = patchy::Document(64, 48, patchy::PixelFormat::rgba8());
  const auto base_id =
      doc.add_pixel_layer("Base", solid_pixels(64, 48, patchy::PixelFormat::rgba8(), QColor(200, 40, 40))).id();
  doc.set_active_layer(base_id);
  auto* canvas = require_canvas(window);
  canvas->set_document(&doc);
  Access::refresh_layer_ui(window);
  const auto history = Access::active_session_undo_depth(window);
  auto* dialog_action = require_action(window, "layerNewWithDialogAction");
  auto* layer_new_menu = window.findChild<QMenu*>(QStringLiteral("layerNewMenu"));
  CHECK(layer_new_menu != nullptr && layer_new_menu->actions().indexOf(dialog_action) == 0);
  CHECK(layer_new_menu->actions().indexOf(require_action(window, "layerNewAction")) == 1);

  bool seen = false;
  drive_new_layer_dialog([](QDialog& dialog) { dialog.reject(); }, seen);
  dialog_action->trigger();
  CHECK(seen);
  CHECK(std::as_const(doc).layers().size() == 1U);
  CHECK(Access::active_session_undo_depth(window) == history);

  seen = false;
  drive_new_layer_dialog(
      [](QDialog& dialog) {
        auto* name = dialog.findChild<QLineEdit*>(QStringLiteral("newLayerNameEdit"));
        auto* clip = dialog.findChild<QCheckBox*>(QStringLiteral("newLayerClipCheck"));
        auto* color = dialog.findChild<QComboBox*>(QStringLiteral("newLayerColorCombo"));
        auto* mode = dialog.findChild<QComboBox*>(QStringLiteral("newLayerModeCombo"));
        auto* opacity = dialog.findChild<QSpinBox*>(QStringLiteral("newLayerOpacitySpin"));
        CHECK(name != nullptr && clip != nullptr && color != nullptr && mode != nullptr && opacity != nullptr);
        if (name == nullptr || clip == nullptr || color == nullptr || mode == nullptr || opacity == nullptr) {
          dialog.reject();
          return;
        }
        CHECK(name->text() == QStringLiteral("Layer 2"));  // default_new_layer_name: one layer so far
        CHECK(clip->isEnabled() && !clip->isChecked());
        CHECK(color->count() == 8 && color->currentIndex() == 0 && color->currentText() == QStringLiteral("None"));
        CHECK(color->itemText(3) == QStringLiteral("Yellow") && !color->itemIcon(3).isNull());
        CHECK(mode->currentData().toInt() == static_cast<int>(patchy::BlendMode::Normal));
        CHECK(opacity->value() == 100 && opacity->minimum() == 0 && opacity->maximum() == 100);
        name->setText(QStringLiteral("Glaze"));
        clip->setChecked(true);
        color->setCurrentIndex(static_cast<int>(patchy::kLayerColorYellow));
        mode->setCurrentIndex(mode->findData(static_cast<int>(patchy::BlendMode::Multiply)));
        opacity->setValue(40);
        for (const auto scheme : {patchy::ui::ColorSchemePreference::Dark, patchy::ui::ColorSchemePreference::Light}) {
          ColorSchemeRestorer restore(scheme);
          QApplication::processEvents();
          save_widget_artifact(scheme == patchy::ui::ColorSchemePreference::Dark ? "issue275_new_layer_dialog_dark"
                                                                                 : "issue275_new_layer_dialog_light",
                               dialog);
        }
        click_dialog_ok(dialog);
      },
      seen);
  CHECK(dialog_action->shortcut() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N));
  dialog_action->trigger();
  QApplication::processEvents();
  CHECK(seen);
  CHECK(std::as_const(doc).layers().size() == 2U);
  CHECK(Access::active_session_undo_depth(window) == history + 1);
  {
    const auto& added = std::as_const(doc).layers().back();
    CHECK(added.name() == "Glaze" && added.kind() == patchy::LayerKind::Pixel);
    CHECK(added.clipped());
    CHECK(added.color_label() == patchy::kLayerColorYellow);
    CHECK(added.blend_mode() == patchy::BlendMode::Multiply);
    CHECK(std::abs(added.opacity() - 0.4F) < 1e-6F);
    CHECK(doc.active_layer_id() == added.id());
  }
  Access::undo(window);
  CHECK(std::as_const(doc).layers().size() == 1U);

  // New Layer: a default layer at once, no dialog.
  bool unexpected_dialog = false;
  QTimer::singleShot(0, [&unexpected_dialog] {
    if (auto* dialog = find_top_level_dialog(QStringLiteral("newLayerDialog")); dialog != nullptr) {
      unexpected_dialog = true;
      dialog->reject();
    }
  });
  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();
  CHECK(!unexpected_dialog);
  CHECK(std::as_const(doc).layers().size() == 2U);
  {
    const auto& added = std::as_const(doc).layers().back();
    CHECK(added.name() == "Layer 2" && !added.clipped() && added.color_label() == patchy::kLayerColorNone);
    CHECK(added.blend_mode() == patchy::BlendMode::Normal && added.opacity() == 1.0F);
  }

  // No layer below to clip to: the option is off and never applies; an emptied name falls
  // back to the default.
  doc = patchy::Document(32, 24, patchy::PixelFormat::rgba8());
  canvas->set_document(&doc);
  Access::refresh_layer_ui(window);
  seen = false;
  drive_new_layer_dialog(
      [](QDialog& dialog) {
        auto* clip = dialog.findChild<QCheckBox*>(QStringLiteral("newLayerClipCheck"));
        auto* name = dialog.findChild<QLineEdit*>(QStringLiteral("newLayerNameEdit"));
        CHECK(clip != nullptr && !clip->isEnabled());
        if (clip != nullptr) {
          clip->setChecked(true);
        }
        if (name != nullptr) {
          name->clear();
        }
        click_dialog_ok(dialog);
      },
      seen);
  dialog_action->trigger();
  CHECK(seen);
  CHECK(std::as_const(doc).layers().size() == 1U);
  CHECK(!std::as_const(doc).layers().front().clipped());
  CHECK(std::as_const(doc).layers().front().name() == "Layer 1");
}

// GitHub issue 408: Alt-click on the Layers footer's New Layer button opens the New Layer
// dialog; a plain click still adds a layer at once.
void ui_new_layer_button_alt_click_opens_new_layer_dialog() {
  using Access = patchy::ui::MainWindowTestAccess;
  auto&& [window, canvas] = ShownWindowFixture{};
  auto* new_button = window.findChild<QPushButton*>(QStringLiteral("layerNewButton"));
  CHECK(new_button != nullptr);
  if (new_button == nullptr) {
    return;
  }
  const auto& doc = std::as_const(Access::document(window));
  const auto layers_before = doc.layers().size();
  const auto reject_unexpected_dialog = [](bool& unexpected) {
    unexpected = false;
    QTimer::singleShot(0, [&unexpected] {
      if (auto* dialog = find_top_level_dialog(QStringLiteral("newLayerDialog")); dialog != nullptr) {
        unexpected = true;
        dialog->reject();
      }
    });
  };

  bool unexpected_dialog = false;
  reject_unexpected_dialog(unexpected_dialog);
  QTest::mouseClick(new_button, Qt::LeftButton, Qt::NoModifier);
  QApplication::processEvents();
  CHECK(!unexpected_dialog);
  CHECK(doc.layers().size() == layers_before + 1U);

  const auto history = Access::active_session_undo_depth(window);
  bool seen = false;
  drive_new_layer_dialog(
      [](QDialog& dialog) {
        auto* name = dialog.findChild<QLineEdit*>(QStringLiteral("newLayerNameEdit"));
        CHECK(name != nullptr);
        if (name != nullptr) {
          name->setText(QStringLiteral("From Alt"));
        }
        click_dialog_ok(dialog);
      },
      seen);
  QTest::mouseClick(new_button, Qt::LeftButton, Qt::AltModifier);
  QApplication::processEvents();
  CHECK(seen);
  CHECK(doc.layers().size() == layers_before + 2U);
  CHECK(doc.layers().back().name() == "From Alt");
  CHECK(Access::active_session_undo_depth(window) == history + 1);

  // Cancel adds nothing, and the next plain click is plain again.
  seen = false;
  drive_new_layer_dialog([](QDialog& dialog) { dialog.reject(); }, seen);
  QTest::mouseClick(new_button, Qt::LeftButton, Qt::AltModifier);
  QApplication::processEvents();
  CHECK(seen);
  CHECK(doc.layers().size() == layers_before + 2U);
  reject_unexpected_dialog(unexpected_dialog);
  new_button->click();
  QApplication::processEvents();
  CHECK(!unexpected_dialog);
  CHECK(doc.layers().size() == layers_before + 3U);
}

// GitHub issue 410: the clipping option sits under Name and "Fill with neutral color" under
// Mode, both in the field column; the fill's label and enabled state follow the mode.
void ui_new_layer_dialog_places_options_and_neutral_fill_follows_mode() {
  using Access = patchy::ui::MainWindowTestAccess;
  auto&& [window, canvas] = ShownWindowFixture{};
  const auto& doc = std::as_const(Access::document(window));
  const auto layers_before = doc.layers().size();
  bool seen = false;
  drive_new_layer_dialog(
      [](QDialog& dialog) {
        auto* form = dialog.findChild<QFormLayout*>();
        auto* name = dialog.findChild<QLineEdit*>(QStringLiteral("newLayerNameEdit"));
        auto* clip = dialog.findChild<QCheckBox*>(QStringLiteral("newLayerClipCheck"));
        auto* mode = dialog.findChild<QComboBox*>(QStringLiteral("newLayerModeCombo"));
        auto* neutral = dialog.findChild<QCheckBox*>(QStringLiteral("newLayerNeutralFillCheck"));
        CHECK(form != nullptr && name != nullptr && clip != nullptr && mode != nullptr && neutral != nullptr);
        if (form == nullptr || name == nullptr || clip == nullptr || mode == nullptr || neutral == nullptr) {
          dialog.reject();
          return;
        }
        const auto position = [form](QWidget* widget) {
          int row = -1;
          QFormLayout::ItemRole role = QFormLayout::SpanningRole;
          form->getWidgetPosition(widget, &row, &role);
          return std::pair{row, role};
        };
        const auto [name_row, name_role] = position(name);
        const auto [clip_row, clip_role] = position(clip);
        const auto [mode_row, mode_role] = position(mode);
        const auto [neutral_row, neutral_role] = position(neutral);
        CHECK(name_row == 0 && clip_row == 1 && clip_role == QFormLayout::FieldRole);
        CHECK(neutral_row == mode_row + 1 && neutral_role == QFormLayout::FieldRole);
        CHECK(name_role == QFormLayout::FieldRole && mode_role == QFormLayout::FieldRole);
        CHECK(form->labelForField(clip) == nullptr && form->labelForField(neutral) == nullptr);
        // Lined up with the fields, not the labels.
        CHECK(clip->mapTo(&dialog, QPoint()).x() == name->mapTo(&dialog, QPoint()).x());
        CHECK(neutral->mapTo(&dialog, QPoint()).x() == mode->mapTo(&dialog, QPoint()).x());

        const QString white = QStringLiteral("Fill with neutral color (white)");
        const QString black = QStringLiteral("Fill with neutral color (black)");
        const QString gray = QStringLiteral("Fill with neutral color (50% gray)");
        const QString none = QStringLiteral("No neutral color exists for current mode");
        // Normal has no neutral color.
        CHECK(!neutral->isEnabled() && !neutral->isChecked());
        CHECK(neutral->text() == none);
        const std::array<std::pair<patchy::BlendMode, QString>, 26> expected{{
            {patchy::BlendMode::Darken, white},       {patchy::BlendMode::Multiply, white},
            {patchy::BlendMode::ColorBurn, white},    {patchy::BlendMode::LinearBurn, white},
            {patchy::BlendMode::DarkerColor, white},  {patchy::BlendMode::Divide, white},
            {patchy::BlendMode::Lighten, black},      {patchy::BlendMode::Screen, black},
            {patchy::BlendMode::ColorDodge, black},   {patchy::BlendMode::LinearDodge, black},
            {patchy::BlendMode::LighterColor, black}, {patchy::BlendMode::Difference, black},
            {patchy::BlendMode::Exclusion, black},    {patchy::BlendMode::Subtract, black},
            {patchy::BlendMode::Overlay, gray},       {patchy::BlendMode::SoftLight, gray},
            {patchy::BlendMode::HardLight, gray},     {patchy::BlendMode::VividLight, gray},
            {patchy::BlendMode::LinearLight, gray},   {patchy::BlendMode::PinLight, gray},
            {patchy::BlendMode::HardMix, none},       {patchy::BlendMode::Normal, none},
            {patchy::BlendMode::Dissolve, none},      {patchy::BlendMode::Hue, none},
            {patchy::BlendMode::Luminosity, none},    {patchy::BlendMode::Color, none},
        }};
        for (const auto& [blend, label] : expected) {
          const auto index = mode->findData(static_cast<int>(blend));
          CHECK(index >= 0);
          mode->setCurrentIndex(index);
          CHECK(neutral->text() == label);
          CHECK(neutral->isEnabled() == (label != none));
        }
        int offered = 0;
        for (int index = 0; index < mode->count(); ++index) {
          if (mode->itemData(index).isValid()) {
            mode->setCurrentIndex(index);
            offered += neutral->isEnabled() ? 1 : 0;
          }
        }
        CHECK(offered == 20);

        mode->setCurrentIndex(mode->findData(static_cast<int>(patchy::BlendMode::Overlay)));
        neutral->setChecked(true);
        for (const auto scheme : {patchy::ui::ColorSchemePreference::Dark, patchy::ui::ColorSchemePreference::Light}) {
          ColorSchemeRestorer restore(scheme);
          QApplication::processEvents();
          save_widget_artifact(scheme == patchy::ui::ColorSchemePreference::Dark ? "issue410_new_layer_dialog_dark"
                                                                                 : "issue410_new_layer_dialog_light",
                               dialog);
        }
        // A checked fill is not applied once the mode has none.
        mode->setCurrentIndex(mode->findData(static_cast<int>(patchy::BlendMode::Hue)));
        CHECK(!neutral->isEnabled());
        click_dialog_ok(dialog);
      },
      seen);
  require_action(window, "layerNewWithDialogAction")->trigger();
  QApplication::processEvents();
  CHECK(seen);
  CHECK(doc.layers().size() == layers_before + 1U);
  const auto& added = doc.layers().back();
  CHECK(added.blend_mode() == patchy::BlendMode::Hue);
  CHECK(patchy::pixel_alpha_at(added.pixels(), 0, 0) == 0.0F);
}

// GitHub issue 410: the neutral fill lands in the new layer, in the same undo step, at the
// document's depth: 50% gray is 128 in 8 bits, the 16-bit midpoint, and 0.5 linear in 32 bits.
void ui_new_layer_dialog_neutral_fill_fills_layer_at_each_depth() {
  using Access = patchy::ui::MainWindowTestAccess;
  patchy::set_deep_editing_override(true);
  struct Case {
    patchy::BlendMode mode;
    float expected;  // the deep scale in the depth's own domain (load_pixel)
  };
  const auto add_filled_layer = [](patchy::ui::MainWindow& window, patchy::BlendMode blend) {
    bool seen = false;
    drive_new_layer_dialog(
        [blend](QDialog& dialog) {
          auto* mode = dialog.findChild<QComboBox*>(QStringLiteral("newLayerModeCombo"));
          auto* neutral = dialog.findChild<QCheckBox*>(QStringLiteral("newLayerNeutralFillCheck"));
          CHECK(mode != nullptr && neutral != nullptr);
          if (mode == nullptr || neutral == nullptr) {
            dialog.reject();
            return;
          }
          mode->setCurrentIndex(mode->findData(static_cast<int>(blend)));
          CHECK(neutral->isEnabled());
          neutral->setChecked(true);
          click_dialog_ok(dialog);
        },
        seen);
    require_action(window, "layerNewWithDialogAction")->trigger();
    QApplication::processEvents();
    CHECK(seen);
  };
  const std::array<std::pair<const char*, patchy::BitDepth>, 3> depths{{
      {nullptr, patchy::BitDepth::UInt8},
      {"imageMode16BitAction", patchy::BitDepth::UInt16},
      {"imageMode32BitAction", patchy::BitDepth::Float32},
  }};
  for (const auto& [mode_action, expected_depth] : depths) {
    auto&& [window, canvas] = ShownWindowFixture{};
    if (mode_action != nullptr) {
      require_action(window, mode_action)->trigger();
      QApplication::processEvents();
    }
    const auto& doc = std::as_const(Access::document(window));
    const auto depth = doc.color_state().bit_depth;
    CHECK(depth == expected_depth);
    const float gray = depth == patchy::BitDepth::UInt8 ? 128.0F : 127.5F;
    const std::array<Case, 3> cases{{{patchy::BlendMode::Overlay, gray},
                                     {patchy::BlendMode::Multiply, 255.0F},
                                     {patchy::BlendMode::Screen, 0.0F}}};
    for (const auto& item : cases) {
      const auto layers_before = doc.layers().size();
      const auto history = Access::active_session_undo_depth(window);
      add_filled_layer(window, item.mode);
      CHECK(doc.layers().size() == layers_before + 1U);
      CHECK(Access::active_session_undo_depth(window) == history + 1);
      const auto& added = doc.layers().back();
      CHECK(added.blend_mode() == item.mode);
      CHECK(added.pixels().format().bit_depth == depth);
      CHECK(added.pixels().width() == doc.width() && added.pixels().height() == doc.height());
      for (const auto& [x, y] : {std::pair{0, 0}, std::pair{doc.width() - 1, doc.height() - 1}}) {
        const auto pixel = patchy::load_pixel(added.pixels().format(), added.pixels().pixel(x, y));
        CHECK(std::abs(pixel[0] - item.expected) < 0.01F);
        CHECK(std::abs(pixel[1] - item.expected) < 0.01F);
        CHECK(std::abs(pixel[2] - item.expected) < 0.01F);
        CHECK(pixel[3] == 255.0F);
      }
      // One step: undo takes the filled layer away whole.
      Access::undo(window);
      QApplication::processEvents();
      CHECK(doc.layers().size() == layers_before);
    }
  }
  patchy::set_deep_editing_override(std::nullopt);
}

// GitHub issues 275 and 294: the new commands and the dialog follow a language switch.
void ui_new_layer_dialog_and_merge_visible_retranslate() {
  LanguageRestorer language;
  CHECK(patchy::ui::LocalizationManager::instance().set_language(QStringLiteral("en"), false));
  auto&& [window, canvas] = ShownWindowFixture{};
  auto* dialog_action = require_action(window, "layerNewWithDialogAction");
  auto* merge_action = require_action(window, "layerMergeVisibleLayersAction");
  CHECK(dialog_action->text() == QStringLiteral("New &Layer..."));
  CHECK(merge_action->text() == QStringLiteral("&Merge Visible"));
  CHECK(merge_action->statusTip() == QStringLiteral("Merge every visible layer into one, keeping the hidden layers"));

  CHECK(patchy::ui::LocalizationManager::instance().set_language(QStringLiteral("ja"), false));
  QApplication::processEvents();
  CHECK(dialog_action->text() == QStringLiteral("新規レイヤー(&L)..."));
  CHECK(merge_action->text() == QStringLiteral("表示レイヤーを結合(&M)"));
  bool seen = false;
  drive_new_layer_dialog(
      [](QDialog& dialog) {
        CHECK(dialog.windowTitle() == QStringLiteral("新規レイヤー"));
        auto* clip = dialog.findChild<QCheckBox*>(QStringLiteral("newLayerClipCheck"));
        CHECK(clip != nullptr && clip->text() == QStringLiteral("下のレイヤーでクリッピングマスクを作成"));
        dialog.reject();
      },
      seen);
  dialog_action->trigger();
  CHECK(seen);

  CHECK(patchy::ui::LocalizationManager::instance().set_language(QStringLiteral("en"), false));
  QApplication::processEvents();
  CHECK(dialog_action->text() == QStringLiteral("New &Layer..."));
  CHECK(merge_action->text() == QStringLiteral("&Merge Visible"));
}


void ui_first_tab_still_draws_after_second_tab_created() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  CHECK(tabs != nullptr);

  accept_new_document_dialog(360, 240);
  require_action_by_text(window, QStringLiteral("New"))->trigger();
  QApplication::processEvents();
  CHECK(tabs->count() == 2);

  tabs->setCurrentIndex(0);
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(30, 190, 255));
  drag(*canvas, QPoint(80, 80), QPoint(200, 120));
  QApplication::processEvents();

  const auto image = canvas->grab().toImage().convertToFormat(QImage::Format_RGB32);
  CHECK(color_close(QColor(image.pixel(140, 100)), QColor(30, 190, 255), 80));
  save_widget_artifact("ui_first_tab_draw_after_second_tab", *canvas);
}

// The document-tab context menu's file actions: all three are disabled on a
// never-saved document, Copy File Path puts the native path on the clipboard,
// and Reopen Document reloads the file from disk in place (same tab, undo
// history cleared, unsaved changes guarded by a prompt).
void ui_document_tab_context_menu_file_actions() {
  std::filesystem::create_directories("test-artifacts");
  const auto path = QFileInfo(QDir(QStringLiteral("test-artifacts"))
                                  .filePath(QStringLiteral("ui_tab_menu_file_actions.tga")))
                        .absoluteFilePath();
  const auto write_solid_file = [&path](QColor color) {
    patchy::Document source(8, 6, patchy::PixelFormat::rgb8());
    source.add_pixel_layer("Background", solid_pixels(8, 6, patchy::PixelFormat::rgb8(), color));
    patchy::tga::DocumentIo::write_file(source, std::filesystem::path(path.toStdWString()));
  };
  const QColor original_color(200, 40, 40);
  const QColor replaced_color(40, 200, 90);
  write_solid_file(original_color);

  patchy::ui::MainWindow window;
  show_window(window);  // Tab 0: the untitled startup document (no disk path).
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  CHECK(tabs != nullptr);
  auto* tab_bar = tabs->findChild<QTabBar*>();
  CHECK(tab_bar != nullptr);

  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();
  CHECK(tabs->count() == 2);
  const auto document_tab = tabs->currentIndex();
  CHECK(document_tab == 1);

  const auto corner_color = [&window] {
    auto& document = patchy::ui::MainWindowTestAccess::document(window);
    const auto* px = document.layers().front().pixels().pixel(0, 0);
    return QColor(px[0], px[1], px[2]);
  };
  CHECK(corner_color() == original_color);

  // Opens the tab's context menu, records the file actions' enabled state, and
  // optionally triggers one of them while the menu is open (the menu runs a
  // nested exec loop, hence the timer).
  bool menu_seen = false;
  bool file_actions_enabled = false;
  const auto open_menu = [&](int tab_index, const QString& trigger_object_name) {
    menu_seen = false;
    file_actions_enabled = false;
    QTimer::singleShot(0, [&, trigger_object_name] {
      for (auto* widget : QApplication::topLevelWidgets()) {
        auto* menu = qobject_cast<QMenu*>(widget);
        if (menu == nullptr || menu->objectName() != QStringLiteral("documentTabContextMenu")) {
          continue;
        }
        const auto action_by_name = [menu](const QString& name) -> QAction* {
          for (auto* action : menu->actions()) {
            if (action->objectName() == name) {
              return action;
            }
          }
          return nullptr;
        };
        auto* reopen_action = action_by_name(QStringLiteral("documentTabReopenAction"));
        auto* reveal_action = action_by_name(QStringLiteral("documentTabRevealAction"));
        auto* copy_path_action = action_by_name(QStringLiteral("documentTabCopyPathAction"));
        CHECK(reopen_action != nullptr);
        CHECK(reveal_action != nullptr);
        CHECK(copy_path_action != nullptr);
        menu_seen = true;
        file_actions_enabled =
            reopen_action->isEnabled() && reveal_action->isEnabled() && copy_path_action->isEnabled();
        if (!trigger_object_name.isEmpty()) {
          auto* trigger_action = action_by_name(trigger_object_name);
          CHECK(trigger_action != nullptr);
          trigger_action->trigger();
        }
        menu->close();
        return;
      }
      CHECK(false);
    });
    const auto context_point = tab_bar->tabRect(tab_index).center();
    QContextMenuEvent context_event(QContextMenuEvent::Mouse, context_point,
                                    tab_bar->mapToGlobal(context_point));
    QApplication::sendEvent(tab_bar, &context_event);
    QApplication::processEvents();
    CHECK(menu_seen);
  };

  // The never-saved startup document offers the file actions disabled.
  open_menu(0, QString());
  CHECK(!file_actions_enabled);

  // Copy File Path puts the native path on the clipboard.
  open_menu(document_tab, QStringLiteral("documentTabCopyPathAction"));
  CHECK(file_actions_enabled);
  CHECK(QApplication::clipboard()->text() == QDir::toNativeSeparators(path));

  // Reopen on an unmodified document reloads the disk file in place.
  write_solid_file(replaced_color);
  open_menu(document_tab, QStringLiteral("documentTabReopenAction"));
  QApplication::processEvents();
  CHECK(tabs->count() == 2);
  CHECK(tabs->currentIndex() == document_tab);
  CHECK(corner_color() == replaced_color);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == 0);
  CHECK(!patchy::ui::MainWindowTestAccess::active_session_is_modified(window));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_path(window) == path);

  // A modified document prompts before reopening. The prompt runs a nested
  // event loop, so it is dismissed from a polling timer.
  require_action_by_text(window, QStringLiteral("Flip Layer Horizontal"))->trigger();
  QApplication::processEvents();
  CHECK(patchy::ui::MainWindowTestAccess::active_session_is_modified(window));
  write_solid_file(original_color);
  const auto dismiss_prompt_with = [&window](QMessageBox::StandardButton button) {
    auto* dismiss_timer = new QTimer(&window);
    dismiss_timer->setInterval(10);
    QObject::connect(dismiss_timer, &QTimer::timeout, &window, [button, dismiss_timer] {
      auto* dialog =
          qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("reopenDocumentMessageBox")));
      if (dialog == nullptr) {
        return;
      }
      dismiss_timer->stop();
      dismiss_timer->deleteLater();
      dialog->button(button)->click();
    });
    dismiss_timer->start();
  };

  // Cancel keeps the in-memory document and its modified state...
  dismiss_prompt_with(QMessageBox::Cancel);
  open_menu(document_tab, QStringLiteral("documentTabReopenAction"));
  QApplication::processEvents();
  CHECK(patchy::ui::MainWindowTestAccess::active_session_is_modified(window));
  CHECK(corner_color() == replaced_color);

  // ...and Yes discards it in favor of the disk file.
  dismiss_prompt_with(QMessageBox::Yes);
  open_menu(document_tab, QStringLiteral("documentTabReopenAction"));
  QApplication::processEvents();
  CHECK(!patchy::ui::MainWindowTestAccess::active_session_is_modified(window));
  CHECK(corner_color() == original_color);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == 0);
}

}  // namespace

// GitHub issue 139: Image > Flip Horizontal mirrors an off-center layer about the
// canvas, and Layer > Arrange > Rotate Layer Right turns it about its own center.
void ui_image_flip_and_layer_rotate_menu_actions_apply() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  if (document.layers().empty()) {
    (void)document.add_pixel_layer("Background", solid_pixels(document.width(), document.height(),
                                                              patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  }
  // A layer smaller than the canvas: added canvas-sized, then given its own buffer.
  const auto layer_id = document.add_pixel_layer("Block", solid_pixels(document.width(), document.height(),
                                                                       patchy::PixelFormat::rgba8(),
                                                                       QColor(0, 0, 0, 0))).id();
  document.find_layer(layer_id)->set_pixels(solid_pixels(60, 20, patchy::PixelFormat::rgba8(), QColor(200, 30, 30)));
  document.find_layer(layer_id)->set_bounds(patchy::Rect{100, 50, 60, 20});
  document.set_active_layer(layer_id);
  patchy::ui::MainWindowTestAccess::refresh_layer_ui(window);

  require_action(window, "imageFlipHorizontalAction")->trigger();
  QApplication::processEvents();
  {
    const auto* layer = std::as_const(document).find_layer(layer_id);
    CHECK(layer != nullptr);
    CHECK(layer->bounds().x == document.width() - 100 - 60 && layer->bounds().y == 50);
  }
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(std::as_const(document).find_layer(layer_id)->bounds().x == 100);

  require_action(window, "layerRotateClockwiseAction")->trigger();
  QApplication::processEvents();
  {
    const auto* layer = std::as_const(document).find_layer(layer_id);
    CHECK(layer != nullptr);
    CHECK(layer->bounds().width == 20 && layer->bounds().height == 60);
    CHECK(layer->bounds().x == 120 && layer->bounds().y == 30);  // center (130, 60) kept
  }
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Rotated layer clockwise"));
}

std::vector<patchy::test::TestCase> layer_context_lifecycle_tests() {
  return {
      {"ui_image_flip_and_layer_rotate_menu_actions_apply", ui_image_flip_and_layer_rotate_menu_actions_apply},
      {"ui_move_layer_menu_selects_overlapping_layers_and_reveals_rows",
       ui_move_layer_menu_selects_overlapping_layers_and_reveals_rows},
      {"ui_move_layer_menu_respects_pixels_masks_and_visibility",
       ui_move_layer_menu_respects_pixels_masks_and_visibility},
      {"ui_move_layer_menu_preserves_pan_and_cancels_stale_clicks",
       ui_move_layer_menu_preserves_pan_and_cancels_stale_clicks},
      {"ui_right_drag_does_not_pan_canvas", ui_right_drag_does_not_pan_canvas},
      {"ui_selection_context_menu_offers_remove_object", ui_selection_context_menu_offers_remove_object},
      {"ui_stroke_selection_from_context_menu_survives_following_mouse_input",
       ui_stroke_selection_from_context_menu_survives_following_mouse_input},
      {"ui_move_context_menu_offers_free_transform_on_raster_layer",
       ui_move_context_menu_offers_free_transform_on_raster_layer},
      {"ui_layer_style_color_overlay_patch_double_click_opens_picker",
       ui_layer_style_color_overlay_patch_double_click_opens_picker},
      {"ui_layer_context_menu_exposes_blending_options_dialog",
       ui_layer_context_menu_exposes_blending_options_dialog},
      {"ui_layer_context_menu_names_groups_as_groups", ui_layer_context_menu_names_groups_as_groups},
      {"ui_layer_row_double_click_opens_blending_options_dialog",
       ui_layer_row_double_click_opens_blending_options_dialog},
      {"ui_layer_fx_badge_button_opens_layer_style_dialog", ui_layer_fx_badge_button_opens_layer_style_dialog},
      {"ui_layer_style_stroke_blend_mode_round_trips", ui_layer_style_stroke_blend_mode_round_trips},
      {"ui_layer_row_double_click_opens_folder_styles_and_edits_adjustments",
       ui_layer_row_double_click_opens_folder_styles_and_edits_adjustments},
      {"ui_layer_context_menu_rasterizes_text_and_layer_styles",
       ui_layer_context_menu_rasterizes_text_and_layer_styles},
      {"ui_layer_rasterize_recurses_into_selected_folders",
       ui_layer_rasterize_recurses_into_selected_folders},
      {"ui_layer_context_menu_layer_style_actions_follow_selection_state",
       ui_layer_context_menu_layer_style_actions_follow_selection_state},
      {"ui_layer_style_copy_paste_delete_applies_to_selected_layers",
       ui_layer_style_copy_paste_delete_applies_to_selected_layers},
      {"ui_closing_last_document_leaves_empty_workspace", ui_closing_last_document_leaves_empty_workspace},
      {"ui_close_last_tab_with_active_text_edit_commits_editor_first",
       ui_close_last_tab_with_active_text_edit_commits_editor_first},
      {"ui_save_prompt_uses_yes_no_cancel_with_letter_hotkeys",
       ui_save_prompt_uses_yes_no_cancel_with_letter_hotkeys},
      {"ui_document_tab_context_menu_closes_tabs_and_file_menu_closes_all",
       ui_document_tab_context_menu_closes_tabs_and_file_menu_closes_all},
      {"ui_new_document_and_canvas_size_dialogs_work", ui_new_document_and_canvas_size_dialogs_work},
      {"ui_canvas_size_preserves_layers_and_crop_option_resets",
       ui_canvas_size_preserves_layers_and_crop_option_resets},
      {"ui_new_document_presets_and_clipboard_work", ui_new_document_presets_and_clipboard_work},
      {"ui_new_document_preset_double_click_creates_document", ui_new_document_preset_double_click_creates_document},
      {"ui_new_document_dialog_remembers_last_settings", ui_new_document_dialog_remembers_last_settings},
      {"ui_new_document_template_groups_hold_every_template", ui_new_document_template_groups_hold_every_template},
      {"ui_new_document_search_filters_every_group", ui_new_document_search_filters_every_group},
      {"ui_new_document_reopens_on_the_last_picked_group", ui_new_document_reopens_on_the_last_picked_group},
      {"ui_new_document_review_captures", ui_new_document_review_captures},
      {"ui_new_document_dialog_always_opens_centered", ui_new_document_dialog_always_opens_centered},
      {"ui_new_document_recent_row_lists_creates_newest_first", ui_new_document_recent_row_lists_creates_newest_first},
      {"ui_new_document_recent_row_follows_the_clipboard", ui_new_document_recent_row_follows_the_clipboard},
      {"ui_new_document_recent_row_fits_the_dialog_width", ui_new_document_recent_row_fits_the_dialog_width},
      {"ui_new_document_recent_row_seeds_from_old_last_used_settings", ui_new_document_recent_row_seeds_from_old_last_used_settings},
      {"ui_new_document_recent_row_review_captures", ui_new_document_recent_row_review_captures},
      {"ui_new_document_dialog_fits_short_screens", ui_new_document_dialog_fits_short_screens},
      {"ui_new_document_dialog_remembers_unit", ui_new_document_dialog_remembers_unit},
      {"ui_new_document_opens_fit_to_view", ui_new_document_opens_fit_to_view},
      {"ui_new_document_background_starts_locked", ui_new_document_background_starts_locked},
      {"ui_background_layer_stays_at_bottom_and_takes_no_styles",
       ui_background_layer_stays_at_bottom_and_takes_no_styles},
      {"ui_background_layer_marker_and_short_menu", ui_background_layer_marker_and_short_menu},
      {"ui_new_layer_dialog_applies_each_field_and_cancel_adds_nothing",
       ui_new_layer_dialog_applies_each_field_and_cancel_adds_nothing},
      {"ui_new_layer_dialog_and_merge_visible_retranslate", ui_new_layer_dialog_and_merge_visible_retranslate},
      {"ui_new_layer_button_alt_click_opens_new_layer_dialog", ui_new_layer_button_alt_click_opens_new_layer_dialog},
      {"ui_new_layer_dialog_places_options_and_neutral_fill_follows_mode",
       ui_new_layer_dialog_places_options_and_neutral_fill_follows_mode},
      {"ui_new_layer_dialog_neutral_fill_fills_layer_at_each_depth",
       ui_new_layer_dialog_neutral_fill_fills_layer_at_each_depth},
      {"ui_merge_down_into_position_locked_background_works",
       ui_merge_down_into_position_locked_background_works},
      {"ui_first_tab_still_draws_after_second_tab_created", ui_first_tab_still_draws_after_second_tab_created},
      {"ui_document_tab_context_menu_file_actions", ui_document_tab_context_menu_file_actions},
      {"ui_rotate_canvas_arbitrary_dialog_rotates_and_expands_canvas",
       ui_rotate_canvas_arbitrary_dialog_rotates_and_expands_canvas},
  };
}
