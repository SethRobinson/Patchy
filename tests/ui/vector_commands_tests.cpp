// Vector commands on existing shapes (docs/vector-commands.md): Simplify Path
// (live-preview dialog, commit, cancel, undo, scripting) and Combine Shapes
// (Layers-panel multi-selection into the bottom shape layer, undo, scripting).
#include "ui_test_support.hpp"

#include "core/path_simplify.hpp"
#include "core/vector_raster.hpp"
#include "core/vector_shape.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/script_engine.hpp"

#include <QAction>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QListWidget>
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

}  // namespace

std::vector<patchy::test::TestCase> vector_commands_tests() {
  return {
      {"ui_simplify_path_dialog_previews_commits_and_undoes",
       ui_simplify_path_dialog_previews_commits_and_undoes},
      {"ui_simplify_path_cancel_restores_original", ui_simplify_path_cancel_restores_original},
      {"ui_combine_shapes_subtracts_front_and_undoes", ui_combine_shapes_subtracts_front_and_undoes},
      {"ui_script_simplify_path_reports_anchor_counts", ui_script_simplify_path_reports_anchor_counts},
      {"ui_script_combine_shapes_returns_base_layer", ui_script_combine_shapes_returns_base_layer},
      {"ui_script_ungroup_returns_children", ui_script_ungroup_returns_children},
  };
}
