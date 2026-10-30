#include <QStatusBar>
#include "ui/canvas_widget.hpp"
#include "ui/main_window.hpp"
#include "test_harness.hpp"
#include <QApplication>
#include <QEventLoop>
#include <algorithm>
#include <atomic>
#include <array>
#include <cstdint>
#include <cstring>
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
#include <vector>
#include "ui_test_access.hpp"
// Vector commands on existing shapes (docs/vector-commands.md): Simplify Path
// (live-preview dialog, commit, cancel, undo, scripting) and Combine Shapes
// (Layers-panel multi-selection into the bottom shape layer, undo, scripting).
#include "ui_test_support.hpp"
#include "core/path_simplify.hpp"
#include "core/vector_raster.hpp"
#include "core/vector_shape.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/script_engine.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QSpinBox>
#include <QStringList>
#include <QTimer>
#include <cmath>
#include <cstdio>
#include <exception>
#include <utility>

using namespace patchy::test::ui;

namespace {

void shape_drag(patchy::ui::CanvasWidget& canvas, QPoint document_from, QPoint document_to) {
  drag(canvas, canvas.widget_position_for_document_point(document_from),
       canvas.widget_position_for_document_point(document_to));
  QApplication::processEvents();
}

// A rectangle shape layer from a tool drag (Shape mode, sharp corners).
patchy::LayerId make_rect_shape_layer(patchy::ui::MainWindow& window, patchy::ui::CanvasWidget& canvas,
                                      QPoint from, QPoint to) {
  canvas.set_tool(patchy::ui::CanvasTool::Rectangle);
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  CHECK(radius_spin != nullptr);
  radius_spin->setValue(0);
  shape_drag(canvas, from, to);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto active = document.active_layer_id();
  CHECK(active.has_value());
  return *active;
}

std::size_t anchor_count(patchy::Document& document, patchy::LayerId id) {
  const auto* layer = std::as_const(document).find_layer(id);
  CHECK(layer != nullptr && layer->vector_shape() != nullptr);
  return patchy::vector_path_anchor_count(layer->vector_shape()->path);
}

// Replaces the layer's path with a 64-gon circle (the over-anchored shape a
// trace produces) and re-bakes it.
void replace_with_dense_circle(patchy::ui::MainWindow& window, patchy::ui::CanvasWidget& canvas,
                               patchy::LayerId id, QPointF center, double radius) {
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  auto* layer = document.find_layer(id);
  CHECK(layer != nullptr && layer->vector_shape() != nullptr);
  auto content = *std::as_const(*layer).vector_shape();
  patchy::PathSubpath subpath;
  subpath.closed = true;
  subpath.op = patchy::PathCombineOp::Add;
  subpath.shape_group = 0;
  for (int i = 0; i < 64; ++i) {
    const double angle = 2.0 * 3.14159265358979323846 * i / 64.0;
    patchy::PathAnchor anchor;
    anchor.anchor_x = center.x() + radius * std::cos(angle);
    anchor.anchor_y = center.y() + radius * std::sin(angle);
    anchor.in_x = anchor.anchor_x;
    anchor.in_y = anchor.anchor_y;
    anchor.out_x = anchor.anchor_x;
    anchor.out_y = anchor.anchor_y;
    subpath.anchors.push_back(anchor);
  }
  content.path.subpaths = {subpath};
  content.origination.clear();
  layer->set_vector_shape(std::move(content));
  patchy::update_vector_shape_raster(*layer, patchy::Rect::from_size(document.width(), document.height()),
                                     &document.metadata().patterns);
  canvas.document_changed();
  QApplication::processEvents();
  CHECK(anchor_count(document, id) == 64);
}

// Drives the non-modal Simplify Path dialog once it opens; assertions are
// collected, not thrown, because a throw across the dialog loop needs the
// unwind helper (kept as the fallback).
void drive_simplify_dialog(QStringList& failures, bool& drove, const std::function<void(QDialog&)>& body) {
  QTimer::singleShot(0, [&failures, &drove, body] {
    try {
      auto* dialog = find_top_level_dialog(QStringLiteral("simplifyPathDialog"));
      if (dialog == nullptr) {
        failures << QStringLiteral("simplifyPathDialog not found");
        return;
      }
      drove = true;
      body(*dialog);
    } catch (...) {
      if (!patchy::ui::unwind_non_modal_dialog_loop(std::current_exception())) {
        throw;
      }
    }
  });
}

void report_failures(const QStringList& failures) {
  for (const auto& failure : failures) {
    std::fprintf(stderr, "  vector command: %s\n", qPrintable(failure));
  }
  CHECK(failures.isEmpty());
}

void ui_simplify_path_dialog_previews_commits_and_undoes() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = make_rect_shape_layer(window, *canvas, QPoint(100, 100), QPoint(300, 220));
  replace_with_dense_circle(window, *canvas, layer_id, QPointF(200.0, 160.0), 60.0);
  const auto undo_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);

  bool drove = false;
  QStringList failures;
  drive_simplify_dialog(failures, drove, [&](QDialog& dialog) {
    auto* tolerance = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("simplifyPathToleranceSpin"));
    auto* label = dialog.findChild<QLabel*>(QStringLiteral("simplifyPathAnchorsLabel"));
    if (tolerance == nullptr || label == nullptr) {
      failures << QStringLiteral("dialog controls missing");
      dialog.reject();
      return;
    }
    tolerance->setValue(1.0);
    QApplication::processEvents();
    if (!label->text().startsWith(QStringLiteral("Anchors: 64 ->"))) {
      failures << QStringLiteral("readout: ") + label->text();
    }
    // The preview already applied to the layer, without arming an undo entry.
    if (anchor_count(document, layer_id) >= 64) {
      failures << QStringLiteral("preview did not apply");
    }
    if (patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) != undo_before) {
      failures << QStringLiteral("preview armed an undo entry");
    }
    dialog.accept();
  });
  require_action(window, "pathSimplifyAction")->trigger();
  QApplication::processEvents();
  CHECK(drove);
  report_failures(failures);

  CHECK(anchor_count(document, layer_id) < 64);
  CHECK(anchor_count(document, layer_id) >= 3);
  CHECK(std::as_const(document).find_layer(layer_id)->vector_shape()->origination.empty());
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before + 1);
  CHECK(window.statusBar()->currentMessage().startsWith(QStringLiteral("Simplified the path")));
  patchy::ui::MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(anchor_count(document, layer_id) == 64);
}

void ui_simplify_path_cancel_restores_original() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = make_rect_shape_layer(window, *canvas, QPoint(100, 100), QPoint(300, 220));
  replace_with_dense_circle(window, *canvas, layer_id, QPointF(200.0, 160.0), 60.0);
  const auto undo_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  const auto original = std::as_const(document).find_layer(layer_id)->vector_shape()->path;

  bool drove = false;
  QStringList failures;
  drive_simplify_dialog(failures, drove, [&](QDialog& dialog) {
    auto* tolerance = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("simplifyPathToleranceSpin"));
    if (tolerance != nullptr) {
      tolerance->setValue(2.0);
      QApplication::processEvents();
      if (anchor_count(document, layer_id) >= 64) {
        failures << QStringLiteral("preview did not apply");
      }
    }
    dialog.reject();
  });
  require_action(window, "pathSimplifyAction")->trigger();
  QApplication::processEvents();
  CHECK(drove);
  report_failures(failures);
  CHECK(std::as_const(document).find_layer(layer_id)->vector_shape()->path == original);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before);

  // Nothing targetable: a plain raster layer refuses with a status error.
  std::optional<patchy::LayerId> background_id;
  for (const auto& layer : std::as_const(document).layers()) {
    if (layer.name() == "Background") {
      background_id = layer.id();
    }
  }
  CHECK(background_id.has_value());
  document.set_active_layer(*background_id);
  patchy::ui::MainWindowTestAccess::refresh_paths_panel(window);
  require_action(window, "pathSimplifyAction")->trigger();
  QApplication::processEvents();
  CHECK(window.statusBar()->currentMessage().startsWith(QStringLiteral("Select a path or shape layer")));
}

// GitHub issue 396: Simplify Path from the path tools' canvas menu edits the
// clicked shape layer's own path, previews live and Cancel restores it; on a
// path with nothing to remove, the readout says so and OK adds no undo entry.
void ui_simplify_path_from_canvas_menu_previews_and_explains_no_change() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = make_rect_shape_layer(window, *canvas, QPoint(100, 100), QPoint(300, 220));
  replace_with_dense_circle(window, *canvas, layer_id, QPointF(200.0, 160.0), 60.0);
  const auto original = std::as_const(document).find_layer(layer_id)->vector_shape()->path;
  const auto undo_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  canvas->set_tool(patchy::ui::CanvasTool::DirectSelect);
  QApplication::processEvents();

  bool drove = false;
  QStringList failures;
  const auto drive_preview = [&] {
    drive_simplify_dialog(failures, drove, [&](QDialog& dialog) {
      auto* tolerance = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("simplifyPathToleranceSpin"));
      auto* label = dialog.findChild<QLabel*>(QStringLiteral("simplifyPathAnchorsLabel"));
      if (tolerance == nullptr || label == nullptr) {
        failures << QStringLiteral("dialog controls missing");
        dialog.reject();
        return;
      }
      tolerance->setValue(2.0);
      QApplication::processEvents();
      if (anchor_count(document, layer_id) >= 64) {
        failures << QStringLiteral("preview did not reach the clicked shape layer");
      }
      if (label->text().contains(QLatin1Char('\n'))) {
        failures << QStringLiteral("hint shown although points were removed: ") + label->text();
      }
      save_stacked_artifact("issue_396_simplify_preview", {canvas->grab().toImage(), dialog.grab().toImage()});
      dialog.reject();
    });
  };
  // The menu is exec'd: once it is up, arm the dialog driver and pick Simplify
  // (its dialog runs inside the menu's loop, as a pick from a real click does).
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* menu = qobject_cast<QMenu*>(widget);
      if (menu != nullptr && menu->objectName() == QStringLiteral("canvasPathContextMenu")) {
        for (auto* action : menu->actions()) {
          if (action->objectName() == QStringLiteral("pathSimplifyAction")) {
            drive_preview();
            action->trigger();
            menu->close();
            return;
          }
        }
        menu->close();
      }
    }
    failures << QStringLiteral("canvas path menu without Simplify Path");
  });
  const auto widget_point = canvas->widget_position_for_document_point(QPoint(260, 160));
  CHECK(canvas->show_path_context_menu(QPointF(widget_point), canvas->mapToGlobal(widget_point)));
  QApplication::processEvents();
  CHECK(drove);
  report_failures(failures);
  CHECK(std::as_const(document).find_layer(layer_id)->vector_shape()->path == original);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before);

  // A four-corner rectangle has nothing to remove at any tolerance.
  const auto rect_id = make_rect_shape_layer(window, *canvas, QPoint(350, 250), QPoint(450, 330));
  CHECK(anchor_count(document, rect_id) == 4);
  const auto rect_undo_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  drove = false;
  drive_simplify_dialog(failures, drove, [&](QDialog& dialog) {
    auto* tolerance = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("simplifyPathToleranceSpin"));
    auto* label = dialog.findChild<QLabel*>(QStringLiteral("simplifyPathAnchorsLabel"));
    if (tolerance == nullptr || label == nullptr) {
      failures << QStringLiteral("dialog controls missing");
      dialog.reject();
      return;
    }
    tolerance->setValue(0.1);
    QApplication::processEvents();
    if (label->text() != QStringLiteral("Anchors: 4 -> 4\nThis path has no points to remove.")) {
      failures << QStringLiteral("readout: ") + label->text();
    }
    save_widget_artifact("issue_396_simplify_no_change", dialog);
    dialog.accept();
  });
  require_action(window, "pathSimplifyAction")->trigger();
  QApplication::processEvents();
  CHECK(drove);
  report_failures(failures);
  CHECK(anchor_count(document, rect_id) == 4);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == rect_undo_before);
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("This path has no points to remove."));
}

void ui_combine_shapes_subtracts_front_and_undoes() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto base_id = make_rect_shape_layer(window, *canvas, QPoint(100, 100), QPoint(300, 220));
  const auto front_id = make_rect_shape_layer(window, *canvas, QPoint(200, 160), QPoint(400, 300));
  CHECK(base_id != front_id);
  const auto layers_before = document.layers().size();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(250, 190)), Qt::black, 8));

  auto* subtract = require_action(window, "layerCombineSubtractAction");
  auto* unite = require_action(window, "layerCombineUniteAction");
  CHECK(!subtract->isEnabled());  // one active layer
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  layer_list->clearSelection();
  require_layer_item(*layer_list, QStringLiteral("Rectangle 2"))->setSelected(true);  // front
  require_layer_item(*layer_list, QStringLiteral("Rectangle 1"))->setSelected(true);  // base
  QApplication::processEvents();
  CHECK(subtract->isEnabled());
  CHECK(unite->isEnabled());

  const auto undo_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  subtract->trigger();
  QApplication::processEvents();
  CHECK(document.layers().size() == layers_before - 1);
  const auto* base = std::as_const(document).find_layer(base_id);
  CHECK(base != nullptr && base->name() == "Rectangle 1");
  CHECK(std::as_const(document).find_layer(front_id) == nullptr);
  CHECK(document.active_layer_id() == base_id);
  const auto& subpaths = base->vector_shape()->path.subpaths;
  CHECK(subpaths.size() == 2);
  CHECK(subpaths[0].op == patchy::PathCombineOp::Add);
  CHECK(subpaths[1].op == patchy::PathCombineOp::Subtract);
  CHECK(subpaths[1].shape_group == 1);
  // The front cut out of the base: overlap is background, the rest stays.
  CHECK(color_close(canvas_pixel(*canvas, QPoint(250, 190)), Qt::white, 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(150, 130)), Qt::black, 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(350, 280)), Qt::white, 8));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before + 1);
  CHECK(window.statusBar()->currentMessage().startsWith(QStringLiteral("Combined 2 shape layer")));

  patchy::ui::MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(document.layers().size() == layers_before);
  CHECK(std::as_const(document).find_layer(front_id) != nullptr);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(250, 190)), Qt::black, 8));
}

// Ctrl+E merges compatible shapes immediately. Mixed content asks before
// rasterizing, and defaults to retaining the editable shapes.
void ui_merge_down_keeps_shape_layers_vector() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto base_id = make_rect_shape_layer(window, *canvas, QPoint(100, 100), QPoint(300, 220));
  const auto front_id = make_rect_shape_layer(window, *canvas, QPoint(200, 160), QPoint(400, 300));
  const auto layers_before = document.layers().size();

  // The front layer is active: Merge Down folds it into the base below.
  CHECK(document.active_layer_id() == front_id);
  const auto undo_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  require_action(window, "layerMergeDownAction")->trigger();
  QApplication::processEvents();
  CHECK(document.layers().size() == layers_before - 1);
  CHECK(std::as_const(document).find_layer(front_id) == nullptr);
  const auto* base = std::as_const(document).find_layer(base_id);
  CHECK(base != nullptr && base->name() == "Rectangle 1");
  CHECK(document.active_layer_id() == base_id);
  CHECK(base->vector_shape() != nullptr);  // still a vector shape layer
  const auto& subpaths = base->vector_shape()->path.subpaths;
  CHECK(subpaths.size() == 2);
  CHECK(subpaths[0].op == patchy::PathCombineOp::Add);
  CHECK(subpaths[1].op == patchy::PathCombineOp::Add);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(150, 130)), Qt::black, 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(350, 280)), Qt::black, 8));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before + 1);
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Merged shapes down"));

  patchy::ui::MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(document.layers().size() == layers_before);
  CHECK(std::as_const(document).find_layer(front_id) != nullptr);

  // A shape merging onto the raster Background rasterizes only by choice.
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  layer_list->clearSelection();
  require_layer_item(*layer_list, QStringLiteral("Rectangle 1"))->setSelected(true);
  QApplication::processEvents();
  bool saw_merge_dialog = false;
  QTimer::singleShot(0, [&] {
    try {
      auto* dialog = find_top_level_dialog(QStringLiteral("mergeLayersDialog"));
      CHECK(dialog != nullptr);
      auto* keep = dialog->findChild<QCheckBox*>(QStringLiteral("mergeKeepVectorsCheck"));
      CHECK(keep != nullptr && keep->isChecked());
      keep->setChecked(false);
      saw_merge_dialog = true;
      dialog->accept();
    } catch (...) {
      (void)patchy::ui::unwind_non_modal_dialog_loop(std::current_exception());
    }
  });
  require_action(window, "layerMergeDownAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_merge_dialog);
  CHECK(std::as_const(document).find_layer(base_id) == nullptr ||
        std::as_const(document).find_layer(base_id)->vector_shape() == nullptr);
}

void run_script_and_wait(patchy::ui::MainWindow& window, const QString& source, const char* name) {
  auto& host = window.script_engine_host();
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QLatin1String(name);
  (void)host.run_source(source, std::move(options));
  QElapsedTimer timer;
  timer.start();
  while (host.run_active() && timer.elapsed() < 30000) {
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
  }
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
  CHECK(!host.run_active());
  CHECK(!host.last_run_had_error());
}

bool backlog_contains(patchy::ui::MainWindow& window, const QString& needle) {
  for (const auto& line : window.script_engine_host().message_backlog()) {
    if (line.contains(needle)) {
      return true;
    }
  }
  return false;
}

void ui_script_simplify_path_reports_anchor_counts() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = make_rect_shape_layer(window, *canvas, QPoint(100, 100), QPoint(300, 220));
  replace_with_dense_circle(window, *canvas, layer_id, QPointF(200.0, 160.0), 60.0);
  run_script_and_wait(window, QStringLiteral(R"JS(
    var result = app.activeDocument.activeLayer.simplifyPath({tolerance: 1, cornerAngle: 60});
    console.log('simplify:' + result.anchorsBefore + ':' + (result.anchorsAfter < result.anchorsBefore));
    try {
      app.activeDocument.activeLayer.simplifyPath({bogus: 1});
      console.log('no-throw');
    } catch (error) {
      console.log('threw:' + error.message);
    }
  )JS"),
                      "simplify-test");
  CHECK(backlog_contains(window, QStringLiteral("simplify:64:true")));
  CHECK(backlog_contains(window, QStringLiteral("threw:")));
  CHECK(anchor_count(document, layer_id) < 64);
}

void ui_script_combine_shapes_returns_base_layer() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto base_id = make_rect_shape_layer(window, *canvas, QPoint(100, 100), QPoint(300, 220));
  make_rect_shape_layer(window, *canvas, QPoint(200, 160), QPoint(400, 300));
  const auto layers_before = document.layers().size();
  run_script_and_wait(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    var base = doc.combineShapes([doc.findLayer('Rectangle 2'), doc.findLayer('Rectangle 1')], 'unite');
    console.log('combine:' + base.name + ':' + doc.layers.length);
    try {
      doc.combineShapes([base], 'unite');
      console.log('no-throw');
    } catch (error) {
      console.log('threw:' + error.message);
    }
  )JS"),
                      "combine-test");
  CHECK(backlog_contains(window, QStringLiteral("combine:Rectangle 1:%1").arg(layers_before - 1)));
  CHECK(backlog_contains(window, QStringLiteral("threw:")));
  const auto* base = std::as_const(document).find_layer(base_id);
  CHECK(base != nullptr && base->vector_shape()->path.subpaths.size() == 2);
  CHECK(base->vector_shape()->path.subpaths[1].op == patchy::PathCombineOp::Add);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(350, 280)), Qt::black, 8));
}

void ui_script_ungroup_returns_children() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();
  layer_list->clearSelection();
  layer_list->item(0)->setSelected(true);
  layer_list->item(1)->setSelected(true);
  require_action(window, "layerNewFolderAction")->trigger();
  QApplication::processEvents();
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layers_before = document.layers().size();
  run_script_and_wait(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    var released = doc.activeLayer.ungroup();
    console.log('ungroup:' + released.length + ':' + doc.layers.length + ':' + (doc.activeLayer.name === released[0].name));
    try {
      doc.activeLayer.ungroup();
      console.log('no-throw');
    } catch (error) {
      console.log('threw:' + error.message);
    }
  )JS"),
                      "ungroup-test");
  CHECK(backlog_contains(window, QStringLiteral("ungroup:2:%1:true").arg(layers_before + 1)));
  CHECK(backlog_contains(window, QStringLiteral("threw:")));
}

// GitHub #521: Combine Shapes is in the Layers panel menu for two or more shape layers and
// in the canvas path menu under Path Select, and it also combines two selected components of
// one path (the work path here): the front one takes the op, the bottom one keeps its own.
void ui_combine_shapes_in_context_menus_and_on_path_components() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto base_id = make_rect_shape_layer(window, *canvas, QPoint(100, 100), QPoint(300, 220));
  const auto front_id = make_rect_shape_layer(window, *canvas, QPoint(200, 160), QPoint(400, 300));
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  layer_list->clearSelection();
  require_layer_item(*layer_list, QStringLiteral("Rectangle 2"))->setSelected(true);
  require_layer_item(*layer_list, QStringLiteral("Rectangle 1"))->setSelected(true);
  QApplication::processEvents();

  // The Layers panel menu lists the four commands in a Combine Shapes submenu.
  bool layer_menu_listed = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* menu = qobject_cast<QMenu*>(widget);
      if (menu == nullptr || menu->objectName() != QStringLiteral("layerContextMenu")) {
        continue;
      }
      if (auto* combine = menu->findChild<QMenu*>(QStringLiteral("layerContextCombineShapesMenu"));
          combine != nullptr) {
        QStringList names;
        for (auto* action : combine->actions()) {
          names << action->objectName();
        }
        layer_menu_listed = names == QStringList{QStringLiteral("layerCombineUniteAction"),
                                                 QStringLiteral("layerCombineSubtractAction"),
                                                 QStringLiteral("layerCombineIntersectAction"),
                                                 QStringLiteral("layerCombineExcludeAction")};
      }
      menu->close();
      return;
    }
  });
  CHECK(QMetaObject::invokeMethod(layer_list, "customContextMenuRequested", Qt::DirectConnection,
                                  Q_ARG(QPoint, layer_list->visualItemRect(
                                      require_layer_item(*layer_list, QStringLiteral("Rectangle 1"))).center())));
  QApplication::processEvents();
  CHECK(layer_menu_listed);

  // The canvas path menu under Path Select offers the same actions; Subtract runs from it.
  canvas->set_view_zoom_centered(1.0);
  canvas->set_tool(patchy::ui::CanvasTool::PathSelect);
  QApplication::processEvents();
  QTimer::singleShot(0, [] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* menu = qobject_cast<QMenu*>(widget);
      if (menu == nullptr || menu->objectName() != QStringLiteral("canvasPathContextMenu")) {
        continue;
      }
      for (auto* action : menu->actions()) {
        if (action->objectName() == QStringLiteral("layerCombineSubtractAction")) {
          CHECK(action->isEnabled());
          action->trigger();
        }
      }
      menu->close();
      return;
    }
  });
  const auto widget_point = canvas->widget_position_for_document_point(QPoint(250, 190));
  CHECK(canvas->show_path_context_menu(QPointF(widget_point), canvas->mapToGlobal(widget_point)));
  QApplication::processEvents();
  CHECK(std::as_const(document).find_layer(front_id) == nullptr);
  const auto* base = std::as_const(document).find_layer(base_id);
  CHECK(base != nullptr && base->vector_shape()->path.subpaths.size() == 2);
  CHECK(base->vector_shape()->path.subpaths[1].op == patchy::PathCombineOp::Subtract);

  // Two components of the work path: select both with Path Select, then Exclude.
  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  CHECK(mode_combo != nullptr);
  mode_combo->setCurrentIndex(1);  // Path
  shape_drag(*canvas, QPoint(450, 100), QPoint(550, 200));
  shape_drag(*canvas, QPoint(500, 150), QPoint(600, 250));
  const auto* work = document.work_path();
  CHECK(work != nullptr && work->path().subpaths.size() == 2);
  canvas->set_tool(patchy::ui::CanvasTool::PathSelect);
  shape_drag(*canvas, QPoint(460, 100), QPoint(460, 100));  // the first component's top edge
  const auto shift_point = canvas->widget_position_for_document_point(QPoint(600, 240));
  send_mouse(*canvas, QEvent::MouseButtonPress, shift_point, Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, shift_point, Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(canvas->path_edit_selected_groups().size() == 2);
  auto* exclude = require_action(window, "layerCombineExcludeAction");
  CHECK(exclude->isEnabled());
  const auto undo_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  exclude->trigger();
  QApplication::processEvents();
  work = document.work_path();
  CHECK(work->path().subpaths[0].op == patchy::PathCombineOp::Add);
  CHECK(work->path().subpaths[1].op == patchy::PathCombineOp::Xor);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before + 1);
}

// GitHub #521: a Polygon/Star stays live. Its Sides and Star inset change afterwards in the
// Properties panel (and from the options bar while it is selected), also after a W / H edit.
void ui_polygon_stays_live_and_edits_in_properties_panel() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::show_panel(window, QStringLiteral("propertiesDock"));
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  require_action(window, "toolPolygonAction")->trigger();
  QApplication::processEvents();
  auto* bar_sides = window.findChild<QSpinBox*>(QStringLiteral("polygonSidesSpin"));
  auto* bar_inset = window.findChild<QSpinBox*>(QStringLiteral("polygonStarInsetSpin"));
  CHECK(bar_sides != nullptr && bar_inset != nullptr);
  bar_sides->setValue(5);
  bar_inset->setValue(0);
  canvas->set_view_zoom_centered(1.0);
  shape_drag(*canvas, QPoint(200, 200), QPoint(200, 120));  // center-out, radius 80
  const auto layer_id = *document.active_layer_id();
  const auto shape = [&]() -> const patchy::VectorShapeContent& {
    return *std::as_const(document).find_layer(layer_id)->vector_shape();
  };
  CHECK(shape().origination.size() == 1);
  CHECK(shape().origination[0].kind == patchy::LiveShapeKind::Polygon);
  CHECK(shape().path.subpaths[0].anchors.size() == 5);
  CHECK(std::abs(shape().path.subpaths[0].anchors[0].anchor_y - 120.0) < 0.5);  // first vertex at the cursor

  auto* panel = window.findChild<QWidget*>(QStringLiteral("propertiesLiveShapePanel"));
  auto* sides = window.findChild<QSpinBox*>(QStringLiteral("propertiesPolygonSidesSpin"));
  auto* inset = window.findChild<QSpinBox*>(QStringLiteral("propertiesPolygonStarInsetSpin"));
  auto* radius_row = window.findChild<QWidget*>(QStringLiteral("propertiesShapeRadiusRow"));
  CHECK(panel != nullptr && sides != nullptr && inset != nullptr && radius_row != nullptr);
  CHECK(!panel->isHidden());
  CHECK(radius_row->isHidden());
  CHECK(sides->value() == 5);
  const auto undo_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  sides->setValue(6);
  QApplication::processEvents();
  CHECK(shape().path.subpaths[0].anchors.size() == 6);
  CHECK(shape().origination[0].polygon_sides == 6);
  inset->setValue(50);
  QApplication::processEvents();
  CHECK(shape().path.subpaths[0].anchors.size() == 12);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before + 2);

  // A W edit keeps it live: the next Sides edit still regenerates it at the new size.
  auto* width = window.findChild<QDoubleSpinBox*>(QStringLiteral("propertiesShapeWidthSpin"));
  CHECK(width != nullptr);
  const auto before = shape().path.bounds();
  CHECK(before.has_value());
  width->setValue((before->right - before->left) * 2.0);
  process_events_for(450);
  CHECK(shape().origination.size() == 1);
  sides->setValue(7);
  QApplication::processEvents();
  CHECK(shape().path.subpaths[0].anchors.size() == 14);
  const auto after = shape().path.bounds();
  CHECK(after.has_value() && (after->right - after->left) > (before->right - before->left) * 1.6);
  // The options bar's Sides follows into the selected polygon too.
  bar_sides->setValue(8);
  QApplication::processEvents();
  CHECK(shape().path.subpaths[0].anchors.size() == 16);
  if (auto* properties = window.findChild<QWidget*>(QStringLiteral("propertiesPanel")); properties != nullptr) {
    save_widget_artifact("ui_i521_properties_star", *properties);
  }
  save_widget_artifact("ui_i521_star_canvas", *canvas);
  // A point edit ends the live state: the rows hide.
  auto path = shape().path;
  path.subpaths[0].anchors[0].anchor_x += 15.0;
  canvas->set_tool(patchy::ui::CanvasTool::DirectSelect);
  canvas->replace_path_edit_target(path, {path.subpaths[0].shape_group});
  QApplication::processEvents();
  CHECK(panel->isHidden());
}

// GitHub #521: Path-mode components keep their live parameters, and the Properties panel's
// W / H / X / Y size and place the component Path Select picked on the work path.
void ui_path_components_size_place_and_stay_live() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::show_panel(window, QStringLiteral("propertiesDock"));
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  CHECK(radius_spin != nullptr && mode_combo != nullptr);
  radius_spin->setValue(0);
  mode_combo->setCurrentIndex(1);  // Path
  canvas->set_view_zoom_centered(1.0);
  shape_drag(*canvas, QPoint(100, 100), QPoint(300, 220));
  canvas->set_tool(patchy::ui::CanvasTool::Ellipse);
  shape_drag(*canvas, QPoint(400, 100), QPoint(500, 200));
  const auto work = [&]() -> const patchy::DocumentPath& { return *document.work_path(); };
  CHECK(work().path().subpaths.size() == 2);
  CHECK(work().live_shapes().size() == 2);

  auto* size_panel = window.findChild<QWidget*>(QStringLiteral("propertiesShapeSizePanel"));
  auto* width = window.findChild<QDoubleSpinBox*>(QStringLiteral("propertiesShapeWidthSpin"));
  auto* x = window.findChild<QDoubleSpinBox*>(QStringLiteral("propertiesShapeXSpin"));
  auto* y = window.findChild<QDoubleSpinBox*>(QStringLiteral("propertiesShapeYSpin"));
  auto* radius = window.findChild<QDoubleSpinBox*>(QStringLiteral("propertiesShapeRadiusSpin"));
  CHECK(size_panel != nullptr && width != nullptr && x != nullptr && y != nullptr && radius != nullptr);
  canvas->set_tool(patchy::ui::CanvasTool::PathSelect);
  QApplication::processEvents();
  CHECK(size_panel->isHidden() || !width->isEnabled());  // nothing selected on the work path yet
  shape_drag(*canvas, QPoint(200, 100), QPoint(200, 100));  // the rectangle's top edge
  CHECK(canvas->path_edit_selected_groups().size() == 1);
  CHECK(!size_panel->isHidden() && width->isEnabled());
  CHECK(std::abs(width->value() - 200.0) < 0.5);
  CHECK(std::abs(x->value() - 100.0) < 0.5);
  CHECK(std::abs(y->value() - 100.0) < 0.5);

  const auto undo_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  width->setValue(100.0);
  process_events_for(450);
  auto bounds = patchy::VectorPath{{work().path().subpaths[0]}}.bounds();
  CHECK(bounds.has_value() && std::abs(bounds->left - 100.0) < 0.5 && std::abs(bounds->right - 200.0) < 0.5);
  CHECK(std::abs(work().path().subpaths[1].anchors[0].anchor_x - 450.0) < 0.5);  // the ellipse stayed
  x->setValue(150.0);
  process_events_for(450);
  bounds = patchy::VectorPath{{work().path().subpaths[0]}}.bounds();
  CHECK(bounds.has_value() && std::abs(bounds->left - 150.0) < 0.5 && std::abs(bounds->right - 250.0) < 0.5);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before + 2);

  // Still a live rectangle: a Radius makes it rounded at its new size and place.
  auto* radius_row = window.findChild<QWidget*>(QStringLiteral("propertiesShapeRadiusRow"));
  CHECK(radius_row != nullptr && !radius_row->isHidden());
  radius->setValue(10.0);
  QApplication::processEvents();
  CHECK(work().path().subpaths[0].anchors.size() == 8);
  bounds = patchy::VectorPath{{work().path().subpaths[0]}}.bounds();
  CHECK(bounds.has_value() && std::abs(bounds->left - 150.0) < 0.5 && std::abs(bounds->right - 250.0) < 0.5);
  CHECK(canvas->path_edit_selected_groups().size() == 1);  // the whole component stays selected
  if (auto* properties = window.findChild<QWidget*>(QStringLiteral("propertiesPanel")); properties != nullptr) {
    save_widget_artifact("ui_i521_properties_path_size", *properties);
  }
  save_widget_artifact("ui_i521_path_size_canvas", *canvas);
}

}  // namespace

std::vector<patchy::test::TestCase> vector_commands_tests() {
  return {
      {"ui_simplify_path_dialog_previews_commits_and_undoes",
       ui_simplify_path_dialog_previews_commits_and_undoes},
      {"ui_simplify_path_cancel_restores_original", ui_simplify_path_cancel_restores_original},
      {"ui_simplify_path_from_canvas_menu_previews_and_explains_no_change",
       ui_simplify_path_from_canvas_menu_previews_and_explains_no_change},
      {"ui_combine_shapes_subtracts_front_and_undoes", ui_combine_shapes_subtracts_front_and_undoes},
      {"ui_combine_shapes_in_context_menus_and_on_path_components",
       ui_combine_shapes_in_context_menus_and_on_path_components},
      {"ui_polygon_stays_live_and_edits_in_properties_panel", ui_polygon_stays_live_and_edits_in_properties_panel},
      {"ui_path_components_size_place_and_stay_live", ui_path_components_size_place_and_stay_live},
      {"ui_merge_down_keeps_shape_layers_vector", ui_merge_down_keeps_shape_layers_vector},
      {"ui_script_simplify_path_reports_anchor_counts", ui_script_simplify_path_reports_anchor_counts},
      {"ui_script_combine_shapes_returns_base_layer", ui_script_combine_shapes_returns_base_layer},
      {"ui_script_ungroup_returns_children", ui_script_ungroup_returns_children},
  };
}
