#include "ui/canvas_widget.hpp"
#include "core/adjustment_layer.hpp"
#include "core/contour_presets.hpp"
#include "core/gradient_presets.hpp"
#include "core/layer_metadata.hpp"
#include "core/pattern_presets.hpp"
#include "core/smart_filter.hpp"
#include "core/smart_filter_effects.hpp"
#include "core/smart_object.hpp"
#include "core/text_warp.hpp"
#include "ui/smart_object_render.hpp"
#include "core/layer_tree.hpp"
#include "core/palette.hpp"
#include "core/palette_presets.hpp"
#include "ui/palette_panel.hpp"
#include "ui/pattern_library.hpp"
#include "ui/pattern_manager_dialog.hpp"
#include "ui/photo_pattern_presets.hpp"
#include "ui/style_browser.hpp"
#include "ui/style_library.hpp"
#include "ui/style_manager_dialog.hpp"
#include "psd/asl_io.hpp"
#include "psd/psd_binary.hpp"
#include "psd/psd_layer_effects.hpp"
#include "core/style_presets.hpp"
#include "ui/brush_tip_library.hpp"
#include "ui/brush_tip_manager_dialog.hpp"
#include "ui/brush_tip_picker.hpp"
#include "ui/blend_if_range_editor.hpp"
#include "ui/color_panel.hpp"
#include "ui/default_brush_tips.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/document_float_window.hpp"
#include "ui/compatibility_report.hpp"
#include "ui/curves_editor.hpp"
#include "ui/curves_presets.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/filter_look_library.hpp"
#include "ui/font_picker.hpp"
#include "ui/gradient_stops_editor.hpp"
#include "ui/gradient_library.hpp"
#include "ui/gradient_manager_dialog.hpp"
#include "formats/acv_curves_io.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/ico_document_io.hpp"
#include "formats/tga_document_io.hpp"
#include "ui/image_document_io.hpp"
#include "ui/image_save_options_dialog.hpp"
#include "ui/layer_list_widget.hpp"
#include "ui/layer_style_dialog.hpp"
#include "ui/localization.hpp"
#include "ui/main_window.hpp"
#include "ui/print_dialog.hpp"
#include "ui/selection_outline.hpp"
#include "ui/sprite_sheet_dialog.hpp"
#include "ui/splash_dialog.hpp"
#include "ui/app_settings.hpp"
#include "ui/update_checker.hpp"
#include "ui/visual_filter_gallery_dialog.hpp"
#include "ui/zoomable_image_preview.hpp"
#include "ui/zoom_status_bar.hpp"
#include "filters/builtin_filters.hpp"
#include "psd/psd_document_io.hpp"
#include "psd/psd_filter_effects.hpp"
#include "render/compositor.hpp"
#include "synthetic_dng.hpp"
#include "test_fonts.hpp"
#include "test_harness.hpp"
#include "local_psd_fixtures.hpp"

#include <QAbstractItemModel>
#include <QAbstractSpinBox>
#include <QAbstractItemView>
#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QByteArray>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDataStream>
#include <QDockWidget>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFrame>
#include <QGroupBox>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QInputDevice>
#include <QInputDialog>
#include <QKeyEvent>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QListView>
#include <QLayout>
#include <QListWidget>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QLocale>
#include <QSizeGrip>
#include <QMetaObject>
#include <QMouseEvent>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QMessageBox>
#include <QIODevice>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPolygonF>
#include <QThread>
#include <QPaintEvent>
#include <QPixmap>
#include <QPointingDevice>
#include <QProgressDialog>
#include <QPushButton>
#include <QStackedWidget>
#include <QRadioButton>
#include <QSpinBox>
#include <QStringList>
#include <QScrollBar>
#include <QScreen>
#include <QSettings>
#include <QSlider>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QStyleOptionSpinBox>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTabletEvent>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextLayout>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVariant>
#include <QWheelEvent>
#include <QWindow>
#include <QWidget>

#include <algorithm>
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
#include <utility>
#include <vector>

#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

namespace {

using namespace patchy::test::ui;

// ===========================================================================
// Float windows (Window > Float in Window)
// ===========================================================================

patchy::Document make_float_test_document(QColor color, int width = 64, int height = 48) {
  patchy::Document document(width, height, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(width, height, patchy::PixelFormat::rgba8(), color));
  return document;
}

QWidget* find_document_float_window(patchy::ui::MainWindow& window) {
  return window.findChild<QWidget*>(QStringLiteral("documentFloatWindow"));
}

// deleteLater from dock/close is only collected once deferred deletes run.
void flush_deferred_deletes() {
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QApplication::processEvents();
}

void ui_float_document_window_hosts_canvas_and_redocks() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(tabs != nullptr);
  window.add_document_session(make_float_test_document(QColor(50, 80, 140)), QStringLiteral("Floaty"));
  QApplication::processEvents();
  CHECK(tabs->count() == 2);
  CHECK(tabs->currentIndex() == 1);
  auto* float_canvas = patchy::ui::MainWindowTestAccess::canvas(window);
  CHECK(float_canvas == tabs->widget(1));

  CHECK(require_action(window, "windowFloatDocumentAction")->isEnabled());
  CHECK(!require_action(window, "windowDockDocumentAction")->isEnabled());
  CHECK(!require_action(window, "windowConsolidateTabsAction")->isEnabled());

  require_action(window, "windowFloatDocumentAction")->trigger();
  QApplication::processEvents();

  CHECK(tabs->count() == 1);
  auto* float_window = find_document_float_window(window);
  CHECK(float_window != nullptr);
  CHECK(float_window->isWindow());
  CHECK(float_window->isVisible());
  CHECK(float_window->windowTitle() == QStringLiteral("Floaty"));
  CHECK(float_window->isAncestorOf(float_canvas));
  CHECK(patchy::ui::MainWindowTestAccess::canvas(window) == float_canvas);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_is_floated(window));
  CHECK(patchy::ui::MainWindowTestAccess::session_count(window) == 2);
  CHECK(!require_action(window, "windowFloatDocumentAction")->isEnabled());
  CHECK(require_action(window, "windowDockDocumentAction")->isEnabled());
  CHECK(require_action(window, "windowConsolidateTabsAction")->isEnabled());
  // Registry shortcuts must fire while the float is the active window: the
  // actions are associated with it (Qt::WindowShortcut matches any associated
  // window; ApplicationShortcut would leak into modal dialogs instead).
  CHECK(float_window->actions().contains(require_action(window, "fileSaveAction")));
  CHECK(float_window->actions().contains(require_action(window, "fileNewAction")));

  require_action(window, "windowDockDocumentAction")->trigger();
  QApplication::processEvents();
  flush_deferred_deletes();
  CHECK(tabs->count() == 2);
  CHECK(tabs->indexOf(float_canvas) == 1);
  CHECK(tabs->currentWidget() == float_canvas);
  CHECK(patchy::ui::MainWindowTestAccess::canvas(window) == float_canvas);
  CHECK(!patchy::ui::MainWindowTestAccess::active_session_is_floated(window));
  CHECK(find_document_float_window(window) == nullptr);
  CHECK(require_action(window, "windowFloatDocumentAction")->isEnabled());
  CHECK(!require_action(window, "windowDockDocumentAction")->isEnabled());
}

void ui_float_window_preserves_channel_target_per_canvas() {
  const auto make_channel_document = [](QString channel_name, std::uint8_t value) {
    auto document = make_float_test_document(QColor(70, 130, 190));
    patchy::PixelBuffer pixels(document.width(), document.height(), patchy::PixelFormat::gray8());
    pixels.clear(value);
    document.add_channel(patchy::DocumentChannel(document.allocate_channel_id(),
                                                  channel_name.toStdString(),
                                                  patchy::DocumentChannelKind::Alpha,
                                                  std::move(pixels)));
    return document;
  };

  patchy::ui::MainWindow window;
  window.add_document_session(make_channel_document(QStringLiteral("Float Alpha"), 64),
                              QStringLiteral("Floated Channels"));
  show_window(window);
  auto* channels = window.findChild<QListWidget*>(QStringLiteral("channelList"));
  CHECK(channels != nullptr);
  auto* float_canvas = require_canvas(window);
  channels->setCurrentItem(require_layer_item(*channels, QStringLiteral("Float Alpha")));
  QApplication::processEvents();
  const auto float_channel_id = float_canvas->active_document_channel_id();
  CHECK(float_channel_id.has_value());

  require_action(window, "windowFloatDocumentAction")->trigger();
  QApplication::processEvents();
  CHECK(find_document_float_window(window) != nullptr);
  CHECK(patchy::ui::MainWindowTestAccess::canvas(window) == float_canvas);

  window.add_document_session(make_channel_document(QStringLiteral("Tab Alpha"), 192),
                              QStringLiteral("Tabbed Channels"));
  QApplication::processEvents();
  auto* tab_canvas = require_canvas(window);
  CHECK(tab_canvas != float_canvas);
  channels->setCurrentItem(require_layer_item(*channels, QStringLiteral("Green")));
  QApplication::processEvents();
  CHECK(tab_canvas->layer_edit_target() ==
        patchy::ui::CanvasWidget::LayerEditTarget::ComponentGreen);

  patchy::ui::MainWindowTestAccess::activate_canvas(window, float_canvas);
  QApplication::processEvents();
  CHECK(patchy::ui::MainWindowTestAccess::canvas(window) == float_canvas);
  CHECK(float_canvas->active_document_channel_id() == float_channel_id);
  CHECK(channels->currentItem() != nullptr);
  CHECK(channels->currentItem()->text() == QStringLiteral("Float Alpha"));

  patchy::ui::MainWindowTestAccess::activate_canvas(window, tab_canvas);
  QApplication::processEvents();
  CHECK(patchy::ui::MainWindowTestAccess::canvas(window) == tab_canvas);
  CHECK(tab_canvas->layer_edit_target() ==
        patchy::ui::CanvasWidget::LayerEditTarget::ComponentGreen);
  CHECK(channels->currentItem() != nullptr);
  CHECK(channels->currentItem()->text() == QStringLiteral("Green"));

  patchy::ui::MainWindowTestAccess::activate_canvas(window, float_canvas);
  QApplication::processEvents();
  CHECK(patchy::ui::MainWindowTestAccess::canvas(window) == float_canvas);
  CHECK(float_canvas->active_document_channel_id() == float_channel_id);
}

void ui_float_window_edit_routes_to_owning_session() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(tabs != nullptr);
  window.add_document_session(make_float_test_document(QColor(255, 255, 255)), QStringLiteral("Painted"));
  QApplication::processEvents();
  auto* float_canvas = patchy::ui::MainWindowTestAccess::canvas(window);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  QApplication::processEvents();
  float_canvas->set_primary_color(QColor(200, 30, 30));
  float_canvas->set_brush_size(16);
  float_canvas->set_brush_opacity(100);

  require_action(window, "windowFloatDocumentAction")->trigger();
  QApplication::processEvents();
  auto* tab_canvas = dynamic_cast<patchy::ui::CanvasWidget*>(tabs->currentWidget());
  CHECK(tab_canvas != nullptr);
  CHECK(tab_canvas != float_canvas);

  // The TAB document is active; the stroke lands on the FLOATED canvas.
  patchy::ui::MainWindowTestAccess::activate_canvas(window, tab_canvas);
  QApplication::processEvents();
  CHECK(patchy::ui::MainWindowTestAccess::canvas(window) == tab_canvas);

  const auto tab_undo_before = patchy::ui::MainWindowTestAccess::undo_depth_for_canvas(window, tab_canvas);
  const auto float_undo_before = patchy::ui::MainWindowTestAccess::undo_depth_for_canvas(window, float_canvas);

  drag(*float_canvas, float_canvas->widget_position_for_document_point(QPoint(10, 10)),
       float_canvas->widget_position_for_document_point(QPoint(28, 28)));
  QApplication::processEvents();

  // The edit snapshots the OWNING session's undo stack, never the active one.
  CHECK(patchy::ui::MainWindowTestAccess::undo_depth_for_canvas(window, float_canvas) == float_undo_before + 1);
  CHECK(patchy::ui::MainWindowTestAccess::undo_depth_for_canvas(window, tab_canvas) == tab_undo_before);
  CHECK(color_close(canvas_pixel_center(*float_canvas, QPoint(19, 19)), QColor(200, 30, 30), 24));
}

void ui_float_activation_commits_text_editor_to_owning_document() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(tabs != nullptr);
  auto* first_canvas = require_canvas(window);
  window.add_document_session(make_float_test_document(QColor(240, 240, 240)), QStringLiteral("Other"));
  QApplication::processEvents();
  auto* second_canvas = patchy::ui::MainWindowTestAccess::canvas(window);
  CHECK(second_canvas != first_canvas);
  tabs->setCurrentIndex(0);
  QApplication::processEvents();
  CHECK(patchy::ui::MainWindowTestAccess::canvas(window) == first_canvas);
  auto* first_document = patchy::ui::MainWindowTestAccess::document_for_canvas(window, first_canvas);
  CHECK(first_document != nullptr);
  const auto first_layers_before = first_document->layers().size();

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  QApplication::processEvents();
  const auto center = first_canvas->rect().center();
  send_mouse(*first_canvas, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*first_canvas, QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  auto* editor = first_canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  editor->setPlainText(QStringLiteral("Floaty"));
  QApplication::processEvents();

  // Programmatic activation (the float WindowActivate path) must settle the edit
  // into the OUTGOING document before canvas_ moves.
  patchy::ui::MainWindowTestAccess::activate_canvas(window, second_canvas);
  QApplication::processEvents();

  CHECK(patchy::ui::MainWindowTestAccess::canvas(window) == second_canvas);
  CHECK(first_canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
  CHECK(first_document->layers().size() == first_layers_before + 1);
  bool committed_into_first = false;
  for (const auto& layer : first_document->layers()) {
    if (patchy::layer_is_text(layer)) {
      committed_into_first = true;
    }
  }
  CHECK(committed_into_first);
  auto* second_document = patchy::ui::MainWindowTestAccess::document_for_canvas(window, second_canvas);
  CHECK(second_document != nullptr);
  CHECK(second_document->layers().size() == 1);
}

void ui_float_window_close_prompts_and_closes_document() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(tabs != nullptr);
  window.add_document_session(make_float_test_document(QColor(90, 90, 90)), QStringLiteral("Muta"));
  QApplication::processEvents();
  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();
  require_action(window, "windowFloatDocumentAction")->trigger();
  QApplication::processEvents();
  auto* float_window = find_document_float_window(window);
  CHECK(float_window != nullptr);
  CHECK(patchy::ui::MainWindowTestAccess::session_count(window) == 2);

  const auto dismiss_save_prompt = [&](QMessageBox::StandardButton button, bool& seen) {
    auto* dismiss_timer = new QTimer(&window);
    dismiss_timer->setInterval(10);
    QObject::connect(dismiss_timer, &QTimer::timeout, &window, [&seen, dismiss_timer, button] {
      auto* dialog = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("saveChangesMessageBox")));
      if (dialog == nullptr) {
        return;
      }
      seen = true;
      dismiss_timer->stop();
      dismiss_timer->deleteLater();
      dialog->button(button)->click();
    });
    dismiss_timer->start();
  };

  // Cancel keeps the window and the document.
  bool cancel_prompt_seen = false;
  dismiss_save_prompt(QMessageBox::Cancel, cancel_prompt_seen);
  CHECK(!float_window->close());
  QApplication::processEvents();
  CHECK(cancel_prompt_seen);
  CHECK(patchy::ui::MainWindowTestAccess::session_count(window) == 2);
  CHECK(find_document_float_window(window) == float_window);
  CHECK(float_window->isVisible());

  // No (discard) closes the floated document; activation falls back to the tab.
  bool discard_prompt_seen = false;
  dismiss_save_prompt(QMessageBox::No, discard_prompt_seen);
  CHECK(float_window->close());
  QApplication::processEvents();
  flush_deferred_deletes();
  CHECK(discard_prompt_seen);
  CHECK(patchy::ui::MainWindowTestAccess::session_count(window) == 1);
  CHECK(find_document_float_window(window) == nullptr);
  CHECK(tabs->count() == 1);
  CHECK(patchy::ui::MainWindowTestAccess::canvas(window) == tabs->widget(0));
  CHECK(require_action(window, "fileSaveAction")->isEnabled());
}

void ui_close_all_documents_includes_floats() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(tabs != nullptr);
  window.add_document_session(make_float_test_document(QColor(10, 60, 10)), QStringLiteral("Away"));
  QApplication::processEvents();
  require_action(window, "windowFloatDocumentAction")->trigger();
  QApplication::processEvents();
  CHECK(patchy::ui::MainWindowTestAccess::session_count(window) == 2);
  CHECK(tabs->count() == 1);
  CHECK(find_document_float_window(window) != nullptr);

  require_action(window, "fileCloseAllAction")->trigger();
  QApplication::processEvents();
  flush_deferred_deletes();

  CHECK(patchy::ui::MainWindowTestAccess::session_count(window) == 0);
  CHECK(tabs->count() == 0);
  CHECK(find_document_float_window(window) == nullptr);
  CHECK(!require_action(window, "fileSaveAction")->isEnabled());
  CHECK(require_action(window, "fileNewAction")->isEnabled());
}

void ui_float_window_respects_preview_dialog_edit_lock() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(tabs != nullptr);
  window.add_document_session(make_float_test_document(QColor(200, 200, 200)), QStringLiteral("Locked out"));
  QApplication::processEvents();
  require_action(window, "windowFloatDocumentAction")->trigger();
  QApplication::processEvents();
  auto* float_canvas = patchy::ui::MainWindowTestAccess::canvas(window);
  auto* tab_canvas = dynamic_cast<patchy::ui::CanvasWidget*>(tabs->currentWidget());
  CHECK(float_canvas != nullptr && tab_canvas != nullptr);
  patchy::ui::MainWindowTestAccess::activate_canvas(window, tab_canvas);
  QApplication::processEvents();
  CHECK(patchy::ui::MainWindowTestAccess::canvas(window) == tab_canvas);

  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyFilterDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      CHECK(dialog != nullptr);
      // Every session canvas is locked, not just the active one: the float stays
      // clickable while the dialog is open.
      CHECK(tab_canvas->edit_locked());
      CHECK(float_canvas->edit_locked());
      // Activation of another canvas is refused while locked.
      patchy::ui::MainWindowTestAccess::activate_canvas(window, float_canvas);
      CHECK(patchy::ui::MainWindowTestAccess::canvas(window) == tab_canvas);
      // A paint attempt on the floated canvas is a no-op.
      const auto float_undo_before =
          patchy::ui::MainWindowTestAccess::undo_depth_for_canvas(window, float_canvas);
      drag(*float_canvas, float_canvas->widget_position_for_document_point(QPoint(8, 8)),
           float_canvas->widget_position_for_document_point(QPoint(24, 24)));
      CHECK(patchy::ui::MainWindowTestAccess::undo_depth_for_canvas(window, float_canvas) == float_undo_before);
      CHECK(!require_action(window, "windowFloatDocumentAction")->isEnabled());
      CHECK(!require_action(window, "windowDockDocumentAction")->isEnabled());
      CHECK(!require_action(window, "windowConsolidateTabsAction")->isEnabled());
      saw_dialog = true;
      dialog->reject();
      return;
    }
    CHECK(false);
  });
  require_action(window, "imageAdjustInvertAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);
  CHECK(!tab_canvas->edit_locked());
  CHECK(!float_canvas->edit_locked());
  patchy::ui::MainWindowTestAccess::activate_canvas(window, float_canvas);
  QApplication::processEvents();
  CHECK(patchy::ui::MainWindowTestAccess::canvas(window) == float_canvas);
}

void ui_float_only_document_keeps_actions_enabled() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(tabs != nullptr);
  CHECK(tabs->count() == 1);

  require_action(window, "windowFloatDocumentAction")->trigger();
  QApplication::processEvents();
  CHECK(tabs->count() == 0);
  CHECK(patchy::ui::MainWindowTestAccess::session_count(window) == 1);
  CHECK(patchy::ui::MainWindowTestAccess::canvas(window) != nullptr);
  CHECK(require_action(window, "fileSaveAction")->isEnabled());
  CHECK(require_action(window, "layerNewAction")->isEnabled());

  // A document opened while everything is floated lands as a tab and activates.
  accept_new_document_dialog(160, 120);
  require_action(window, "fileNewAction")->trigger();
  QApplication::processEvents();
  CHECK(tabs->count() == 1);
  CHECK(patchy::ui::MainWindowTestAccess::session_count(window) == 2);
  CHECK(patchy::ui::MainWindowTestAccess::canvas(window) == tabs->widget(0));

  // Float the second one too, then consolidate both back; activation sticks.
  require_action(window, "windowFloatDocumentAction")->trigger();
  QApplication::processEvents();
  CHECK(tabs->count() == 0);
  CHECK(window.findChildren<QWidget*>(QStringLiteral("documentFloatWindow")).size() == 2);
  auto* active_before = patchy::ui::MainWindowTestAccess::canvas(window);

  require_action(window, "windowConsolidateTabsAction")->trigger();
  QApplication::processEvents();
  flush_deferred_deletes();
  CHECK(tabs->count() == 2);
  CHECK(window.findChildren<QWidget*>(QStringLiteral("documentFloatWindow")).isEmpty());
  CHECK(patchy::ui::MainWindowTestAccess::canvas(window) == active_before);
  CHECK(tabs->currentWidget() == active_before);
  CHECK(!require_action(window, "windowConsolidateTabsAction")->isEnabled());
}

void ui_float_window_title_tracks_modified_state() {
  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(make_float_test_document(QColor(140, 60, 60)), QStringLiteral("Titled"));
  QApplication::processEvents();
  require_action(window, "windowFloatDocumentAction")->trigger();
  QApplication::processEvents();
  auto* float_window = find_document_float_window(window);
  CHECK(float_window != nullptr);
  CHECK(float_window->windowTitle() == QStringLiteral("Titled"));

  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();
  CHECK(float_window->windowTitle() == QStringLiteral("Titled*"));
}

void ui_float_window_activation_switches_panels() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(tabs != nullptr);
  CHECK(layer_list != nullptr);
  auto* first_canvas = require_canvas(window);
  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();
  const auto first_document_rows = layer_list->count();
  CHECK(first_document_rows >= 2);

  window.add_document_session(make_float_test_document(QColor(20, 20, 120)), QStringLiteral("Solo layer"));
  QApplication::processEvents();
  auto* float_canvas = patchy::ui::MainWindowTestAccess::canvas(window);
  require_action(window, "windowFloatDocumentAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->count() == 1);
  CHECK(require_action_by_text(window, QStringLiteral("Undo")) != nullptr);
  CHECK(!require_action_by_text(window, QStringLiteral("Undo"))->isEnabled());

  patchy::ui::MainWindowTestAccess::activate_canvas(window, first_canvas);
  QApplication::processEvents();
  CHECK(layer_list->count() == first_document_rows);
  CHECK(require_action_by_text(window, QStringLiteral("Undo"))->isEnabled());
  CHECK(!window.windowTitle().contains(QStringLiteral("Solo layer")));

  patchy::ui::MainWindowTestAccess::activate_canvas(window, float_canvas);
  QApplication::processEvents();
  CHECK(layer_list->count() == 1);
  CHECK(!require_action_by_text(window, QStringLiteral("Undo"))->isEnabled());
  auto float_window_title = window.windowTitle();
  float_window_title.remove(QStringLiteral("[*]"));
  CHECK(float_window_title.contains(QStringLiteral("Solo layer")));
}

void ui_float_window_smart_object_child_commits_to_parent() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(tabs != nullptr);
  CHECK(tabs->count() == 1);
  const auto parent_tab_index = tabs->currentIndex();

  require_action(window, "layerConvertSmartObjectAction")->trigger();
  QApplication::processEvents();
  patchy::ui::MainWindowTestAccess::open_smart_object_contents(window);
  QApplication::processEvents();
  CHECK(tabs->count() == 2);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_is_smart_object_child(window));

  // Float the CHILD, modify it, and commit with Save: the parent takes the
  // commit exactly as it does for a tabbed child.
  require_action(window, "windowFloatDocumentAction")->trigger();
  QApplication::processEvents();
  CHECK(tabs->count() == 1);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_is_floated(window));
  auto* parent_canvas = dynamic_cast<patchy::ui::CanvasWidget*>(tabs->widget(parent_tab_index));
  CHECK(parent_canvas != nullptr);
  const auto parent_undo_before = patchy::ui::MainWindowTestAccess::undo_depth_for_canvas(window, parent_canvas);

  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();
  CHECK(patchy::ui::MainWindowTestAccess::save_document(window));
  QApplication::processEvents();
  CHECK(!patchy::ui::MainWindowTestAccess::active_session_is_modified(window));
  CHECK(patchy::ui::MainWindowTestAccess::undo_depth_for_canvas(window, parent_canvas) ==
        parent_undo_before + 1);

  // Closing the PARENT prompts for the floated child, closes it (session-id
  // recursion; a floated child has no tab index), then the modified parent's
  // own save prompt is discarded.
  int close_poll_attempts = 0;
  bool saw_children_prompt = false;
  bool saw_save_prompt = false;
  QTimer close_poller;
  QObject::connect(&close_poller, &QTimer::timeout,
                   [&close_poll_attempts, &saw_children_prompt, &saw_save_prompt, &close_poller] {
    if (++close_poll_attempts > 500) {
      close_poller.stop();
      return;
    }
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* box = qobject_cast<QMessageBox*>(widget);
      if (box == nullptr || !box->isVisible()) {
        continue;
      }
      if (box->objectName() == QStringLiteral("closeSmartObjectChildrenMessageBox")) {
        saw_children_prompt = true;
        box->button(QMessageBox::Yes)->click();
        return;
      }
      if (box->objectName() == QStringLiteral("saveChangesMessageBox")) {
        saw_save_prompt = true;
        box->button(QMessageBox::No)->click();
        return;
      }
    }
  });
  close_poller.start(10);
  const bool closed = patchy::ui::MainWindowTestAccess::close_document_tab(window, parent_tab_index);
  QApplication::processEvents();
  close_poller.stop();
  flush_deferred_deletes();
  CHECK(closed);
  CHECK(saw_children_prompt);
  CHECK(saw_save_prompt);
  CHECK(patchy::ui::MainWindowTestAccess::session_count(window) == 0);
  CHECK(tabs->count() == 0);
  CHECK(find_document_float_window(window) == nullptr);
}

void ui_window_float_all_tile_and_cascade() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(tabs != nullptr);
  window.add_document_session(make_float_test_document(QColor(200, 60, 60)), QStringLiteral("Two"));
  window.add_document_session(make_float_test_document(QColor(60, 200, 60)), QStringLiteral("Three"));
  QApplication::processEvents();
  CHECK(tabs->count() == 3);
  auto* active_before = patchy::ui::MainWindowTestAccess::canvas(window);

  require_action(window, "windowFloatAllAction")->trigger();
  QApplication::processEvents();
  CHECK(tabs->count() == 0);
  auto floats = window.findChildren<QWidget*>(QStringLiteral("documentFloatWindow"));
  CHECK(floats.size() == 3);
  CHECK(patchy::ui::MainWindowTestAccess::canvas(window) == active_before);
  CHECK(!require_action(window, "windowFloatAllAction")->isEnabled());
  CHECK(require_action(window, "windowTileAction")->isEnabled());
  CHECK(require_action(window, "windowCascadeAction")->isEnabled());

  require_action(window, "windowTileAction")->trigger();
  QApplication::processEvents();
  // Arrangement is confined to the document WORKSPACE (the tab-widget area), so
  // the tool palette, options bar, and panels stay visible; never the whole screen.
  const QRect workspace(tabs->mapToGlobal(QPoint(0, 0)), tabs->size());
  for (auto* first : floats) {
    const QRect first_global(first->mapToGlobal(QPoint(0, 0)), first->size());
    CHECK(workspace.adjusted(-4, -4, 4, 4).contains(first_global));
    for (auto* second : floats) {
      if (first == second) {
        continue;
      }
      // Interiors must not overlap (shared edges are fine).
      CHECK(!first->geometry().adjusted(1, 1, -1, -1).intersects(second->geometry().adjusted(1, 1, -1, -1)));
    }
  }
  CHECK(patchy::ui::MainWindowTestAccess::canvas(window) == active_before);

  require_action(window, "windowCascadeAction")->trigger();
  QApplication::processEvents();
  // Session order matches creation order, and findChildren returns creation
  // order for these direct children, so consecutive floats stagger uniformly.
  for (int index = 1; index < floats.size(); ++index) {
    const auto delta = floats[index]->geometry().topLeft() - floats[index - 1]->geometry().topLeft();
    CHECK(delta == QPoint(36, 36));
    CHECK(floats[index]->size() == floats[index - 1]->size());
  }
  const QRect cascade_first_global(floats.first()->mapToGlobal(QPoint(0, 0)), floats.first()->size());
  CHECK(workspace.adjusted(-4, -4, 4, 4).contains(cascade_first_global.topLeft()));
  CHECK(patchy::ui::MainWindowTestAccess::canvas(window) == active_before);

  require_action(window, "windowConsolidateTabsAction")->trigger();
  QApplication::processEvents();
  flush_deferred_deletes();
  CHECK(tabs->count() == 3);
  CHECK(require_action(window, "windowFloatAllAction")->isEnabled());
}

void ui_tab_drag_out_tears_off_document() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(tabs != nullptr);
  window.add_document_session(make_float_test_document(QColor(120, 120, 220)), QStringLiteral("Torn"));
  QApplication::processEvents();
  CHECK(tabs->count() == 2);
  auto* tab_bar = tabs->tabBar();
  CHECK(tab_bar != nullptr);
  auto* torn_canvas = dynamic_cast<patchy::ui::CanvasWidget*>(tabs->widget(1));
  CHECK(torn_canvas != nullptr);

  // Press on the second tab, drag it well below the bar: the tab tears off into
  // a float window. A horizontal drag must NOT tear (QTabBar's reorder).
  const auto press_point = tab_bar->tabRect(1).center();
  send_mouse(*tab_bar, QEvent::MouseButtonPress, press_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*tab_bar, QEvent::MouseMove, press_point + QPoint(30, 0), Qt::NoButton, Qt::LeftButton);
  CHECK(tabs->count() == 2);  // horizontal move: still a reorder drag
  send_mouse(*tab_bar, QEvent::MouseMove, press_point + QPoint(30, 90), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();

  CHECK(tabs->count() == 1);
  auto* float_window = find_document_float_window(window);
  CHECK(float_window != nullptr);
  CHECK(float_window->isVisible());
  CHECK(float_window->isAncestorOf(torn_canvas));
  CHECK(patchy::ui::MainWindowTestAccess::canvas(window) == torn_canvas);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_is_floated(window));

  // The gesture state must have reset: releasing and pressing again on the
  // remaining tab does not tear anything.
  send_mouse(*tab_bar, QEvent::MouseButtonRelease, press_point, Qt::LeftButton, Qt::NoButton);
  CHECK(tabs->count() == 1);
}

void ui_float_drag_over_tab_bar_docks() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(tabs != nullptr);
  window.add_document_session(make_float_test_document(QColor(220, 160, 40)), QStringLiteral("Docker"));
  QApplication::processEvents();
  require_action(window, "windowFloatDocumentAction")->trigger();
  QApplication::processEvents();
  auto* float_window = find_document_float_window(window);
  CHECK(float_window != nullptr);
  CHECK(tabs->count() == 1);

  const auto zone = patchy::ui::MainWindowTestAccess::float_dock_zone(window);
  CHECK(!zone.isEmpty());

  // A drop far away from the tab bar keeps the float floating.
  patchy::ui::MainWindowTestAccess::dock_float_at(window, float_window, zone.bottomRight() + QPoint(400, 400));
  QApplication::processEvents();
  CHECK(patchy::ui::MainWindowTestAccess::session_count(window) == 2);
  CHECK(find_document_float_window(window) == float_window);

  // A drop on the tab bar docks it back.
  patchy::ui::MainWindowTestAccess::dock_float_at(window, float_window, zone.center());
  QApplication::processEvents();
  flush_deferred_deletes();
  CHECK(tabs->count() == 2);
  CHECK(find_document_float_window(window) == nullptr);
  CHECK(!patchy::ui::MainWindowTestAccess::active_session_is_floated(window));

  // Empty-bar variant: float everything, then dock into the tab widget's strip.
  require_action(window, "windowFloatAllAction")->trigger();
  QApplication::processEvents();
  CHECK(tabs->count() == 0);
  const auto empty_zone = patchy::ui::MainWindowTestAccess::float_dock_zone(window);
  CHECK(!empty_zone.isEmpty());
  auto floats = window.findChildren<QWidget*>(QStringLiteral("documentFloatWindow"));
  CHECK(floats.size() == 2);
  patchy::ui::MainWindowTestAccess::dock_float_at(window, floats.first(), empty_zone.center());
  QApplication::processEvents();
  flush_deferred_deletes();
  CHECK(tabs->count() == 1);
  CHECK(window.findChildren<QWidget*>(QStringLiteral("documentFloatWindow")).size() == 1);
}

void ui_float_dock_highlight_tracks_drop_zone() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(tabs != nullptr);
  window.add_document_session(make_float_test_document(QColor(90, 140, 200)), QStringLiteral("Hover"));
  QApplication::processEvents();
  require_action(window, "windowFloatDocumentAction")->trigger();
  QApplication::processEvents();

  const auto zone = patchy::ui::MainWindowTestAccess::float_dock_zone(window);
  CHECK(!zone.isEmpty());
  // Lazily created: no overlay exists until a drag actually hovers the zone.
  CHECK(window.findChild<QWidget*>(QStringLiteral("floatDockHighlight")) == nullptr);

  patchy::ui::MainWindowTestAccess::float_dock_feedback_at(window, zone.center());
  QApplication::processEvents();
  auto* highlight = window.findChild<QWidget*>(QStringLiteral("floatDockHighlight"));
  CHECK(highlight != nullptr);
  CHECK(highlight->isVisible());
  CHECK(highlight->testAttribute(Qt::WA_TransparentForMouseEvents));
  const QRect highlight_global(highlight->mapToGlobal(QPoint(0, 0)), highlight->size());
  CHECK(highlight_global == zone);
  save_widget_artifact("ui_float_dock_highlight", *tabs);

  // Outside the zone the affordance disappears; back inside it returns.
  patchy::ui::MainWindowTestAccess::float_dock_feedback_at(window, zone.bottomRight() + QPoint(300, 300));
  CHECK(!highlight->isVisible());
  patchy::ui::MainWindowTestAccess::float_dock_feedback_at(window, zone.center());
  CHECK(highlight->isVisible());

  // After the drop docks the float, the affordance is gone for good measure.
  auto* float_window = find_document_float_window(window);
  CHECK(float_window != nullptr);
  patchy::ui::MainWindowTestAccess::dock_float_at(window, float_window, zone.center());
  QApplication::processEvents();
  flush_deferred_deletes();
  CHECK(tabs->count() == 2);
  patchy::ui::MainWindowTestAccess::float_dock_feedback_at(window, zone.bottomRight() + QPoint(300, 300));
  CHECK(!highlight->isVisible());
}

void ui_float_window_accepts_file_drop() {
  ensure_artifact_dir();
  const auto image_path = std::filesystem::absolute(std::filesystem::path("test-artifacts") / "float-drop.png");
  const auto image_path_qt = QString::fromStdString(image_path.string());
  QImage source(6, 4, QImage::Format_RGB32);
  source.fill(QColor(80, 20, 60));
  CHECK(source.save(image_path_qt));

  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(tabs != nullptr);
  require_action(window, "windowFloatDocumentAction")->trigger();
  QApplication::processEvents();
  auto* float_window = find_document_float_window(window);
  CHECK(float_window != nullptr);
  CHECK(tabs->count() == 0);

  QMimeData mime_data;
  mime_data.setUrls(QList<QUrl>{QUrl::fromLocalFile(image_path_qt)});
  const auto drop_position = float_window->rect().center();

  QDragEnterEvent drag_enter(drop_position, Qt::CopyAction, &mime_data, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(float_window, &drag_enter);
  QApplication::processEvents();
  CHECK(drag_enter.isAccepted());

  QDropEvent drop(QPointF(drop_position), Qt::CopyAction, &mime_data, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(float_window, &drop);
  QApplication::processEvents();
  CHECK(drop.isAccepted());

  // The dropped file opens as a TAB in the main window; the float stays floated.
  CHECK(patchy::ui::MainWindowTestAccess::session_count(window) == 2);
  CHECK(tabs->count() == 1);
  CHECK(tabs->tabText(0) == QStringLiteral("float-drop.png"));
  CHECK(find_document_float_window(window) == float_window);
}

}  // namespace

std::vector<patchy::test::TestCase> float_window_tests() {
  return {
      {"ui_float_document_window_hosts_canvas_and_redocks", ui_float_document_window_hosts_canvas_and_redocks},
      {"ui_float_window_preserves_channel_target_per_canvas",
       ui_float_window_preserves_channel_target_per_canvas},
      {"ui_float_window_edit_routes_to_owning_session", ui_float_window_edit_routes_to_owning_session},
      {"ui_float_activation_commits_text_editor_to_owning_document",
       ui_float_activation_commits_text_editor_to_owning_document},
      {"ui_float_window_close_prompts_and_closes_document", ui_float_window_close_prompts_and_closes_document},
      {"ui_close_all_documents_includes_floats", ui_close_all_documents_includes_floats},
      {"ui_float_window_respects_preview_dialog_edit_lock", ui_float_window_respects_preview_dialog_edit_lock},
      {"ui_float_only_document_keeps_actions_enabled", ui_float_only_document_keeps_actions_enabled},
      {"ui_float_window_title_tracks_modified_state", ui_float_window_title_tracks_modified_state},
      {"ui_float_window_activation_switches_panels", ui_float_window_activation_switches_panels},
      {"ui_float_window_smart_object_child_commits_to_parent",
       ui_float_window_smart_object_child_commits_to_parent},
      {"ui_float_window_accepts_file_drop", ui_float_window_accepts_file_drop},
      {"ui_window_float_all_tile_and_cascade", ui_window_float_all_tile_and_cascade},
      {"ui_tab_drag_out_tears_off_document", ui_tab_drag_out_tears_off_document},
      {"ui_float_drag_over_tab_bar_docks", ui_float_drag_over_tab_bar_docks},
      {"ui_float_dock_highlight_tracks_drop_zone", ui_float_dock_highlight_tracks_drop_zone},
  };
}
