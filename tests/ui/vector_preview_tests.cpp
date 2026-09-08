#include "ui_test_support.hpp"
#include "local_psd_fixtures.hpp"
#include "core/vector_live_shapes.hpp"
#include "core/vector_raster.hpp"
#include "ui/background_workers.hpp"
#include "ui/script_engine.hpp"
#include "ui/vector_preview_renderer.hpp"

#include <QElapsedTimer>
#include <QScrollBar>

#include <cstdio>
#include <limits>
#include <utility>

using namespace patchy;
using namespace patchy::ui;
using namespace patchy::test::ui;

namespace {

struct PreviewPreferenceGuard {
  QVariant before = app_settings().value(QStringLiteral("view/vectorPreview"));
  PreviewPreferenceGuard() { app_settings().remove(QStringLiteral("view/vectorPreview")); }
  ~PreviewPreferenceGuard() {
    if (before.isValid()) { app_settings().setValue(QStringLiteral("view/vectorPreview"), before); }
    else { app_settings().remove(QStringLiteral("view/vectorPreview")); }
  }
};

VectorShapeContent ellipse(double x, double y, double width, double height, RgbColor color) {
  LiveShapeParams params;
  params.kind = LiveShapeKind::Ellipse;
  params.left = x; params.top = y; params.right = x + width; params.bottom = y + height;
  VectorShapeContent shape;
  shape.path.subpaths = generate_live_shape_subpaths(params);
  shape.fill.color = color;
  return shape;
}

Layer shape_layer(Document& doc, VectorShapeContent shape) {
  Layer layer(doc.allocate_layer_id(), "Vector", LayerKind::Pixel);
  layer.set_vector_shape(std::move(shape));
  update_vector_shape_raster(layer, Rect::from_size(doc.width(), doc.height()), nullptr);
  return layer;
}

Document sample() {
  Document doc(96, 80, PixelFormat::rgba8());
  VectorShapeContent paper;
  paper.fill.color = {244, 241, 233};
  doc.add_layer(shape_layer(doc, paper));
  auto shape = ellipse(9.125, 7.5, 71.5, 60.25, {40, 140, 190});
  shape.stroke.enabled = true;
  shape.stroke.width = 0.8;
  shape.stroke.cap = VectorStrokeCap::Round;
  shape.stroke.join = VectorStrokeJoin::Round;
  shape.stroke.content.color = {20, 35, 45};
  auto hole = ellipse(28, 22, 32, 27, {});
  hole.path.subpaths[0].op = PathCombineOp::Subtract;
  hole.path.subpaths[0].shape_group = 1;
  shape.path.subpaths.push_back(hole.path.subpaths[0]);
  Layer group(doc.allocate_layer_id(), "Group", LayerKind::Group);
  group.set_blend_mode(BlendMode::PassThrough);
  group.set_opacity(0.7F);
  auto first = shape_layer(doc, shape);
  first.set_fill_opacity(0.8F);
  group.add_child(std::move(first));
  auto second = shape_layer(doc, ellipse(40, 17, 42, 40, {220, 70, 30}));
  second.set_opacity(0.6F);
  group.add_child(std::move(second));
  doc.add_layer(std::move(group));
  return doc;
}

void settle(CanvasWidget& canvas) {
  canvas.update();
  CHECK(process_events_until([&] { return canvas.render_settled(); }, 60000));
  QApplication::processEvents();
}

void ui_vector_preview_tiles_match_full_vector_raster() {
  auto doc = sample();
  const auto original = psd::DocumentIo::write_layered_rgb8(doc);
  const auto scene = build_vector_preview_scene(doc);
  CHECK(scene.fallback == VectorPreviewFallback::None);
  for (const int scale : {1, 4, 8}) {
    auto enlarged = doc;
    resize_image_and_layers(enlarged, doc.width() * scale, doc.height() * scale);
    const auto expected = qimage_from_document(enlarged, true);
    const auto result = render_vector_preview(scene, {expected.size(), static_cast<double>(scale), {}});
    CHECK(result.fallback == VectorPreviewFallback::None);
    CHECK(result.image == expected);
    CHECK(result.peak_raster_bytes <= kVectorPreviewRasterBudget);
  }
  // Fractional origin and zoom have one phase across all tiles. A viewport
  // crop at an integer screen offset must equal the corresponding full view.
  const VectorPreviewView full{{700, 620}, 5.375, {37.25, 24.625}};
  const auto a = render_vector_preview(scene, full);
  const auto b = render_vector_preview(scene, {{420, 360}, full.scale, full.offset - QPointF(231, 193)});
  CHECK(a.fallback == VectorPreviewFallback::None);
  CHECK(b.image == a.image.copy(231, 193, 420, 360));
  CHECK(psd::DocumentIo::write_layered_rgb8(doc) == original);
}

void ui_vector_preview_strokes_complements_and_group_isolation() {
  for (const auto alignment : {VectorStrokeAlignment::Inside, VectorStrokeAlignment::Center, VectorStrokeAlignment::Outside}) {
    for (const auto join : {VectorStrokeJoin::Miter, VectorStrokeJoin::Round, VectorStrokeJoin::Bevel}) {
      Document doc(40, 40, PixelFormat::rgba8());
      auto shape = ellipse(3.25, 4.5, 31, 27, {170, 130, 60});
      shape.stroke.enabled = true;
      shape.stroke.width = 1.25;
      shape.stroke.alignment = alignment;
      shape.stroke.join = join;
      shape.stroke.cap = join == VectorStrokeJoin::Miter ? VectorStrokeCap::Butt :
          join == VectorStrokeJoin::Round ? VectorStrokeCap::Round : VectorStrokeCap::Square;
      shape.stroke.dashes = {2, 1};
      shape.stroke.dash_offset = 0.35;
      shape.stroke.opacity = 0.7;
      shape.path_inverted = true;
      shape.path.subpaths[0].closed = false;
      Layer group(doc.allocate_layer_id(), "Isolated", LayerKind::Group);
      group.set_opacity(0.65F);
      group.add_child(shape_layer(doc, shape));
      doc.add_layer(std::move(group));
      const auto scene = build_vector_preview_scene(doc);
      auto enlarged = doc;
      resize_image_and_layers(enlarged, 320, 320);
      const auto result = render_vector_preview(scene, {{320, 320}, 8.0, {}});
      CHECK(result.fallback == VectorPreviewFallback::None);
      CHECK(result.image == qimage_from_document(enlarged, true));
    }
  }
}

void ui_vector_preview_eligibility_and_resource_guards() {
  auto doc = sample();
  auto& raster = doc.add_layer(Layer(doc.allocate_layer_id(), "Raster", PixelBuffer(1, 1, PixelFormat::rgba8())));
  CHECK(build_vector_preview_scene(doc).fallback == VectorPreviewFallback::Content);
  raster.set_visible(false);
  CHECK(build_vector_preview_scene(doc).fallback == VectorPreviewFallback::None);
  auto& layer = doc.layers().front();
  layer.set_clipped(true);
  CHECK(build_vector_preview_scene(doc).fallback == VectorPreviewFallback::Blending);
  layer.set_clipped(false);
  auto changed = *std::as_const(layer).vector_shape();
  changed.fill.kind = VectorFillKind::Gradient;
  layer.set_vector_shape(changed);
  CHECK(build_vector_preview_scene(doc).fallback == VectorPreviewFallback::Paint);
  changed.fill.kind = VectorFillKind::Solid;
  layer.set_vector_shape(changed);
  layer.set_vector_mask(LayerVectorMask{});
  CHECK(build_vector_preview_scene(doc).fallback == VectorPreviewFallback::Masks);
  layer.clear_vector_mask();
  LayerDropShadow shadow;
  shadow.enabled = true;
  layer.layer_style().drop_shadows.push_back(shadow);
  CHECK(build_vector_preview_scene(doc).fallback == VectorPreviewFallback::Effects);
  layer.layer_style() = {};
  const auto scene = build_vector_preview_scene(doc);
  CHECK(render_vector_preview(scene, {{100000, 100000}, 8, {}}).fallback == VectorPreviewFallback::Memory);
  CHECK(render_vector_preview(scene, {{100, 100}, 8, {}}, kVectorPreviewRasterBudget).fallback == VectorPreviewFallback::Memory);
  CHECK(render_vector_preview(scene, {{100, 100}, 8, {}}, std::numeric_limits<std::uint64_t>::max()).fallback == VectorPreviewFallback::Memory);
  CHECK(render_vector_preview(scene, {{100, 100}, std::numeric_limits<double>::infinity(), {}}).fallback == VectorPreviewFallback::Coordinates);
  auto huge = sample();
  auto& shape_layer_ref = huge.layers()[1].children()[0];
  auto huge_shape = *std::as_const(shape_layer_ref).vector_shape();
  huge_shape.path.subpaths[0].anchors[0].in_x = 1e12;
  shape_layer_ref.set_vector_shape(std::move(huge_shape));
  CHECK(render_vector_preview(build_vector_preview_scene(huge), {{100, 100}, 8, {}}).fallback == VectorPreviewFallback::Coordinates);
  std::atomic_bool cancelled{true};
  CHECK(render_vector_preview(scene, {{100, 100}, 8, {}}, 0, &cancelled).image.isNull());
}

void ui_vector_preview_canvas_cache_invalidation_and_lifetime() {
  auto doc = sample();
  const auto bytes = psd::DocumentIo::write_layered_rgb8(doc);
  const auto revision = std::as_const(doc).layers()[0].render_revision();
  CanvasWidget canvas;
  canvas.resize(620, 520);
  canvas.set_document(&doc);
  canvas.show();
  canvas.set_zoom_centered(8);
  canvas.center_document_in_view();
  canvas.set_tool(CanvasTool::Pan);
  canvas.set_vector_preview_enabled(true);
  settle(canvas);
  CHECK(canvas.vector_preview_status().contains(QStringLiteral("sharp vector view")));
  const auto first = canvas.grab().toImage();
  const auto runs = canvas.render_cache_diagnostics().vector_preview_renders;
  for (int i = 0; i < 4; ++i) { (void)canvas.grab(); }
  CHECK(canvas.render_cache_diagnostics().vector_preview_renders == runs);
  CHECK(std::as_const(doc).layers()[0].render_revision() == revision);
  CHECK(psd::DocumentIo::write_layered_rgb8(doc) == bytes);
  for (const double zoom : {4.0, 11.0, 6.375, 128.0, 8.0}) {
    canvas.set_zoom_centered(zoom);
    (void)canvas.grab();
  }
  canvas.center_document_in_view();
  settle(canvas);
  CHECK(canvas.grab().toImage() == first);
  canvas.set_tiling_preview_enabled(true);
  (void)canvas.grab();
  CHECK(canvas.vector_preview_status().contains(QStringLiteral("alternate canvas views")));
  canvas.set_tiling_preview_enabled(false);
  settle(canvas);
  canvas.set_layer_edit_target(CanvasWidget::LayerEditTarget::ComponentRed);
  (void)canvas.grab();
  CHECK(canvas.vector_preview_status().contains(QStringLiteral("alternate canvas views")));
  canvas.set_component_channel_preview(CanvasWidget::LayerEditTarget::Content);
  settle(canvas);
  CHECK(canvas.vector_preview_status().contains(QStringLiteral("sharp vector view")));
  doc.set_active_layer(std::as_const(doc).layers()[1].children()[0].id());
  CHECK(canvas.begin_free_transform());
  (void)canvas.grab();
  CHECK(canvas.vector_preview_status().contains(QStringLiteral("alternate canvas views")));
  canvas.cancel_free_transform();
  settle(canvas);
  auto& raster = doc.add_layer(Layer(doc.allocate_layer_id(), "Raster", PixelBuffer(2, 2, PixelFormat::rgba8())));
  const auto raster_id = raster.id();
  canvas.document_changed();
  settle(canvas);
  CHECK(canvas.vector_preview_status().contains(QStringLiteral("non-vector")));
  doc.find_layer(raster_id)->set_visible(false);
  canvas.document_changed();
  settle(canvas);
  CHECK(canvas.vector_preview_status().contains(QStringLiteral("sharp vector view")));
  canvas.set_zoom_centered(4);
  (void)canvas.grab();
  canvas.set_document(nullptr);
  QApplication::processEvents();
  canvas.set_document(&doc);
  canvas.set_zoom_centered(8);
  settle(canvas);
  auto closing = std::make_unique<CanvasWidget>();
  closing->resize(620, 520);
  closing->set_document(&doc);
  closing->show();
  closing->set_zoom_centered(8);
  closing->set_vector_preview_enabled(true);
  (void)closing->grab();
  closing.reset();
  wait_for_tracked_background_workers();
  QApplication::processEvents();
}

void ui_vector_preview_action_persistence_script_and_history() {
  PreviewPreferenceGuard guard;
  MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& doc = MainWindowTestAccess::document(window);
  doc = sample();
  canvas->set_document(&doc);
  auto* action = window.findChild<QAction*>(QStringLiteral("viewVectorPreviewAction"));
  CHECK(action && !action->isChecked());
  const auto history = MainWindowTestAccess::active_session_undo_depth(window);
  const auto modified = MainWindowTestAccess::active_session_is_modified(window);
  const auto bytes = psd::DocumentIo::write_layered_rgb8(doc);
  ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("vector-preview-test");
  (void)window.script_engine_host().run_source(QStringLiteral(
      "if (!app.runCommand('view.vector_preview')) throw new Error('command'); patchy.ui.zoom = 800;"
      "if (!patchy.ui.captureWindow('test-artifacts/ui_vector_preview_capture.png')) throw new Error('capture');"), options);
  CHECK(process_events_until([&] { return !window.script_engine_host().run_active(); }));
  settle(*canvas);
  CHECK(!window.script_engine_host().last_run_had_error());
  CHECK(!QImage(QStringLiteral("test-artifacts/ui_vector_preview_capture.png")).isNull());
  CHECK(action->isChecked());
  CHECK(app_settings().value(QStringLiteral("view/vectorPreview")).toBool());
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == history);
  CHECK(MainWindowTestAccess::active_session_is_modified(window) == modified);
  CHECK(psd::DocumentIo::write_layered_rgb8(doc) == bytes);
  (void)window.script_engine_host().run_source(QStringLiteral(
      "app.activeDocument.layers[1].children[0].x += 4;"), options);
  CHECK(process_events_until([&] { return !window.script_engine_host().run_active(); }));
  settle(*canvas);
  CHECK(!window.script_engine_host().last_run_had_error());
  const auto moved = psd::DocumentIo::write_layered_rgb8(MainWindowTestAccess::document(window));
  CHECK(moved != bytes);
  MainWindowTestAccess::undo(window);
  settle(*canvas);
  CHECK(psd::DocumentIo::write_layered_rgb8(MainWindowTestAccess::document(window)) == bytes);
  MainWindowTestAccess::redo(window);
  settle(*canvas);
  CHECK(psd::DocumentIo::write_layered_rgb8(MainWindowTestAccess::document(window)) == moved);
  MainWindowTestAccess::create_default_document(window);
  CHECK(require_canvas(window)->vector_preview_enabled());
  MainWindowTestAccess::activate_canvas(window, canvas);
  CHECK(canvas->vector_preview_enabled());
  MainWindow another;
  show_window(another);
  CHECK(require_canvas(another)->vector_preview_enabled());
}

void ui_vector_preview_little_everywhere_if_available() {
  const auto path = patchy::test::local_format_fixture_path("vector-preview", "Little-Everywhere.psd");
  if (!std::filesystem::exists(path)) { return; }
  auto doc = psd::DocumentIo::read_file(path);
  const auto scene = build_vector_preview_scene(doc);
  CHECK(scene.fallback == VectorPreviewFallback::None);
  CanvasWidget canvas;
  canvas.resize(1280, 900);
  canvas.set_document(&doc);
  canvas.show();
  canvas.set_tool(CanvasTool::Pan);
  // Center the captures on actual bench geometry, including at maximum zoom.
  const auto find_bench = [&](const auto& self, const std::vector<Layer>& layers) -> const Layer* {
    for (const auto& layer : layers) {
      if (layer.name() == "Slatted bench") { return &layer; }
      if (const auto* found = self(self, layer.children())) { return found; }
    }
    return nullptr;
  };
  const auto* bench = find_bench(find_bench, std::as_const(doc).layers());
  CHECK(bench != nullptr);
  const auto find_anchor = [&](const auto& self, const Layer& layer) -> std::optional<QPointF> {
    if (const auto* shape = layer.vector_shape()) {
      for (const auto& subpath : shape->path.subpaths) {
        if (!subpath.anchors.empty()) {
          return QPointF(subpath.anchors[0].anchor_x, subpath.anchors[0].anchor_y);
        }
      }
    }
    for (const auto& child : layer.children()) {
      if (auto point = self(self, child)) { return point; }
    }
    return {};
  };
  const auto anchor = find_anchor(find_anchor, *bench);
  CHECK(anchor.has_value());
  canvas.zoom_to_document_rect(QRect(static_cast<int>(anchor->x()) - 10, static_cast<int>(anchor->y()) - 10, 20, 20));
  for (const double zoom : {1.0, 4.0, 8.0, 6.375, 128.0}) {
    canvas.set_vector_preview_enabled(false);
    canvas.set_zoom_centered(zoom);
    settle(canvas);
    save_widget_artifact("vector-preview-little-" + std::to_string(zoom * 100) + "-pixels", canvas);
    canvas.set_vector_preview_enabled(true);
    settle(canvas);
    save_widget_artifact("vector-preview-little-" + std::to_string(zoom * 100) + "-vectors", canvas);
    if (zoom * canvas.devicePixelRatioF() > 1) {
      CHECK(canvas.vector_preview_status().contains(QStringLiteral("sharp vector view")));
      const auto stats = canvas.render_cache_diagnostics();
      CHECK(stats.vector_preview_peak_raster_bytes <= kVectorPreviewRasterBudget);
      std::printf("Vector Preview %.1f%%: %.1f ms, raster reservation %llu bytes\n", zoom * 100,
                  stats.vector_preview_elapsed_ms, static_cast<unsigned long long>(stats.vector_preview_peak_raster_bytes));
    }
  }
}

}  // namespace

std::vector<patchy::test::TestCase> vector_preview_tests() {
  return {
      {"ui_vector_preview_tiles_match_full_vector_raster", ui_vector_preview_tiles_match_full_vector_raster},
      {"ui_vector_preview_strokes_complements_and_group_isolation", ui_vector_preview_strokes_complements_and_group_isolation},
      {"ui_vector_preview_eligibility_and_resource_guards", ui_vector_preview_eligibility_and_resource_guards},
      {"ui_vector_preview_canvas_cache_invalidation_and_lifetime", ui_vector_preview_canvas_cache_invalidation_and_lifetime},
      {"ui_vector_preview_action_persistence_script_and_history", ui_vector_preview_action_persistence_script_and_history},
      {"ui_vector_preview_little_everywhere_if_available", ui_vector_preview_little_everywhere_if_available},
  };
}
