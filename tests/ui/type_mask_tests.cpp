// Type Mask tool (GitHub issue 670; docs/type-mask.md): typed text becomes a selection in the
// shape of the letters and no layer is made. The selection must match what a text layer's
// letters select, follow Shift / Alt / Shift+Alt like the other selection tools, cancel on Esc,
// follow the Vertical toggle, and undo in one step.
#include "core/document.hpp"
#include "core/layer.hpp"
#include "core/layer_metadata.hpp"
#include "core/pixel_buffer.hpp"
#include "test_fonts.hpp"
#include "test_harness.hpp"
#include "ui/canvas_types.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/main_window.hpp"
#include "ui_test_access.hpp"
#include "ui_test_support.hpp"

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QPushButton>
#include <QRect>
#include <QTest>
#include <QTextCursor>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <utility>

namespace {

using patchy::test::ui::process_events_for;
using patchy::test::ui::require_action;
using patchy::test::ui::require_canvas;
using patchy::test::ui::send_key;
using patchy::test::ui::send_mouse;
using patchy::ui::CanvasTool;
using patchy::ui::CanvasWidget;
using patchy::ui::MainWindow;
using patchy::ui::MainWindowTestAccess;

QTextEdit* live_editor(CanvasWidget& canvas) {
  return canvas.findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
}

// Clicks with the active type tool at `document_point` (the press carries `modifiers`) and
// returns the session's editor.
QTextEdit* open_session(CanvasWidget& canvas, QPoint document_point, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  const auto widget_point = canvas.widget_position_for_document_point(document_point);
  send_mouse(canvas, QEvent::MouseButtonPress, widget_point, Qt::LeftButton, Qt::LeftButton, modifiers);
  send_mouse(canvas, QEvent::MouseButtonRelease, widget_point, Qt::LeftButton, Qt::NoButton, modifiers);
  QApplication::processEvents();
  return live_editor(canvas);
}

// Types `text` into a fresh session, lets the preview land, and commits with Ctrl+Return.
void type_and_commit(CanvasWidget& canvas, QTextEdit& editor, const QString& text) {
  editor.selectAll();
  editor.insertPlainText(text);
  editor.moveCursor(QTextCursor::End);
  process_events_for(250);
  send_key(editor, Qt::Key_Return, Qt::ControlModifier);
  QApplication::processEvents();
  process_events_for(150);
  CHECK(live_editor(canvas) == nullptr);
}

QRect coverage_bounds(const patchy::PixelBuffer& coverage) {
  QRect bounds;
  for (int y = 0; y < coverage.height(); ++y) {
    const auto row = coverage.row(y);
    for (int x = 0; x < coverage.width(); ++x) {
      if (row[static_cast<std::size_t>(x)] >= 128U) {
        bounds = bounds.united(QRect(x, y, 1, 1));
      }
    }
  }
  return bounds;
}

std::int64_t selected_count(const patchy::PixelBuffer& coverage, const QRect& within = QRect()) {
  std::int64_t count = 0;
  for (int y = 0; y < coverage.height(); ++y) {
    const auto row = coverage.row(y);
    for (int x = 0; x < coverage.width(); ++x) {
      if (row[static_cast<std::size_t>(x)] >= 128U && (within.isNull() || within.contains(x, y))) {
        ++count;
      }
    }
  }
  return count;
}

double coverage_iou(const patchy::PixelBuffer& a, const patchy::PixelBuffer& b) {
  std::int64_t both = 0;
  std::int64_t either = 0;
  for (int y = 0; y < a.height() && y < b.height(); ++y) {
    const auto row_a = a.row(y);
    const auto row_b = b.row(y);
    for (int x = 0; x < a.width() && x < b.width(); ++x) {
      const bool in_a = row_a[static_cast<std::size_t>(x)] >= 128U;
      const bool in_b = row_b[static_cast<std::size_t>(x)] >= 128U;
      both += (in_a && in_b) ? 1 : 0;
      either += (in_a || in_b) ? 1 : 0;
    }
  }
  return either == 0 ? 0.0 : static_cast<double>(both) / static_cast<double>(either);
}

int layer_count(const std::vector<patchy::Layer>& layers) {
  int count = 0;
  for (const auto& layer : layers) {
    count += 1 + layer_count(layer.children());
  }
  return count;
}

void select_rect(MainWindow& window, CanvasWidget& canvas, const QRect& rect) {
  const auto& document = MainWindowTestAccess::document(window);
  patchy::PixelBuffer coverage(document.width(), document.height(), patchy::PixelFormat::gray8());
  for (int y = rect.top(); y <= rect.bottom(); ++y) {
    for (int x = rect.left(); x <= rect.right(); ++x) {
      *coverage.pixel(x, y) = 255U;
    }
  }
  canvas.apply_grayscale_to_selection(coverage);
}

struct TypeMaskFixture {
  patchy::test::ui::ShownWindowFixture shown;
  MainWindow& window{shown.window};
  CanvasWidget& canvas{*shown.canvas};

  TypeMaskFixture() {
    patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
    canvas.set_zoom(1.0);
    canvas.set_primary_color(QColor(20, 20, 20));
  }
  void use_tool(const char* action) {
    require_action(window, action)->trigger();
    QApplication::processEvents();
  }
};

// The commit makes a selection that matches the letters a text layer of the same text draws
// (its Ctrl+click selection), adds no layer and is one undo step.
void ui_type_mask_commit_selects_the_letters_without_a_layer() {
  TypeMaskFixture fixture;
  auto& canvas = fixture.canvas;
  auto& document = MainWindowTestAccess::document(fixture.window);
  const QPoint origin(60, 120);

  // Reference: the same text as a text layer, its letters loaded as a selection.
  fixture.use_tool("toolTypeAction");
  auto* reference_editor = open_session(canvas, origin);
  CHECK(reference_editor != nullptr);
  if (reference_editor == nullptr) {
    return;
  }
  type_and_commit(canvas, *reference_editor, QStringLiteral("Mask Hg"));
  const auto text_id = document.active_layer_id();
  CHECK(text_id.has_value());
  const auto* text_layer = text_id ? std::as_const(document).find_layer(*text_id) : nullptr;
  CHECK(text_layer != nullptr && patchy::layer_is_text(*text_layer));
  if (text_layer == nullptr) {
    return;
  }
  canvas.select_layer_opaque_pixels(*text_id);
  const auto reference = canvas.selection_as_grayscale();
  const auto reference_bounds = coverage_bounds(reference);
  CHECK(reference_bounds.width() > 40 && reference_bounds.height() > 10);
  canvas.clear_selection();
  CHECK(!canvas.has_selection());

  fixture.use_tool("toolTypeMaskAction");
  CHECK(canvas.tool() == CanvasTool::TypeMask);
  const int layers_before = layer_count(document.layers());
  const auto undo_before = MainWindowTestAccess::active_session_undo_depth(fixture.window);
  // The press lands on the text layer: Type Mask still starts new mask text there.
  auto* editor = open_session(canvas, origin);
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return;
  }
  CHECK(canvas.type_mask_overlay_active());
  type_and_commit(canvas, *editor, QStringLiteral("Mask Hg"));
  CHECK(!canvas.type_mask_overlay_active());
  CHECK(layer_count(document.layers()) == layers_before);
  CHECK(std::as_const(document).find_layer(*text_id) != nullptr);
  CHECK(canvas.has_selection());
  const auto selection = canvas.selection_as_grayscale();
  const auto bounds = coverage_bounds(selection);
  CHECK(std::abs(bounds.left() - reference_bounds.left()) <= 2);
  CHECK(std::abs(bounds.top() - reference_bounds.top()) <= 2);
  CHECK(std::abs(bounds.right() - reference_bounds.right()) <= 2);
  CHECK(std::abs(bounds.bottom() - reference_bounds.bottom()) <= 2);
  const auto iou = coverage_iou(selection, reference);
  CHECK(iou > 0.85);
  if (iou <= 0.85) {
    std::fprintf(stderr, "type mask IoU %.3f\n", iou);
  }
  CHECK(MainWindowTestAccess::active_session_undo_depth(fixture.window) == undo_before + 1U);

  // One undo step restores the selection before the commit (none).
  patchy::test::ui::require_hotkey_action(fixture.window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  CHECK(!canvas.has_selection());
  CHECK(layer_count(document.layers()) == layers_before);
}

// Shift adds the letters, Alt subtracts them, Shift+Alt intersects, as with the selection tools.
void ui_type_mask_modifiers_combine_with_the_selection() {
  TypeMaskFixture fixture;
  auto& canvas = fixture.canvas;
  fixture.use_tool("toolTypeMaskAction");
  // A box over the left half of where the letters land.
  const QRect box(40, 60, 120, 160);
  const QPoint origin(60, 120);
  const auto run = [&](Qt::KeyboardModifiers modifiers) {
    select_rect(fixture.window, canvas, box);
    auto* editor = open_session(canvas, origin, modifiers);
    CHECK(editor != nullptr);
    if (editor != nullptr) {
      type_and_commit(canvas, *editor, QStringLiteral("WWWWWW"));
    }
    return canvas.selection_as_grayscale();
  };
  const auto replaced = run(Qt::NoModifier);
  const auto letters_bounds = coverage_bounds(replaced);
  CHECK(letters_bounds.left() < box.right() && letters_bounds.right() > box.right() + 20);
  const auto letters_total = selected_count(replaced);
  const auto letters_in_box = selected_count(replaced, box);
  const auto box_area = static_cast<std::int64_t>(box.width()) * box.height();
  CHECK(letters_in_box > 0 && letters_in_box < letters_total);

  const auto added = run(Qt::ShiftModifier);
  // Within a few edge pixels: the stored selection rounds some half-covered letter edges.
  CHECK(std::llabs(selected_count(added) - (box_area + (letters_total - letters_in_box))) <= letters_total / 100);

  const auto subtracted = run(Qt::AltModifier);
  CHECK(std::llabs(selected_count(subtracted) - (box_area - letters_in_box)) <= letters_total / 100);
  CHECK(selected_count(subtracted, QRect(box.right() + 1, 0, 2000, 2000)) == 0);

  const auto intersected = run(Qt::ShiftModifier | Qt::AltModifier);
  CHECK(std::llabs(selected_count(intersected) - letters_in_box) <= letters_total / 100);
}

// Esc cancels: no selection change, no layer, no history step, even with "Use Esc to commit
// text" on. The session works with no layer selected.
void ui_type_mask_escape_cancels() {
  TypeMaskFixture fixture;
  auto& canvas = fixture.canvas;
  auto& document = MainWindowTestAccess::document(fixture.window);
  patchy::test::ui::require_hotkey_action(fixture.window, QStringLiteral("select.deselect_layers"))->trigger();
  QApplication::processEvents();
  CHECK(!document.active_layer_id().has_value());
  fixture.use_tool("toolTypeMaskAction");
  select_rect(fixture.window, canvas, QRect(10, 10, 50, 50));
  const auto before = canvas.selection_as_grayscale();
  const int layers_before = layer_count(document.layers());
  const auto undo_before = MainWindowTestAccess::active_session_undo_depth(fixture.window);
  MainWindowTestAccess::set_escape_commits_text(fixture.window, true);
  auto* editor = open_session(canvas, QPoint(80, 200));
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return;
  }
  editor->selectAll();
  editor->insertPlainText(QStringLiteral("Cancel me"));
  process_events_for(250);
  CHECK(layer_count(document.layers()) > layers_before);  // the provisional row while typing
  send_key(*editor, Qt::Key_Escape);
  QApplication::processEvents();
  process_events_for(100);
  MainWindowTestAccess::set_escape_commits_text(fixture.window, false);
  CHECK(live_editor(canvas) == nullptr);
  CHECK(!canvas.type_mask_overlay_active());
  CHECK(layer_count(document.layers()) == layers_before);
  CHECK(MainWindowTestAccess::active_session_undo_depth(fixture.window) == undo_before);
  const auto after = canvas.selection_as_grayscale();
  CHECK(std::equal(after.data().begin(), after.data().end(), before.data().begin(), before.data().end()));

  // With no layer selected the commit still makes the selection.
  auto* second = open_session(canvas, QPoint(80, 200));
  CHECK(second != nullptr);
  if (second != nullptr) {
    type_and_commit(canvas, *second, QStringLiteral("Hello"));
  }
  CHECK(layer_count(document.layers()) == layers_before);
  CHECK(coverage_bounds(canvas.selection_as_grayscale()).width() > 60);
}

// The options bar's Vertical toggle arms the next Type Mask session: its letters stack in a
// column, so the selection is taller than wide. Shift+T walks Type and Type Mask.
void ui_type_mask_vertical_toggle_and_cycle() {
  TypeMaskFixture fixture;
  auto& canvas = fixture.canvas;
  fixture.use_tool("toolTypeAction");
  QTest::keyClick(&canvas, Qt::Key_T, Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(canvas.tool() == CanvasTool::TypeMask);
  auto* type_button = fixture.window.findChild<QToolButton*>(QStringLiteral("typeToolButton"));
  CHECK(type_button != nullptr && type_button->defaultAction() == require_action(fixture.window, "toolTypeMaskAction"));
  QTest::keyClick(&canvas, Qt::Key_T, Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(canvas.tool() == CanvasTool::Text);
  fixture.use_tool("toolTypeMaskAction");

  auto* horizontal = open_session(canvas, QPoint(300, 60));
  CHECK(horizontal != nullptr);
  if (horizontal != nullptr) {
    type_and_commit(canvas, *horizontal, QStringLiteral("ABCD"));
  }
  const auto wide = coverage_bounds(canvas.selection_as_grayscale());
  CHECK(wide.width() > wide.height());

  auto* vertical_button = fixture.window.findChild<QPushButton*>(QStringLiteral("textOrientationButton"));
  CHECK(vertical_button != nullptr && vertical_button->isVisible());
  if (vertical_button == nullptr) {
    return;
  }
  vertical_button->click();
  QApplication::processEvents();
  CHECK(vertical_button->isChecked());
  auto* vertical = open_session(canvas, QPoint(300, 60));
  CHECK(vertical != nullptr);
  if (vertical != nullptr) {
    type_and_commit(canvas, *vertical, QStringLiteral("ABCD"));
  }
  const auto tall = coverage_bounds(canvas.selection_as_grayscale());
  CHECK(tall.height() > tall.width() * 2);
}

// Review captures for the issue: the Type flyout, typing over the tint, the selection.
void ui_type_mask_review_captures() {
  TypeMaskFixture fixture;
  auto& window = fixture.window;
  auto& canvas = fixture.canvas;
  auto* button = window.findChild<QToolButton*>(QStringLiteral("typeToolButton"));
  CHECK(button != nullptr && button->menu() != nullptr);
  if (button == nullptr || button->menu() == nullptr) {
    return;
  }
  fixture.use_tool("toolTypeMaskAction");
  auto* menu = button->menu();
  QTimer::singleShot(0, [&] {
    patchy::test::ui::save_window_popup_artifact("ui_type_mask_flyout", window, *button, *menu);
    menu->close();
  });
  button->showMenu();
  QApplication::processEvents();

  // Some artwork under the tint.
  auto& document = MainWindowTestAccess::document(window);
  if (const auto active = document.active_layer_id(); active.has_value()) {
    if (auto* layer = document.find_layer(*active); layer != nullptr) {
      auto& pixels = layer->pixels();
      for (int y = 0; y < pixels.height(); ++y) {
        for (int x = 0; x < pixels.width(); ++x) {
          auto* pixel = pixels.pixel(x, y);
          pixel[0] = static_cast<std::uint8_t>(60 + (x * 160) / std::max(1, pixels.width()));
          pixel[1] = static_cast<std::uint8_t>(120 + (y * 100) / std::max(1, pixels.height()));
          pixel[2] = 200U;
          pixel[3] = 255U;
        }
      }
      canvas.document_changed();
    }
  }
  auto* editor = open_session(canvas, QPoint(90, 160));
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return;
  }
  editor->selectAll();
  editor->insertPlainText(QStringLiteral("Patchy"));
  editor->moveCursor(QTextCursor::End);
  process_events_for(400);
  patchy::test::ui::save_widget_artifact("ui_type_mask_typing", window);
  send_key(*editor, Qt::Key_Return, Qt::ControlModifier);
  QApplication::processEvents();
  process_events_for(300);
  CHECK(canvas.has_selection());
  patchy::test::ui::save_widget_artifact("ui_type_mask_selection", window);
}

}  // namespace

std::vector<patchy::test::TestCase> type_mask_tests() {
  return {
      {"ui_type_mask_commit_selects_the_letters_without_a_layer",
       ui_type_mask_commit_selects_the_letters_without_a_layer},
      {"ui_type_mask_modifiers_combine_with_the_selection", ui_type_mask_modifiers_combine_with_the_selection},
      {"ui_type_mask_escape_cancels", ui_type_mask_escape_cancels},
      {"ui_type_mask_vertical_toggle_and_cycle", ui_type_mask_vertical_toggle_and_cycle},
      {"ui_type_mask_review_captures", ui_type_mask_review_captures},
  };
}
