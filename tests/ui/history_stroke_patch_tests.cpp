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
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
// Per-stroke history entries (core/stroke_patch.hpp, docs/performance.md
// "History"): a paint stroke retains only the pixels inside its bounds, undo
// and redo restore exactly what the whole-document snapshot path restores, a
// mask stroke patches the mask alone, patch entries mix with snapshot entries
// through undo/redo/jumps and New Document From This State, and the memory
// budget counts patch bytes.
#include "core/document.hpp"
#include "core/pixel_buffer.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/main_window.hpp"
#include <QApplication>
#include <QColor>
#include <QListWidget>
#include <QPoint>
#include <QRect>
#include <QTest>
#include <algorithm>
#include <cstdio>
#include <cstddef>
#include <utility>
#include <vector>
#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

namespace {

using namespace patchy::test::ui;
using patchy::ui::CanvasTool;
using patchy::ui::MainWindowTestAccess;

const QColor kGray(128, 128, 128, 255);

// A gray layer with a black block in its upper left, so Smudge, Clone and the
// local adjustment brushes all have an edge to work against.
patchy::Document make_document(int width, int height) {
  patchy::Document document(width, height, patchy::PixelFormat::rgba8());
  auto pixels = solid_pixels(width, height, patchy::PixelFormat::rgba8(), kGray);
  fill_pixel_rect(pixels, QRect(0, 0, width / 3, height / 2), QColor(0, 0, 0, 255));
  document.add_pixel_layer("Paint", std::move(pixels));
  return document;
}

patchy::ui::CanvasWidget* open_document(patchy::ui::MainWindow& window, int width, int height) {
  window.add_document_session(make_document(width, height), QStringLiteral("Strokes"));
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_primary_color(Qt::black);
  canvas->set_brush_size(40);
  canvas->set_brush_opacity(100);
  canvas->set_brush_softness(0);
  canvas->set_brush_build_up(false);
  QApplication::processEvents();
  return canvas;
}

const patchy::Layer& paint_layer(patchy::ui::MainWindow& window) {
  const auto& document = std::as_const(MainWindowTestAccess::document(window));
  CHECK(!document.layers().empty());
  return document.layers().front();
}

// Copies are copy-on-write, so a copy taken here keeps these bytes however the
// live layer changes afterwards.
patchy::PixelBuffer content_pixels(patchy::ui::MainWindow& window) {
  return paint_layer(window).pixels();
}

patchy::PixelBuffer mask_pixels(patchy::ui::MainWindow& window) {
  const auto& mask = paint_layer(window).mask();
  CHECK(mask.has_value());
  return mask->pixels;
}

bool pixels_equal(const patchy::PixelBuffer& a, const patchy::PixelBuffer& b) {
  if (a.width() != b.width() || a.height() != b.height() || a.format() != b.format()) {
    return false;
  }
  const auto left = a.data();
  const auto right = b.data();
  return std::equal(left.begin(), left.end(), right.begin(), right.end());
}

// A press/move/release stroke along a straight line, in document coordinates.
void stroke(patchy::ui::CanvasWidget& canvas, QPoint from, QPoint to, int steps = 6) {
  const auto at = [&canvas](QPoint document_point) {
    return canvas.widget_position_for_document_point(document_point);
  };
  send_mouse(canvas, QEvent::MouseButtonPress, at(from), Qt::LeftButton, Qt::LeftButton);
  for (int step = 1; step <= steps; ++step) {
    const QPoint point(from.x() + (to.x() - from.x()) * step / steps, from.y() + (to.y() - from.y()) * step / steps);
    send_mouse(canvas, QEvent::MouseMove, at(point), Qt::NoButton, Qt::LeftButton);
  }
  send_mouse(canvas, QEvent::MouseButtonRelease, at(to), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
}

void set_clone_source(patchy::ui::CanvasWidget& canvas, QPoint document_point) {
  const auto widget_point = canvas.widget_position_for_document_point(document_point);
  send_mouse(canvas, QEvent::MouseButtonPress, widget_point, Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
  send_mouse(canvas, QEvent::MouseButtonRelease, widget_point, Qt::LeftButton, Qt::NoButton, Qt::AltModifier);
  QApplication::processEvents();
}

void undo(patchy::ui::MainWindow& window) {
  MainWindowTestAccess::undo(window);
  QApplication::processEvents();
}

void redo(patchy::ui::MainWindow& window) {
  MainWindowTestAccess::redo(window);
  QApplication::processEvents();
}

QListWidget* require_history_list(patchy::ui::MainWindow& window) {
  auto* list = window.findChild<QListWidget*>(QStringLiteral("historyList"));
  CHECK(list != nullptr);
  return list;
}

void click_history_row(QListWidget& list, int row) {
  auto* item = list.item(row);
  CHECK(item != nullptr);
  list.scrollToItem(item);
  QTest::mouseClick(list.viewport(), Qt::LeftButton, Qt::NoModifier, list.visualItemRect(item).center());
  QApplication::processEvents();
}

// The reporter's scenario (GitHub issue 109): a 2000x2000 document. A stroke's
// history entry must cost about its bounds, not the painted layer (16 MB).
void ui_stroke_history_entry_costs_the_stroke_bounds_not_the_layer() {
  patchy::ui::MainWindow window;
  auto* canvas = open_document(window, 2000, 2000);
  canvas->set_tool(CanvasTool::Brush);
  const std::size_t layer_bytes = 2000U * 2000U * 4U;

  const auto before = MainWindowTestAccess::recompute_history_retained_bytes(window);
  stroke(*canvas, QPoint(600, 900), QPoint(1000, 900));
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == 1);
  CHECK(MainWindowTestAccess::top_undo_entry_is_stroke_patch(window));
  const auto patch_bytes = MainWindowTestAccess::top_undo_entry_patch_bytes(window);
  const auto patch_delta = MainWindowTestAccess::recompute_history_retained_bytes(window) - before;
  // A 400 px stroke of a 40 px hard brush: roughly 440 x 40 x 4 bytes, with
  // slack for stamp spacing and antialiasing. Far from the 16 MB layer.
  const std::size_t expected = 440U * 40U * 4U;
  CHECK(patch_bytes >= expected / 2);
  CHECK(patch_bytes <= expected * 3);
  CHECK(patch_delta == patch_bytes);
  CHECK(patch_delta < layer_bytes / 50);

  // The same stroke through the whole-document snapshot path retains the
  // painted layer's detached copy: the per-stroke cost the issue measured.
  MainWindowTestAccess::set_stroke_patch_history_enabled(window, false);
  const auto before_snapshot = MainWindowTestAccess::recompute_history_retained_bytes(window);
  stroke(*canvas, QPoint(600, 1100), QPoint(1000, 1100));
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == 2);
  CHECK(!MainWindowTestAccess::top_undo_entry_is_stroke_patch(window));
  const auto snapshot_delta = MainWindowTestAccess::recompute_history_retained_bytes(window) - before_snapshot;
  CHECK(snapshot_delta >= layer_bytes);
  CHECK(snapshot_delta < layer_bytes + layer_bytes / 8);
  std::printf("  history bytes per stroke on 2000x2000: snapshot %zu, patch %zu\n", snapshot_delta, patch_delta);

  // Both entries undo in order; the first stroke's patch restores exact gray.
  undo(window);
  undo(window);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == 0);
  const auto restored = content_pixels(window);
  const auto* document_pixel = restored.pixel(800, 900);
  CHECK(document_pixel[0] == 128 && document_pixel[1] == 128 && document_pixel[2] == 128 && document_pixel[3] == 255);
  // The document opens at 100%, where (800, 900) sits outside the viewport;
  // zoom out so the rendered check reads a visible pixel.
  canvas->set_zoom(0.25);
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(800, 900)), kGray, 2));
}

struct ToolRound {
  CanvasTool tool;
  const char* name;
};

// Runs one stroke of `tool` on a fresh document and checks that undo and redo
// land exactly on the captured before/after pixels. Returns the post-stroke
// pixels so the two history paths can be compared.
patchy::PixelBuffer run_tool_round(ToolRound round, bool patch_enabled) {
  patchy::ui::MainWindow window;
  MainWindowTestAccess::set_stroke_patch_history_enabled(window, patch_enabled);
  auto* canvas = open_document(window, 600, 400);
  canvas->set_tool(round.tool);
  if (round.tool == CanvasTool::Clone) {
    canvas->set_clone_aligned(false);
    set_clone_source(*canvas, QPoint(60, 60));
  }
  const auto before = content_pixels(window);
  // Across the black/gray edge at x = 200 (the block spans y < 200).
  stroke(*canvas, QPoint(140, 150), QPoint(300, 150));
  const auto after = content_pixels(window);
  CHECK(!pixels_equal(before, after));
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == 1);
  CHECK(MainWindowTestAccess::top_undo_entry_is_stroke_patch(window) == patch_enabled);

  undo(window);
  CHECK(pixels_equal(content_pixels(window), before));
  CHECK(MainWindowTestAccess::active_session_redo_depth(window) == 1);
  redo(window);
  CHECK(pixels_equal(content_pixels(window), after));
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == 1);
  // And once more, so a patch that has already captured its `after` pixels
  // keeps restoring both directions.
  undo(window);
  CHECK(pixels_equal(content_pixels(window), before));
  redo(window);
  CHECK(pixels_equal(content_pixels(window), after));
  return after;
}

void ui_stroke_history_undo_redo_match_the_snapshot_path_for_every_paint_tool() {
  const ToolRound rounds[] = {
      {CanvasTool::Brush, "Brush"},   {CanvasTool::Eraser, "Eraser"}, {CanvasTool::Smudge, "Smudge"},
      {CanvasTool::Dodge, "Dodge"},   {CanvasTool::Burn, "Burn"},     {CanvasTool::Clone, "Clone"},
      {CanvasTool::MixerBrush, "Mixer Brush"},
  };
  for (const auto& round : rounds) {
    const auto patched = run_tool_round(round, true);
    const auto snapshotted = run_tool_round(round, false);
    if (!pixels_equal(patched, snapshotted)) {
      std::printf("  %s: patch-path and snapshot-path strokes differ\n", round.name);
      CHECK(false);
    }
  }
}

void ui_stroke_history_mask_stroke_patches_only_the_mask() {
  patchy::ui::MainWindow window;
  auto* canvas = open_document(window, 400, 300);
  require_action(window, "layerAddMaskAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::Mask);
  const auto mask_depth = MainWindowTestAccess::active_session_undo_depth(window);
  const auto content_before = content_pixels(window);
  const auto mask_before = mask_pixels(window);

  canvas->set_tool(CanvasTool::Brush);
  stroke(*canvas, QPoint(100, 150), QPoint(300, 150));
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == mask_depth + 1);
  CHECK(MainWindowTestAccess::top_undo_entry_is_stroke_patch(window));
  const auto mask_after = mask_pixels(window);
  CHECK(!pixels_equal(mask_before, mask_after));
  CHECK(pixels_equal(content_pixels(window), content_before));
  // A 200 px stroke of a 40 px brush on a gray8 mask: about 240 x 40 bytes.
  const auto patch_bytes = MainWindowTestAccess::top_undo_entry_patch_bytes(window);
  CHECK(patch_bytes >= 240U * 40U / 2);
  CHECK(patch_bytes <= 240U * 40U * 3);

  undo(window);
  CHECK(pixels_equal(mask_pixels(window), mask_before));
  CHECK(pixels_equal(content_pixels(window), content_before));
  // Undoing a mask stroke keeps the mask as the edit target (GitHub issue 105).
  CHECK(canvas->layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::Mask);
  redo(window);
  CHECK(pixels_equal(mask_pixels(window), mask_after));
  CHECK(pixels_equal(content_pixels(window), content_before));
}

void ui_stroke_history_patches_mix_with_snapshots_across_jumps_and_new_documents() {
  patchy::ui::MainWindow window;
  auto* canvas = open_document(window, 400, 300);
  auto* history = require_history_list(window);
  canvas->set_tool(CanvasTool::Brush);

  const auto state0 = content_pixels(window);
  stroke(*canvas, QPoint(60, 240), QPoint(340, 240));
  const auto state1 = content_pixels(window);
  CHECK(MainWindowTestAccess::top_undo_entry_is_stroke_patch(window));
  CHECK(history->count() == 2);
  CHECK(history->item(1)->text() == QStringLiteral("Brush stroke"));

  // A whole-document command between two strokes.
  canvas->set_primary_color(QColor(30, 60, 220));
  use_solid_fill_settings(canvas);
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  const auto state2 = content_pixels(window);
  CHECK(!MainWindowTestAccess::top_undo_entry_is_stroke_patch(window));

  canvas->set_primary_color(Qt::black);
  stroke(*canvas, QPoint(60, 60), QPoint(340, 60));
  const auto state3 = content_pixels(window);
  CHECK(MainWindowTestAccess::top_undo_entry_is_stroke_patch(window));
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == 3);
  CHECK(history->count() == 4);

  // Step back and forward through the mixed stack.
  undo(window);
  CHECK(pixels_equal(content_pixels(window), state2));
  undo(window);
  CHECK(pixels_equal(content_pixels(window), state1));
  undo(window);
  CHECK(pixels_equal(content_pixels(window), state0));
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == 0);
  redo(window);
  CHECK(pixels_equal(content_pixels(window), state1));
  redo(window);
  CHECK(pixels_equal(content_pixels(window), state2));
  redo(window);
  CHECK(pixels_equal(content_pixels(window), state3));
  CHECK(MainWindowTestAccess::active_session_redo_depth(window) == 0);

  // History-panel jumps rotate several entries at once in both directions.
  click_history_row(*history, 0);
  CHECK(pixels_equal(content_pixels(window), state0));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(200, 240)), kGray, 2));
  click_history_row(*history, 3);
  CHECK(pixels_equal(content_pixels(window), state3));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(200, 60)), Qt::black, 2));
  click_history_row(*history, 1);
  CHECK(pixels_equal(content_pixels(window), state1));
  click_history_row(*history, 2);
  CHECK(pixels_equal(content_pixels(window), state2));
  click_history_row(*history, 3);
  CHECK(pixels_equal(content_pixels(window), state3));

  // New Document From This State materializes patch rows: row 0 sits under a
  // patch entry whose base is the Fill snapshot, row 2 under a patch whose
  // base is the live document.
  MainWindowTestAccess::open_history_state_as_new_document(window, history->item(0)->data(Qt::UserRole).toLongLong());
  QApplication::processEvents();
  CHECK(MainWindowTestAccess::session_count(window) == 2);
  CHECK(pixels_equal(content_pixels(window), state0));
  MainWindowTestAccess::activate_session(window, 0);
  QApplication::processEvents();
  MainWindowTestAccess::open_history_state_as_new_document(window, history->item(2)->data(Qt::UserRole).toLongLong());
  QApplication::processEvents();
  CHECK(MainWindowTestAccess::session_count(window) == 3);
  CHECK(pixels_equal(content_pixels(window), state2));
  // The original is untouched by either spawn.
  MainWindowTestAccess::activate_session(window, 0);
  QApplication::processEvents();
  CHECK(pixels_equal(content_pixels(window), state3));
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == 3);
}

// Patch bytes count toward the history budget; a zero budget evicts patch
// entries from the front down to the floor and the survivors still undo.
void ui_stroke_history_patch_bytes_count_toward_the_budget() {
  qputenv("PATCHY_HISTORY_BUDGET_TEST_MB", "0");
  struct Restore {
    ~Restore() { qunsetenv("PATCHY_HISTORY_BUDGET_TEST_MB"); }
  } restore;
  patchy::ui::MainWindow window;
  auto* canvas = open_document(window, 400, 300);
  canvas->set_tool(CanvasTool::Brush);
  std::vector<patchy::PixelBuffer> states;
  states.push_back(content_pixels(window));
  for (int index = 0; index < 5; ++index) {
    stroke(*canvas, QPoint(40, 40 + index * 50), QPoint(360, 40 + index * 50));
    states.push_back(content_pixels(window));
    CHECK(MainWindowTestAccess::top_undo_entry_is_stroke_patch(window));
  }
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == 3);
  CHECK(MainWindowTestAccess::recompute_history_retained_bytes(window) > 0);
  undo(window);
  CHECK(pixels_equal(content_pixels(window), states[4]));
  undo(window);
  CHECK(pixels_equal(content_pixels(window), states[3]));
  undo(window);
  CHECK(pixels_equal(content_pixels(window), states[2]));
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == 0);
  redo(window);
  CHECK(pixels_equal(content_pixels(window), states[3]));
}

}  // namespace

std::vector<patchy::test::TestCase> history_stroke_patch_tests() {
  return {
      {"ui_stroke_history_entry_costs_the_stroke_bounds_not_the_layer",
       ui_stroke_history_entry_costs_the_stroke_bounds_not_the_layer},
      {"ui_stroke_history_undo_redo_match_the_snapshot_path_for_every_paint_tool",
       ui_stroke_history_undo_redo_match_the_snapshot_path_for_every_paint_tool},
      {"ui_stroke_history_mask_stroke_patches_only_the_mask", ui_stroke_history_mask_stroke_patches_only_the_mask},
      {"ui_stroke_history_patches_mix_with_snapshots_across_jumps_and_new_documents",
       ui_stroke_history_patches_mix_with_snapshots_across_jumps_and_new_documents},
      {"ui_stroke_history_patch_bytes_count_toward_the_budget",
       ui_stroke_history_patch_bytes_count_toward_the_budget},
  };
}
