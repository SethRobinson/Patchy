// Files dropped on a document's canvas (GitHub issue 749): Preferences > Application
// "Files dropped on a canvas" chooses between opening them in new tabs (the default) and
// placing them in that document as embedded Smart Objects (docs/smart-object-editing.md,
// "Canvas drops").
#include "core/document.hpp"
#include "core/smart_object.hpp"
#include "ui/app_settings.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/main_window.hpp"
#include "ui/qt_paths.hpp"
#include "app_shell_test_support.hpp"
#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include "unicode_path_names.hpp"

#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLinearGradient>
#include <QList>
#include <QMimeData>
#include <QPainter>
#include <QScrollArea>
#include <QStatusBar>
#include <QString>
#include <QStringList>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QUrl>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace patchy::test::ui;
using patchy::test::ui::app_shell_support::run_preferences_dialog;
using patchy::ui::MainWindowTestAccess;

const QString kCanvasFileDropKey = QStringLiteral("imports/canvasFileDrop");

// Sets the canvas-drop preference for one test and puts the stored value back after it,
// with the recent-file state that opening tabs touches.
struct CanvasDropPreference {
  SettingsValueRestorer policy{kCanvasFileDropKey};
  SettingsValueRestorer recent_files{QStringLiteral("recentFiles")};
  SettingsValueRestorer recent_folders{QStringLiteral("recentFolders")};
  SettingsValueRestorer last_open{QStringLiteral("lastOpenDirectory")};

  explicit CanvasDropPreference(bool place) {
    auto settings = patchy::ui::app_settings();
    if (place) {
      settings.setValue(kCanvasFileDropKey, QStringLiteral("smartObject"));
    } else {
      settings.remove(kCanvasFileDropKey);
    }
    settings.sync();
  }
};

patchy::Document make_board_document(int width = 400, int height = 300, QColor color = QColor(236, 236, 232)) {
  patchy::Document document(width, height, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(width, height, patchy::PixelFormat::rgba8(), color));
  return document;
}

QString write_art(const QString& path, int width = 120, int height = 80, QColor color = QColor(200, 60, 40)) {
  QImage image(width, height, QImage::Format_RGBA8888);
  image.fill(color);
  CHECK(image.save(path));
  return QDir::toNativeSeparators(path);
}

struct FileDrop {
  bool entered{false};
  bool move_accepted{false};
  bool drop_accepted{false};
};

// A file drag from outside the app: enter, move and drop on `target`, delivered the way
// Qt delivers a native one (QApplication::notify finds the widget that takes the enter and
// sends it the move and the drop). The deferred work runs on the caller's processEvents.
FileDrop send_file_drop(QWidget& target, QPoint position, const QStringList& paths) {
  QMimeData mime;
  QList<QUrl> urls;
  for (const auto& path : paths) {
    urls.push_back(QUrl::fromLocalFile(path));
  }
  mime.setUrls(urls);
  const Qt::DropActions actions = Qt::CopyAction | Qt::MoveAction;
  FileDrop result;
  QDragEnterEvent enter(position, actions, &mime, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(&target, &enter);
  result.entered = enter.isAccepted();
  QDragMoveEvent move(position, actions, &mime, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(&target, &move);
  result.move_accepted = move.isAccepted();
  QDropEvent drop(QPointF(position), actions, &mime, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(&target, &drop);
  result.drop_accepted = drop.isAccepted();
  return result;
}

std::size_t layer_count(const patchy::Document* document) {
  return document == nullptr ? 0U : std::as_const(*document).layers().size();
}

bool layer_has_block(const patchy::Layer& layer, const char* key) {
  const auto& blocks = layer.unknown_psd_blocks();
  return std::any_of(blocks.begin(), blocks.end(), [key](const auto& block) { return block.key == key; });
}

// Default preference: a file dropped on a canvas opens in a new tab, as it always has.
void ui_canvas_file_drop_default_opens_a_tab() {
  CanvasDropPreference preference(/*place=*/false);
  QTemporaryDir temp;
  CHECK(temp.isValid());
  const auto art = write_art(temp.filePath(QStringLiteral("default-drop.png")));

  patchy::ui::MainWindow window;
  show_window_empty(window);
  window.add_document_session(make_board_document(), QStringLiteral("Board"));
  QApplication::processEvents();
  auto* canvas = MainWindowTestAccess::canvas(window);
  auto* board = MainWindowTestAccess::document_for_canvas(window, canvas);
  CHECK(canvas != nullptr && board != nullptr);
  const auto sessions = MainWindowTestAccess::session_count(window);

  const auto drop = send_file_drop(*canvas, canvas->rect().center(), {art});
  CHECK(drop.entered && drop.drop_accepted);
  CHECK(MainWindowTestAccess::session_count(window) == sessions);  // nothing until the drop returns
  QApplication::processEvents();
  CHECK(MainWindowTestAccess::session_count(window) == sessions + 1);
  CHECK(MainWindowTestAccess::active_session_title(window) == QStringLiteral("default-drop.png"));
  CHECK(layer_count(board) == 1U);
  CHECK(MainWindowTestAccess::undo_depth_for_canvas(window, canvas) == 0);
}

// "Place as Smart Object": the file lands in the dropped-on document as an embedded smart
// object, exactly as File > Place > Place Embedded places it, and no tab opens.
void ui_canvas_file_drop_places_smart_object() {
  CanvasDropPreference preference(/*place=*/true);
  QTemporaryDir temp;
  CHECK(temp.isValid());
  const auto art = write_art(temp.filePath(QStringLiteral("drop-art.png")));

  patchy::ui::MainWindow window;
  show_window_empty(window);
  window.add_document_session(make_board_document(), QStringLiteral("Board"));
  QApplication::processEvents();
  auto* canvas = MainWindowTestAccess::canvas(window);
  auto* board = MainWindowTestAccess::document_for_canvas(window, canvas);
  CHECK(canvas != nullptr && board != nullptr);
  const auto sessions = MainWindowTestAccess::session_count(window);

  const auto drop = send_file_drop(*canvas, canvas->rect().center(), {art});
  CHECK(drop.entered && drop.move_accepted && drop.drop_accepted);
  CHECK(layer_count(board) == 1U);  // the work is deferred past the native drop call
  QApplication::processEvents();

  CHECK(MainWindowTestAccess::session_count(window) == sessions);
  CHECK(layer_count(board) == 2U);
  {
    const auto& placed = std::as_const(*board).layers().back();
    CHECK(placed.name() == "drop-art");
    CHECK(patchy::layer_is_smart_object(placed));
    CHECK(layer_has_block(placed, "SoLd"));
    CHECK(std::as_const(*board).active_layer_id() == placed.id());
    const auto placement = patchy::smart_object_placement_from_layer(placed);
    CHECK(placement.has_value());
    if (placement.has_value()) {
      // Place Embedded's rule: the file's own size, centered on the 400 x 300 canvas.
      CHECK(placement->width == 120.0 && placement->height == 80.0);
      CHECK(std::abs((placement->transform[0] + placement->transform[4]) / 2.0 - 200.0) < 0.01);
      CHECK(std::abs((placement->transform[1] + placement->transform[5]) / 2.0 - 150.0) < 0.01);
    }
    CHECK(std::as_const(*board).metadata().smart_objects.blocks.size() == 1U);
  }
  CHECK(MainWindowTestAccess::undo_depth_for_canvas(window, canvas) == 1);
  CHECK(MainWindowTestAccess::current_history_label(window) == QStringLiteral("Place Embedded"));
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Placed drop-art.png as a smart object"));

  MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(layer_count(board) == 1U);
  CHECK(MainWindowTestAccess::session_count(window) == sessions);
}

// Several placeable files in one drop: a smart object layer each, one undo step for all.
void ui_canvas_file_drop_places_several_files_as_one_undo_step() {
  CanvasDropPreference preference(/*place=*/true);
  QTemporaryDir temp;
  CHECK(temp.isValid());
  const auto first = write_art(temp.filePath(QStringLiteral("first.png")), 60, 40, QColor(220, 40, 40));
  const auto second = write_art(temp.filePath(QStringLiteral("second.jpg")), 80, 50, QColor(40, 160, 60));
  const auto third = write_art(temp.filePath(QStringLiteral("third.bmp")), 30, 30, QColor(40, 60, 220));

  patchy::ui::MainWindow window;
  show_window_empty(window);
  window.add_document_session(make_board_document(), QStringLiteral("Board"));
  QApplication::processEvents();
  auto* canvas = MainWindowTestAccess::canvas(window);
  auto* board = MainWindowTestAccess::document_for_canvas(window, canvas);
  CHECK(canvas != nullptr && board != nullptr);
  const auto sessions = MainWindowTestAccess::session_count(window);

  CHECK(send_file_drop(*canvas, canvas->rect().center(), {first, second, third}).drop_accepted);
  QApplication::processEvents();
  CHECK(MainWindowTestAccess::session_count(window) == sessions);
  CHECK(layer_count(board) == 4U);
  {
    // Drop order, the last file on top and active.
    const auto& layers = std::as_const(*board).layers();
    CHECK(layers[1].name() == "first" && layers[2].name() == "second" && layers[3].name() == "third");
    for (std::size_t index = 1; index < layers.size(); ++index) {
      CHECK(patchy::layer_is_smart_object(layers[index]));
    }
    CHECK(std::as_const(*board).active_layer_id() == layers[3].id());
  }
  CHECK(MainWindowTestAccess::undo_depth_for_canvas(window, canvas) == 1);
  // English shows a count message's source text as written, like "Added 2 layer(s)".
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Placed 3 file(s) as smart objects"));

  MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(layer_count(board) == 1U);
}

// Only a canvas places: the tab bar, the window around the canvas and a window with no
// document keep opening tabs whatever the preference says.
void ui_canvas_file_drop_elsewhere_still_opens_tabs() {
  CanvasDropPreference preference(/*place=*/true);
  QTemporaryDir temp;
  CHECK(temp.isValid());
  const auto on_tabs = write_art(temp.filePath(QStringLiteral("on-tabs.png")));
  const auto on_window = write_art(temp.filePath(QStringLiteral("on-window.png")));
  const auto on_empty = write_art(temp.filePath(QStringLiteral("on-empty.png")));

  {
    patchy::ui::MainWindow window;
    show_window_empty(window);
    window.add_document_session(make_board_document(), QStringLiteral("Board"));
    QApplication::processEvents();
    auto* board_canvas = MainWindowTestAccess::canvas(window);
    auto* board = MainWindowTestAccess::document_for_canvas(window, board_canvas);
    auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
    CHECK(board != nullptr && tabs != nullptr);
    const auto sessions = MainWindowTestAccess::session_count(window);

    auto* tab_bar = tabs->tabBar();
    CHECK(send_file_drop(*tab_bar, tab_bar->tabRect(0).center(), {on_tabs}).drop_accepted);
    QApplication::processEvents();
    CHECK(MainWindowTestAccess::session_count(window) == sessions + 1);
    CHECK(MainWindowTestAccess::active_session_title(window) == QStringLiteral("on-tabs.png"));
    CHECK(layer_count(board) == 1U);

    // The window itself (its chrome and panels), not a canvas.
    CHECK(send_file_drop(window, QPoint(50, 50), {on_window}).drop_accepted);
    QApplication::processEvents();
    CHECK(MainWindowTestAccess::session_count(window) == sessions + 2);
    CHECK(MainWindowTestAccess::active_session_title(window) == QStringLiteral("on-window.png"));
    CHECK(layer_count(board) == 1U);
    CHECK(MainWindowTestAccess::undo_depth_for_canvas(window, board_canvas) == 0);
  }
  {
    // No document open: the file opens.
    patchy::ui::MainWindow window;
    show_window_empty(window);
    CHECK(MainWindowTestAccess::session_count(window) == 0);
    CHECK(send_file_drop(window, QPoint(50, 50), {on_empty}).drop_accepted);
    QApplication::processEvents();
    CHECK(MainWindowTestAccess::session_count(window) == 1);
    CHECK(MainWindowTestAccess::active_session_title(window) == QStringLiteral("on-empty.png"));
    CHECK(layer_count(&MainWindowTestAccess::document(window)) == 1U);
  }
}

// The file goes into the document under the pointer: a float window's canvas places into
// the float's document, also while a tabbed document is the active one.
void ui_canvas_file_drop_places_into_float_window_document() {
  CanvasDropPreference preference(/*place=*/true);
  QTemporaryDir temp;
  CHECK(temp.isValid());
  const auto art = write_art(temp.filePath(QStringLiteral("float-art.png")));

  patchy::ui::MainWindow window;
  show_window_empty(window);
  window.add_document_session(make_board_document(200, 160), QStringLiteral("Tabbed"));
  QApplication::processEvents();
  auto* tabbed_canvas = MainWindowTestAccess::canvas(window);
  window.add_document_session(make_board_document(300, 240, QColor(30, 30, 40)), QStringLiteral("Floated"));
  QApplication::processEvents();
  auto* float_canvas = MainWindowTestAccess::canvas(window);
  require_action(window, "windowFloatDocumentAction")->trigger();
  QApplication::processEvents();
  CHECK(window.findChild<QWidget*>(QStringLiteral("documentFloatWindow")) != nullptr);
  auto* tabbed = MainWindowTestAccess::document_for_canvas(window, tabbed_canvas);
  auto* floated = MainWindowTestAccess::document_for_canvas(window, float_canvas);
  CHECK(tabbed != nullptr && floated != nullptr && tabbed != floated);

  // Work in the tabbed document; the float is a background document.
  MainWindowTestAccess::activate_canvas(window, tabbed_canvas);
  QApplication::processEvents();
  CHECK(MainWindowTestAccess::canvas(window) == tabbed_canvas);
  const auto sessions = MainWindowTestAccess::session_count(window);

  const auto drop = send_file_drop(*float_canvas, float_canvas->rect().center(), {art});
  CHECK(drop.entered && drop.drop_accepted);
  QApplication::processEvents();
  CHECK(MainWindowTestAccess::session_count(window) == sessions);
  CHECK(layer_count(floated) == 2U);
  CHECK(layer_count(tabbed) == 1U);
  CHECK(patchy::layer_is_smart_object(std::as_const(*floated).layers().back()));
  {
    const auto placement = patchy::smart_object_placement_from_layer(std::as_const(*floated).layers().back());
    CHECK(placement.has_value());
    if (placement.has_value()) {
      // Centered on the float's own 300 x 240 canvas, not the tabbed document's.
      CHECK(std::abs((placement->transform[0] + placement->transform[4]) / 2.0 - 150.0) < 0.01);
      CHECK(std::abs((placement->transform[1] + placement->transform[5]) / 2.0 - 120.0) < 0.01);
    }
  }
  // The dropped-on document is the active one now; its history holds the step.
  CHECK(MainWindowTestAccess::canvas(window) == float_canvas);
  CHECK(MainWindowTestAccess::undo_depth_for_canvas(window, float_canvas) == 1);
  CHECK(MainWindowTestAccess::undo_depth_for_canvas(window, tabbed_canvas) == 0);
  MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(layer_count(floated) == 1U);

  // And the other way: a drop on the tabbed canvas while the float is active.
  CHECK(MainWindowTestAccess::canvas(window) == float_canvas);
  CHECK(send_file_drop(*tabbed_canvas, tabbed_canvas->rect().center(), {art}).drop_accepted);
  QApplication::processEvents();
  CHECK(layer_count(tabbed) == 2U);
  CHECK(layer_count(floated) == 1U);
  CHECK(MainWindowTestAccess::canvas(window) == tabbed_canvas);
  CHECK(MainWindowTestAccess::session_count(window) == sessions);
}

// A mixed drop: the image places, the folder beside it opens like File > Open Folder.
void ui_canvas_file_drop_mixed_drop_keeps_other_handling() {
  CanvasDropPreference preference(/*place=*/true);
  QTemporaryDir temp;
  CHECK(temp.isValid());
  const auto art = write_art(temp.filePath(QStringLiteral("mixed-art.png")));
  CHECK(QDir(temp.path()).mkdir(QStringLiteral("pages")));
  write_art(temp.filePath(QStringLiteral("pages/page-1.png")), 16, 12, QColor(10, 10, 10));
  write_art(temp.filePath(QStringLiteral("pages/page-2.png")), 16, 12, QColor(90, 90, 90));

  patchy::ui::MainWindow window;
  show_window_empty(window);
  window.add_document_session(make_board_document(), QStringLiteral("Board"));
  QApplication::processEvents();
  auto* canvas = MainWindowTestAccess::canvas(window);
  auto* board = MainWindowTestAccess::document_for_canvas(window, canvas);
  CHECK(canvas != nullptr && board != nullptr);
  const auto sessions = MainWindowTestAccess::session_count(window);

  const auto drop = send_file_drop(*canvas, canvas->rect().center(),
                                   {art, QDir::toNativeSeparators(temp.filePath(QStringLiteral("pages")))});
  CHECK(drop.entered && drop.drop_accepted);
  QApplication::processEvents();
  CHECK(layer_count(board) == 2U);
  CHECK(patchy::layer_is_smart_object(std::as_const(*board).layers().back()));
  CHECK(MainWindowTestAccess::undo_depth_for_canvas(window, canvas) == 1);
  CHECK(MainWindowTestAccess::session_count(window) == sessions + 2);
  CHECK(MainWindowTestAccess::session_title(window, sessions) == QStringLiteral("page-1.png"));
  CHECK(MainWindowTestAccess::session_title(window, sessions + 1) == QStringLiteral("page-2.png"));
}

// A file that looks placeable and is not reports the failure in Place Embedded's own
// message box; it never opens as a tab instead, and the document is untouched.
void ui_canvas_file_drop_reports_a_failed_place() {
  CanvasDropPreference preference(/*place=*/true);
  QTemporaryDir temp;
  CHECK(temp.isValid());
  const auto whole = write_art(temp.filePath(QStringLiteral("whole.png")));
  const auto broken = QDir::toNativeSeparators(temp.filePath(QStringLiteral("broken.png")));
  {
    // A PNG cut off after its header: the signature reads as PNG, the pixels never decode.
    QFile source(whole);
    CHECK(source.open(QIODevice::ReadOnly));
    const auto bytes = source.read(40);
    QFile target(broken);
    CHECK(target.open(QIODevice::WriteOnly));
    CHECK(target.write(bytes) == bytes.size());
  }

  patchy::ui::MainWindow window;
  show_window_empty(window);
  window.add_document_session(make_board_document(), QStringLiteral("Board"));
  QApplication::processEvents();
  auto* canvas = MainWindowTestAccess::canvas(window);
  auto* board = MainWindowTestAccess::document_for_canvas(window, canvas);
  CHECK(canvas != nullptr && board != nullptr);
  const auto sessions = MainWindowTestAccess::session_count(window);

  bool saw_message = false;
  when_dialog_visible(QStringLiteral("placeEmbeddedFailedMessageBox"), [&saw_message](QDialog& dialog) {
    saw_message = true;
    dialog.accept();
  });
  CHECK(send_file_drop(*canvas, canvas->rect().center(), {broken}).drop_accepted);
  CHECK(process_events_until([&saw_message] { return saw_message; }));
  QApplication::processEvents();
  CHECK(MainWindowTestAccess::session_count(window) == sessions);
  CHECK(layer_count(board) == 1U);
  CHECK(MainWindowTestAccess::undo_depth_for_canvas(window, canvas) == 0);
}

// Preferences > Application "Files dropped on a canvas:" stores imports/canvasFileDrop;
// a new window reads it back and its canvas drops follow it.
void ui_preferences_canvas_file_drop_combo_saves_the_choice() {
  CanvasDropPreference preference(/*place=*/false);
  QTemporaryDir temp;
  CHECK(temp.isValid());
  const auto art = write_art(temp.filePath(QStringLiteral("after-preferences.png")));
  {
    patchy::ui::MainWindow window;
    show_window(window);
    run_preferences_dialog(window, /*accept=*/true, [](QDialog& dialog) {
      auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("preferencesCanvasFileDropCombo"));
      CHECK(combo != nullptr);
      if (combo == nullptr) {
        return;
      }
      CHECK(combo->currentData().toString() == QStringLiteral("tab"));
      CHECK(combo->currentText() == QStringLiteral("Open in a new tab"));
      QStringList values;
      for (int index = 0; index < combo->count(); ++index) {
        values << combo->itemData(index).toString();
      }
      CHECK((values == QStringList{QStringLiteral("tab"), QStringLiteral("smartObject")}));
      CHECK(!combo->toolTip().isEmpty());
      combo->setCurrentIndex(combo->findData(QStringLiteral("smartObject")));
      // Visual record: the row in the Application page.
      CHECK(select_preferences_page(dialog, QStringLiteral("Application")));
      dialog.resize(std::max(dialog.width(), 900), dialog.height());
      for (auto* parent = combo->parentWidget(); parent != nullptr; parent = parent->parentWidget()) {
        if (auto* scroll = qobject_cast<QScrollArea*>(parent)) {
          scroll->ensureWidgetVisible(combo, 50, 150);
          break;
        }
      }
      QApplication::processEvents();
      save_widget_artifact("preferences_canvas_file_drop_combo", dialog);
    });
    CHECK(patchy::ui::app_settings().value(kCanvasFileDropKey).toString() == QStringLiteral("smartObject"));
  }

  // A new window: Preferences shows the saved answer, and a canvas drop places.
  patchy::ui::MainWindow window;
  show_window_empty(window);
  window.add_document_session(make_board_document(), QStringLiteral("Board"));
  QApplication::processEvents();
  run_preferences_dialog(window, /*accept=*/false, [](QDialog& dialog) {
    auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("preferencesCanvasFileDropCombo"));
    CHECK(combo != nullptr && combo->currentData().toString() == QStringLiteral("smartObject"));
  });
  auto* canvas = MainWindowTestAccess::canvas(window);
  auto* board = MainWindowTestAccess::document_for_canvas(window, canvas);
  CHECK(canvas != nullptr && board != nullptr);
  const auto sessions = MainWindowTestAccess::session_count(window);
  CHECK(send_file_drop(*canvas, canvas->rect().center(), {art}).drop_accepted);
  QApplication::processEvents();
  CHECK(MainWindowTestAccess::session_count(window) == sessions);
  CHECK(layer_count(board) == 2U);

  // Back to "Open in a new tab": the same drop opens a tab again.
  run_preferences_dialog(window, /*accept=*/true, [](QDialog& dialog) {
    auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("preferencesCanvasFileDropCombo"));
    CHECK(combo != nullptr);
    if (combo != nullptr) {
      combo->setCurrentIndex(combo->findData(QStringLiteral("tab")));
    }
  });
  CHECK(patchy::ui::app_settings().value(kCanvasFileDropKey).toString() == QStringLiteral("tab"));
  CHECK(send_file_drop(*canvas, canvas->rect().center(), {art}).drop_accepted);
  QApplication::processEvents();
  CHECK(MainWindowTestAccess::session_count(window) == sessions + 1);
  CHECK(layer_count(board) == 2U);
}

// Unicode and special-character file names place by a canvas drop: every character class
// in the folder and the file name, the layer named after the file and the bytes embedded.
void ui_unicode_canvas_file_drop_places_smart_object() {
  CanvasDropPreference preference(/*place=*/true);
  const auto dir = std::filesystem::absolute(patchy::test::unicode_artifact_dir(u8"canvas-drop"));
  const auto to_q = [](std::u8string_view text) {
    return QString::fromUtf8(reinterpret_cast<const char*>(text.data()), static_cast<qsizetype>(text.size()));
  };

  patchy::ui::MainWindow window;
  show_window_empty(window);
  window.add_document_session(make_board_document(), QStringLiteral("Board"));
  QApplication::processEvents();
  auto* canvas = MainWindowTestAccess::canvas(window);
  auto* board = MainWindowTestAccess::document_for_canvas(window, canvas);
  CHECK(canvas != nullptr && board != nullptr);
  const auto sessions = MainWindowTestAccess::session_count(window);

  QStringList paths;
  QStringList stems;
  for (const auto stem : patchy::test::kUnicodePathStems) {
    stems.push_back(to_q(stem));
  }
  stems.push_back(to_q(patchy::test::kUnicodeCombinedStem));
  for (qsizetype index = 0; index < stems.size(); ++index) {
    const auto file = dir / patchy::ui::to_filesystem_path(stems.at(index) + QStringLiteral(".png"));
    paths.push_back(write_art(patchy::ui::to_qstring(file), 20 + static_cast<int>(index) * 4, 16));
  }

  CHECK(send_file_drop(*canvas, canvas->rect().center(), paths).drop_accepted);
  QApplication::processEvents();
  CHECK(MainWindowTestAccess::session_count(window) == sessions);
  CHECK(layer_count(board) == static_cast<std::size_t>(stems.size()) + 1U);
  const auto& layers = std::as_const(*board).layers();
  const auto& store = std::as_const(*board).metadata().smart_objects;
  for (qsizetype index = 0; index < stems.size() && static_cast<std::size_t>(index) + 1U < layers.size(); ++index) {
    const auto& placed = layers[static_cast<std::size_t>(index) + 1U];
    CHECK(patchy::layer_is_smart_object(placed));
    CHECK(QString::fromStdString(placed.name()).normalized(QString::NormalizationForm_C) ==
          stems.at(index).normalized(QString::NormalizationForm_C));
    const auto placement = patchy::smart_object_placement_from_layer(placed);
    CHECK(placement.has_value());
    if (!placement.has_value()) {
      continue;
    }
    CHECK(placement->width == 20.0 + static_cast<double>(index) * 4.0);
    // The embedded element holds the file's own bytes under its own name.
    bool found = false;
    for (const auto& block : store.blocks) {
      for (const auto& source : block.sources) {
        if (source.uuid != placement->uuid) {
          continue;
        }
        found = true;
        CHECK(QString::fromStdString(source.filename).normalized(QString::NormalizationForm_C) ==
              (stems.at(index) + QStringLiteral(".png")).normalized(QString::NormalizationForm_C));
        QFile file(paths.at(index));
        CHECK(file.open(QIODevice::ReadOnly));
        const auto bytes = file.readAll();
        CHECK(source.file_bytes != nullptr && source.file_bytes->size() == static_cast<std::size_t>(bytes.size()));
      }
    }
    CHECK(found);
  }
  CHECK(MainWindowTestAccess::undo_depth_for_canvas(window, canvas) == 1);
}

// Review capture for issue 749: a document with a dropped file placed as a smart object,
// the Layers panel showing the new layer.
void ui_canvas_file_drop_review_capture() {
  CanvasDropPreference preference(/*place=*/true);
  QTemporaryDir temp;
  CHECK(temp.isValid());
  const auto photo_path = temp.filePath(QStringLiteral("sunset.png"));
  {
    QImage photo(360, 240, QImage::Format_RGBA8888);
    QPainter painter(&photo);
    QLinearGradient sky(0, 0, 0, 240);
    sky.setColorAt(0.0, QColor(40, 60, 130));
    sky.setColorAt(0.6, QColor(240, 130, 70));
    sky.setColorAt(1.0, QColor(250, 210, 120));
    painter.fillRect(photo.rect(), sky);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 236, 170));
    painter.drawEllipse(QPoint(180, 150), 46, 46);
    painter.setBrush(QColor(30, 34, 60));
    painter.drawRect(0, 190, 360, 50);
    painter.end();
    // 72 ppi like the document below, so the photo lands at its own pixel size.
    photo.setDotsPerMeterX(2835);
    photo.setDotsPerMeterY(2835);
    CHECK(photo.save(photo_path));
  }

  patchy::ui::MainWindow window;
  show_window_empty(window);
  auto poster = make_board_document(640, 420, QColor(244, 244, 240));
  poster.print_settings().horizontal_ppi = 72.0;
  poster.print_settings().vertical_ppi = 72.0;
  window.add_document_session(std::move(poster), QStringLiteral("Poster"));
  QApplication::processEvents();
  auto* canvas = MainWindowTestAccess::canvas(window);
  auto* board = MainWindowTestAccess::document_for_canvas(window, canvas);
  CHECK(canvas != nullptr && board != nullptr);
  CHECK(send_file_drop(*canvas, canvas->rect().center(), {QDir::toNativeSeparators(photo_path)}).drop_accepted);
  QApplication::processEvents();
  CHECK(layer_count(board) == 2U);
  CHECK(patchy::layer_is_smart_object(std::as_const(*board).layers().back()));
  QApplication::processEvents();
  save_widget_artifact("canvas_file_drop_placed_smart_object", window);
}

}  // namespace

std::vector<patchy::test::TestCase> canvas_file_drop_tests() {
  return {
      {"ui_canvas_file_drop_default_opens_a_tab", ui_canvas_file_drop_default_opens_a_tab},
      {"ui_canvas_file_drop_places_smart_object", ui_canvas_file_drop_places_smart_object},
      {"ui_canvas_file_drop_places_several_files_as_one_undo_step",
       ui_canvas_file_drop_places_several_files_as_one_undo_step},
      {"ui_canvas_file_drop_elsewhere_still_opens_tabs", ui_canvas_file_drop_elsewhere_still_opens_tabs},
      {"ui_canvas_file_drop_places_into_float_window_document",
       ui_canvas_file_drop_places_into_float_window_document},
      {"ui_canvas_file_drop_mixed_drop_keeps_other_handling", ui_canvas_file_drop_mixed_drop_keeps_other_handling},
      {"ui_canvas_file_drop_reports_a_failed_place", ui_canvas_file_drop_reports_a_failed_place},
      {"ui_preferences_canvas_file_drop_combo_saves_the_choice",
       ui_preferences_canvas_file_drop_combo_saves_the_choice},
      {"ui_unicode_canvas_file_drop_places_smart_object", ui_unicode_canvas_file_drop_places_smart_object},
      {"ui_canvas_file_drop_review_capture", ui_canvas_file_drop_review_capture},
  };
}
