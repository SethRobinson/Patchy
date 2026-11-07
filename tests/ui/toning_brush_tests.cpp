// The toning and detail brushes (Dodge, Burn, Sponge, Blur, Sharpen, Smudge): speed paths
// that must keep their bytes (GitHub issue 660). Part 3 of the brush-engine stroke group
// (brush_engine_stroke_tests.cpp registers it last, so the earlier order is unchanged).

#include "core/document.hpp"
#include "core/document_depth.hpp"
#include "core/pixel_depth.hpp"
#include "local_psd_fixtures.hpp"
#include "test_harness.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/qt_paths.hpp"
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

// GitHub issue 660 follow-up: Blur and Sharpen work per brush dab, like Photoshop's tools, so a
// stroke at the default Strength 50% visibly softens (or crisps) a hard edge and a noise patch,
// and painting over the same place again keeps building. Photoshop 2026 (40 px hard brush, one
// pass at Strength 50%) takes a 64|192 edge to 102|158 with Blur and 44|209 with its default
// Sharpen (Protect Detail on), and a
// noise patch's spread from 37 to 12 with Blur. Writes the crops (original, Blur x1, Blur x3,
// Sharpen x1, Sharpen x3; edge above, noise below) as test-artifacts/ui_blur_sharpen_build_up.png.
void ui_blur_sharpen_brushes_build_up_per_dab() {
  constexpr int kWidth = 400;
  constexpr int kHeight = 240;
  const auto noise_at = [](int x, int y) {
    auto h = static_cast<std::uint32_t>(x) * 73856093U ^ static_cast<std::uint32_t>(y) * 19349663U;
    h ^= h >> 13;
    h *= 0x5bd1e995U;
    h ^= h >> 15;
    return static_cast<std::uint8_t>(64 + h % 129);
  };
  struct Result {
    patchy::PixelBuffer pixels;
    int edge_left{0};
    int edge_right{0};
    double noise_spread{0.0};
  };
  const auto spread = [](const patchy::PixelBuffer& pixels) {
    double sum = 0.0;
    double sum_squares = 0.0;
    int count = 0;
    for (int y = 172; y <= 188; ++y) {
      for (int x = 150; x <= 250; ++x) {
        const auto value = static_cast<double>(pixels.pixel(x, y)[0]);
        sum += value;
        sum_squares += value * value;
        ++count;
      }
    }
    const auto mean = sum / count;
    return std::sqrt(std::max(0.0, sum_squares / count - mean * mean));
  };
  const auto paint = [&](patchy::ui::CanvasTool tool, int passes) {
    auto pixels = solid_pixels(kWidth, kHeight, patchy::PixelFormat::rgb8(), QColor(0, 0, 0));
    for (int y = 0; y < kHeight; ++y) {
      for (int x = 0; x < kWidth; ++x) {
        auto* px = pixels.pixel(x, y);
        px[0] = px[1] = px[2] = y < 120 ? static_cast<std::uint8_t>(x < 200 ? 64 : 192) : noise_at(x, y);
      }
    }
    patchy::Document document(kWidth, kHeight, patchy::PixelFormat::rgb8());
    const auto layer_id = document.add_pixel_layer("Edge and noise", std::move(pixels)).id();
    patchy::ui::CanvasWidget canvas;
    canvas.resize(460, 300);
    canvas.set_document(&document);
    canvas.set_zoom(1.0);
    canvas.show();
    QApplication::processEvents();
    canvas.set_tool(tool);
    canvas.set_brush_size(40);
    canvas.set_brush_softness(0);
    canvas.set_local_adjustment_strength(50);
    for (int pass = 0; pass < passes; ++pass) {
      for (const int y : {60, 180}) {
        std::vector<QPoint> path;
        for (int x = 100; x <= 300; x += 4) {
          path.emplace_back(x, y);
        }
        drag_document_path(canvas, path, 1);
        QApplication::processEvents();
      }
    }
    Result result;
    result.pixels = std::as_const(*document.find_layer(layer_id)).pixels();
    result.edge_left = result.pixels.pixel(199, 60)[0];
    result.edge_right = result.pixels.pixel(200, 60)[0];
    result.noise_spread = spread(result.pixels);
    return result;
  };
  using Tool = patchy::ui::CanvasTool;
  const auto original = paint(Tool::BlurBrush, 0);
  const auto blur_once = paint(Tool::BlurBrush, 1);
  const auto blur_thrice = paint(Tool::BlurBrush, 3);
  const auto sharpen_once = paint(Tool::SharpenBrush, 1);
  const auto sharpen_thrice = paint(Tool::SharpenBrush, 3);
  // The crops first, so a failing check still leaves the picture.
  ensure_artifact_dir();
  QImage image(200, 5 * 84, QImage::Format_RGB32);
  image.fill(qRgb(40, 40, 40));
  int row = 0;
  for (const auto* result : {&original, &blur_once, &blur_thrice, &sharpen_once, &sharpen_thrice}) {
    for (int y = 0; y < 80; ++y) {
      const auto source_y = y < 40 ? 40 + y : 160 + (y - 40);
      for (int x = 0; x < 200; ++x) {
        const auto* px = result->pixels.pixel(100 + x, source_y);
        image.setPixel(x, row * 84 + y, qRgb(px[0], px[1], px[2]));
      }
    }
    ++row;
  }
  const auto saved = image.save(QStringLiteral("test-artifacts/ui_blur_sharpen_build_up.png"));
  std::printf("[INFO] blur/sharpen edge 64|192 -> blur x1 %d|%d, x3 %d|%d; sharpen x1 %d|%d, x3 %d|%d; noise spread "
              "%.1f -> blur x1 %.1f, x3 %.1f; sharpen x1 %.1f, x3 %.1f\n",
              blur_once.edge_left, blur_once.edge_right, blur_thrice.edge_left, blur_thrice.edge_right,
              sharpen_once.edge_left, sharpen_once.edge_right, sharpen_thrice.edge_left, sharpen_thrice.edge_right,
              original.noise_spread, blur_once.noise_spread, blur_thrice.noise_spread, sharpen_once.noise_spread,
              sharpen_thrice.noise_spread);
  CHECK(original.edge_left == 64 && original.edge_right == 192);
  // One pass at the default Strength clearly softens the edge and the noise (Photoshop: 102|158, 37 to 12).
  CHECK(blur_once.edge_left >= 64 + 28 && blur_once.edge_right <= 192 - 28);
  CHECK(blur_once.noise_spread <= original.noise_spread * 0.5);
  // Painting over it again keeps softening.
  CHECK(blur_thrice.edge_left >= blur_once.edge_left + 6);
  CHECK(blur_thrice.noise_spread <= blur_once.noise_spread * 0.75);
  // Sharpen crisps the edge (Photoshop's default: 44|209) and builds up too.
  CHECK(sharpen_once.edge_left <= 64 - 15 && sharpen_once.edge_right >= 192 + 15);
  CHECK(sharpen_once.noise_spread >= original.noise_spread * 1.25);
  CHECK(sharpen_thrice.noise_spread >= sharpen_once.noise_spread * 1.1);
  CHECK(saved);
}

// GitHub issue 660, second follow-up: Sharpen scrubbed over a JPEG face turned its chroma noise
// into magenta and green speckle and clipped it to full range within two passes (a 3x3 step at
// 0.75 per channel per dab). Sharpen now moves every channel of a pixel by the same lightness
// change, so on a noisy color patch the color differences stay as they were, nothing clips, and
// five passes at the default Strength sharpen the noise but stay bounded.
void ui_sharpen_keeps_color_and_stays_bounded() {
  constexpr int kWidth = 200;
  constexpr int kHeight = 120;
  const auto noise_at = [](int x, int y, int channel) {
    auto h = static_cast<std::uint32_t>(x) * 73856093U ^ static_cast<std::uint32_t>(y) * 19349663U ^
             static_cast<std::uint32_t>(channel) * 83492791U;
    h ^= h >> 13;
    h *= 0x5bd1e995U;
    h ^= h >> 15;
    return static_cast<int>(h % 17) - 8;
  };
  auto pixels = solid_pixels(kWidth, kHeight, patchy::PixelFormat::rgb8(), QColor(0, 0, 0));
  constexpr std::array<int, 3> kBase{180, 120, 90};
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      auto* px = pixels.pixel(x, y);
      for (int channel = 0; channel < 3; ++channel) {
        px[channel] = static_cast<std::uint8_t>(kBase[static_cast<std::size_t>(channel)] + noise_at(x, y, channel));
      }
    }
  }
  const auto original = pixels;
  patchy::Document document(kWidth, kHeight, patchy::PixelFormat::rgb8());
  const auto layer_id = document.add_pixel_layer("Skin", std::move(pixels)).id();
  patchy::ui::CanvasWidget canvas;
  canvas.resize(260, 180);
  canvas.set_document(&document);
  canvas.set_zoom(1.0);
  canvas.show();
  QApplication::processEvents();
  canvas.set_tool(patchy::ui::CanvasTool::SharpenBrush);
  canvas.set_brush_size(40);
  canvas.set_brush_softness(0);
  canvas.set_local_adjustment_strength(50);
  for (int pass = 0; pass < 5; ++pass) {
    std::vector<QPoint> path;
    for (int x = 30; x <= 170; x += 4) {
      path.emplace_back(x, 60);
    }
    drag_document_path(canvas, path, 1);
    QApplication::processEvents();
  }
  const auto& result = std::as_const(*document.find_layer(layer_id)).pixels();
  const auto luma = [](const std::uint8_t* px) { return (54.0 * px[0] + 183.0 * px[1] + 19.0 * px[2]) / 256.0; };
  const auto spread = [&luma](const patchy::PixelBuffer& buffer) {
    double sum = 0.0;
    double sum_squares = 0.0;
    int count = 0;
    for (int y = 50; y <= 70; ++y) {
      for (int x = 50; x <= 150; ++x) {
        const auto value = luma(buffer.pixel(x, y));
        sum += value;
        sum_squares += value * value;
        ++count;
      }
    }
    const auto mean = sum / count;
    return std::sqrt(std::max(0.0, sum_squares / count - mean * mean));
  };
  int clipped = 0;
  int color_shifts = 0;
  for (int y = 50; y <= 70; ++y) {
    for (int x = 50; x <= 150; ++x) {
      const auto* after = result.pixel(x, y);
      const auto* before = original.pixel(x, y);
      if (after[0] == 0 || after[0] == 255 || after[1] == 0 || after[1] == 255 || after[2] == 0 || after[2] == 255) {
        ++clipped;
        continue;
      }
      const auto red_green = (after[0] - after[1]) - (before[0] - before[1]);
      const auto blue_green = (after[2] - after[1]) - (before[2] - before[1]);
      if (std::abs(red_green) > 1 || std::abs(blue_green) > 1) {
        ++color_shifts;
      }
    }
  }
  const auto before_spread = spread(original);
  const auto after_spread = spread(result);
  std::printf("[INFO] sharpen x5 on color noise: luma spread %.2f -> %.2f, clipped %d, color shifts %d\n",
              before_spread, after_spread, clipped, color_shifts);
  CHECK(clipped == 0);
  CHECK(color_shifts == 0);
  CHECK(after_spread >= before_spread * 1.3);
  CHECK(after_spread <= before_spread * 8.0);
}

// GitHub issue 660, second follow-up, on real photos: 512 px crops of a portrait (full size, and
// a quarter-size JPEG at quality 80; local-test-fixtures/sharpen-photo) sharpened at the default
// Strength with a hard 60 px brush in lines 60 px apart, one, two and five passes, the rig
// Photoshop 2026 was measured with. Writes test-artifacts/ui_sharpen_photo_<crop>_x<passes>.png.
// Pixels keep their color differences (within rounding) unless a channel clipped, at most 0.2%
// of them may shift, and no more pixels may clip than Photoshop's default Sharpen clips (the old
// per-channel rule clipped 20% after five passes and shifted the color of 27 to 79%).
void ui_sharpen_photo_keeps_color_over_passes_if_available() {
  for (const char* crop : {"face", "small"}) {
    const auto path = patchy::test::local_format_fixture_path("sharpen-photo", std::string(crop) + ".png");
    patchy::test::skip_unless_exists(path);
    QImage source(patchy::ui::to_qstring(path));
    CHECK(!source.isNull());
    source = source.convertToFormat(QImage::Format_RGB888);
    const auto width = source.width();
    const auto height = source.height();
    auto pixels = solid_pixels(width, height, patchy::PixelFormat::rgb8(), QColor(0, 0, 0));
    for (int y = 0; y < height; ++y) {
      const auto* line = source.constScanLine(y);
      for (int x = 0; x < width; ++x) {
        auto* px = pixels.pixel(x, y);
        px[0] = line[x * 3];
        px[1] = line[x * 3 + 1];
        px[2] = line[x * 3 + 2];
      }
    }
    const auto original = pixels;
    patchy::Document document(width, height, patchy::PixelFormat::rgb8());
    const auto layer_id = document.add_pixel_layer("Photo", std::move(pixels)).id();
    patchy::ui::CanvasWidget canvas;
    canvas.resize(width + 140, height + 140);
    canvas.set_document(&document);
    canvas.set_zoom(1.0);
    canvas.show();
    QApplication::processEvents();
    canvas.set_tool(patchy::ui::CanvasTool::SharpenBrush);
    canvas.set_brush_size(60);
    canvas.set_brush_softness(0);
    canvas.set_local_adjustment_strength(50);
    int done = 0;
    for (const int passes : {1, 2, 5}) {
      for (; done < passes; ++done) {
        for (int y = 30; y < height; y += 60) {
          std::vector<QPoint> path_points;
          for (int x = -30; x <= width + 30; x += 6) {
            path_points.emplace_back(x, y);
          }
          drag_document_path(canvas, path_points, 1);
          QApplication::processEvents();
        }
      }
      const auto& result = std::as_const(*document.find_layer(layer_id)).pixels();
      QImage image(width, height, QImage::Format_RGB32);
      int stroked = 0;
      int clipped = 0;
      int color_shifts = 0;
      for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
          const auto* after = result.pixel(x, y);
          image.setPixel(x, y, qRgb(after[0], after[1], after[2]));
          // Only the rows within 20 px of a line, which every pass covers fully.
          if (const auto offset = (y + 30) % 60; offset > 20 && offset < 40) {
            continue;
          }
          const auto* before = original.pixel(x, y);
          ++stroked;
          const auto clips = [](const std::uint8_t* px) {
            return px[0] == 0 || px[0] == 255 || px[1] == 0 || px[1] == 255 || px[2] == 0 || px[2] == 255;
          };
          if (clips(after)) {
            clipped += clips(before) ? 0 : 1;
            continue;
          }
          const auto red_green = (after[0] - after[1]) - (before[0] - before[1]);
          const auto blue_green = (after[2] - after[1]) - (before[2] - before[1]);
          if (std::abs(red_green) > 1 || std::abs(blue_green) > 1) {
            ++color_shifts;
          }
        }
      }
      ensure_artifact_dir();
      const auto saved = image.save(QStringLiteral("test-artifacts/ui_sharpen_photo_%1_x%2.png")
                                        .arg(QString::fromLatin1(crop))
                                        .arg(passes));
      std::printf("[INFO] sharpen photo %s x%d: clipped %.3f, color shifts %.3f\n", crop, passes,
                  static_cast<double>(clipped) / stroked, static_cast<double>(color_shifts) / stroked);
      CHECK(saved);
      // A pixel one channel of which clipped in an earlier pass may shift; nothing else does.
      CHECK(color_shifts * 500 <= stroked);
      // At most what Photoshop's default Sharpen clips on the quarter-size crop (3%, 7.7%, 22%).
      CHECK(clipped * 100 <= stroked * (passes == 1 ? 3 : passes == 2 ? 7 : 20));
    }
  }
}

}  // namespace

std::vector<patchy::test::TestCase> brush_engine_stroke_tests_part3() {
  return {
      {"ui_toning_brush_span_and_strips_keep_bytes", ui_toning_brush_span_and_strips_keep_bytes},
      {"ui_toning_stroke_coalesces_queued_moves_and_keeps_bytes",
       ui_toning_stroke_coalesces_queued_moves_and_keeps_bytes},
      {"ui_dodge_burn_ranges_follow_calibrated_curves", ui_dodge_burn_ranges_follow_calibrated_curves},
      {"ui_toning_alt_stroke_paints_opposite_operation", ui_toning_alt_stroke_paints_opposite_operation},
      {"ui_blur_sharpen_brushes_build_up_per_dab", ui_blur_sharpen_brushes_build_up_per_dab},
      {"ui_sharpen_keeps_color_and_stays_bounded", ui_sharpen_keeps_color_and_stays_bounded},
      {"ui_sharpen_photo_keeps_color_over_passes_if_available", ui_sharpen_photo_keeps_color_over_passes_if_available},
  };
}
