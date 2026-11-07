// The toning and detail brushes (Dodge, Burn, Sponge, Blur, Sharpen, Smudge): speed paths
// that must keep their bytes (GitHub issue 660). Part 3 of the brush-engine stroke group
// (brush_engine_stroke_tests.cpp registers it last, so the earlier order is unchanged).

#include "core/document.hpp"
#include "core/document_depth.hpp"
#include "core/pixel_depth.hpp"
#include "test_harness.hpp"
#include "ui/canvas_widget.hpp"
#include "ui_test_support.hpp"

#include <QApplication>
#include <QMouseEvent>
#include <QImage>
#include <QKeyEvent>
#include <QScopeGuard>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <utility>
#include <vector>

namespace {

using namespace patchy::test::ui;

struct ToningCase {
  const char* name;
  patchy::ui::CanvasTool tool;
  int size;
  int softness;
  int strength;
  int roundness;
  bool feathered_selection;
  bool deep;
};

// One stroke of `test_case` over a 700x520 test pattern at 50% zoom, every fourth move
// folding three samples into it and every seventh repeating its point; the layer bytes.
std::vector<std::uint8_t> paint_toning_stroke(const ToningCase& test_case, bool skip, bool single_threaded) {
  if (single_threaded) {
    qputenv("PATCHY_RENDER_SINGLE_THREADED", "1");
  }
  const auto restore_threads = qScopeGuard([single_threaded] {
    if (single_threaded) {
      qunsetenv("PATCHY_RENDER_SINGLE_THREADED");
    }
  });
  constexpr int kWidth = 700;
  constexpr int kHeight = 520;
  auto pixels = solid_pixels(kWidth, kHeight, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 255));
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = static_cast<std::uint8_t>((x * 7 + y * 3) % 256);
      px[1] = static_cast<std::uint8_t>((y * 5) % 256);
      px[2] = static_cast<std::uint8_t>(((x ^ y) * 11) % 256);
      px[3] = static_cast<std::uint8_t>(x < 60 ? 128 : 255);
    }
  }
  patchy::Document document(kWidth, kHeight, patchy::PixelFormat::rgba8());
  const auto layer_id = document.add_pixel_layer("Photo", std::move(pixels)).id();
  if (test_case.deep) {
    patchy::convert_document_depth(document, patchy::BitDepth::UInt16);
  }
  patchy::ui::CanvasWidget canvas;
  canvas.resize(520, 420);
  canvas.set_document(&document);
  canvas.set_zoom(0.5);
  canvas.set_clone_stamp_skip_enabled_for_testing(skip);
  canvas.show();
  QApplication::processEvents();
  const auto widget_point = [&canvas](QPoint document_point) {
    return canvas.widget_position_for_document_point(document_point);
  };
  if (test_case.feathered_selection) {
    canvas.set_tool(patchy::ui::CanvasTool::EllipticalMarquee);
    canvas.set_selection_feather_radius(25);
    drag(canvas, widget_point(QPoint(200, 90)), widget_point(QPoint(640, 470)));
    QApplication::processEvents();
  }
  canvas.set_tool(test_case.tool);
  canvas.set_brush_size(test_case.size);
  canvas.set_brush_softness(test_case.softness);
  canvas.set_brush_opacity(test_case.strength);
  canvas.set_local_adjustment_strength(test_case.strength);
  canvas.set_brush_base_shape(30.0, test_case.roundness);
  std::vector<QPoint> path;
  for (int index = 0; index <= 90; ++index) {
    const auto t = static_cast<double>(index);
    path.emplace_back(static_cast<int>(std::lround(400.0 + 160.0 * std::sin(t * 0.05))),
                      static_cast<int>(std::lround(260.0 + 120.0 * std::sin(t * 0.11) + (index % 5) * 2)));
    if (index % 7 == 3) {
      path.push_back(path.back());
    }
  }
  std::vector<QPointF> folded;
  canvas.set_coalesced_mouse_positions_provider(
      [&folded](const QMouseEvent&) -> std::vector<QPointF> { return folded; });
  const auto layer_bytes = [&document, layer_id] {
    const auto bytes = std::as_const(*document.find_layer(layer_id)).pixels().data();
    return std::vector<std::uint8_t>(bytes.begin(), bytes.end());
  };
  const auto before = layer_bytes();
  send_mouse(canvas, QEvent::MouseButtonPress, widget_point(path.front()), Qt::LeftButton, Qt::LeftButton);
  for (std::size_t index = 1; index < path.size();) {
    const auto extra = index % 4 == 0 ? std::min<std::size_t>(3, path.size() - 1 - index) : 0;
    folded.clear();
    for (std::size_t sample = 0; sample < extra; ++sample) {
      folded.emplace_back(widget_point(path[index + sample]));
    }
    const auto event_point = widget_point(path[index + extra]);
    QMouseEvent event(QEvent::MouseMove, event_point, canvas.mapToGlobal(event_point), Qt::NoButton, Qt::LeftButton,
                      Qt::NoModifier);
    QApplication::sendEvent(&canvas, &event);
    index += extra + 1;
  }
  folded.clear();
  send_mouse(canvas, QEvent::MouseButtonRelease, widget_point(path.back()), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  const auto after = layer_bytes();
  CHECK(after != before);
  return after;
}

// GitHub issue 660: Dodge, Burn, Sponge, Blur and Sharpen segments walk only their capsule
// (and of a segment continuing the last one only what can change) and fan big segments out
// over bands; a big Smudge dab fans its rows out. Every pixel reads only frozen inputs and
// writes only itself, so the bytes equal the whole-rectangle walk on one thread.
void ui_toning_brush_span_and_strips_keep_bytes() {
  const std::vector<ToningCase> cases{
      {"dodge_hard", patchy::ui::CanvasTool::Dodge, 301, 0, 100, 100, false, false},
      {"burn_soft_60", patchy::ui::CanvasTool::Burn, 301, 100, 60, 100, false, false},
      {"sponge_oval", patchy::ui::CanvasTool::Sponge, 251, 40, 100, 55, false, false},
      {"blur_feathered", patchy::ui::CanvasTool::BlurBrush, 201, 30, 80, 100, true, false},
      {"sharpen_deep_16", patchy::ui::CanvasTool::SharpenBrush, 181, 70, 75, 100, false, true},
      {"dodge_deep_16_feathered", patchy::ui::CanvasTool::Dodge, 221, 50, 50, 100, true, true},
      {"smudge", patchy::ui::CanvasTool::Smudge, 241, 50, 70, 100, false, false},
      {"smudge_deep_16_feathered", patchy::ui::CanvasTool::Smudge, 201, 30, 60, 100, true, true},
  };
  patchy::set_deep_editing_override(true);
  for (const auto& test_case : cases) {
    // The full walk on one thread is the stroke as it was before the issue.
    const auto full = paint_toning_stroke(test_case, false, true);
    const auto skipped = paint_toning_stroke(test_case, true, true);
    const auto parallel = paint_toning_stroke(test_case, true, false);
    if (skipped != full || parallel != full) {
      std::printf("[INFO] toning case %s: skip %s, strips %s\n", test_case.name, skipped == full ? "same" : "changed",
                  parallel == full ? "same" : "changed");
    }
    CHECK(skipped == full);
    CHECK(parallel == full);
  }
  patchy::set_deep_editing_override(std::nullopt);
}

// GitHub issue 660: queued moves of a Smudge or toning stroke coalesce per event-loop pass
// with every position kept, so the bytes equal the same moves sent one by one.
void ui_toning_stroke_coalesces_queued_moves_and_keeps_bytes() {
  for (const auto tool : {patchy::ui::CanvasTool::Dodge, patchy::ui::CanvasTool::BlurBrush,
                          patchy::ui::CanvasTool::Smudge}) {
    struct Result {
      std::vector<std::uint8_t> bytes;
      int coalesced{0};
    };
    const auto paint = [tool](bool coalesce) {
      constexpr int kWidth = 420;
      constexpr int kHeight = 300;
      auto pixels = solid_pixels(kWidth, kHeight, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 255));
      for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
          auto* px = pixels.pixel(x, y);
          px[0] = static_cast<std::uint8_t>((x * 7 + y * 3) % 256);
          px[1] = static_cast<std::uint8_t>((y * 5) % 256);
          px[2] = static_cast<std::uint8_t>(((x ^ y) * 11) % 256);
        }
      }
      patchy::Document document(kWidth, kHeight, patchy::PixelFormat::rgba8());
      const auto layer_id = document.add_pixel_layer("Photo", std::move(pixels)).id();
      patchy::ui::CanvasWidget canvas;
      canvas.resize(520, 400);
      canvas.set_document(&document);
      canvas.set_zoom(1.0);
      canvas.set_drag_move_coalescing_for_testing(coalesce ? patchy::ui::CanvasWidget::DragMoveCoalescing::AllMoves
                                                           : patchy::ui::CanvasWidget::DragMoveCoalescing::Off);
      canvas.show();
      QApplication::processEvents();
      canvas.set_tool(tool);
      canvas.set_brush_size(61);
      canvas.set_brush_softness(60);
      canvas.set_brush_opacity(70);
      canvas.set_local_adjustment_strength(70);
      const auto widget_point = [&canvas](QPoint document_point) {
        return canvas.widget_position_for_document_point(document_point);
      };
      // Every third move carries two samples the platform folded into it.
      std::map<std::pair<int, int>, std::vector<QPointF>> folded_by_event;
      canvas.set_coalesced_mouse_positions_provider([&folded_by_event](const QMouseEvent& event) {
        const auto found = folded_by_event.find({event.position().toPoint().x(), event.position().toPoint().y()});
        return found == folded_by_event.end() ? std::vector<QPointF>{} : found->second;
      });
      Result result;
      const auto coalesced_before = canvas.render_cache_diagnostics().drag_moves_coalesced;
      for (const int stroke : {0, 1}) {
        std::vector<QPoint> path;
        for (int index = 0; index <= 60; ++index) {
          const auto t = static_cast<double>(index);
          path.emplace_back(static_cast<int>(std::lround(120.0 + 3.0 * t)),
                            static_cast<int>(std::lround(90.0 + stroke * 90.0 + 30.0 * std::sin(t * 0.2))));
        }
        folded_by_event.clear();
        std::vector<QPoint> event_points;
        for (std::size_t index = 1; index < path.size();) {
          if (index % 3 == 0 && index + 2 < path.size()) {
            const auto event_point = widget_point(path[index + 2]);
            folded_by_event[{event_point.x(), event_point.y()}] = {QPointF(widget_point(path[index])),
                                                                   QPointF(widget_point(path[index + 1]))};
            event_points.push_back(event_point);
            index += 3;
          } else {
            event_points.push_back(widget_point(path[index]));
            index += 1;
          }
        }
        send_mouse(canvas, QEvent::MouseButtonPress, widget_point(path.front()), Qt::LeftButton, Qt::LeftButton);
        if (coalesce) {
          // Queued as a window system queues them, in bursts of seven per event-loop pass.
          constexpr std::size_t kBurst = 7;
          const auto queued = event_points.size() - event_points.size() % kBurst - kBurst;
          for (std::size_t index = 0; index < queued; ++index) {
            const auto position = QPointF(event_points[index]);
            QApplication::postEvent(&canvas, new QMouseEvent(QEvent::MouseMove, position, canvas.mapToGlobal(position),
                                                             Qt::NoButton, Qt::LeftButton, Qt::NoModifier));
            if (index % kBurst == kBurst - 1) {
              QApplication::processEvents();
            }
          }
          // The last moves arrive without the event loop running: the first applies, the
          // rest are still recorded when the release lands, which applies them first.
          for (std::size_t index = queued; index < event_points.size(); ++index) {
            send_mouse(canvas, QEvent::MouseMove, event_points[index], Qt::NoButton, Qt::LeftButton);
          }
        } else {
          for (const auto& position : event_points) {
            send_mouse(canvas, QEvent::MouseMove, position, Qt::NoButton, Qt::LeftButton);
          }
        }
        send_mouse(canvas, QEvent::MouseButtonRelease, widget_point(path.back()), Qt::LeftButton, Qt::NoButton);
        QApplication::processEvents();
      }
      result.coalesced = canvas.render_cache_diagnostics().drag_moves_coalesced - coalesced_before;
      const auto bytes = std::as_const(*document.find_layer(layer_id)).pixels().data();
      result.bytes.assign(bytes.begin(), bytes.end());
      return result;
    };
    const auto one_by_one = paint(false);
    const auto coalesced = paint(true);
    CHECK(one_by_one.coalesced == 0);
    CHECK(coalesced.coalesced > 0);
    CHECK(coalesced.bytes == one_by_one.bytes);
  }
}

// GitHub issue 661: Dodge and Burn Ranges are narrow, smooth curves calibrated against
// Photoshop (ui/dodge_burn_curves.hpp). A gray ramp (0..255, 4 px a level) gets one hard
// stroke per band at the default Strength 50% with Protect Tones on: Burn on Shadows leaves
// light tones alone, Burn on Highlights never turns white black, Dodge on Shadows leaves
// highlights alone, and the Midtones bell peaks near middle gray. Writes the bands as
// test-artifacts/ui_dodge_burn_ranges.png (rows: original, Burn Shadows/Midtones/Highlights,
// Dodge Shadows/Midtones/Highlights).
void ui_dodge_burn_ranges_follow_calibrated_curves() {
  constexpr int kWidth = 1024;
  constexpr int kBand = 40;
  constexpr int kBands = 7;
  auto pixels = solid_pixels(kWidth, kBand * kBands, patchy::PixelFormat::rgb8(), QColor(0, 0, 0));
  for (int y = 0; y < kBand * kBands; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = px[1] = px[2] = static_cast<std::uint8_t>(x / 4);
    }
  }
  patchy::Document document(kWidth, kBand * kBands, patchy::PixelFormat::rgb8());
  const auto layer_id = document.add_pixel_layer("Ramp", std::move(pixels)).id();
  patchy::ui::CanvasWidget canvas;
  canvas.resize(1100, 380);
  canvas.set_document(&document);
  canvas.set_zoom(1.0);
  canvas.show();
  QApplication::processEvents();
  canvas.set_brush_size(30);
  canvas.set_brush_softness(0);
  canvas.set_local_adjustment_strength(50);
  canvas.set_local_protect_tones(true);
  using Range = patchy::ui::CanvasWidget::LocalToneRange;
  struct Band {
    patchy::ui::CanvasTool tool;
    Range range;
  };
  const std::array<Band, 6> bands{Band{patchy::ui::CanvasTool::Burn, Range::Shadows},
                                  Band{patchy::ui::CanvasTool::Burn, Range::Midtones},
                                  Band{patchy::ui::CanvasTool::Burn, Range::Highlights},
                                  Band{patchy::ui::CanvasTool::Dodge, Range::Shadows},
                                  Band{patchy::ui::CanvasTool::Dodge, Range::Midtones},
                                  Band{patchy::ui::CanvasTool::Dodge, Range::Highlights}};
  for (std::size_t index = 0; index < bands.size(); ++index) {
    canvas.set_tool(bands[index].tool);
    canvas.set_local_tone_range(bands[index].range);
    const auto y = static_cast<int>(index + 1) * kBand + kBand / 2;
    std::vector<QPoint> path;
    for (int x = -20; x <= kWidth + 20; x += 8) {
      path.emplace_back(x, y);
    }
    drag_document_path(canvas, path, 1);
    QApplication::processEvents();
  }
  const auto& result = std::as_const(*document.find_layer(layer_id)).pixels();
  const auto level = [&result](int band, int value) {
    return static_cast<int>(result.pixel(value * 4 + 1, band * kBand + kBand / 2)[0]);
  };
  // The bands first, so a failing check still leaves the picture.
  ensure_artifact_dir();
  QImage image(kWidth, kBand * kBands, QImage::Format_RGB32);
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < kWidth; ++x) {
      const auto* px = result.pixel(x, y);
      image.setPixel(x, y, qRgb(px[0], px[1], px[2]));
    }
  }
  const auto saved = image.save(QStringLiteral("test-artifacts/ui_dodge_burn_ranges.png"));
  // Burn, Shadows: dark tones darken, light tones stay.
  CHECK(level(1, 64) <= 64 - 25);
  CHECK(level(1, 128) >= 128 - 25);
  CHECK(level(1, 208) >= 207);
  // Burn, Midtones: a bell around middle gray; black and white stay.
  CHECK(level(2, 128) <= 128 - 25);
  CHECK(level(2, 0) == 0);
  CHECK(level(2, 255) == 255);
  // Burn, Highlights: white darkens a little, never to black; shadows stay.
  CHECK(level(3, 255) >= 195 && level(3, 255) <= 215);
  CHECK(level(3, 48) == 48);
  // Dodge, Shadows: shadows lift, highlights stay.
  CHECK(level(4, 72) >= 72 + 12);
  CHECK(level(4, 208) == 208);
  CHECK(level(4, 0) == 0);
  // Dodge, Midtones and Highlights.
  CHECK(level(5, 136) >= 136 + 25);
  CHECK(level(5, 16) <= 18);
  CHECK(level(6, 160) >= 160 + 55);
  CHECK(level(6, 48) <= 49);
  // Untouched first band.
  CHECK(level(0, 100) == 100);
  CHECK(saved);
}

// GitHub issue 662: a stroke that starts with Alt held paints with the opposite tool (Dodge
// and Burn, Blur and Sharpen swap; Sponge stays itself), byte for byte the stroke the other
// tool paints, and the status bar names the operation while Alt is held.
void ui_toning_alt_stroke_paints_opposite_operation() {
  const auto paint = [](patchy::ui::CanvasTool tool, Qt::KeyboardModifiers modifiers) {
    constexpr int kWidth = 160;
    constexpr int kHeight = 100;
    auto pixels = solid_pixels(kWidth, kHeight, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 255));
    for (int y = 0; y < kHeight; ++y) {
      for (int x = 0; x < kWidth; ++x) {
        auto* px = pixels.pixel(x, y);
        px[0] = static_cast<std::uint8_t>((x * 7 + y * 3) % 256);
        px[1] = static_cast<std::uint8_t>((y * 5 + 40) % 256);
        px[2] = static_cast<std::uint8_t>(((x ^ y) * 11) % 256);
      }
    }
    patchy::Document document(kWidth, kHeight, patchy::PixelFormat::rgba8());
    const auto layer_id = document.add_pixel_layer("Photo", std::move(pixels)).id();
    patchy::ui::CanvasWidget canvas;
    canvas.resize(300, 200);
    canvas.set_document(&document);
    canvas.set_zoom(1.0);
    canvas.show();
    QApplication::processEvents();
    canvas.set_tool(tool);
    canvas.set_brush_size(31);
    canvas.set_brush_softness(40);
    canvas.set_local_adjustment_strength(60);
    canvas.set_local_tone_range(patchy::ui::CanvasWidget::LocalToneRange::Midtones);
    drag_document_path(canvas, {QPoint(30, 50), QPoint(80, 40), QPoint(130, 60)}, 6, modifiers);
    QApplication::processEvents();
    const auto bytes = std::as_const(*document.find_layer(layer_id)).pixels().data();
    return std::vector<std::uint8_t>(bytes.begin(), bytes.end());
  };
  using Tool = patchy::ui::CanvasTool;
  CHECK(paint(Tool::Dodge, Qt::AltModifier) == paint(Tool::Burn, Qt::NoModifier));
  CHECK(paint(Tool::Burn, Qt::AltModifier) == paint(Tool::Dodge, Qt::NoModifier));
  CHECK(paint(Tool::BlurBrush, Qt::AltModifier) == paint(Tool::SharpenBrush, Qt::NoModifier));
  CHECK(paint(Tool::SharpenBrush, Qt::AltModifier) == paint(Tool::BlurBrush, Qt::NoModifier));
  CHECK(paint(Tool::Dodge, Qt::AltModifier) != paint(Tool::Dodge, Qt::NoModifier));
  CHECK(paint(Tool::Sponge, Qt::AltModifier) == paint(Tool::Sponge, Qt::NoModifier));

  // The status bar names what a press would paint while Alt is held.
  patchy::Document document(80, 60, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Photo", solid_pixels(80, 60, patchy::PixelFormat::rgba8(), QColor(120, 120, 120, 255)));
  patchy::ui::CanvasWidget canvas;
  canvas.resize(200, 150);
  canvas.set_document(&document);
  canvas.show();
  QApplication::processEvents();
  QString status;
  canvas.set_status_callback([&status](QString message) { status = std::move(message); });
  canvas.set_tool(Tool::Dodge);
  QKeyEvent alt_down(QEvent::KeyPress, Qt::Key_Alt, Qt::NoModifier);
  QApplication::sendEvent(&canvas, &alt_down);
  CHECK(status.contains(QStringLiteral("Burn")));
  QKeyEvent alt_up(QEvent::KeyRelease, Qt::Key_Alt, Qt::AltModifier);
  QApplication::sendEvent(&canvas, &alt_up);
  CHECK(status == QStringLiteral("Dodge"));
  canvas.set_tool(Tool::SharpenBrush);
  QKeyEvent alt_down_again(QEvent::KeyPress, Qt::Key_Alt, Qt::NoModifier);
  QApplication::sendEvent(&canvas, &alt_down_again);
  CHECK(status.contains(QStringLiteral("Blur")));
  QKeyEvent alt_up_again(QEvent::KeyRelease, Qt::Key_Alt, Qt::AltModifier);
  QApplication::sendEvent(&canvas, &alt_up_again);
}

}  // namespace

std::vector<patchy::test::TestCase> brush_engine_stroke_tests_part3() {
  return {
      {"ui_toning_brush_span_and_strips_keep_bytes", ui_toning_brush_span_and_strips_keep_bytes},
      {"ui_toning_stroke_coalesces_queued_moves_and_keeps_bytes",
       ui_toning_stroke_coalesces_queued_moves_and_keeps_bytes},
      {"ui_dodge_burn_ranges_follow_calibrated_curves", ui_dodge_burn_ranges_follow_calibrated_curves},
      {"ui_toning_alt_stroke_paints_opposite_operation", ui_toning_alt_stroke_paints_opposite_operation},
  };
}
