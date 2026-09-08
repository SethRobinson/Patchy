#include "ui_test_support.hpp"
#include "local_psd_fixtures.hpp"

#include "core/layer_metadata.hpp"
#include "core/layer_render_utils.hpp"
#include "core/layer_tree.hpp"
#include "core/vector_live_shapes.hpp"
#include "core/vector_raster.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/layer_merge.hpp"
#include "ui/script_engine.hpp"
#include "ui/vector_preview_renderer.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QPushButton>
#include <QTimer>

#include <cstdio>
#include <utility>

using namespace patchy;
using namespace patchy::ui;
using namespace patchy::test::ui;

namespace {

VectorShapeContent rectangle(double x, double y, double w, double h, RgbColor color = {20, 80, 140}) {
  LiveShapeParams params;
  params.kind = LiveShapeKind::Rectangle;
  params.left = x; params.top = y; params.right = x + w; params.bottom = y + h;
  VectorShapeContent shape;
  shape.path.subpaths = generate_live_shape_subpaths(params);
  shape.fill.color = color;
  return shape;
}

Layer vector_layer(Document& doc, VectorShapeContent shape) {
  Layer result(doc.allocate_layer_id(), "Shape", LayerKind::Pixel);
  result.metadata()[kLayerMetadataVectorShape] = "1";
  result.set_vector_shape(std::move(shape));
  update_vector_shape_raster(result, Rect::from_size(doc.width(), doc.height()), &std::as_const(doc).metadata().patterns);
  return result;
}

Layer pixel_layer(Document& doc, int x, int y, RgbColor color) {
  PixelBuffer pixels(16, 16, PixelFormat::rgba8());
  for (int py = 0; py < 16; ++py) {
    for (int px = 0; px < 16; ++px) {
      auto* p = pixels.pixel(px, py);
      p[0] = color.red; p[1] = color.green; p[2] = color.blue; p[3] = 150;
    }
  }
  Layer layer(doc.allocate_layer_id(), "Bitmap", std::move(pixels));
  layer.set_bounds({x, y, 16, 16});
  return layer;
}

std::vector<LayerId> roots(const Document& doc) {
  std::vector<LayerId> ids;
  for (const auto& layer : doc.layers()) { ids.push_back(layer.id()); }
  return ids;
}

void check_close_images(const QImage& before, const QImage& after, int tolerance = 1) {
  CHECK(!before.isNull() && before.size() == after.size());
  for (int y = 0; y < before.height(); ++y) {
    for (int x = 0; x < before.width(); ++x) {
      const auto a = before.pixelColor(x, y);
      const auto b = after.pixelColor(x, y);
      CHECK(std::abs(a.alpha() - b.alpha()) <= tolerance);
      if (a.alpha() > 1 && b.alpha() > 1) {
        CHECK(std::abs(a.red() - b.red()) <= tolerance);
        CHECK(std::abs(a.green() - b.green()) <= tolerance);
        CHECK(std::abs(a.blue() - b.blue()) <= tolerance);
      }
    }
  }
}

Document grouped_sample() {
  Document doc(128, 96, PixelFormat::rgba8());
  Layer group(doc.allocate_layer_id(), "Mixed group", LayerKind::Group);
  group.set_blend_mode(BlendMode::PassThrough);
  group.set_opacity(0.7F);
  group.add_child(pixel_layer(doc, 3, 4, {180, 20, 30}));
  group.add_child(pixel_layer(doc, 10, 8, {20, 180, 30}));
  group.add_child(vector_layer(doc, rectangle(8, 8, 24, 20)));
  group.add_child(vector_layer(doc, rectangle(20, 18, 24, 20)));
  group.add_child(pixel_layer(doc, 26, 25, {30, 20, 180}));
  group.add_child(vector_layer(doc, rectangle(30, 30, 20, 20, {140, 80, 20})));
  group.add_child(vector_layer(doc, rectangle(40, 40, 20, 20, {140, 80, 20})));
  doc.add_layer(std::move(group));
  return doc;
}

void ui_layer_merge_mixed_group_keeps_vectors_order_opacity_and_psd() {
  const auto doc = grouped_sample();
  const auto original = psd::DocumentIo::write_layered_rgb8(doc);
  const auto revision = doc.layers()[0].content_revision();
  const auto plan = plan_layer_merge(doc, roots(doc));
  CHECK(plan.changed && plan.removed_layers == 3);
  CHECK(plan.vector_layers == 2 && plan.bitmap_layers == 2);
  CHECK(doc.layers()[0].content_revision() == revision);
  CHECK(psd::DocumentIo::write_layered_rgb8(doc) == original);
  const auto result = render_layer_merge(doc, plan);
  CHECK(result.layers().size() == 1 && result.layers()[0].kind() == LayerKind::Group);
  CHECK(result.layers()[0].opacity() == doc.layers()[0].opacity());
  const auto& children = result.layers()[0].children();
  CHECK(children.size() == 4);
  CHECK(!layer_is_vector_shape(children[0]) && layer_is_vector_shape(children[1]));
  CHECK(!layer_is_vector_shape(children[2]) && layer_is_vector_shape(children[3]));
  CHECK(children[1].vector_shape()->path.subpaths.size() == 2);
  check_close_images(qimage_from_document(doc, true), qimage_from_document(result, true));
  const auto reread = psd::DocumentIo::read(psd::DocumentIo::write_layered_rgb8(result));
  CHECK(layer_is_vector_shape(reread.layers()[0].children()[1]));
  CHECK(reread.layers()[0].children()[1].vector_shape()->path.subpaths.size() == 2);
  check_close_images(qimage_from_document(result, true), qimage_from_document(reread, true));
}

void ui_layer_merge_holes_alpha_and_strokes_keep_independent_operations() {
  Document doc(180, 96, PixelFormat::rgba8());
  for (const int x : {8, 94}) {
    auto shape = rectangle(x, 8, 50, 60);
    auto hole = rectangle(x + 12, 20, 20, 28);
    hole.path.subpaths[0].shape_group = 1;
    hole.path.subpaths[0].op = PathCombineOp::Subtract;
    shape.path.subpaths.push_back(hole.path.subpaths[0]);
    shape.stroke.enabled = true;
    shape.stroke.width = 1.25;
    shape.stroke.miter_limit = 2;
    shape.stroke.cap = VectorStrokeCap::Round;
    shape.stroke.join = VectorStrokeJoin::Round;
    shape.stroke.dashes = {2, 1};
    auto layer = vector_layer(doc, shape);
    layer.set_opacity(0.6F);
    doc.add_layer(std::move(layer));
  }
  const auto plan = plan_layer_merge(doc, roots(doc));
  CHECK(plan.changed && plan.vector_layers == 1);
  const auto result = render_layer_merge(doc, plan);
  const auto& shape = *result.layers()[0].vector_shape();
  CHECK(shape.path.subpaths.size() == 4);
  CHECK(shape.path.subpaths[3].op == PathCombineOp::Subtract);
  CHECK(shape.path.subpaths[2].shape_group != shape.path.subpaths[0].shape_group);
  check_close_images(qimage_from_document(doc, true), qimage_from_document(result, true));
  // An independent intersection must not intersect the preceding layer too.
  auto intersect = *std::as_const(doc).layers()[1].vector_shape();
  intersect.path.subpaths[1].op = PathCombineOp::Intersect;
  doc.layers()[1].set_vector_shape(intersect);
  CHECK(!plan_layer_merge(doc, roots(doc)).changed);
  intersect.path_inverted = true;
  doc.layers()[1].set_vector_shape(intersect);
  CHECK(!plan_layer_merge(doc, roots(doc)).changed);
}

void ui_layer_merge_gradients_patterns_and_paint_alignment() {
  for (const auto kind : {VectorFillKind::Gradient, VectorFillKind::Pattern}) {
    Document doc(180, 96, PixelFormat::rgba8());
    PatternResource tile;
    tile.id = "7a5b670b-78fa-4cb8-a617-e13c701588d4";
    tile.name = "Merge texture";
    const auto tile_layer = pixel_layer(doc, 0, 0, {180, 140, 30});
    tile.tile = tile_layer.pixels();
    doc.metadata().patterns.adopt(tile);
    for (const int x : {8, 100}) {
      auto shape = rectangle(x, 8, 50, 60);
      shape.fill.kind = kind;
      shape.fill.gradient.align_with_layer = false;
      shape.fill.gradient.color_stops = {{0, {230, 40, 20}}, {1, {20, 40, 230}}};
      shape.fill.gradient.alpha_stops = {{0, 0.3F}, {1, 0.9F}};
      shape.fill.pattern_id = tile.id;
      shape.fill.pattern_name = tile.name;
      shape.fill.pattern_scale = 0.75;
      shape.fill.pattern_angle_degrees = 25;
      doc.add_layer(vector_layer(doc, shape));
    }
    const auto plan = plan_layer_merge(doc, roots(doc));
    CHECK(plan.changed && plan.vector_layers == 1);
    const auto result = render_layer_merge(doc, plan);
    CHECK(result.layers()[0].vector_shape()->fill.kind == kind);
    check_close_images(qimage_from_document(doc, true), qimage_from_document(result, true));
    const auto reread = psd::DocumentIo::read(psd::DocumentIo::write_layered_rgb8(result));
    CHECK(reread.layers()[0].vector_shape()->fill.kind == kind);
    check_close_images(qimage_from_document(result, true), qimage_from_document(reread, true), 2);
    if (kind == VectorFillKind::Gradient) {
      for (auto& layer : doc.layers()) {
        auto shape = *std::as_const(layer).vector_shape();
        shape.fill.gradient.align_with_layer = true;
        layer.set_vector_shape(shape);
      }
    } else {
      set_layer_effects_reference_point(doc.layers()[1], 10, 4);
    }
    CHECK(!plan_layer_merge(doc, roots(doc)).changed);
  }
}

void ui_layer_merge_group_and_vector_type_choices() {
  Document doc(128, 96, PixelFormat::rgba8());
  for (const int x : {4, 64}) {
    Layer group(doc.allocate_layer_id(), "Folder", LayerKind::Group);
    group.set_blend_mode(BlendMode::PassThrough);
    group.add_child(vector_layer(doc, rectangle(x, 8, 24, 24)));
    group.add_child(vector_layer(doc, rectangle(x + 10, 18, 24, 24)));
    doc.add_layer(std::move(group));
  }
  const auto per_group = render_layer_merge(doc, plan_layer_merge(doc, roots(doc), {true, true, true}));
  CHECK(per_group.layers().size() == 2 && per_group.layers()[0].children().size() == 1);
  const auto across = render_layer_merge(doc, plan_layer_merge(doc, roots(doc), {true, false, true}));
  CHECK(across.layers().size() == 1 && across.layers()[0].vector_shape() != nullptr);
  CHECK(across.layers()[0].vector_shape()->path.subpaths.size() == 4);
  check_close_images(qimage_from_document(doc, true), qimage_from_document(across, true));
  doc.layers()[1].set_opacity(0.5F);
  const auto isolated = render_layer_merge(doc, plan_layer_merge(doc, roots(doc), {true, false, true}));
  CHECK(isolated.layers().size() == 2 && isolated.layers()[1].kind() == LayerKind::Group);
  check_close_images(qimage_from_document(doc, true), qimage_from_document(isolated, true));
  Document colors(96, 80, PixelFormat::rgba8());
  colors.add_layer(vector_layer(colors, rectangle(8, 8, 40, 40, {255, 0, 0})));
  colors.add_layer(vector_layer(colors, rectangle(28, 28, 40, 40, {0, 0, 255})));
  CHECK(!plan_layer_merge(colors, roots(colors)).changed);
  const auto bottom_paint = render_layer_merge(colors, plan_layer_merge(colors, roots(colors), {true, true, false}));
  CHECK(bottom_paint.layers().size() == 1);
  CHECK((bottom_paint.layers()[0].vector_shape()->fill.color == RgbColor{255, 0, 0}));
  const auto bitmap = render_layer_merge(colors, plan_layer_merge(colors, roots(colors), {false, false, true}));
  CHECK(bitmap.layers().size() == 1 && !layer_is_vector_shape(bitmap.layers()[0]));
  check_close_images(qimage_from_document(colors, true), qimage_from_document(bitmap, true));
}

void ui_layer_merge_protected_layers_and_unselected_order_are_barriers() {
  Document doc(96, 80, PixelFormat::rgba8());
  for (int i = 0; i < 8; ++i) {
    doc.add_layer(vector_layer(doc, rectangle(5 + i * 2, 5 + i * 2, 40, 40)));
  }
  doc.layers()[0].set_visible(false);
  doc.layers()[1].set_lock_flags(kLayerLockImagePixels);
  LayerMask mask;
  mask.default_color = 200;
  mask.pixels = PixelBuffer(1, 1, PixelFormat::gray8());
  mask.bounds = {0, 0, 1, 1};
  doc.layers()[2].set_mask(mask);
  doc.layers()[3].layer_style().drop_shadows.push_back({true});
  doc.layers()[4].set_blend_mode(BlendMode::Multiply);
  doc.layers()[6].set_clipped(true);
  const auto bytes = psd::DocumentIo::write_layered_rgb8(doc);
  CHECK(!plan_layer_merge(doc, roots(doc)).changed);
  CHECK(psd::DocumentIo::write_layered_rgb8(doc) == bytes);
  Document ordered(96, 80, PixelFormat::rgba8());
  ordered.add_layer(vector_layer(ordered, rectangle(8, 8, 40, 40)));
  ordered.add_layer(pixel_layer(ordered, 24, 24, {230, 20, 20}));
  ordered.add_layer(vector_layer(ordered, rectangle(28, 28, 40, 40)));
  const auto ids = roots(ordered);
  CHECK(!plan_layer_merge(ordered, {ids[0], ids[2]}).changed);
  Document separated(180, 80, PixelFormat::rgba8());
  separated.add_layer(vector_layer(separated, rectangle(8, 8, 20, 20)));
  separated.add_layer(vector_layer(separated, rectangle(78, 8, 20, 20, {230, 20, 20})));
  separated.add_layer(vector_layer(separated, rectangle(140, 8, 20, 20)));
  const auto collected = plan_layer_merge(separated, roots(separated));
  CHECK(collected.changed && collected.vector_layers == 2);
  const auto collected_doc = render_layer_merge(separated, collected);
  check_close_images(qimage_from_document(separated, true), qimage_from_document(collected_doc, true));
  Layer protected_group(doc.allocate_layer_id(), "Locked contents", LayerKind::Group);
  protected_group.set_blend_mode(BlendMode::PassThrough);
  protected_group.add_child(doc.layers()[1]);
  Document protected_doc(96, 80, PixelFormat::rgba8());
  protected_doc.add_layer(std::move(protected_group));
  CHECK(!plan_layer_merge(protected_doc, roots(protected_doc), {false, false, true}).changed);
  Document unparsed(96, 80, PixelFormat::rgba8());
  auto unknown_vector = pixel_layer(unparsed, 0, 0, {});
  unknown_vector.metadata()[kLayerMetadataVectorLock] = "unparsed";
  unparsed.add_layer(std::move(unknown_vector));
  unparsed.add_layer(pixel_layer(unparsed, 8, 8, {}));
  CHECK(merge_selection_contains_vectors(unparsed, roots(unparsed)));
  CHECK(!plan_layer_merge(unparsed, roots(unparsed)).changed);
  Document custom(96, 80, PixelFormat::rgba8());
  custom.add_layer(vector_layer(custom, rectangle(8, 8, 40, 40)));
  auto opaque = rectangle(20, 20, 40, 40);
  LiveShapeParams annotation;
  annotation.kind = LiveShapeKind::Custom;
  annotation.raw_descriptor = {1, 2, 3};
  opaque.origination.push_back(annotation);
  custom.add_layer(vector_layer(custom, opaque));
  // Opaque annotation payloads remain on their original layer and group id.
  CHECK(!plan_layer_merge(custom, roots(custom)).changed);
  CHECK(std::as_const(custom).layers()[1].vector_shape()->origination[0].raw_descriptor == annotation.raw_descriptor);
}

void run_merge_script(MainWindow& window, const QString& script) {
  ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("merge-layers-test");
  (void)window.script_engine_host().run_source(script, options);
  CHECK(process_events_until([&] { return !window.script_engine_host().run_active(); }));
  CHECK(!window.script_engine_host().last_run_had_error());
}

void ui_layer_merge_dialog_cancel_accept_and_history() {
  MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& doc = MainWindowTestAccess::document(window);
  doc = grouped_sample();
  doc.set_active_layer(std::as_const(doc).layers()[0].id());
  canvas->set_document(&doc);
  MainWindowTestAccess::refresh_layer_ui(window);
  canvas->set_zoom_centered(4.0);
  canvas->set_vector_preview_enabled(true);
  CHECK(process_events_until([&] { return canvas->render_settled(); }));
  const auto bytes = psd::DocumentIo::write_layered_rgb8(doc);
  const auto history = MainWindowTestAccess::active_session_undo_depth(window);
  const auto modified = MainWindowTestAccess::active_session_is_modified(window);
  for (const bool accept : {false, true}) {
    bool saw = false;
    QTimer::singleShot(0, [&] {
      try {
        auto* dialog = find_top_level_dialog(QStringLiteral("mergeLayersDialog"));
        CHECK(dialog != nullptr);
        CHECK(dialog->findChild<QCheckBox*>(QStringLiteral("mergeKeepVectorsCheck"))->isChecked());
        CHECK(!dialog->findChild<QCheckBox*>(QStringLiteral("mergeWithinGroupsCheck"))->isChecked());
        dialog->findChild<QCheckBox*>(QStringLiteral("mergeWithinGroupsCheck"))->setChecked(true);
        CHECK(dialog->findChild<QCheckBox*>(QStringLiteral("mergeSeparateVectorTypesCheck"))->isChecked());
        CHECK(dialog->findChild<QLabel*>(QStringLiteral("mergeLayersSummaryLabel"))->text().contains(QStringLiteral("2 vector layers")));
        CHECK(psd::DocumentIo::write_layered_rgb8(doc) == bytes);
        CHECK(MainWindowTestAccess::active_session_undo_depth(window) == history);
        save_widget_artifact("ui_layer_merge_dialog", *dialog);
        saw = true;
        if (accept) { dialog->accept(); } else { dialog->reject(); }
      } catch (...) {
        (void)unwind_non_modal_dialog_loop(std::current_exception());
      }
    });
    require_action(window, "layerMergeDownAction")->trigger();
    CHECK(saw);
    if (!accept) {
      CHECK(psd::DocumentIo::write_layered_rgb8(doc) == bytes);
      CHECK(MainWindowTestAccess::active_session_is_modified(window) == modified);
      CHECK(MainWindowTestAccess::active_session_undo_depth(window) == history);
    }
  }
  CHECK(std::as_const(doc).layers()[0].children().size() == 4);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == history + 1);
  CHECK(process_events_until([&] { return canvas->render_settled(); }));
  CHECK(canvas->vector_preview_status().contains(QStringLiteral("sharp vector view")));
  const auto merged = psd::DocumentIo::write_layered_rgb8(doc);
  MainWindowTestAccess::undo(window);
  CHECK(process_events_until([&] { return canvas->render_settled(); }));
  CHECK(psd::DocumentIo::write_layered_rgb8(doc) == bytes);
  MainWindowTestAccess::redo(window);
  CHECK(process_events_until([&] { return canvas->render_settled(); }));
  CHECK(psd::DocumentIo::write_layered_rgb8(doc) == merged);
}

void ui_layer_merge_script_validation_noop_and_undo() {
  MainWindow window;
  show_window(window);
  auto& doc = MainWindowTestAccess::document(window);
  doc = grouped_sample();
  require_canvas(window)->set_document(&doc);
  const auto bytes = psd::DocumentIo::write_layered_rgb8(doc);
  const auto history = MainWindowTestAccess::active_session_undo_depth(window);
  run_merge_script(window, QStringLiteral(R"JS(
    const doc = app.activeDocument;
    const group = doc.layers[0];
    for (const options of [{bogus:true}, {keepVectors:1}, null, [], false]) {
      let threw = false;
      try { doc.mergeLayers([group], options); } catch (e) { threw = true; }
      if (!threw) throw new Error('invalid options accepted');
    }
    for (const layers of [[], [null], [group, null]]) {
      let threw = false;
      try { doc.mergeLayers(layers); } catch (e) { threw = true; }
      if (!threw) throw new Error('invalid layers accepted');
    }
    const result = doc.mergeLayers([group.children[2]]);
    if (result.length !== 1) throw new Error('single layer');
  )JS"));
  CHECK(psd::DocumentIo::write_layered_rgb8(doc) == bytes);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == history);
  run_merge_script(window, QStringLiteral(R"JS(
    const doc = app.activeDocument;
    const result = doc.mergeLayers([doc.layers[0]], {keepVectors:true, withinGroups:true, separateVectorTypes:true});
    if (result.length !== 4) throw new Error('wrong output count');
    if (doc.layers[0].children.length !== 4) throw new Error('folder was flattened');
  )JS"));
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == history + 1);
  MainWindowTestAccess::undo(window);
  CHECK(psd::DocumentIo::write_layered_rgb8(doc) == bytes);
}

void ui_layer_merge_little_everywhere_if_available() {
  const auto path = patchy::test::local_format_fixture_path("vector-preview", "Little-Everywhere.psd");
  if (!std::filesystem::exists(path)) { return; }
  const auto doc = psd::DocumentIo::read_file(path);
  QElapsedTimer timer;
  timer.start();
  const auto plan = plan_layer_merge(doc, roots(doc));
  const auto planning_ms = timer.elapsed();
  std::printf("Little-Everywhere plan: %zu removals, %zu vectors, %zu bitmaps, %zu other layers\n",
              plan.removed_layers, plan.vector_layers, plan.bitmap_layers, plan.kept_layers);
  CHECK(plan.changed && plan.removed_layers > 100);
  const auto merged = render_layer_merge(doc, plan);
  CHECK(plan.bitmap_layers == 0 && plan.vector_layers > 0);
  CHECK(layer_tree_count(merged.layers()) + plan.removed_layers == layer_tree_count(doc.layers()));
  std::printf("Little-Everywhere merge: %zu -> %zu layers; %zu vectors; planning %lld ms; total %lld ms\n",
              layer_tree_count(doc.layers()), layer_tree_count(merged.layers()), plan.vector_layers,
              static_cast<long long>(planning_ms), static_cast<long long>(timer.elapsed()));
  const auto before_scene = build_vector_preview_scene(doc);
  const auto after_scene = build_vector_preview_scene(merged);
  const auto find_bench = [&](const auto& self, const std::vector<Layer>& layers) -> const Layer* {
    for (const auto& layer : layers) {
      if (layer.name() == "Slatted bench") { return &layer; }
      if (const auto* child = self(self, layer.children())) { return child; }
    }
    return nullptr;
  };
  const auto* bench = find_bench(find_bench, doc.layers());
  CHECK(bench != nullptr);
  const auto bounds = layer_render_bounds(*bench);
  const QPointF center(bounds.x + bounds.width * 0.5, bounds.y + bounds.height * 0.5);
  for (const auto& view : {VectorPreviewView{{doc.width(), doc.height()}, 1.0, {}},
                           VectorPreviewView{{1200, 900}, 4.0, QPointF(600, 450) - center * 4.0}}) {
    const auto before = render_vector_preview(before_scene, view);
    const auto after = render_vector_preview(after_scene, view);
    CHECK(before.fallback == VectorPreviewFallback::None && after.fallback == VectorPreviewFallback::None);
    const auto prefix = QStringLiteral("test-artifacts/layer-merge-little-%1").arg(view.scale);
    CHECK(before.image.save(prefix + QStringLiteral("-before.png")));
    CHECK(after.image.save(prefix + QStringLiteral("-after.png")));
    check_close_images(before.image, after.image, 2);
  }
  const auto reread = psd::DocumentIo::read(psd::DocumentIo::write_layered_rgb8(merged));
  const auto restored = plan_layer_merge(reread, roots(reread));
  CHECK(restored.vector_layers == plan.vector_layers && restored.bitmap_layers == 0);
}

}  // namespace

std::vector<patchy::test::TestCase> layer_merge_tests() {
  return {
      {"ui_layer_merge_mixed_group_keeps_vectors_order_opacity_and_psd", ui_layer_merge_mixed_group_keeps_vectors_order_opacity_and_psd},
      {"ui_layer_merge_holes_alpha_and_strokes_keep_independent_operations", ui_layer_merge_holes_alpha_and_strokes_keep_independent_operations},
      {"ui_layer_merge_gradients_patterns_and_paint_alignment", ui_layer_merge_gradients_patterns_and_paint_alignment},
      {"ui_layer_merge_group_and_vector_type_choices", ui_layer_merge_group_and_vector_type_choices},
      {"ui_layer_merge_protected_layers_and_unselected_order_are_barriers", ui_layer_merge_protected_layers_and_unselected_order_are_barriers},
      {"ui_layer_merge_dialog_cancel_accept_and_history", ui_layer_merge_dialog_cancel_accept_and_history},
      {"ui_layer_merge_script_validation_noop_and_undo", ui_layer_merge_script_validation_noop_and_undo},
      {"ui_layer_merge_little_everywhere_if_available", ui_layer_merge_little_everywhere_if_available},
  };
}
