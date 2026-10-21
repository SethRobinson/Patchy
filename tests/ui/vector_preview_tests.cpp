#include <QStatusBar>
#include "ui/canvas_widget.hpp"
#include "formats/acv_curves_io.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/ico_document_io.hpp"
#include "formats/tga_document_io.hpp"
#include "ui/image_document_io.hpp"
#include "ui/main_window.hpp"
#include "ui/app_settings.hpp"
#include "psd/psd_document_io.hpp"
#include "synthetic_dng.hpp"
#include "test_harness.hpp"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QImage>
#include <QTimer>
#include <QVariant>
#include <algorithm>
#include <atomic>
#include <array>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include "ui_test_access.hpp"
#include "ui_test_support.hpp"
#include "local_psd_fixtures.hpp"
#include "core/artboard.hpp"
#include "core/vector_live_shapes.hpp"
#include "core/vector_raster.hpp"
#include "core/vector_shape.hpp"
#include "core/adjustment_layer.hpp"
#include "ui/background_workers.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/script_engine.hpp"
#include "ui/vector_preview_renderer.hpp"
#include "ui/canvas_widget_shared.hpp"
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
  CHECK(build_vector_preview_scene(doc).fallback == VectorPreviewFallback::None);
  raster.set_visible(false);
  CHECK(build_vector_preview_scene(doc).fallback == VectorPreviewFallback::None);
  auto& layer = doc.layers().front();
  layer.set_clipped(true);
  CHECK(build_vector_preview_scene(doc).fallback == VectorPreviewFallback::None);
  layer.set_clipped(false);
  auto changed = *std::as_const(layer).vector_shape();
  changed.fill.kind = VectorFillKind::Gradient;
  layer.set_vector_shape(changed);
  CHECK(build_vector_preview_scene(doc).fallback == VectorPreviewFallback::None);
  changed.fill.kind = VectorFillKind::Solid;
  layer.set_vector_shape(changed);
  layer.set_vector_mask(LayerVectorMask{});
  CHECK(build_vector_preview_scene(doc).fallback == VectorPreviewFallback::None);
  layer.clear_vector_mask();
  LayerDropShadow shadow;
  shadow.enabled = true;
  layer.layer_style().drop_shadows.push_back(shadow);
  CHECK(build_vector_preview_scene(doc).fallback == VectorPreviewFallback::None);
  layer.layer_style().drop_shadows[0].size = 1e12F;
  layer.layer_style().drop_shadows[0].enabled = false;
  CHECK(render_vector_preview(build_vector_preview_scene(doc), {{100, 100}, 4, {}}).fallback == VectorPreviewFallback::None);
  layer.layer_style().drop_shadows[0].enabled = true;
  layer.layer_style().effects_visible = false;
  CHECK(render_vector_preview(build_vector_preview_scene(doc), {{100, 100}, 4, {}}).fallback == VectorPreviewFallback::None);
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

Document mixed_sample() {
  auto doc = sample();
  PixelBuffer pixels(40, 35, PixelFormat::rgba8());
  for (int y = 0; y < pixels.height(); ++y) {
    for (int x = 0; x < pixels.width(); ++x) {
      auto* p = pixels.pixel(x, y);
      p[0] = ((x / 3 + y / 3) % 2) ? 220 : 40;
      p[1] = 180; p[2] = 120; p[3] = 180;
    }
  }
  Layer raster(doc.allocate_layer_id(), "Bitmap", pixels);
  raster.set_bounds({24, 23, pixels.width(), pixels.height()});
  raster.set_blend_mode(BlendMode::Multiply);
  doc.add_layer(std::move(raster));
  auto& group = doc.layers()[1];
  PixelBuffer mask(45, 50, PixelFormat::gray8());
  mask.clear(200);
  group.set_mask(LayerMask{{20, 10, 45, 50}, std::move(mask), 30, false});
  auto& child = group.children()[1];
  child.set_clipped(true);
  child.set_blend_mode(BlendMode::Screen);
  LayerBlendIf blend;
  blend.channels[0].underlying_layer.white_low = 210;
  blend.channels[0].underlying_layer.white_high = 250;
  CHECK(child.set_blend_if(blend));
  child.set_restricted_channels(kRestrictBlue);
  Layer adjustment(doc.allocate_layer_id(), "Invert", LayerKind::Adjustment);
  adjustment.metadata()[kLayerMetadataAdjustmentType] = "invert";
  adjustment.set_opacity(0.3F);
  doc.add_layer(std::move(adjustment));
  return doc;
}

void ui_vector_preview_mixed_content_matches_compositor() {
  auto doc = mixed_sample();
  const auto original = psd::DocumentIo::write_layered_rgb8(doc);
  const auto scene = build_vector_preview_scene(doc);
  CHECK(scene.fallback == VectorPreviewFallback::None);
  CHECK(scene.has_vectors);
  const auto native = render_vector_preview(scene, {{doc.width(), doc.height()}, 1, {}});
  CHECK(native.fallback == VectorPreviewFallback::None);
  CHECK(native.image == qimage_from_document(doc, true));
  const VectorPreviewView full{{700, 600}, 4.25, {17.375, 20.625}};
  const auto a = render_vector_preview(scene, full);
  const auto b = render_vector_preview(scene, {{400, 330}, full.scale, full.offset - QPointF(211, 189)});
  CHECK(a.fallback == VectorPreviewFallback::None);
  CHECK(b.fallback == VectorPreviewFallback::None);
  CHECK(b.image == a.image.copy(211, 189, 400, 330));
  CHECK(a.image.save(QStringLiteral("test-artifacts/vector-preview-mixed.png")));
  CHECK(psd::DocumentIo::write_layered_rgb8(doc) == original);
}

void ui_vector_preview_gradient_pattern_effects_and_vector_masks() {
  for (int variant = 0; variant < 18; ++variant) {
    auto doc = sample();
    auto& group = doc.layers()[1];
    auto& layer = group.children()[0];
    auto shape = *std::as_const(layer).vector_shape();
    LayerStyleGradient gradient;
    gradient.color_stops = {{0, {240, 40, 20}}, {1, {10, 150, 240}}};
    gradient.alpha_stops = {{0, 1}, {1, 0.6F}};
    gradient.angle_degrees = 27;
    if (variant == 0) { shape.fill.kind = VectorFillKind::Gradient; shape.fill.gradient = gradient; }
    if (variant == 1) {
      PixelBuffer pattern(3, 3, PixelFormat::rgba8());
      pattern.clear(255);
      pattern.pixel(1, 1)[0] = 0;
      doc.metadata().patterns.adopt({"preview-pattern", "Pattern", std::move(pattern)});
      shape.fill.kind = VectorFillKind::Pattern;
      shape.fill.pattern_id = "preview-pattern";
      shape.fill.pattern_scale = 2;
      shape.fill.pattern_phase_x = 1.25;
      set_layer_effects_reference_point(layer, 11, 7);
    }
    if (variant == 2) {
      LayerVectorMask mask;
      mask.path = ellipse(20, 10, 60, 58, {}).path;
      mask.density = 220;
      group.set_vector_mask(std::move(mask));
      update_vector_mask_raster(group, {0, 0, doc.width(), doc.height()});
    }
    if (variant == 3) {
      LayerDropShadow shadow;
      shadow.enabled = true; shadow.size = 2; shadow.distance = 3;
      layer.layer_style().drop_shadows.push_back(shadow);
    }
    if (variant == 4) {
      LayerGradientFill fill;
      fill.enabled = true; fill.gradient = gradient;
      layer.layer_style().gradient_fills.push_back(fill);
    }
    if (variant == 5) {
      LayerStroke stroke;
      stroke.enabled = true; stroke.size = 2;
      stroke.uses_gradient = true; stroke.gradient = gradient;
      layer.layer_style().strokes.push_back(stroke);
    }
    if (variant == 6) {
      LayerBevelEmboss bevel;
      bevel.enabled = true; bevel.size = 2;
      layer.layer_style().bevels.push_back(bevel);
    }
    if (variant == 7) {
      LayerDropShadow shadow;
      shadow.enabled = true; shadow.size = 2; shadow.distance = 3;
      group.layer_style().drop_shadows.push_back(shadow);
    }
    if (variant == 8) {
      LayerVectorMask mask;
      mask.path = ellipse(20, 10, 60, 58, {}).path;
      mask.feather = 1.25;
      mask.inverted = true;
      group.set_vector_mask(std::move(mask));
      update_vector_mask_raster(group, {0, 0, doc.width(), doc.height()});
    }
    if (variant == 9) {
      LayerInnerGlow inner;
      inner.enabled = true; inner.size = 2;
      layer.layer_style().inner_glows.push_back(inner);
      LayerOuterGlow outer;
      outer.enabled = true; outer.size = 2;
      layer.layer_style().outer_glows.push_back(outer);
    }
    if (variant == 10) {
      LayerSatin satin;
      satin.enabled = true; satin.size = 2; satin.distance = 2;
      layer.layer_style().satins.push_back(satin);
    }
    if (variant == 11) {
      LayerGradientFill fill;
      fill.enabled = true; fill.gradient = gradient;
      group.layer_style().gradient_fills.push_back(fill);
      PixelBuffer mask(30, 25, PixelFormat::gray8());
      mask.clear(255);
      layer.set_mask(LayerMask{{30, 30, 30, 25}, std::move(mask), 0, false});
    }
    if (variant == 12) {
      PixelBuffer pattern(3, 3, PixelFormat::rgba8());
      pattern.clear(255);
      pattern.pixel(1, 1)[1] = 0;
      doc.metadata().patterns.adopt({"overlay-pattern", "Overlay", std::move(pattern)});
      LayerPatternOverlay overlay;
      overlay.enabled = true; overlay.pattern_id = "overlay-pattern";
      overlay.scale = 2; overlay.phase_x = 1.25F; overlay.phase_y = -3;
      layer.layer_style().pattern_overlays.push_back(overlay);
      set_layer_effects_reference_point(layer, 11, 7);
    }
    if (variant == 13) {
      LayerInnerGlow glow;
      glow.enabled = true; glow.size = 2;
      doc.layers()[0].layer_style().inner_glows.push_back(glow);
    }
    if (variant == 14 || variant == 15) {
      LayerStroke stroke;
      stroke.enabled = true; stroke.size = 2;
      stroke.uses_gradient = true; stroke.gradient = gradient;
      stroke.gradient.align_with_layer = false;
      layer.layer_style().strokes.push_back(stroke);
      if (variant == 15) {
        LayerBevelEmboss bevel;
        bevel.enabled = true; bevel.size = 2; bevel.style = BevelEmbossStyleKind::StrokeEmboss;
        layer.layer_style().bevels.push_back(bevel);
      }
    }
    if (variant == 16 || variant == 17) {
      gradient.alpha_stops = {{0, 0}, {0.5F, 0}, {1, 1}};
      auto& paint = variant == 16 ? shape.fill : shape.stroke.content;
      paint.kind = VectorFillKind::Gradient;
      paint.gradient = gradient;
    }
    layer.set_vector_shape(std::move(shape));
    update_vector_shape_raster(layer, {0, 0, doc.width(), doc.height()}, &std::as_const(doc).metadata().patterns);
    const auto scene = build_vector_preview_scene(doc);
    const auto native = render_vector_preview(scene, {{doc.width(), doc.height()}, 1, {}});
    CHECK(native.fallback == VectorPreviewFallback::None);
    const auto expected = qimage_from_document(doc, true);
    if (native.image != expected) {
      (void)native.image.save(QStringLiteral("test-artifacts/vector-preview-variant-%1-actual.png").arg(variant));
      (void)expected.save(QStringLiteral("test-artifacts/vector-preview-variant-%1-expected.png").arg(variant));
      throw std::runtime_error("native mixed variant " + std::to_string(variant));
    }
    const VectorPreviewView full{{530, 460}, 4, {}};
    const auto a = render_vector_preview(scene, full);
    const auto b = render_vector_preview(scene, {{310, 280}, 4, {-197, -173}});
    CHECK(a.fallback == VectorPreviewFallback::None);
    CHECK(b.fallback == VectorPreviewFallback::None);
    if (b.image != a.image.copy(197, 173, 310, 280)) {
      (void)b.image.save(QStringLiteral("test-artifacts/vector-preview-variant-%1-crop.png").arg(variant));
      (void)a.image.save(QStringLiteral("test-artifacts/vector-preview-variant-%1-full.png").arg(variant));
      throw std::runtime_error("tile mixed variant " + std::to_string(variant));
    }
  }
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
  CHECK(canvas.vector_preview_status().contains(QStringLiteral("sharp vector view")));
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
  CHECK(action->text() == QStringLiteral("Dynamic Vector Preview"));
  const auto history = MainWindowTestAccess::active_session_undo_depth(window);
  const auto modified = MainWindowTestAccess::active_session_is_modified(window);
  const auto bytes = psd::DocumentIo::write_layered_rgb8(doc);
  window.statusBar()->showMessage(QStringLiteral("Keep this editing message"));
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
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Keep this editing message"));
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
  auto* restored_action = require_action(another, "viewVectorPreviewAction");
  CHECK(restored_action->isChecked());
  // The first click after loading an enabled preference must turn it off.
  restored_action->trigger();
  CHECK(!restored_action->isChecked());
  CHECK(!require_canvas(another)->vector_preview_enabled());
  restored_action->trigger();
  CHECK(require_canvas(another)->vector_preview_enabled());

  bool saw_preferences = false;
  QTimer::singleShot(0, [&] {
    try {
      auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
      CHECK(dialog != nullptr);
      auto* check = dialog->findChild<QCheckBox*>(QStringLiteral("preferencesDynamicVectorPreviewCheck"));
      CHECK(check && check->isChecked());
      check->setChecked(false);
      saw_preferences = true;
      dialog->accept();
    } catch (...) {
      (void)unwind_non_modal_dialog_loop(std::current_exception());
    }
  });
  require_action(another, "filePreferencesAction")->trigger();
  CHECK(saw_preferences);
  CHECK(!restored_action->isChecked());
  CHECK(!require_canvas(another)->vector_preview_enabled());
  CHECK(!app_settings().value(QStringLiteral("view/vectorPreview")).toBool());
}

void ui_vector_preview_little_everywhere_if_available() {
  const auto path = patchy::test::local_format_fixture_path("vector-preview", "Little-Everywhere.psd");
  patchy::test::skip_unless_exists(path);
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

void ui_vector_preview_resource_notice_is_once_and_never_covers_other_status() {
  PreviewPreferenceGuard guard;
  MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& doc = MainWindowTestAccess::document(window);
  doc = sample();
  auto& layer = doc.layers()[1].children()[0];
  auto shape = *std::as_const(layer).vector_shape();
  shape.path.subpaths[0].anchors[0].in_x = 1e12;
  layer.set_vector_shape(std::move(shape));
  canvas->set_document(&doc);
  canvas->set_zoom_centered(8);
  window.statusBar()->clearMessage();
  require_action(window, "viewVectorPreviewAction")->trigger();
  settle(*canvas);
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("preview range")));
  window.statusBar()->clearMessage();
  for (const double zoom : {4.0, 8.0, 6.375}) {
    canvas->set_zoom_centered(zoom);
    settle(*canvas);
    CHECK(window.statusBar()->currentMessage().isEmpty());
  }
  window.statusBar()->showMessage(QStringLiteral("Another tool's message"));
  canvas->set_vector_preview_enabled(false);
  canvas->set_vector_preview_enabled(true);
  settle(*canvas);
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Another tool's message"));
}

// With Dynamic Vector Preview on, View > Pixel Grid vanished (the preview frame covered
// the composite's deep-zoom raster, grid included) and only flashed during zooms. The grid
// must show above the sharp view at every deep zoom, mid-zoom while the old frame is
// mapped, and across preview toggles; guides, the document grid and selection edges stay
// above it too (docs/vector-preview.md, "Overlay order").
struct InteriorGridCheck {
  int mismatches{0};  // fill or line color where the other was expected
  int line_pixels{0};
  int other{0};       // neither color: an overlay drawn over the interior
};

InteriorGridCheck interior_grid_check(CanvasWidget& canvas, QRgb fill, bool grid) {
  QApplication::processEvents();
  const auto image = canvas.grab().toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
  const auto ratio = canvas.devicePixelRatioF();
  const auto device_x = [&](int x) {
    return qRound(canvas.widget_position_for_document_point(QPoint(x, 0)).x() * ratio);
  };
  const auto device_y = [&](int y) {
    return qRound(canvas.widget_position_for_document_point(QPoint(0, y)).y() * ratio);
  };
  const auto content = canvas_content_rect(canvas);
  const QRect device_content(qRound(content.left() * ratio), qRound(content.top() * ratio),
                             qRound(content.width() * ratio), qRound(content.height() * ratio));
  const auto inside = device_content.intersected(image.rect());
  const auto line = pixel_grid_line_color(fill);
  InteriorGridCheck result;
  // Document cells well inside the 4..20 x 3..15 rectangle, away from its edges.
  for (int y = 5; y < 13; ++y) {
    for (int x = 6; x < 18; ++x) {
      for (int dy = device_y(y); dy < device_y(y + 1); ++dy) {
        for (int dx = device_x(x); dx < device_x(x + 1); ++dx) {
          if (!inside.contains(dx, dy)) { continue; }
          const bool on_line = grid && (dx == device_x(x) || dy == device_y(y));
          const auto shown = image.pixel(dx, dy) | 0xff000000U;
          if (on_line) { ++result.line_pixels; }
          if (shown == (on_line ? line : fill)) { continue; }
          if (shown == line || shown == fill) { ++result.mismatches; } else { ++result.other; }
        }
      }
    }
  }
  return result;
}

void ui_vector_preview_keeps_pixel_grid_and_overlays_above() {
  Document doc(24, 18, PixelFormat::rgba8());
  VectorShapeContent paper;
  paper.fill.color = {244, 241, 233};
  doc.add_layer(shape_layer(doc, paper));
  LiveShapeParams params;
  params.kind = LiveShapeKind::Rectangle;
  params.left = 4; params.top = 3; params.right = 20; params.bottom = 15;
  VectorShapeContent box;
  box.path.subpaths = generate_live_shape_subpaths(params);
  box.fill.color = {40, 140, 190};
  doc.add_layer(shape_layer(doc, box));
  const auto fill = qRgb(40, 140, 190);

  CanvasWidget canvas;
  canvas.resize(320, 240);
  canvas.set_document(&doc);
  canvas.show();
  canvas.set_tool(CanvasTool::Pan);
  canvas.set_pixel_grid_visible(true);
  canvas.set_vector_preview_enabled(false);
  canvas.set_view_zoom_centered(8.0);
  settle(canvas);
  const auto report = [&](const char* when, const InteriorGridCheck& check) {
    std::cerr << when << ": " << check.line_pixels << " line pixels, " << check.mismatches << " mismatches, "
              << check.other << " other\n";
  };
  auto check = interior_grid_check(canvas, fill, true);
  report("preview off", check);
  CHECK(check.line_pixels > 0 && check.mismatches == 0 && check.other == 0);

  canvas.set_vector_preview_enabled(true);
  settle(canvas);
  CHECK(canvas.vector_preview_status().contains(QStringLiteral("sharp vector view")));
  check = interior_grid_check(canvas, fill, true);
  report("preview on", check);
  save_image_artifact("ui_vector_preview_pixel_grid_800", canvas.grab().toImage());
  CHECK(check.line_pixels > 0 && check.mismatches == 0 && check.other == 0);

  // Mid-zoom (the old frame mapped to the new view, or the new one landed): grid either way.
  for (const double zoom : {12.0, 6.0, 9.0}) {
    canvas.set_view_zoom_centered(zoom);
    check = interior_grid_check(canvas, fill, true);
    report("zoom step", check);
    CHECK(check.line_pixels > 0 && check.mismatches == 0 && check.other == 0);
    settle(canvas);
    CHECK(canvas.vector_preview_status().contains(QStringLiteral("sharp vector view")));
    check = interior_grid_check(canvas, fill, true);
    report("zoom settled", check);
    CHECK(check.line_pixels > 0 && check.mismatches == 0 && check.other == 0);
  }
  // Below 600% the grid stays off with the preview on, as without it.
  canvas.set_view_zoom_centered(5.0);
  settle(canvas);
  check = interior_grid_check(canvas, fill, false);
  CHECK(check.mismatches == 0 && check.other == 0);
  canvas.set_view_zoom_centered(8.0);
  settle(canvas);

  // View > Pixel Grid off removes it under the preview too; toggling the preview keeps it.
  canvas.set_pixel_grid_visible(false);
  check = interior_grid_check(canvas, fill, false);
  CHECK(check.mismatches == 0 && check.other == 0);
  canvas.set_pixel_grid_visible(true);
  canvas.set_vector_preview_enabled(false);
  settle(canvas);
  CHECK(interior_grid_check(canvas, fill, true).line_pixels > 0);
  canvas.set_vector_preview_enabled(true);
  settle(canvas);
  CHECK(canvas.vector_preview_status().contains(QStringLiteral("sharp vector view")));
  check = interior_grid_check(canvas, fill, true);
  report("preview toggled back on", check);
  CHECK(check.line_pixels > 0 && check.mismatches == 0 && check.other == 0);

  // The other overlays draw above the preview: each adds pixels of its own color.
  doc.guides().push_back({GuideOrientation::Vertical, 10 * 32});
  canvas.set_guides_visible(true);
  check = interior_grid_check(canvas, fill, true);
  report("guide", check);
  CHECK(check.other > 0);
  canvas.set_guides_visible(false);
  CHECK(interior_grid_check(canvas, fill, true).other == 0);

  doc.grid_settings().horizontal_cycle_32 = 8 * 32;
  doc.grid_settings().vertical_cycle_32 = 8 * 32;
  canvas.set_grid_subdivisions(2);
  canvas.set_grid_visible(true);
  check = interior_grid_check(canvas, fill, true);
  report("document grid", check);
  CHECK(check.other > 0);
  canvas.set_grid_visible(false);
  CHECK(interior_grid_check(canvas, fill, true).other == 0);

  QImage alpha(6, 4, QImage::Format_Grayscale8);
  alpha.fill(255);
  canvas.apply_automatic_selection_mask(QRect(8, 7, 6, 4), alpha, SelectionMode::Replace, QStringLiteral("Select"));
  settle(canvas);
  CHECK(canvas.vector_preview_status().contains(QStringLiteral("sharp vector view")));
  check = interior_grid_check(canvas, fill, true);
  report("selection edges", check);
  CHECK(check.other > 0);
  canvas.set_selection_edges_visible(false);
  CHECK(interior_grid_check(canvas, fill, true).other == 0);
}

// Dynamic Vector Preview in an artboard document: the scene keeps each artboard (its rect
// mapped to the view), so the compositor clips the children and paints the paper exactly as
// the document render does at that scale.
void ui_vector_preview_renders_artboards() {
  Document doc(60, 50, PixelFormat::rgba8());
  auto board = make_artboard_group(doc.allocate_layer_id(), "Board", Rect{6, 5, 40, 30}, ArtboardBackground::White);
  // The ellipse runs past the board's right edge, where the artboard clips it.
  board.add_child(shape_layer(doc, ellipse(20.25, 9.5, 34.5, 21.75, {200, 40, 40})));
  doc.layers().push_back(std::move(board));
  doc.add_layer(shape_layer(doc, ellipse(48.5, 36.25, 10, 12, {40, 90, 200})));  // loose, off the board
  doc.metadata().artboard_settings.emplace();
  const auto scene = build_vector_preview_scene(doc);
  CHECK(scene.fallback == VectorPreviewFallback::None);
  CHECK(scene.has_vectors);
  for (const int scale : {1, 4, 8}) {
    auto enlarged = doc;
    resize_image_and_layers(enlarged, doc.width() * scale, doc.height() * scale);
    const auto expected = qimage_from_document(enlarged, true);
    const auto result = render_vector_preview(scene, {expected.size(), static_cast<double>(scale), {}});
    CHECK(result.fallback == VectorPreviewFallback::None);
    CHECK(result.image == expected);
  }
}

// Document cells (zoom x zoom widget squares) that hold both the shape's red and the white
// behind it: a sharp vector edge splits cells, the document-pixel view never does (each
// cell is one color, apart from overlay lines, which are neither color).
int split_edge_cells(const QImage& image, const CanvasWidget& canvas, QSize document_size) {
  int count = 0;
  for (int y = 0; y < document_size.height(); ++y) {
    for (int x = 0; x < document_size.width(); ++x) {
      const auto top_left = canvas.widget_position_for_document_point(QPoint(x, y));
      const auto bottom_right = canvas.widget_position_for_document_point(QPoint(x + 1, y + 1));
      bool red = false;
      bool white = false;
      for (int py = top_left.y() + 2; py < bottom_right.y() - 2; ++py) {
        for (int px = top_left.x() + 2; px < bottom_right.x() - 2; ++px) {
          if (!image.rect().contains(px, py)) {
            continue;
          }
          const auto color = image.pixelColor(px, py);
          red = red || (std::abs(color.red() - 210) <= 30 && color.green() <= 70 && color.blue() <= 70);
          white = white || (color.red() >= 235 && color.green() >= 235 && color.blue() >= 235);
        }
      }
      count += red && white ? 1 : 0;
    }
  }
  return count;
}

// Dynamic Vector Preview through a Free Transform session on a shape layer, in a plain
// document or inside an artboard: the open session (rotated, under Nearest Neighbor too)
// shows the transformed path at view resolution instead of the resampled document pixels,
// and the committed result stays sharp.
void check_free_transform_stays_sharp(bool on_artboard) {
  PreviewPreferenceGuard preference_guard;
  {
    MainWindow window;
    show_window(window);
    Document doc(48, 36, PixelFormat::rgba8());
    LayerId shape_id{};
    auto shape = shape_layer(doc, ellipse(6, 8, 30, 16, {210, 30, 30}));
    shape.metadata()[kLayerMetadataVectorShape] = "1";  // a shape layer, as the shape tools make
    shape_id = shape.id();
    if (on_artboard) {
      auto board = make_artboard_group(doc.allocate_layer_id(), "Board", Rect{0, 0, 48, 36}, ArtboardBackground::White);
      board.add_child(std::move(shape));
      doc.layers().push_back(std::move(board));
      doc.metadata().artboard_settings.emplace();
    } else {
      PixelBuffer white(48, 36, PixelFormat::rgba8());
      white.clear(255);
      doc.add_layer(Layer(doc.allocate_layer_id(), "Background", std::move(white)));
      doc.add_layer(std::move(shape));
    }
    window.add_document_session(std::move(doc), on_artboard ? QStringLiteral("Board") : QStringLiteral("Plain"));
    MainWindowTestAccess::select_layers(window, {shape_id}, shape_id);
    auto* canvas = require_canvas(window);
    auto* preview_action = require_hotkey_action(window, QStringLiteral("view.vector_preview"));
    if (!preview_action->isChecked()) {
      preview_action->trigger();
    }
    CHECK(canvas->vector_preview_enabled());
    canvas->set_zoom(10.0);
    settle(*canvas);
    const QSize size(48, 36);
    const std::string label = on_artboard ? "artboard" : "plain";
    const auto widget_color = [&](QPoint document_point) {
      return canvas->grab().toImage().pixelColor(canvas->widget_position_for_document_point(document_point) + QPoint(5, 5));
    };
    const auto is_red = [](QColor color) { return color.red() >= 180 && color.green() <= 70; };
    const auto is_white = [](QColor color) { return color.red() >= 235 && color.green() >= 235 && color.blue() >= 235; };
    save_widget_artifact("dynamic_vector_static_" + label, *canvas);
    CHECK(split_edge_cells(canvas->grab().toImage(), *canvas, size) > 20);

    require_hotkey_action(window, QStringLiteral("edit.free_transform"))->trigger();
    CHECK(canvas->free_transform_active());
    settle(*canvas);
    save_widget_artifact("dynamic_vector_free_transform_" + label, *canvas);
    CHECK(split_edge_cells(canvas->grab().toImage(), *canvas, size) > 20);

    // A quarter turn about the shape's center: the frame shows the turned shape.
    CHECK(canvas->set_transform_controls_state(QPointF(21, 16), 100.0, 100.0, 90.0));
    settle(*canvas);
    save_widget_artifact("dynamic_vector_free_transform_rotated_" + label, *canvas);
    CHECK(split_edge_cells(canvas->grab().toImage(), *canvas, size) > 20);
    CHECK(is_red(widget_color(QPoint(21, 3))));     // inside the turned shape only
    CHECK(is_white(widget_color(QPoint(33, 16))));  // inside the original shape only
    // The pixel grid over the frame takes its tones from the transformed document: no grid
    // line still traces the shape where it was.
    const auto rotated = canvas->grab().toImage();
    const auto line_x = canvas->widget_position_for_document_point(QPoint(33, 16));
    for (int dx = -1; dx <= 1; ++dx) {
      const auto color = rotated.pixelColor(line_x + QPoint(dx, 5));
      CHECK(color.red() - color.green() < 40);
    }
    canvas->set_transform_interpolation(TransformInterpolation::NearestNeighbor);
    settle(*canvas);
    CHECK(split_edge_cells(canvas->grab().toImage(), *canvas, size) > 20);

    canvas->finish_free_transform();
    CHECK(!canvas->free_transform_active());
    settle(*canvas);
    CHECK(split_edge_cells(canvas->grab().toImage(), *canvas, size) > 20);
    CHECK(is_red(widget_color(QPoint(21, 3))));
    CHECK(is_white(widget_color(QPoint(33, 16))));
  }
}

void ui_vector_preview_stays_sharp_through_free_transform() { check_free_transform_stays_sharp(false); }

void ui_vector_preview_stays_sharp_through_free_transform_on_artboard() { check_free_transform_stays_sharp(true); }

}  // namespace

std::vector<patchy::test::TestCase> vector_preview_tests() {
  return {
      {"ui_vector_preview_tiles_match_full_vector_raster", ui_vector_preview_tiles_match_full_vector_raster},
      {"ui_vector_preview_strokes_complements_and_group_isolation", ui_vector_preview_strokes_complements_and_group_isolation},
      {"ui_vector_preview_eligibility_and_resource_guards", ui_vector_preview_eligibility_and_resource_guards},
      {"ui_vector_preview_mixed_content_matches_compositor", ui_vector_preview_mixed_content_matches_compositor},
      {"ui_vector_preview_gradient_pattern_effects_and_vector_masks", ui_vector_preview_gradient_pattern_effects_and_vector_masks},
      {"ui_vector_preview_canvas_cache_invalidation_and_lifetime", ui_vector_preview_canvas_cache_invalidation_and_lifetime},
      {"ui_vector_preview_action_persistence_script_and_history", ui_vector_preview_action_persistence_script_and_history},
      {"ui_vector_preview_resource_notice_is_once_and_never_covers_other_status", ui_vector_preview_resource_notice_is_once_and_never_covers_other_status},
      {"ui_vector_preview_little_everywhere_if_available", ui_vector_preview_little_everywhere_if_available},
      {"ui_vector_preview_keeps_pixel_grid_and_overlays_above", ui_vector_preview_keeps_pixel_grid_and_overlays_above},
      {"ui_vector_preview_renders_artboards", ui_vector_preview_renders_artboards},
      {"ui_vector_preview_stays_sharp_through_free_transform", ui_vector_preview_stays_sharp_through_free_transform},
      {"ui_vector_preview_stays_sharp_through_free_transform_on_artboard",
       ui_vector_preview_stays_sharp_through_free_transform_on_artboard},
  };
}
