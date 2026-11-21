// Byte-identity corpus for the QImage render path (render_document_rect and
// friends), the UI-side twin of tests/core/composite_corpus_tests.cpp. Renders
// every committed PSD fixture plus local-test-fixtures/composite-corpus
// documents single-threaded in both alpha modes and compares FNV-1a digests
// against per-directory baselines (the committed one,
// test-fixtures/psd/render-digests.txt, is tracked). A digest change means
// rendered bytes changed, not just speed. See tests/composite_corpus_support.hpp
// for the re-pin procedure.

#include "composite_corpus_support.hpp"
#include "color/color_management.hpp"
#include "ui/canvas_widget.hpp"
#include "local_psd_fixtures.hpp"
#include "test_harness.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include <QApplication>
#include "core/document.hpp"
#include "core/document_depth.hpp"
#include "core/adjustment_layer.hpp"
#include "core/layer.hpp"
#include "core/layer_render_utils.hpp"
#include "core/psd_source_colors.hpp"
#include "core/vector_raster.hpp"
#include "core/worker_budget.hpp"
#include "render/compositor.hpp"
#include "psd/psd_document_io.hpp"
#include "ui/image_document_io.hpp"
#include "ui/qt_paths.hpp"
#include "ui/smart_object_render.hpp"
#include <QImage>
#include <QMouseEvent>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

namespace {

using namespace patchy::test::corpus;

std::uint64_t fnv1a_hash(std::uint64_t hash, std::span<const std::uint8_t> bytes) {
  for (const auto byte : bytes) {
    hash ^= byte;
    hash *= 1099511628211ULL;
  }
  return hash;
}

// Hashes row payloads only: QImage scanlines are padded to 4-byte boundaries
// and the padding bytes are uninitialized for RGB888.
std::uint64_t image_digest(const QImage& image) {
  auto hash = 14695981039346656037ULL;
  const auto row_bytes =
      static_cast<std::size_t>(image.width()) * static_cast<std::size_t>(image.depth() / 8);
  for (int y = 0; y < image.height(); ++y) {
    hash = fnv1a_hash(hash, std::span<const std::uint8_t>(image.constScanLine(y), row_bytes));
  }
  return hash;
}

void composite_corpus_render_digests_are_stable() {
  ScopedSingleThreadedRender single_threaded;
  int problems = 0;
  std::size_t documents = 0;
  for (const auto& directory : corpus_directories()) {
    const auto files = psd_files_in(directory.dir);
    if (files.empty()) {
      continue;
    }
    std::map<std::string, std::string> digests;
    for (const auto& file : files) {
      std::optional<patchy::Document> document;
      try {
        document.emplace(patchy::psd::DocumentIo::read_file(file));
      } catch (const std::exception& error) {
        if (directory.committed) {
          throw;
        }
        std::cout << "[INFO] skipping unreadable " << file.filename().string() << ": " << error.what() << '\n';
        continue;
      }
      const auto rgba = patchy::ui::qimage_from_document(*document, true);
      const auto rgb = patchy::ui::qimage_from_document(*document, false);
      digests[file.filename().string()] = digest_hex(image_digest(rgba)) + ' ' + digest_hex(image_digest(rgb));
    }
    if (digests.empty()) {
      continue;
    }
    documents += digests.size();
    problems += compare_corpus_baseline("render", directory, 2, digests);
  }
  if (documents == 0) {
    patchy::test::skip("no corpus documents found");
  }
  CHECK(problems == 0);
}

patchy::PixelBuffer solid_rgba(std::int32_t width, std::int32_t height, std::uint8_t red, std::uint8_t green,
                               std::uint8_t blue, std::uint8_t alpha) {
  patchy::PixelBuffer pixels(width, height, patchy::PixelFormat::rgba8());
  auto data = pixels.data();
  for (std::size_t index = 0; index + 3 < data.size(); index += 4) {
    data[index] = red;
    data[index + 1] = green;
    data[index + 2] = blue;
    data[index + 3] = alpha;
  }
  return pixels;
}

patchy::PixelBuffer solid_rgb(std::int32_t width, std::int32_t height, std::uint8_t red, std::uint8_t green,
                              std::uint8_t blue) {
  patchy::PixelBuffer pixels(width, height, patchy::PixelFormat::rgb8());
  auto data = pixels.data();
  for (std::size_t index = 0; index + 2 < data.size(); index += 3) {
    data[index] = red;
    data[index + 1] = green;
    data[index + 2] = blue;
  }
  return pixels;
}

// Rendering with a LayerBoundsOverride (the move/transform preview path) must
// produce the same bytes as rendering a document whose layer actually sits at
// the overridden bounds. Guards the override-aware isolated-group bounds
// (layer_render_bounds_for_render): the group here isolates (non-PassThrough,
// group opacity), the moved child carries a sized drop shadow so effect
// padding participates, and the override rect is disjoint from the original
// so a buffer sized to the wrong rect clips content and fails the compare.
void group_isolation_override_bounds_match_actual_layer_move() {
  patchy::Document document(300, 200, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Backdrop", solid_rgb(300, 200, 90, 120, 150));

  patchy::Layer group(document.allocate_layer_id(), "Group", patchy::LayerKind::Group);
  group.set_blend_mode(patchy::BlendMode::Normal);  // non-pass-through: isolates
  group.set_opacity(0.6F);

  patchy::Layer moved(document.allocate_layer_id(), "Moved", solid_rgba(60, 40, 255, 64, 32, 200));
  const auto moved_id = moved.id();
  moved.set_bounds(patchy::Rect{20, 20, 60, 40});
  moved.set_blend_mode(patchy::BlendMode::Multiply);
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.blend_mode = patchy::BlendMode::Normal;
  shadow.color = patchy::RgbColor{0, 0, 0};
  shadow.opacity = 1.0F;
  shadow.angle_degrees = 135.0F;
  shadow.distance = 4.0F;
  shadow.size = 6.0F;
  moved.layer_style().drop_shadows.push_back(shadow);
  group.add_child(std::move(moved));

  patchy::Layer sibling(document.allocate_layer_id(), "Sibling", solid_rgba(50, 50, 32, 200, 96, 180));
  sibling.set_bounds(patchy::Rect{60, 30, 50, 50});
  sibling.set_blend_mode(patchy::BlendMode::Screen);
  group.add_child(std::move(sibling));

  document.add_layer(std::move(group));

  const patchy::Rect override_bounds{190, 120, 60, 40};
  const auto override_rgba = patchy::ui::qimage_from_document_rect_with_layer_bounds(
      document, QRect(0, 0, 300, 200), true, moved_id, override_bounds);
  const auto override_rgb = patchy::ui::qimage_from_document_rect_with_layer_bounds(
      document, QRect(0, 0, 300, 200), false, moved_id, override_bounds);

  auto reference = document;
  auto* moved_layer = reference.find_layer(moved_id);
  CHECK(moved_layer != nullptr);
  moved_layer->set_bounds(override_bounds);
  const auto actual_rgba = patchy::ui::qimage_from_document(reference, true);
  const auto actual_rgb = patchy::ui::qimage_from_document(reference, false);

  CHECK(image_digest(override_rgba) == image_digest(actual_rgba));
  CHECK(image_digest(override_rgb) == image_digest(actual_rgb));
}


void styled_group_partial_render_and_child_edit_match_full_render() {
  for (const auto mode : {patchy::BlendMode::Normal, patchy::BlendMode::PassThrough}) {
    patchy::Document document(256, 192, patchy::PixelFormat::rgba8());
    patchy::Layer group(document.allocate_layer_id(), "Styled", patchy::LayerKind::Group);
    group.set_blend_mode(mode);
    patchy::LayerInnerGlow glow;
    glow.enabled = true;
    glow.blend_mode = patchy::BlendMode::Normal;
    glow.color = {255, 0, 0};
    glow.opacity = 1.0F;
    glow.size = 12.0F;
    group.layer_style().inner_glows.push_back(glow);
    patchy::Layer child(document.allocate_layer_id(), "Child", solid_rgba(240, 176, 50, 100, 160, 255));
    child.set_bounds({-8, -8, 240, 176});
    const auto child_id = child.id();
    group.add_child(std::move(child));
    const auto group_id = group.id();
    document.add_layer(std::move(group));
    auto* editable = document.find_layer(child_id);
    CHECK(editable != nullptr);
    const auto full = patchy::ui::qimage_from_document(document, true);
    const QRect patch(30, 62, 185, 23);
    CHECK(patchy::ui::qimage_from_document_rect(document, patch, true) == full.copy(patch));
    const auto group_revision = std::as_const(document).find_layer(group_id)->content_revision();
    for (int y=70; y<90; ++y) {
      for (int x=90; x<110; ++x) editable->pixels().pixel(x,y)[3] = 0;
    }
    CHECK(std::as_const(document).find_layer(group_id)->content_revision() == group_revision);
    const auto after = patchy::ui::qimage_from_document(document, true);
    // Force an unrelated group revision to obtain an independently keyed reference.
    document.find_layer(group_id)->layer_style().inner_glows[0].opacity = 1.0F;
    const auto fresh = patchy::ui::qimage_from_document(document, true);
    CHECK(after == fresh);
    CHECK(after != full);
  }
}

void group_clipped_adjustment_preview_keeps_backdrop_and_tracks_child_move() {
  for (const auto mode : {patchy::BlendMode::PassThrough, patchy::BlendMode::Normal}) {
    patchy::Document document(300, 200, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Backdrop", solid_rgb(300, 200, 0, 0, 0));
    patchy::Layer group(document.allocate_layer_id(), "Clip base", patchy::LayerKind::Group);
    group.set_blend_mode(mode);
    patchy::Layer child(document.allocate_layer_id(), "Child", solid_rgba(60, 40, 0, 0, 0, 255));
    const auto child_id = child.id();
    child.set_bounds({20, 20, 60, 40});
    group.add_child(std::move(child));
    document.add_layer(std::move(group));
    patchy::AdjustmentSettings invert;
    invert.kind = patchy::AdjustmentKind::Invert;
    patchy::Layer adjustment(document.allocate_layer_id(), "Clipped invert", patchy::LayerKind::Adjustment);
    patchy::configure_adjustment_layer(adjustment, invert);
    adjustment.set_clipped(true);
    document.add_layer(std::move(adjustment));

    const auto full = patchy::ui::qimage_from_document(document, true);
    CHECK(full.pixelColor(0, 0) == QColor(0, 0, 0));
    CHECK(full.pixelColor(40, 40) == QColor(255, 255, 255));
    const QRect patch(30, 30, 200, 100);
    CHECK(patchy::ui::qimage_from_document_rect(document, patch, true) == full.copy(patch));

    const patchy::Rect moved_bounds{190, 120, 60, 40};
    const auto preview = patchy::ui::qimage_from_document_rect_with_layer_bounds(
        document, QRect(0, 0, 300, 200), true, child_id, moved_bounds);
    document.find_layer(child_id)->set_bounds(moved_bounds);
    const auto moved = patchy::ui::qimage_from_document(document, true);
    CHECK(preview == moved);
    CHECK(moved.pixelColor(40, 40) == QColor(0, 0, 0));
    CHECK(moved.pixelColor(200, 130) == QColor(255, 255, 255));
  }
}

void ui_styled_brush_damage_matches_full_render() {
  patchy::Document document(480, 320, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer paint(480, 320, patchy::PixelFormat::rgba8());
  paint.clear(0);
  // Transparent space within the gradient anchor and opaque pixels that can
  // pick up shading from a dab on the other side of the edge.
  for (int y = 70; y < 250; ++y) {
    for (int x = 60; x < 380; ++x) {
      if (x < 85 || x > 215) {
        auto* pixel = paint.pixel(x, y);
        pixel[0] = 180;
        pixel[3] = 255;
      }
    }
  }
  auto& layer = document.add_pixel_layer("Paint", std::move(paint));
  auto& style = layer.layer_style();
  auto& satin = style.satins.emplace_back();
  satin.enabled = true;
  satin.distance = 28;
  satin.size = 11;
  auto& bevel = style.bevels.emplace_back();
  bevel.enabled = true;
  bevel.size = 27;
  bevel.soften = 3;
  auto& glow = style.outer_glows.emplace_back();
  glow.enabled = true;
  glow.size = 9;
  auto& gradient = style.gradient_fills.emplace_back();
  gradient.enabled = true;
  gradient.gradient.angle_degrees = 0;
  gradient.gradient.color_stops = {{0.0F, {20, 30, 40}}, {1.0F, {240, 200, 150}}};

  patchy::ui::CanvasWidget canvas;
  canvas.resize(520, 360);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Brush);
  canvas.set_brush_size(31);
  canvas.set_brush_opacity(100);
  canvas.set_brush_softness(0);
  canvas.show();
  QApplication::processEvents();
  canvas.repaint();
  const auto compare = [&] {
    const auto actual = canvas.current_composite();
    CHECK(!actual.isNull());
    CHECK(actual == patchy::ui::qimage_from_document(document, true).convertToFormat(actual.format()));
  };
  compare();
  const auto before = canvas.render_cache_diagnostics().partial_patches;
  const auto send = [&](QEvent::Type type, QPoint point, Qt::MouseButton button, Qt::MouseButtons held) {
    patchy::test::ui::send_mouse(canvas, type, canvas.widget_position_for_document_point(point), button, held);
    // A dab that moves the gradient anchor redraws the silhouette on a worker.
    canvas.repaint();
    while (!canvas.render_settled()) {
      QApplication::processEvents();
      canvas.repaint();
    }
    compare();
  };
  send(QEvent::MouseButtonPress, {198, 160}, Qt::LeftButton, Qt::LeftButton);
  send(QEvent::MouseMove, {212, 162}, Qt::NoButton, Qt::LeftButton);
  CHECK(canvas.render_cache_diagnostics().partial_patches > before);
  // Growing the visible matte moves the gradient across previously painted pixels.
  send(QEvent::MouseMove, {400, 170}, Qt::NoButton, Qt::LeftButton);
  send(QEvent::MouseButtonRelease, {400, 170}, Qt::LeftButton, Qt::NoButton);

  // Painting over the same extremum with another color keeps the anchor.
  // Even the first dab of the new gesture should only patch the brush area.
  canvas.set_primary_color(Qt::green);
  const auto before_repaint = canvas.render_cache_diagnostics();
  send(QEvent::MouseButtonPress, {400, 170}, Qt::LeftButton, Qt::LeftButton);
  const auto after_repaint = canvas.render_cache_diagnostics();
  CHECK(after_repaint.partial_patches > before_repaint.partial_patches);
  CHECK(after_repaint.dirty_region_pixels - before_repaint.dirty_region_pixels < 480U * 320U / 4U);
  send(QEvent::MouseButtonRelease, {400, 170}, Qt::LeftButton, Qt::NoButton);
}

// A stroke that leaves the painted bounds moves a layer-aligned gradient across
// the whole silhouette on every move. Art over half the canvas used to turn
// each of those moves into a full refresh, and smaller art into a redraw of
// the silhouette with full-layer masks inside the mouse handler.
void ui_styled_brush_growth_redraws_silhouette_in_background() {
  constexpr int kWidth = 1600;
  constexpr int kHeight = 1000;
  patchy::Document document(kWidth, kHeight, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer paint(kWidth, kHeight, patchy::PixelFormat::rgba8());
  paint.clear(0);
  // A soft left edge, a hole and a notch near the stroke give the bands of
  // the redraw contours whose shading must match the full render.
  for (int y = 60; y < 940; ++y) {
    for (int x = 100; x < 1000; ++x) {
      const bool hole = x >= 500 && x < 600 && y >= 300 && y < 520;
      const bool notch = x >= 900 && y >= 430 && y < 470;
      if (hole || notch) {
        continue;
      }
      auto* pixel = paint.pixel(x, y);
      pixel[0] = 180;
      pixel[1] = static_cast<std::uint8_t>(60 + (y % 97));
      pixel[3] = static_cast<std::uint8_t>(x < 108 ? 32 * (x - 100) + 16 : 255);
    }
  }
  auto& layer = document.add_pixel_layer("Paint", std::move(paint));
  auto& style = layer.layer_style();
  auto& satin = style.satins.emplace_back();
  satin.enabled = true;
  satin.distance = 28;
  satin.size = 11;
  auto& bevel = style.bevels.emplace_back();
  bevel.enabled = true;
  bevel.size = 27;
  bevel.soften = 3;
  auto& glow = style.outer_glows.emplace_back();
  glow.enabled = true;
  glow.size = 9;
  auto& stroke = style.strokes.emplace_back();
  stroke.enabled = true;
  stroke.size = 3;
  auto& gradient = style.gradient_fills.emplace_back();
  gradient.enabled = true;
  gradient.gradient.angle_degrees = 0;
  gradient.gradient.color_stops = {{0.0F, {20, 30, 40}}, {1.0F, {240, 200, 150}}};

  patchy::ui::CanvasWidget canvas;
  canvas.resize(900, 700);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Brush);
  canvas.set_brush_size(61);
  canvas.set_brush_opacity(100);
  canvas.set_brush_softness(0);
  canvas.show();
  canvas.fit_to_view();
  QApplication::processEvents();
  canvas.repaint();
  const auto layer_id = layer.id();
  const auto settle = [&] {
    canvas.repaint();
    while (!canvas.render_settled()) {
      QApplication::processEvents();
      canvas.repaint();
    }
  };
  const auto compare = [&] {
    const auto actual = canvas.current_composite();
    CHECK(!actual.isNull());
    // The reference has its own revision, so it shares no cached mask or
    // bounds with the stroke.
    patchy::Document fresh = std::as_const(document);
    static_cast<void>(fresh.find_layer(layer_id)->pixels());
    CHECK(actual == patchy::ui::qimage_from_document(std::as_const(fresh), true).convertToFormat(actual.format()));
  };
  const auto alpha_bounds = [&] {
    const auto& painted = *std::as_const(document).find_layer(layer_id);
    return patchy::layer_visible_alpha_bounds(painted, painted.bounds()).value_or(patchy::Rect{});
  };
  // Sent without the event loop a finished pass lands from, so the test decides
  // when one may land.
  const auto send = [&](QEvent::Type type, QPoint point, Qt::MouseButton button, Qt::MouseButtons held) {
    const auto position = canvas.widget_position_for_document_point(point);
    QMouseEvent event(type, position, canvas.mapToGlobal(position), button, held, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &event);
  };
  settle();
  compare();
  CHECK(alpha_bounds() == (patchy::Rect{100, 60, 900, 880}));
  send(QEvent::MouseButtonPress, {880, 500}, Qt::LeftButton, Qt::LeftButton);
  send(QEvent::MouseMove, {905, 504}, Qt::NoButton, Qt::LeftButton);
  CHECK(alpha_bounds().width == 900);
  CHECK(canvas.render_settled());
  compare();

  // These moves widen the painted bounds (the smoothed path trails the pointer,
  // so the first may not). The mouse handler patches the brush area alone; the
  // silhouette, over half this canvas, follows from a background pass.
  auto width = 900;
  int widened = 0;
  for (const int x : {1040, 1160, 1280}) {
    const auto before = canvas.render_cache_diagnostics();
    send(QEvent::MouseMove, {x, 500 + (x - 880) / 8}, Qt::NoButton, Qt::LeftButton);
    const auto after = canvas.render_cache_diagnostics();
    const bool wider = alpha_bounds().width > width;
    width = alpha_bounds().width;
    widened += wider ? 1 : 0;
    CHECK(after.full_refreshes == before.full_refreshes);
    CHECK(after.dirty_region_pixels - before.dirty_region_pixels < 150'000U);
    CHECK(after.brush_realign_jobs == before.brush_realign_jobs + (wider ? 1 : 0));
    CHECK(canvas.render_settled() == !wider);
    settle();
    CHECK(canvas.render_cache_diagnostics().full_refreshes == before.full_refreshes);
    compare();
  }
  CHECK(widened >= 2);

  // Moves that arrive while a pass is still rendering: one that turns back,
  // whose brush area the pass must not cover with its older picture, and
  // others that move the anchor again, which the next pass has to pick up.
  {
    const auto before = canvas.render_cache_diagnostics();
    send(QEvent::MouseMove, {1380, 560}, Qt::NoButton, Qt::LeftButton);
    send(QEvent::MouseMove, {1450, 600}, Qt::NoButton, Qt::LeftButton);
    CHECK(canvas.render_cache_diagnostics().brush_realign_jobs == before.brush_realign_jobs + 1);
    send(QEvent::MouseMove, {1330, 640}, Qt::NoButton, Qt::LeftButton);
    send(QEvent::MouseMove, {1480, 700}, Qt::NoButton, Qt::LeftButton);
    send(QEvent::MouseMove, {1540, 760}, Qt::NoButton, Qt::LeftButton);
    // Still the first pass: the later moves joined it instead of starting their own.
    CHECK(canvas.render_cache_diagnostics().brush_realign_jobs == before.brush_realign_jobs + 1);
    CHECK(!canvas.render_settled());
    send(QEvent::MouseButtonRelease, {1540, 760}, Qt::LeftButton, Qt::NoButton);
    settle();
    const auto after = canvas.render_cache_diagnostics();
    CHECK(after.brush_realign_jobs == before.brush_realign_jobs + 2);
    CHECK(after.full_refreshes == before.full_refreshes);
    CHECK(alpha_bounds().width > width);
    compare();
  }

  // Back inside the bounds, a stroke redraws only the brush area again.
  const auto before_inside = canvas.render_cache_diagnostics();
  send(QEvent::MouseButtonPress, {400, 500}, Qt::LeftButton, Qt::LeftButton);
  send(QEvent::MouseMove, {440, 506}, Qt::NoButton, Qt::LeftButton);
  send(QEvent::MouseMove, {480, 512}, Qt::NoButton, Qt::LeftButton);
  send(QEvent::MouseButtonRelease, {480, 512}, Qt::LeftButton, Qt::NoButton);
  const auto after_inside = canvas.render_cache_diagnostics();
  CHECK(canvas.render_settled());
  CHECK(after_inside.full_refreshes == before_inside.full_refreshes);
  CHECK(after_inside.brush_realign_jobs == before_inside.brush_realign_jobs);
  CHECK(after_inside.dirty_region_pixels - before_inside.dirty_region_pixels < 200'000U);
  compare();

  // Another change while a pass is pending, as an undo would make, takes over
  // the pass's region.
  send(QEvent::MouseButtonPress, {1300, 850}, Qt::LeftButton, Qt::LeftButton);
  send(QEvent::MouseMove, {1300, 950}, Qt::NoButton, Qt::LeftButton);
  send(QEvent::MouseMove, {1300, 990}, Qt::NoButton, Qt::LeftButton);
  CHECK(!canvas.render_settled());
  send(QEvent::MouseButtonRelease, {1300, 990}, Qt::LeftButton, Qt::NoButton);
  canvas.document_changed_effect_bounds(QRegion(QRect(0, 0, 10, 10)));
  settle();
  compare();
}

void backglass_group_clipped_invert_preview_matches_photoshop_if_available() {
  const auto path = patchy::test::local_format_fixture_path("backglass-invert", "Backglass_homebrew.psd");
  patchy::test::skip_unless_exists(path);
  const auto document = patchy::psd::DocumentIo::read_file(path);
  patchy::psd::ReadOptions options;
  options.prefer_flat_composite = true;
  const auto reference = patchy::psd::DocumentIo::read_file(path, options);
  const auto rendered = patchy::ui::qimage_from_document(document, true);
  const auto expected = patchy::ui::qimage_from_document(reference, true);
  // Compare both dragons and the surrounding background exactly. The core
  // fixture test also checks opaque text; fractional text edges have a
  // separate Photoshop blending difference unrelated to clipping.
  const QRect left(0, 0, 500, document.height());
  const QRect right(1400, 0, document.width() - 1400, document.height());
  CHECK(rendered.copy(left) == expected.copy(left));
  CHECK(rendered.copy(right) == expected.copy(right));
  const QRect patch(70, 160, 300, 100);
  CHECK(patchy::ui::qimage_from_document_rect(document, patch, true) == rendered.copy(patch));
}

void styled_group_parallel_strips_match_single_threaded() {
  patchy::Document document(2048, 2048, patchy::PixelFormat::rgba8());
  patchy::Layer group(document.allocate_layer_id(), "Styled", patchy::LayerKind::Group);
  group.set_blend_mode(patchy::BlendMode::Normal);
  patchy::LayerInnerGlow glow;
  glow.enabled = true;
  glow.blend_mode = patchy::BlendMode::Normal;
  glow.color = {255, 0, 0};
  glow.opacity = 1.0F;
  glow.size = 8.0F;
  group.layer_style().inner_glows.push_back(glow);
  patchy::Layer child(document.allocate_layer_id(), "Child", solid_rgba(2000, 2000, 50, 100, 160, 255));
  child.set_bounds({24, 24, 2000, 2000});
  group.add_child(std::move(child));
  document.add_layer(std::move(group));
  const auto parallel = patchy::ui::qimage_from_document(document, true);
  ScopedSingleThreadedRender single;
  const auto sequential = patchy::ui::qimage_from_document(document, true);
  CHECK(parallel == sequential);
}

void knockout_preview_preserves_alpha_depth_and_partial_bounds() {
  for (const auto depth : {patchy::BitDepth::UInt8, patchy::BitDepth::UInt16, patchy::BitDepth::Float32}) {
    patchy::Document document(48, 32, patchy::PixelFormat::rgba8());
    document.add_pixel_layer("Opaque but not Background", solid_rgba(48, 32, 255, 0, 0, 255));
    patchy::Layer group(document.allocate_layer_id(), "Pass Through", patchy::LayerKind::Group);
    group.set_blend_mode(patchy::BlendMode::PassThrough);
    patchy::Layer hole(document.allocate_layer_id(), "Hole", solid_rgba(16, 16, 0, 0, 255, 255));
    const auto id = hole.id();
    hole.set_bounds({8, 8, 16, 16});
    hole.set_fill_opacity(0.0F);
    hole.unknown_psd_blocks().push_back({"knko", {2, 0, 0, 0}});
    group.add_child(std::move(hole));
    document.add_layer(std::move(group));
    patchy::convert_document_depth(document, depth);
    const auto rgba = patchy::ui::qimage_from_document(document, true);
    const auto rgb = patchy::ui::qimage_from_document(document, false);
    CHECK(rgba.pixelColor(12, 12).alpha() == 0);
    CHECK(rgba.pixelColor(0, 0) == QColor(255, 0, 0));
    CHECK(rgb.pixelColor(12, 12) == QColor(255, 255, 255));
    const QRect rect(10, 4, 25, 20);
    CHECK(patchy::ui::qimage_from_document_rect(document, rect, true) == rgba.copy(rect));
    CHECK(patchy::ui::qimage_from_document_rect(document, rect, false) == rgb.copy(rect));
    const auto moved_preview = patchy::ui::qimage_from_document_rect_with_layer_bounds(
        document, QRect(0, 0, 48, 32), true, id, {24, 4, 16, 16});
    document.find_layer(id)->set_bounds({24, 4, 16, 16});
    CHECK(moved_preview == patchy::ui::qimage_from_document(document, true));
  }
}

void hidden_knockout_does_not_change_rgb_matte_blending() {
  patchy::Document document(8, 8, patchy::PixelFormat::rgba8());
  auto& bottom = document.add_pixel_layer("Screen", solid_rgba(8, 8, 64, 128, 192, 255));
  bottom.set_blend_mode(patchy::BlendMode::Screen);
  const auto expected = patchy::ui::qimage_from_document(document, false);
  patchy::Layer group(document.allocate_layer_id(), "Hidden knockout", patchy::LayerKind::Group);
  const auto group_id = group.id();
  group.set_blend_mode(patchy::BlendMode::PassThrough);
  patchy::Layer hole(document.allocate_layer_id(), "Hole", solid_rgba(8, 8, 0, 0, 255, 255));
  const auto hole_id = hole.id();
  hole.set_fill_opacity(0.0F);
  hole.unknown_psd_blocks().push_back({"knko", {2, 0, 0, 0}});
  group.add_child(std::move(hole));
  document.add_layer(std::move(group));
  for (const auto id : {group_id, hole_id}) {
    document.find_layer(id)->set_visible(false);
    CHECK(patchy::ui::qimage_from_document(document, false) == expected);
    document.find_layer(id)->set_visible(true);
    CHECK(patchy::ui::qimage_from_document_rect_with_hidden_layers(
              document, QRect(0, 0, 8, 8), false, {id}) == expected);
    document.find_layer(id)->set_opacity(0.0F);
    CHECK(patchy::ui::qimage_from_document(document, false) == expected);
    document.find_layer(id)->set_opacity(1.0F);
  }
}

void grayscale_smart_object_rerender_matches_photoshop() {
  for (const auto* fixture : {"gamma22", "dot20"}) {
    const auto base = std::string("smart-object-gray/") + fixture;
    for (const auto depth : {patchy::BitDepth::UInt8, patchy::BitDepth::UInt16}) {
      auto document = patchy::psd::DocumentIo::read_file(
          patchy::test::committed_psd_fixture_path(base + ".psd"));
      patchy::convert_document_depth(document, depth);
      const auto imported = patchy::ui::qimage_from_document(document, true);
      const QImage expected(patchy::ui::to_qstring(
          patchy::test::committed_psd_fixture_path(base + ".png")));
      CHECK(!expected.isNull());
      bool refreshed = false;
      for (const auto& layer : std::as_const(document).layers()) {
        if (!patchy::layer_is_smart_object(layer)) continue;
        const auto* source = std::as_const(document).metadata().smart_objects.find(
            patchy::smart_object_source_uuid(layer));
        CHECK(source != nullptr);
        const auto bytes = *source->file_bytes;
        const auto source_image = patchy::ui::decode_smart_object_source_image(*source);
        CHECK(source_image.has_value());
        CHECK(source_image->pixelColor(4, 8) == QColor(255, 0, 0));
        const auto placement = patchy::smart_object_placement_from_layer(layer);
        CHECK(placement.has_value());
        const auto direct = patchy::ui::render_smart_object_image_preview(
            *source_image, *placement, std::nullopt,
            patchy::ui::CanvasWidget::TransformInterpolation::Bicubic, nullptr, document);
        CHECK(direct.has_value());
        const auto preview = patchy::ui::render_smart_object_layer_preview(
            document, layer, patchy::ui::CanvasWidget::TransformInterpolation::Bicubic);
        CHECK(preview.has_value());
        CHECK(std::equal(preview->rendered.pixels.data().begin(), preview->rendered.pixels.data().end(),
                         direct->rendered.pixels.data().begin(), direct->rendered.pixels.data().end()));
        auto* target = document.find_layer(layer.id());
        CHECK(target != nullptr);
        CHECK(patchy::ui::refresh_smart_object_layer_preview(
            document, *target, patchy::ui::CanvasWidget::TransformInterpolation::Bicubic));
        CHECK(*source->file_bytes == bytes);
        refreshed = true;
      }
      CHECK(refreshed);
      const auto actual = patchy::ui::qimage_from_document(document, true);
      CHECK(actual.size() == expected.size());
      for (int y = 0; y < actual.height(); ++y) {
        for (int x = 0; x < actual.width(); ++x) {
          const auto color = actual.pixelColor(x, y);
          const auto before = imported.pixelColor(x, y);
          CHECK(color.alpha() == before.alpha());
          if (color.alpha() == 0) continue;
          CHECK(color.red() == color.green() && color.green() == color.blue());
          CHECK(std::abs(color.red() - before.red()) <= 3);
          // Native gray compositing has a separate fractional-alpha difference;
          // opaque colors isolate source conversion from that blend-domain gap.
          if (color.alpha() == 255) {
            CHECK(std::abs(color.red() - expected.pixelColor(x, y).red()) <= 3);
          }
        }
      }
    }
  }
}

void cmyk_smart_object_and_gradient_rerender_match_photoshop() {
  auto document = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("cmyk-render/smart-gradient.psd"));
  const QImage expected(patchy::ui::to_qstring(
      patchy::test::committed_psd_fixture_path("cmyk-render/smart-gradient.png")));
  CHECK(!expected.isNull());
  std::vector<patchy::LayerId> smart, shapes;
  for (const auto& layer : std::as_const(document).layers()) {
    if (patchy::layer_is_smart_object(layer)) smart.push_back(layer.id());
    if (layer.vector_shape()) shapes.push_back(layer.id());
  }
  CHECK(smart.size() == 1 && shapes.size() == 1);
  for (const auto id : smart) {
    auto* layer = document.find_layer(id);
    CHECK(patchy::ui::refresh_smart_object_layer_preview(
        document, *layer, patchy::ui::CanvasWidget::TransformInterpolation::Bicubic));
  }
  for (const auto id : shapes) patchy::update_vector_shape_raster(
      *document.find_layer(id), patchy::Rect::from_size(document.width(), document.height()),
      &std::as_const(document).metadata().patterns);
  const auto actual = patchy::ui::qimage_from_document(document, true);
  const auto core = patchy::Compositor{}.flatten_rgb8(document);
  const auto partial = patchy::ui::qimage_from_document_rect(document, QRect(5, 7, 35, 31), true);
  CHECK(actual.size() == expected.size());
  CHECK(partial == actual.copy(QRect(5, 7, 35, 31)));
  std::int64_t total_error = 0;
  for (int y = 0; y < actual.height(); ++y) for (int x = 0; x < actual.width(); ++x) {
    const auto color = actual.pixelColor(x, y);
    CHECK(color.alpha() == 255);
    CHECK(color.red() == core.pixel(x, y)[0] && color.green() == core.pixel(x, y)[1] && color.blue() == core.pixel(x, y)[2]);
    const auto reference = expected.pixelColor(x, y);
    // One ink-byte of gradient rounding can amplify near a profile's gamut
    // boundary. Pin both the largest error and the much smaller mean error.
    const std::array<int, 3> error{std::abs(color.red() - reference.red()),
        std::abs(color.green() - reference.green()), std::abs(color.blue() - reference.blue())};
    for (const auto value : error) { CHECK(value <= 12); total_error += value; }
  }
  CHECK(total_error < actual.width() * actual.height() * 3 * 2);
}

void grayscale_smart_object_fallback_preserves_alpha_and_source() {
  patchy::Document document(2, 1, patchy::PixelFormat::rgba8());
  QImage source(2, 1, QImage::Format_RGBA8888);
  source.setPixelColor(0, 0, QColor(255, 0, 0, 128));
  source.setPixelColor(1, 0, QColor(0, 0, 255, 0));
  CHECK(patchy::ui::smart_object_image_for_document(source, document) == source);
  auto space = std::make_shared<patchy::PsdNativeColorSpace>();
  space->mode = 1;
  document.metadata().psd_native_color_space = space;
  // An unusable profile falls back to deterministic luminance.
  space->profile = {1, 2, 3};
  const auto gray = patchy::ui::smart_object_image_for_document(source, document);
  CHECK(gray.pixelColor(0, 0) == QColor(77, 77, 77, 128));
  CHECK(gray.pixelColor(1, 0) == QColor(28, 28, 28, 0));
  CHECK(source.pixelColor(0, 0) == QColor(255, 0, 0, 128));
  // An untagged document converts through the assumed Dot Gain 20% gray, like a file that
  // embeds it, keeping alpha.
  space->profile.clear();
  const auto untagged = patchy::ui::smart_object_image_for_document(source, document);
  space->profile = patchy::assumed_gray_profile();
  const auto tagged = patchy::ui::smart_object_image_for_document(source, document);
  CHECK(untagged == tagged);
  const auto red = untagged.pixelColor(0, 0);
  CHECK(red.alpha() == 128 && red.red() == red.green() && red.green() == red.blue());
  CHECK(source.pixelColor(0, 0) == QColor(255, 0, 0, 128));
}

// A paint stroke whose dabs change more effects than the painted layer's own: the layer
// sits in groups that have effects (or in a folder that is a clipping base), or the stroke
// paints the layer's mask. Every frame must equal a fresh full render, with no full
// refresh, and the state before the stroke must keep its cached masks for Undo.
struct StrokeScene {
  const char* name{""};
  patchy::Document document{480, 320, patchy::PixelFormat::rgba8()};
  patchy::LayerId painted{0};
  // A pixel layer whose revision the reference render bumps, so it shares no cached mask
  // or silhouette with the stroke.
  patchy::LayerId reference_layer{0};
  bool mask_target{false};
  bool eraser{false};
  bool patches_silhouette{false};
  // The stroke moves a layer-aligned gradient, which redraws the layer's whole silhouette.
  bool moves_gradient{false};
};

patchy::PixelBuffer stroke_scene_ink() {
  patchy::PixelBuffer ink(480, 320, patchy::PixelFormat::rgba8());
  ink.clear(0);
  // A soft left edge and a hole: contours close to the stroke.
  for (int y = 60; y < 260; ++y) {
    for (int x = 70; x < 400; ++x) {
      if (x >= 200 && x < 240 && y >= 120 && y < 180) {
        continue;
      }
      auto* pixel = ink.pixel(x, y);
      pixel[0] = 200;
      pixel[1] = static_cast<std::uint8_t>(60 + (y % 90));
      pixel[2] = 50;
      pixel[3] = static_cast<std::uint8_t>(x < 78 ? 32 * (x - 70) + 16 : 255);
    }
  }
  return ink;
}

void add_stroke_scene_effects(patchy::Layer& layer, bool exterior, bool interior) {
  auto& style = layer.layer_style();
  if (exterior) {
    auto& shadow = style.drop_shadows.emplace_back();
    shadow.enabled = true;
    shadow.distance = 9;
    shadow.size = 7;
    auto& glow = style.outer_glows.emplace_back();
    glow.enabled = true;
    glow.size = 9;
    auto& stroke = style.strokes.emplace_back();
    stroke.enabled = true;
    stroke.size = 4;
    stroke.color = {20, 20, 60};
  }
  if (interior) {
    auto& bevel = style.bevels.emplace_back();
    bevel.enabled = true;
    bevel.size = 9;
    bevel.soften = 2;
    auto& satin = style.satins.emplace_back();
    satin.enabled = true;
    satin.distance = 12;
    satin.size = 7;
    auto& inner = style.inner_shadows.emplace_back();
    inner.enabled = true;
  }
}

StrokeScene make_stroke_scene(const char* name) {
  StrokeScene scene;
  scene.name = name;
  auto& document = scene.document;
  document.add_layer(patchy::Layer(document.allocate_layer_id(), "Backdrop", solid_rgba(480, 320, 225, 228, 235, 255)));
  const std::string_view kind = name;
  patchy::Layer ink(document.allocate_layer_id(), "Ink", stroke_scene_ink());
  scene.painted = ink.id();
  scene.reference_layer = ink.id();
  if (kind == "layer_mask") {
    add_stroke_scene_effects(ink, true, true);
    patchy::PixelBuffer mask(480, 320, patchy::PixelFormat::gray8());
    mask.clear(255);
    ink.set_mask(patchy::LayerMask{patchy::Rect{0, 0, 480, 320}, std::move(mask), 255, false});
    scene.mask_target = true;
    document.add_layer(std::move(ink));
    return scene;
  }
  patchy::Layer group(document.allocate_layer_id(), "Group", patchy::LayerKind::Group);
  patchy::Layer chip(document.allocate_layer_id(), "Chip", solid_rgba(90, 70, 60, 150, 90, 255));
  chip.set_bounds({330, 210, 90, 70});
  scene.patches_silhouette = true;
  if (kind == "clip_base_folder") {
    // No effects: the folder's merged children are the clipping shape.
    group.add_child(std::move(chip));
    group.add_child(std::move(ink));
    document.add_layer(std::move(group));
    patchy::Layer member(document.allocate_layer_id(), "Member", solid_rgba(480, 320, 40, 60, 200, 140));
    member.set_clipped(true);
    document.add_layer(std::move(member));
    return scene;
  }
  if (kind == "nested_groups") {
    add_stroke_scene_effects(ink, false, true);
    patchy::Layer inner(document.allocate_layer_id(), "Inner", patchy::LayerKind::Group);
    add_stroke_scene_effects(inner, false, true);
    auto& glow = inner.layer_style().outer_glows.emplace_back();
    glow.enabled = true;
    glow.size = 6;
    inner.add_child(std::move(ink));
    add_stroke_scene_effects(group, true, false);
    group.add_child(std::move(chip));
    group.add_child(std::move(inner));
    document.add_layer(std::move(group));
    return scene;
  }
  add_stroke_scene_effects(group, true, true);
  if (kind == "isolated_group") {
    group.set_blend_mode(patchy::BlendMode::Normal);
    group.set_opacity(0.8F);
  }
  if (kind == "group_mask") {
    patchy::PixelBuffer mask(480, 320, patchy::PixelFormat::gray8());
    mask.clear(255);
    group.set_mask(patchy::LayerMask{patchy::Rect{0, 0, 480, 320}, std::move(mask), 255, false});
    scene.painted = group.id();
    scene.mask_target = true;
  }
  scene.eraser = kind == "group_eraser";
  if (kind == "gradient_in_group") {
    // The painted layer's own gradient follows its bounds, which the second stroke
    // widens: the group's children then change over the layer's whole silhouette.
    auto& fill = ink.layer_style().gradient_fills.emplace_back();
    fill.enabled = true;
    fill.gradient.angle_degrees = 0;
    fill.gradient.color_stops = {{0.0F, {20, 30, 40}}, {1.0F, {240, 200, 150}}};
    scene.moves_gradient = true;
  }
  group.add_child(std::move(chip));
  group.add_child(std::move(ink));
  document.add_layer(std::move(group));
  if (kind == "deep_group") {
    patchy::convert_document_depth(document, patchy::BitDepth::UInt16);
  }
  return scene;
}

void check_stroke_scene(StrokeScene scene) {
  auto& document = scene.document;
  document.set_active_layer(scene.painted);
  patchy::ui::CanvasWidget canvas;
  canvas.resize(520, 360);
  canvas.set_document(&document);
  if (scene.mask_target) {
    canvas.set_layer_edit_target(patchy::ui::CanvasWidget::LayerEditTarget::Mask);
    CHECK(canvas.layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::Mask);
  }
  canvas.set_tool(scene.eraser ? patchy::ui::CanvasTool::Eraser : patchy::ui::CanvasTool::Brush);
  canvas.set_primary_color(scene.mask_target ? QColor(Qt::black) : QColor(40, 90, 220));
  canvas.set_brush_size(31);
  canvas.set_brush_opacity(100);
  canvas.set_brush_softness(30);
  canvas.show();
  QApplication::processEvents();
  canvas.repaint();
  const auto settle = [&] {
    canvas.repaint();
    while (!canvas.render_settled()) {
      QApplication::processEvents();
      canvas.repaint();
    }
  };
  int frame = 0;
  const auto compare = [&] {
    ++frame;
    const auto actual = canvas.current_composite();
    CHECK(!actual.isNull());
    patchy::Document fresh = std::as_const(document);
    static_cast<void>(fresh.find_layer(scene.reference_layer)->pixels());
    const auto expected = patchy::ui::qimage_from_document(std::as_const(fresh), true).convertToFormat(actual.format());
    const bool equal = actual == expected;
    CHECK(equal);
    if (!equal && actual.size() == expected.size()) {
      int worst = 0;
      int count = 0;
      QRect where;
      for (int y = 0; y < actual.height(); ++y) {
        const auto* a = actual.constScanLine(y);
        const auto* b = expected.constScanLine(y);
        for (int x = 0; x < actual.width() * 4; ++x) {
          const auto delta = std::abs(static_cast<int>(a[x]) - static_cast<int>(b[x]));
          if (delta != 0) {
            ++count;
            worst = std::max(worst, delta);
            where |= QRect(x / 4, y, 1, 1);
          }
        }
      }
      std::cout << "[stroke scene] " << scene.name << " frame " << frame << ": " << count
                << " channel values differ, worst " << worst << ", inside " << where.x() << ',' << where.y() << ' '
                << where.width() << 'x' << where.height() << '\n';
    }
  };
  const auto send = [&](QEvent::Type type, QPoint point, Qt::MouseButton button, Qt::MouseButtons held) {
    patchy::test::ui::send_mouse(canvas, type, canvas.widget_position_for_document_point(point), button, held);
    settle();
    compare();
  };
  settle();
  compare();
  // The state Undo returns to, with the revisions its masks are cached under.
  const patchy::Document before = std::as_const(document);
  const auto before_image = canvas.current_composite();
  const auto diagnostics = canvas.render_cache_diagnostics();
  const auto silhouette_patches = patchy::ui::group_silhouette_patches();

  // Across the hole and the soft edge, then out over the transparent margin.
  send(QEvent::MouseButtonPress, {150, 150}, Qt::LeftButton, Qt::LeftButton);
  int moves = 0;
  for (const auto point : {QPoint(178, 156), QPoint(214, 168), QPoint(262, 176), QPoint(300, 150)}) {
    send(QEvent::MouseMove, point, Qt::NoButton, Qt::LeftButton);
    ++moves;
  }
  send(QEvent::MouseButtonRelease, {300, 150}, Qt::LeftButton, Qt::NoButton);
  // A second stroke starts from what the first left cached.
  send(QEvent::MouseButtonPress, {95, 230}, Qt::LeftButton, Qt::LeftButton);
  for (const auto point : {QPoint(80, 200), QPoint(64, 170), QPoint(96, 130)}) {
    send(QEvent::MouseMove, point, Qt::NoButton, Qt::LeftButton);
    ++moves;
  }
  send(QEvent::MouseButtonRelease, {96, 130}, Qt::LeftButton, Qt::NoButton);

  const auto after = canvas.render_cache_diagnostics();
  CHECK(after.full_refreshes == diagnostics.full_refreshes);
  CHECK(after.partial_patches >= diagnostics.partial_patches + moves);
  if (!scene.moves_gradient) {
    // The patches stayed near the dabs: no move redrew a quarter of the canvas.
    CHECK(after.dirty_region_pixels - diagnostics.dirty_region_pixels <
          static_cast<std::uint64_t>(after.partial_patches - diagnostics.partial_patches) * (480U * 320U / 4U));
  }
  if (scene.patches_silhouette) {
    // One patched silhouette for each move and each press at least (the second press
    // starts from the first stroke's last one); a full flatten adds none.
    CHECK(patchy::ui::group_silhouette_patches() >= silhouette_patches + static_cast<std::uint64_t>(moves) + 2U);
  }

  // Undo renders the earlier state: its silhouette and masks are still cached.
  const auto misses = patchy::ui::style_mask_cache_misses();
  CHECK(patchy::ui::qimage_from_document(before, true).convertToFormat(before_image.format()) == before_image);
  CHECK(patchy::ui::style_mask_cache_misses() == misses);
}

// A render window must not change what an effect looks like (November 2026, the 4000x2781
// pinball poster: strips differed from the one-pass walk by up to 44/255 over half the
// canvas, and a committed Move patch from a fresh render by 17/255). Two effects read more
// than their own window:
// - a spread or Precise falloff takes each pixel's strength from the largest alpha of its
//   connected shape, which a window of a wider layer sees only part of;
// - stacked Strokes knock each other out, which a window holding only the band below the
//   layer used to skip.
void effect_windows_match_the_whole_layer() {
  // 4.3 Mpx: a full render takes strips whatever its layers do.
  constexpr int kWidth = 2400;
  constexpr int kHeight = 1800;
  patchy::Document document(kWidth, kHeight, patchy::PixelFormat::rgba8());
  document.add_layer(
      patchy::Layer(document.allocate_layer_id(), "Paper", solid_rgba(kWidth, kHeight, 70, 80, 95, 255)));
  {
    // One connected lattice hanging off both sides of the canvas, fading along both axes:
    // every window holds a different brightest pixel, and the brightest of all is off the
    // canvas at the lower right.
    constexpr int kLatticeWidth = 5200;
    constexpr int kLatticeHeight = 700;
    patchy::PixelBuffer pixels(kLatticeWidth, kLatticeHeight, patchy::PixelFormat::rgba8());
    for (int y = 0; y < kLatticeHeight; ++y) {
      auto row = pixels.row(y);
      for (int x = 0; x < kLatticeWidth; ++x) {
        if (y % 60 >= 6 && x % 90 >= 6) {
          continue;
        }
        auto* pixel = row.data() + static_cast<std::size_t>(x) * 4U;
        pixel[0] = 120;
        pixel[1] = 220;
        pixel[2] = 255;
        pixel[3] = static_cast<std::uint8_t>(40 + (x * 150) / (kLatticeWidth - 1) + (y * 65) / (kLatticeHeight - 1));
      }
    }
    patchy::Layer lattice(document.allocate_layer_id(), "Lattice", std::move(pixels));
    lattice.set_bounds(patchy::Rect{-1900, 900, kLatticeWidth, kLatticeHeight});
    auto& style = lattice.layer_style();
    patchy::LayerOuterGlow soft;
    soft.enabled = true;
    soft.size = 63.0F;
    soft.spread = 17.0F;  // an 11 px spread: past the exact dilation, into the strength band
    soft.opacity = 0.6F;
    style.outer_glows.push_back(soft);
    patchy::LayerOuterGlow precise;
    precise.enabled = true;
    precise.technique = patchy::LayerGlowTechnique::Precise;
    precise.size = 24.0F;
    precise.spread = 10.0F;
    precise.opacity = 0.5F;
    precise.color = patchy::RgbColor{255, 120, 200};
    style.outer_glows.push_back(precise);
    patchy::LayerDropShadow shadow;
    shadow.enabled = true;
    shadow.size = 40.0F;
    shadow.spread = 40.0F;
    shadow.distance = 14.0F;
    shadow.opacity = 0.6F;
    style.drop_shadows.push_back(shadow);
    document.add_layer(std::move(lattice));
  }
  // Plates with two stacked strokes, bottoms three rows apart from row 380 to 560, so a
  // strip boundary of any strip count falls inside some plate's band below its edge.
  constexpr int kFirstBottom = 380;
  for (int index = 0; index <= 60; ++index) {
    patchy::Layer plate(document.allocate_layer_id(), "Plate", solid_rgba(24, 90, 230, 170, 60, 255));
    plate.set_bounds(patchy::Rect{30 + index * 38, kFirstBottom + index * 3 - 90, 24, 90});
    auto& style = plate.layer_style();
    patchy::LayerStroke upper;
    upper.enabled = true;
    upper.size = 11.0F;
    upper.opacity = 0.6F;
    upper.color = patchy::RgbColor{255, 255, 255};
    style.strokes.push_back(upper);
    patchy::LayerStroke lower;
    lower.enabled = true;
    lower.size = 10.0F;
    lower.color = patchy::RgbColor{20, 20, 20};
    style.strokes.push_back(lower);
    document.add_layer(std::move(plate));
  }

  const auto threaded = patchy::ui::qimage_from_document(document, true);
  QImage whole;
  {
    ScopedSingleThreadedRender single;
    whole = patchy::ui::qimage_from_document(document, true);
  }
  CHECK(!whole.isNull());
  struct Difference {
    long long pixels{0};
    int worst{0};
    QRect extent;
  };
  const auto difference = [](const char* label, const QImage& left, const QImage& right) {
    Difference result;
    const auto a = left.convertToFormat(QImage::Format_RGBA8888);
    const auto b = right.convertToFormat(QImage::Format_RGBA8888);
    if (a.size() != b.size()) {
      result.pixels = -1;
      result.worst = 255;
      return result;
    }
    for (int y = 0; y < a.height(); ++y) {
      const auto* row_a = a.constScanLine(y);
      const auto* row_b = b.constScanLine(y);
      for (int x = 0; x < a.width(); ++x) {
        int delta = 0;
        for (int c = 0; c < 4; ++c) {
          delta = std::max(delta, std::abs(static_cast<int>(row_a[x * 4 + c]) - static_cast<int>(row_b[x * 4 + c])));
        }
        if (delta != 0) {
          ++result.pixels;
          result.worst = std::max(result.worst, delta);
          result.extent = result.extent.united(QRect(x, y, 1, 1));
        }
      }
    }
    if (result.pixels != 0) {
      std::fprintf(stderr, "  %s: %lld pixels differ, worst %d, in %d,%d %dx%d\n", label, result.pixels, result.worst,
                   result.extent.x(), result.extent.y(), result.extent.width(), result.extent.height());
    }
    return result;
  };

  // Strips against the one-pass walk. The lattice's rows must be the same bytes. Around
  // the plates a stroke below full opacity rounds one level differently in some windows,
  // with or without this fix (1,470 pixels here); the stacked-stroke bug was 68 levels.
  const QRect lattice_rows(0, 700, kWidth, kHeight - 700);
  CHECK(difference("strips, lattice rows", threaded.copy(lattice_rows), whole.copy(lattice_rows)).pixels == 0);
  CHECK(difference("strips, whole canvas", threaded, whole).worst <= 1);

  // Windows of the one-pass walk itself against the whole render.
  const auto window_difference = [&](QRect rect) {
    ScopedSingleThreadedRender single;
    const auto patches = patchy::ui::qimage_patches_from_document_region(document, QRegion(rect), true);
    Difference total;
    if (patches.empty()) {
      total.worst = 255;
    }
    for (const auto& patch : patches) {
      const auto one = difference("window", patch.image,
                                  whole.copy(patch.document_rect).convertToFormat(patch.image.format()));
      total.pixels += one.pixels;
      total.worst = std::max(total.worst, one.worst);
    }
    return total;
  };
  // A band through the lattice and its glow, a block inside it, and its right end: each
  // window's own components stop at the window's edges.
  CHECK(window_difference(QRect(0, 1000, kWidth, 180)).pixels == 0);
  CHECK(window_difference(QRect(900, 820, 700, 300)).pixels == 0);
  CHECK(window_difference(QRect(kWidth - 400, 860, 400, 420)).pixels == 0);
  // Only the band below a plate, none of the plate itself; then plates whole.
  CHECK(window_difference(QRect(20, kFirstBottom + 3, 200, 30)).worst <= 1);
  CHECK(window_difference(QRect(30 + 20 * 38 - 20, kFirstBottom + 20 * 3 + 2, 70, 12)).worst <= 1);
  CHECK(window_difference(QRect(0, 250, 900, 320)).worst <= 1);
}

// A region patch between 0.5 and 4 Mpx composites in strips across the workers, as a
// full render does from 4 Mpx up, while every effect mask it reads is a full-layer one:
// its bytes are then those of one pass (and of that part of a full render). A layer whose
// effects reach past the canvas edge has masks windowed to each render, so a patch that
// reads them keeps the one pass. 8-bit and 16-bit, a single rect and the rect-plus-sliver
// region an arrow-key nudge makes (before, such a patch took one thread: 300 ms a nudge
// for a 3.4 Mpx layer with five effects on 32 threads, against 77 ms for the whole canvas).
void region_patch_strips_match_one_pass() {
  if (patchy::hardware_worker_threads() < 2) {
    return;  // one worker: every render is one pass
  }
  constexpr int kWidth = 2000;
  constexpr int kHeight = 1500;
  for (const bool deep : {false, true}) {
    patchy::Document document(kWidth, kHeight, patchy::PixelFormat::rgba8());
    {
      patchy::PixelBuffer photo(kWidth, kHeight, patchy::PixelFormat::rgba8());
      for (int y = 0; y < kHeight; ++y) {
        auto* row = photo.pixel(0, y);
        for (int x = 0; x < kWidth; ++x) {
          row[x * 4 + 0] = static_cast<std::uint8_t>(60 + (x * 7 + y * 3) % 150);
          row[x * 4 + 1] = static_cast<std::uint8_t>(80 + (x / 9 + y / 5) % 120);
          row[x * 4 + 2] = static_cast<std::uint8_t>(100 + (y * 11) % 90);
          row[x * 4 + 3] = 255;
        }
      }
      document.add_layer(patchy::Layer(document.allocate_layer_id(), "Photo", std::move(photo)));
    }
    // A soft-edged slab with a hole: contours inside every strip.
    patchy::PixelBuffer slab(1100, 800, patchy::PixelFormat::rgba8());
    slab.clear(0);
    for (int y = 0; y < 800; ++y) {
      for (int x = 0; x < 1100; ++x) {
        if (x >= 400 && x < 600 && y >= 300 && y < 520) {
          continue;
        }
        auto* pixel = slab.pixel(x, y);
        pixel[0] = 200;
        pixel[1] = static_cast<std::uint8_t>(60 + (y % 90));
        pixel[2] = 50;
        pixel[3] = static_cast<std::uint8_t>(x < 16 ? 16 * x + 8 : 255);
      }
    }
    patchy::Layer ink(document.allocate_layer_id(), "Ink", std::move(slab));
    const auto ink_id = ink.id();
    ink.set_bounds({420, 330, 1100, 800});
    add_stroke_scene_effects(ink, true, true);
    document.add_layer(std::move(ink));
    patchy::Layer group(document.allocate_layer_id(), "Group", patchy::LayerKind::Group);
    add_stroke_scene_effects(group, true, false);
    patchy::Layer chip(document.allocate_layer_id(), "Chip", solid_rgba(300, 200, 60, 150, 90, 255));
    chip.set_bounds({1500, 180, 300, 200});
    group.add_child(std::move(chip));
    patchy::Layer blob(document.allocate_layer_id(), "Blob", solid_rgba(250, 250, 40, 60, 200, 180));
    blob.set_bounds({150, 1100, 250, 250});
    group.add_child(std::move(blob));
    document.add_layer(std::move(group));
    if (deep) {
      patchy::convert_document_depth(document, patchy::BitDepth::UInt16);
    }

    // `whole_matches`: the patch is also that part of a full render. Not asked of a patch
    // that reads masks windowed to its clip, whose blurs may round differently per window.
    const auto check_region = [&](const QRegion& region, bool expect_strips, bool whole_matches = true) {
      const auto strips_before = patchy::ui::patch_strip_renders();
      const auto fallbacks_before = patchy::ui::patch_strip_fallbacks();
      const auto striped = patchy::ui::qimage_patches_from_document_region(document, region, true);
      const auto strips = patchy::ui::patch_strip_renders() - strips_before;
      CHECK(expect_strips ? strips > 0 : strips == 0);
      CHECK(patchy::ui::patch_strip_fallbacks() == fallbacks_before);
      std::vector<patchy::ui::RenderedDocumentPatch> one_pass;
      QImage whole;
      {
        ScopedSingleThreadedRender single;
        one_pass = patchy::ui::qimage_patches_from_document_region(document, region, true);
        whole = patchy::ui::qimage_from_document(document, true);
      }
      CHECK(patchy::ui::patch_strip_renders() - strips_before == strips);
      CHECK(striped.size() == one_pass.size() && !striped.empty());
      for (std::size_t index = 0; index < std::min(striped.size(), one_pass.size()); ++index) {
        CHECK(striped[index].document_rect == one_pass[index].document_rect);
        CHECK(striped[index].image == one_pass[index].image);
        CHECK(!whole_matches ||
              striped[index].image ==
                  whole.copy(striped[index].document_rect).convertToFormat(striped[index].image.format()));
      }
    };
    // One rect of 1.65 Mpx.
    check_region(QRegion(QRect(200, 150, 1500, 1100)), true);
    // A nudge's shape: three bands, the middle one (0.75 Mpx) with the sliver's columns.
    check_region(QRegion(QRect(200, 150, 1500, 1000)) + QRect(1700, 400, 4, 500), true);
    // Every band large enough for strips.
    check_region(QRegion(QRect(100, 100, 1800, 1300)) + QRect(1900, 600, 4, 300), true);
    // Under half a megapixel: one pass, as before.
    check_region(QRegion(QRect(500, 400, 700, 600)), false);

    // The slab's glow and shadow now cross the canvas edge.
    document.find_layer(ink_id)->set_bounds({-40, 330, 1100, 800});
    check_region(QRegion(QRect(0, 150, 1500, 1100)), false, false);
    // A patch that does not reach that layer's effects still takes strips.
    check_region(QRegion(QRect(1150, 60, 800, 700)), true);
  }
}

void ui_styled_group_and_mask_strokes_match_full_render() {
  for (const char* name : {"layer_mask", "pass_through_group", "isolated_group", "nested_groups", "clip_base_folder",
                           "group_mask", "group_eraser", "deep_group", "gradient_in_group"}) {
    check_stroke_scene(make_stroke_scene(name));
  }
}

}  // namespace

std::vector<patchy::test::TestCase> composite_render_tests() {
  return {
      {"cmyk_smart_object_and_gradient_rerender_match_photoshop", cmyk_smart_object_and_gradient_rerender_match_photoshop},
      {"grayscale_smart_object_rerender_matches_photoshop", grayscale_smart_object_rerender_matches_photoshop},
      {"grayscale_smart_object_fallback_preserves_alpha_and_source", grayscale_smart_object_fallback_preserves_alpha_and_source},
      {"composite_corpus_render_digests_are_stable", composite_corpus_render_digests_are_stable},
      {"group_isolation_override_bounds_match_actual_layer_move",
       group_isolation_override_bounds_match_actual_layer_move},
      {"ui_styled_brush_damage_matches_full_render", ui_styled_brush_damage_matches_full_render},
      {"ui_styled_brush_growth_redraws_silhouette_in_background",
       ui_styled_brush_growth_redraws_silhouette_in_background},
      {"ui_styled_group_and_mask_strokes_match_full_render", ui_styled_group_and_mask_strokes_match_full_render},
      {"styled_group_partial_render_and_child_edit_match_full_render", styled_group_partial_render_and_child_edit_match_full_render},
      {"styled_group_parallel_strips_match_single_threaded", styled_group_parallel_strips_match_single_threaded},
      {"region_patch_strips_match_one_pass", region_patch_strips_match_one_pass},
      {"effect_windows_match_the_whole_layer", effect_windows_match_the_whole_layer},
      {"knockout_preview_preserves_alpha_depth_and_partial_bounds", knockout_preview_preserves_alpha_depth_and_partial_bounds},
      {"hidden_knockout_does_not_change_rgb_matte_blending", hidden_knockout_does_not_change_rgb_matte_blending},
      {"group_clipped_adjustment_preview_keeps_backdrop_and_tracks_child_move",
       group_clipped_adjustment_preview_keeps_backdrop_and_tracks_child_move},
      {"backglass_group_clipped_invert_preview_matches_photoshop_if_available",
       backglass_group_clipped_invert_preview_matches_photoshop_if_available},
  };
}
