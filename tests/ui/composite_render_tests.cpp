// Byte-identity corpus for the QImage render path (render_document_rect and
// friends), the UI-side twin of tests/core/composite_corpus_tests.cpp. Renders
// every committed PSD fixture plus local-test-fixtures/composite-corpus
// documents single-threaded in both alpha modes and compares FNV-1a digests
// against per-directory baselines (the committed one,
// test-fixtures/psd/render-digests.txt, is tracked). A digest change means
// rendered bytes changed, not just speed. See tests/composite_corpus_support.hpp
// for the re-pin procedure.

#include "composite_corpus_support.hpp"
#include "local_psd_fixtures.hpp"
#include "test_harness.hpp"
#include "ui_test_groups.hpp"

#include "core/document.hpp"
#include "core/document_depth.hpp"
#include "core/adjustment_layer.hpp"
#include "core/layer.hpp"
#include "core/psd_source_colors.hpp"
#include "psd/psd_document_io.hpp"
#include "ui/image_document_io.hpp"
#include "ui/qt_paths.hpp"
#include "ui/smart_object_render.hpp"

#include <QImage>

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
    std::cout << "[SKIP] no corpus documents found\n";
    return;
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

void backglass_group_clipped_invert_preview_matches_photoshop_if_available() {
  const auto path = patchy::test::local_format_fixture_path("backglass-invert", "Backglass_homebrew.psd");
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] local Backglass fixture unavailable\n";
    return;
  }
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

void grayscale_smart_object_fallback_preserves_alpha_and_source() {
  patchy::Document document(2, 1, patchy::PixelFormat::rgba8());
  QImage source(2, 1, QImage::Format_RGBA8888);
  source.setPixelColor(0, 0, QColor(255, 0, 0, 128));
  source.setPixelColor(1, 0, QColor(0, 0, 255, 0));
  CHECK(patchy::ui::smart_object_image_for_document(source, document) == source);
  auto space = std::make_shared<patchy::PsdNativeColorSpace>();
  space->mode = 1;
  document.metadata().psd_native_color_space = space;
  for (const auto& profile : {std::vector<std::uint8_t>{}, std::vector<std::uint8_t>{1, 2, 3}}) {
    space->profile = profile;
    const auto gray = patchy::ui::smart_object_image_for_document(source, document);
    CHECK(gray.pixelColor(0, 0) == QColor(77, 77, 77, 128));
    CHECK(gray.pixelColor(1, 0) == QColor(28, 28, 28, 0));
    CHECK(source.pixelColor(0, 0) == QColor(255, 0, 0, 128));
  }
}

}  // namespace

std::vector<patchy::test::TestCase> composite_render_tests() {
  return {
      {"grayscale_smart_object_rerender_matches_photoshop", grayscale_smart_object_rerender_matches_photoshop},
      {"grayscale_smart_object_fallback_preserves_alpha_and_source", grayscale_smart_object_fallback_preserves_alpha_and_source},
      {"composite_corpus_render_digests_are_stable", composite_corpus_render_digests_are_stable},
      {"group_isolation_override_bounds_match_actual_layer_move",
       group_isolation_override_bounds_match_actual_layer_move},
      {"styled_group_partial_render_and_child_edit_match_full_render", styled_group_partial_render_and_child_edit_match_full_render},
      {"styled_group_parallel_strips_match_single_threaded", styled_group_parallel_strips_match_single_threaded},
      {"knockout_preview_preserves_alpha_depth_and_partial_bounds", knockout_preview_preserves_alpha_depth_and_partial_bounds},
      {"hidden_knockout_does_not_change_rgb_matte_blending", hidden_knockout_does_not_change_rgb_matte_blending},
      {"group_clipped_adjustment_preview_keeps_backdrop_and_tracks_child_move",
       group_clipped_adjustment_preview_keeps_backdrop_and_tracks_child_move},
      {"backglass_group_clipped_invert_preview_matches_photoshop_if_available",
       backglass_group_clipped_invert_preview_matches_photoshop_if_available},
  };
}
