// The Wayland drag rule (docs/interactive-previews-drag-coalescing.md, "Rule for new drags"):
// Wayland delivers every pointer motion, so a drag whose move does real work coalesces its
// queued moves. The drags the November 2026 audit fixed (Path Select's transform box, Mixer
// Brush and Pattern Stamp strokes, the Type tool's box handles) and the guard that makes a new
// canvas drag branch name its coalescing.
#include "core/layer_metadata.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/main_window.hpp"
#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include <QApplication>
#include <QFile>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSpinBox>
#include <QTextEdit>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace patchy::test::ui;

namespace {

using patchy::ui::CanvasTool;
using patchy::ui::CanvasWidget;
using patchy::ui::MainWindowTestAccess;

void post_move(QWidget& widget, QPoint position, QPoint global) {
  QApplication::postEvent(&widget, new QMouseEvent(QEvent::MouseMove, QPointF(position), QPointF(global),
                                                   Qt::NoButton, Qt::LeftButton, Qt::NoModifier));
}

void deliver_move(QWidget& widget, QPoint position, QPoint global) {
  QMouseEvent event(QEvent::MouseMove, QPointF(position), QPointF(global), Qt::NoButton, Qt::LeftButton,
                    Qt::NoModifier);
  QApplication::sendEvent(&widget, &event);
}

const patchy::PathAnchor& first_anchor(patchy::Document& document, patchy::LayerId id) {
  const auto* layer = std::as_const(document).find_layer(id);
  static const patchy::PathAnchor none{};
  if (layer == nullptr || layer->vector_shape() == nullptr || layer->vector_shape()->path.subpaths.empty() ||
      layer->vector_shape()->path.subpaths[0].anchors.empty()) {
    CHECK(false);
    return none;
  }
  return layer->vector_shape()->path.subpaths[0].anchors[0];
}

// Path Select's transform box (Ctrl+T with a path tool) re-rasterized the shape layer for every
// move, so on Wayland the box and the fill fell seconds behind the pointer on a large shape. Its
// moves now coalesce like a Path Select drag's (GitHub #678): queued moves apply once at the latest
// position, a move recorded right before the release still lands, and the session commits as one
// undo step.
void ui_path_transform_drag_coalesces_queued_moves() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = MainWindowTestAccess::document(window);
  canvas->set_view_zoom_centered(1.0);
  canvas->set_tool(CanvasTool::Rectangle);
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  CHECK(radius_spin != nullptr);
  if (radius_spin != nullptr) {
    radius_spin->setValue(0);
  }
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(100, 100)),
       canvas->widget_position_for_document_point(QPoint(300, 220)));
  QApplication::processEvents();
  const auto active = document.active_layer_id();
  CHECK(active.has_value());
  if (!active.has_value()) {
    return;
  }
  const auto layer_id = *active;
  canvas->set_snap_enabled(false);
  require_action(window, "toolPathSelectAction")->trigger();
  QApplication::processEvents();
  const auto click = canvas->widget_position_for_document_point(QPoint(200, 100));
  drag(*canvas, click, click);
  QApplication::processEvents();
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->path_transform_active());
  if (!canvas->path_transform_active()) {
    return;
  }
  const auto anchor_x = first_anchor(document, layer_id).anchor_x;
  const auto anchor_y = first_anchor(document, layer_id).anchor_y;
  canvas->set_drag_move_coalescing_for_testing(CanvasWidget::DragMoveCoalescing::AllMoves);
  const auto diagnostics = [canvas] { return canvas->render_cache_diagnostics(); };
  const auto start = canvas->widget_position_for_document_point(QPoint(200, 160));
  send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  const auto before = diagnostics();
  for (int move = 1; move <= 30; ++move) {
    const auto position = start + QPoint(move, 0);
    post_move(*canvas, position, canvas->mapToGlobal(position));
  }
  QApplication::processEvents();
  QApplication::processEvents();
  QApplication::processEvents();
  const auto after = diagnostics();
  // The first move applied at once, the other 29 waited for the pass end, which applied the latest.
  CHECK(after.drag_moves_coalesced - before.drag_moves_coalesced == 29);
  std::printf("[INFO] path transform: %d rerasters for 30 queued moves\n",
              after.path_drag_rerasters - before.path_drag_rerasters);
  CHECK(after.path_drag_rerasters - before.path_drag_rerasters <= 2);
  CHECK(std::abs(first_anchor(document, layer_id).anchor_x - (anchor_x + 30.0)) < 1.0);
  const auto end = start + QPoint(45, 12);
  deliver_move(*canvas, end - QPoint(3, 0), canvas->mapToGlobal(end - QPoint(3, 0)));  // applied, opens a pass
  deliver_move(*canvas, end, canvas->mapToGlobal(end));                                // recorded
  CHECK(diagnostics().drag_moves_coalesced - before.drag_moves_coalesced == 30);
  send_mouse(*canvas, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  canvas->commit_path_transform();
  QApplication::processEvents();
  CHECK(!canvas->path_transform_active());
  CHECK(std::abs(first_anchor(document, layer_id).anchor_x - (anchor_x + 45.0)) < 1.0);
  CHECK(std::abs(first_anchor(document, layer_id).anchor_y - (anchor_y + 12.0)) < 1.0);
  MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(std::abs(first_anchor(document, layer_id).anchor_x - anchor_x) < 0.5);
}

// Mixer Brush and Pattern Stamp strokes painted every queued move one by one; they now coalesce
// like Brush and Clone strokes (GitHub #597, #677), keeping every recorded position as a folded
// sample, so a queued stroke paints the same bytes as one whose moves arrive one by one.
void ui_mixer_and_pattern_stamp_strokes_coalesce_queued_moves_and_keep_bytes() {
  for (const auto tool : {CanvasTool::MixerBrush, CanvasTool::PatternStamp}) {
    struct Result {
      std::vector<std::uint8_t> bytes;
      int coalesced{0};
    };
    const auto paint = [tool](bool queued) {
      constexpr int kWidth = 700;
      constexpr int kHeight = 500;
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
      CanvasWidget canvas;
      canvas.resize(480, 360);
      canvas.set_document(&document);
      canvas.set_zoom(0.5);
      canvas.set_drag_move_coalescing_for_testing(queued ? CanvasWidget::DragMoveCoalescing::AllMoves
                                                         : CanvasWidget::DragMoveCoalescing::Off);
      canvas.show();
      QApplication::processEvents();
      canvas.set_tool(tool);
      if (tool == CanvasTool::PatternStamp) {
        patchy::PatternResource pattern;
        pattern.id = "drag-coalescing-test";
        pattern.name = "Checks";
        pattern.tile = solid_pixels(16, 16, patchy::PixelFormat::rgba8(), QColor(240, 200, 40));
        fill_pixel_rect(pattern.tile, QRect(0, 0, 8, 8), QColor(20, 60, 200));
        fill_pixel_rect(pattern.tile, QRect(8, 8, 8, 8), QColor(20, 60, 200));
        canvas.set_pattern_stamp_pattern(pattern);
      }
      canvas.set_primary_color(QColor(30, 90, 200));
      canvas.set_brush_size(61);
      canvas.set_brush_softness(50);
      canvas.set_brush_opacity(100);
      canvas.set_brush_flow(100);
      canvas.set_brush_build_up(false);
      const auto widget_point = [&canvas](QPointF document_point) {
        return canvas.widget_position_for_document_point(document_point.toPoint());
      };
      std::vector<QPointF> path;
      for (int index = 0; index <= 90; ++index) {
        const auto t = static_cast<double>(index);
        const auto travel = std::fmod(14.0 * t, 800.0);
        path.emplace_back(100.0 + (travel < 400.0 ? travel : 800.0 - travel), 100.0 + 3.0 * t);
      }
      for (int wait = 0; wait < 200 && !canvas.render_settled(); ++wait) {
        canvas.repaint();
        QApplication::processEvents();
      }
      const auto before = canvas.render_cache_diagnostics();
      send_mouse(canvas, QEvent::MouseButtonPress, widget_point(path.front()), Qt::LeftButton, Qt::LeftButton);
      for (std::size_t index = 1; index < path.size(); ++index) {
        const auto position = widget_point(path[index]);
        if (queued) {
          // Queued as a window system queues them, in bursts of nine per event-loop pass.
          post_move(canvas, position, canvas.mapToGlobal(position));
          if (index % 9 == 0) {
            QApplication::processEvents();
          }
        } else {
          deliver_move(canvas, position, canvas.mapToGlobal(position));
        }
      }
      QApplication::processEvents();
      QApplication::processEvents();
      send_mouse(canvas, QEvent::MouseButtonRelease, widget_point(path.back()), Qt::LeftButton, Qt::NoButton);
      QApplication::processEvents();
      Result result;
      const auto bytes = std::as_const(*document.find_layer(layer_id)).pixels().data();
      result.bytes.assign(bytes.begin(), bytes.end());
      result.coalesced = canvas.render_cache_diagnostics().drag_moves_coalesced - before.drag_moves_coalesced;
      return result;
    };
    const auto sent = paint(false);
    const auto queued = paint(true);
    std::printf("[INFO] %s stroke: %d moves coalesced\n",
                tool == CanvasTool::MixerBrush ? "Mixer Brush" : "Pattern Stamp", queued.coalesced);
    CHECK(sent.coalesced == 0);
    CHECK(queued.coalesced > 0);
    CHECK(queued.bytes == sent.bytes);
  }
}

// The Type tool's box handles re-laid out the whole story for every move, so on Wayland a resize
// of a long paragraph stood still and then jumped. The handles' moves now coalesce through the
// widget drag coalescing (defer_widget_drag_move: MainWindow's filter handles them): a burst of
// moves resizes once, at the latest position, the release applies a move recorded right before
// it, and the box ends where the same moves applied one by one leave it.
void ui_text_box_resize_drag_coalesces_queued_moves() {
  struct Result {
    int coalesced{0};
    int width_after_burst{0};
    int width_after_release{0};
    int start_width{0};
  };
  const auto run = [](bool queued) {
    Result result;
    patchy::ui::MainWindow window;
    show_window(window);  // a 1024 x 768 document
    window.set_cli_automation_mode(true);  // Match --run-script's missing-font substitution on Linux.
    auto* canvas = require_canvas(window);
    canvas->set_zoom(1.0);
    QApplication::processEvents();
    require_action_by_text(window, QStringLiteral("Type"))->trigger();
    QApplication::processEvents();
    drag(*canvas, canvas->widget_position_for_document_point(QPoint(40, 40)),
         canvas->widget_position_for_document_point(QPoint(400, 240)));
    process_events_for(150);
    auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
    CHECK(editor != nullptr);
    if (editor == nullptr) {
      return result;
    }
    editor->setPlainText(QStringLiteral("The quick brown fox jumps over the lazy dog. ").repeated(6));
    process_events_for(150);
    auto* handle =
        canvas->findChild<QWidget*>(QStringLiteral("textBoxResizeHandleRight"), Qt::FindDirectChildrenOnly);
    CHECK(handle != nullptr && handle->isVisible());
    if (handle == nullptr) {
      return result;
    }
    editor->setFocus(Qt::OtherFocusReason);
    QApplication::processEvents();
    const auto width = [editor] { return editor->property("patchy.documentTextWidth").toInt(); };
    result.start_width = width();
    patchy::ui::set_slider_drag_coalescing_all_moves_for_testing(queued);
    const auto restore = qScopeGuard([] { patchy::ui::set_slider_drag_coalescing_all_moves_for_testing(false); });
    // Pressed and dragged on the canvas at the handle, as the side-handle tests drive it.
    const auto start = handle->geometry().center();
    send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    const auto before = patchy::ui::slider_drag_moves_coalesced_for_testing();
    // Sent, not posted: the coalescing's AllMoves switch records sent moves too, and a move posted
    // behind the handle's first preview would find the session re-laid out under it.
    for (int move = 1; move <= 30; ++move) {
      const auto position = start + QPoint(move * 3, 0);
      deliver_move(*canvas, position, canvas->mapToGlobal(position));
    }
    // The handle's preview lands after its moves (schedule_text_editor_preview), so the box is
    // read once that has settled.
    process_events_for(150);
    result.width_after_burst = width();
    const auto end = start + QPoint(130, 0);
    deliver_move(*canvas, end - QPoint(4, 0), canvas->mapToGlobal(end - QPoint(4, 0)));  // applied, opens a pass
    deliver_move(*canvas, end, canvas->mapToGlobal(end));                                // recorded when queued
    result.coalesced = patchy::ui::slider_drag_moves_coalesced_for_testing() - before;
    send_mouse(*canvas, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
    process_events_for(50);
    result.width_after_release = width();
    return result;
  };
  const auto sent = run(false);
  const auto queued = run(true);
  std::printf("[INFO] text box: width %d, then %d/%d after the burst sent/queued, %d/%d after the release, "
              "%d coalesced\n",
              sent.start_width, sent.width_after_burst, queued.width_after_burst, sent.width_after_release,
              queued.width_after_release, queued.coalesced);
  CHECK(sent.coalesced == 0);
  CHECK(queued.coalesced == 30);  // 29 of the burst, and the move right before the release
  CHECK(sent.width_after_burst > sent.start_width + 60);
  CHECK(queued.width_after_burst == sent.width_after_burst);
  CHECK(queued.width_after_release == sent.width_after_release);
  CHECK(sent.width_after_release > sent.width_after_burst + 20);
}

// The Type box's corner rotate zones (#684 follow-up) re-render the turned preview per move: a
// burst of moves turns once, at the latest position, the release applies a move recorded right
// before it, and the box ends at the angle the same moves applied one by one leave it.
void ui_text_box_rotate_drag_coalesces_queued_moves() {
  struct Result {
    int coalesced{0};
    double turn_after_burst{0.0};
    double turn_after_release{0.0};
  };
  const auto turn_of = [](QTextEdit& editor) {
    const auto transform =
        patchy::parse_layer_affine_transform(editor.property("patchy.textTransformOverride").toString().toStdString());
    return transform.has_value() ? std::atan2((*transform)[1], (*transform)[0]) * 180.0 / 3.14159265358979323846
                                 : 0.0;
  };
  const auto run = [&](bool queued) {
    Result result;
    patchy::ui::MainWindow window;
    show_window(window);  // a 1024 x 768 document
    window.set_cli_automation_mode(true);
    auto* canvas = require_canvas(window);
    canvas->set_zoom(1.0);
    QApplication::processEvents();
    require_action_by_text(window, QStringLiteral("Type"))->trigger();
    QApplication::processEvents();
    drag(*canvas, canvas->widget_position_for_document_point(QPoint(200, 200)),
         canvas->widget_position_for_document_point(QPoint(560, 400)));
    process_events_for(150);
    auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
    CHECK(editor != nullptr);
    if (editor == nullptr) {
      return result;
    }
    editor->setPlainText(QStringLiteral("The quick brown fox jumps over the lazy dog. ").repeated(6));
    process_events_for(150);
    editor->setFocus(Qt::OtherFocusReason);
    QApplication::processEvents();
    const auto frame = editor->geometry();
    const QPointF center(frame.left() + frame.width() / 2.0, frame.top() + frame.height() / 2.0);
    const QPoint start = QPoint(frame.right() + 1, frame.bottom() + 1) + QPoint(14, 14);
    const auto at = [&](double degrees) {
      const auto radians = degrees * 3.14159265358979323846 / 180.0;
      const auto offset = QPointF(start) - center;
      return (center + QPointF(offset.x() * std::cos(radians) - offset.y() * std::sin(radians),
                               offset.x() * std::sin(radians) + offset.y() * std::cos(radians)))
          .toPoint();
    };
    patchy::ui::set_slider_drag_coalescing_all_moves_for_testing(queued);
    const auto restore = qScopeGuard([] { patchy::ui::set_slider_drag_coalescing_all_moves_for_testing(false); });
    send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    const auto before = patchy::ui::slider_drag_moves_coalesced_for_testing();
    for (int move = 1; move <= 30; ++move) {
      const auto position = at(move * 1.0);
      deliver_move(*canvas, position, canvas->mapToGlobal(position));
    }
    process_events_for(150);
    result.turn_after_burst = turn_of(*editor);
    deliver_move(*canvas, at(38.0), canvas->mapToGlobal(at(38.0)));  // applied, opens a pass
    deliver_move(*canvas, at(40.0), canvas->mapToGlobal(at(40.0)));  // recorded when queued
    result.coalesced = patchy::ui::slider_drag_moves_coalesced_for_testing() - before;
    send_mouse(*canvas, QEvent::MouseButtonRelease, at(40.0), Qt::LeftButton, Qt::NoButton);
    process_events_for(50);
    result.turn_after_release = turn_of(*editor);
    return result;
  };
  const auto sent = run(false);
  const auto queued = run(true);
  std::printf("[INFO] text box turn: %.2f/%.2f after the burst sent/queued, %.2f/%.2f after the release, "
              "%d coalesced\n",
              sent.turn_after_burst, queued.turn_after_burst, sent.turn_after_release, queued.turn_after_release,
              queued.coalesced);
  CHECK(sent.coalesced == 0);
  CHECK(queued.coalesced == 30);  // 29 of the burst, and the move right before the release
  CHECK(std::abs(sent.turn_after_burst - 30.0) < 2.0);
  CHECK(std::abs(queued.turn_after_burst - sent.turn_after_burst) < 0.01);
  CHECK(std::abs(queued.turn_after_release - sent.turn_after_release) < 0.01);
  CHECK(std::abs(sent.turn_after_release - 40.0) < 2.0);
}

// A marquee drag (it rebuilds the selection per move), the Magnetic Lasso's hover trace (an edge
// search per move), panning, view rotation and the scrubby zoom (each re-lays out the view) apply
// only the newest move of each event-loop pass, and stand where the same moves sent one by one
// leave them. The Magnetic Lasso's anchors depend on the moves it solved, so it checks that its
// moves coalesce and that a click after the burst lands where the pointer is.
void ui_view_and_selection_drags_coalesce_queued_moves() {
  struct Case {
    const char* name;
    CanvasTool tool;
    bool hover;  // the gesture is a click, then moves with no button held
    std::vector<std::size_t> compared;  // the state entries the drag sets (see `state` below)
  };
  // Rotation and zoom keep the view inside its pan limits at every step they take, so where the
  // canvas origin ends up depends on the steps; the angle and the zoom do not.
  const Case cases[] = {
      {"ellipse marquee", CanvasTool::EllipticalMarquee, false, {0, 1, 2, 3}},
      {"pan", CanvasTool::Pan, false, {4, 5}},
      {"rotate view", CanvasTool::RotateView, false, {7}},
      {"zoom scrub", CanvasTool::Zoom, false, {6}},
      {"magnetic lasso", CanvasTool::MagneticLasso, true, {}},
  };
  for (const auto& entry : cases) {
    const auto run = [&entry](bool queued) {
      constexpr int kWidth = 1600;
      constexpr int kHeight = 1200;
      auto pixels = solid_pixels(kWidth, kHeight, patchy::PixelFormat::rgba8(), QColor(40, 40, 40, 255));
      fill_pixel_rect(pixels, QRect(500, 300, 600, 500), QColor(220, 220, 220));
      patchy::Document document(kWidth, kHeight, patchy::PixelFormat::rgba8());
      document.add_pixel_layer("Photo", std::move(pixels));
      CanvasWidget canvas;
      canvas.resize(640, 480);
      canvas.set_document(&document);
      canvas.set_zoom(0.25);
      canvas.set_zoom_scrubby(entry.tool == CanvasTool::Zoom);
      canvas.set_drag_move_coalescing_for_testing(queued ? CanvasWidget::DragMoveCoalescing::AllMoves
                                                         : CanvasWidget::DragMoveCoalescing::Off);
      canvas.show();
      QApplication::processEvents();
      canvas.set_tool(entry.tool);
      QApplication::processEvents();
      // Rotate View turns about the view centre and ignores the pointer near it, so its moves
      // follow an arc around the centre; the other drags move diagonally.
      const QPointF centre(canvas.width() / 2.0, canvas.height() / 2.0);
      const auto arc = [&centre](int step) {
        const auto radians = (-150.0 + 2.0 * step) * 3.14159265358979 / 180.0;
        return (centre + 150.0 * QPointF(std::cos(radians), std::sin(radians))).toPoint();
      };
      const auto rotating = entry.tool == CanvasTool::RotateView;
      const auto start = rotating ? arc(0) : canvas.widget_position_for_document_point(QPoint(600, 400));
      send_mouse(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
      if (entry.hover) {
        send_mouse(canvas, QEvent::MouseButtonRelease, start, Qt::LeftButton, Qt::NoButton);
      }
      QApplication::processEvents();
      const auto buttons = entry.hover ? Qt::NoButton : Qt::LeftButton;
      const auto before = canvas.render_cache_diagnostics().drag_moves_coalesced;
      QPoint position = start;
      for (int move = 1; move <= 30; ++move) {
        position = rotating ? arc(move) : start + QPoint(move * 4, move * 3);
        auto* event = new QMouseEvent(QEvent::MouseMove, QPointF(position), QPointF(canvas.mapToGlobal(position)),
                                      Qt::NoButton, buttons, Qt::NoModifier);
        if (queued) {
          QApplication::postEvent(&canvas, event);
        } else {
          QApplication::sendEvent(&canvas, event);
          delete event;
        }
      }
      QApplication::processEvents();
      QApplication::processEvents();
      QApplication::processEvents();
      const auto coalesced = canvas.render_cache_diagnostics().drag_moves_coalesced - before;
      // Where the drag stands mid-gesture: the marquee, the view, the zoom, the rotation.
      const auto readout = canvas.selection_readout_rect().value_or(QRect());
      const auto origin = canvas.widget_position_f(QPointF(0.0, 0.0));
      std::vector<double> state{static_cast<double>(readout.x()),      static_cast<double>(readout.y()),
                                static_cast<double>(readout.width()),  static_cast<double>(readout.height()),
                                std::round(origin.x() * 100.0),        std::round(origin.y() * 100.0),
                                std::round(canvas.zoom() * 1.0e6),     std::round(canvas.view_rotation() * 1.0e4)};
      int anchors_before_click = 0;
      int anchors_after_click = 0;
      if (entry.hover) {
        anchors_before_click = canvas.magnetic_lasso_anchor_count();
        send_mouse(canvas, QEvent::MouseButtonPress, position, Qt::LeftButton, Qt::LeftButton);
        send_mouse(canvas, QEvent::MouseButtonRelease, position, Qt::LeftButton, Qt::NoButton);
        anchors_after_click = canvas.magnetic_lasso_anchor_count();
        send_key(canvas, Qt::Key_Escape);
      } else {
        send_mouse(canvas, QEvent::MouseButtonRelease, position, Qt::LeftButton, Qt::NoButton);
      }
      QApplication::processEvents();
      return std::tuple{coalesced, state, anchors_after_click - anchors_before_click};
    };
    const auto [sent_coalesced, sent_state, sent_click] = run(false);
    const auto [queued_coalesced, queued_state, queued_click] = run(true);
    std::printf("[INFO] %s: %d of 30 queued moves coalesced\n", entry.name, queued_coalesced);
    CHECK(sent_coalesced == 0);
    CHECK(queued_coalesced == 29);
    if (entry.hover) {
      // The click flushed the recorded move first, then dropped its anchor there.
      CHECK(queued_click >= 1);
      CHECK(sent_click >= 1);
    } else {
      // Equal up to rounding: a scrub multiplies its zoom steps, the coalesced one takes them at once.
      CHECK(queued_state.size() == sent_state.size());
      for (const auto index : entry.compared) {
        if (std::abs(queued_state[index] - sent_state[index]) > 2.0) {
          std::printf("[INFO] %s: state %zu is %.0f queued, %.0f sent\n", entry.name, index, queued_state[index],
                      sent_state[index]);
          CHECK(false);
        }
      }
    }
  }
}

// The guard: every top-level branch of CanvasWidget::mouseMoveEvent is listed here with how its
// moves stay cheap on Wayland. A new branch (a new drag) fails this test until it is classified:
// join coalescing_drag_active when its move does real work, or record its measured cost
// (docs/interactive-previews-drag-coalescing.md, "Rule for new drags").
void ui_canvas_drag_branches_follow_the_wayland_drag_rule() {
  QFile source(QStringLiteral(PATCHY_SOURCE_DIR "/src/ui/canvas_widget_events.cpp"));
  CHECK(source.open(QIODevice::ReadOnly));
  if (!source.isOpen()) {
    return;
  }
  const auto text = QString::fromUtf8(source.readAll());
  const auto begin = text.indexOf(QStringLiteral("void CanvasWidget::mouseMoveEvent(QMouseEvent* event) {"));
  CHECK(begin >= 0);
  const auto end = text.indexOf(QStringLiteral("\nvoid CanvasWidget::"), begin + 10);
  const auto lines = text.mid(begin, end - begin).split(QLatin1Char('\n'));
  std::vector<std::string> branches;
  static const QRegularExpression branch_start(QStringLiteral("^  (?:\\} else )?if \\((.*)$"));
  for (qsizetype index = 0; index < lines.size(); ++index) {
    const auto match = branch_start.match(lines[index]);
    if (!match.hasMatch()) {
      continue;
    }
    auto condition = match.captured(1);
    while (!condition.trimmed().endsWith(QLatin1Char('{')) && index + 1 < lines.size()) {
      condition += QLatin1Char(' ') + lines[++index].trimmed();
    }
    condition = condition.simplified();
    condition.remove(QRegularExpression(QStringLiteral("\\)\\s*\\{$")));
    branches.push_back(condition.toStdString());
  }
  // How each branch keeps up: "coalesced" (coalescing_drag_active names it), "cheap" (its move
  // only moves an overlay or a view: the audit's measured costs are in the doc), or "not a drag".
  const std::map<std::string, const char*> classified{
      {"processing_render_wait_active_", "not a drag"},
      {"defer_coalesced_drag_move(*event)", "the coalescing itself"},
      {"!handling_tablet_event_", "not a drag"},
      {"preview_point_drag_ >= 0 && move_preview_point_handle(event, false)", "cheap"},
      {"!brush_adjust_dragging_", "not a drag"},
      {"!preview_points_.empty()", "not a drag"},
      {"brush_adjust_dragging_", "cheap"},
      {"mouse_pen_action_button_ != Qt::NoButton", "cheap"},
      {"color_picking_", "coalesced"},
      {"paint_ctrl_click_.has_value() && !move_drag_pending_ && !moving_layer_", "not a drag"},
      {"!panning_ && (event->buttons() & Qt::RightButton) != 0 && (event->buttons() & Qt::LeftButton) == 0",
       "not a drag"},
      {"hand_zoom_pending_", "not a drag"},
      {"panning_", "coalesced"},
      {"rotating_view_", "coalesced"},
      {"transient_read_dragging_", "cheap"},
      {"dragging_text_entry_selection_", "cheap"},
      {"edit_locked_ && !zooming_ && !selection_tool_passes_edit_lock()", "not a drag"},
      {"dragging_guide_", "cheap"},
      {"tool_ != CanvasTool::Gradient && (gradient_handle_drag_.has_value() || gradient_handles_enabled()) && "
       "move_gradient_fill_handle(event)",
       "coalesced"},
      {"pen_family_tool_active()", "cheap; its path edits coalesce"},
      {"shape_temp_path_select_ && (event->buttons() & Qt::LeftButton) == 0", "not a drag"},
      {"shape_tool_active() && !drawing_shape_ && !shape_handle_drag_active()", "coalesced"},
      {"(shape_handle_drag_active() || shape_tool_active()) && !drawing_shape_ && move_shape_resize_handle(event)",
       "coalesced"},
      {"move_vector_mask_drag_", "coalesced"},
      {"tool_ == CanvasTool::PathSelect || tool_ == CanvasTool::DirectSelect", "coalesced"},
      {"dragging_warp_handle_", "renders once per painted frame (GitHub #298)"},
      {"warping_layer_", "not a drag"},
      {"dragging_transform_", "coalesced"},
      {"transforming_layer_", "not a drag"},
      {"artboard_drag_active()", "cheap"},
      {"tool_ == CanvasTool::Artboard", "not a drag"},
      {"tool_ == CanvasTool::Gradient && !dragging_guide_ && move_gradient_fill_handle(event)", "coalesced"},
      {"tool_ == CanvasTool::Ruler && !dragging_guide_", "cheap"},
      {"tool_ == CanvasTool::PerspectiveCrop && perspective_crop_session_pending()", "cheap"},
      {"tool_ == CanvasTool::Crop && crop_straightening_", "cheap"},
      {"tool_ == CanvasTool::Crop && crop_dragging_out_", "cheap"},
      {"tool_ == CanvasTool::Crop && crop_rotating_ && (event->buttons() & Qt::LeftButton) != 0", "cheap"},
      {"tool_ == CanvasTool::Crop && crop_drag_handle_ != TransformHandle::None && (event->buttons() & "
       "Qt::LeftButton) != 0",
       "cheap"},
      {"tool_ == CanvasTool::Crop && crop_session_active_", "not a drag"},
      {"move_layer_selection_gesture_", "cheap"},
      {"dragging_text_rect_", "cheap"},
      {"painting_", "coalesced, every point kept"},
      {"drawing_shape_", "cheap"},
      {"move_drag_pending_ || moving_layer_", "coalesced"},
      {"moving_selection_", "cheap"},
      {"marquee_resize_handle_ != TransformHandle::None", "cheap"},
      {"selecting_", "coalesced"},
      {"polygon_lassoing_ && document_ != nullptr", "cheap"},
      {"lassoing_ && document_ != nullptr", "cheap"},
      {"magnetic_lassoing_ && document_ != nullptr", "coalesced"},
      {"quick_selecting_", "cheap"},
      {"object_selecting_", "cheap"},
      {"spot_healing_stroke_active_", "cheap"},
      {"patch_tool_dragging_", "cheap"},
      {"red_eye_dragging_", "cheap"},
      {"zooming_ && document_ != nullptr", "coalesced while scrubbing; the zoom box is cheap"},
  };
  std::set<std::string> seen;
  for (const auto& branch : branches) {
    seen.insert(branch);
    if (classified.find(branch) == classified.end()) {
      std::printf("[INFO] unclassified mouseMoveEvent branch: if (%s). A drag whose move does real work must "
                  "coalesce (docs/interactive-previews-drag-coalescing.md, \"Rule for new drags\"); then list it "
                  "here.\n",
                  branch.c_str());
      CHECK(false);
    }
  }
  for (const auto& [branch, how] : classified) {
    if (seen.find(branch) == seen.end()) {
      std::printf("[INFO] listed branch no longer in mouseMoveEvent (update the list): if (%s) [%s]\n",
                  branch.c_str(), how);
      CHECK(false);
    }
  }
  CHECK(branches.size() >= 40U);
}

}  // namespace

std::vector<patchy::test::TestCase> drag_coalescing_tests() {
  return {
      {"ui_path_transform_drag_coalesces_queued_moves", ui_path_transform_drag_coalesces_queued_moves},
      {"ui_mixer_and_pattern_stamp_strokes_coalesce_queued_moves_and_keep_bytes",
       ui_mixer_and_pattern_stamp_strokes_coalesce_queued_moves_and_keep_bytes},
      {"ui_text_box_resize_drag_coalesces_queued_moves", ui_text_box_resize_drag_coalesces_queued_moves},
      {"ui_text_box_rotate_drag_coalesces_queued_moves", ui_text_box_rotate_drag_coalesces_queued_moves},
      {"ui_view_and_selection_drags_coalesce_queued_moves", ui_view_and_selection_drags_coalesce_queued_moves},
      {"ui_canvas_drag_branches_follow_the_wayland_drag_rule", ui_canvas_drag_branches_follow_the_wayland_drag_rule},
  };
}
