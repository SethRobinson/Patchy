// GitHub issue 584: F1 Patchy Help, F12 File > Revert, Tab View > Hide Panels, and the
// Home/End/Page Up/Page Down view scrolling keys (docs/view-navigation.md "Keyboard
// scrolling", docs/screen-modes.md "Hide Panels").
#include "ui/canvas_widget.hpp"
#include "ui/hotkey_registry.hpp"
#include "ui/main_window.hpp"
#include "ui/panel_column.hpp"
#include "ui/qt_paths.hpp"
#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include "unicode_path_names.hpp"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QKeySequence>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QTest>
#include <QToolBar>
#include <QUrl>

#include <cmath>
#include <filesystem>

using namespace patchy::test::ui;

namespace {

using patchy::ui::CanvasWidget;
using patchy::ui::MainWindow;
using patchy::ui::MainWindowTestAccess;

bool has_shortcut(QAction* action, const QKeySequence& key) {
  return action != nullptr && action->shortcuts().contains(key);
}

void ui_issue_584_default_keys_and_patchy_help() {
  auto&& [window, canvas] = ShownWindowFixture{};
  (void)canvas;
  CHECK(has_shortcut(require_action(window, "helpPatchyHelpAction"), QKeySequence(Qt::Key_F1)));
  CHECK(has_shortcut(require_action(window, "fileRevertAction"), QKeySequence(Qt::Key_F12)));
  CHECK(has_shortcut(require_action(window, "viewHidePanelsAction"), QKeySequence(Qt::Key_Tab)));
  CHECK(has_shortcut(require_action(window, "viewScrollTopLeftAction"), QKeySequence(Qt::Key_Home)));
  CHECK(has_shortcut(require_action(window, "viewScrollBottomRightAction"), QKeySequence(Qt::Key_End)));
  CHECK(has_shortcut(require_action(window, "viewScrollPageUpAction"), QKeySequence(Qt::Key_PageUp)));
  CHECK(has_shortcut(require_action(window, "viewScrollPageDownAction"), QKeySequence(Qt::Key_PageDown)));
  CHECK(has_shortcut(require_action(window, "viewScrollPageLeftAction"), QKeySequence(Qt::SHIFT | Qt::Key_PageUp)));
  CHECK(has_shortcut(require_action(window, "viewScrollPageRightAction"), QKeySequence(Qt::SHIFT | Qt::Key_PageDown)));
  // The keys the issue asked to change stay as they are: F2 Rename Layer, F5 Force Refresh,
  // Ctrl+Y Redo.
  CHECK(has_shortcut(require_action(window, "layerRenameAction"), QKeySequence(Qt::Key_F2)));
  CHECK(has_shortcut(require_action(window, "windowForceRefreshAction"), QKeySequence(Qt::Key_F5)));
  const auto* redo = window.hotkey_registry().find_command(QStringLiteral("edit.redo"));
  CHECK(redo != nullptr && has_shortcut(redo->action.data(), QKeySequence(Qt::CTRL | Qt::Key_Y)));
  // Patchy Help opens the feature list on GitHub (read from the action, never opened here).
  auto* help = require_action(window, "helpPatchyHelpAction");
  CHECK(help != nullptr && help->property(patchy::ui::kHelpUrlProperty).toUrl() ==
                               QUrl(QStringLiteral("https://github.com/SethRobinson/Patchy/blob/main/docs/features.md")));
}

void ui_issue_584_revert_reloads_the_saved_file() {
  ensure_artifact_dir();
  const auto dir = patchy::ui::to_qstring(std::filesystem::absolute(patchy::test::unicode_artifact_dir(u8"revert")));
  const auto stem = patchy::ui::to_qstring(patchy::test::unicode_path_piece(patchy::test::kUnicodeCombinedStem));
  const auto path = QDir(dir).filePath(stem + QStringLiteral(" revert.psd"));
  {
    MainWindow writer;
    MainWindowTestAccess::create_document(writer, 32, 24);
    CHECK(MainWindowTestAccess::save_document_to_path(writer, path));
  }
  auto&& [window, untitled_canvas] = ShownWindowFixture{};
  auto* revert = require_action(window, "fileRevertAction");
  auto* new_layer = require_action(window, "layerNewAction");
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  CHECK(revert != nullptr && new_layer != nullptr && tabs != nullptr);
  if (revert == nullptr || new_layer == nullptr || tabs == nullptr) {
    return;
  }
  // An untitled document has no file to go back to.
  CHECK(MainWindowTestAccess::active_session_path(window).isEmpty());
  CHECK(!revert->isEnabled());

  MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();
  auto* file_canvas = MainWindowTestAccess::canvas(window);
  CHECK(file_canvas != untitled_canvas);
  // Another document after it, so the reverted tab must come back to its own place.
  MainWindowTestAccess::create_default_document(window);
  QApplication::processEvents();
  tabs->setCurrentWidget(file_canvas);
  QApplication::processEvents();
  CHECK(MainWindowTestAccess::canvas(window) == file_canvas);
  const auto file_index = tabs->indexOf(file_canvas);
  const auto sessions = MainWindowTestAccess::session_count(window);
  CHECK(file_index == 1 && tabs->count() == 3);
  CHECK(QFileInfo(MainWindowTestAccess::active_session_path(window)) == QFileInfo(path));
  CHECK(revert->isEnabled());
  const auto saved_layers = std::as_const(MainWindowTestAccess::document(window)).layers().size();

  new_layer->trigger();
  QApplication::processEvents();
  CHECK(MainWindowTestAccess::active_session_is_modified(window));
  CHECK(std::as_const(MainWindowTestAccess::document(window)).layers().size() == saved_layers + 1);

  const auto answer_revert = [](QMessageBox::StandardButton button) {
    when_dialog_visible(QStringLiteral("revertDocumentMessageBox"), [button](QDialog& dialog) {
      auto* box = qobject_cast<QMessageBox*>(&dialog);
      CHECK(box != nullptr && box->button(button) != nullptr);
      if (box != nullptr && box->button(button) != nullptr) {
        box->button(button)->click();
      }
    });
  };
  // Cancel keeps the edit.
  answer_revert(QMessageBox::Cancel);
  revert->trigger();
  QApplication::processEvents();
  CHECK(MainWindowTestAccess::canvas(window) == file_canvas);
  CHECK(std::as_const(MainWindowTestAccess::document(window)).layers().size() == saved_layers + 1);

  answer_revert(QMessageBox::Yes);
  revert->trigger();
  QApplication::processEvents();
  auto* reverted_canvas = MainWindowTestAccess::canvas(window);
  CHECK(reverted_canvas != file_canvas);
  CHECK(MainWindowTestAccess::session_count(window) == sessions);
  CHECK(tabs->count() == 3 && tabs->indexOf(reverted_canvas) == file_index);
  CHECK(QFileInfo(MainWindowTestAccess::active_session_path(window)) == QFileInfo(path));
  CHECK(!MainWindowTestAccess::active_session_is_modified(window));
  CHECK(std::as_const(MainWindowTestAccess::document(window)).layers().size() == saved_layers);

  // Unchanged: F12 reloads at once, without a question. The closed question box can leave
  // the offscreen window inactive, and window shortcuts match only in the active window.
  CHECK(revert->isEnabled());
  window.activateWindow();
  CHECK(process_events_until([&window] { return window.isActiveWindow(); }));
  reverted_canvas->setFocus();
  QTest::keyClick(reverted_canvas, Qt::Key_F12);
  QApplication::processEvents();
  CHECK(find_top_level_dialog(QStringLiteral("revertDocumentMessageBox")) == nullptr);
  CHECK(MainWindowTestAccess::canvas(window) != reverted_canvas);
  CHECK(tabs->indexOf(MainWindowTestAccess::canvas(window)) == file_index);

  // Back on the untitled document, Revert is off again.
  tabs->setCurrentWidget(untitled_canvas);
  QApplication::processEvents();
  CHECK(MainWindowTestAccess::canvas(window) == untitled_canvas);
  CHECK(!revert->isEnabled());
}

void ui_issue_584_tab_hides_and_restores_panels() {
  auto&& [window, canvas] = ShownWindowFixture{};
  auto* hide_panels = require_action(window, "viewHidePanelsAction");
  auto* cycle = require_action(window, "viewScreenModeCycleAction");
  auto* column = MainWindowTestAccess::panel_column(window);
  auto* tool_palette = window.findChild<QToolBar*>(QStringLiteral("toolPalette"));
  auto* options_bar = window.findChild<QToolBar*>(QStringLiteral("Options"));
  auto* zoom_edit = window.statusBar()->findChild<QLineEdit*>(QStringLiteral("statusZoomEdit"));
  CHECK(hide_panels != nullptr && cycle != nullptr && column != nullptr && tool_palette != nullptr &&
        options_bar != nullptr && zoom_edit != nullptr);
  if (hide_panels == nullptr || cycle == nullptr || column == nullptr || tool_palette == nullptr ||
      options_bar == nullptr || zoom_edit == nullptr) {
    return;
  }
  CHECK(hide_panels->isCheckable() && !hide_panels->isChecked());
  // The user's own layout: the Options bar hidden from the Window menu stays hidden after.
  options_bar->hide();
  QApplication::processEvents();
  const auto column_width = column->width();
  const bool cycle_carried_before = window.actions().contains(cycle);

  // A focused text field keeps its Tab.
  zoom_edit->setFocus();
  QApplication::processEvents();
  QTest::keyClick(zoom_edit, Qt::Key_Tab);
  QApplication::processEvents();
  CHECK(!window.panels_hidden() && tool_palette->isVisible() && column->isVisible());

  canvas->setFocus();
  QTest::keyClick(canvas, Qt::Key_Tab);
  process_events_for(50);
  CHECK(window.panels_hidden() && hide_panels->isChecked());
  CHECK(tool_palette->isHidden() && options_bar->isHidden() && column->isHidden());
  CHECK(window.menuBar()->isVisible() && window.statusBar()->isVisible());
  // The window carries the hidden bars' commands, so their keys still work.
  CHECK(window.actions().contains(cycle));
  save_widget_artifact("issue_584_hide_panels", window);

  canvas->setFocus();
  QTest::keyClick(canvas, Qt::Key_Tab);
  process_events_for(50);
  CHECK(!window.panels_hidden() && !hide_panels->isChecked());
  CHECK(tool_palette->isVisible() && options_bar->isHidden() && column->isVisible());
  CHECK(column->width() == column_width);
  CHECK(window.actions().contains(cycle) == cycle_carried_before);

  // A screen-mode change shows the panels first; Full Screen has nothing left to hide.
  hide_panels->trigger();
  CHECK(window.panels_hidden());
  window.set_screen_mode(MainWindow::ScreenMode::FullScreen);
  process_events_for(50);
  CHECK(!window.panels_hidden() && !hide_panels->isEnabled());
  window.set_screen_mode(MainWindow::ScreenMode::Standard);
  process_events_for(50);
  CHECK(hide_panels->isEnabled() && tool_palette->isVisible() && column->isVisible() && options_bar->isHidden());
  options_bar->show();
  QApplication::processEvents();
}

// The widget point where document point `document_point` lands, as floating point.
QPointF widget_point(CanvasWidget& canvas, QPoint document_point) {
  return QPointF(canvas.widget_position_for_document_point(document_point));
}

bool near(QPointF a, QPointF b) {
  return std::abs(a.x() - b.x()) <= 1.0 && std::abs(a.y() - b.y()) <= 1.0;
}

void ui_issue_584_home_end_and_page_keys_scroll_the_view() {
  auto&& [window, canvas] = ShownWindowFixture{};
  const auto& document = std::as_const(MainWindowTestAccess::document(window));
  const QPoint far_corner(document.width(), document.height());
  const auto press = [&](int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    canvas->setFocus();
    QTest::keyClick(canvas, static_cast<Qt::Key>(key), modifiers);
    QApplication::processEvents();
  };
  // A view the whole image fits in: nothing moves.
  canvas->fit_to_view();
  QApplication::processEvents();
  const auto fitted = widget_point(*canvas, QPoint());
  press(Qt::Key_Home);
  press(Qt::Key_PageDown);
  CHECK(near(widget_point(*canvas, QPoint()), fitted));

  canvas->set_view_zoom_centered(8.0);
  QApplication::processEvents();
  const auto area = canvas->view_content_rect();
  CHECK(document.width() * canvas->zoom() > area.width() * 2.0 && document.height() * canvas->zoom() > area.height() * 2.0);

  press(Qt::Key_Home);
  CHECK(near(widget_point(*canvas, QPoint()), area.topLeft()));
  // Already at the top-left: Page Up and Shift+Page Up stay at the edge.
  press(Qt::Key_PageUp);
  press(Qt::Key_PageUp, Qt::ShiftModifier);
  CHECK(near(widget_point(*canvas, QPoint()), area.topLeft()));

  press(Qt::Key_PageDown);
  CHECK(near(widget_point(*canvas, QPoint()), area.topLeft() - QPointF(0.0, area.height())));
  press(Qt::Key_PageDown, Qt::ShiftModifier);
  CHECK(near(widget_point(*canvas, QPoint()), area.topLeft() - QPointF(area.width(), area.height())));
  press(Qt::Key_PageUp);
  CHECK(near(widget_point(*canvas, QPoint()), area.topLeft() - QPointF(area.width(), 0.0)));

  press(Qt::Key_End);
  CHECK(near(widget_point(*canvas, far_corner), area.bottomRight()));
  // Paging past the far edge stops there.
  press(Qt::Key_PageDown);
  press(Qt::Key_PageDown, Qt::ShiftModifier);
  CHECK(near(widget_point(*canvas, far_corner), area.bottomRight()));
  save_widget_artifact("issue_584_end_key_view", *canvas);

  // A focused list keeps Home for itself.
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  if (layer_list != nullptr) {
    const auto before = widget_point(*canvas, QPoint());
    layer_list->setFocus();
    QApplication::processEvents();
    QTest::keyClick(layer_list, Qt::Key_Home);
    QApplication::processEvents();
    CHECK(near(widget_point(*canvas, QPoint()), before));
  }
}

}  // namespace

std::vector<patchy::test::TestCase> view_shortcut_keys_tests() {
  return {
      {"ui_issue_584_default_keys_and_patchy_help", ui_issue_584_default_keys_and_patchy_help},
      {"ui_issue_584_revert_reloads_the_saved_file", ui_issue_584_revert_reloads_the_saved_file},
      {"ui_issue_584_tab_hides_and_restores_panels", ui_issue_584_tab_hides_and_restores_panels},
      {"ui_issue_584_home_end_and_page_keys_scroll_the_view", ui_issue_584_home_end_and_page_keys_scroll_the_view},
  };
}
