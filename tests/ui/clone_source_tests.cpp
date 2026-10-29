// Clone source settings (GitHub issue 424; docs/healing.md "Clone source settings"): the
// Clone Stamp's scale, rotation and flips (byte-identical output at the identity), the
// editable offset, the four source slots, the Healing Brush keeping its untransformed
// source, the overlay under the hovered brush, and the gear popup.
#include "core/clone_source_transform.hpp"
#include "core/document.hpp"
#include "core/document_depth.hpp"
#include "core/pixel_depth.hpp"
#include "ui/app_settings.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/clone_source_panel.hpp"
#include "ui/main_window.hpp"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QDoubleSpinBox>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QStringList>
#include <QToolBar>
#include <QToolButton>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <string_view>
#include <utility>
#include <vector>
#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

namespace {

using namespace patchy::test::ui;
using patchy::CloneSourceTransform;
using patchy::ui::CanvasTool;
using patchy::ui::CanvasWidget;

// Every pixel a different, reproducible color: a coordinate hash.
patchy::PixelBuffer noise_pixels(std::int32_t width, std::int32_t height) {
  auto pixels = solid_pixels(width, height, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 255));
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      auto value = static_cast<std::uint32_t>(x) * 73856093U ^ static_cast<std::uint32_t>(y) * 19349663U;
      value ^= value >> 13U;
      value *= 0x5bd1e995U;
      value ^= value >> 15U;
      auto* px = pixels.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(value);
      px[1] = static_cast<std::uint8_t>(value >> 8U);
      px[2] = static_cast<std::uint8_t>(value >> 16U);
      px[3] = 255;
    }
  }
  return pixels;
}

// R and G name the coordinate (4 levels per pixel), so a sample says where it came from.
patchy::PixelBuffer coordinate_pixels(std::int32_t size) {
  auto pixels = solid_pixels(size, size, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 255));
  for (std::int32_t y = 0; y < size; ++y) {
    for (std::int32_t x = 0; x < size; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(x * 4);
      px[1] = static_cast<std::uint8_t>(y * 4);
      px[2] = 100;
      px[3] = 255;
    }
  }
  return pixels;
}

void prepare_clone_canvas(CanvasWidget& canvas, patchy::Document& document, int brush_size, int softness,
                          int opacity) {
  canvas.resize(260, 200);
  canvas.set_document(&document);
  canvas.set_tool(CanvasTool::Clone);
  canvas.set_brush_size(brush_size);
  canvas.set_brush_softness(softness);
  canvas.set_brush_opacity(opacity);
  canvas.show();
  QApplication::processEvents();
}

void alt_click(CanvasWidget& canvas, QPoint document_point) {
  const auto at = canvas.widget_position_for_document_point(document_point);
  send_mouse(canvas, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
  send_mouse(canvas, QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton, Qt::AltModifier);
}

void click(CanvasWidget& canvas, QPoint document_point) {
  const auto at = canvas.widget_position_for_document_point(document_point);
  send_mouse(canvas, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
}

// Opens the gear's popup under it, as its click does (showMenu would block in exec()).
void open_gear_popup(QToolButton& gear, QMenu& menu) {
  menu.popup(gear.mapToGlobal(QPoint(0, gear.height())));
  QApplication::processEvents();
}

CloneSourceTransform flipped(bool horizontal, bool vertical, double scale = 100.0, double rotation = 0.0) {
  CloneSourceTransform transform{scale, rotation};
  transform.flip_horizontal = horizontal;
  transform.flip_vertical = vertical;
  return transform;
}

std::array<std::uint8_t, 4> pixel_at(const patchy::Layer& layer, QPoint point) {
  const auto* px = std::as_const(layer).pixels().pixel(point.x(), point.y());
  return {px[0], px[1], px[2], px[3]};
}

// At 100% and 0 degrees a clone stroke writes exactly what it wrote before the source
// settings existed: a fresh canvas, one whose transform went to 50% and 30 degrees and was
// reset, and one at a whole turn (which folds to 0) give the same bytes, soft edges and
// partial opacity included, on 8-bit and 16-bit layers; a hard dab copies the source bytes.
void ui_clone_source_identity_transform_keeps_stroke_bytes() {
  patchy::set_deep_editing_override(true);
  for (const bool deep : {false, true}) {
    const auto run = [deep](const std::function<void(CanvasWidget&)>& setup) {
      patchy::Document document(96, 64, patchy::PixelFormat::rgba8());
      const auto id = document.add_pixel_layer("Noise", noise_pixels(96, 64)).id();
      if (deep) {
        patchy::convert_document_depth(document, patchy::BitDepth::UInt16);
      }
      CanvasWidget canvas;
      prepare_clone_canvas(canvas, document, 15, 60, 70);
      setup(canvas);
      alt_click(canvas, QPoint(20, 30));
      drag(canvas, canvas.widget_position_for_document_point(QPoint(55, 24)),
           canvas.widget_position_for_document_point(QPoint(80, 40)));
      QApplication::processEvents();
      const auto bytes = std::as_const(*document.find_layer(id)).pixels().data();
      return std::vector<std::uint8_t>(bytes.begin(), bytes.end());
    };
    const auto untouched = run([](CanvasWidget&) {});
    const auto reset = run([](CanvasWidget& canvas) {
      canvas.set_clone_source_transform(CloneSourceTransform{50.0, 30.0});
      CHECK(!canvas.clone_source_transform().is_identity());
      canvas.set_clone_source_transform(CloneSourceTransform{});
    });
    const auto whole_turn = run([](CanvasWidget& canvas) {
      canvas.set_clone_source_transform(CloneSourceTransform{100.0, 360.0});
      CHECK(canvas.clone_source_transform().is_identity());
    });
    const auto transformed = run([](CanvasWidget& canvas) {
      canvas.set_clone_source_transform(CloneSourceTransform{125.0, 0.0});
    });
    CHECK(!untouched.empty());
    CHECK(untouched == reset);
    CHECK(untouched == whole_turn);
    CHECK(untouched != transformed);
  }
  patchy::set_deep_editing_override(std::nullopt);

  // A hard, opaque dab copies the source bytes one for one.
  patchy::Document document(96, 64, patchy::PixelFormat::rgba8());
  auto& layer = document.add_pixel_layer("Noise", noise_pixels(96, 64));
  const auto expected = pixel_at(layer, QPoint(22, 31));
  CanvasWidget canvas;
  prepare_clone_canvas(canvas, document, 9, 0, 100);
  alt_click(canvas, QPoint(20, 30));
  click(canvas, QPoint(70, 40));
  CHECK(pixel_at(layer, QPoint(72, 41)) == expected);
}

// Scale and rotation turn the sampled content about the source point (8 and 16 bits).
void ui_clone_source_scale_and_rotation_transform_the_sample() {
  patchy::set_deep_editing_override(true);
  for (const bool deep : {false, true}) {
    const auto sample = [deep](const CloneSourceTransform& transform, const std::vector<QPoint>& probes) {
      patchy::Document document(64, 64, patchy::PixelFormat::rgba8());
      auto& layer = document.add_pixel_layer("Coordinates", coordinate_pixels(64));
      const auto id = layer.id();
      if (deep) {
        patchy::convert_document_depth(document, patchy::BitDepth::UInt16);
      }
      CanvasWidget canvas;
      prepare_clone_canvas(canvas, document, 9, 0, 100);
      canvas.set_clone_source_transform(transform);
      alt_click(canvas, QPoint(16, 16));
      click(canvas, QPoint(48, 48));
      std::vector<std::array<int, 2>> read;
      const auto* target = document.find_layer(id);
      for (const auto probe : probes) {
        const auto& pixels = std::as_const(*target).pixels();
        const auto value = patchy::load_pixel(pixels.format(), pixels.pixel(probe.x(), probe.y()));
        read.push_back({static_cast<int>(std::lround(value[0])), static_cast<int>(std::lround(value[1]))});
      }
      return read;
    };
    // 200%: two destination pixels per source pixel; a half step averages the two.
    const auto doubled = sample(CloneSourceTransform{200.0, 0.0}, {QPoint(48, 48), QPoint(50, 48), QPoint(49, 48),
                                                                    QPoint(48, 46)});
    CHECK((doubled[0] == std::array<int, 2>{64, 64}));
    CHECK((doubled[1] == std::array<int, 2>{68, 64}));
    CHECK((doubled[2] == std::array<int, 2>{66, 64}));
    CHECK((doubled[3] == std::array<int, 2>{64, 60}));
    // 90 degrees clockwise: right of the anchor samples above the source point, below it
    // samples to the right.
    const auto turned = sample(CloneSourceTransform{100.0, 90.0}, {QPoint(49, 48), QPoint(48, 49), QPoint(46, 48)});
    CHECK((turned[0] == std::array<int, 2>{64, 60}));
    CHECK((turned[1] == std::array<int, 2>{68, 64}));
    CHECK((turned[2] == std::array<int, 2>{64, 72}));
  }
  patchy::set_deep_editing_override(std::nullopt);
}

// Flip Horizontal and Flip Vertical (GitHub issue 424 follow-up) mirror the sampled content
// about the source point: right of the anchor samples left of the source point, below it
// samples above (8 and 16 bits). A hard dab at 100% copies whole pixels.
void ui_clone_source_flip_paints_mirrored_pixels() {
  patchy::set_deep_editing_override(true);
  for (const bool deep : {false, true}) {
    const auto sample = [deep](const CloneSourceTransform& transform, const std::vector<QPoint>& probes) {
      patchy::Document document(64, 64, patchy::PixelFormat::rgba8());
      auto& layer = document.add_pixel_layer("Coordinates", coordinate_pixels(64));
      const auto id = layer.id();
      if (deep) {
        patchy::convert_document_depth(document, patchy::BitDepth::UInt16);
      }
      CanvasWidget canvas;
      prepare_clone_canvas(canvas, document, 9, 0, 100);
      canvas.set_clone_source_transform(transform);
      alt_click(canvas, QPoint(16, 16));
      click(canvas, QPoint(48, 48));
      std::vector<std::array<int, 2>> read;
      const auto* target = document.find_layer(id);
      for (const auto probe : probes) {
        const auto& pixels = std::as_const(*target).pixels();
        const auto value = patchy::load_pixel(pixels.format(), pixels.pixel(probe.x(), probe.y()));
        read.push_back({static_cast<int>(std::lround(value[0])), static_cast<int>(std::lround(value[1]))});
      }
      return read;
    };
    const std::vector<QPoint> probes = {QPoint(48, 48), QPoint(50, 48), QPoint(48, 51), QPoint(46, 47)};
    // Unflipped for reference: (50, 48) samples (18, 16).
    const auto plain = sample(CloneSourceTransform{}, probes);
    CHECK((plain[1] == std::array<int, 2>{72, 64}));
    const auto horizontal = sample(flipped(true, false), probes);
    CHECK((horizontal[0] == std::array<int, 2>{64, 64}));
    CHECK((horizontal[1] == std::array<int, 2>{56, 64}));
    CHECK((horizontal[2] == std::array<int, 2>{64, 76}));
    CHECK((horizontal[3] == std::array<int, 2>{72, 60}));
    const auto vertical = sample(flipped(false, true), probes);
    CHECK((vertical[0] == std::array<int, 2>{64, 64}));
    CHECK((vertical[1] == std::array<int, 2>{72, 64}));
    CHECK((vertical[2] == std::array<int, 2>{64, 52}));
    CHECK((vertical[3] == std::array<int, 2>{56, 68}));
    const auto both = sample(flipped(true, true), probes);
    CHECK((both[1] == std::array<int, 2>{56, 64}));
    CHECK((both[2] == std::array<int, 2>{64, 52}));
    CHECK((both[3] == std::array<int, 2>{72, 68}));
    // With 200%: a step of two right of the anchor samples one left of the source point.
    const auto doubled = sample(flipped(true, false, 200.0), {QPoint(50, 48)});
    CHECK((doubled[0] == std::array<int, 2>{60, 64}));
  }
  patchy::set_deep_editing_override(std::nullopt);
}

// The offset can be typed in; each of the four slots keeps its own source, offset and
// transform, and switching back restores them.
void ui_clone_source_offset_edit_and_slots_switch_sources() {
  patchy::Document document(64, 64, patchy::PixelFormat::rgba8());
  auto& layer = document.add_pixel_layer("Coordinates", coordinate_pixels(64));
  CanvasWidget canvas;
  prepare_clone_canvas(canvas, document, 1, 0, 100);
  int changes = 0;
  canvas.set_clone_source_changed_callback([&changes] { ++changes; });

  CHECK(canvas.clone_source_slot() == 0);
  alt_click(canvas, QPoint(4, 4));
  CHECK(changes == 1);
  auto slot = canvas.clone_source_slot_state(0);
  CHECK(slot.set && slot.point == QPoint(4, 4) && !slot.offset_set);
  // A typed offset fixes the next strokes (Aligned is on).
  canvas.set_clone_source_offset(QPoint(-20, -30));
  slot = canvas.clone_source_slot_state(0);
  CHECK(slot.offset_set && slot.offset == QPoint(-20, -30));
  click(canvas, QPoint(40, 50));
  CHECK((pixel_at(layer, QPoint(40, 50)) == std::array<std::uint8_t, 4>{80, 80, 100, 255}));
  CHECK(canvas.clone_source_slot_state(0).offset == QPoint(-20, -30));

  // Slot 2: empty until its own Alt-click, with its own transform.
  canvas.set_clone_source_slot(1);
  CHECK(canvas.clone_source_slot() == 1);
  CHECK(!canvas.clone_source_slot_state(1).set);
  CHECK(canvas.clone_source_transform().is_identity());
  alt_click(canvas, QPoint(10, 2));
  canvas.set_clone_source_transform(flipped(true, false, 200.0));
  CHECK(canvas.clone_source_slot_state(0).point == QPoint(4, 4));
  CHECK(canvas.clone_source_slot_state(0).transform.is_identity());
  CHECK(canvas.clone_source_slot_state(1).point == QPoint(10, 2));

  // Back to slot 1: its source, offset and identity transform return.
  canvas.set_clone_source_slot(0);
  CHECK(canvas.clone_source_transform().is_identity());
  click(canvas, QPoint(44, 52));
  CHECK((pixel_at(layer, QPoint(44, 52)) == std::array<std::uint8_t, 4>{96, 88, 100, 255}));
  canvas.set_clone_source_slot(1);
  CHECK(canvas.clone_source_transform().scale_percent == 200.0);
  CHECK(canvas.clone_source_transform().flip_horizontal && !canvas.clone_source_transform().flip_vertical);
  CHECK(canvas.clone_source_slot_state(1).set);
  CHECK(changes >= 6);
}

// The Healing Brush shares the sources and offsets but never the transform: its source
// stays the plain Alt-click offset (docs/legal-constraints.md, the healing carve-out).
void ui_clone_source_healing_brush_ignores_transform() {
  patchy::Document document(64, 64, patchy::PixelFormat::rgba8());
  auto& layer = document.add_pixel_layer("Coordinates", coordinate_pixels(64));
  CanvasWidget canvas;
  prepare_clone_canvas(canvas, document, 9, 0, 100);
  canvas.set_tool(CanvasTool::Healing);
  canvas.set_healing_mode(patchy::ui::HealingMode::Replace);
  canvas.set_clone_source_transform(flipped(true, true, 200.0, 90.0));
  alt_click(canvas, QPoint(16, 16));
  click(canvas, QPoint(48, 48));
  CHECK((pixel_at(layer, QPoint(50, 48)) == std::array<std::uint8_t, 4>{72, 64, 100, 255}));
  CHECK((pixel_at(layer, QPoint(48, 50)) == std::array<std::uint8_t, 4>{64, 72, 100, 255}));
}

// With Show overlay on, the source shows under the hovered brush (at the overlay opacity,
// clipped to the brush); off, or while painting, the canvas shows the target.
void ui_clone_source_overlay_previews_source_under_brush() {
  patchy::Document document(96, 64, patchy::PixelFormat::rgba8());
  auto pixels = solid_pixels(96, 64, patchy::PixelFormat::rgba8(), QColor(30, 40, 210, 255));
  fill_pixel_rect(pixels, QRect(0, 0, 48, 64), QColor(220, 40, 30, 255));
  document.add_pixel_layer("Halves", std::move(pixels));
  CanvasWidget canvas;
  prepare_clone_canvas(canvas, document, 20, 0, 100);
  alt_click(canvas, QPoint(20, 32));
  const auto hover = canvas.widget_position_for_document_point(QPoint(72, 32));
  const auto pixel_under_brush = [&canvas, hover] {
    send_mouse(canvas, QEvent::MouseMove, hover, Qt::NoButton, Qt::NoButton);
    QApplication::processEvents();
    const auto image = canvas.grab().toImage();
    return image.pixelColor(hover + QPoint(2, 2));
  };
  // On at 100% by default (GitHub issue 426).
  CHECK(canvas.clone_overlay_shown());
  CHECK(canvas.clone_overlay_opacity() == 100);
  canvas.set_clone_overlay_shown(false);
  const auto plain = pixel_under_brush();
  CHECK(color_close(plain, QColor(30, 40, 210), 12));

  canvas.set_clone_overlay_shown(true);
  canvas.set_clone_overlay_opacity(100);
  const auto overlaid = pixel_under_brush();
  CHECK(color_close(overlaid, QColor(220, 40, 30), 12));
  // Outside the brush the target shows.
  const auto outside = canvas.grab().toImage().pixelColor(
      canvas.widget_position_for_document_point(QPoint(90, 32)));
  CHECK(color_close(outside, QColor(30, 40, 210), 12));

  canvas.set_clone_overlay_opacity(50);
  const auto half = pixel_under_brush();
  CHECK(half.red() > 90 && half.red() < 160 && half.blue() > 90 && half.blue() < 160);

  canvas.set_clone_overlay_shown(false);
  CHECK(color_close(pixel_under_brush(), QColor(30, 40, 210), 12));
}

// The overlay shows the flipped source: with the source point just left of the red/blue
// boundary, the unflipped preview is red left of the pointer and blue right of it, and
// Flip Horizontal swaps them.
void ui_clone_source_overlay_shows_flipped_source() {
  patchy::Document document(96, 64, patchy::PixelFormat::rgba8());
  auto pixels = solid_pixels(96, 64, patchy::PixelFormat::rgba8(), QColor(30, 40, 210, 255));
  fill_pixel_rect(pixels, QRect(0, 0, 48, 64), QColor(220, 40, 30, 255));
  document.add_pixel_layer("Halves", std::move(pixels));
  CanvasWidget canvas;
  prepare_clone_canvas(canvas, document, 24, 0, 100);
  canvas.set_clone_aligned(true);
  canvas.set_clone_overlay_shown(true);
  canvas.set_clone_overlay_opacity(100);
  alt_click(canvas, QPoint(47, 32));
  const auto hover = canvas.widget_position_for_document_point(QPoint(72, 32));
  const auto sides = [&canvas, hover] {
    send_mouse(canvas, QEvent::MouseMove, hover, Qt::NoButton, Qt::NoButton);
    QApplication::processEvents();
    const auto image = canvas.grab().toImage();
    return std::pair{image.pixelColor(canvas.widget_position_for_document_point(QPoint(66, 32))),
                     image.pixelColor(canvas.widget_position_for_document_point(QPoint(78, 32)))};
  };
  const auto [plain_left, plain_right] = sides();
  CHECK(color_close(plain_left, QColor(220, 40, 30), 12));
  CHECK(color_close(plain_right, QColor(30, 40, 210), 12));
  canvas.set_clone_source_transform(flipped(true, false));
  const auto [mirrored_left, mirrored_right] = sides();
  CHECK(color_close(mirrored_left, QColor(30, 40, 210), 12));
  CHECK(color_close(mirrored_right, QColor(220, 40, 30), 12));
}

// The gear on the Clone Stamp bar opens the settings and edits reach the canvas. The
// gear is the only way in: no Window menu entry, no hotkey command.
void ui_clone_source_gear_popup_drives_canvas() {
  SettingsValueRestorer saved_aligned(QStringLiteral("tools/cloneAligned"));
  SettingsValueRestorer saved_overlay(QStringLiteral("tools/cloneOverlay"));
  SettingsValueRestorer saved_opacity(QStringLiteral("tools/cloneOverlayOpacity"));
  auto&& [window, canvas] = ShownWindowFixture{};
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  QApplication::processEvents();
  auto* gear = window.findChild<QToolButton*>(QStringLiteral("cloneSourceOptionsButton"));
  auto* menu = window.findChild<QMenu*>(QStringLiteral("cloneSourceMenu"));
  auto* panel = window.findChild<patchy::ui::CloneSourcePanel*>(QStringLiteral("cloneSourcePanel"));
  CHECK(gear != nullptr && menu != nullptr && panel != nullptr);
  if (gear == nullptr || menu == nullptr || panel == nullptr) {
    return;
  }
  CHECK(!gear->isVisible());

  CHECK(window.findChild<QAction*>(QStringLiteral("windowCloneSourceAction")) == nullptr);
  for (auto* action : window.findChildren<QAction*>()) {
    CHECK(action->text().remove(QLatin1Char('&')) != QStringLiteral("Clone Source"));
  }

  require_action_by_text(window, QStringLiteral("Clone"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == CanvasTool::Clone);
  CHECK(gear->isVisible());
  open_gear_popup(*gear, *menu);
  CHECK(menu->isVisible());
  // An earlier test may have saved Aligned off; the offset fields need it on.
  if (auto* aligned = window.findChild<QCheckBox*>(QStringLiteral("cloneAlignedCheck")); aligned != nullptr) {
    aligned->setChecked(true);
  }
  CHECK(canvas->clone_aligned());
  CHECK(!panel->source_label()->text().isEmpty());
  CHECK(!panel->offset_x_spin()->isEnabled());  // no source yet

  canvas->set_brush_size(30);
  alt_click(*canvas, QPoint(60, 60));
  QApplication::processEvents();
  CHECK(panel->source_label()->text() == QStringLiteral("60, 60"));
  CHECK(panel->offset_x_spin()->isEnabled());

  panel->scale_spin()->setValue(150.0);
  CHECK(canvas->clone_source_transform().scale_percent == 150.0);
  panel->rotation_spin()->setValue(-45.0);
  CHECK(canvas->clone_source_transform().rotation_degrees == -45.0);
  CHECK(panel->reset_button()->isEnabled());
  // Flip Horizontal and Vertical are toggles stored with the source.
  CHECK(!panel->flip_horizontal_button()->isChecked() && !panel->flip_vertical_button()->isChecked());
  panel->flip_horizontal_button()->click();
  CHECK(canvas->clone_source_transform().flip_horizontal);
  CHECK(canvas->clone_source_transform().scale_percent == 150.0);
  panel->flip_vertical_button()->click();
  CHECK(canvas->clone_source_transform().flip_vertical);
  panel->flip_vertical_button()->click();
  CHECK(!canvas->clone_source_transform().flip_vertical);
  CHECK(panel->flip_horizontal_button()->isChecked() && !panel->flip_vertical_button()->isChecked());
  panel->offset_x_spin()->setValue(-120);
  CHECK(canvas->clone_source_slot_state(0).offset_set);
  CHECK(canvas->clone_source_slot_state(0).offset.x() == -120);

  panel->overlay_check()->setChecked(true);
  panel->overlay_opacity_spin()->setValue(65);
  CHECK(canvas->clone_overlay_shown() && canvas->clone_overlay_opacity() == 65);
  patchy::ui::MainWindowTestAccess::save_tool_settings(window);
  {
    auto settings = patchy::ui::app_settings();
    CHECK(settings.value(QStringLiteral("tools/cloneOverlay")).toBool());
    CHECK(settings.value(QStringLiteral("tools/cloneOverlayOpacity")).toInt() == 65);
  }
  save_window_popup_artifact("ui_clone_source_popup", window, *gear, *menu);

  panel->reset_button()->click();
  CHECK(canvas->clone_source_transform().is_identity());
  CHECK(panel->scale_spin()->value() == 100.0);
  CHECK(!panel->flip_horizontal_button()->isChecked());
  CHECK(!panel->reset_button()->isEnabled());

  // Slot 2 from the popup: an empty source, then back.
  panel->slot_button(1)->click();
  CHECK(canvas->clone_source_slot() == 1);
  CHECK(!panel->offset_x_spin()->isEnabled());
  panel->slot_button(0)->click();
  CHECK(canvas->clone_source_slot() == 0);
  CHECK(panel->offset_x_spin()->value() == -120);

  // On the Healing Brush the transform fields are off.
  require_action_by_text(window, QStringLiteral("Healing Brush"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == CanvasTool::Healing);
  CHECK(!panel->scale_spin()->isEnabled());
  CHECK(!panel->flip_horizontal_button()->isEnabled() && !panel->flip_vertical_button()->isEnabled());
  menu->close();
  QApplication::processEvents();
}

// The flip toggles (GitHub issue 424 follow-up) center on their "Flip:" label like the
// Scale and Rotation fields do on theirs, and the glyph fills the button: a 24 px square
// with a 20 px icon. The popup in both schemes and the Image > Rotate & Flip menu (which
// shares the glyphs) are the issue shots.
void ui_clone_source_flip_buttons_center_on_their_label() {
  for (const auto scheme : {patchy::ui::ColorSchemePreference::Dark, patchy::ui::ColorSchemePreference::Light}) {
    ColorSchemeRestorer restore_scheme(scheme);
    const std::string suffix = scheme == patchy::ui::ColorSchemePreference::Light ? "_light" : "_dark";
    auto&& [window, canvas] = ShownWindowFixture{};
    require_action_by_text(window, QStringLiteral("Clone"))->trigger();
    QApplication::processEvents();
    auto* gear = window.findChild<QToolButton*>(QStringLiteral("cloneSourceOptionsButton"));
    auto* menu = window.findChild<QMenu*>(QStringLiteral("cloneSourceMenu"));
    auto* panel = window.findChild<patchy::ui::CloneSourcePanel*>(QStringLiteral("cloneSourcePanel"));
    CHECK(gear != nullptr && menu != nullptr && panel != nullptr);
    if (gear == nullptr || menu == nullptr || panel == nullptr) {
      return;
    }
    canvas->set_brush_size(30);
    alt_click(*canvas, QPoint(60, 60));
    open_gear_popup(*gear, *menu);
    panel->flip_horizontal_button()->click();
    QApplication::processEvents();
    const auto label_named = [panel](const QString& text) -> QLabel* {
      for (auto* label : panel->findChildren<QLabel*>()) {
        if (label->text() == text) {
          return label;
        }
      }
      return nullptr;
    };
    const auto center_y = [panel](const QWidget* widget) {
      return widget->mapTo(panel, QPoint(0, 0)).y() + widget->height() / 2;
    };
    auto* flip_label = label_named(QStringLiteral("Flip:"));
    auto* scale_label = label_named(QStringLiteral("Scale:"));
    CHECK(flip_label != nullptr && scale_label != nullptr);
    if (flip_label == nullptr || scale_label == nullptr) {
      return;
    }
    for (auto* button : {panel->flip_horizontal_button(), panel->flip_vertical_button()}) {
      CHECK(std::abs(center_y(button) - center_y(flip_label)) <= 1);
      CHECK(std::abs(center_y(button) - center_y(panel->reset_button())) <= 1);
      CHECK(button->size() == QSize(24, 24));
      CHECK(button->iconSize() == QSize(20, 20));
    }
    CHECK(std::abs(center_y(panel->scale_spin()) - center_y(scale_label)) <= 1);
    CHECK(std::abs(center_y(panel->rotation_spin()) - center_y(scale_label)) <= 1);
    save_window_popup_artifact("ui_clone_source_flip_popup" + suffix, window, *gear, *menu);
    menu->close();
    QApplication::processEvents();

    // The Image > Rotate & Flip entries draw the same glyphs.
    auto* flip_action = require_hotkey_action(window, QStringLiteral("image.flip_horizontal"));
    QMenu* flip_menu = nullptr;
    for (auto* candidate : window.findChildren<QMenu*>()) {
      if (candidate->actions().contains(flip_action)) {
        flip_menu = candidate;
      }
    }
    CHECK(flip_menu != nullptr);
    if (flip_menu != nullptr) {
      flip_menu->popup(window.mapToGlobal(QPoint(40, 40)));
      QApplication::processEvents();
      save_widget_artifact("ui_clone_source_flip_menu_icons" + suffix, *flip_menu);
      flip_menu->hide();
      QApplication::processEvents();
    }
  }
}

// Screenshot for the issue: lives only in the clone source popup (GitHub issue 458; issue 426 had put a
// copy on the bar, which wrapped the Healing Brush row at 1366 px). Neither bar has the
// checkbox, the gear follows Aligned on the same row, and the popup's checkbox is on at 100%
// with no saved setting, drives every canvas and persists. The artifact is the issue shot:
// the Healing Brush bar at 1366 px.
void ui_clone_source_overlay_check_lives_in_the_popup() {
  SettingsValueRestorer saved_overlay(QStringLiteral("tools/cloneOverlay"));
  SettingsValueRestorer saved_opacity(QStringLiteral("tools/cloneOverlayOpacity"));
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("tools/cloneOverlay"));
    settings.remove(QStringLiteral("tools/cloneOverlayOpacity"));
    settings.sync();
  }
  auto&& [window, canvas] = ShownWindowFixture{};
  window.resize(1366, 738);
  auto* aligned = window.findChild<QCheckBox*>(QStringLiteral("cloneAlignedCheck"));
  auto* gear = window.findChild<QToolButton*>(QStringLiteral("cloneSourceOptionsButton"));
  auto* menu = window.findChild<QMenu*>(QStringLiteral("cloneSourceMenu"));
  auto* panel = window.findChild<patchy::ui::CloneSourcePanel*>(QStringLiteral("cloneSourcePanel"));
  auto* options = window.findChild<QWidget*>(QStringLiteral("OptionsContent"));
  auto* options_bar = window.findChild<QToolBar*>(QStringLiteral("Options"));
  CHECK(aligned != nullptr && gear != nullptr && menu != nullptr && panel != nullptr && options != nullptr &&
        options_bar != nullptr);
  if (aligned == nullptr || gear == nullptr || menu == nullptr || panel == nullptr || options == nullptr ||
      options_bar == nullptr) {
    return;
  }
  for (const auto* tool_name : {"Healing Brush", "Clone"}) {
    require_action_by_text(window, QString::fromLatin1(tool_name))->trigger();
    QApplication::processEvents();
    process_events_for(20);
    if (std::string_view(tool_name) == "Healing Brush") {
      save_widget_artifact("issue458_healing_bar_1366", *options_bar);
    }
    CHECK(window.findChild<QCheckBox*>(QStringLiteral("cloneOverlayCheck")) == nullptr);
    int visible_overlay_checks = 0;
    int lines_top = -1;
    bool one_line = true;
    for (auto* child : options->findChildren<QWidget*>(Qt::FindDirectChildrenOnly)) {
      if (!child->isVisible()) {
        continue;
      }
      if (const auto* check = qobject_cast<QCheckBox*>(child);
          check != nullptr && check->text().contains(QStringLiteral("Overlay"), Qt::CaseInsensitive)) {
        ++visible_overlay_checks;
      }
      if (lines_top < 0) {
        lines_top = child->geometry().top();
      } else if (std::abs(child->geometry().top() - lines_top) >= 8) {
        one_line = false;
      }
    }
    CHECK(visible_overlay_checks == 0);
    // The row fits one line at 1366 px, the gear right after Aligned.
    CHECK(one_line);
    CHECK(aligned->isVisible() && gear->isVisible());
    CHECK(gear->x() > aligned->x());
    CHECK(gear->geometry().center().y() == aligned->geometry().center().y());
    int shown_sliders = 0;
    for (auto* child : options->findChildren<QWidget*>(Qt::FindDirectChildrenOnly)) {
      if (child->isVisible() && child->property("optionsBarCollapsible").toBool()) {
        ++shown_sliders;
      }
    }
    std::printf("  %s bar at 1366 px: %d duplicate slider(s) shown\n", tool_name, shown_sliders);
  }

  // The popup's checkbox: on at 100% by default, drives the canvas, and persists.
  CHECK(canvas->clone_overlay_shown());
  CHECK(canvas->clone_overlay_opacity() == 100);
  open_gear_popup(*gear, *menu);
  CHECK(panel->overlay_check()->isChecked());
  CHECK(panel->overlay_opacity_spin()->value() == 100);
  panel->overlay_check()->setChecked(false);
  CHECK(!canvas->clone_overlay_shown());
  panel->overlay_opacity_spin()->setValue(70);
  CHECK(canvas->clone_overlay_opacity() == 70);
  menu->close();
  QApplication::processEvents();
  patchy::ui::MainWindowTestAccess::save_tool_settings(window);
  {
    auto settings = patchy::ui::app_settings();
    CHECK(!settings.value(QStringLiteral("tools/cloneOverlay")).toBool());
    CHECK(settings.value(QStringLiteral("tools/cloneOverlayOpacity")).toInt() == 70);
  }
  open_gear_popup(*gear, *menu);
  CHECK(!panel->overlay_check()->isChecked());
  panel->overlay_check()->setChecked(true);
  CHECK(canvas->clone_overlay_shown());
  CHECK(canvas->clone_overlay_opacity() == 70);
  menu->close();
  QApplication::processEvents();
  patchy::ui::MainWindowTestAccess::save_tool_settings(window);
  {
    auto settings = patchy::ui::app_settings();
    CHECK(settings.value(QStringLiteral("tools/cloneOverlay")).toBool());
  }
}

// Screenshot for the issue: the popup open over a scene, and the overlay in use with a
// turned, enlarged source.
void ui_clone_source_overlay_screenshot() {
  SettingsValueRestorer saved_aligned(QStringLiteral("tools/cloneAligned"));
  SettingsValueRestorer saved_overlay(QStringLiteral("tools/cloneOverlay"));
  SettingsValueRestorer saved_opacity(QStringLiteral("tools/cloneOverlayOpacity"));
  ensure_artifact_dir();
  QImage scene(480, 320, QImage::Format_RGBA8888);
  {
    QPainter painter(&scene);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QLinearGradient sky(0, 0, 0, 320);
    sky.setColorAt(0.0, QColor(96, 150, 210));
    sky.setColorAt(0.65, QColor(200, 220, 236));
    sky.setColorAt(0.66, QColor(96, 140, 70));
    sky.setColorAt(1.0, QColor(60, 100, 50));
    painter.fillRect(scene.rect(), sky);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(250, 214, 90));
    painter.drawEllipse(QPointF(390, 70), 34, 34);
    for (int i = 0; i < 3; ++i) {
      painter.setBrush(QColor(170 + i * 20, 70 + i * 30, 120 - i * 20));
      painter.drawEllipse(QPointF(70 + i * 46, 236 - i * 8), 18, 18);
      painter.setBrush(QColor(70, 120, 50));
      painter.drawRect(QRectF(67 + i * 46, 252 - i * 8, 6, 40));
    }
  }
  const auto input = QString::fromStdString((std::filesystem::path("test-artifacts") / "clone_source_scene.png").string());
  CHECK(scene.save(input));
  auto&& [window, canvas] = ShownWindowFixture{};
  patchy::ui::MainWindowTestAccess::open_document_path(window, input);
  QApplication::processEvents();
  canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Clone"))->trigger();
  QApplication::processEvents();
  auto* aligned = window.findChild<QCheckBox*>(QStringLiteral("cloneAlignedCheck"));
  auto* panel = window.findChild<patchy::ui::CloneSourcePanel*>(QStringLiteral("cloneSourcePanel"));
  auto* gear = window.findChild<QToolButton*>(QStringLiteral("cloneSourceOptionsButton"));
  auto* menu = window.findChild<QMenu*>(QStringLiteral("cloneSourceMenu"));
  CHECK(aligned != nullptr && panel != nullptr && gear != nullptr && menu != nullptr);
  if (aligned == nullptr || panel == nullptr || gear == nullptr || menu == nullptr) {
    return;
  }
  aligned->setChecked(true);
  canvas->set_brush_size(70);
  canvas->set_brush_softness(0);
  alt_click(*canvas, QPoint(116, 228));
  open_gear_popup(*gear, *menu);
  panel->scale_spin()->setValue(130.0);
  panel->rotation_spin()->setValue(-15.0);
  panel->overlay_check()->setChecked(true);
  panel->overlay_opacity_spin()->setValue(80);
  menu->close();
  QApplication::processEvents();
  // One stroke with the turned, enlarged source, then the popup with its offset.
  click(*canvas, QPoint(250, 232));
  open_gear_popup(*gear, *menu);
  CHECK(panel->offset_x_spin()->isEnabled());
  save_window_popup_artifact("ui_clone_source_popup_scene", window, *gear, *menu);
  menu->close();
  QApplication::processEvents();
  // Aligned off: the overlay shows the source point under the pointer.
  aligned->setChecked(false);
  const auto hover = canvas->widget_position_for_document_point(QPoint(380, 232));
  send_mouse(*canvas, QEvent::MouseMove, hover, Qt::NoButton, Qt::NoButton);
  QApplication::processEvents();
  const auto shot = canvas->grab().toImage();
  const auto crop = QRect(canvas->widget_position_for_document_point(QPoint(0, 150)),
                          canvas->widget_position_for_document_point(QPoint(480, 320)))
                        .intersected(shot.rect());
  save_image_artifact("ui_clone_source_overlay_in_use", shot.copy(crop));
}

// The resize tests (GitHub issue 516): a red source half and a blue target half, the
// source picked well inside the red so every brush size shows plain red under the brush.
struct CloneResizeScene {
  patchy::Document document{160, 96, patchy::PixelFormat::rgba8()};
  CanvasWidget canvas;
  QPoint hover;
  double scale{1.0};  // widget pixels per document pixel

  explicit CloneResizeScene(int brush_size) {
    auto pixels = solid_pixels(160, 96, patchy::PixelFormat::rgba8(), QColor(30, 40, 210, 255));
    fill_pixel_rect(pixels, QRect(0, 0, 80, 96), QColor(220, 40, 30, 255));
    document.add_pixel_layer("Halves", std::move(pixels));
    prepare_clone_canvas(canvas, document, brush_size, 0, 100);
    canvas.set_clone_aligned(false);
    canvas.set_clone_overlay_shown(true);
    canvas.set_clone_overlay_opacity(100);
    alt_click(canvas, QPoint(30, 48));
    hover = canvas.widget_position_for_document_point(QPoint(120, 48));
    scale = static_cast<double>(canvas.widget_position_for_document_point(QPoint(100, 0)).x() -
                                canvas.widget_position_for_document_point(QPoint(0, 0)).x()) /
            100.0;
    send_mouse(canvas, QEvent::MouseMove, hover, Qt::NoButton, Qt::NoButton);
    QApplication::processEvents();
  }

  // The widget pixels the overlay changes: the canvas with the overlay against the
  // canvas without it, everything else (the resize disc, its readout) being equal.
  [[nodiscard]] QRect overlay_bounds() {
    const auto on = canvas.grab().toImage();
    canvas.set_clone_overlay_shown(false);
    const auto off = canvas.grab().toImage();
    canvas.set_clone_overlay_shown(true);
    QApplication::processEvents();
    QRect bounds;
    for (int y = 0; y < on.height(); ++y) {
      for (int x = 0; x < on.width(); ++x) {
        if (!color_close(on.pixelColor(x, y), off.pixelColor(x, y), 8)) {
          bounds = bounds.united(QRect(x, y, 1, 1));
        }
      }
    }
    return bounds;
  }

  // The overlay sits centred on `center` at `brush_size` across.
  void check_overlay(QPoint center, int brush_size) {
    const auto bounds = overlay_bounds();
    const auto diameter = static_cast<double>(brush_size) * scale;
    CHECK(!bounds.isEmpty());
    CHECK(std::abs(bounds.center().x() - center.x()) <= 2);
    CHECK(std::abs(bounds.center().y() - center.y()) <= 2);
    CHECK(std::abs(bounds.width() - diameter) <= 3.0);
    CHECK(std::abs(bounds.height() - diameter) <= 3.0);
  }
};

// What the screen shows after an update: the last frame with the repainted region
// replaced by a fresh paint. Pixels outside the region keep their old values.
QImage repaint_onto(const QImage& last_frame, const QImage& fresh, const QRegion& repainted) {
  QImage shown = last_frame.copy();
  QPainter painter(&shown);
  painter.setClipRegion(repainted);
  painter.drawImage(0, 0, fresh);
  painter.end();
  return shown;
}

// Changes the brush size the way [ and ] do (the size spin calls set_brush_size) with the
// pointer still: the next frame on screen matches a full repaint, so no rim of the old
// overlay is left, and the overlay is the new size on the brush.
QImage clone_overlay_after_size_step(CloneResizeScene& scene, int size, QImage* stale_view) {
  const auto last_frame = scene.canvas.grab().toImage();
  PaintRegionRecorder recorder(&scene.canvas);
  scene.canvas.installEventFilter(&recorder);
  recorder.reset();
  scene.canvas.set_brush_size(size);
  QApplication::processEvents();
  scene.canvas.removeEventFilter(&recorder);
  const auto fresh = scene.canvas.grab().toImage();
  const auto shown = repaint_onto(last_frame, fresh, recorder.region());
  if (stale_view != nullptr) {
    *stale_view = shown;
  }
  return fresh;
}

void ui_clone_source_overlay_follows_bracket_resize() {
  CloneResizeScene scene(40);
  scene.check_overlay(scene.hover, 40);
  for (const int size : {12, 40}) {
    QImage shown;
    const auto fresh = clone_overlay_after_size_step(scene, size, &shown);
    CHECK(shown == fresh);
    scene.check_overlay(scene.hover, size);
  }
}

// The Alt+right-drag resize anchors its disc at the press point while the mouse moves on:
// the overlay stays centred on the disc at the dragged size, during the drag and after.
void ui_clone_source_overlay_follows_drag_resize() {
  CloneResizeScene scene(20);
  scene.check_overlay(scene.hover, 20);
  const QPoint moved = scene.hover + QPoint(15, 0);
  send_mouse(scene.canvas, QEvent::MouseButtonPress, scene.hover, Qt::RightButton, Qt::RightButton, Qt::AltModifier);
  send_mouse(scene.canvas, QEvent::MouseMove, moved, Qt::NoButton, Qt::RightButton, Qt::AltModifier);
  QApplication::processEvents();
  const auto dragged = scene.canvas.brush_size();
  CHECK(dragged > 20);
  scene.check_overlay(scene.hover, dragged);
  send_mouse(scene.canvas, QEvent::MouseButtonRelease, moved, Qt::RightButton, Qt::NoButton, Qt::AltModifier);
  QApplication::processEvents();
  CHECK(scene.canvas.brush_size() == dragged);
  scene.check_overlay(scene.hover, dragged);
}

// With no source a Clone press is refused and the pointer is the not-allowed cursor, so
// the brush footprint is painted on the canvas at every size (GitHub issue 516: below the
// large-brush size [ left no circle and no centre cross at all).
struct NoSourceCloneScene {
  patchy::Document document{120, 80, patchy::PixelFormat::rgba8()};
  CanvasWidget canvas;
  QPoint hover;
  double scale{1.0};

  explicit NoSourceCloneScene(int brush_size) {
    document.add_pixel_layer("Gray", solid_pixels(120, 80, patchy::PixelFormat::rgba8(), QColor(128, 128, 128, 255)));
    prepare_clone_canvas(canvas, document, brush_size, 0, 100);
    hover = canvas.widget_position_for_document_point(QPoint(60, 40));
    scale = static_cast<double>(canvas.widget_position_for_document_point(QPoint(100, 0)).x() -
                                canvas.widget_position_for_document_point(QPoint(0, 0)).x()) /
            100.0;
    send_mouse(canvas, QEvent::MouseMove, hover, Qt::NoButton, Qt::NoButton);
    QApplication::processEvents();
  }

  // The painted outline crosses the row through the pointer at the brush radius, both sides.
  [[nodiscard]] bool ring_shown(const QImage& image, int brush_size) const {
    const auto radius = static_cast<int>(std::lround(static_cast<double>(brush_size) * scale / 2.0));
    for (const int side : {-1, 1}) {
      bool found = false;
      for (int distance = radius - 3; distance <= radius + 2; ++distance) {
        if (!color_close(image.pixelColor(hover.x() + side * distance, hover.y()), QColor(128, 128, 128), 40)) {
          found = true;
        }
      }
      if (!found) {
        return false;
      }
    }
    return true;
  }
};

void ui_clone_source_no_source_bracket_resize_keeps_brush_outline() {
  NoSourceCloneScene scene(48);
  CHECK(scene.scale >= 1.0);
  CHECK(scene.canvas.cursor().shape() == Qt::ForbiddenCursor);
  CHECK(scene.ring_shown(scene.canvas.grab().toImage(), 48));
  for (const int size : {24, 48, 24}) {
    const auto last_frame = scene.canvas.grab().toImage();
    PaintRegionRecorder recorder(&scene.canvas);
    scene.canvas.installEventFilter(&recorder);
    recorder.reset();
    scene.canvas.set_brush_size(size);
    QApplication::processEvents();
    scene.canvas.removeEventFilter(&recorder);
    const auto fresh = scene.canvas.grab().toImage();
    CHECK(repaint_onto(last_frame, fresh, recorder.region()) == fresh);
    CHECK(scene.ring_shown(fresh, size));
    CHECK(scene.canvas.cursor().shape() == Qt::ForbiddenCursor);
  }
}

// Alt-click on the pasteboard turns the source off (GitHub issue 516): the marker and the
// overlay go, a press asks for a source again, and the Healing Brush does the same.
void ui_clone_source_alt_click_outside_canvas_clears_source() {
  patchy::Document document(96, 64, patchy::PixelFormat::rgba8());
  auto pixels = solid_pixels(96, 64, patchy::PixelFormat::rgba8(), QColor(30, 40, 210, 255));
  fill_pixel_rect(pixels, QRect(0, 0, 48, 64), QColor(220, 40, 30, 255));
  auto& layer = document.add_pixel_layer("Halves", std::move(pixels));
  CanvasWidget canvas;
  prepare_clone_canvas(canvas, document, 20, 0, 100);
  canvas.set_clone_overlay_shown(true);
  canvas.set_clone_overlay_opacity(100);
  QStringList statuses;
  canvas.set_status_callback([&statuses](QString message) { statuses << message; });
  const auto outside = canvas.widget_position_for_document_point(QPoint(-12, 32));
  CHECK(canvas.rect().contains(outside));
  const auto alt_click_outside = [&canvas, outside] {
    send_mouse(canvas, QEvent::MouseButtonPress, outside, Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
    send_mouse(canvas, QEvent::MouseButtonRelease, outside, Qt::LeftButton, Qt::NoButton, Qt::AltModifier);
    QApplication::processEvents();
  };
  const auto source_set = [&canvas] { return canvas.clone_source_slot_state(canvas.clone_source_slot()).set; };

  alt_click(canvas, QPoint(20, 32));
  CHECK(source_set());
  alt_click_outside();
  CHECK(!source_set());
  CHECK(!statuses.isEmpty() && statuses.last() == QStringLiteral("Clone source cleared"));
  // No overlay under the hovered brush: the target shows.
  const auto hover = canvas.widget_position_for_document_point(QPoint(72, 32));
  send_mouse(canvas, QEvent::MouseMove, hover, Qt::NoButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(color_close(canvas.grab().toImage().pixelColor(hover + QPoint(2, 2)), QColor(30, 40, 210), 12));
  // A press asks for a source and paints nothing.
  click(canvas, QPoint(72, 32));
  CHECK((pixel_at(layer, QPoint(72, 32)) == std::array<std::uint8_t, 4>{30, 40, 210, 255}));
  CHECK(!statuses.isEmpty() && statuses.last().contains(QStringLiteral("to set a clone source")));

  canvas.set_tool(CanvasTool::Healing);
  alt_click(canvas, QPoint(20, 32));
  CHECK(source_set());
  alt_click_outside();
  CHECK(!source_set());
  CHECK(!statuses.isEmpty() && statuses.last() == QStringLiteral("Healing source cleared"));
}

// Captures for the issue: the canvas as shown after ] then [ (stale rims show when the
// old overlay is not repainted), and during an Alt+right-drag resize.
void ui_clone_source_overlay_resize_screenshot() {
  ensure_artifact_dir();
  {
    CloneResizeScene scene(24);
    QImage shown;
    clone_overlay_after_size_step(scene, 48, &shown);
    const auto grown = shown;
    clone_overlay_after_size_step(scene, 16, &shown);
    // Left: the frame after ] (24 to 48 px); right: after [ (48 to 16 px).
    QImage pair(grown.width() * 2 + 8, grown.height(), QImage::Format_ARGB32);
    pair.fill(QColor(40, 40, 40));
    QPainter painter(&pair);
    painter.drawImage(0, 0, grown);
    painter.drawImage(grown.width() + 8, 0, shown);
    painter.end();
    save_image_artifact("ui_clone_overlay_bracket_resize", pair);
  }
  {
    CloneResizeScene scene(20);
    send_mouse(scene.canvas, QEvent::MouseButtonPress, scene.hover, Qt::RightButton, Qt::RightButton,
               Qt::AltModifier);
    send_mouse(scene.canvas, QEvent::MouseMove, scene.hover + QPoint(20, 0), Qt::NoButton, Qt::RightButton,
               Qt::AltModifier);
    QApplication::processEvents();
    save_image_artifact("ui_clone_overlay_drag_resize", scene.canvas.grab().toImage());
    send_mouse(scene.canvas, QEvent::MouseButtonRelease, scene.hover + QPoint(20, 0), Qt::RightButton,
               Qt::NoButton, Qt::AltModifier);
  }
  {
    // No source yet, after [ from 48 to 24 px: the brush circle and its centre cross.
    NoSourceCloneScene scene(48);
    scene.canvas.set_brush_size(24);
    QApplication::processEvents();
    save_image_artifact("ui_clone_no_source_outline_after_bracket", scene.canvas.grab().toImage());
  }
}

}  // namespace

std::vector<patchy::test::TestCase> clone_source_tests() {
  return {
      {"ui_clone_source_identity_transform_keeps_stroke_bytes", ui_clone_source_identity_transform_keeps_stroke_bytes},
      {"ui_clone_source_scale_and_rotation_transform_the_sample",
       ui_clone_source_scale_and_rotation_transform_the_sample},
      {"ui_clone_source_flip_paints_mirrored_pixels", ui_clone_source_flip_paints_mirrored_pixels},
      {"ui_clone_source_offset_edit_and_slots_switch_sources", ui_clone_source_offset_edit_and_slots_switch_sources},
      {"ui_clone_source_healing_brush_ignores_transform", ui_clone_source_healing_brush_ignores_transform},
      {"ui_clone_source_overlay_previews_source_under_brush", ui_clone_source_overlay_previews_source_under_brush},
      {"ui_clone_source_overlay_shows_flipped_source", ui_clone_source_overlay_shows_flipped_source},
      {"ui_clone_source_gear_popup_drives_canvas", ui_clone_source_gear_popup_drives_canvas},
      {"ui_clone_source_overlay_check_lives_in_the_popup", ui_clone_source_overlay_check_lives_in_the_popup},
      {"ui_clone_source_overlay_screenshot", ui_clone_source_overlay_screenshot},
      {"ui_clone_source_overlay_follows_bracket_resize", ui_clone_source_overlay_follows_bracket_resize},
      {"ui_clone_source_overlay_follows_drag_resize", ui_clone_source_overlay_follows_drag_resize},
      {"ui_clone_source_overlay_resize_screenshot", ui_clone_source_overlay_resize_screenshot},
      {"ui_clone_source_no_source_bracket_resize_keeps_brush_outline",
       ui_clone_source_no_source_bracket_resize_keeps_brush_outline},
      {"ui_clone_source_alt_click_outside_canvas_clears_source", ui_clone_source_alt_click_outside_canvas_clears_source},
      {"ui_clone_source_flip_buttons_center_on_their_label", ui_clone_source_flip_buttons_center_on_their_label},
  };
}
