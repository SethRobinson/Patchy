#include <QClipboard>
#include <QScrollBar>
#include <QStatusBar>

#include "ui/canvas_widget.hpp"
#include "ui/recent_file_list.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/image_document_io.hpp"
#include "ui/main_window.hpp"
#include "ui/tool_palette_footer.hpp"
#include "ui/start_panel.hpp"
#include "ui/icon_theme.hpp"
#include "ui/app_settings.hpp"
#include "ui/build_info.hpp"
#include "ui/network_mounts.hpp"
#include "synthetic_dng.hpp"
#include "test_harness.hpp"
#include <patchy_version.hpp>
#include <QAbstractItemModel>
#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMetaObject>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QIODevice>
#include <QThread>
#include <QPushButton>
#include <QSpinBox>
#include <QStringList>
#include <QTabWidget>
#include <QTest>
#include <QTimer>
#include <QWidgetAction>
#include <QAbstractButton>
#include <QTabBar>
#include <QSplitter>
#include <QToolBar>
#include <QPainter>
#include <QPixmap>
#include <QToolButton>
#include <QWidget>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <array>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <map>
#include <vector>
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include "app_shell_test_support.hpp"

namespace {
using namespace patchy::test::ui::app_shell_support;

struct ToolPaletteOverflowSetup {
  QToolBar* palette{nullptr};
  QPushButton* foreground{nullptr};
  QPushButton* background{nullptr};
  QToolButton* quick_mask{nullptr};
  QToolButton* extension{nullptr};
  int fits_height{0};
};

// Looks up the tool palette widgets, verifies everything fits at a tall
// height, then shrinks the window to a ~40px palette shortage so the tail of
// the bottom cluster overflows into the extension button.
ToolPaletteOverflowSetup shrink_window_until_tool_palette_overflows(patchy::ui::MainWindow& window);

// A recent-folder pick opens the Open dialog on a later event-loop turn (after the menu
// closes), so a one-shot timer queued beside it can fire first and leave the modal
// waiting forever. This polls until the dialog is actually showing, then closes it, and
// returns the directory it opened in (empty when it never appeared).
QString answer_deferred_open_dialog(const std::function<void()>& trigger);

ToolPaletteOverflowSetup shrink_window_until_tool_palette_overflows(patchy::ui::MainWindow& window) {
  ToolPaletteOverflowSetup setup;
  setup.palette = window.findChild<QToolBar*>(QStringLiteral("toolPalette"));
  CHECK(setup.palette != nullptr);
  setup.foreground = window.findChild<QPushButton*>(QStringLiteral("foregroundColorButton"));
  setup.background = window.findChild<QPushButton*>(QStringLiteral("backgroundColorButton"));
  setup.quick_mask = window.findChild<QToolButton*>(QStringLiteral("quickMaskButton"));
  setup.extension = setup.palette->findChild<QToolButton*>(QStringLiteral("qt_toolbar_ext_button"));
  CHECK(setup.foreground != nullptr);
  CHECK(setup.background != nullptr);
  CHECK(setup.quick_mask != nullptr);
  CHECK(setup.extension != nullptr);

  // The chrome above and below the palette (menu bar, options bar, status bar)
  // keeps its height across window resizes, so palette height tracks window
  // height 1:1 and the fitting window height can be derived from the hint.
  const int chrome_height = window.height() - setup.palette->height();
  const int fits_height = setup.palette->sizeHint().height() + chrome_height;
  setup.fits_height = fits_height;

  window.resize(window.width(), fits_height + 24);
  QApplication::processEvents();
  process_events_for(60);
  CHECK(setup.quick_mask->isVisible());
  CHECK(!setup.extension->isVisible());
  // The footer sits at the bottom of the bar, under the tools, never inside the
  // toolbar layout.
  auto* footer = setup.palette->findChild<QWidget*>(QStringLiteral("toolPaletteFooter"));
  CHECK(footer != nullptr);
  if (footer != nullptr) {
    CHECK(footer->isVisible());
    CHECK(footer->geometry().bottom() >= setup.palette->height() - 6);
    CHECK(footer->geometry().top() > setup.palette->widgetForAction(require_action(window, "toolZoomAction"))
                                         ->geometry()
                                         .bottom());
  }

  const int short_height = fits_height - 40;
  CHECK(short_height > 0);
  window.resize(window.width(), short_height);
  QApplication::processEvents();
  process_events_for(60);
  // A minimum-size clamp refusing the resize would make the overflow
  // assertions vacuous, so pin the height that was actually applied.
  CHECK(window.height() == short_height);
  CHECK(setup.extension->isVisible());
  return setup;
}

QString answer_deferred_open_dialog(const std::function<void()>& trigger) {
  QString opened_in;
  bool answered = false;
  QTimer poll;
  poll.setInterval(10);
  QObject::connect(&poll, &QTimer::timeout, [&] {
    auto* dialog = qobject_cast<QFileDialog*>(find_top_level_dialog(QStringLiteral("openFileDialog")));
    if (dialog == nullptr || !dialog->isVisible()) {
      return;
    }
    poll.stop();
    opened_in = QFileInfo(dialog->directory().absolutePath()).absoluteFilePath();
    answered = true;
    dialog->reject();
  });
  poll.start();
  trigger();
  process_events_until([&] { return answered; }, 10000);
  poll.stop();
  return opened_in;
}

// A QFont holds its size in points OR pixels and the unused accessor returns -1.
// macOS resolves inherited widget fonts by pixel size, so the natural
// `f.setPointSizeF(f.pointSizeF() * k)` silently left every derived font at full
// size there (and spammed "Point size <= 0" through the offscreen suite). The
// pixel-defined cases below are the macOS condition, reproduced on any platform.
void ui_derived_font_sizes_scale_in_points_and_pixels() {
  QFont point_font;
  point_font.setPointSizeF(20.0);
  CHECK(std::abs(patchy::ui::scaled_font(point_font, 0.85).pointSizeF() - 17.0) < 0.0001);
  CHECK(patchy::ui::scaled_font(point_font, 0.85).pixelSize() == -1);
  CHECK(std::abs(patchy::ui::offset_font(point_font, -1, false).pointSizeF() - 19.0) < 0.0001);

  QFont pixel_font;
  pixel_font.setPixelSize(20);
  // The bug: this must shrink, not stay 20 and not become a negative point size.
  CHECK(patchy::ui::scaled_font(pixel_font, 0.85).pixelSize() == 17);
  CHECK(patchy::ui::scaled_font(pixel_font, 0.85).pointSizeF() == -1.0);
  CHECK(patchy::ui::scaled_font(pixel_font, 1.9).pixelSize() == 38);
  CHECK(patchy::ui::offset_font(pixel_font, -1, false).pixelSize() == 19);

  // Floors keep a derived size positive rather than clamping a real one.
  CHECK(patchy::ui::scaled_font(pixel_font, 0.001).pixelSize() == 1);
  QFont small_point_font;
  small_point_font.setPointSizeF(8.0);
  CHECK(patchy::ui::offset_font(small_point_font, -4, false).pointSizeF() >= 7.0);

  // Bold rides along with offset_font; scaling never touches weight.
  CHECK(patchy::ui::offset_font(point_font, 0, true).bold());
  CHECK(!patchy::ui::scaled_font(point_font, 2.0).bold());

  // A non-positive or non-finite scale is refused rather than corrupting the font.
  QFont untouched = point_font;
  patchy::ui::scale_font_size(untouched, 0.0);
  CHECK(std::abs(untouched.pointSizeF() - 20.0) < 0.0001);
  patchy::ui::scale_font_size(untouched, std::numeric_limits<double>::quiet_NaN());
  CHECK(std::abs(untouched.pointSizeF() - 20.0) < 0.0001);
}

void ui_startup_opens_empty_workspace_with_start_panel() {
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("newDocument"));
  }
  patchy::ui::MainWindow window;
  show_window_empty(window);

  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  auto* panel = window.findChild<QWidget*>(QStringLiteral("startPanel"));
  auto* new_button = window.findChild<QPushButton*>(QStringLiteral("startPanelNewButton"));
  auto* open_button = window.findChild<QPushButton*>(QStringLiteral("startPanelOpenButton"));
  auto* info = window.findChild<QLabel*>(QStringLiteral("documentInfoLabel"));
  CHECK(tabs != nullptr);
  CHECK(panel != nullptr);
  CHECK(new_button != nullptr);
  CHECK(open_button != nullptr);
  CHECK(info != nullptr);

  // Startup: no auto-created document, the start panel fills the tab area.
  CHECK(tabs->count() == 0);
  CHECK(patchy::ui::MainWindowTestAccess::session_count(window) == 0);
  CHECK(panel->isVisible());
  CHECK(panel->size() == tabs->size());
  CHECK(info->text() == QStringLiteral("No document"));
  CHECK(require_action(window, "fileNewAction")->isEnabled());
  CHECK(require_action(window, "fileOpenAction")->isEnabled());
  CHECK(!require_action(window, "fileSaveAction")->isEnabled());
  CHECK(!require_action(window, "filePlaceEmbeddedAction")->isEnabled());
  save_widget_artifact("ui_start_panel", window);

  // The New Document button runs the regular dialog flow and the panel hides.
  accept_new_document_dialog(400, 300);
  new_button->click();
  QApplication::processEvents();
  CHECK(tabs->count() == 1);
  CHECK(!panel->isVisible());
  CHECK(info->text().contains(QStringLiteral("400 x 300 px")));

  // Closing the last document brings the panel back.
  CHECK(QMetaObject::invokeMethod(tabs, "tabCloseRequested", Qt::DirectConnection, Q_ARG(int, 0)));
  QApplication::processEvents();
  CHECK(tabs->count() == 0);
  CHECK(panel->isVisible());
}

void ui_start_panel_recent_files_open_on_click() {
  ensure_artifact_dir();
  const auto recent_path = QFileInfo(QStringLiteral("test-artifacts/start_panel_recent.png")).absoluteFilePath();
  {
    QImage recent_image(48, 32, QImage::Format_RGB32);
    recent_image.fill(QColor(90, 150, 210));
    CHECK(recent_image.save(recent_path));
    auto settings = patchy::ui::app_settings();
    // A dead entry ahead of the live one: the panel list filters to existing files.
    settings.setValue(QStringLiteral("recentFiles"),
                      QStringList{QStringLiteral("Z:/definitely/missing/file.png"), recent_path});
    settings.sync();
  }

  patchy::ui::MainWindow window;
  show_window_empty(window);
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  auto* panel = window.findChild<QWidget*>(QStringLiteral("startPanel"));
  auto* recent_list = window.findChild<QListWidget*>(QStringLiteral("startPanelRecentList"));
  auto* info = window.findChild<QLabel*>(QStringLiteral("documentInfoLabel"));
  CHECK(tabs != nullptr);
  CHECK(panel != nullptr);
  CHECK(recent_list != nullptr);
  CHECK(info != nullptr);
  CHECK(panel->isVisible());
  CHECK(recent_list->isVisible());
  // The dead entry drops once the background existence check reports back.
  CHECK(process_events_until([&] { return recent_list->count() == 1; }));
  CHECK(recent_list->item(0)->text() == QStringLiteral("start_panel_recent.png"));

  auto* layers = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layers != nullptr);
  int layer_rebuilds = 0;
  QObject::connect(layers->model(), &QAbstractItemModel::modelReset, &window, [&] { ++layer_rebuilds; });

  const auto row_center = recent_list->visualItemRect(recent_list->item(0)).center();
  send_mouse(*recent_list->viewport(), QEvent::MouseButtonPress, row_center, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*recent_list->viewport(), QEvent::MouseButtonRelease, row_center, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(tabs->count() == 1);
  CHECK(!panel->isVisible());
  CHECK(info->text().contains(QStringLiteral("48 x 32 px")));
  CHECK(layer_rebuilds == 1);
}

void ui_large_document_session_keeps_loading_responsive() {
  patchy::ui::MainWindow window;
  show_window_empty(window);
  auto* panel = window.findChild<QWidget*>(QStringLiteral("startPanel"));
  auto* layers = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layers != nullptr);
  CHECK(panel != nullptr && panel->isVisible());

  patchy::Document document(96, 96, patchy::PixelFormat::rgba8());
  constexpr int row_count = 2048;
  for (int index = 0; index < row_count; ++index) {
    document.add_layer(patchy::Layer(document.allocate_layer_id(), "Layer " + std::to_string(index),
                                    solid_pixels(1, 1, patchy::PixelFormat::rgba8(), QColor(80, 140, 210))));
  }
  EnvironmentVariableRestorer restore_delay("PATCHY_PROCESSING_RENDER_TEST_DELAY_MS");
  qputenv("PATCHY_PROCESSING_RENDER_TEST_DELAY_MS", QByteArray("2000"));

  int rebuilds = 0;
  int partial_row_ticks = 0;
  bool welcome_over_canvas = false;
  bool canvas_unlocked = false;
  QImage first_spinner;
  QImage changed_spinner;
  QImage loading_window;
  QObject::connect(layers->model(), &QAbstractItemModel::modelReset, &window, [&] { ++rebuilds; });
  QTimer heartbeat;
  heartbeat.setInterval(20);
  QObject::connect(&heartbeat, &QTimer::timeout, &window, [&] {
    auto* canvas = window.findChild<patchy::ui::CanvasWidget*>();
    if (canvas == nullptr || layers->count() != row_count ||
        layers->itemWidget(layers->item(0)) == nullptr ||
        layers->itemWidget(layers->item(row_count - 1)) != nullptr) {
      return;
    }
    ++partial_row_ticks;
    welcome_over_canvas = welcome_over_canvas || panel->isVisible();
    canvas_unlocked = canvas_unlocked || !canvas->edit_locked();
    if (!canvas->render_settled() && changed_spinner.isNull()) {
      const auto badge = canvas->grab().toImage().copy((canvas->width() - 168) / 2, 12, 168, 50);
      if (first_spinner.isNull()) {
        first_spinner = badge;
      } else if (!images_equal_rgba(first_spinner, badge)) {
        changed_spinner = badge;
        loading_window = window.grab().toImage();
      }
    }
  });
  heartbeat.start();
  window.add_document_session(std::move(document), QStringLiteral("Many layers"));
  heartbeat.stop();

  auto* canvas = window.findChild<patchy::ui::CanvasWidget*>();
  CHECK(canvas != nullptr);
  CHECK(rebuilds == 1);
  CHECK(partial_row_ticks >= 2);
  CHECK(!welcome_over_canvas);
  CHECK(!canvas_unlocked);
  CHECK(!first_spinner.isNull() && !changed_spinner.isNull());
  ensure_artifact_dir();
  CHECK(loading_window.save(QStringLiteral("test-artifacts/ui_large_document_session_loading.png")));
  CHECK(!canvas->edit_locked());
  CHECK(!panel->isVisible());
  CHECK(layers->count() == row_count);
  for (int row = 0; row < row_count; ++row) {
    CHECK(layers->itemWidget(layers->item(row)) != nullptr);
  }
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
  while (!canvas->render_settled() && std::chrono::steady_clock::now() < deadline) {
    QApplication::processEvents();
    QThread::msleep(5);
  }
  CHECK(canvas->render_settled());
  save_widget_artifact("ui_large_document_session_loaded", window);
}

void ui_start_panel_recent_files_scroll_and_context_menu() {
  ensure_artifact_dir();
  constexpr int kSeededFiles = 14;  // more than the eight rows the box shows at once
  QStringList recent_files;
  for (int index = 0; index < kSeededFiles; ++index) {
    const auto path =
        QFileInfo(QStringLiteral("test-artifacts/start_panel_scroll_%1.png").arg(index, 2, 10, QLatin1Char('0')))
            .absoluteFilePath();
    QImage image(16, 16, QImage::Format_RGB32);
    image.fill(QColor(40 + index * 8, 120, 200));
    CHECK(image.save(path));
    recent_files << path;
  }
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("recentFiles"), recent_files);
    settings.sync();
  }

  patchy::ui::MainWindow window;
  show_window_empty(window);
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  auto* recent_list = window.findChild<QListWidget*>(QStringLiteral("startPanelRecentList"));
  CHECK(tabs != nullptr);
  CHECK(recent_list != nullptr);
  CHECK(recent_list->isVisible());

  // A window with no room for eight rows shrinks the box and still scrolls, rather
  // than pushing the panel footer off screen.
  CHECK(recent_list->count() == kSeededFiles);
  CHECK(recent_list->height() < 8 * 40 + 14);
  CHECK(recent_list->height() >= 4 * 40);
  CHECK(recent_list->verticalScrollBar()->maximum() > 0);

  // Tall enough for the whole panel, so the box reaches its eight-row cap instead.
  window.resize(1180, 1000);
  QApplication::processEvents();

  // Every remembered file is a row, but the box stops at eight and scrolls for
  // the rest. Before this cap the list took QAbstractScrollArea's default hint
  // and silently clipped everything past the fourth row.
  CHECK(recent_list->count() == kSeededFiles);
  CHECK(recent_list->sizeHint().height() == 8 * 40 + 14);
  CHECK(recent_list->height() == 8 * 40 + 14);
  CHECK(recent_list->verticalScrollBar()->maximum() > 0);
  CHECK(recent_list->verticalScrollBar()->isVisible());
  CHECK(recent_list->horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff);
  save_widget_artifact("ui_start_panel_recent_scroll", window);

  recent_list->verticalScrollBar()->setValue(recent_list->verticalScrollBar()->maximum());
  QApplication::processEvents();
  CHECK(recent_list->visualItemRect(recent_list->item(kSeededFiles - 1)).intersects(recent_list->viewport()->rect()));

  // Right-click a row: the start panel gets the same menu the Open Recent menu builds.
  QApplication::clipboard()->clear();
  bool saw_context_menu = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* menu = qobject_cast<QMenu*>(widget);
      if (menu == nullptr || menu->objectName() != QStringLiteral("recentFileContextMenu")) {
        continue;
      }
      auto* copy_action = find_menu_action_by_text(*menu, QStringLiteral("Copy File Path"));
      auto* explorer_action = find_menu_action_by_text(*menu, QStringLiteral("Open in File Explorer"));
      CHECK(copy_action != nullptr);
      CHECK(explorer_action != nullptr);
      CHECK(copy_action->objectName() == QStringLiteral("recentFileCopyPathAction"));
      CHECK(explorer_action->objectName() == QStringLiteral("recentFileOpenInExplorerAction"));
      copy_action->trigger();
      menu->close();
      saw_context_menu = true;
      return;
    }
    CHECK(false);
  });

  auto* target_item = recent_list->item(kSeededFiles - 1);
  const auto context_point = recent_list->visualItemRect(target_item).center();
  QContextMenuEvent context_event(QContextMenuEvent::Mouse, context_point,
                                  recent_list->viewport()->mapToGlobal(context_point));
  QApplication::sendEvent(recent_list->viewport(), &context_event);
  QApplication::processEvents();

  CHECK(saw_context_menu);
  CHECK(QApplication::clipboard()->text() == QDir::toNativeSeparators(recent_files.last()));
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("File path copied"));
  // A right-click must never also open the row the way a left-click does.
  CHECK(tabs->count() == 0);
}

// A hovered recent row offers a remove (x) button, and the row's context menu a
// Remove from Recent Files item; either drops only that entry from the stored
// list and never opens it (GitHub issue 157).
void ui_start_panel_recent_row_remove_button_and_menu() {
  ensure_artifact_dir();
  QStringList recent_files;
  for (int index = 0; index < 3; ++index) {
    const auto path =
        QFileInfo(QStringLiteral("test-artifacts/start_panel_remove_%1.png").arg(index)).absoluteFilePath();
    QImage image(16, 16, QImage::Format_RGB32);
    image.fill(QColor(60 + index * 40, 90, 160));
    CHECK(image.save(path));
    recent_files << path;
  }
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("recentFiles"), recent_files);
    settings.sync();
  }
  const auto stored_recent_files = [] {
    auto settings = patchy::ui::app_settings();
    settings.sync();
    return settings.value(QStringLiteral("recentFiles")).toStringList();
  };

  patchy::ui::MainWindow window;
  show_window_empty(window);
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  auto* recent_list = dynamic_cast<patchy::ui::RecentFileList*>(
      window.findChild<QListWidget*>(QStringLiteral("startPanelRecentList")));
  CHECK(tabs != nullptr && recent_list != nullptr);
  CHECK(recent_list->has_remove_button());
  CHECK(recent_list->count() == 3);

  // Hovering the button lights it; the row's own click area is untouched.
  const auto button_center = [recent_list](int row) {
    return patchy::ui::RecentFileList::remove_button_rect(recent_list->visualItemRect(recent_list->item(row)))
        .center();
  };
  send_mouse(*recent_list->viewport(), QEvent::MouseMove, button_center(1), Qt::NoButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(recent_list->remove_button_hovered(1));
  CHECK(!recent_list->remove_button_hovered(0));
  save_widget_artifact("ui_start_panel_recent_remove_hover", window);

  // A press that leaves the button before release removes nothing.
  send_mouse(*recent_list->viewport(), QEvent::MouseButtonPress, button_center(1), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*recent_list->viewport(), QEvent::MouseButtonRelease, recent_list->visualItemRect(recent_list->item(1)).topLeft() + QPoint(12, 8),
             Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(recent_list->count() == 3);
  CHECK(tabs->count() == 0);

  // A click on the button removes that row only and opens nothing.
  send_mouse(*recent_list->viewport(), QEvent::MouseButtonPress, button_center(1), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*recent_list->viewport(), QEvent::MouseButtonRelease, button_center(1), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  QApplication::processEvents();
  CHECK(tabs->count() == 0);
  CHECK(recent_list->count() == 2);
  CHECK(!stored_recent_files().contains(recent_files[1]));
  CHECK(stored_recent_files().contains(recent_files[0]));
  CHECK(stored_recent_files().contains(recent_files[2]));

  // The context menu's Remove item does the same.
  bool saw_remove = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* menu = qobject_cast<QMenu*>(widget);
      if (menu == nullptr || menu->objectName() != QStringLiteral("recentFileContextMenu")) {
        continue;
      }
      auto* remove_action = find_menu_action_by_text(*menu, QStringLiteral("Remove from Recent Files"));
      CHECK(remove_action != nullptr);
      if (remove_action != nullptr) {
        CHECK(remove_action->objectName() == QStringLiteral("recentFileRemoveAction"));
        remove_action->trigger();
        saw_remove = true;
      }
      menu->close();
      return;
    }
  });
  const auto context_point = recent_list->visualItemRect(recent_list->item(0)).center();
  QContextMenuEvent context_event(QContextMenuEvent::Mouse, context_point,
                                  recent_list->viewport()->mapToGlobal(context_point));
  QApplication::sendEvent(recent_list->viewport(), &context_event);
  QApplication::processEvents();
  QApplication::processEvents();
  CHECK(saw_remove);
  CHECK(tabs->count() == 0);
  CHECK(stored_recent_files() == QStringList{recent_files[2]});
  CHECK(recent_list->count() == 1);
  CHECK(recent_list->item(0)->data(patchy::ui::kRecentPathRole).toString() == recent_files[2]);

  // File > Open Recent File rows carry the same x. The menu stays as it is
  // (the list refills in place) and rebuilds on the next File menu open.
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("recentFiles"), recent_files);
    settings.sync();
  }
  auto* recent_menu = window.findChild<QMenu*>(QStringLiteral("fileOpenRecentMenu"));
  CHECK(recent_menu != nullptr);
  auto* file_menu = qobject_cast<QMenu*>(recent_menu->parentWidget());
  CHECK(file_menu != nullptr);
  QMetaObject::invokeMethod(file_menu, "aboutToShow");
  QApplication::processEvents();
  CHECK(recent_menu_paths(*recent_menu) == recent_files);
  auto* menu_list = dynamic_cast<patchy::ui::RecentFileList*>(
      recent_menu->findChild<QListWidget*>(QStringLiteral("fileOpenRecentList")));
  CHECK(menu_list != nullptr && menu_list->has_remove_button());
  menu_list->resize(menu_list->sizeHint());
  const auto menu_button = patchy::ui::RecentFileList::remove_button_rect(
                               menu_list->visualItemRect(menu_list->item(0)))
                               .center();
  send_mouse(*menu_list->viewport(), QEvent::MouseMove, menu_button, Qt::NoButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(menu_list->remove_button_hovered(0));
  save_widget_artifact("ui_open_recent_menu_remove_hover", *menu_list);
  send_mouse(*menu_list->viewport(), QEvent::MouseButtonPress, menu_button, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*menu_list->viewport(), QEvent::MouseButtonRelease, menu_button, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  QApplication::processEvents();
  CHECK(tabs->count() == 0);
  CHECK(!stored_recent_files().contains(recent_files[0]));
  CHECK(menu_list->count() == 2);  // the same list widget, refilled
  CHECK(recent_menu->findChild<QListWidget*>(QStringLiteral("fileOpenRecentList")) == menu_list);
  QMetaObject::invokeMethod(file_menu, "aboutToShow");
  QApplication::processEvents();
  CHECK(recent_menu_paths(*recent_menu) == (QStringList{recent_files[1], recent_files[2]}));

  // Open Recent Folder shares the list and its x.
  const auto folder_a = QFileInfo(QStringLiteral("test-artifacts")).absoluteFilePath();
  const auto folder_b = QFileInfo(QStringLiteral(".")).absoluteFilePath();
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("recentFolders"), QStringList{folder_a, folder_b});
    settings.sync();
  }
  QMetaObject::invokeMethod(file_menu, "aboutToShow");
  QApplication::processEvents();
  auto* folder_list = dynamic_cast<patchy::ui::RecentFileList*>(
      window.findChild<QListWidget*>(QStringLiteral("fileOpenRecentFolderList")));
  CHECK(folder_list != nullptr && folder_list->has_remove_button() && folder_list->count() == 2);
  folder_list->resize(folder_list->sizeHint());
  const auto folder_button = patchy::ui::RecentFileList::remove_button_rect(
                                 folder_list->visualItemRect(folder_list->item(0)))
                                 .center();
  send_mouse(*folder_list->viewport(), QEvent::MouseButtonPress, folder_button, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*folder_list->viewport(), QEvent::MouseButtonRelease, folder_button, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  QApplication::processEvents();
  CHECK(folder_list->count() == 1);
  auto settings = patchy::ui::app_settings();
  settings.sync();
  CHECK(settings.value(QStringLiteral("recentFolders")).toStringList() == QStringList{folder_b});
}

// Seth's case: the Open Recent list's x keeps the list where it was, as the start panel's
// does. The scroll position stays and the highlight moves to the entry that took the
// removed row, or to the new last row when the bottom entry went.
void ui_open_recent_menu_remove_keeps_place() {
  ensure_artifact_dir();
  QStringList recent_files;
  for (int index = 0; index < 20; ++index) {
    recent_files << QFileInfo(QStringLiteral("test-artifacts/open_recent_keep_place_%1.png").arg(index))
                        .absoluteFilePath();
    QImage image(8, 8, QImage::Format_RGB32);
    image.fill(QColor(40 + index * 10, 90, 160));
    CHECK(image.save(recent_files.last()));
  }
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("recentFiles"), recent_files);
    settings.sync();
  }
  patchy::ui::MainWindow window;
  show_window_empty(window);
  auto* recent_menu = window.findChild<QMenu*>(QStringLiteral("fileOpenRecentMenu"));
  CHECK(recent_menu != nullptr);
  if (recent_menu == nullptr) {
    return;
  }
  auto* file_menu = qobject_cast<QMenu*>(recent_menu->parentWidget());
  CHECK(file_menu != nullptr);
  QMetaObject::invokeMethod(file_menu, "aboutToShow");
  QApplication::processEvents();
  auto* list = dynamic_cast<patchy::ui::RecentFileList*>(
      recent_menu->findChild<QListWidget*>(QStringLiteral("fileOpenRecentList")));
  CHECK(list != nullptr && list->count() == 20);
  if (list == nullptr) {
    return;
  }
  list->resize(list->sizeHint());
  QApplication::processEvents();
  auto* scroll_bar = list->verticalScrollBar();
  CHECK(scroll_bar->maximum() > 0);
  const auto click_remove = [list](int row) {
    const auto button =
        patchy::ui::RecentFileList::remove_button_rect(list->visualItemRect(list->item(row))).center();
    send_mouse(*list->viewport(), QEvent::MouseMove, button, Qt::NoButton, Qt::NoButton);
    send_mouse(*list->viewport(), QEvent::MouseButtonPress, button, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*list->viewport(), QEvent::MouseButtonRelease, button, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    QApplication::processEvents();
  };
  const auto row_path = [list](int row) { return list->item(row)->data(patchy::ui::kRecentPathRole).toString(); };

  // Scrolled down a few rows, a middle entry goes.
  const auto row_height = list->visualItemRect(list->item(0)).height();
  scroll_bar->setValue(row_height * 4);
  QApplication::processEvents();
  const auto scrolled = scroll_bar->value();
  CHECK(scrolled > 0);
  click_remove(8);
  CHECK(list->count() == 19);
  CHECK(row_path(8) == recent_files[9]);
  CHECK(list->currentRow() == 8);
  if (scroll_bar->value() != scrolled) {
    std::printf("scroll after remove %d, before %d\n", scroll_bar->value(), scrolled);
  }
  CHECK(scroll_bar->value() == scrolled);

  // The bottom entry goes: the highlight lands on the new last row.
  scroll_bar->setValue(scroll_bar->maximum());
  QApplication::processEvents();
  click_remove(list->count() - 1);
  CHECK(list->count() == 18);
  CHECK(list->currentRow() == 17);
  CHECK(row_path(17) == recent_files[18]);
  save_widget_artifact("ui_open_recent_menu_remove_keeps_place", *list);
}

// With no document open (the Home screen), every menu-bar command that needs a
// document is disabled: Image > Crop to Selection (Advanced) and Layer > Edit
// Adjustment crashed there, and Layer > Smart Objects stayed enabled (GitHub
// issue 162). Only commands that work without a document stay live.
void ui_empty_workspace_disables_document_commands() {
  patchy::ui::MainWindow window;
  show_window_empty(window);
  const auto command_id = [&window](const QAction* action) {
    for (const auto& command : window.hotkey_registry().commands()) {
      if (command.action == action) {
        return command.id;
      }
    }
    return QString();
  };
  QStringList enabled;
  std::function<void(QMenu*, const QString&)> walk = [&](QMenu* menu, const QString& path) {
    QMetaObject::invokeMethod(menu, "aboutToShow");  // dynamic menus fill on show
    QApplication::processEvents();
    for (auto* action : menu->actions()) {
      if (action->isSeparator() || qobject_cast<QWidgetAction*>(action) != nullptr || !action->isVisible()) {
        continue;
      }
      const auto label = path + QStringLiteral(" > ") + action->text().remove(QLatin1Char('&'));
      if (action->menu() != nullptr) {
        walk(action->menu(), label);
        continue;
      }
      if (action->isEnabled() && !patchy::ui::command_works_without_document(*action, command_id(action))) {
        enabled << QStringLiteral("%1 [%2 / %3]").arg(label, command_id(action), action->objectName());
      }
    }
  };
  for (auto* top : window.menuBar()->actions()) {
    if (top->menu() != nullptr) {
      walk(top->menu(), top->text().remove(QLatin1Char('&')));
    }
  }
  for (const auto& line : enabled) {
    std::printf("  enabled without a document: %s\n", line.toUtf8().constData());
  }
  CHECK(enabled.isEmpty());
}

void ui_start_panel_recent_filter_narrows_rows_and_opens_match() {
  ensure_artifact_dir();
  // An empty kRecentPathRole path marks the no-match placeholder (ui/recent_file_list.hpp).
  using patchy::ui::kRecentPathRole;
  QStringList recent_files;
  for (const auto* name : {"start_panel_filter_alpha", "start_panel_filter_beta", "start_panel_filter_gamma"}) {
    const auto path =
        QFileInfo(QStringLiteral("test-artifacts/%1.png").arg(QLatin1String(name))).absoluteFilePath();
    QImage image(32, 24, QImage::Format_RGB32);
    image.fill(QColor(70, 130, 180));
    CHECK(image.save(path));
    recent_files << path;
  }

  SettingsValueRestorer recent_files_restorer(QStringLiteral("recentFiles"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("recentFiles"), recent_files);
    settings.sync();
  }

  patchy::ui::MainWindow window;
  show_window_empty(window);
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  auto* panel = window.findChild<QWidget*>(QStringLiteral("startPanel"));
  auto* recent_list = window.findChild<QListWidget*>(QStringLiteral("startPanelRecentList"));
  auto* filter_edit = window.findChild<QLineEdit*>(QStringLiteral("startPanelRecentFilterEdit"));
  CHECK(tabs != nullptr);
  CHECK(panel != nullptr);
  CHECK(recent_list != nullptr);
  CHECK(filter_edit != nullptr);
  CHECK(filter_edit->isVisible());
  CHECK(filter_edit->text().isEmpty());
  CHECK(recent_list->count() == 3);
  // The panel opens with the filter focused, so a name can be typed straight away.
  CHECK(window.focusWidget() == filter_edit);

  filter_edit->setText(QStringLiteral("beta"));
  QApplication::processEvents();
  CHECK(recent_list->count() == 1);
  CHECK(recent_list->item(0)->text() == QStringLiteral("start_panel_filter_beta.png"));

  // The clear button sits centered inside the field. The application QToolButton
  // rule's 26 px minimum used to be applied to it as a real minimum size, which
  // pushed the x below the 24 px edit's bottom border.
  // Qt names the clear action, not the button, so this finds it by type.
  auto* clear_button = filter_edit->findChild<QToolButton*>();
  CHECK(clear_button != nullptr);
  CHECK(clear_button->isVisible());
  CHECK(clear_button->height() <= filter_edit->height());
  CHECK(clear_button->geometry().right() < filter_edit->width());
  CHECK(std::abs(clear_button->geometry().center().y() - filter_edit->rect().center().y()) <= 1);

  // A bare folder name no longer matches (GitHub issue 158: a folder called JPG made
  // "jpg" match everything); a token with a slash reaches the folder path.
  filter_edit->setText(QStringLiteral("test-artifacts"));
  QApplication::processEvents();
  CHECK(recent_list->count() == 1);  // the placeholder row only
  filter_edit->setText(QStringLiteral("test-artifacts/"));
  QApplication::processEvents();
  CHECK(recent_list->count() == 3);

  // Nothing matching keeps the box in place with one dimmed placeholder row that
  // opens no document when clicked.
  filter_edit->setText(QStringLiteral("no-such-recent-file"));
  QApplication::processEvents();
  CHECK(recent_list->isVisible());
  CHECK(recent_list->count() == 1);
  CHECK(recent_list->item(0)->data(kRecentPathRole).toString().isEmpty());
  // The clear button fades in on a property animation, so the artifact needs the
  // event loop to run before it shows the x in its final place.
  process_events_for(250);
  save_widget_artifact("ui_start_panel_recent_filter", window);
  const auto placeholder_center = recent_list->visualItemRect(recent_list->item(0)).center();
  send_mouse(*recent_list->viewport(), QEvent::MouseButtonPress, placeholder_center, Qt::LeftButton,
             Qt::LeftButton);
  send_mouse(*recent_list->viewport(), QEvent::MouseButtonRelease, placeholder_center, Qt::LeftButton,
             Qt::NoButton);
  QApplication::processEvents();
  CHECK(tabs->count() == 0);
  CHECK(panel->isVisible());

  // Enter opens the top match without touching the mouse.
  filter_edit->setText(QStringLiteral("gamma"));
  QApplication::processEvents();
  CHECK(recent_list->count() == 1);
  send_key(*filter_edit, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(tabs->count() == 1);
  CHECK(!panel->isVisible());
  CHECK(tabs->tabText(0) == QStringLiteral("start_panel_filter_gamma.png"));

  // The filter starts clean every time the panel comes back.
  CHECK(QMetaObject::invokeMethod(tabs, "tabCloseRequested", Qt::DirectConnection, Q_ARG(int, 0)));
  QApplication::processEvents();
  CHECK(panel->isVisible());
  CHECK(filter_edit->text().isEmpty());
  CHECK(recent_list->count() == 3);
}

void ui_start_panel_shows_about_info_and_update_status() {
  patchy::ui::MainWindow window;
  show_window_empty(window);

  auto* panel = window.findChild<patchy::ui::StartPanel*>(QStringLiteral("startPanel"));
  CHECK(panel != nullptr);
  CHECK(panel->isVisible());

  // The about branding lives on the panel now: tagline, version, credit, and links.
  auto* tagline = window.findChild<QLabel*>(QStringLiteral("startPanelTagline"));
  CHECK(tagline != nullptr);
  CHECK(tagline->text() == QStringLiteral("Open source photo editing. Free forever, no subscriptions."));
  auto* version = window.findChild<QLabel*>(QStringLiteral("startPanelVersion"));
  CHECK(version != nullptr);
  // The number comes from the configure-time generated patchy_version.hpp
  // (cmake/patchy_version.hpp.in), so the label must show the configured
  // version verbatim: an empty or placeholder value means the header broke.
  CHECK(version->text() == QStringLiteral("Version %1 (built %2)")
                               .arg(QStringLiteral(PATCHY_VERSION), patchy::ui::build_timestamp_text()));
  CHECK(!QStringLiteral(PATCHY_VERSION).isEmpty());
  CHECK(QStringLiteral(PATCHY_VERSION) != QStringLiteral("0.0.0"));
  auto* credit = window.findChild<QLabel*>(QStringLiteral("startPanelCredit"));
  CHECK(credit != nullptr);
  CHECK(credit->textFormat() == Qt::RichText);
  CHECK(credit->openExternalLinks());
  CHECK(credit->text().startsWith(QStringLiteral("Created by ")));
  CHECK(credit->text().contains(QStringLiteral("href=\"https://github.com/SethRobinson\"")));
  CHECK(credit->text().contains(QStringLiteral(">Seth A. Robinson</a>")));
  CHECK(!credit->text().contains(QStringLiteral("@link_text")));
  // The contributor credits belong to the About dialog alone; the start panel
  // keeps its footer short so the recent-files list gets the room.
  CHECK(window.findChild<QLabel*>(QStringLiteral("startPanelContributors")) == nullptr);

  const auto link_labels = panel->findChildren<QLabel*>(QStringLiteral("startPanelHome"));
  CHECK(link_labels.size() == 2);
  QString combined_text;
  for (const auto* label : link_labels) {
    CHECK(label->textFormat() == Qt::RichText);
    CHECK(label->textInteractionFlags().testFlag(Qt::LinksAccessibleByMouse));
    CHECK(label->openExternalLinks());
    combined_text += label->text();
    combined_text += QLatin1Char('\n');
  }
  CHECK(combined_text.contains(QStringLiteral("href=\"https://github.com/SethRobinson/Patchy\"")));
  CHECK(combined_text.contains(QStringLiteral(">SethRobinson/Patchy</a>")));
  CHECK(combined_text.contains(QStringLiteral("href=\"https://rtsoft.com\"")));
  CHECK(combined_text.contains(QStringLiteral(">rtsoft.com</a>")));

  // The update-status footer line stays hidden until a status is pushed and
  // hides again when cleared.
  auto* status = window.findChild<QLabel*>(QStringLiteral("startPanelUpdateStatus"));
  CHECK(status != nullptr);
  CHECK(!status->isVisible());
  panel->set_update_status(QStringLiteral("Update available: Patchy 9.99."));
  QApplication::processEvents();
  CHECK(status->isVisible());
  CHECK(status->text() == QStringLiteral("Update available: Patchy 9.99."));
  save_widget_artifact("ui_start_panel_about_info", window);
  panel->set_update_status(QString());
  CHECK(!status->isVisible());
}

// GitHub issue 163: the Window menu lists every side panel and both toolbars as checkable
// entries, alphabetically, with the document-window commands under Window > Arrange.
// Picking a closed, background-tab or collapsed panel brings it forward expanded; picking
// one already in front closes it.
// A checked Window menu entry hides its panel even while the panel is collapsed
// (GitHub issue 234); choosing it again shows the panel, expanded.
void ui_window_menu_hides_collapsed_panel() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* column = patchy::ui::MainWindowTestAccess::panel_column(window);
  auto* window_menu = window.findChild<QMenu*>(QStringLiteral("windowMenu"));
  auto* palette_toggle = window.findChild<QToolButton*>(QStringLiteral("paletteDockCollapseButton"));
  auto* palette_entry = require_action(window, "windowPanelPaletteAction");
  CHECK(column != nullptr && window_menu != nullptr && palette_toggle != nullptr);
  if (column == nullptr || window_menu == nullptr || palette_toggle == nullptr) {
    return;
  }
  const auto palette = QStringLiteral("paletteDock");
  if (!column->panel_expanded(palette)) {
    palette_toggle->click();
    QApplication::processEvents();
  }
  CHECK(column->panel_expanded(palette));
  palette_toggle->click();
  QApplication::processEvents();
  CHECK(column->panel_visible(palette));
  CHECK(!column->panel_expanded(palette));
  emit window_menu->aboutToShow();
  QApplication::processEvents();
  CHECK(palette_entry->isChecked());

  palette_entry->trigger();
  QApplication::processEvents();
  CHECK(!column->panel_visible(palette));
  CHECK(column->section_widget(palette)->isHidden());
  CHECK(!palette_entry->isChecked());

  palette_entry->trigger();
  QApplication::processEvents();
  CHECK(column->panel_visible(palette));
  CHECK(column->section_widget(palette)->isVisible());
  CHECK(column->panel_expanded(palette));
  CHECK(palette_toggle->isChecked());
  CHECK(palette_entry->isChecked());
}

void ui_window_menu_lists_panels_and_toolbars() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* column = patchy::ui::MainWindowTestAccess::panel_column(window);
  auto* window_menu = window.findChild<QMenu*>(QStringLiteral("windowMenu"));
  auto* arrange_menu = window.findChild<QMenu*>(QStringLiteral("windowArrangeMenu"));
  CHECK(column != nullptr && window_menu != nullptr && arrange_menu != nullptr);
  if (column == nullptr || window_menu == nullptr || arrange_menu == nullptr) {
    return;
  }
  CHECK(window_menu->actions().contains(arrange_menu->menuAction()));
  for (const char* name : {"windowFloatDocumentAction", "windowDockDocumentAction", "windowFloatAllAction",
                           "windowConsolidateTabsAction", "windowTileAction", "windowCascadeAction"}) {
    CHECK(arrange_menu->actions().contains(require_action(window, name)));
    CHECK(!window_menu->actions().contains(require_action(window, name)));
  }

  emit window_menu->aboutToShow();
  QApplication::processEvents();
  QStringList entry_texts;
  for (auto* action : window_menu->actions()) {
    if (action->isCheckable() && !action->objectName().startsWith(QStringLiteral("windowDocument"))) {
      entry_texts << action->text().remove(QLatin1Char('&'));
      CHECK(action->isChecked());
    }
  }
  CHECK(entry_texts == QStringList({QStringLiteral("Channels"), QStringLiteral("History"), QStringLiteral("Info"),
                                    QStringLiteral("Layers"), QStringLiteral("Options"), QStringLiteral("Palette"),
                                    QStringLiteral("Paths"), QStringLiteral("Properties"),
                                    QStringLiteral("Tool Palette")}));

  const auto history = QStringLiteral("historyDock");
  auto* history_toggle = window.findChild<QToolButton*>(QStringLiteral("historyDockCollapseButton"));
  auto* history_entry = require_action(window, "windowPanelHistoryAction");
  CHECK(history_toggle != nullptr);
  if (history_toggle == nullptr) {
    return;
  }
  // In front and expanded: the entry closes it, and the next pick brings it back.
  // A collapsed panel closes the same way (ui_window_menu_hides_collapsed_panel).
  history_toggle->setChecked(true);
  QApplication::processEvents();
  CHECK(column->panel_expanded(history));
  history_entry->trigger();
  QApplication::processEvents();
  CHECK(!column->panel_visible(history));
  CHECK(column->section_widget(history)->isHidden());
  CHECK(!history_entry->isChecked());
  history_entry->trigger();
  QApplication::processEvents();
  CHECK(column->panel_visible(history) && column->section_widget(history)->isVisible());
  CHECK(history_entry->isChecked());

  // Behind another tab of its group: the entry brings its tab forward instead of closing it.
  const auto layers = QStringLiteral("layersDock");
  const auto channels = QStringLiteral("channelsDock");
  CHECK(column->panel_in_front(layers) && !column->panel_in_front(channels));
  require_action(window, "windowPanelChannelsAction")->trigger();
  QApplication::processEvents();
  CHECK(column->panel_in_front(channels));
  CHECK(column->panel_visible(layers));
  require_action(window, "windowPanelLayersAction")->trigger();
  QApplication::processEvents();
  CHECK(column->panel_in_front(layers));
  CHECK(column->panel_visible(channels));

  // The toolbars keep Qt's own toggle.
  auto* tool_palette = window.findChild<QToolBar*>(QStringLiteral("toolPalette"));
  CHECK(tool_palette != nullptr);
  if (tool_palette != nullptr) {
    CHECK(window_menu->actions().contains(tool_palette->toggleViewAction()));
    tool_palette->toggleViewAction()->trigger();
    QApplication::processEvents();
    CHECK(tool_palette->isHidden());
    tool_palette->toggleViewAction()->trigger();
    QApplication::processEvents();
    CHECK(!tool_palette->isHidden());
  }

  // Captures for review: test-artifacts/menu_windowMenu.png and menu_windowArrangeMenu.png.
  for (auto* menu : {window_menu, arrange_menu}) {
    emit window_menu->aboutToShow();
    menu->popup(window.mapToGlobal(QPoint(40, 40)));
    QApplication::processEvents();
    save_widget_artifact("menu_" + menu->objectName().toStdString(), *menu);
    menu->hide();
  }
}

// Closing a panel and opening it again leaves every section at the column's full width,
// stacked in one column with nothing beside it. Under the old dock column a reopened dock
// could come back narrower, with the window background showing beside its title.
void ui_panel_close_and_reopen_keeps_full_width() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* column = patchy::ui::MainWindowTestAccess::panel_column(window);
  CHECK(column != nullptr);
  if (column == nullptr) {
    return;
  }
  const auto check_full_width = [&](const QString& context) {
    auto* splitter = column->splitter();
    for (int index = 0; index < splitter->count(); ++index) {
      auto* section = splitter->widget(index);
      if (section->isHidden()) {
        continue;
      }
      if (section->x() != 0 || section->width() != splitter->width()) {
        std::printf("%s: section %s at x %d width %d, column %d\n", qPrintable(context),
                    qPrintable(section->objectName()), section->x(), section->width(), splitter->width());
      }
      CHECK(section->x() == 0);
      CHECK(section->width() == splitter->width());
    }
  };
  for (const auto& id : column->panel_ids()) {
    patchy::ui::MainWindowTestAccess::show_panel(window, id);
    QApplication::processEvents();
    column->set_panel_visible(id, false);
    QApplication::processEvents();
    check_full_width(QStringLiteral("closed ") + id);
    patchy::ui::MainWindowTestAccess::show_panel(window, id);
    QApplication::processEvents();
    QApplication::processEvents();
    CHECK(column->panel_visible(id) && column->panel_in_front(id) && column->panel_expanded(id));
    check_full_width(QStringLiteral("reopened ") + id);
  }
  save_widget_artifact("ui_panel_close_and_reopen_keeps_full_width", *column);
}

// Seth's case: expanding a panel and collapsing it again (or closing it) must hand its
// height back to the other panels at once, never leave a band between the title rows.
void ui_panel_collapse_and_close_leave_no_gap() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* column = patchy::ui::MainWindowTestAccess::panel_column(window);
  CHECK(column != nullptr);
  if (column == nullptr) {
    return;
  }
  auto* splitter = column->splitter();
  const auto check_filled = [&](const QString& context) {
    QApplication::processEvents();
    QApplication::processEvents();
    int expected_top = 0;
    int last_bottom = -1;
    for (int index = 0; index < splitter->count(); ++index) {
      auto* section = splitter->widget(index);
      if (section->isHidden()) {
        continue;
      }
      if (section->y() != expected_top) {
        std::printf("%s: section %s at y %d, expected %d\n", qPrintable(context),
                    qPrintable(section->objectName()), section->y(), expected_top);
      }
      CHECK(section->y() == expected_top);
      expected_top = section->geometry().bottom() + 1 + splitter->handleWidth();
      last_bottom = section->geometry().bottom();
    }
    if (last_bottom != splitter->height() - 1) {
      std::printf("%s: last section ends at %d, column height %d\n", qPrintable(context), last_bottom,
                  splitter->height());
    }
    CHECK(last_bottom == splitter->height() - 1);
  };
  check_filled(QStringLiteral("start"));
  for (const auto& id : {QStringLiteral("propertiesDock"), QStringLiteral("historyDock"),
                         QStringLiteral("infoDock"), QStringLiteral("paletteDock")}) {
    auto* toggle = column->collapse_button(id);
    CHECK(toggle != nullptr);
    if (toggle == nullptr) {
      continue;
    }
    if (column->panel_expanded(id)) {
      toggle->click();
      check_filled(QStringLiteral("collapsed ") + id);
    }
    toggle->click();
    CHECK(column->panel_expanded(id));
    check_filled(QStringLiteral("expanded ") + id);
    toggle->click();
    CHECK(!column->panel_expanded(id));
    check_filled(QStringLiteral("collapsed again ") + id);
    toggle->click();
    column->set_panel_visible(id, false);
    check_filled(QStringLiteral("closed expanded ") + id);
    patchy::ui::MainWindowTestAccess::show_panel(window, id);
    check_filled(QStringLiteral("reopened ") + id);
    column->collapse_button(id)->click();
    check_filled(QStringLiteral("collapsed after reopen ") + id);
  }
  save_widget_artifact("ui_panel_collapse_and_close_leave_no_gap", *column);
}

// Dragging a title row up or down moves its section in the column (the tab group by its
// title row or its tab bar); a drop line shows where it lands, Escape cancels, and the
// moved sections keep the column filled.
void ui_panel_title_drag_reorders_sections() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* column = patchy::ui::MainWindowTestAccess::panel_column(window);
  CHECK(column != nullptr);
  if (column == nullptr) {
    return;
  }
  auto* splitter = column->splitter();
  const QStringList defaults = {QStringLiteral("layersDock"), QStringLiteral("historyDock"),
                                QStringLiteral("propertiesDock"), QStringLiteral("infoDock"),
                                QStringLiteral("paletteDock")};
  CHECK(column->section_order() == defaults);
  const auto check_filled = [&](const char* context) {
    int expected_top = 0;
    int last_bottom = -1;
    for (int index = 0; index < splitter->count(); ++index) {
      auto* section = splitter->widget(index);
      if (section->isHidden()) {
        continue;
      }
      if (section->y() != expected_top) {
        std::printf("%s: section %s at y %d, expected %d\n", context, qPrintable(section->objectName()), section->y(),
                    expected_top);
      }
      CHECK(section->y() == expected_top);
      expected_top = section->geometry().bottom() + 1 + splitter->handleWidth();
      last_bottom = section->geometry().bottom();
    }
    CHECK(last_bottom == splitter->height() - 1);
  };
  const auto header_of = [&window](const char* prefix) {
    return window.findChild<QWidget*>(QLatin1String(prefix) + QStringLiteral("DockTitle"));
  };
  // Presses `handle` at `from`, crosses the drag distance, then moves to the column point
  // `to` (and releases unless told not to).
  const auto drag = [&](QWidget* handle, QPoint from, QPoint column_to, bool release) {
    send_mouse(*handle, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*handle, QEvent::MouseMove, from + QPoint(0, QApplication::startDragDistance() + 2), Qt::NoButton,
               Qt::LeftButton);
    const auto to = handle->mapFromGlobal(column->mapToGlobal(column_to));
    send_mouse(*handle, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
    QApplication::processEvents();
    if (release) {
      send_mouse(*handle, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
      QApplication::processEvents();
      QApplication::processEvents();
    }
  };

  // Info dragged by its title text (which passes the mouse up to the row, as a real click
  // does) above the tab group: Info moves to the top, keeping its height.
  auto* info_header = column->title_label(QStringLiteral("infoDock"));
  CHECK(info_header != nullptr);
  const auto info_height = column->section_widget(QStringLiteral("infoDock"))->height();
  drag(info_header, QPoint(40, info_header->height() / 2), QPoint(20, 2), false);
  auto* indicator = column->findChild<QWidget*>(QStringLiteral("panelDropIndicator"));
  CHECK(indicator != nullptr && indicator->isVisible());
  if (indicator != nullptr) {
    if (indicator->y() > 2) {
      std::printf("drop line at y %d, height %d, first section at y %d\n", indicator->y(), indicator->height(),
                  splitter->widget(0)->mapTo(column, QPoint(0, 0)).y());
    }
    CHECK(indicator->y() <= 2);
  }
  save_widget_artifact("ui_panel_title_drag_reorders_sections_drop_line", *column);
  send_mouse(*info_header, QEvent::MouseButtonRelease, info_header->mapFromGlobal(column->mapToGlobal(QPoint(20, 2))),
             Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  QApplication::processEvents();
  CHECK(!indicator->isVisible());
  CHECK(column->section_order() == (QStringList{QStringLiteral("infoDock"), QStringLiteral("layersDock"),
                                                QStringLiteral("historyDock"), QStringLiteral("propertiesDock"),
                                                QStringLiteral("paletteDock")}));
  CHECK(splitter->widget(0)->objectName() == QStringLiteral("infoDock"));
  CHECK(column->section_widget(QStringLiteral("infoDock"))->height() == info_height);
  check_filled("info to top");

  // Escape cancels a drag: nothing moves.
  auto* properties_header = header_of("properties");
  const auto before_escape = column->section_order();
  drag(properties_header, QPoint(40, properties_header->height() / 2), QPoint(20, column->height() - 2), false);
  CHECK(indicator->isVisible());
  QTest::keyClick(&window, Qt::Key_Escape);
  CHECK(!indicator->isVisible());
  send_mouse(*properties_header, QEvent::MouseButtonRelease, QPoint(40, 4), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(column->section_order() == before_escape);

  // A plain click on a title row moves nothing.
  send_mouse(*properties_header, QEvent::MouseButtonPress, QPoint(40, 4), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*properties_header, QEvent::MouseButtonRelease, QPoint(40, 4), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(column->section_order() == before_escape);

  // The tab group dragged by its tab bar to the bottom; a press there still picks a tab.
  auto* tabs = column->tab_bar(QStringLiteral("layersDock"));
  CHECK(tabs != nullptr);
  const auto channels_tab = tabs->tabRect(1).center();
  drag(tabs, channels_tab, QPoint(20, column->height() - 2), true);
  CHECK(column->section_order().last() == QStringLiteral("layersDock"));
  CHECK(splitter->widget(splitter->count() - 1)->objectName() == QStringLiteral("panelTabGroup"));
  CHECK(column->panel_in_front(QStringLiteral("channelsDock")));
  check_filled("group to bottom");

  // move_section and set_section_order do the same without a mouse.
  column->move_section(QStringLiteral("paletteDock"), 0);
  CHECK(column->section_order().first() == QStringLiteral("paletteDock"));
  column->set_section_order(defaults);
  QApplication::processEvents();
  CHECK(column->section_order() == defaults);
  check_filled("defaults again");
}

// Right-clicking a panel's title row opens the panel menu (collapse or expand, close, the
// panel toggles and Reset Panels); on the tab group's tab bar the close entry closes the
// whole group.
void ui_panel_header_right_click_opens_panel_menu() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* column = patchy::ui::MainWindowTestAccess::panel_column(window);
  CHECK(column != nullptr);
  if (column == nullptr) {
    return;
  }
  // Opens the menu on `target`, checks it and triggers the entry named `trigger` (or
  // closes the menu when empty).
  const auto right_click = [&](QWidget* target, const QString& expected_close, const QString& trigger) {
    bool saw_menu = false;
    QTimer::singleShot(0, [&] {
      auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
      CHECK(menu != nullptr);
      if (menu == nullptr) {
        return;
      }
      saw_menu = true;
      CHECK(menu->objectName() == QStringLiteral("panelHeaderMenu"));
      auto* close = menu->findChild<QAction*>(QStringLiteral("panelHeaderCloseAction"));
      CHECK(close != nullptr && close->text() == expected_close);
      CHECK(menu->findChild<QAction*>(QStringLiteral("panelHeaderCollapseAction")) != nullptr);
      CHECK(menu->actions().contains(require_action(window, "windowResetPanelsAction")));
      CHECK(menu->actions().contains(require_action(window, "windowPanelPaletteAction")));
      QAction* chosen = trigger.isEmpty() ? nullptr : menu->findChild<QAction*>(trigger);
      menu->close();
      if (chosen != nullptr) {
        chosen->trigger();
      }
    });
    const QPoint position(target->width() / 2, target->height() / 2);
    QContextMenuEvent event(QContextMenuEvent::Mouse, position, target->mapToGlobal(position));
    QApplication::sendEvent(target, &event);
    QApplication::processEvents();
    CHECK(saw_menu);
  };
  for (const char* prefix : {"layers", "channels", "paths", "history", "properties", "info", "palette"}) {
    auto* header = window.findChild<QWidget*>(QLatin1String(prefix) + QStringLiteral("DockTitle"));
    CHECK(header != nullptr);
    if (header == nullptr) {
      continue;
    }
    const auto id = QLatin1String(prefix) + QStringLiteral("Dock");
    patchy::ui::MainWindowTestAccess::show_panel(window, id);
    QApplication::processEvents();
    right_click(header, QStringLiteral("Close Panel"), QString());
  }
  // Close Panel from History's title row closes History only.
  patchy::ui::MainWindowTestAccess::show_panel(window, QStringLiteral("historyDock"));
  QApplication::processEvents();
  right_click(window.findChild<QWidget*>(QStringLiteral("historyDockTitle")), QStringLiteral("Close Panel"),
              QStringLiteral("panelHeaderCloseAction"));
  CHECK(!column->panel_visible(QStringLiteral("historyDock")));
  CHECK(column->panel_visible(QStringLiteral("propertiesDock")));
  // Collapse from the Properties title row.
  patchy::ui::MainWindowTestAccess::show_panel(window, QStringLiteral("propertiesDock"));
  QApplication::processEvents();
  right_click(window.findChild<QWidget*>(QStringLiteral("propertiesDockTitle")), QStringLiteral("Close Panel"),
              QStringLiteral("panelHeaderCollapseAction"));
  CHECK(!column->panel_expanded(QStringLiteral("propertiesDock")));
  // The tab bar's menu closes the whole group.
  auto* tab_bar = column->tab_bar(QStringLiteral("layersDock"));
  CHECK(tab_bar != nullptr);
  if (tab_bar != nullptr) {
    right_click(tab_bar, QStringLiteral("Close Tab Group"), QStringLiteral("panelHeaderCloseAction"));
    for (const auto& id : {QStringLiteral("layersDock"), QStringLiteral("channelsDock"), QStringLiteral("pathsDock")}) {
      CHECK(!column->panel_visible(id));
    }
    CHECK(column->section_widget(QStringLiteral("layersDock"))->isHidden());
  }
}

// The saved panel layout: closed panels and toolbars, collapsed panels, the tab group's front
// panel, the section order and heights and a picked column width come back in the next window; a
// record from another layout version is ignored. Offscreen windows neither read nor write
// it on their own.
void ui_panel_layout_returns_in_the_next_window() {
  const QStringList keys = {QStringLiteral("window/panelLayoutVersion"), QStringLiteral("window/hiddenPanels"),
                            QStringLiteral("window/collapsedPanels"), QStringLiteral("window/frontPanel"),
                            QStringLiteral("window/panelColumnWidth"), QStringLiteral("window/panelSplitterState"),
                            QStringLiteral("window/panelOrder")};
  std::vector<std::unique_ptr<SettingsValueRestorer>> restorers;
  for (const auto& key : keys) {
    restorers.push_back(std::make_unique<SettingsValueRestorer>(key));
  }
  {
    auto settings = patchy::ui::app_settings();
    for (const auto& key : keys) {
      settings.remove(key);
    }
    settings.sync();
  }
  CHECK(!patchy::ui::MainWindowTestAccess::panel_layout_persists());
  const auto properties = QStringLiteral("propertiesDock");
  const auto history = QStringLiteral("historyDock");
  bool properties_expanded_before = false;
  int picked_width = 0;
  {
    patchy::ui::MainWindow window;
    show_window(window);
    auto* column = patchy::ui::MainWindowTestAccess::panel_column(window);
    properties_expanded_before = column->panel_expanded(properties);
    column->set_panel_expanded(properties, !properties_expanded_before);
    column->set_panel_visible(history, false);
    require_action(window, "windowPanelChannelsAction")->trigger();
    window.findChild<QToolBar*>(QStringLiteral("Options"))->toggleViewAction()->trigger();
    column->move_section(QStringLiteral("infoDock"), 0);
    QApplication::processEvents();
    picked_width = patchy::ui::MainWindowTestAccess::right_dock_content_minimum_width(window) + 40;
    patchy::ui::MainWindowTestAccess::set_right_dock_stack_width(window, picked_width);
    QApplication::processEvents();
    picked_width = patchy::ui::MainWindowTestAccess::right_dock_pinned_width(window);
    CHECK(column->width() == picked_width);
    // Closing an offscreen window writes nothing.
    window.close();
    QApplication::processEvents();
    CHECK(!patchy::ui::app_settings().contains(QStringLiteral("window/hiddenPanels")));
    patchy::ui::MainWindowTestAccess::write_panel_layout(window);
  }
  {
    // Read before the first show, as the constructor does on a desktop.
    patchy::ui::MainWindow window;
    patchy::ui::MainWindowTestAccess::read_panel_layout(window);
    show_window(window);
    auto* column = patchy::ui::MainWindowTestAccess::panel_column(window);
    CHECK(!column->panel_visible(history));
    CHECK(column->section_widget(history)->isHidden());
    CHECK(column->panel_visible(QStringLiteral("layersDock")));
    CHECK(window.findChild<QToolBar*>(QStringLiteral("Options"))->isHidden());
    CHECK(!window.findChild<QToolBar*>(QStringLiteral("toolPalette"))->isHidden());
    CHECK(column->panel_expanded(properties) == !properties_expanded_before);
    CHECK(!patchy::ui::MainWindowTestAccess::right_dock_width_follows_content(window));
    CHECK(patchy::ui::MainWindowTestAccess::right_dock_pinned_width(window) == picked_width);
    CHECK(column->width() == picked_width);
    emit window.findChild<QMenu*>(QStringLiteral("windowMenu"))->aboutToShow();
    CHECK(!require_action(window, "windowPanelHistoryAction")->isChecked());
    CHECK(column->panel_in_front(QStringLiteral("channelsDock")));
    CHECK(column->section_order().first() == QStringLiteral("infoDock"));
    CHECK(column->splitter()->widget(0)->objectName() == QStringLiteral("infoDock"));
  }
  {
    // A record from another layout version is ignored: the defaults stand.
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("window/panelLayoutVersion"), 999);
    settings.sync();
    patchy::ui::MainWindow window;
    patchy::ui::MainWindowTestAccess::read_panel_layout(window);
    show_window(window);
    auto* column = patchy::ui::MainWindowTestAccess::panel_column(window);
    CHECK(column->panel_visible(history));
    CHECK(!window.findChild<QToolBar*>(QStringLiteral("Options"))->isHidden());
    CHECK(column->panel_expanded(properties) == properties_expanded_before);
    CHECK(patchy::ui::MainWindowTestAccess::right_dock_width_follows_content(window));
    CHECK(column->section_order().first() == QStringLiteral("layersDock"));
  }
}

// Window > Reset Panels returns the first-launch layout: closed panels and toolbars back, the
// first-launch order, the default collapse states, Layers in front and the content-fit column
// width.
void ui_reset_panels_restores_first_launch_layout() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* column = patchy::ui::MainWindowTestAccess::panel_column(window);
  auto* tool_palette = window.findChild<QToolBar*>(QStringLiteral("toolPalette"));
  CHECK(column != nullptr && tool_palette != nullptr);
  if (column == nullptr || tool_palette == nullptr) {
    return;
  }
  const auto collapse_states = [column] {
    QList<bool> states;
    for (const auto& id : column->panel_ids()) {
      states << column->panel_expanded(id);
    }
    return states;
  };
  const auto defaults = collapse_states();
  column->set_panel_visible(QStringLiteral("paletteDock"), false);
  column->set_panel_visible(QStringLiteral("pathsDock"), false);
  require_action(window, "windowPanelChannelsAction")->trigger();
  column->set_panel_expanded(QStringLiteral("historyDock"), !column->panel_expanded(QStringLiteral("historyDock")));
  tool_palette->toggleViewAction()->trigger();
  column->move_section(QStringLiteral("propertiesDock"), 0);
  column->move_section(QStringLiteral("layersDock"), 5);
  patchy::ui::MainWindowTestAccess::set_right_dock_stack_width(
      window, patchy::ui::MainWindowTestAccess::right_dock_content_minimum_width(window) + 60);
  QApplication::processEvents();
  CHECK(!column->panel_visible(QStringLiteral("paletteDock")));
  CHECK(tool_palette->isHidden());
  CHECK(!patchy::ui::MainWindowTestAccess::right_dock_width_follows_content(window));

  require_action(window, "windowResetPanelsAction")->trigger();
  QApplication::processEvents();
  QApplication::processEvents();
  for (const auto& id : column->panel_ids()) {
    CHECK(column->panel_visible(id));
  }
  CHECK(column->panel_in_front(QStringLiteral("layersDock")));
  CHECK(collapse_states() == defaults);
  // Top to bottom in the first-launch order.
  auto* splitter = column->splitter();
  const QStringList order = {QStringLiteral("panelTabGroup"), QStringLiteral("historyDock"),
                             QStringLiteral("propertiesDock"), QStringLiteral("infoDock"),
                             QStringLiteral("paletteDock")};
  for (int index = 0; index < splitter->count(); ++index) {
    CHECK(splitter->widget(index)->objectName() == order.value(index));
    CHECK(splitter->widget(index)->isVisible());
  }
  CHECK(!tool_palette->isHidden());
  CHECK(window.toolBarArea(tool_palette) == Qt::LeftToolBarArea);
  CHECK(patchy::ui::MainWindowTestAccess::right_dock_width_follows_content(window));
  CHECK(patchy::ui::MainWindowTestAccess::right_dock_pinned_width(window) ==
        patchy::ui::MainWindowTestAccess::right_dock_content_minimum_width(window));
  CHECK(column->width() == patchy::ui::MainWindowTestAccess::right_dock_pinned_width(window));
  emit window.findChild<QMenu*>(QStringLiteral("windowMenu"))->aboutToShow();
  CHECK(require_action(window, "windowPanelPaletteAction")->isChecked());
}

// Reset Panels used to re-add every dock, and the column's minimum briefly asked for more
// height than the window had: the window grew past the bottom of the screen. It keeps its
// size and place, on a short screen too.
void ui_reset_panels_keeps_the_window_geometry() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* column = patchy::ui::MainWindowTestAccess::panel_column(window);
  const int short_screen = 720;
  patchy::ui::MainWindowTestAccess::set_available_window_height(window, short_screen);
  patchy::ui::MainWindowTestAccess::fit_right_dock_floors_to_screen(window);
  window.resize(1100, short_screen);
  QApplication::processEvents();
  // Expanded panels ask for the most height.
  for (const auto& id : column->panel_ids()) {
    column->set_panel_expanded(id, true);
  }
  QApplication::processEvents();
  QApplication::processEvents();
  window.resize(1100, short_screen);
  QApplication::processEvents();
  const auto before = window.geometry();
  column->set_panel_visible(QStringLiteral("historyDock"), false);
  QApplication::processEvents();
  require_action(window, "windowResetPanelsAction")->trigger();
  for (int pass = 0; pass < 4; ++pass) {
    QApplication::processEvents();
  }
  CHECK(window.geometry() == before);
  // The column still fits that screen once the layout settles.
  CHECK(window.minimumSizeHint().height() <= before.height());
  patchy::ui::MainWindowTestAccess::set_available_window_height(window, 0);
}

// The Layers panel's new fill or adjustment layer button (GitHub issue 252) lists the fill
// layers, a separator, then the adjustments, exactly as Layer > New Fill Layer and Layer >
// New Adjustment Layer do, so a new kind lands in both.
void ui_layer_panel_new_layer_menu_lists_fills_and_adjustments() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* button = window.findChild<QToolButton*>(QStringLiteral("layerNewAdjustmentButton"));
  auto* fill_menu = window.findChild<QMenu*>(QStringLiteral("layerNewFillMenu"));
  auto* adjustment_menu = window.findChild<QMenu*>(QStringLiteral("layerNewAdjustmentMenu"));
  CHECK(button != nullptr && fill_menu != nullptr && adjustment_menu != nullptr);
  if (button == nullptr || fill_menu == nullptr || adjustment_menu == nullptr || button->menu() == nullptr) {
    CHECK(false);
    return;
  }
  auto* panel_menu = button->menu();
  CHECK(panel_menu->objectName() == QStringLiteral("layerNewAdjustmentButtonMenu"));

  // Review capture: the panel menu as it opens.
  panel_menu->popup(button->mapToGlobal(QPoint(0, button->height())));
  QApplication::processEvents();
  save_widget_artifact("issue_252_panel_menu", *panel_menu);
  panel_menu->hide();
  QApplication::processEvents();

  const auto texts = [](const QMenu& menu) {
    QStringList out;
    for (const auto* action : menu.actions()) {
      out << (action->isSeparator() ? QStringLiteral("---") : QString(action->text()).remove(QLatin1Char('&')));
    }
    return out;
  };
  const auto fills = texts(*fill_menu);
  const auto adjustments = texts(*adjustment_menu);
  CHECK((fills == QStringList{QStringLiteral("Solid Color..."), QStringLiteral("Gradient..."),
                              QStringLiteral("Pattern...")}));
  CHECK(adjustments.contains(QStringLiteral("Channel Mixer...")));
  CHECK(texts(*panel_menu) == fills + QStringList{QStringLiteral("---")} + adjustments);
  CHECK(button->toolTip().startsWith(QStringLiteral("New Fill or Adjustment Layer")));

  // The panel's fill entry creates the fill layer like the Layer menu's (Solid Color asks for a color).
  bool picked = false;
  QTimer::singleShot(0, [&picked] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (auto* dialog = qobject_cast<QDialog*>(widget); dialog != nullptr && dialog->isVisible()) {
        picked = true;
        dialog->accept();
        return;
      }
    }
  });
  const auto layers_before = patchy::ui::MainWindowTestAccess::document(window).layers().size();
  panel_menu->actions().front()->trigger();
  QApplication::processEvents();
  CHECK(picked);
  CHECK(patchy::ui::MainWindowTestAccess::document(window).layers().size() == layers_before + 1);
}

void ui_main_window_renders_color_controls() {
  patchy::ui::MainWindow window;
  show_window(window);

  auto* foreground = window.findChild<QPushButton*>(QStringLiteral("foregroundColorButton"));
  auto* background = window.findChild<QPushButton*>(QStringLiteral("backgroundColorButton"));
  CHECK(foreground != nullptr);
  CHECK(background != nullptr);
  CHECK(foreground->text() == QStringLiteral("FG"));
  CHECK(background->text() == QStringLiteral("BG"));
  CHECK(!foreground->text().contains('#'));
  CHECK(!background->text().contains('#'));
  CHECK(window.findChild<QWidget*>(QStringLiteral("swatchesDock")) == nullptr);
  CHECK(window.findChild<QToolButton*>(QStringLiteral("swatchesDockCollapseButton")) == nullptr);
  CHECK(window.findChildren<QPushButton*>(QStringLiteral("swatchButton")).isEmpty());
  CHECK(window.findChild<QWidget*>(QStringLiteral("paletteDock")) != nullptr);
  const QStringList expected_menus = {QStringLiteral("File"),   QStringLiteral("Edit"),   QStringLiteral("Image"),
                                      QStringLiteral("Layer"),  QStringLiteral("Type"),   QStringLiteral("Select"),
                                      QStringLiteral("Filter"), QStringLiteral("Plugins"), QStringLiteral("View"),
                                      QStringLiteral("Window"), QStringLiteral("Help")};
  QStringList actual_menus;
  for (auto* action : window.menuBar()->actions()) {
    actual_menus << action->text().remove('&');
  }
  CHECK(actual_menus == expected_menus);
  CHECK(window.menuBar()->height() >= 30);
  // Frameless + custom chrome (badge, window buttons) exist only where Patchy draws its
  // own frame (Windows and Linux); macOS uses the native frame and must NOT have them.
  CHECK(window.windowFlags().testFlag(Qt::FramelessWindowHint) ==
        patchy::ui::MainWindow::use_custom_window_chrome());
  auto* app_badge = window.menuBar()->findChild<QLabel*>(QStringLiteral("patchyBadge"));
  auto* window_close = window.findChild<QToolButton*>(QStringLiteral("windowCloseButton"));
  if (patchy::ui::MainWindow::use_custom_window_chrome()) {
    CHECK(app_badge != nullptr);
    CHECK(app_badge->pixmap(Qt::ReturnByValue).isNull() == false);
    CHECK(window_close != nullptr);
    CHECK(window_close->mapTo(&window, QPoint(window_close->width(), 0)).x() >= window.width() - 1);
  } else {
    CHECK(app_badge == nullptr);
    CHECK(window_close == nullptr);
  }
  CHECK(window.findChild<QAction*>(QStringLiteral("workspaceHomeAction")) == nullptr);
  CHECK(window.findChild<QAction*>(QStringLiteral("helpHomepageAction")) == nullptr);
  auto* recent_menu = window.findChild<QMenu*>(QStringLiteral("fileOpenRecentMenu"));
  CHECK(recent_menu != nullptr);
  auto* filter_menu = window.findChild<QMenu*>(QStringLiteral("filterMenu"));
  CHECK(filter_menu != nullptr);
  QStringList filter_action_texts;
  const std::function<void(QMenu*)> collect_filter_actions = [&](QMenu* menu) {
    for (auto* action : menu->actions()) {
      if (action->isSeparator()) {
        continue;
      }
      if (auto* submenu = action->menu(); submenu != nullptr) {
        collect_filter_actions(submenu);
        continue;
      }
      auto text = action->text();
      text.remove('&');
      filter_action_texts << text;
    }
  };
  collect_filter_actions(filter_menu);
  CHECK(filter_action_texts.contains(QStringLiteral("Soft Glow")));
  CHECK(filter_action_texts.contains(QStringLiteral("Punchy Color")));
  CHECK(filter_action_texts.contains(QStringLiteral("Noir")));
  CHECK(filter_action_texts.contains(QStringLiteral("Cinematic Matte")));
  CHECK(filter_action_texts.contains(QStringLiteral("Vintage Fade")));
  CHECK(filter_action_texts.contains(QStringLiteral("Twirl")));
  CHECK(filter_action_texts.contains(QStringLiteral("Clouds")));
  CHECK(filter_action_texts.contains(QStringLiteral("Pixel Mosaic")));
  CHECK(filter_action_texts.contains(QStringLiteral("Unsharp Mask")));
  CHECK(filter_action_texts.contains(QStringLiteral("Motion Blur")));
  CHECK(filter_action_texts.contains(QStringLiteral("Color Halftone")));
  CHECK(!filter_action_texts.contains(QStringLiteral("Brightness +24")));
  CHECK(!filter_action_texts.contains(QStringLiteral("Contrast +25%")));
  CHECK(!filter_action_texts.contains(QStringLiteral("Brightness")));
  CHECK(!filter_action_texts.contains(QStringLiteral("Contrast")));
  CHECK(!filter_action_texts.contains(QStringLiteral("Auto Tone")));
  CHECK(!filter_action_texts.contains(QStringLiteral("Auto Contrast")));
  CHECK(!filter_action_texts.contains(QStringLiteral("Auto Color")));
  CHECK(!filter_action_texts.contains(QStringLiteral("Desaturate")));
  for (auto* action : filter_menu->actions()) {
    CHECK(!action->isIconVisibleInMenu());
  }
  auto* adjustments_menu = window.findChild<QMenu*>(QStringLiteral("imageAdjustmentsMenu"));
  CHECK(adjustments_menu != nullptr);
  QStringList adjustment_action_texts;
  for (auto* action : adjustments_menu->actions()) {
    CHECK(!action->isIconVisibleInMenu());
    if (!action->isSeparator()) {
      auto text = action->text();
      text.remove('&');
      adjustment_action_texts << text;
    }
  }
  CHECK(adjustment_action_texts.contains(QStringLiteral("Brightness/Contrast...")));
  CHECK(adjustment_action_texts.contains(QStringLiteral("Auto Tone")));
  CHECK(adjustment_action_texts.contains(QStringLiteral("Auto Contrast")));
  CHECK(adjustment_action_texts.contains(QStringLiteral("Auto Color")));
  CHECK(adjustment_action_texts.contains(QStringLiteral("Auto All")));
  CHECK(!adjustment_action_texts.contains(QStringLiteral("Brightness...")));
  CHECK(!adjustment_action_texts.contains(QStringLiteral("Contrast...")));
  CHECK(window.findChild<QAction*>(QStringLiteral("imageAdjustBrightnessAction")) == nullptr);
  CHECK(window.findChild<QAction*>(QStringLiteral("imageAdjustContrastAction")) == nullptr);
  auto* new_adjustments_menu = window.findChild<QMenu*>(QStringLiteral("layerNewAdjustmentMenu"));
  CHECK(new_adjustments_menu != nullptr);
  CHECK(require_action(window, "layerNewLevelsAdjustmentAction") != nullptr);
  CHECK(require_action(window, "layerNewCurvesAdjustmentAction") != nullptr);
  CHECK(require_action(window, "layerNewHueSaturationAdjustmentAction") != nullptr);
  CHECK(require_action(window, "layerNewColorBalanceAdjustmentAction") != nullptr);
  CHECK(require_action(window, "layerNewInvertAdjustmentAction") != nullptr);
  CHECK(require_action(window, "layerNewPosterizeAdjustmentAction") != nullptr);
  CHECK(require_action(window, "layerNewThresholdAdjustmentAction") != nullptr);
  CHECK(require_action(window, "layerNewBrightnessContrastAdjustmentAction") != nullptr);
  CHECK(window.findChild<QToolButton*>(QStringLiteral("layerNewAdjustmentButton")) != nullptr);
  // The Layer menu stays short enough for a small browser viewport (wasm) by
  // grouping the new-layer, mask, and arrange sets into submenus; the direct
  // row bound keeps it from silently regrowing.
  auto* layer_menu = window.findChild<QMenu*>(QStringLiteral("layerMenu"));
  CHECK(layer_menu != nullptr);
  CHECK(layer_menu->actions().size() <= 23);
  auto* layer_new_menu = window.findChild<QMenu*>(QStringLiteral("layerNewMenu"));
  CHECK(layer_new_menu != nullptr);
  CHECK(layer_new_menu->actions().contains(require_action(window, "layerNewAction")));
  CHECK(layer_new_menu->actions().contains(require_action(window, "layerNewFolderAction")));
  CHECK(layer_new_menu->actions().contains(require_action(window, "layerViaCopyAction")));
  CHECK(layer_new_menu->actions().contains(require_action(window, "layerViaCutAction")));
  auto* layer_mask_menu = window.findChild<QMenu*>(QStringLiteral("layerMaskMenu"));
  CHECK(layer_mask_menu != nullptr);
  for (const char* mask_action_name :
       {"layerAddMaskAction", "layerEditMaskAction", "layerMaskOverlayAction", "layerViewMaskAction",
        "layerLinkMaskAction", "layerDisableMaskAction", "layerInvertMaskAction", "layerApplyMaskAction",
        "layerDeleteMaskAction"}) {
    CHECK(layer_mask_menu->actions().contains(require_action(window, mask_action_name)));
  }
  auto* layer_arrange_menu = window.findChild<QMenu*>(QStringLiteral("layerArrangeMenu"));
  CHECK(layer_arrange_menu != nullptr);
  CHECK(layer_arrange_menu->actions().contains(require_action_by_text(window, QStringLiteral("Move Layer Up"))));
  // Image > Rotate & Flip and Layer > Rotate & Flip hold the turns and flips; Arrange keeps
  // only the stacking order and Align / Distribute.
  auto* layer_rotate_flip_menu = window.findChild<QMenu*>(QStringLiteral("layerRotateFlipMenu"));
  CHECK(layer_rotate_flip_menu != nullptr);
  CHECK(layer_menu->actions().contains(layer_rotate_flip_menu->menuAction()));
  for (const char* name : {"layerRotateClockwiseAction", "layerRotateCounterclockwiseAction", "layerRotate180Action"}) {
    CHECK(layer_rotate_flip_menu->actions().contains(require_action(window, name)));
    CHECK(!layer_arrange_menu->actions().contains(require_action(window, name)));
  }
  for (const char* text : {"Flip Layer Horizontal", "Flip Layer Vertical"}) {
    CHECK(layer_rotate_flip_menu->actions().contains(require_action_by_text(window, QLatin1String(text))));
    CHECK(!layer_arrange_menu->actions().contains(require_action_by_text(window, QLatin1String(text))));
  }
  auto* image_menu = window.findChild<QMenu*>(QStringLiteral("imageMenu"));
  auto* image_rotate_flip_menu = window.findChild<QMenu*>(QStringLiteral("imageRotateFlipMenu"));
  CHECK(image_menu != nullptr && image_rotate_flip_menu != nullptr);
  CHECK(image_menu->actions().contains(image_rotate_flip_menu->menuAction()));
  for (const char* name : {"imageRotateClockwiseAction", "imageRotateCounterclockwiseAction", "imageRotate180Action",
                           "imageRotateArbitraryAction", "imageFlipHorizontalAction", "imageFlipVerticalAction"}) {
    CHECK(image_rotate_flip_menu->actions().contains(require_action(window, name)));
    CHECK(!image_menu->actions().contains(require_action(window, name)));
  }
  CHECK(image_menu->actions().contains(require_action(window, "imageShiftSeamsAction")));
  // Captures for review: test-artifacts/menu_imageMenu.png and the three others.
  for (auto* menu : {image_menu, image_rotate_flip_menu, layer_menu, layer_rotate_flip_menu}) {
    menu->popup(window.mapToGlobal(QPoint(40, 40)));
    QApplication::processEvents();
    save_widget_artifact("menu_" + menu->objectName().toStdString(), *menu);
    menu->hide();
  }
  auto* feather_spin = window.findChild<QSpinBox*>(QStringLiteral("selectionFeatherSpin"));
  CHECK(feather_spin != nullptr);
  // Photoshop's 0..1000 px selection Feather.
  CHECK(feather_spin->maximum() == 1000);
  auto* feather_canvas = require_canvas(window);
  feather_canvas->set_selection_feather_radius(1000);
  CHECK(feather_canvas->selection_feather_radius() == 1000);
  feather_canvas->set_selection_feather_radius(5000);
  CHECK(feather_canvas->selection_feather_radius() == 1000);
  feather_canvas->set_selection_feather_radius(0);
  for (auto* button : window.findChildren<QPushButton*>()) {
    CHECK(button->text() != QStringLiteral("Select and Mask..."));
  }
  auto* options_bar = window.findChild<QToolBar*>(QStringLiteral("Options"));
  CHECK(options_bar != nullptr);
  CHECK(options_bar->height() >= 36);
  auto* tool_palette = window.findChild<QToolBar*>(QStringLiteral("toolPalette"));
  CHECK(tool_palette != nullptr);
  CHECK(tool_palette->width() <= 45);
  auto* marquee_button = window.findChild<QToolButton*>(QStringLiteral("marqueeToolButton"));
  CHECK(marquee_button != nullptr);
  CHECK(marquee_button->menu() != nullptr);
  CHECK(marquee_button->menu()->actions().size() == 4);  // Marquee, Elliptical, separator, Cycle
  CHECK(marquee_button->defaultAction() == require_action_by_text(window, QStringLiteral("Marquee")));
  auto* shape_button = window.findChild<QToolButton*>(QStringLiteral("shapeToolButton"));
  CHECK(shape_button != nullptr);
  CHECK(shape_button->menu() != nullptr);
  CHECK(shape_button->menu()->actions().size() == 7);  // Rect/Ellipse/Polygon/Line/Custom Shape + separator + Cycle
  CHECK(shape_button->defaultAction() == require_action_by_text(window, QStringLiteral("Rect")));

  save_widget_artifact("ui_main_window", window);
}

// GitHub issue 224: the footer's Quick Mask button shows Patchy's own icon (a framed
// window half tinted with the mask red; fully tinted when checked) instead of "QM"
// text, in both schemes; the action keeps its id, Q shortcut and tooltip.
void ui_tool_palette_quick_mask_button_shows_its_icon() {
  ensure_artifact_dir();
  patchy::ui::MainWindow window;
  show_window(window);
  auto* button = window.findChild<QToolButton*>(QStringLiteral("quickMaskButton"));
  auto* palette = window.findChild<QToolBar*>(QStringLiteral("toolPalette"));
  auto* action = require_hotkey_action(window, QStringLiteral("select.quick_mask"));
  CHECK(button != nullptr);
  CHECK(palette != nullptr);
  if (button == nullptr || palette == nullptr || action == nullptr) {
    return;
  }
  CHECK(button->defaultAction() == action);
  CHECK(action->objectName() == QStringLiteral("selectQuickMaskAction"));
  CHECK(button->toolButtonStyle() == Qt::ToolButtonIconOnly);
  CHECK(button->iconSize() == QSize(22, 22));
  CHECK(!button->icon().isNull());
  CHECK(action->iconText() != QStringLiteral("QM"));
  CHECK(action->shortcut() == QKeySequence(Qt::Key_Q));
  CHECK(action->toolTip().contains(QStringLiteral("Quick Mask")));
  CHECK(action->toolTip().contains(QStringLiteral("(Q)")));

  // The whole button box, border included, sits inside the footer and is centered
  // under the well, inside the palette's right border (GitHub
  // issue 224: the box ran over the palette's right border and lost its bottom line).
  auto* footer = palette->findChild<QWidget*>(QStringLiteral("toolPaletteFooter"));
  CHECK(footer != nullptr);
  if (footer == nullptr) {
    return;
  }
  const auto check_placement = [&] {
    const auto box = button->geometry();
    CHECK(footer->rect().contains(box));
    CHECK(box.top() >= patchy::ui::ToolPaletteFooter::kWellHeight);
    // The footer's last column is the palette's 1 px right border.
    const int cell = std::min(footer->width(), patchy::ui::ToolPaletteFooter::kWidth) - 1;
    CHECK(std::abs(box.left() - (cell - 1 - box.right())) <= 1);
  };
  check_placement();

  // No footer control draws into the footer's left column or the two columns at its
  // right edge (the palette's 1 px border and the column beside it), so nothing
  // touches the toolbox border (GitHub issue 251: the swap glyph's arrow ended
  // beside it). Rendered at 100% and 150%.
  const auto check_edges_clear = [&](const std::string& artifact) {
    const auto background = patchy::ui::theme().tool_palette_bg;
    QList<QImage> renders;
    // The real palette around the footer, its right border included, for the shot.
    palette->repaint();
    renders << palette->grab(QRect(0, footer->geometry().top(), palette->width(), footer->height())).toImage();
    std::vector<int> touching_counts;
    {
      const int width = std::min(footer->width(), patchy::ui::ToolPaletteFooter::kWidth);
      for (const qreal scale : {1.0, 1.5}) {
        QPixmap rendered(QSize(qRound(footer->width() * scale), qRound(footer->height() * scale)));
        rendered.setDevicePixelRatio(scale);
        rendered.fill(background);
        footer->render(&rendered);
        const auto image = rendered.toImage();
        int touching = 0;
        for (int y = 0; y < image.height(); ++y) {
          const int clear_from = static_cast<int>(std::floor((width - 2) * scale));
          for (int x = 0; x < image.width(); x = x == 0 ? clear_from : x + 1) {
            const auto color = image.pixelColor(x, y);
            if (std::abs(color.red() - background.red()) > 6 || std::abs(color.green() - background.green()) > 6 ||
                std::abs(color.blue() - background.blue()) > 6) {
              ++touching;
            }
          }
        }
        renders << image;
        touching_counts.push_back(touching);
        if (touching != 0) {
          fprintf(stderr, "  footer %d px at %.1fx: %d edge pixels drawn\n", width, scale, touching);
        }
      }
    }
    save_stacked_artifact(artifact, renders);  // before the checks: the BEFORE shot of a failing run
    for (const int touching : touching_counts) {
      CHECK(touching == 0);
    }
  };

  // The swap glyph's inked gap to the foreground swatch on its left equals its gap
  // to the background swatch below it, and it keeps 2 px clear of the palette's
  // right border (GitHub issue 251, second pass: Seth asked for equal spacing).
  const auto check_swap_gaps = [&](const std::string& artifact) {
    auto* swap = footer->findChild<QWidget*>(QStringLiteral("colorSwapButton"));
    auto* foreground = footer->findChild<QWidget*>(QStringLiteral("foregroundColorButton"));
    auto* background_swatch = footer->findChild<QWidget*>(QStringLiteral("backgroundColorButton"));
    CHECK(swap != nullptr && foreground != nullptr && background_swatch != nullptr);
    if (swap == nullptr || foreground == nullptr || background_swatch == nullptr) {
      return;
    }
    const auto background = patchy::ui::theme().tool_palette_bg;
    QPixmap rendered(footer->size());
    rendered.setDevicePixelRatio(1.0);
    rendered.fill(background);
    footer->render(&rendered);
    const auto image = rendered.toImage();
    const auto box = swap->geometry();
    int left = std::numeric_limits<int>::max();
    int right = -1;
    int bottom = -1;
    for (int y = box.top(); y <= box.bottom(); ++y) {
      for (int x = box.left(); x <= box.right(); ++x) {
        const auto color = image.pixelColor(x, y);
        if (std::abs(color.red() - background.red()) > 6 || std::abs(color.green() - background.green()) > 6 ||
            std::abs(color.blue() - background.blue()) > 6) {
          left = std::min(left, x);
          right = std::max(right, x);
          bottom = std::max(bottom, y);
        }
      }
    }
    CHECK(right >= 0);
    const int left_gap = left - foreground->geometry().right() - 1;
    const int bottom_gap = background_swatch->geometry().top() - bottom - 1;
    const int border_column = std::min(footer->width(), patchy::ui::ToolPaletteFooter::kWidth) - 1;
    const int border_clearance = border_column - right - 1;
    fprintf(stderr, "  swap glyph gaps: left %d, bottom %d, border clearance %d\n", left_gap, bottom_gap,
            border_clearance);
    // The upper-right corner of the well at 8x, so a single pixel shows.
    const QRect corner(foreground->geometry().right() - 3, 0, border_column + 1 - (foreground->geometry().right() - 3),
                       background_swatch->geometry().top() + 4);
    save_image_artifact(artifact, image.copy(corner).scaled(corner.size() * 8, Qt::IgnoreAspectRatio,
                                                            Qt::FastTransformation));
    CHECK(left_gap == bottom_gap);
    CHECK(left_gap >= 1);
    CHECK(border_clearance >= 2);
  };

  // Off and On are different drawings: On tints the whole window, so it has
  // clearly more mask-red pixels. The frame is icon ink in each scheme.
  const auto red_pixels = [](const QImage& image) {
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
      for (int x = 0; x < image.width(); ++x) {
        const auto color = image.pixelColor(x, y);
        if (color.alpha() > 100 && color.red() > color.green() + 60 && color.red() > color.blue() + 40) {
          ++count;
        }
      }
    }
    return count;
  };
  const auto ink_pixels = [](const QImage& image, QColor ink) {
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
      for (int x = 0; x < image.width(); ++x) {
        const auto color = image.pixelColor(x, y);
        if (color.alpha() > 200 && std::abs(color.red() - ink.red()) < 24 && std::abs(color.green() - ink.green()) < 24 &&
            std::abs(color.blue() - ink.blue()) < 24) {
          ++count;
        }
      }
    }
    return count;
  };
  QList<QImage> strips;
  for (const auto scheme : {patchy::ui::ColorSchemePreference::Dark, patchy::ui::ColorSchemePreference::Light}) {
    ColorSchemeRestorer restorer(scheme);
    process_events_for(60);
    check_edges_clear(scheme == patchy::ui::ColorSchemePreference::Dark ? "ui_tool_palette_footer_edges_dark"
                                                                         : "ui_tool_palette_footer_edges_light");
    check_swap_gaps(scheme == patchy::ui::ColorSchemePreference::Dark ? "ui_tool_palette_swap_arrow_dark"
                                                                       : "ui_tool_palette_swap_arrow_light");
    const auto ink = patchy::ui::theme().icon_ink;
    const auto off = action->icon().pixmap(QSize(22, 22), QIcon::Normal, QIcon::Off).toImage();
    const auto on = action->icon().pixmap(QSize(22, 22), QIcon::Normal, QIcon::On).toImage();
    CHECK(!off.isNull());
    CHECK(!on.isNull());
    CHECK(off != on);
    CHECK(red_pixels(off) >= 20);
    CHECK(red_pixels(on) > red_pixels(off) + 30);
    CHECK(ink_pixels(off, ink) >= 20);
    CHECK(ink_pixels(on, ink) >= 20);
    // The icon's frame survives at 22 px and at 150%: its bottom edge is ink too.
    for (const qreal scale : {1.0, 1.5}) {
      QPixmap scaled(QSize(qRound(22 * scale), qRound(22 * scale)));
      scaled.setDevicePixelRatio(scale);
      scaled.fill(Qt::transparent);
      QPainter icon_painter(&scaled);
      action->icon().paint(&icon_painter, QRect(0, 0, 22, 22), Qt::AlignCenter, QIcon::Normal, QIcon::Off);
      icon_painter.end();
      const auto image = scaled.toImage();
      const auto lower_half = image.copy(0, image.height() / 2, image.width(), image.height() / 2);
      const auto upper_half = image.copy(0, 0, image.width(), image.height() / 2);
      CHECK(ink_pixels(lower_half, ink) >= ink_pixels(upper_half, ink) / 3);
    }

    // The checked box shows its accent border on all four sides, at 100% and 150%
    // (issue 224: the bottom line was cut off by the footer).
    action->setChecked(true);
    footer->repaint();
    const auto border = patchy::ui::theme().accent_border_bright;
    const auto near_border = [&border](QColor color) {
      return std::abs(color.red() - border.red()) <= 12 && std::abs(color.green() - border.green()) <= 12 &&
             std::abs(color.blue() - border.blue()) <= 12;
    };
    const auto box = button->geometry();
    QList<QImage> scaled_footers;
    for (const qreal scale : {1.0, 1.5}) {
      QPixmap rendered(QSize(qRound(footer->width() * scale), qRound(footer->height() * scale)));
      rendered.setDevicePixelRatio(scale);
      rendered.fill(patchy::ui::theme().tool_palette_bg);
      footer->render(&rendered);
      const auto image = rendered.toImage();
      const auto device = [scale](int logical) { return static_cast<int>(std::floor(logical * scale)); };
      const auto side_has_border = [&](int x0, int y0, int x1, int y1) {
        for (int y = std::max(0, y0); y <= std::min(image.height() - 1, y1); ++y) {
          for (int x = std::max(0, x0); x <= std::min(image.width() - 1, x1); ++x) {
            if (near_border(image.pixelColor(x, y))) {
              return true;
            }
          }
        }
        return false;
      };
      const int cx = device(box.center().x());
      const int cy = device(box.center().y());
      const int pad = scale > 1.0 ? 1 : 0;
      CHECK(side_has_border(cx, device(box.top()) - pad, cx, device(box.top()) + pad + 1));
      CHECK(side_has_border(cx, device(box.bottom()) - pad, cx, device(box.bottom() + 1) - 1 + pad));
      CHECK(side_has_border(device(box.left()) - pad, cy, device(box.left()) + pad + 1, cy));
      CHECK(side_has_border(device(box.right()) - pad, cy, device(box.right() + 1) - 1 + pad, cy));
      scaled_footers << image;
    }
    action->setChecked(false);
    footer->repaint();

    // The footer as it sits under the tools, unchecked then checked, plus the
    // checked footer at 150%.
    const auto footer_rect = QRect(0, std::max(0, palette->height() - 110), palette->width(), 110);
    palette->repaint();
    const auto unchecked = palette->grab(footer_rect).toImage();
    action->setChecked(true);
    palette->repaint();
    const auto checked = palette->grab(footer_rect).toImage();
    action->setChecked(false);
    const auto& hi_dpi = scaled_footers.back();
    QImage row(unchecked.width() * 2 + hi_dpi.width() + 16, std::max(unchecked.height(), hi_dpi.height()),
               QImage::Format_ARGB32);
    row.fill(patchy::ui::theme().tool_palette_bg);
    QPainter painter(&row);
    painter.drawImage(0, row.height() - unchecked.height(), unchecked);
    painter.drawImage(unchecked.width() + 8, row.height() - checked.height(), checked);
    painter.drawImage(unchecked.width() * 2 + 16, row.height() - hi_dpi.height(), hi_dpi);
    painter.end();
    strips << row;
  }
  save_stacked_artifact("ui_tool_palette_quick_mask_icon", strips);
}

void ui_tool_palette_overflow_hides_quick_mask_before_swatches() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto setup = shrink_window_until_tool_palette_overflows(window);

  // The footer (color well, Quick Mask) is pinned to the bottom and never
  // overflows: a short window folds the tools above it into the extension and
  // the extension button sits above the footer, whole and unclipped.
  CHECK(setup.quick_mask->isVisible());
  CHECK(setup.foreground->isVisible());
  CHECK(setup.background->isVisible());
  CHECK(setup.palette->width() <= 45);
  auto* footer = setup.palette->findChild<QWidget*>(QStringLiteral("toolPaletteFooter"));
  CHECK(footer != nullptr);
  if (footer != nullptr) {
    CHECK(footer->geometry().bottom() >= setup.palette->height() - 6);
    // Qt lets the extension button run 2 px into the margin; the well's
    // controls start below that.
    CHECK(setup.extension->geometry().bottom() <= footer->geometry().top() + 2);
    CHECK(setup.extension->geometry().bottom() < footer->geometry().top() + setup.foreground->geometry().top());
    CHECK(setup.extension->geometry().top() >= 0);
    CHECK(setup.extension->height() >= 12);
    CHECK(setup.palette->rect().contains(setup.extension->geometry()));
    // The swatches overlap foreground-over-background inside the 43 px well.
    const auto fg = setup.foreground->geometry();
    const auto bg = setup.background->geometry();
    CHECK(fg.intersects(bg));
    CHECK(fg.left() < bg.left() && fg.top() < bg.top());
    CHECK(bg.right() < patchy::ui::ToolPaletteFooter::kWidth);
  }
  save_widget_artifact("ui_tool_palette_overflow_pinned_footer", window);
}

// GitHub issues 126 and 124: after a window grows and shrinks again (a maximize and
// restore, a panel expanding), the extension chevron must still sit whole above the footer
// and never over a tool, and it must be visible whenever tools overflow.
void ui_tool_palette_extension_survives_resize_cycles() {
  patchy::ui::MainWindow window;
  window.setAnimated(false);
  show_window(window);
  const auto setup = shrink_window_until_tool_palette_overflows(window);
  auto* footer = setup.palette->findChild<QWidget*>(QStringLiteral("toolPaletteFooter"));
  CHECK(footer != nullptr);
  if (footer == nullptr) {
    return;
  }
  const auto check_state = [&](const char* stage) {
    QApplication::processEvents();
    process_events_for(30);
    bool any_tool_hidden = false;
    for (auto* button : setup.palette->findChildren<QToolButton*>()) {
      if (button != setup.extension && !footer->isAncestorOf(button) && button->isHidden()) {
        any_tool_hidden = true;
      }
    }
    if (any_tool_hidden && !setup.extension->isVisible()) {
      std::printf("  %s: tools overflow but the extension button is hidden\n", stage);
      CHECK(setup.extension->isVisible());
    }
    if (!setup.extension->isVisible()) {
      return;
    }
    const auto ext = setup.extension->geometry();
    if (ext.bottom() > footer->geometry().top() + 2 || ext.top() < 0) {
      std::printf("  %s: extension %d..%d vs footer top %d\n", stage, ext.top(), ext.bottom(), footer->geometry().top());
    }
    CHECK(ext.bottom() <= footer->geometry().top() + 2);
    CHECK(ext.top() >= 0);
    for (auto* button : setup.palette->findChildren<QToolButton*>()) {
      if (button == setup.extension || footer->isAncestorOf(button) || !button->isVisible()) {
        continue;
      }
      if (ext.intersects(button->geometry())) {
        std::printf("  %s: extension overlaps %s\n", stage, button->defaultAction() != nullptr
                        ? button->defaultAction()->text().toUtf8().constData() : "a button");
      }
      CHECK(!ext.intersects(button->geometry()));
    }
  };
  check_state("short");
  const auto short_size = window.size();
  for (int cycle = 0; cycle < 2; ++cycle) {
    window.resize(1180, 1000);  // a maximize
    check_state("tall");
    window.resize(short_size);  // and its restore
    check_state("short again");
  }
  window.resize(short_size.width(), short_size.height() - 60);  // a panel expanding below
  check_state("shorter");
  window.resize(short_size);
  check_state("short once more");
}

// GitHub issue 241 follow-up: opening and closing the tool palette's extra columns
// narrows and widens the canvas, and the document stays centered both ways.
void ui_tool_palette_expansion_keeps_the_document_centered() {
  patchy::ui::MainWindow window;
  window.setAnimated(false);
  show_window(window);
  patchy::ui::MainWindowTestAccess::create_document(window, 120, 80);
  QApplication::processEvents();
  const auto setup = shrink_window_until_tool_palette_overflows(window);
  auto* canvas = require_canvas(window);
  canvas->set_view_zoom_centered(1.0);
  QApplication::processEvents();
  const auto centered = [canvas] {
    const auto position = canvas->widget_position_f(QPointF(60.0, 40.0));
    return std::abs(position.x() - canvas->width() / 2.0) <= 1.0 &&
           std::abs(position.y() - canvas->height() / 2.0) <= 1.0;
  };
  CHECK(centered());
  const auto collapsed_width = canvas->width();
  setup.extension->click();
  CHECK(process_events_until([&] { return canvas->width() < collapsed_width; }));
  QApplication::processEvents();
  CHECK(centered());
  setup.extension->click();
  CHECK(process_events_until([&] { return canvas->width() == collapsed_width; }));
  QApplication::processEvents();
  CHECK(centered());
}

void ui_tool_palette_extension_button_expands_palette() {
  patchy::ui::MainWindow window;
  // The expanded geometry is otherwise applied through the ~200ms main-window
  // widget animator; disable it so one event-loop settle suffices.
  window.setAnimated(false);
  show_window(window);
  const auto setup = shrink_window_until_tool_palette_overflows(window);
  CHECK(setup.quick_mask->isVisible());  // the footer never overflows

  const auto rendered = [](const QIcon& icon) { return icon.pixmap(QSize(16, 16)).toImage(); };
  // The arrow points where the expansion goes: right while collapsed, left once
  // the extra column is open (GitHub issue 122).
  CHECK(rendered(setup.extension->icon()) == rendered(patchy::ui::themed_svg_icon(QStringLiteral("chevron-right"))));
  setup.extension->click();
  CHECK(process_events_until([&] { return setup.palette->width() > 45; }));
  CHECK(rendered(setup.extension->icon()) == rendered(patchy::ui::themed_svg_icon(QStringLiteral("chevron-left"))));
  CHECK(setup.foreground->isVisible());
  CHECK(setup.background->isVisible());
  CHECK(setup.quick_mask->isVisible());
  // The reservation controller widens the palette's slot so the expansion
  // never overlays the canvas.
  CHECK(process_events_until(
      [&] { return !window.centralWidget()->geometry().intersects(setup.palette->geometry()); }));
  save_widget_artifact("ui_tool_palette_overflow_expanded", window);

  // Stock Qt collapses the expansion half a second after the pointer leaves
  // the bar; the palette keeps it open until it is explicitly toggled closed.
  QEvent leave_event(QEvent::Leave);
  QApplication::sendEvent(setup.palette, &leave_event);
  process_events_for(700);
  CHECK(setup.palette->width() > 45);
  CHECK(setup.quick_mask->isVisible());

  setup.extension->click();
  CHECK(process_events_until([&] { return setup.palette->width() <= 45; }));
  CHECK(setup.quick_mask->isVisible());  // the footer never overflows
  CHECK(setup.foreground->isVisible());
}

// GitHub issues 121 and 124 probes. Expanding the palette into extra columns
// must keep the first column's buttons exactly where the collapsed column had
// them (a 1 px shift of Move, Crop, Brush and Eraser was reported) and must
// reserve only the columns it fills (a third, empty column was reported), and
// both must hold across window resizes while the expansion stays open.
void ui_tool_palette_expansion_keeps_columns_tight_and_aligned() {
  patchy::ui::MainWindow window;
  window.setAnimated(false);
  show_window(window);
  const auto setup = shrink_window_until_tool_palette_overflows(window);
  auto* footer = setup.palette->findChild<QWidget*>(QStringLiteral("toolPaletteFooter"));
  CHECK(footer != nullptr);
  if (footer == nullptr) {
    return;
  }
  const auto tool_buttons = [&] {
    std::vector<QToolButton*> buttons;
    for (auto* button : setup.palette->findChildren<QToolButton*>()) {
      if (button != setup.extension && !footer->isAncestorOf(button) && button->isVisible()) {
        buttons.push_back(button);
      }
    }
    return buttons;
  };
  const auto name_of = [](QToolButton* button) {
    return button->defaultAction() != nullptr ? button->defaultAction()->text().toUtf8() : QByteArray("a button");
  };
  std::map<QToolButton*, QRect> collapsed;
  for (auto* button : tool_buttons()) {
    collapsed[button] = button->geometry();
  }
  CHECK(!collapsed.empty());

  setup.extension->click();
  CHECK(process_events_until([&] { return setup.palette->width() > 45; }));
  process_events_for(80);

  const auto check_columns = [&](const char* stage) {
    QApplication::processEvents();
    process_events_for(80);
    int max_right = 0;
    int moved = 0;
    for (auto* button : tool_buttons()) {
      max_right = std::max(max_right, button->geometry().right());
      const auto it = collapsed.find(button);
      if (it == collapsed.end() || button->geometry().x() >= it->second.x() + it->second.width()) {
        continue;  // a button that moved to an extra column (a shorter window needs more of them)
      }
      if (it->second.x() != button->geometry().x() || it->second.width() != button->geometry().width()) {
        ++moved;
        std::printf("  %s: %s was %dx%d at x=%d, now %dx%d at x=%d", stage, name_of(button).constData(),
                    it->second.width(), it->second.height(), it->second.x(), button->width(), button->height(),
                    button->geometry().x());
        std::puts("");
      }
    }
    const int slack = setup.palette->width() - max_right;
    std::printf("  %s: palette width %d, rightmost tool edge %d, slack %d, extension checked %d", stage,
                setup.palette->width(), max_right, slack, static_cast<int>(setup.extension->isChecked()));
    std::puts("");
    CHECK(moved == 0);
    CHECK(slack < 40);
  };
  check_columns("expanded");
  const auto short_size = window.size();
  window.resize(short_size.width(), short_size.height() + 24);
  check_columns("taller");
  window.resize(short_size.width(), short_size.height() - 80);
  check_columns("shorter");
  window.resize(short_size);
  check_columns("short again");
  window.resize(1180, 1000);
  check_columns("tall");
  window.resize(short_size);
  check_columns("short once more");
}

void ui_tool_palette_expansion_sticky_after_tool_pick() {
  patchy::ui::MainWindow window;
  window.setAnimated(false);
  show_window(window);
  const auto setup = shrink_window_until_tool_palette_overflows(window);

  setup.extension->click();
  CHECK(process_events_until([&] { return setup.palette->width() > 45; }));
  CHECK(process_events_until(
      [&] { return !window.centralWidget()->geometry().intersects(setup.palette->geometry()); }));

  // Picking a palette item keeps the expansion open: an auto-collapse here ate
  // the first click of the Zoom button's double-click, and the reserved-width
  // layout means the open expansion costs the canvas nothing it was not
  // already paying.
  auto* eraser_button = qobject_cast<QAbstractButton*>(
      setup.palette->widgetForAction(require_action(window, "toolEraserAction")));
  CHECK(eraser_button != nullptr);
  eraser_button->click();
  process_events_for(60);
  CHECK(setup.palette->width() > 45);
  CHECK(setup.quick_mask->isVisible());
  CHECK(require_action(window, "toolEraserAction")->isChecked());
  CHECK(!window.centralWidget()->geometry().intersects(setup.palette->geometry()));

  // The extension button stays the explicit way to close the expansion, and
  // closing releases the reserved width.
  setup.extension->click();
  CHECK(process_events_until([&] { return setup.palette->width() <= 45; }));
  CHECK(setup.palette->minimumWidth() == 43);
  CHECK(setup.quick_mask->isVisible());  // the footer never overflows
  CHECK(setup.foreground->isVisible());
}

// Regression: with collapse-on-pick, the first click of a double-click on the
// expanded Zoom button closed the expansion, the button moved out from under
// the pointer, and the second click fell through to whatever was underneath.
void ui_tool_palette_expanded_zoom_double_click_sets_actual_pixels() {
  patchy::ui::MainWindow window;
  window.setAnimated(false);
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom_centered(2.0);
  CHECK(canvas->zoom() == 2.0);
  const auto setup = shrink_window_until_tool_palette_overflows(window);

  setup.extension->click();
  CHECK(process_events_until([&] { return setup.palette->width() > 45; }));

  auto* zoom_button = window.findChild<QToolButton*>(QStringLiteral("zoomToolButton"));
  CHECK(zoom_button != nullptr);
  CHECK(zoom_button->isVisible());
  const QPoint center(zoom_button->width() / 2, zoom_button->height() / 2);
  click_widget_like_a_user(*zoom_button, center);
  // Give any (formerly fatal) deferred collapse a chance to run between the
  // two clicks, the way the double-click interval does for a real user.
  process_events_for(60);
  CHECK(setup.palette->width() > 45);
  CHECK(zoom_button->isVisible());
  send_double_click(*zoom_button, center);
  CHECK(canvas->zoom() == 1.0);
  CHECK(setup.palette->width() > 45);
}

// GitHub issue 88: with the main-window animator on (the default outside these
// tests), collapsing the expansion on Qt 6.11 left the buttons where the wide
// bar had centered them. Only a build against that Qt fails here without the
// fix (the Linux remote suite runs on it); Qt 6.8.3 pins the animation
// hand-back instead.
void ui_tool_palette_animated_collapse_realigns_buttons() {
  patchy::ui::MainWindow window;
  CHECK(window.isAnimated());
  show_window(window);
  const auto setup = shrink_window_until_tool_palette_overflows(window);

  setup.extension->click();
  CHECK(process_events_until([&] { return setup.palette->width() > 45; }));
  CHECK(process_events_until(
      [&] { return !window.centralWidget()->geometry().intersects(setup.palette->geometry()); }));
  process_events_for(400);
  CHECK(window.isAnimated());

  setup.extension->click();
  CHECK(process_events_until([&] { return setup.palette->width() <= 45; }));
  process_events_for(400);
  CHECK(window.isAnimated());
  CHECK(setup.palette->width() <= 45);
  CHECK(!window.centralWidget()->geometry().intersects(setup.palette->geometry()));
  int visible_children = 0;
  for (auto* child : setup.palette->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) {
    if (!child->isVisible()) {
      continue;
    }
    ++visible_children;
    CHECK(child->geometry().left() >= 0);
    CHECK(child->geometry().right() < setup.palette->width());
  }
  CHECK(visible_children > 10);
}

void ui_tool_palette_expansion_collapses_when_window_fits_again() {
  patchy::ui::MainWindow window;
  window.setAnimated(false);
  show_window(window);
  const auto setup = shrink_window_until_tool_palette_overflows(window);

  setup.extension->click();
  CHECK(process_events_until([&] { return setup.palette->width() > 45; }));

  // Growing the window until everything fits leaves the expansion nothing to
  // reveal; the reservation controller closes it and releases the minimum.
  window.resize(window.width(), setup.fits_height + 24);
  CHECK(process_events_until([&] { return setup.palette->width() <= 45; }));
  CHECK(process_events_until([&] { return !setup.extension->isVisible(); }));
  CHECK(setup.palette->minimumWidth() == 43);
  CHECK(setup.quick_mask->isVisible());
}

// Dissolve's dither threshold is a pure function of the document coordinate,
// which is exactly what the dirty-rect patch machinery needs: rendering a
// sub-rect must produce the same pixels as the same region of a full render.
// A stateful or scanline-counted noise field would pass every core unit test
// and still crawl under the canvas as the user pans.
void ui_dissolve_clipped_render_matches_full_render() {
  patchy::Document document(160, 120, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background",
                           solid_pixels(160, 120, patchy::PixelFormat::rgba8(), QColor(Qt::black)));
  auto& top = document.add_pixel_layer(
      "Dissolve", solid_pixels(160, 120, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  top.set_blend_mode(patchy::BlendMode::Dissolve);
  top.set_opacity(0.5F);

  const auto full = patchy::ui::qimage_from_document(document, true);
  const std::array<QRect, 4> patches{QRect(0, 0, 71, 53), QRect(71, 0, 89, 53), QRect(0, 53, 71, 67),
                                     QRect(71, 53, 89, 67)};
  int painted = 0;
  for (const auto& patch : patches) {
    const auto clipped = patchy::ui::qimage_from_document_rect(document, patch, true);
    CHECK(clipped.width() == patch.width());
    CHECK(clipped.height() == patch.height());
    for (int y = 0; y < patch.height(); ++y) {
      for (int x = 0; x < patch.width(); ++x) {
        const auto expected = full.pixelColor(patch.x() + x, patch.y() + y);
        CHECK(clipped.pixelColor(x, y) == expected);
        // All or nothing, never a blended grey.
        CHECK(expected.red() == 0 || expected.red() == 255);
        painted += expected.red() == 255 ? 1 : 0;
      }
    }
  }
  // Roughly half of 160x120 dithered on.
  CHECK(painted > 9000);
  CHECK(painted < 10500);
}

void ui_window_force_refresh_action_rebuilds_cache() {
  patchy::Document document(180, 130, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(180, 130, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::Layer layer(document.allocate_layer_id(), "Blue Block",
                      solid_pixels(44, 28, patchy::PixelFormat::rgba8(), QColor(20, 90, 235)));
  layer.set_bounds(patchy::Rect{42, 38, 44, 28});
  const auto expected = patchy::ui::qimage_from_document(document, true);

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Force Refresh"));
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  auto* action = require_action(window, "windowForceRefreshAction");
  CHECK(action->isEnabled());
  CHECK(action->shortcuts().contains(QKeySequence(Qt::Key_F5)));

  const auto before = canvas->render_cache_diagnostics();
  action->trigger();
  QApplication::processEvents();
  const auto after = canvas->render_cache_diagnostics();
  CHECK(after.full_refreshes == before.full_refreshes + 1);
  CHECK(after.forced_refreshes == before.forced_refreshes + 1);

  CHECK(color_close(canvas_pixel(*canvas, QPoint(48, 44)), expected.pixelColor(48, 44), 0));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(16, 16)), expected.pixelColor(16, 16), 0));
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Forced refresh"));
  save_widget_artifact("ui_window_force_refresh", window);
}

void ui_canvas_ignores_opaque_psd_flat_cache_for_first_paint_transparency() {
  patchy::Document document(40, 30, patchy::PixelFormat::rgb8());
  auto layer_pixels = solid_pixels(40, 30, patchy::PixelFormat::rgba8(), Qt::transparent);
  fill_pixel_rect(layer_pixels, QRect(16, 12, 12, 10), QColor(230, 20, 30, 255));
  document.add_pixel_layer("Transparent Layer", std::move(layer_pixels));

  auto flat_composite = solid_pixels(40, 30, patchy::PixelFormat::rgb8(), Qt::black);
  fill_pixel_rect(flat_composite, QRect(16, 12, 12, 10), QColor(230, 20, 30));
  document.metadata().psd_flat_composite = std::move(flat_composite);

  patchy::ui::CanvasWidget canvas;
  canvas.resize(140, 100);
  canvas.set_document(&document);
  canvas.show();
  QApplication::processEvents();

  CHECK(color_close(canvas_pixel(canvas, QPoint(2, 2)), QColor(188, 188, 188), 1));
  CHECK(color_close(canvas_pixel(canvas, QPoint(14, 2)), QColor(236, 236, 236), 1));
  CHECK(color_close(canvas_pixel(canvas, QPoint(18, 14)), QColor(230, 20, 30), 1));
  CHECK(canvas.render_cache_diagnostics().full_refreshes == 1);
}

void ui_top_menu_items_highlight_on_hover() {
  patchy::ui::MainWindow window;
  show_window(window);

  auto* file_menu = window.menuBar()->actions().front()->menu();
  CHECK(file_menu != nullptr);
  auto* open_action = require_action(window, "fileOpenAction");
  CHECK(file_menu->actions().contains(open_action));

  file_menu->popup(window.mapToGlobal(QPoint(40, 40)));
  QApplication::processEvents();
  const auto open_rect = file_menu->actionGeometry(open_action);
  CHECK(open_rect.isValid());
  const QPoint sample_point(open_rect.left() + 8, open_rect.center().y());
  const auto idle_color = file_menu->grab().toImage().pixelColor(sample_point);

  send_mouse(*file_menu, QEvent::MouseMove, open_rect.center(), Qt::NoButton, Qt::NoButton);
  if (file_menu->activeAction() != open_action) {
    file_menu->setActiveAction(open_action);
    QApplication::processEvents();
  }

  const auto hover_color = file_menu->grab().toImage().pixelColor(sample_point);
  CHECK(color_close(idle_color, QColor(58, 58, 58), 6));
  CHECK(color_close(hover_color, QColor(78, 111, 149), 6));
  CHECK(!color_close(idle_color, hover_color, 10));
  file_menu->close();
}

void ui_save_as_dialog_lists_recent_files() {
  ensure_artifact_dir();
  const auto first_path =
      QFileInfo(QStringLiteral("test-artifacts/recent-save-target.psd")).absoluteFilePath();
  const auto second_path =
      QFileInfo(QStringLiteral("test-artifacts/recent-save-backup.png")).absoluteFilePath();
  {
    QFile first(first_path);
    CHECK(first.open(QIODevice::WriteOnly));
    CHECK(first.write("patchy psd placeholder") > 0);
    QFile second(second_path);
    CHECK(second.open(QIODevice::WriteOnly));
    CHECK(second.write("patchy png placeholder") > 0);
  }

  SettingsValueRestorer recent_files_restorer(QStringLiteral("recentFiles"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("recentFiles"), QStringList{first_path, second_path});
    settings.sync();
  }

  patchy::ui::MainWindow window;
  show_window(window);

  auto* recent_menu = window.findChild<QMenu*>(QStringLiteral("fileOpenRecentMenu"));
  CHECK(recent_menu != nullptr);
  // The filter row leads the menu; the scrolling file list follows it.
  CHECK(recent_menu->actions().size() >= 2);
  CHECK(recent_menu->actions()[0]->objectName() == QStringLiteral("fileOpenRecentFilterAction"));
  CHECK(recent_menu->actions()[1]->objectName() == QStringLiteral("fileOpenRecentListAction"));
  CHECK(recent_menu_paths(*recent_menu) == (QStringList{first_path, second_path}));
  auto* first_row = recent_menu_list_item(*recent_menu, first_path);
  CHECK(first_row != nullptr);
  CHECK(first_row->text() == QFileInfo(first_path).fileName());
  CHECK(first_row->toolTip() == QDir::toNativeSeparators(first_path));

  auto* save_as_action = require_action(window, "fileSaveAsAction");
  CHECK(save_as_action->shortcut() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));

  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QFileDialog*>(find_top_level_dialog(QStringLiteral("saveAsFileDialog")));
    CHECK(dialog != nullptr);
    auto* combo = dialog->findChild<QComboBox*>(QStringLiteral("saveAsRecentFileNameCombo"));
    CHECK(combo != nullptr);
    CHECK(combo->isEditable());
    CHECK(combo->count() == 2);
    CHECK(combo->itemText(0) == first_path);
    CHECK(combo->itemData(0).toString() == first_path);
    CHECK(combo->itemData(0, Qt::ToolTipRole).toString() == first_path);
    CHECK(combo->itemText(1) == second_path);
    CHECK(combo->itemData(1).toString() == second_path);
    combo->setCurrentIndex(1);
    QApplication::processEvents();
    const auto selected_files = dialog->selectedFiles();
    CHECK(!selected_files.isEmpty());
    CHECK(QFileInfo(selected_files.first()).absoluteFilePath() == second_path);
    saw_dialog = true;
    dialog->reject();
  });
  save_as_action->trigger();
  CHECK(saw_dialog);
}

void ui_open_recent_lists_two_hundred_files_in_one_scrolling_list() {
  ensure_artifact_dir();
  QStringList recent_files;
  for (int i = 0; i < 205; ++i) {
    const auto path =
        QFileInfo(QStringLiteral("test-artifacts/recent-file-%1.psd").arg(i, 3, 10, QLatin1Char('0')))
            .absoluteFilePath();
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    CHECK(file.write("patchy recent placeholder") > 0);
    recent_files << path;
  }

  SettingsValueRestorer recent_files_restorer(QStringLiteral("recentFiles"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("recentFiles"), recent_files);
    settings.sync();
  }

  patchy::ui::MainWindow window;
  show_window(window);

  // One list of every entry (the persisted cap is 200), no paged submenus and no
  // per-file menu rows: the menu stays a compact box beside the File menu.
  auto* recent_menu = window.findChild<QMenu*>(QStringLiteral("fileOpenRecentMenu"));
  CHECK(recent_menu != nullptr);
  auto* list = recent_menu->findChild<QListWidget*>(QStringLiteral("fileOpenRecentList"));
  CHECK(list != nullptr);
  const auto all_paths = recent_menu_paths(*recent_menu);
  CHECK(all_paths.size() == 200);
  for (int i = 0; i < all_paths.size(); ++i) {
    CHECK(all_paths[i] == recent_files[i]);
  }
  CHECK(!all_paths.contains(recent_files[200]));
  for (auto* action : recent_menu->actions()) {
    CHECK(action->menu() == nullptr);
    CHECK(action->data().toString().isEmpty());
  }
  CHECK(recent_menu->actions().contains(require_action(window, "fileClearRecentAction")));

  recent_menu->popup(window.mapToGlobal(QPoint(40, 40)));
  QApplication::processEvents();
  CHECK(list->isVisible());
  CHECK(list->verticalScrollBar()->maximum() > 0);  // the rest scroll
  CHECK(list->height() <= 12 * 40 + 14);
  CHECK(recent_menu->width() <= 640);
  if (auto* screen = QApplication::primaryScreen(); screen != nullptr) {
    CHECK(recent_menu->height() <= screen->availableGeometry().height());
  }
  save_widget_artifact("ui_open_recent_scrolling_list", *recent_menu);

  // Right-click on a row offers the shared recent-entry context menu.
  QApplication::clipboard()->clear();
  auto* older_row = recent_menu_list_item(*recent_menu, recent_files[50]);
  CHECK(older_row != nullptr);
  list->scrollToItem(older_row);
  QApplication::processEvents();
  bool saw_context_menu = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* menu = qobject_cast<QMenu*>(widget);
      if (menu == nullptr || menu->objectName() != QStringLiteral("recentFileContextMenu")) {
        continue;
      }
      auto* copy_action = find_menu_action_by_text(*menu, QStringLiteral("Copy File Path"));
      CHECK(copy_action != nullptr);
      CHECK(copy_action->objectName() == QStringLiteral("recentFileCopyPathAction"));
      copy_action->trigger();
      menu->close();
      saw_context_menu = true;
      return;
    }
    CHECK(false);
  });
  const auto context_point = list->visualItemRect(older_row).center();
  QContextMenuEvent context_event(QContextMenuEvent::Mouse, context_point,
                                  list->viewport()->mapToGlobal(context_point));
  QApplication::sendEvent(list->viewport(), &context_event);
  QApplication::processEvents();
  CHECK(saw_context_menu);
  CHECK(QApplication::clipboard()->text() == QDir::toNativeSeparators(recent_files[50]));

  // A click opens the row; a vanished file reports it and leaves the list.
  recent_menu->popup(window.mapToGlobal(QPoint(40, 40)));
  QApplication::processEvents();
  list = recent_menu->findChild<QListWidget*>(QStringLiteral("fileOpenRecentList"));
  CHECK(list != nullptr);
  older_row = recent_menu_list_item(*recent_menu, recent_files[50]);
  CHECK(older_row != nullptr);
  list->scrollToItem(older_row);
  QApplication::processEvents();
  CHECK(QFile::remove(recent_files[50]));
  QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, list->visualItemRect(older_row).center());
  QApplication::processEvents();  // the deferred open fires
  QApplication::processEvents();
  CHECK(!recent_menu->isVisible());
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Recent file is missing"));
  const auto refreshed_paths = recent_menu_paths(*recent_menu);
  CHECK(refreshed_paths.size() == 199);
  CHECK(!refreshed_paths.contains(recent_files[50]));
}

void ui_open_recent_filter_narrows_entries_and_opens_first_match() {
  ensure_artifact_dir();
  QStringList recent_files;
  for (int i = 0; i < 120; ++i) {
    const bool beta = (i % 2) == 1;
    const auto pattern = beta ? QStringLiteral("test-artifacts/recent-filter-beta-%1.png")
                              : QStringLiteral("test-artifacts/recent-filter-alpha-%1.psd");
    const auto path = QFileInfo(pattern.arg(i, 3, 10, QLatin1Char('0'))).absoluteFilePath();
    if (i == 1 || i == 3) {
      // Enter opens these for real, so they must be loadable images.
      QImage image(48, 32 + i, QImage::Format_RGB32);
      image.fill(QColor(90, 150, 210));
      CHECK(image.save(path));
    } else {
      QFile file(path);
      CHECK(file.open(QIODevice::WriteOnly));
      CHECK(file.write("patchy recent placeholder") > 0);
    }
    recent_files << path;
  }

  SettingsValueRestorer recent_files_restorer(QStringLiteral("recentFiles"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("recentFiles"), recent_files);
    settings.sync();
  }

  patchy::ui::MainWindow window;
  show_window_empty(window);

  auto* recent_menu = window.findChild<QMenu*>(QStringLiteral("fileOpenRecentMenu"));
  CHECK(recent_menu != nullptr);
  CHECK(!recent_menu->actions().isEmpty());
  auto* filter_action = recent_menu->actions().front();
  CHECK(filter_action->objectName() == QStringLiteral("fileOpenRecentFilterAction"));
  auto* filter_edit = recent_menu->findChild<QLineEdit*>(QStringLiteral("fileOpenRecentFilterEdit"));
  CHECK(filter_edit != nullptr);
  auto* list = recent_menu->findChild<QListWidget*>(QStringLiteral("fileOpenRecentList"));
  CHECK(list != nullptr);

  recent_menu->popup(window.mapToGlobal(QPoint(40, 40)));
  QApplication::processEvents();
  CHECK(filter_edit->text().isEmpty());
  const auto list_height = list->height();
  const auto menu_size = recent_menu->size();

  // Baseline: every entry, newest first and highlighted.
  CHECK(recent_menu_paths(*recent_menu).size() == 120);
  CHECK(list->currentRow() == 0);

  // Typing narrows to matches from the whole list without resizing the menu.
  filter_edit->setText(QStringLiteral("beta"));
  QApplication::processEvents();
  const auto filtered = recent_menu_paths(*recent_menu);
  CHECK(filtered.size() == 60);
  for (const auto& path : filtered) {
    CHECK(path.contains(QStringLiteral("beta")));
  }
  CHECK(filtered.front() == recent_files[1]);
  CHECK(filtered.contains(recent_files[119]));
  CHECK(list->currentRow() == 0);
  CHECK(list->height() == list_height);
  CHECK(recent_menu->size() == menu_size);

  // Space-separated words AND together, matching case-insensitively.
  filter_edit->setText(QStringLiteral("BETA 003"));
  QApplication::processEvents();
  CHECK(recent_menu_paths(*recent_menu) == QStringList{recent_files[3]});

  // Arrow keys move the list highlight while the edit keeps focus.
  filter_edit->setText(QStringLiteral("beta"));
  QApplication::processEvents();
  send_key(*filter_edit, Qt::Key_Down);
  CHECK(list->currentRow() == 1);
  CHECK(list->currentItem()->data(patchy::ui::kRecentPathRole).toString() == recent_files[3]);
  send_key(*filter_edit, Qt::Key_Up);
  CHECK(list->currentRow() == 0);

  // No matches: only the inert placeholder row shows.
  filter_edit->setText(QStringLiteral("zzz-no-such-file"));
  QApplication::processEvents();
  CHECK(recent_menu_paths(*recent_menu).isEmpty());
  CHECK(list->count() == 1);
  CHECK(list->item(0)->text() == QStringLiteral("No matching recent files"));
  CHECK(list->item(0)->flags() == Qt::NoItemFlags);
  CHECK(list->height() == list_height);

  // Right-click on the filter row or the placeholder must not open the context menu.
  const auto filter_point = recent_menu->actionGeometry(filter_action).center();
  QContextMenuEvent filter_context_event(QContextMenuEvent::Mouse, filter_point,
                                         recent_menu->mapToGlobal(filter_point));
  QApplication::sendEvent(recent_menu, &filter_context_event);
  const auto placeholder_point = list->visualItemRect(list->item(0)).center();
  QContextMenuEvent placeholder_context_event(QContextMenuEvent::Mouse, placeholder_point,
                                              list->viewport()->mapToGlobal(placeholder_point));
  QApplication::sendEvent(list->viewport(), &placeholder_context_event);
  QApplication::processEvents();
  CHECK(!top_level_widget_exists(QStringLiteral("recentFileContextMenu")));

  // Clearing the filter restores every entry.
  filter_edit->clear();
  QApplication::processEvents();
  CHECK(recent_menu_paths(*recent_menu).size() == 120);
  CHECK(recent_menu_paths(*recent_menu).front() == recent_files[0]);

  // Enter opens the highlighted row: the second beta match, a real 48 x 35 PNG.
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  CHECK(tabs != nullptr);
  CHECK(tabs->count() == 0);
  filter_edit->setText(QStringLiteral("beta"));
  QApplication::processEvents();
  send_key(*filter_edit, Qt::Key_Down);
  send_key(*filter_edit, Qt::Key_Return);
  QApplication::processEvents();  // the deferred open fires
  QApplication::processEvents();
  CHECK(!recent_menu->isVisible());
  CHECK(tabs->count() == 1);
  auto* info = window.findChild<QLabel*>(QStringLiteral("documentInfoLabel"));
  CHECK(info != nullptr);
  CHECK(info->text().contains(QStringLiteral("48 x 35 px")));
}

// The Open dialog comes back on the format row picked last time instead of the
// all-formats row (GitHub issue 114). Nothing is picked here, so the test seeds
// the remembered row itself and checks the dialog honors it.
void ui_open_dialog_remembers_last_filter() {
  SettingsValueRestorer filter_restorer(QStringLiteral("lastOpenFilter"));
  patchy::ui::app_settings().remove(QStringLiteral("lastOpenFilter"));
  patchy::ui::MainWindow window;
  show_window(window);
  QStringList rows;
  QString selected_first;
  QString selected_second;
  const auto open_and_read = [&](QString* selected) {
    bool answered = false;
    QTimer poll;
    poll.setInterval(10);
    QObject::connect(&poll, &QTimer::timeout, [&] {
      auto* dialog = qobject_cast<QFileDialog*>(find_top_level_dialog(QStringLiteral("openFileDialog")));
      if (dialog == nullptr || !dialog->isVisible()) {
        return;
      }
      poll.stop();
      rows = dialog->nameFilters();
      *selected = dialog->selectedNameFilter();
      answered = true;
      dialog->reject();
    });
    poll.start();
    require_action(window, "fileOpenAction")->trigger();
    process_events_until([&] { return answered; }, 10000);
    poll.stop();
    CHECK(answered);
  };
  open_and_read(&selected_first);
  CHECK(rows.size() > 2);
  if (rows.size() <= 2) {
    return;
  }
  // Without a remembered row the all-formats row leads.
  CHECK(selected_first == rows.front());
  patchy::ui::app_settings().setValue(QStringLiteral("lastOpenFilter"), rows[1]);
  open_and_read(&selected_second);
  CHECK(selected_second == rows[1]);
  // A cancelled dialog leaves the remembered row alone.
  CHECK(patchy::ui::app_settings().value(QStringLiteral("lastOpenFilter")).toString() == rows[1]);
}

// A failed open names the file above the reason, so a multi-file import that
// trips on one file says which (GitHub issue 112).
void ui_open_failed_box_names_the_file() {
  ensure_artifact_dir();
  const auto path = QFileInfo(QStringLiteral("test-artifacts/not-really-an-image.png")).absoluteFilePath();
  {
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write("this is not a PNG");
  }
  patchy::ui::MainWindow window;
  show_window(window);
  QString box_text;
  bool answered = false;
  QTimer poll;
  poll.setInterval(10);
  QObject::connect(&poll, &QTimer::timeout, [&] {
    auto* box = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("openFailedMessageBox")));
    if (box == nullptr || !box->isVisible()) {
      return;
    }
    poll.stop();
    box_text = box->text();
    answered = true;
    box->reject();
  });
  poll.start();
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  process_events_until([&] { return answered; }, 10000);
  poll.stop();
  CHECK(answered);
  // The full path, not just the name: a batch open of many files needs to say which one
  // failed and where it lives (GitHub issues 112 and 117).
  CHECK(box_text.startsWith(QDir::toNativeSeparators(path)));
  CHECK(box_text.contains(QStringLiteral("not-really-an-image.png")));
  CHECK(box_text.size() > QDir::toNativeSeparators(path).size() + 2);
}

void ui_open_recent_folders_list_two_hundred_folders_with_filter() {
  ensure_artifact_dir();
  QStringList recent_folders;
  for (int i = 0; i < 205; ++i) {
    const auto folder =
        QFileInfo(QStringLiteral("test-artifacts/recent-folder-%1").arg(i, 3, 10, QLatin1Char('0')))
            .absoluteFilePath();
    CHECK(QDir().mkpath(folder));
    recent_folders << folder;
  }

  SettingsValueRestorer recent_folders_restorer(QStringLiteral("recentFolders"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("recentFolders"), recent_folders);
    settings.sync();
  }

  patchy::ui::MainWindow window;
  show_window(window);

  // Open Recent Folder matches Open Recent File: a filter row over one scrolling list
  // of every entry (the persisted cap is 200), no paged submenus or per-folder rows.
  auto* folders_menu = window.findChild<QMenu*>(QStringLiteral("fileOpenRecentFolderMenu"));
  CHECK(folders_menu != nullptr);
  CHECK(folders_menu->actions().size() >= 2);
  CHECK(folders_menu->actions()[0]->objectName() == QStringLiteral("fileOpenRecentFolderFilterAction"));
  CHECK(folders_menu->actions()[1]->objectName() == QStringLiteral("fileOpenRecentFolderListAction"));
  auto* filter_edit = folders_menu->findChild<QLineEdit*>(QStringLiteral("fileOpenRecentFolderFilterEdit"));
  CHECK(filter_edit != nullptr);
  CHECK(filter_edit->placeholderText() == QStringLiteral("Filter recent folders..."));
  auto* list = folders_menu->findChild<QListWidget*>(QStringLiteral("fileOpenRecentFolderList"));
  CHECK(list != nullptr);
  const auto all_paths = recent_menu_paths(*folders_menu);
  CHECK(all_paths.size() == 200);
  for (int i = 0; i < all_paths.size(); ++i) {
    CHECK(all_paths[i] == recent_folders[i]);
  }
  CHECK(!all_paths.contains(recent_folders[200]));
  for (auto* action : folders_menu->actions()) {
    CHECK(action->menu() == nullptr);
    CHECK(action->data().toString().isEmpty());
  }
  CHECK(folders_menu->actions().contains(require_action(window, "fileClearRecentFoldersAction")));
  // Two-line rows: the folder's name over its parent directory.
  auto* first_row = recent_menu_list_item(*folders_menu, recent_folders[0]);
  CHECK(first_row != nullptr);
  CHECK(first_row->text() == QStringLiteral("recent-folder-000"));
  CHECK(first_row->toolTip() == QDir::toNativeSeparators(recent_folders[0]));

  folders_menu->popup(window.mapToGlobal(QPoint(40, 40)));
  QApplication::processEvents();
  CHECK(list->isVisible());
  CHECK(list->verticalScrollBar()->maximum() > 0);
  CHECK(list->height() <= 12 * 40 + 14);
  CHECK(folders_menu->width() <= 640);
  if (auto* screen = QApplication::primaryScreen(); screen != nullptr) {
    CHECK(folders_menu->height() <= screen->availableGeometry().height());
  }
  save_widget_artifact("ui_open_recent_folder_scrolling_list", *folders_menu);
  const auto list_height = list->height();

  // The filter narrows the whole list without resizing it; arrows move the highlight.
  filter_edit->setText(QStringLiteral("folder-05"));
  QApplication::processEvents();
  const auto filtered = recent_menu_paths(*folders_menu);
  CHECK(filtered.size() == 10);
  CHECK(filtered.front() == recent_folders[50]);
  CHECK(filtered.back() == recent_folders[59]);
  CHECK(list->height() == list_height);
  CHECK(list->currentRow() == 0);
  send_key(*filter_edit, Qt::Key_Down);
  CHECK(list->currentItem()->data(patchy::ui::kRecentPathRole).toString() == recent_folders[51]);

  filter_edit->setText(QStringLiteral("zzz-no-such-folder"));
  QApplication::processEvents();
  CHECK(recent_menu_paths(*folders_menu).isEmpty());
  CHECK(list->count() == 1);
  CHECK(list->item(0)->text() == QStringLiteral("No matching recent folders"));

  // Right-click on a row offers Copy Folder Path.
  filter_edit->setText(QStringLiteral("folder-05"));
  QApplication::processEvents();
  QApplication::clipboard()->clear();
  auto* row = recent_menu_list_item(*folders_menu, recent_folders[50]);
  CHECK(row != nullptr);
  bool saw_context_menu = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* menu = qobject_cast<QMenu*>(widget);
      if (menu == nullptr || menu->objectName() != QStringLiteral("recentFileContextMenu")) {
        continue;
      }
      auto* copy_action = find_menu_action_by_text(*menu, QStringLiteral("Copy Folder Path"));
      CHECK(copy_action != nullptr);
      CHECK(copy_action->objectName() == QStringLiteral("recentFolderCopyPathAction"));
      copy_action->trigger();
      menu->close();
      saw_context_menu = true;
      return;
    }
    CHECK(false);
  });
  const auto context_point = list->visualItemRect(row).center();
  QContextMenuEvent context_event(QContextMenuEvent::Mouse, context_point,
                                  list->viewport()->mapToGlobal(context_point));
  QApplication::sendEvent(list->viewport(), &context_event);
  QApplication::processEvents();
  CHECK(saw_context_menu);
  CHECK(QApplication::clipboard()->text() == QDir::toNativeSeparators(recent_folders[50]));

  // Enter opens the Open dialog in the highlighted folder, after the menu closes.
  folders_menu->popup(window.mapToGlobal(QPoint(40, 40)));
  QApplication::processEvents();
  filter_edit = folders_menu->findChild<QLineEdit*>(QStringLiteral("fileOpenRecentFolderFilterEdit"));
  CHECK(filter_edit != nullptr);
  filter_edit->setText(QStringLiteral("folder-05"));
  QApplication::processEvents();
  send_key(*filter_edit, Qt::Key_Down);
  const auto opened_in = answer_deferred_open_dialog([&] {
    send_key(*filter_edit, Qt::Key_Return);
    CHECK(!folders_menu->isVisible());
  });
  CHECK(opened_in == QFileInfo(recent_folders[51]).absoluteFilePath());
}

void ui_recent_file_context_menu_copies_path() {
  ensure_artifact_dir();
  const auto first_path = QFileInfo(QStringLiteral("test-artifacts/recent-copy-target.psd")).absoluteFilePath();
  {
    QFile first(first_path);
    CHECK(first.open(QIODevice::WriteOnly));
    CHECK(first.write("patchy psd placeholder") > 0);
  }

  SettingsValueRestorer recent_files_restorer(QStringLiteral("recentFiles"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("recentFiles"), QStringList{first_path});
    settings.sync();
  }

  QApplication::clipboard()->clear();

  patchy::ui::MainWindow window;
  show_window(window);

  auto* recent_menu = window.findChild<QMenu*>(QStringLiteral("fileOpenRecentMenu"));
  CHECK(recent_menu != nullptr);
  CHECK(recent_menu->contextMenuPolicy() == Qt::CustomContextMenu);
  CHECK(!recent_menu->actions().isEmpty());
  // The file entry is a row of the menu's scrolling list.
  auto* recent_list = recent_menu->findChild<QListWidget*>(QStringLiteral("fileOpenRecentList"));
  CHECK(recent_list != nullptr);
  auto* recent_row = recent_menu_list_item(*recent_menu, first_path);
  CHECK(recent_row != nullptr);

  recent_menu->popup(window.mapToGlobal(QPoint(40, 40)));
  QApplication::processEvents();

  bool saw_context_menu = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* menu = qobject_cast<QMenu*>(widget);
      if (menu == nullptr || menu->objectName() != QStringLiteral("recentFileContextMenu")) {
        continue;
      }
      auto* copy_action = find_menu_action_by_text(*menu, QStringLiteral("Copy File Path"));
      CHECK(copy_action != nullptr);
      CHECK(copy_action->objectName() == QStringLiteral("recentFileCopyPathAction"));
      auto* explorer_action = find_menu_action_by_text(*menu, QStringLiteral("Open in File Explorer"));
      CHECK(explorer_action != nullptr);
      CHECK(explorer_action->objectName() == QStringLiteral("recentFileOpenInExplorerAction"));
      copy_action->trigger();
      menu->close();
      saw_context_menu = true;
      return;
    }
    CHECK(false);
  });

  const auto context_point = recent_list->visualItemRect(recent_row).center();
  QContextMenuEvent context_event(QContextMenuEvent::Mouse, context_point,
                                  recent_list->viewport()->mapToGlobal(context_point));
  QApplication::sendEvent(recent_list->viewport(), &context_event);
  QApplication::processEvents();

  CHECK(saw_context_menu);
  CHECK(QApplication::clipboard()->text() == QDir::toNativeSeparators(first_path));
  recent_menu->close();
}

void ui_recent_folder_context_menu_copies_path_and_offers_explorer() {
  ensure_artifact_dir();
  const auto folder = QFileInfo(QStringLiteral("test-artifacts/recent-folder-context")).absoluteFilePath();
  CHECK(QDir().mkpath(folder));

  SettingsValueRestorer recent_folders_restorer(QStringLiteral("recentFolders"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("recentFolders"), QStringList{folder});
    settings.sync();
  }

  QApplication::clipboard()->clear();

  patchy::ui::MainWindow window;
  show_window(window);

  auto* folders_menu = window.findChild<QMenu*>(QStringLiteral("fileOpenRecentFolderMenu"));
  CHECK(folders_menu != nullptr);
  CHECK(folders_menu->contextMenuPolicy() == Qt::CustomContextMenu);
  CHECK(!folders_menu->actions().isEmpty());
  auto* folder_list = folders_menu->findChild<QListWidget*>(QStringLiteral("fileOpenRecentFolderList"));
  CHECK(folder_list != nullptr);
  auto* folder_row = recent_menu_list_item(*folders_menu, folder);
  CHECK(folder_row != nullptr);

  folders_menu->popup(window.mapToGlobal(QPoint(40, 40)));
  QApplication::processEvents();

  bool saw_context_menu = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* menu = qobject_cast<QMenu*>(widget);
      if (menu == nullptr || menu->objectName() != QStringLiteral("recentFileContextMenu")) {
        continue;
      }
      auto* copy_action = find_menu_action_by_text(*menu, QStringLiteral("Copy Folder Path"));
      CHECK(copy_action != nullptr);
      CHECK(copy_action->objectName() == QStringLiteral("recentFolderCopyPathAction"));
      // Confirm the Explorer option is present, but do not trigger it (it would spawn explorer.exe).
      auto* explorer_action = find_menu_action_by_text(*menu, QStringLiteral("Open in File Explorer"));
      CHECK(explorer_action != nullptr);
      CHECK(explorer_action->objectName() == QStringLiteral("recentFolderOpenInExplorerAction"));
      copy_action->trigger();
      menu->close();
      saw_context_menu = true;
      return;
    }
    CHECK(false);
  });

  const auto context_point = folder_list->visualItemRect(folder_row).center();
  QContextMenuEvent context_event(QContextMenuEvent::Mouse, context_point,
                                  folder_list->viewport()->mapToGlobal(context_point));
  QApplication::sendEvent(folder_list->viewport(), &context_event);
  QApplication::processEvents();

  CHECK(saw_context_menu);
  CHECK(QApplication::clipboard()->text() == QDir::toNativeSeparators(folder));
  folders_menu->close();
}

void ui_save_as_remembers_last_save_directory_between_windows() {
  ensure_artifact_dir();
  const auto remembered_dir = QFileInfo(QStringLiteral("test-artifacts/remembered-save-dir")).absoluteFilePath();
  CHECK(QDir().mkpath(remembered_dir));
  const auto saved_path = QDir(remembered_dir).filePath(QStringLiteral("remembered-save.psd"));
  QFile::remove(saved_path);

  SettingsValueRestorer last_save_directory_restorer(QStringLiteral("lastSaveDirectory"));
  SettingsValueRestorer recent_files_restorer(QStringLiteral("recentFiles"));

  {
    patchy::ui::MainWindow window;
    show_window(window);
    bool saved = false;
    QTimer::singleShot(0, [&] {
      auto* dialog = qobject_cast<QFileDialog*>(find_top_level_dialog(QStringLiteral("saveAsFileDialog")));
      CHECK(dialog != nullptr);
      dialog->setDirectory(remembered_dir);
      dialog->selectFile(saved_path);
      saved = true;
      static_cast<QDialog*>(dialog)->accept();
    });
    require_action(window, "fileSaveAsAction")->trigger();
    CHECK(saved);
    CHECK(QFileInfo::exists(saved_path));
  }

  {
    auto settings = patchy::ui::app_settings();
    CHECK(QFileInfo(settings.value(QStringLiteral("lastSaveDirectory")).toString()).absoluteFilePath() ==
          QFileInfo(remembered_dir).absoluteFilePath());
  }

  patchy::ui::MainWindow next_window;
  show_window(next_window);

  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QFileDialog*>(find_top_level_dialog(QStringLiteral("saveAsFileDialog")));
    CHECK(dialog != nullptr);
    CHECK(QFileInfo(dialog->directory().absolutePath()).absoluteFilePath() ==
          QFileInfo(remembered_dir).absoluteFilePath());
    const auto selected_files = dialog->selectedFiles();
    CHECK(!selected_files.isEmpty());
    CHECK(QFileInfo(selected_files.first()).absolutePath() == QFileInfo(remembered_dir).absoluteFilePath());
    saw_dialog = true;
    dialog->reject();
  });
  require_action(next_window, "fileSaveAsAction")->trigger();
  CHECK(saw_dialog);
}

void ui_open_remembers_last_directory_and_lists_recent_folders() {
  ensure_artifact_dir();
  const auto folder_a = QFileInfo(QStringLiteral("test-artifacts/recent-open-dir-a")).absoluteFilePath();
  const auto folder_b = QFileInfo(QStringLiteral("test-artifacts/recent-open-dir-b")).absoluteFilePath();
  const auto missing_folder = QFileInfo(QStringLiteral("test-artifacts/recent-open-dir-missing")).absoluteFilePath();
  CHECK(QDir().mkpath(folder_a));
  CHECK(QDir().mkpath(folder_b));
  remove_test_scratch_dir(missing_folder);

  SettingsValueRestorer last_open_directory_restorer(QStringLiteral("lastOpenDirectory"));
  SettingsValueRestorer recent_folders_restorer(QStringLiteral("recentFolders"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("lastOpenDirectory"), folder_a);
    // Include a stale entry to confirm self-healing drops folders that no longer exist.
    settings.setValue(QStringLiteral("recentFolders"), QStringList{folder_a, missing_folder, folder_b});
    settings.sync();
  }

  patchy::ui::MainWindow window;
  show_window(window);

  auto* folders_menu = window.findChild<QMenu*>(QStringLiteral("fileOpenRecentFolderMenu"));
  CHECK(folders_menu != nullptr);
  const auto listed_folders = [folders_menu] { return recent_menu_paths(*folders_menu); };
  // The stale entry drops once the background existence check reports back.
  CHECK(process_events_until([&] { return listed_folders() == QStringList({folder_a, folder_b}); }));
  CHECK(folders_menu->actions().contains(require_action(window, "fileClearRecentFoldersAction")));

  // The Open dialog starts in the remembered directory.
  bool saw_open_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QFileDialog*>(find_top_level_dialog(QStringLiteral("openFileDialog")));
    CHECK(dialog != nullptr);
    CHECK(QFileInfo(dialog->directory().absolutePath()).absoluteFilePath() ==
          QFileInfo(folder_a).absoluteFilePath());
    saw_open_dialog = true;
    dialog->reject();
  });
  require_action(window, "fileOpenAction")->trigger();
  CHECK(saw_open_dialog);

  // Picking a recent folder opens the dialog pointed at that folder.
  auto* folder_list = folders_menu->findChild<QListWidget*>(QStringLiteral("fileOpenRecentFolderList"));
  CHECK(folder_list != nullptr);
  auto* folder_b_row = recent_menu_list_item(*folders_menu, folder_b);
  CHECK(folder_b_row != nullptr);
  const auto opened_in = answer_deferred_open_dialog([&] { emit folder_list->itemClicked(folder_b_row); });
  CHECK(opened_in == QFileInfo(folder_b).absoluteFilePath());

  // Clearing empties both the menu and the persisted list.
  require_action(window, "fileClearRecentFoldersAction")->trigger();
  QApplication::processEvents();
  CHECK(recent_menu_paths(*folders_menu).isEmpty());
  CHECK(!folders_menu->isEnabled());
  {
    auto settings = patchy::ui::app_settings();
    CHECK(settings.value(QStringLiteral("recentFolders")).toStringList().isEmpty());
  }
}

// Recent-history existence checks run on a worker and skip network paths: an
// unreachable share used to block startup and every File menu open for the SMB
// timeout. The unroutable TEST-NET host would hold a stat far past the wait
// below, so the missing local entry dropping in time proves the share was not
// stat'ed, and the share entry stays listed.
// The mount classifier behind is_network_recent_path on macOS and Linux: a
// synthetic table, so the result does not depend on the machine's mounts.
void ui_network_mount_classification_uses_deepest_mount_point() {
  using patchy::ui::MountEntry;
  const std::vector<MountEntry> mounts{
      {QStringLiteral("/"), QStringLiteral("apfs"), true},
      {QStringLiteral("/Volumes/share"), QStringLiteral("smbfs"), false},
      {QStringLiteral("/Volumes/share2"), QStringLiteral("apfs"), true},
      {QStringLiteral("/Volumes/share/nested-local"), QStringLiteral("apfs"), true},
      {QStringLiteral("/mnt/nas/"), QStringLiteral("nfs4"), true},  // Linux: the type decides
      {QStringLiteral("/run/user/1000/gvfs"), QStringLiteral("fuse.gvfsd-fuse"), true},
      {QStringLiteral("/net"), QStringLiteral("autofs"), true},
      {QStringLiteral("/media/usb"), QStringLiteral("fuse.ntfs-3g"), true},
  };
  const auto network = [&](const char* path) {
    return patchy::ui::path_is_on_network_mount(QString::fromUtf8(path), mounts);
  };
  CHECK(!network("/Users/seth/Pictures/a.psd"));
  CHECK(network("/Volumes/share/a.psd"));
  CHECK(network("/Volumes/share"));
  CHECK(!network("/Volumes/share2/a.psd"));   // sibling with a common prefix
  CHECK(!network("/Volumes/sharex/a.psd"));   // no component boundary match
  CHECK(!network("/Volumes/share/nested-local/a.psd"));  // deeper local mount wins
  CHECK(network("/mnt/nas/photos/a.psd"));    // trailing slash on the mount point
  CHECK(network("/run/user/1000/gvfs/smb-share:server=nas/a.psd"));
  CHECK(network("/net/box/a.psd"));           // automount trigger
  CHECK(!network("/media/usb/a.psd"));        // local FUSE stays local
  CHECK(!network(""));
  CHECK(!patchy::ui::path_is_on_network_mount(QStringLiteral("/anything"), {}));

  const auto* entry = patchy::ui::mount_for_path(QStringLiteral("/Volumes/share/x/y"), mounts);
  CHECK(entry != nullptr && entry->file_system == QStringLiteral("smbfs"));
  CHECK(patchy::ui::is_network_file_system_type(QStringLiteral("cifs")));
  CHECK(patchy::ui::is_network_file_system_type(QStringLiteral("fuse.sshfs")));
  CHECK(!patchy::ui::is_network_file_system_type(QStringLiteral("ext4")));
  CHECK(!patchy::ui::is_network_file_system_type(QStringLiteral("fuse.portal")));

  // /proc/self/mounts escapes spaces in mount points as \040.
  const auto parsed = patchy::ui::parse_proc_mounts(
      "proc /proc proc rw,nosuid 0 0\n"
      "/dev/sda1 / ext4 rw,relatime 0 0\n"
      "nas:/export /mnt/my\\040nas nfs4 rw,vers=4.2 0 0\n"
      "gvfsd-fuse /run/user/1000/gvfs fuse.gvfsd-fuse rw,nosuid,nodev,user_id=1000 0 0\n"
      "broken line\n");
  CHECK(parsed.size() == 4);
  if (parsed.size() == 4) {
    CHECK(parsed[1].mount_point == QStringLiteral("/"));
    CHECK(parsed[1].local);
    CHECK(parsed[2].mount_point == QStringLiteral("/mnt/my nas"));
    CHECK(parsed[2].file_system == QStringLiteral("nfs4"));
    CHECK(!parsed[2].local);
    CHECK(!parsed[3].local);
  }
  CHECK(patchy::ui::path_is_on_network_mount(QStringLiteral("/mnt/my nas/a.psd"), parsed));
  CHECK(!patchy::ui::path_is_on_network_mount(QStringLiteral("/home/x/a.psd"), parsed));

  // The live table never blocks and, off Windows, always knows the root.
  const auto live = patchy::ui::read_system_mounts();
#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
  CHECK(!live.empty());
  CHECK(patchy::ui::mount_for_path(QStringLiteral("/"), live) != nullptr);
#else
  CHECK(live.empty());
#endif
}

}  // namespace

std::vector<patchy::test::TestCase> app_shell_tests_part1() {
  return {
      {"ui_derived_font_sizes_scale_in_points_and_pixels", ui_derived_font_sizes_scale_in_points_and_pixels},
      {"ui_startup_opens_empty_workspace_with_start_panel", ui_startup_opens_empty_workspace_with_start_panel},
      {"ui_start_panel_recent_files_open_on_click", ui_start_panel_recent_files_open_on_click},
      {"ui_large_document_session_keeps_loading_responsive", ui_large_document_session_keeps_loading_responsive},
      {"ui_start_panel_recent_files_scroll_and_context_menu", ui_start_panel_recent_files_scroll_and_context_menu},
      {"ui_start_panel_recent_row_remove_button_and_menu", ui_start_panel_recent_row_remove_button_and_menu},
      {"ui_open_recent_menu_remove_keeps_place", ui_open_recent_menu_remove_keeps_place},
      {"ui_empty_workspace_disables_document_commands", ui_empty_workspace_disables_document_commands},
      {"ui_start_panel_recent_filter_narrows_rows_and_opens_match", ui_start_panel_recent_filter_narrows_rows_and_opens_match},
      {"ui_start_panel_shows_about_info_and_update_status", ui_start_panel_shows_about_info_and_update_status},
      {"ui_main_window_renders_color_controls", ui_main_window_renders_color_controls},
      {"ui_layer_panel_new_layer_menu_lists_fills_and_adjustments", ui_layer_panel_new_layer_menu_lists_fills_and_adjustments},
      {"ui_window_menu_lists_panels_and_toolbars", ui_window_menu_lists_panels_and_toolbars},
      {"ui_window_menu_hides_collapsed_panel", ui_window_menu_hides_collapsed_panel},
      {"ui_panel_close_and_reopen_keeps_full_width", ui_panel_close_and_reopen_keeps_full_width},
      {"ui_panel_collapse_and_close_leave_no_gap", ui_panel_collapse_and_close_leave_no_gap},
      {"ui_panel_title_drag_reorders_sections", ui_panel_title_drag_reorders_sections},
      {"ui_panel_header_right_click_opens_panel_menu", ui_panel_header_right_click_opens_panel_menu},
      {"ui_panel_layout_returns_in_the_next_window", ui_panel_layout_returns_in_the_next_window},
      {"ui_reset_panels_restores_first_launch_layout", ui_reset_panels_restores_first_launch_layout},
      {"ui_reset_panels_keeps_the_window_geometry", ui_reset_panels_keeps_the_window_geometry},
      {"ui_tool_palette_quick_mask_button_shows_its_icon", ui_tool_palette_quick_mask_button_shows_its_icon},
      {"ui_tool_palette_overflow_hides_quick_mask_before_swatches", ui_tool_palette_overflow_hides_quick_mask_before_swatches},
      {"ui_tool_palette_extension_survives_resize_cycles", ui_tool_palette_extension_survives_resize_cycles},
      {"ui_tool_palette_extension_button_expands_palette", ui_tool_palette_extension_button_expands_palette},
      {"ui_tool_palette_expansion_keeps_the_document_centered", ui_tool_palette_expansion_keeps_the_document_centered},
      {"ui_tool_palette_expansion_keeps_columns_tight_and_aligned", ui_tool_palette_expansion_keeps_columns_tight_and_aligned},
      {"ui_tool_palette_expansion_sticky_after_tool_pick", ui_tool_palette_expansion_sticky_after_tool_pick},
      {"ui_tool_palette_expanded_zoom_double_click_sets_actual_pixels", ui_tool_palette_expanded_zoom_double_click_sets_actual_pixels},
      {"ui_tool_palette_animated_collapse_realigns_buttons", ui_tool_palette_animated_collapse_realigns_buttons},
      {"ui_tool_palette_expansion_collapses_when_window_fits_again", ui_tool_palette_expansion_collapses_when_window_fits_again},
      {"ui_dissolve_clipped_render_matches_full_render", ui_dissolve_clipped_render_matches_full_render},
      {"ui_window_force_refresh_action_rebuilds_cache", ui_window_force_refresh_action_rebuilds_cache},
      {"ui_canvas_ignores_opaque_psd_flat_cache_for_first_paint_transparency", ui_canvas_ignores_opaque_psd_flat_cache_for_first_paint_transparency},
      {"ui_top_menu_items_highlight_on_hover", ui_top_menu_items_highlight_on_hover},
      {"ui_save_as_dialog_lists_recent_files", ui_save_as_dialog_lists_recent_files},
      {"ui_open_recent_lists_two_hundred_files_in_one_scrolling_list", ui_open_recent_lists_two_hundred_files_in_one_scrolling_list},
      {"ui_open_recent_filter_narrows_entries_and_opens_first_match", ui_open_recent_filter_narrows_entries_and_opens_first_match},
      {"ui_open_dialog_remembers_last_filter", ui_open_dialog_remembers_last_filter},
      {"ui_open_failed_box_names_the_file", ui_open_failed_box_names_the_file},
      {"ui_open_recent_folders_list_two_hundred_folders_with_filter", ui_open_recent_folders_list_two_hundred_folders_with_filter},
      {"ui_recent_file_context_menu_copies_path", ui_recent_file_context_menu_copies_path},
      {"ui_recent_folder_context_menu_copies_path_and_offers_explorer", ui_recent_folder_context_menu_copies_path_and_offers_explorer},
      {"ui_save_as_remembers_last_save_directory_between_windows", ui_save_as_remembers_last_save_directory_between_windows},
      {"ui_open_remembers_last_directory_and_lists_recent_folders", ui_open_remembers_last_directory_and_lists_recent_folders},
      {"ui_network_mount_classification_uses_deepest_mount_point", ui_network_mount_classification_uses_deepest_mount_point},
  };
}
