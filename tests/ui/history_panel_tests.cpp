// History panel interaction tests: per-session rebuild, click-to-jump across
// the undo/redo stacks, cap eviction, keyboard undo/redo highlight moves, and
// the preview-lock disable.

#include "ui/canvas_widget.hpp"
#include "ui/main_window.hpp"
#include "ui/theme_palette.hpp"

#include <QApplication>
#include <QColor>
#include <QDialog>
#include <QListWidget>
#include <QTest>
#include <QTimer>

#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

namespace {

using namespace patchy::test::ui;
using patchy::ui::MainWindowTestAccess;

QListWidget* require_history_list(patchy::ui::MainWindow& window) {
  auto* list = window.findChild<QListWidget*>(QStringLiteral("historyList"));
  CHECK(list != nullptr);
  return list;
}

void click_history_row(QListWidget& list, int row) {
  auto* item = list.item(row);
  CHECK(item != nullptr);
  list.scrollToItem(item);
  QTest::mouseClick(list.viewport(), Qt::LeftButton, Qt::NoModifier,
                    list.visualItemRect(item).center());
  QApplication::processEvents();
}

void fill_with(patchy::ui::MainWindow& window, patchy::ui::CanvasWidget& canvas, QColor color) {
  canvas.set_primary_color(color);
  use_solid_fill_settings(&canvas);
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
}

void ui_history_panel_lists_states_oldest_first_with_current_highlight() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* history = require_history_list(window);
  CHECK(history->isEnabled());
  // The fresh document contributes exactly one state (its creation).
  CHECK(history->count() == 1);
  CHECK(history->currentRow() == 0);

  fill_with(window, *canvas, QColor(200, 30, 30));
  fill_with(window, *canvas, QColor(30, 60, 220));
  CHECK(history->count() == 3);
  CHECK(history->currentRow() == 2);
  CHECK(history->item(2)->text().contains(QStringLiteral("Fill")));
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == 2);

  MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  // Undo moves the highlight instead of inserting an "Undo" row; the undone
  // state stays listed, dimmed with the future-state theme role.
  CHECK(history->count() == 3);
  CHECK(history->currentRow() == 1);
  CHECK(history->item(2)->foreground().color() == patchy::ui::theme().history_future_text);
  CHECK(history->item(0)->foreground().color() != patchy::ui::theme().history_future_text);
}

void ui_history_click_jumps_backward_and_forward() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* history = require_history_list(window);

  fill_with(window, *canvas, QColor(200, 30, 30));
  fill_with(window, *canvas, QColor(30, 160, 40));
  fill_with(window, *canvas, QColor(30, 60, 220));
  CHECK(history->count() == 4);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(40, 40)), QColor(30, 60, 220), 6));

  // Jump two states back in one click; the abandoned future stays listed.
  click_history_row(*history, 1);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(40, 40)), QColor(200, 30, 30), 6));
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == 1);
  CHECK(MainWindowTestAccess::active_session_redo_depth(window) == 2);
  CHECK(history->count() == 4);
  CHECK(history->currentRow() == 1);
  CHECK(history->item(3)->foreground().color() == patchy::ui::theme().history_future_text);

  // Jump forward again to the newest state.
  click_history_row(*history, 3);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(40, 40)), QColor(30, 60, 220), 6));
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == 3);
  CHECK(MainWindowTestAccess::active_session_redo_depth(window) == 0);
  CHECK(history->currentRow() == 3);

  // Clicking the current row changes nothing.
  click_history_row(*history, 3);
  CHECK(history->currentRow() == 3);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == 3);
}

void ui_history_new_edit_after_rollback_discards_future_rows() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* history = require_history_list(window);

  fill_with(window, *canvas, QColor(200, 30, 30));
  fill_with(window, *canvas, QColor(30, 160, 40));
  fill_with(window, *canvas, QColor(30, 60, 220));
  click_history_row(*history, 1);
  CHECK(MainWindowTestAccess::active_session_redo_depth(window) == 2);

  fill_with(window, *canvas, QColor(240, 240, 30));
  CHECK(MainWindowTestAccess::active_session_redo_depth(window) == 0);
  CHECK(history->count() == 3);
  CHECK(history->currentRow() == 2);
  for (int row = 0; row < history->count(); ++row) {
    CHECK(history->item(row)->foreground().color() != patchy::ui::theme().history_future_text);
  }
  CHECK(color_close(canvas_pixel(*canvas, QPoint(40, 40)), QColor(240, 240, 30), 6));
}

void ui_history_cap_eviction_keeps_rows_consistent() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* history = require_history_list(window);

  for (int index = 0; index < 45; ++index) {
    fill_with(window, *canvas,
              index % 2 == 0 ? QColor(200, 30, 30) : QColor(30, 60, 220));
  }
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == 40);
  CHECK(history->count() == 41);
  CHECK(history->currentRow() == 40);

  // The oldest surviving snapshot is the document after fill #5 (the initial
  // state and fills 1-4 were evicted); fill #5 used the even-index red.
  click_history_row(*history, 0);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == 0);
  CHECK(MainWindowTestAccess::active_session_redo_depth(window) == 40);
  CHECK(history->count() == 41);
  CHECK(history->currentRow() == 0);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(40, 40)), QColor(200, 30, 30), 6));
}

void ui_history_keyboard_undo_redo_moves_highlight() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* history = require_history_list(window);

  fill_with(window, *canvas, QColor(200, 30, 30));
  fill_with(window, *canvas, QColor(30, 60, 220));
  CHECK(history->count() == 3);
  CHECK(history->currentRow() == 2);

  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(history->count() == 3);
  CHECK(history->currentRow() == 1);

  require_action_by_text(window, QStringLiteral("Redo"))->trigger();
  QApplication::processEvents();
  CHECK(history->count() == 3);
  CHECK(history->currentRow() == 2);
}

void ui_history_panel_rebuilds_on_activation() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* history = require_history_list(window);
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  CHECK(tabs != nullptr);

  fill_with(window, *canvas, QColor(200, 30, 30));
  CHECK(history->count() == 2);

  MainWindowTestAccess::create_default_document(window);
  QApplication::processEvents();
  CHECK(tabs->count() == 2);
  CHECK(history->count() == 1);

  tabs->setCurrentIndex(0);
  QApplication::processEvents();
  CHECK(history->count() == 2);
  CHECK(history->currentRow() == 1);

  tabs->setCurrentIndex(1);
  QApplication::processEvents();
  CHECK(history->count() == 1);
  CHECK(history->currentRow() == 0);
}

void ui_history_new_document_from_state_creates_independent_session() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* history = require_history_list(window);
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  CHECK(tabs != nullptr);

  fill_with(window, *canvas, QColor(200, 30, 30));
  fill_with(window, *canvas, QColor(30, 60, 220));
  CHECK(history->count() == 3);
  const auto red_state_id = history->item(1)->data(Qt::UserRole).toLongLong();

  MainWindowTestAccess::open_history_state_as_new_document(window, red_state_id);
  QApplication::processEvents();
  CHECK(MainWindowTestAccess::session_count(window) == 2);
  CHECK(tabs->currentIndex() == 1);
  auto* spawned_canvas = require_canvas(window);
  CHECK(spawned_canvas != canvas);
  CHECK(color_close(canvas_pixel(*spawned_canvas, QPoint(40, 40)), QColor(200, 30, 30), 6));
  CHECK(history->count() == 1);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == 0);

  // Editing the spawned document must not leak into the original (the copy is
  // copy-on-write, so shared pixels detach on the first mutation).
  fill_with(window, *spawned_canvas, QColor(30, 160, 40));
  tabs->setCurrentIndex(0);
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(40, 40)), QColor(30, 60, 220), 6));
  CHECK(history->count() == 3);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == 2);

  // The original still jumps within its own history.
  click_history_row(*history, 1);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(40, 40)), QColor(200, 30, 30), 6));
}

void ui_history_clicks_blocked_during_preview_lock() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* history = require_history_list(window);
  fill_with(window, *canvas, QColor(200, 30, 30));
  CHECK(history->isEnabled());

  bool saw_disabled_history = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyFilterDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      CHECK(dialog != nullptr);
      saw_disabled_history = !history->isEnabled();
      dialog->reject();
      return;
    }
    CHECK(false);
  });
  require_action(window, "imageAdjustInvertAction")->trigger();
  CHECK(saw_disabled_history);
  CHECK(history->isEnabled());
}

}  // namespace

std::vector<patchy::test::TestCase> history_panel_tests() {
  return {
      {"ui_history_panel_lists_states_oldest_first_with_current_highlight",
       ui_history_panel_lists_states_oldest_first_with_current_highlight},
      {"ui_history_click_jumps_backward_and_forward", ui_history_click_jumps_backward_and_forward},
      {"ui_history_new_edit_after_rollback_discards_future_rows",
       ui_history_new_edit_after_rollback_discards_future_rows},
      {"ui_history_cap_eviction_keeps_rows_consistent", ui_history_cap_eviction_keeps_rows_consistent},
      {"ui_history_keyboard_undo_redo_moves_highlight", ui_history_keyboard_undo_redo_moves_highlight},
      {"ui_history_panel_rebuilds_on_activation", ui_history_panel_rebuilds_on_activation},
      {"ui_history_new_document_from_state_creates_independent_session",
       ui_history_new_document_from_state_creates_independent_session},
      {"ui_history_clicks_blocked_during_preview_lock", ui_history_clicks_blocked_during_preview_lock},
  };
}
