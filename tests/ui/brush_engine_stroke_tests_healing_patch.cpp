// Part 2 of the brush-engine stroke UI test group (see
// brush_engine_stroke_tests.cpp): Spot Healing, the Patch tool, and the
// retouch Sample All Layers option. The heal commits are pinned byte-exact on
// synthetic documents whose uniform surroundings make the frequency-separation
// math collapse to known values.

#include "core/document.hpp"
#include "ui/app_settings.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/main_window.hpp"

#include "test_harness.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QPoint>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

using namespace patchy::test::ui;

void ui_retouch_sample_all_layers_switches_clone_and_healing_source() {
  patchy::Document document(48, 16, patchy::PixelFormat::rgba8());
  auto base_pixels = solid_pixels(48, 16, patchy::PixelFormat::rgba8(), QColor(200, 50, 25, 255));
  auto* base_detail = base_pixels.pixel(4, 8);
  base_detail[0] = 240;
  base_detail[1] = 90;
  base_detail[2] = 60;
  document.add_pixel_layer("Base", std::move(base_pixels));
  auto edit_pixels = solid_pixels(48, 16, patchy::PixelFormat::rgba8(), QColor(10, 20, 30, 255));
  for (std::int32_t y = 0; y < 16; ++y) {
    for (std::int32_t x = 0; x < 24; ++x) {
      edit_pixels.pixel(x, y)[3] = 0;
    }
  }
  auto& edit_layer = document.add_pixel_layer("Edit", std::move(edit_pixels));
  document.set_active_layer(edit_layer.id());

  patchy::ui::CanvasWidget canvas;
  canvas.resize(192, 64);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Clone);
  canvas.set_brush_size(1);
  canvas.set_brush_opacity(100);
  canvas.set_brush_softness(0);
  canvas.set_healing_diffusion(5);
  // Unaligned keeps every stroke reading from the Alt-clicked source point.
  canvas.set_clone_aligned(false);
  canvas.show();
  QApplication::processEvents();
  // The default preserves the historical always-merged sampling.
  CHECK(canvas.retouch_sample_all_layers());

  const auto stroke_at = [&canvas](QPoint point) {
    const auto widget_point = canvas.widget_position_for_document_point(point);
    send_mouse(canvas, QEvent::MouseButtonPress, widget_point, Qt::LeftButton, Qt::LeftButton);
    send_mouse(canvas, QEvent::MouseButtonRelease, widget_point, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
  };
  const auto source = canvas.widget_position_for_document_point(QPoint(4, 8));
  send_mouse(canvas, QEvent::MouseButtonPress, source, Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
  send_mouse(canvas, QEvent::MouseButtonRelease, source, Qt::LeftButton, Qt::NoButton, Qt::AltModifier);

  // Checked: the clone source reads the merged composite (the Base layer shows
  // through the transparent half of Edit).
  stroke_at(QPoint(36, 8));
  const auto* cloned = edit_layer.pixels().pixel(36, 8);
  CHECK(cloned[0] == 240 && cloned[1] == 90 && cloned[2] == 60 && cloned[3] == 255);

  // Checked healing: detail from the composite source carried into the Edit
  // layer's local tone: (10,20,30) + (240,90,60) - (200,50,25).
  canvas.set_tool(patchy::ui::CanvasTool::Healing);
  stroke_at(QPoint(32, 8));
  const auto* healed = edit_layer.pixels().pixel(32, 8);
  CHECK(healed[0] == 50 && healed[1] == 60 && healed[2] == 65 && healed[3] == 255);

  // Unchecked: the same source point sampled from the active layer alone is
  // fully transparent, so the clone writes transparency.
  canvas.set_retouch_sample_all_layers(false);
  canvas.set_tool(patchy::ui::CanvasTool::Clone);
  stroke_at(QPoint(40, 8));
  CHECK(edit_layer.pixels().pixel(40, 8)[3] == 0);
}

void ui_spot_healing_click_heals_blemish_on_release() {
  patchy::Document document(48, 24, patchy::PixelFormat::rgba8());
  auto pixels = solid_pixels(48, 24, patchy::PixelFormat::rgba8(), QColor(40, 80, 120, 255));
  for (std::int32_t y = 11; y <= 13; ++y) {
    for (std::int32_t x = 15; x <= 17; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = 200;
      px[1] = 200;
      px[2] = 200;
    }
  }
  auto& layer = document.add_pixel_layer("Spot", std::move(pixels));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(192, 96);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::SpotHealing);
  canvas.set_brush_size(12);
  canvas.set_brush_softness(0);
  canvas.set_healing_diffusion(5);
  canvas.show();
  QApplication::processEvents();

  const auto press_point = canvas.widget_position_for_document_point(QPoint(16, 12));
  const auto move_point = canvas.widget_position_for_document_point(QPoint(17, 12));
  send_mouse(canvas, QEvent::MouseButtonPress, press_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, move_point, Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  // Solve-on-release pin: nothing may change while the stroke is being drawn.
  CHECK(layer.pixels().pixel(16, 12)[0] == 200);
  send_mouse(canvas, QEvent::MouseButtonRelease, move_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  // Uniform surroundings collapse the ring-source heal to the base color
  // exactly: source, rim, and both ring tones all read (40,80,120).
  for (const auto point : {QPoint(15, 11), QPoint(16, 12), QPoint(17, 13)}) {
    const auto* healed = layer.pixels().pixel(point.x(), point.y());
    CHECK(healed[0] == 40 && healed[1] == 80 && healed[2] == 120 && healed[3] == 255);
  }
  const auto* outside = layer.pixels().pixel(30, 12);
  CHECK(outside[0] == 40 && outside[1] == 80 && outside[2] == 120 && outside[3] == 255);
}

void ui_spot_healing_escape_cancels_without_pixel_changes() {
  patchy::Document document(48, 24, patchy::PixelFormat::rgba8());
  auto pixels = solid_pixels(48, 24, patchy::PixelFormat::rgba8(), QColor(40, 80, 120, 255));
  auto* blemish = pixels.pixel(16, 12);
  blemish[0] = 200;
  blemish[1] = 200;
  blemish[2] = 200;
  auto& layer = document.add_pixel_layer("Spot", std::move(pixels));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(192, 96);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::SpotHealing);
  canvas.set_brush_size(12);
  canvas.show();
  QApplication::processEvents();

  const auto press_point = canvas.widget_position_for_document_point(QPoint(16, 12));
  const auto move_point = canvas.widget_position_for_document_point(QPoint(18, 12));
  send_mouse(canvas, QEvent::MouseButtonPress, press_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, move_point, Qt::NoButton, Qt::LeftButton);
  send_key(canvas, Qt::Key_Escape);
  send_mouse(canvas, QEvent::MouseButtonRelease, move_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  const auto* untouched = layer.pixels().pixel(16, 12);
  CHECK(untouched[0] == 200 && untouched[1] == 200 && untouched[2] == 200 && untouched[3] == 255);
}

// Draws the Patch tool's freehand outline as a rectangle-ish loop and returns
// with the selection committed.
void draw_patch_outline(patchy::ui::CanvasWidget& canvas, QPoint top_left, QPoint bottom_right) {
  const auto to_widget = [&canvas](QPoint point) {
    return canvas.widget_position_for_document_point(point);
  };
  send_mouse(canvas, QEvent::MouseButtonPress, to_widget(top_left), Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, to_widget(QPoint(bottom_right.x(), top_left.y())), Qt::NoButton,
             Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, to_widget(bottom_right), Qt::NoButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, to_widget(QPoint(top_left.x(), bottom_right.y())), Qt::NoButton,
             Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, to_widget(QPoint(top_left.x(), bottom_right.y())),
             Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
}

void drag_patch_region(patchy::ui::CanvasWidget& canvas, QPoint from, QPoint to) {
  const auto to_widget = [&canvas](QPoint point) {
    return canvas.widget_position_for_document_point(point);
  };
  send_mouse(canvas, QEvent::MouseButtonPress, to_widget(from), Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, to_widget(QPoint((from.x() + to.x()) / 2, to.y())), Qt::NoButton,
             Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, to_widget(to), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  send_mouse(canvas, QEvent::MouseButtonRelease, to_widget(to), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
}

void ui_patch_tool_source_drag_heals_region_on_release() {
  patchy::Document document(64, 24, patchy::PixelFormat::rgba8());
  auto pixels = solid_pixels(64, 24, patchy::PixelFormat::rgba8(), QColor(40, 80, 120, 255));
  for (std::int32_t y = 8; y <= 11; ++y) {
    for (std::int32_t x = 8; x <= 11; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = 220;
      px[1] = 220;
      px[2] = 220;
    }
  }
  auto& layer = document.add_pixel_layer("Patch", std::move(pixels));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(256, 96);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::PatchTool);
  canvas.show();
  canvas.set_zoom(4.0);
  QApplication::processEvents();

  draw_patch_outline(canvas, QPoint(5, 5), QPoint(14, 15));
  CHECK(canvas.selected_document_rect().has_value());
  CHECK(canvas.selected_document_region().contains(QPoint(10, 10)));

  const auto to_widget = [&canvas](QPoint point) {
    return canvas.widget_position_for_document_point(point);
  };
  send_mouse(canvas, QEvent::MouseButtonPress, to_widget(QPoint(10, 10)), Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, to_widget(QPoint(26, 10)), Qt::NoButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, to_widget(QPoint(42, 10)), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  // One-shot-on-release pin: the drag previews but never writes.
  CHECK(layer.pixels().pixel(8, 8)[0] == 220);
  send_mouse(canvas, QEvent::MouseButtonRelease, to_widget(QPoint(42, 10)), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  // The dragged-to area and every ring tone read the uniform base, so the
  // healed region collapses to the base color exactly.
  for (const auto point : {QPoint(8, 8), QPoint(10, 10), QPoint(11, 11)}) {
    const auto* healed = layer.pixels().pixel(point.x(), point.y());
    CHECK(healed[0] == 40 && healed[1] == 80 && healed[2] == 120 && healed[3] == 255);
  }
  // Source mode keeps the selection at the original region.
  CHECK(canvas.selected_document_region().contains(QPoint(10, 10)));
  CHECK(!canvas.selected_document_region().contains(QPoint(42, 10)));
  // The dragged-to area itself is untouched.
  const auto* source_area = layer.pixels().pixel(42, 10);
  CHECK(source_area[0] == 40 && source_area[1] == 80 && source_area[2] == 120 && source_area[3] == 255);
}

void ui_patch_tool_destination_mode_copies_detail_and_moves_selection() {
  patchy::Document document(64, 24, patchy::PixelFormat::rgba8());
  auto pixels = solid_pixels(64, 24, patchy::PixelFormat::rgba8(), QColor(40, 80, 120, 255));
  auto* dot = pixels.pixel(10, 10);
  dot[0] = 90;
  dot[1] = 130;
  dot[2] = 170;
  auto& layer = document.add_pixel_layer("Patch", std::move(pixels));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(256, 96);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::PatchTool);
  canvas.set_patch_tool_mode(patchy::ui::CanvasWidget::PatchToolMode::Destination);
  canvas.show();
  canvas.set_zoom(4.0);
  QApplication::processEvents();

  draw_patch_outline(canvas, QPoint(6, 6), QPoint(15, 15));
  drag_patch_region(canvas, QPoint(10, 10), QPoint(42, 10));

  // The copy lands healed at the drop point: uniform surroundings on both
  // sides make the detail transfer exact.
  const auto* dropped = layer.pixels().pixel(42, 10);
  CHECK(dropped[0] == 90 && dropped[1] == 130 && dropped[2] == 170 && dropped[3] == 255);
  // The original region is untouched and the selection followed the drop.
  const auto* original = layer.pixels().pixel(10, 10);
  CHECK(original[0] == 90 && original[1] == 130 && original[2] == 170 && original[3] == 255);
  CHECK(canvas.selected_document_region().contains(QPoint(42, 10)));
  CHECK(!canvas.selected_document_region().contains(QPoint(10, 10)));
}

void ui_patch_tool_transparent_blends_texture_only() {
  patchy::Document document(64, 24, patchy::PixelFormat::rgba8());
  auto pixels = solid_pixels(64, 24, patchy::PixelFormat::rgba8(), QColor(40, 80, 120, 255));
  for (std::int32_t y = 0; y < 24; ++y) {
    for (std::int32_t x = 36; x < 64; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = 90;
      px[1] = 60;
      px[2] = 30;
    }
  }
  auto* dot = pixels.pixel(42, 10);
  dot[0] = 140;
  dot[1] = 110;
  dot[2] = 80;
  auto& layer = document.add_pixel_layer("Patch", std::move(pixels));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(256, 96);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::PatchTool);
  canvas.set_patch_tool_transparent(true);
  canvas.show();
  canvas.set_zoom(4.0);
  QApplication::processEvents();

  draw_patch_outline(canvas, QPoint(6, 6), QPoint(15, 15));
  drag_patch_region(canvas, QPoint(10, 10), QPoint(42, 10));

  // Transparent transfers only the source's high-pass detail over the
  // destination: the dot adds (almost all of) its +50 offset onto the base -
  // the box-blurred local mean absorbs a sliver - and flat source areas stay
  // within a couple of levels of the untouched base.
  const auto* textured = layer.pixels().pixel(10, 10);
  CHECK(std::abs(static_cast<int>(textured[0]) - 90) <= 3);
  CHECK(std::abs(static_cast<int>(textured[1]) - 130) <= 3);
  CHECK(std::abs(static_cast<int>(textured[2]) - 170) <= 3);
  CHECK(textured[3] == 255);
  const auto* flat = layer.pixels().pixel(12, 12);
  CHECK(std::abs(static_cast<int>(flat[0]) - 40) <= 3);
  CHECK(std::abs(static_cast<int>(flat[1]) - 80) <= 3);
  CHECK(std::abs(static_cast<int>(flat[2]) - 120) <= 3);
  CHECK(flat[3] == 255);
}

void ui_patch_tool_click_inside_is_noop_and_escape_cancels() {
  patchy::Document document(64, 24, patchy::PixelFormat::rgba8());
  auto pixels = solid_pixels(64, 24, patchy::PixelFormat::rgba8(), QColor(40, 80, 120, 255));
  auto* marker = pixels.pixel(10, 10);
  marker[0] = 220;
  auto& layer = document.add_pixel_layer("Patch", std::move(pixels));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(256, 96);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::PatchTool);
  canvas.show();
  canvas.set_zoom(4.0);
  QApplication::processEvents();

  draw_patch_outline(canvas, QPoint(6, 6), QPoint(15, 15));
  const auto to_widget = [&canvas](QPoint point) {
    return canvas.widget_position_for_document_point(point);
  };

  // A click inside the selection is a no-op that keeps the selection.
  send_mouse(canvas, QEvent::MouseButtonPress, to_widget(QPoint(10, 10)), Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, to_widget(QPoint(10, 10)), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(layer.pixels().pixel(10, 10)[0] == 220);
  CHECK(canvas.selected_document_region().contains(QPoint(10, 10)));

  // Escape mid-drag discards the gesture without writing.
  send_mouse(canvas, QEvent::MouseButtonPress, to_widget(QPoint(10, 10)), Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, to_widget(QPoint(42, 10)), Qt::NoButton, Qt::LeftButton);
  send_key(canvas, Qt::Key_Escape);
  send_mouse(canvas, QEvent::MouseButtonRelease, to_widget(QPoint(42, 10)), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(layer.pixels().pixel(10, 10)[0] == 220);
  const auto* drop_area = layer.pixels().pixel(42, 10);
  CHECK(drop_area[0] == 40 && drop_area[1] == 80 && drop_area[2] == 120 && drop_area[3] == 255);
}

// Destination mode dropped onto strongly contrasting content: the membrane
// must shift the copied texture to the drop area's tone (uniform boundary
// offsets solve to a constant), never emit unbounded per-pixel values.
void ui_patch_tool_destination_onto_contrast_adapts_tone() {
  patchy::Document document(128, 64, patchy::PixelFormat::rgba8());
  auto pixels = solid_pixels(128, 64, patchy::PixelFormat::rgba8(), QColor(230, 150, 60, 255));
  for (std::int32_t y = 0; y < 64; ++y) {
    for (std::int32_t x = 64; x < 128; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = 20;
      px[1] = 40;
      px[2] = 90;
    }
  }
  auto& layer = document.add_pixel_layer("Contrast", std::move(pixels));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(300, 160);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::PatchTool);
  canvas.set_patch_tool_mode(patchy::ui::CanvasWidget::PatchToolMode::Destination);
  canvas.show();
  canvas.set_zoom(2.0);
  QApplication::processEvents();

  draw_patch_outline(canvas, QPoint(8, 8), QPoint(28, 28));
  drag_patch_region(canvas, QPoint(18, 18), QPoint(90, 40));
  save_widget_artifact("ui_patch_destination_contrast_after", canvas);

  // The copy carries the orange region's (flat) texture with a constant
  // boundary offset onto the blue side, so interior drop pixels must land on
  // the blue base tone.
  for (const auto point : {QPoint(88, 38), QPoint(90, 40), QPoint(93, 42)}) {
    const auto* healed = layer.pixels().pixel(point.x(), point.y());
    CHECK(std::abs(static_cast<int>(healed[0]) - 20) <= 3);
    CHECK(std::abs(static_cast<int>(healed[1]) - 40) <= 3);
    CHECK(std::abs(static_cast<int>(healed[2]) - 90) <= 3);
    CHECK(healed[3] == 255);
  }
}

// A perfectly closed outline (release exactly on the press point) must commit
// the selection, not read as a click-to-deselect: the click test measures the
// whole traced path's extent, not press-vs-release distance. Covers the Patch
// outline and the plain Lasso, which share the gesture.
void ui_patch_and_lasso_closed_loop_outline_still_selects() {
  patchy::Document document(64, 24, patchy::PixelFormat::rgba8());
  auto pixels = solid_pixels(64, 24, patchy::PixelFormat::rgba8(), QColor(40, 80, 120, 255));
  document.add_pixel_layer("Loop", std::move(pixels));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(256, 96);
  canvas.set_document(&document);
  canvas.show();
  canvas.set_zoom(4.0);
  QApplication::processEvents();

  const auto to_widget = [&canvas](QPoint point) {
    return canvas.widget_position_for_document_point(point);
  };
  const auto draw_closed_loop = [&](QPoint top_left, QPoint bottom_right) {
    send_mouse(canvas, QEvent::MouseButtonPress, to_widget(top_left), Qt::LeftButton, Qt::LeftButton);
    send_mouse(canvas, QEvent::MouseMove, to_widget(QPoint(bottom_right.x(), top_left.y())), Qt::NoButton,
               Qt::LeftButton);
    send_mouse(canvas, QEvent::MouseMove, to_widget(bottom_right), Qt::NoButton, Qt::LeftButton);
    send_mouse(canvas, QEvent::MouseMove, to_widget(QPoint(top_left.x(), bottom_right.y())), Qt::NoButton,
               Qt::LeftButton);
    // Close the loop exactly on the press point before releasing there.
    send_mouse(canvas, QEvent::MouseMove, to_widget(top_left), Qt::NoButton, Qt::LeftButton);
    send_mouse(canvas, QEvent::MouseButtonRelease, to_widget(top_left), Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
  };

  canvas.set_tool(patchy::ui::CanvasTool::PatchTool);
  draw_closed_loop(QPoint(6, 6), QPoint(15, 15));
  CHECK(canvas.selected_document_region().contains(QPoint(10, 10)));

  canvas.set_tool(patchy::ui::CanvasTool::Lasso);
  draw_closed_loop(QPoint(30, 6), QPoint(44, 16));
  CHECK(canvas.selected_document_region().contains(QPoint(37, 11)));

  // A plain click must still deselect (Replace mode), path-extent test or not.
  send_mouse(canvas, QEvent::MouseButtonPress, to_widget(QPoint(50, 10)), Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, to_widget(QPoint(50, 10)), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(!canvas.selected_document_rect().has_value());
}

// Renders before/after artifacts of both heals on a textured gradient - the
// fixture class where the pre-membrane math showed starburst streaks and tone
// rings. Inspect ui_spot_healing_gallery_* / ui_patch_tool_gallery_* when
// touching the healing algorithms.
void ui_spot_healing_and_patch_texture_gallery() {
  constexpr std::int32_t width = 256;
  constexpr std::int32_t height = 160;
  patchy::Document document(width, height, patchy::PixelFormat::rgba8());
  auto pixels = solid_pixels(width, height, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 255));
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      // Smooth diagonal skin-tone gradient plus a fine deterministic weave.
      const auto ramp = static_cast<double>(x) / width * 60.0 + static_cast<double>(y) / height * 40.0;
      const auto weave = 6.0 * std::sin(x * 0.7) * std::sin(y * 0.55);
      auto* px = pixels.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(std::clamp(185.0 + ramp * 0.5 + weave, 0.0, 255.0));
      px[1] = static_cast<std::uint8_t>(std::clamp(135.0 + ramp * 0.4 + weave, 0.0, 255.0));
      px[2] = static_cast<std::uint8_t>(std::clamp(110.0 + ramp * 0.3 + weave, 0.0, 255.0));
    }
  }
  // Two dark blemishes: one for the spot heal, one inside the patch region.
  const auto stamp_blemish = [&pixels](QPoint center, int radius) {
    for (std::int32_t y = center.y() - radius; y <= center.y() + radius; ++y) {
      for (std::int32_t x = center.x() - radius; x <= center.x() + radius; ++x) {
        const auto dx = x - center.x();
        const auto dy = y - center.y();
        if (dx * dx + dy * dy > radius * radius || x < 0 || y < 0 || x >= width || y >= height) {
          continue;
        }
        auto* px = pixels.pixel(x, y);
        px[0] = static_cast<std::uint8_t>(px[0] * 2 / 5 + 20);
        px[1] = static_cast<std::uint8_t>(px[1] * 2 / 5 + 10);
        px[2] = static_cast<std::uint8_t>(px[2] * 2 / 5 + 10);
      }
    }
  };
  stamp_blemish(QPoint(64, 60), 11);
  stamp_blemish(QPoint(64, 116), 9);
  document.add_pixel_layer("Texture", std::move(pixels));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(width + 72, height + 72);
  canvas.set_document(&document);
  canvas.show();
  canvas.set_zoom(1.0);
  QApplication::processEvents();
  save_widget_artifact("ui_healing_gallery_before", canvas);

  canvas.set_tool(patchy::ui::CanvasTool::SpotHealing);
  canvas.set_brush_size(30);
  canvas.set_brush_softness(35);
  const auto spot = canvas.widget_position_for_document_point(QPoint(64, 60));
  send_mouse(canvas, QEvent::MouseButtonPress, spot, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, spot, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  save_widget_artifact("ui_spot_healing_gallery_after", canvas);

  canvas.set_tool(patchy::ui::CanvasTool::PatchTool);
  draw_patch_outline(canvas, QPoint(48, 100), QPoint(82, 132));
  drag_patch_region(canvas, QPoint(64, 116), QPoint(180, 116));
  QApplication::processEvents();
  save_widget_artifact("ui_patch_tool_gallery_after", canvas);

  // Destination mode over the same textured gradient: copy a clean piece onto
  // a brighter area, then chain a second drop from the moved selection.
  canvas.set_patch_tool_mode(patchy::ui::CanvasWidget::PatchToolMode::Destination);
  draw_patch_outline(canvas, QPoint(150, 30), QPoint(184, 62));
  drag_patch_region(canvas, QPoint(166, 46), QPoint(96, 100));
  drag_patch_region(canvas, QPoint(96, 100), QPoint(200, 110));
  QApplication::processEvents();
  save_widget_artifact("ui_patch_destination_gallery_after", canvas);

  // Transparent over the same texture: near-invisible by design (same-texture
  // detail transfer), and above all with NO printed selection outline.
  canvas.set_patch_tool_mode(patchy::ui::CanvasWidget::PatchToolMode::Source);
  canvas.set_patch_tool_transparent(true);
  draw_patch_outline(canvas, QPoint(110, 40), QPoint(150, 80));
  drag_patch_region(canvas, QPoint(130, 60), QPoint(40, 60));
  QApplication::processEvents();
  save_widget_artifact("ui_patch_transparent_gallery_after", canvas);
  canvas.set_patch_tool_transparent(false);
}

void ui_patch_options_sync_canvas_and_persist() {
  SettingsValueRestorer mode_restorer(QStringLiteral("tools/patchMode"));
  SettingsValueRestorer transparent_restorer(QStringLiteral("tools/patchTransparent"));
  SettingsValueRestorer sample_restorer(QStringLiteral("tools/retouchSampleAllLayers"));
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  CHECK(canvas->retouch_sample_all_layers());

  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("patchModeCombo"));
  CHECK(mode_combo != nullptr);
  mode_combo->setCurrentIndex(1);
  CHECK(canvas->patch_tool_mode() == patchy::ui::CanvasWidget::PatchToolMode::Destination);

  auto* transparent_check = window.findChild<QCheckBox*>(QStringLiteral("patchTransparentCheck"));
  CHECK(transparent_check != nullptr);
  transparent_check->setChecked(true);
  CHECK(canvas->patch_tool_transparent());

  auto* sample_check = window.findChild<QCheckBox*>(QStringLiteral("retouchSampleAllLayersCheck"));
  CHECK(sample_check != nullptr);
  sample_check->setChecked(false);
  CHECK(!canvas->retouch_sample_all_layers());

  auto settings = patchy::ui::app_settings();
  CHECK(settings.value(QStringLiteral("tools/patchMode")).toInt() ==
        static_cast<int>(patchy::ui::CanvasWidget::PatchToolMode::Destination));
  CHECK(settings.value(QStringLiteral("tools/patchTransparent")).toBool());
  CHECK(!settings.value(QStringLiteral("tools/retouchSampleAllLayers")).toBool());
}

}  // namespace

std::vector<patchy::test::TestCase> brush_engine_stroke_tests_part2() {
  return {
      {"ui_retouch_sample_all_layers_switches_clone_and_healing_source",
       ui_retouch_sample_all_layers_switches_clone_and_healing_source},
      {"ui_spot_healing_click_heals_blemish_on_release", ui_spot_healing_click_heals_blemish_on_release},
      {"ui_spot_healing_escape_cancels_without_pixel_changes",
       ui_spot_healing_escape_cancels_without_pixel_changes},
      {"ui_patch_tool_source_drag_heals_region_on_release",
       ui_patch_tool_source_drag_heals_region_on_release},
      {"ui_patch_tool_destination_mode_copies_detail_and_moves_selection",
       ui_patch_tool_destination_mode_copies_detail_and_moves_selection},
      {"ui_patch_tool_transparent_blends_texture_only", ui_patch_tool_transparent_blends_texture_only},
      {"ui_patch_tool_click_inside_is_noop_and_escape_cancels",
       ui_patch_tool_click_inside_is_noop_and_escape_cancels},
      {"ui_patch_tool_destination_onto_contrast_adapts_tone",
       ui_patch_tool_destination_onto_contrast_adapts_tone},
      {"ui_patch_and_lasso_closed_loop_outline_still_selects",
       ui_patch_and_lasso_closed_loop_outline_still_selects},
      {"ui_spot_healing_and_patch_texture_gallery", ui_spot_healing_and_patch_texture_gallery},
      {"ui_patch_options_sync_canvas_and_persist", ui_patch_options_sync_canvas_and_persist},
  };
}
