// Artboards in the UI (docs/artboards.md): the Artboard tool, the Move tool's artboard
// moves and auto-nesting, the Layer and Image commands, the Layers and Properties panels,
// New Document's Artboards option, Artboards to Files, print pages, the Multi-Page PDF
// source and the script API.
#include "core/artboard.hpp"
#include "core/document.hpp"
#include "core/layer_metadata.hpp"
#include "core/layer_render_utils.hpp"
#include "ui/app_settings.hpp"
#include "ui/canvas_types.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/export_documents_folder_dialog.hpp"
#include "ui/hotkey_registry.hpp"
#include "ui/hotkey_registry.hpp"
#include "ui/image_document_io.hpp"
#include "ui/image_sequence_dialog.hpp"
#include "ui/main_window.hpp"
#include "ui/print_dialog.hpp"
#include "ui/qt_paths.hpp"
#include "ui/theme_manager.hpp"
#include "ui/theme_palette.hpp"
#include "scripting_test_support.hpp"
#include "local_psd_fixtures.hpp"
#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QProgressDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QToolButton>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#if defined(PATCHY_HAVE_QT_PDF)
#include <QPdfDocument>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {
using namespace patchy::test::ui;
using patchy::ui::MainWindowTestAccess;

patchy::Document board_document(int width, int height) {
  patchy::Document document(width, height, patchy::PixelFormat::rgba8());
  document.print_settings().horizontal_ppi = 72.0;
  document.print_settings().vertical_ppi = 72.0;
  return document;
}

// A pixel layer of `rect`'s size placed at its corner.
patchy::LayerId add_solid_layer(patchy::Document& document, const char* name, QRect rect, QColor color) {
  patchy::Layer layer(document.allocate_layer_id(), name,
                      solid_pixels(rect.width(), rect.height(), patchy::PixelFormat::rgba8(), color));
  layer.set_bounds(patchy::Rect{rect.x(), rect.y(), rect.width(), rect.height()});
  const auto id = layer.id();
  document.add_layer(std::move(layer));
  return id;
}

// An artboard holding one solid layer the artboard's size, on top of the stack.
patchy::LayerId add_board(patchy::Document& document, const char* name, patchy::Rect rect,
                          patchy::ArtboardBackground background, QColor content) {
  auto artboard = patchy::make_artboard_group(document.allocate_layer_id(), name, rect, background);
  if (content.isValid()) {
    patchy::Layer child(document.allocate_layer_id(), std::string(name) + " Art",
                        solid_pixels(rect.width / 2, rect.height / 2, patchy::PixelFormat::rgba8(), content));
    child.set_bounds(patchy::Rect{rect.x, rect.y, rect.width / 2, rect.height / 2});
    artboard.add_child(std::move(child));
  }
  const auto id = artboard.id();
  document.layers().push_back(std::move(artboard));
  document.metadata().artboard_settings.emplace();
  return id;
}

const patchy::Layer* top_level(const patchy::Document& document, const char* name) {
  for (const auto& layer : document.layers()) {
    if (layer.name() == name) {
      return &layer;
    }
  }
  return nullptr;
}

void canvas_drag(patchy::ui::CanvasWidget& canvas, QPoint from, QPoint to,
                 Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  drag_document_path(canvas, {from, to}, 6, modifiers);
  QApplication::processEvents();
}

// Repeatedly runs `step` on a short timer while a modal dialog's exec() blocks.
void drive_modal(const std::shared_ptr<std::function<bool()>>& step, int attempts = 2400) {
  QTimer::singleShot(25, [step, attempts] {
    if ((*step)()) {
      return;
    }
    if (attempts > 0) {
      drive_modal(step, attempts - 1);
    }
  });
}

// The Artboard tool end to end: a drag on empty canvas makes an artboard, a corner handle
// resizes it, a drag inside moves it, arrow keys nudge (one undo step per run), and an
// Alt-drag past the canvas edge copies it and grows the canvas. Every edit undoes.
void ui_artboard_tool_creates_resizes_moves_and_undoes() {
  SettingsValueRestorer background_restorer(QStringLiteral("tools/artboardBackground"));
  patchy::ui::app_settings().setValue(QStringLiteral("tools/artboardBackground"), QStringLiteral("white"));
  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(board_document(300, 200), QStringLiteral("Boards"));
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();
  require_hotkey_action(window, QStringLiteral("tools.artboard"))->trigger();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Artboard);
  const auto& document = [&window]() -> const patchy::Document& { return MainWindowTestAccess::document(window); };
  const auto rect_of = [&](std::size_t index) {
    const auto artboards = patchy::document_artboards(document());
    return index < artboards.size() ? artboards[index]->artboard()->rect : patchy::Rect{};
  };

  canvas_drag(*canvas, {20, 30}, {120, 110});
  auto artboards = patchy::document_artboards(document());
  CHECK(artboards.size() == 1);
  if (artboards.size() != 1) {
    return;
  }
  CHECK(artboards[0]->name() == "Artboard 1");
  CHECK((rect_of(0) == patchy::Rect{20, 30, 100, 80}));
  CHECK(artboards[0]->artboard()->background == patchy::ArtboardBackground::White);
  CHECK(artboards[0]->blend_mode() == patchy::BlendMode::Normal);
  MainWindowTestAccess::undo(window);
  CHECK(patchy::document_artboards(document()).empty());
  MainWindowTestAccess::redo(window);
  CHECK(patchy::document_artboards(document()).size() == 1);

  // The bottom-right handle of the selected artboard.
  canvas_drag(*canvas, {120, 110}, {150, 130});
  CHECK((rect_of(0) == patchy::Rect{20, 30, 130, 100}));
  // A drag inside moves it.
  canvas_drag(*canvas, {60, 60}, {80, 70});
  CHECK((rect_of(0) == patchy::Rect{40, 40, 130, 100}));
  MainWindowTestAccess::undo(window);
  CHECK((rect_of(0) == patchy::Rect{20, 30, 130, 100}));
  MainWindowTestAccess::redo(window);

  // Three nudges, one undo step.
  const auto depth = MainWindowTestAccess::active_session_undo_depth(window);
  for (int i = 0; i < 3; ++i) {
    send_key(*canvas, Qt::Key_Right);
  }
  CHECK(rect_of(0).x == 43);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == depth + 1);
  MainWindowTestAccess::undo(window);
  CHECK(rect_of(0).x == 40);

  // Alt-drag copies; past the right edge the canvas grows to hold the copy.
  canvas_drag(*canvas, {60, 60}, {260, 60}, Qt::AltModifier);
  artboards = patchy::document_artboards(document());
  CHECK(artboards.size() == 2);
  if (artboards.size() == 2) {
    // The copy lands about 200 px right (the move snaps its center to the canvas edge);
    // the source stays.
    const auto& copy = artboards[0]->artboard()->rect.x > 100 ? *artboards[0] : *artboards[1];
    const auto& source = artboards[0]->artboard()->rect.x > 100 ? *artboards[1] : *artboards[0];
    CHECK(std::abs(copy.artboard()->rect.x - 240) <= 8 && copy.artboard()->rect.y == 40);
    CHECK((source.artboard()->rect == patchy::Rect{40, 40, 130, 100}));
    CHECK(document().width() == copy.artboard()->rect.x + 130);
  }
  // The other tools forget a half-made drag.
  require_hotkey_action(window, QStringLiteral("tools.move"))->trigger();
  CHECK(!canvas->artboard_drag_active());
}

// Layer > New > Artboard from Layers, Convert Artboard to Group, Artboard from Group and
// Image > Fit Canvas to Artboards, each one undoable step.
void ui_artboard_layer_commands_and_fit_canvas() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto seed = board_document(300, 200);
  const auto a = add_solid_layer(seed, "A", QRect(10, 10, 50, 40), QColor(255, 0, 0));
  const auto b = add_solid_layer(seed, "B", QRect(100, 50, 40, 40), QColor(0, 0, 255));
  window.add_document_session(std::move(seed), QStringLiteral("Commands"));
  QApplication::processEvents();
  const auto& document = [&window]() -> const patchy::Document& { return MainWindowTestAccess::document(window); };

  MainWindowTestAccess::select_layers(window, {a, b}, b);
  require_hotkey_action(window, QStringLiteral("layer.artboard_from_layers"))->trigger();
  auto artboards = patchy::document_artboards(document());
  CHECK(artboards.size() == 1);
  if (artboards.size() != 1) {
    return;
  }
  const auto board_id = artboards[0]->id();
  CHECK((artboards[0]->artboard()->rect == patchy::Rect{10, 10, 130, 80}));
  CHECK(artboards[0]->children().size() == 2);
  CHECK(artboards[0]->children().size() == 2 && artboards[0]->children()[0].id() == a &&
        artboards[0]->children()[1].id() == b);
  CHECK(document().layers().size() == 1);

  MainWindowTestAccess::select_layers(window, {board_id}, board_id);
  require_hotkey_action(window, QStringLiteral("layer.artboard_to_group"))->trigger();
  const auto* group = document().find_layer(board_id);
  CHECK(group != nullptr && group->kind() == patchy::LayerKind::Group && !group->artboard().has_value());
  CHECK(group != nullptr && group->children().size() == 2);

  MainWindowTestAccess::select_layers(window, {board_id}, board_id);
  require_hotkey_action(window, QStringLiteral("layer.artboard_from_group"))->trigger();
  group = document().find_layer(board_id);
  CHECK(group != nullptr && group->artboard().has_value());
  CHECK(group != nullptr && group->artboard().has_value() &&
        (group->artboard()->rect == patchy::Rect{10, 10, 130, 80}));

  require_hotkey_action(window, QStringLiteral("image.fit_canvas_to_artboards"))->trigger();
  CHECK(document().width() == 130 && document().height() == 80);
  group = document().find_layer(board_id);
  CHECK(group != nullptr && group->artboard().has_value() &&
        (group->artboard()->rect == patchy::Rect{0, 0, 130, 80}));
  // The content moved with the canvas: A's corner is the artboard's.
  const auto* moved_a = document().find_layer(a);
  CHECK(moved_a != nullptr && moved_a->bounds().x == 0 && moved_a->bounds().y == 0);
  MainWindowTestAccess::undo(window);
  CHECK(document().width() == 300 && document().height() == 200);
}

// The Move tool moves a selected artboard with its content; with Auto-Nest on, a layer
// dragged onto an artboard joins it and one dragged off every artboard leaves.
void ui_artboard_move_tool_moves_artboards_and_auto_nests() {
  SettingsValueRestorer nest_restorer(QStringLiteral("tools/artboardAutoNest"));
  patchy::ui::app_settings().setValue(QStringLiteral("tools/artboardAutoNest"), true);
  patchy::ui::MainWindow window;
  show_window(window);
  auto seed = board_document(400, 200);
  const auto loose = add_solid_layer(seed, "Loose", QRect(250, 20, 40, 40), QColor(0, 160, 0));
  const auto board = add_board(seed, "Board", patchy::Rect{0, 0, 150, 150}, patchy::ArtboardBackground::White,
                               QColor(200, 0, 0));
  window.add_document_session(std::move(seed), QStringLiteral("Move"));
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();
  require_hotkey_action(window, QStringLiteral("tools.move"))->trigger();
  const auto& document = [&window]() -> const patchy::Document& { return MainWindowTestAccess::document(window); };

  MainWindowTestAccess::select_layers(window, {board}, board);
  canvas_drag(*canvas, {50, 50}, {70, 60});
  const auto* artboard = document().find_layer(board);
  CHECK(artboard != nullptr && (artboard->artboard()->rect == patchy::Rect{20, 10, 150, 150}));
  CHECK(artboard != nullptr && !artboard->children().empty() && artboard->children()[0].bounds().x == 20 &&
        artboard->children()[0].bounds().y == 10);

  // Loose layer onto the artboard: it nests.
  MainWindowTestAccess::select_layers(window, {loose}, loose);
  canvas_drag(*canvas, {270, 40}, {80, 80});
  CHECK(patchy::artboard_owning_layer(document(), loose) != nullptr &&
        patchy::artboard_owning_layer(document(), loose)->id() == board);
  // And back off every artboard: it returns to the root.
  MainWindowTestAccess::select_layers(window, {loose}, loose);
  canvas_drag(*canvas, {80, 80}, {300, 60});
  CHECK(patchy::artboard_owning_layer(document(), loose) == nullptr);
  CHECK(top_level(document(), "Loose") != nullptr);
}

// Prevent Auto-Nesting (GitHub issue 246): with Auto-Nest Layers on, a locked layer dragged
// over an artboard moves but stays where it is in the tree, and a locked artboard takes
// nothing in. The registered Prevent Auto-Nesting action (no menu-bar entry; the layer
// context menu's Lock submenu shows it) sets it and shows the selection's state; the row
// shows its badge.
void ui_artboard_prevent_auto_nesting_keeps_layer_in_place() {
  SettingsValueRestorer nest_restorer(QStringLiteral("tools/artboardAutoNest"));
  patchy::ui::app_settings().setValue(QStringLiteral("tools/artboardAutoNest"), true);
  patchy::ui::MainWindow window;
  show_window(window);
  auto seed = board_document(400, 200);
  // Loose above the artboard in the stack, so it stays visible over it after the drag.
  const auto board = add_board(seed, "Board", patchy::Rect{0, 0, 150, 150}, patchy::ArtboardBackground::White,
                               QColor(200, 0, 0));
  const auto loose = add_solid_layer(seed, "Loose", QRect(250, 20, 40, 40), QColor(0, 160, 0));
  window.add_document_session(std::move(seed), QStringLiteral("Prevent Auto-Nesting"));
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();
  require_hotkey_action(window, QStringLiteral("tools.move"))->trigger();
  const auto& document = [&window]() -> const patchy::Document& { return MainWindowTestAccess::document(window); };
  const auto prevents = [&document](patchy::LayerId id) {
    const auto* layer = document().find_layer(id);
    return layer != nullptr && patchy::layer_prevents_auto_nesting(*layer);
  };
  auto* action = require_action(window, "layerPreventAutoNestingAction");
  CHECK(action->isCheckable() && action->menuRole() == QAction::NoRole);
  CHECK(require_hotkey_action(window, QStringLiteral("layer.prevent_auto_nesting")) == action);
  // On the window for its hotkey, in no menu-bar menu.
  CHECK(window.actions().contains(action));
  for (auto* menu : window.menuBar()->findChildren<QMenu*>()) {
    CHECK(!menu->actions().contains(action));
  }

  MainWindowTestAccess::select_layers(window, {loose}, loose);
  QApplication::processEvents();
  CHECK(!action->isChecked());
  action->trigger();
  QApplication::processEvents();
  CHECK(prevents(loose) && action->isChecked());
  // Not an editing lock: Lock All and the position lock stay off.
  for (const char* name : {"layerLockAllButton", "layerLockPositionButton"}) {
    auto* button = window.findChild<QToolButton*>(QLatin1String(name));
    CHECK(button != nullptr && !button->isChecked());
  }
  MainWindowTestAccess::select_layers(window, {board}, board);
  QApplication::processEvents();
  CHECK(!action->isChecked());
  MainWindowTestAccess::select_layers(window, {loose}, loose);
  QApplication::processEvents();
  CHECK(action->isChecked());

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* row = layer_list != nullptr ? layer_list->itemWidget(require_layer_item(*layer_list, QStringLiteral("Loose")))
                                    : nullptr;
  const auto badges = row != nullptr ? row->findChildren<QLabel*>(QStringLiteral("layerLockBadge")) : QList<QLabel*>{};
  CHECK(badges.size() == 1);
  CHECK(!badges.isEmpty() && badges.front()->toolTip().contains(QStringLiteral("Auto-nesting")));

  // Captures: test-artifacts/issue_246_drag_before.png and issue_246_drag_after.png.
  save_widget_artifact("issue_246_drag_before", window);
  canvas_drag(*canvas, {270, 40}, {80, 80});
  QApplication::processEvents();
  save_widget_artifact("issue_246_drag_after", window);
  CHECK(patchy::artboard_owning_layer(document(), loose) == nullptr);
  // It moved over the artboard (Snap may shift it a few pixels) but stayed at the top level.
  const auto* moved = top_level(document(), "Loose");
  CHECK(moved != nullptr && moved->bounds().x + 20 < 150 && moved->bounds().y + 20 < 150);

  // Unlocked, but over a locked artboard: still nothing nests.
  action->trigger();
  QApplication::processEvents();
  CHECK(!prevents(loose) && !action->isChecked());
  MainWindowTestAccess::select_layers(window, {board}, board);
  action->trigger();
  QApplication::processEvents();
  CHECK(prevents(board) && action->isChecked());
  MainWindowTestAccess::select_layers(window, {loose}, loose);
  canvas_drag(*canvas, {80, 80}, {90, 90});
  CHECK(patchy::artboard_owning_layer(document(), loose) == nullptr);
  // The artboard unlocked: the next drag nests as usual.
  MainWindowTestAccess::select_layers(window, {board}, board);
  action->trigger();
  QApplication::processEvents();
  CHECK(!prevents(board));
  MainWindowTestAccess::select_layers(window, {loose}, loose);
  canvas_drag(*canvas, {90, 90}, {100, 100});
  CHECK(patchy::artboard_owning_layer(document(), loose) != nullptr &&
        patchy::artboard_owning_layer(document(), loose)->id() == board);
}

// The Layers panel marks artboard rows; the Properties panel shows the selected
// artboard's rect and moves or resizes it.
void ui_artboard_layers_panel_rows_and_properties_panel() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto seed = board_document(300, 200);
  const auto board = add_board(seed, "Card", patchy::Rect{10, 20, 120, 80}, patchy::ArtboardBackground::Black,
                               QColor(255, 255, 0));
  window.add_document_session(std::move(seed), QStringLiteral("Panels"));
  QApplication::processEvents();
  MainWindowTestAccess::select_layers(window, {board}, board);
  QApplication::processEvents();
  int artboard_rows = 0;
  for (auto* row : window.findChildren<QWidget*>(QStringLiteral("layerRowWidget"))) {
    artboard_rows += row->property("layerRowArtboard").toBool() ? 1 : 0;
  }
  CHECK(artboard_rows == 1);

  auto* panel = window.findChild<QWidget*>(QStringLiteral("propertiesArtboardPanel"));
  auto* x = window.findChild<QSpinBox*>(QStringLiteral("propertiesArtboardXSpin"));
  auto* width = window.findChild<QSpinBox*>(QStringLiteral("propertiesArtboardWidthSpin"));
  auto* background = window.findChild<QComboBox*>(QStringLiteral("propertiesArtboardBackgroundCombo"));
  CHECK(panel != nullptr && x != nullptr && width != nullptr && background != nullptr);
  if (panel == nullptr || x == nullptr || width == nullptr || background == nullptr) {
    return;
  }
  CHECK(!panel->isHidden());
  CHECK(x->value() == 10 && width->value() == 120);
  CHECK(background->currentData().toString() == QStringLiteral("black"));
  x->setValue(25);
  QApplication::processEvents();
  const auto& document = MainWindowTestAccess::document(window);
  const auto* artboard = std::as_const(document).find_layer(board);
  CHECK(artboard != nullptr && (artboard->artboard()->rect == patchy::Rect{25, 20, 120, 80}));
  CHECK(artboard != nullptr && artboard->children()[0].bounds().x == 25);  // the content travels
  width->setValue(140);
  QApplication::processEvents();
  artboard = std::as_const(document).find_layer(board);
  CHECK(artboard != nullptr && (artboard->artboard()->rect == patchy::Rect{25, 20, 140, 80}));
  // A plain layer selected: the artboard section hides.
  const auto child = artboard->children()[0].id();
  MainWindowTestAccess::select_layers(window, {child}, child);
  QApplication::processEvents();
  CHECK(panel->isHidden());
}

// The Properties panel's artboard X/Y/W/H fields scrub like the options bar's (issue 46):
// a drag on "X:" previews the move on the canvas and the release moves the artboard in one
// undo step, a drag on the W field itself resizes it in one step, and a plain click on a
// field edits it, number selected.
void ui_artboard_properties_fields_scrub_as_one_undo_step() {
  for (const auto preference : {patchy::ui::ColorSchemePreference::Dark, patchy::ui::ColorSchemePreference::Light}) {
    ColorSchemeRestorer scheme_restorer(preference);
    const std::string scheme = preference == patchy::ui::ColorSchemePreference::Light ? "light" : "dark";
    patchy::ui::MainWindow window;
    show_window(window);
    auto seed = board_document(300, 200);
    const patchy::Rect start{10, 20, 120, 80};
    const auto board = add_board(seed, "Card", start, patchy::ArtboardBackground::White, QColor(40, 120, 220));
    window.add_document_session(std::move(seed), QStringLiteral("Scrub"));
    QApplication::processEvents();
    MainWindowTestAccess::select_layers(window, {board}, board);
    MainWindowTestAccess::show_panel(window, QStringLiteral("propertiesDock"));
    QApplication::processEvents();
    auto* panel = window.findChild<QWidget*>(QStringLiteral("propertiesArtboardPanel"));
    auto* x = window.findChild<QSpinBox*>(QStringLiteral("propertiesArtboardXSpin"));
    auto* width = window.findChild<QSpinBox*>(QStringLiteral("propertiesArtboardWidthSpin"));
    CHECK(panel != nullptr && x != nullptr && width != nullptr);
    if (panel == nullptr || x == nullptr || width == nullptr) {
      return;
    }
    CHECK(panel->isVisible());
    QLabel* x_label = nullptr;
    for (auto* label : panel->findChildren<QLabel*>()) {
      if (label->text() == QStringLiteral("X:")) {
        x_label = label;
      }
    }
    CHECK(x_label != nullptr);
    if (x_label == nullptr) {
      return;
    }
    int finished = 0;
    QObject::connect(x, &QAbstractSpinBox::editingFinished, x, [&finished] { ++finished; });
    const auto rect_now = [&window, board] {
      const auto* artboard = std::as_const(MainWindowTestAccess::document(window)).find_layer(board);
      return artboard != nullptr ? artboard->artboard()->rect : patchy::Rect{};
    };
    const auto child_x = [&window, board] {
      const auto* artboard = std::as_const(MainWindowTestAccess::document(window)).find_layer(board);
      return artboard != nullptr && !artboard->children().empty() ? artboard->children()[0].bounds().x : -1;
    };
    const auto depth = MainWindowTestAccess::active_session_undo_depth(window);
    const int drag_start = QApplication::startDragDistance();
    const auto origin = x_label->rect().center();
    send_mouse(*x_label, QEvent::MouseButtonPress, origin, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*x_label, QEvent::MouseMove, origin + QPoint(drag_start + 10, 0), Qt::NoButton, Qt::LeftButton);
    send_mouse(*x_label, QEvent::MouseMove, origin + QPoint(drag_start + 30, 0), Qt::NoButton, Qt::LeftButton);
    QApplication::processEvents();
    // Captured before any check, so a run on code without the handles still shows the drag.
    if (auto* section = MainWindowTestAccess::panel_section(window, QStringLiteral("propertiesDock"))) {
      save_widget_artifact("artboard_properties_scrub_mid_drag_" + scheme, *section);
    }
    save_widget_artifact("artboard_properties_scrub_mid_drag_window_" + scheme, window);
    for (const auto* name : {"propertiesArtboardXSpin", "propertiesArtboardYSpin", "propertiesArtboardWidthSpin",
                             "propertiesArtboardHeightSpin"}) {
      const auto* spin = panel->findChild<QSpinBox*>(QLatin1String(name));
      CHECK(spin != nullptr && spin->property(patchy::ui::kScrubHandleInstalledProperty).toBool());
    }
    CHECK(x_label->cursor().shape() == Qt::SizeHorCursor);
    // Mid-drag: the canvas previews the move; the document waits for the release. The
    // spot the artboard left shows the pasteboard, not the artboard still in the document.
    CHECK(x->value() == start.x + drag_start + 30);
    CHECK(rect_now() == start);
    auto* canvas = require_canvas(window);
    CHECK(canvas->artboard_drag_active());
    const auto frame = canvas->grab().toImage();
    const auto left_spot = frame.pixelColor(canvas->widget_position_for_document_point(QPoint(start.x + 3, start.y + 5)));
    CHECK(!color_close(left_spot, QColor(40, 120, 220), 40));
    CHECK(!color_close(left_spot, QColor(Qt::white), 30));
    send_mouse(*x_label, QEvent::MouseButtonRelease, origin + QPoint(drag_start + 30, 0), Qt::LeftButton,
               Qt::NoButton);
    QApplication::processEvents();
    CHECK(finished == 1);
    CHECK((rect_now() == patchy::Rect{start.x + drag_start + 30, start.y, start.width, start.height}));
    CHECK(child_x() == start.x + drag_start + 30);  // the content travels with it
    CHECK(MainWindowTestAccess::active_session_undo_depth(window) == depth + 1);
    MainWindowTestAccess::undo(window);
    QApplication::processEvents();
    CHECK(rect_now() == start);
    CHECK(child_x() == start.x);
    CHECK(x->value() == start.x);

    // A drag on the unfocused W field resizes in one step.
    auto* editor = width->findChild<QLineEdit*>();
    CHECK(editor != nullptr);
    if (editor == nullptr) {
      return;
    }
    const QPoint field_point(editor->width() / 2, editor->height() / 2);
    send_mouse(*editor, QEvent::MouseButtonPress, field_point, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*editor, QEvent::MouseMove, field_point + QPoint(drag_start + 5, 0), Qt::NoButton, Qt::LeftButton);
    send_mouse(*editor, QEvent::MouseMove, field_point + QPoint(drag_start + 20, 0), Qt::NoButton, Qt::LeftButton);
    send_mouse(*editor, QEvent::MouseButtonRelease, field_point + QPoint(drag_start + 20, 0), Qt::LeftButton,
               Qt::NoButton);
    QApplication::processEvents();
    CHECK(width->value() == start.width + drag_start + 20);
    CHECK((rect_now() == patchy::Rect{start.x, start.y, start.width + drag_start + 20, start.height}));
    CHECK(MainWindowTestAccess::active_session_undo_depth(window) == depth + 1);

    // A plain click on a field edits it with the number selected and changes nothing.
    const auto value_before_click = x->value();
    auto* x_editor = x->findChild<QLineEdit*>();
    CHECK(x_editor != nullptr);
    if (x_editor != nullptr) {
      x->setFocus(Qt::MouseFocusReason);
      const QPoint x_point(x_editor->width() / 2, x_editor->height() / 2);
      send_mouse(*x_editor, QEvent::MouseButtonPress, x_point, Qt::LeftButton, Qt::LeftButton);
      send_mouse(*x_editor, QEvent::MouseButtonRelease, x_point, Qt::LeftButton, Qt::NoButton);
      QApplication::processEvents();
      CHECK(x->value() == value_before_click);
      CHECK(x_editor->hasSelectedText());
      CHECK(MainWindowTestAccess::active_session_undo_depth(window) == depth + 1);
    }
  }
}

// A W scrub in the Properties panel costs what a handle drag of the Artboard tool costs:
// while it runs the canvas previews the new rect and the document stays untouched, and
// the release commits one resize (Seth, October 21, 2026: the scrub was slow on a large
// document because every pixel ran the whole resize, recomposite and panel refresh).
// Prints the timings of the handle drag, the scrub, arrow steps and a typed value.
void check_artboard_scrub_costs_a_handle_drag(patchy::ui::MainWindow& window, patchy::LayerId board,
                                              const char* name) {
  auto* canvas = require_canvas(window);
  require_hotkey_action(window, QStringLiteral("tools.artboard"))->trigger();
  MainWindowTestAccess::select_layers(window, {board}, board);
  MainWindowTestAccess::show_panel(window, QStringLiteral("propertiesDock"));
  canvas->fit_to_view();
  canvas->force_refresh();
  QApplication::processEvents();
  auto* width = window.findChild<QSpinBox*>(QStringLiteral("propertiesArtboardWidthSpin"));
  auto* panel = window.findChild<QWidget*>(QStringLiteral("propertiesArtboardPanel"));
  CHECK(width != nullptr && panel != nullptr);
  if (width == nullptr || panel == nullptr) {
    return;
  }
  QLabel* w_label = nullptr;
  for (auto* label : panel->findChildren<QLabel*>()) {
    if (label->text() == QStringLiteral("W:")) {
      w_label = label;
    }
  }
  CHECK(w_label != nullptr);
  if (w_label == nullptr) {
    return;
  }
  const auto rect_now = [&window, board] {
    const auto* artboard = std::as_const(MainWindowTestAccess::document(window)).find_layer(board);
    return artboard != nullptr ? artboard->artboard()->rect : patchy::Rect{};
  };
  const auto start = rect_now();
  CHECK(start.width > 100);
  // One step as the user sees it: the event, then the repaint it asked for.
  const auto settle = [&] {
    QApplication::processEvents();
    canvas->repaint();
  };
  const auto elapsed_ms = [](const QElapsedTimer& timer) { return static_cast<double>(timer.nsecsElapsed()) / 1.0e6; };
  constexpr int kSteps = 12;
  const int drag_start = QApplication::startDragDistance();

  // The Artboard tool: drag the right-edge handle inward in 12 steps.
  const auto handle =
      canvas->widget_position_for_document_point(QPoint(start.x + start.width, start.y + start.height / 2));
  QElapsedTimer timer;
  send_mouse(*canvas, QEvent::MouseButtonPress, handle, Qt::LeftButton, Qt::LeftButton);
  double handle_step_ms = 0.0;
  for (int step = 1; step <= kSteps; ++step) {
    timer.start();
    send_mouse(*canvas, QEvent::MouseMove, handle - QPoint(drag_start + 2 * step, 0), Qt::NoButton, Qt::LeftButton);
    settle();
    handle_step_ms += elapsed_ms(timer);
  }
  handle_step_ms /= kSteps;
  timer.start();
  send_mouse(*canvas, QEvent::MouseButtonRelease, handle - QPoint(drag_start + 2 * kSteps, 0), Qt::LeftButton,
             Qt::NoButton);
  settle();
  const double handle_release_ms = elapsed_ms(timer);
  CHECK(rect_now().width < start.width);
  MainWindowTestAccess::undo(window);
  settle();
  CHECK(rect_now() == start);

  // The same kind of change by scrubbing the W label: the document must stay untouched
  // until the release, which commits one undo step.
  const auto depth = MainWindowTestAccess::active_session_undo_depth(window);
  const auto origin = w_label->rect().center();
  send_mouse(*w_label, QEvent::MouseButtonPress, origin, Qt::LeftButton, Qt::LeftButton);
  double scrub_step_ms = 0.0;
  bool document_untouched = true;
  bool previewed = true;
  for (int step = 1; step <= kSteps; ++step) {
    timer.start();
    send_mouse(*w_label, QEvent::MouseMove, origin - QPoint(drag_start + 2 * step, 0), Qt::NoButton, Qt::LeftButton);
    settle();
    scrub_step_ms += elapsed_ms(timer);
    document_untouched = document_untouched && rect_now() == start &&
                         MainWindowTestAccess::active_session_undo_depth(window) == depth;
    previewed = previewed && canvas->artboard_drag_active();
  }
  scrub_step_ms /= kSteps;
  const int scrubbed_width = width->value();
  timer.start();
  send_mouse(*w_label, QEvent::MouseButtonRelease, origin - QPoint(drag_start + 2 * kSteps, 0), Qt::LeftButton,
             Qt::NoButton);
  settle();
  const double scrub_release_ms = elapsed_ms(timer);
  CHECK(scrubbed_width == start.width - (drag_start + 2 * kSteps));  // the scrub counts from the press
  CHECK((rect_now() == patchy::Rect{start.x, start.y, scrubbed_width, start.height}));
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == depth + 1);
  CHECK(!canvas->artboard_drag_active());

  // Arrow steps and a typed value commit at once (one undo step per run).
  auto* editor = width->findChild<QLineEdit*>();
  CHECK(editor != nullptr);
  double arrow_step_ms = 0.0;
  double typed_ms = 0.0;
  if (editor != nullptr) {
    width->setFocus(Qt::OtherFocusReason);
    QApplication::processEvents();
    for (int step = 0; step < 4; ++step) {
      timer.start();
      send_key(*editor, Qt::Key_Up);
      settle();
      arrow_step_ms += elapsed_ms(timer);
    }
    arrow_step_ms /= 4;
    CHECK(rect_now().width == scrubbed_width + 4);
    const int typed = start.width - 50;
    editor->selectAll();
    editor->insert(QString::number(typed));
    timer.start();
    send_key(*editor, Qt::Key_Return);
    settle();
    typed_ms = elapsed_ms(timer);
    CHECK(rect_now().width == typed);
  }
  std::printf("[TIMING] %s: handle drag %.1f ms/step, release %.1f ms; W scrub %.1f ms/step, release %.1f ms; "
              "arrow %.1f ms/step; typed+Enter %.1f ms\n",
              name, handle_step_ms, handle_release_ms, scrub_step_ms, scrub_release_ms, arrow_step_ms, typed_ms);
  std::fflush(stdout);
  CHECK(document_untouched);
  CHECK(previewed);
  // A scrub step costs about what a handle-drag step costs (both repaint a preview).
  CHECK(scrub_step_ms <= 3.0 * handle_step_ms + 20.0);
}

// The timing above on a large document: one 4000 x 3000 artboard holding three
// translucent layers its size, and on the ag-psd artboards sample when present.
void ui_artboard_properties_scrub_previews_like_the_handle_drag() {
  {
    patchy::ui::MainWindow window;
    show_window(window);
    constexpr int kWidth = 4000;
    constexpr int kHeight = 3000;
    auto seed = board_document(kWidth, kHeight);
    const patchy::Rect rect{0, 0, kWidth, kHeight};
    const auto board = add_board(seed, "Big", rect, patchy::ArtboardBackground::White, QColor());
    for (int index = 0; index < 3; ++index) {
      patchy::Layer layer(seed.allocate_layer_id(), "Photo " + std::to_string(index + 1),
                          solid_pixels(kWidth, kHeight, patchy::PixelFormat::rgba8(),
                                       QColor(60 * index + 40, 120, 200 - 50 * index, 170)));
      layer.set_bounds(rect);
      seed.layers().back().add_child(std::move(layer));
    }
    window.add_document_session(std::move(seed), QStringLiteral("Big Board"));
    check_artboard_scrub_costs_a_handle_drag(window, board, "4000x3000 artboard, 3 layers");
  }
  const auto path = patchy::test::source_root_path() / "local-test-fixtures" / "ag-psd" / "artboards" / "src.psd";
  if (!std::filesystem::exists(path)) {
    std::printf("[TIMING] ag-psd artboards sample not present; skipped\n");
    return;
  }
  SettingsValueRestorer notes_setting(QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::MainWindow window;
  show_window(window);
  MainWindowTestAccess::open_document_path(window, patchy::ui::to_qstring(path));
  QApplication::processEvents();
  std::optional<patchy::LayerId> widest;
  int widest_width = 0;
  for (const auto* artboard : patchy::document_artboards(MainWindowTestAccess::document(window))) {
    if (artboard->artboard()->rect.width > widest_width) {
      widest_width = artboard->artboard()->rect.width;
      widest = artboard->id();
    }
  }
  CHECK(widest.has_value());
  if (widest.has_value()) {
    check_artboard_scrub_costs_a_handle_drag(window, *widest, "ag-psd artboards sample");
  }
}

// New Document's Artboards option starts the document as one artboard the canvas size,
// holding "Layer 1".
void ui_artboard_new_document_option() {
  SettingsValueRestorer option_restorer(QStringLiteral("newDocument/artboards"));
  // Start without an image an earlier test left on the clipboard.
  QGuiApplication::clipboard()->clear();
  patchy::ui::MainWindow window;
  show_window(window);
  auto done = std::make_shared<bool>(false);
  auto step = std::make_shared<std::function<bool()>>();
  *step = [done] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyNewDocumentDialog"));
    if (dialog == nullptr) {
      return false;
    }
    auto* artboards = dialog->findChild<QCheckBox*>(QStringLiteral("newDocumentArtboardsCheck"));
    auto* create = dialog->findChild<QPushButton*>(QStringLiteral("newDocumentCreateButton"));
    if (artboards == nullptr || create == nullptr) {
      return false;
    }
    artboards->setChecked(true);
    create->click();
    *done = true;
    return true;
  };
  drive_modal(step);
  require_hotkey_action(window, QStringLiteral("file.new"))->trigger();
  CHECK(*done);
  const auto& document = std::as_const(MainWindowTestAccess::document(window));
  const auto artboards = patchy::document_artboards(document);
  CHECK(artboards.size() == 1);
  if (artboards.size() == 1) {
    CHECK(artboards[0]->name() == "Artboard 1");
    CHECK((artboards[0]->artboard()->rect == patchy::Rect{0, 0, document.width(), document.height()}));
    CHECK(artboards[0]->children().size() == 1 && artboards[0]->children()[0].name() == "Layer 1");
  }
  CHECK(patchy::ui::app_settings().value(QStringLiteral("newDocument/artboards")).toBool());
}

// The plain-document helper the suite uses (accept_new_document_dialog) must not inherit
// Artboards from the Last Used card of an artboard document created just before: the
// checkerboard test found no Background row after one in a full run.
void ui_new_document_auto_accept_ignores_remembered_artboards() {
  SettingsValueRestorer option_restorer(QStringLiteral("newDocument/artboards"));
  QGuiApplication::clipboard()->clear();
  patchy::ui::MainWindow window;
  show_window(window);
  auto done = std::make_shared<bool>(false);
  auto step = std::make_shared<std::function<bool()>>();
  *step = [done] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyNewDocumentDialog"));
    if (dialog == nullptr) {
      return false;
    }
    auto* artboards = dialog->findChild<QCheckBox*>(QStringLiteral("newDocumentArtboardsCheck"));
    auto* create = dialog->findChild<QPushButton*>(QStringLiteral("newDocumentCreateButton"));
    if (artboards == nullptr || create == nullptr) {
      return false;
    }
    artboards->setChecked(true);
    create->click();
    *done = true;
    return true;
  };
  drive_modal(step);
  require_hotkey_action(window, QStringLiteral("file.new"))->trigger();
  CHECK(*done);
  CHECK(patchy::document_artboards(std::as_const(MainWindowTestAccess::document(window))).size() == 1);

  accept_new_document_dialog(220, 180);
  require_hotkey_action(window, QStringLiteral("file.new"))->trigger();
  QApplication::processEvents();
  const auto& plain = std::as_const(MainWindowTestAccess::document(window));
  CHECK(plain.width() == 220 && plain.height() == 180);
  CHECK(patchy::document_artboards(plain).empty());
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  CHECK(layer_list != nullptr && find_layer_item(*layer_list, QStringLiteral("Background")) != nullptr);
}

// File > Export > Artboards to Files writes one file per artboard named for it, at the
// artboard's size, never overwriting in Add mode; Print's artboard pages write one sheet
// per artboard, each its own size when the paper matches.
void ui_artboard_exports_to_files_and_print_pages() {
  QTemporaryDir temp;
  CHECK(temp.isValid());
  SettingsValueRestorer last_save_restorer(QStringLiteral("lastSaveDirectory"));
  patchy::ui::MainWindow window;
  show_window(window);
  auto seed = board_document(400, 200);
  const auto back = add_board(seed, "Back", patchy::Rect{250, 0, 100, 150}, patchy::ArtboardBackground::Black,
                              QColor());
  const auto front = add_board(seed, "Front", patchy::Rect{0, 0, 150, 100}, patchy::ArtboardBackground::White,
                               QColor(255, 0, 0));
  window.add_document_session(std::move(seed), QStringLiteral("Card"));
  QApplication::processEvents();
  CHECK(require_hotkey_action(window, QStringLiteral("file.export_artboards")) != nullptr);

  patchy::ui::ImageSequenceNaming naming;
  naming.use_layer_names = true;
  naming.prefix = QStringLiteral("Card - ");
  const auto written = MainWindowTestAccess::export_artboards_to_folder(
      window, {front, back}, temp.path(), QStringLiteral("png"), naming,
      patchy::ui::ExportDocumentsExistingFiles::AddNumbering);
  CHECK(written.has_value() && written->size() == 2);
  if (written.has_value() && written->size() == 2) {
    CHECK(QFileInfo(written->at(0)).fileName() == QStringLiteral("Card - Front.png"));
    CHECK(QFileInfo(written->at(1)).fileName() == QStringLiteral("Card - Back.png"));
    const QImage front_image(written->at(0));
    const QImage back_image(written->at(1));
    CHECK(front_image.size() == QSize(150, 100) && back_image.size() == QSize(100, 150));
    CHECK(front_image.pixelColor(20, 20) == QColor(255, 0, 0));
    CHECK(front_image.pixelColor(120, 80) == QColor(255, 255, 255));
    CHECK(back_image.pixelColor(50, 75) == QColor(0, 0, 0));
  }
  const auto again = MainWindowTestAccess::export_artboards_to_folder(
      window, {front}, temp.path(), QStringLiteral("png"), naming,
      patchy::ui::ExportDocumentsExistingFiles::AddNumbering);
  CHECK(again.has_value() && again->size() == 1 &&
        QFileInfo(again->front()).fileName() == QStringLiteral("Card - Front 2.png"));

  const auto& document = std::as_const(MainWindowTestAccess::document(window));
  auto settings = patchy::ui::default_print_settings(document, std::nullopt);
  settings.area_mode = patchy::ui::PrintAreaMode::Artboards;
  settings.match_artboard_paper = true;
  const auto pages = patchy::ui::print_artboard_pages(document);
  CHECK(pages.size() == 2);
  const auto sheet = patchy::ui::artboard_page_layout(pages.front()).fullRect(QPageLayout::Point);
  CHECK(std::abs(sheet.width() - 150.0) < 0.5 && std::abs(sheet.height() - 100.0) < 0.5);
  // Each page prints whole, at actual size, centered.
  const auto page_settings = patchy::ui::artboard_page_print_settings(settings);
  CHECK(page_settings.area_mode == patchy::ui::PrintAreaMode::Document &&
        page_settings.scale_mode == patchy::ui::PrintScaleMode::ActualSize && page_settings.center);
#if defined(PATCHY_HAVE_QT_PDF)
  const auto pdf_path = temp.filePath(QStringLiteral("print.pdf"));
  CHECK(patchy::ui::write_print_pdf(pdf_path, document, settings, patchy::ui::default_print_page_layout(),
                                    QStringLiteral("Card")));
  {
    QPdfDocument reader;
    CHECK(reader.load(pdf_path) == QPdfDocument::Error::None);
    CHECK(reader.pageCount() == 2);
    if (reader.pageCount() == 2) {
      CHECK(std::abs(reader.pagePointSize(0).width() - 150.0) < 1.0);
      CHECK(std::abs(reader.pagePointSize(1).height() - 150.0) < 1.0);
    }
  }
  // On the chosen paper instead: two Letter sheets.
  settings.match_artboard_paper = false;
  const auto letter_path = temp.filePath(QStringLiteral("print-letter.pdf"));
  CHECK(patchy::ui::write_print_pdf(letter_path, document, settings, patchy::ui::default_print_page_layout(),
                                    QStringLiteral("Card")));
  QPdfDocument letter;
  CHECK(letter.load(letter_path) == QPdfDocument::Error::None);
  CHECK(letter.pageCount() == 2);
  CHECK(letter.pageCount() != 2 || std::abs(letter.pagePointSize(1).width() - 612.0) < 1.0);
#endif
}

// Export > Multi-Page PDF offers the active document's artboards as its pages, and
// starts on that source for an artboard document.
void ui_artboard_multipage_pdf_source() {
  SettingsValueRestorer source_restorer(QStringLiteral("exportOptions/multiPagePdfSource"));
  patchy::ui::MainWindow window;
  show_window(window);
  auto seed = board_document(300, 200);
  add_board(seed, "One", patchy::Rect{0, 0, 100, 100}, patchy::ArtboardBackground::White, QColor(0, 0, 255));
  add_board(seed, "Two", patchy::Rect{150, 0, 100, 60}, patchy::ArtboardBackground::White, QColor());
  window.add_document_session(std::move(seed), QStringLiteral("Pages"));
  QApplication::processEvents();
  auto seen = std::make_shared<bool>(false);
  // The file first, then PDF Options.
  accept_save_file_dialog_then(QStringLiteral("exportMultiPagePdfFileDialog"), QDir::current().absoluteFilePath(QStringLiteral("test-artifacts/multipage_unused.pdf")), [seen] {
    auto* dialog = find_top_level_dialog(QStringLiteral("multiPagePdfExportDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* artboards = dialog->findChild<QRadioButton*>(QStringLiteral("multiPagePdfArtboardsRadio"));
    CHECK(artboards != nullptr && artboards->isEnabled() && artboards->isChecked());
    CHECK(artboards != nullptr && artboards->text().contains(QStringLiteral("2")));
    *seen = true;
    dialog->reject();
  });
  require_action(window, "fileExportMultiPagePdfAction")->trigger();
  CHECK(*seen);
}

// The commands and tool are registered under their permanent ids, the Move flyout cycles
// on Shift+V, and the menus hold every command.
void ui_artboard_commands_and_hotkeys_registered() {
  patchy::ui::MainWindow window;
  show_window(window);
  for (const char* id : {"tools.artboard", "tools.cycle.move", "layer.new_artboard", "layer.artboard_from_group",
                         "layer.artboard_from_layers", "layer.artboard_to_group", "image.fit_canvas_to_artboards",
                         "file.export_artboards"}) {
    const auto* command = window.hotkey_registry().find_command(QString::fromLatin1(id));
    CHECK(command != nullptr && command->action != nullptr);
  }
  auto* cycle = require_hotkey_action(window, QStringLiteral("tools.cycle.move"));
  CHECK(cycle != nullptr && cycle->shortcut() == QKeySequence(Qt::SHIFT | Qt::Key_V));
  for (const char* name : {"layerNewArtboardAction", "layerArtboardFromGroupAction", "layerArtboardFromLayersAction",
                           "layerArtboardToGroupAction", "fileExportArtboardsAction"}) {
    auto* action = require_action(window, name);
    CHECK(action != nullptr && action->menuRole() == QAction::NoRole);
  }
}

// The script API: add, read, move, restyle, wrap layers, fit, export and refuse what
// artboards cannot do.
void ui_script_artboards_add_move_and_export() {
  QTemporaryDir temp;
  CHECK(temp.isValid());
  patchy::ui::MainWindow window;
  show_window(window);
  const auto folder = QDir::fromNativeSeparators(temp.path());
  const auto pdf_path = folder + QStringLiteral("/boards.pdf");
  const auto quoted = [](const QString& text) {
    return QString::fromUtf8(QJsonDocument(QJsonArray{text}).toJson(QJsonDocument::Compact)).chopped(1).mid(1);
  };
  CHECK(scripting_support::run_script(window, QStringLiteral(R"JS(
    var doc = app.newDocument(100, 100);
    var a = doc.addArtboard('Phone', {x: 0, y: 0, width: 120, height: 200});
    var b = doc.addArtboard('', {x: 220, y: 0, width: 80, height: 80}, {color: '#336699'});
    var names = doc.artboards.map(function (x) { return x.name; });
    if (names.length !== 2 || names[0] !== b.name || b.name !== 'Artboard 1') throw new Error('order ' + names);
    if (!a.isArtboard || a.artboard.width !== 120 || a.artboard.background !== 'white') throw new Error('a ' + JSON.stringify(a.artboard));
    if (b.artboard.background !== 'color' || b.artboard.color !== '#336699') throw new Error('b ' + JSON.stringify(b.artboard));
    if (doc.width !== 300 || doc.height !== 200) throw new Error('canvas ' + doc.width + 'x' + doc.height);
    a.moveTo(10, 0);
    if (a.x !== 10 || a.bounds.x !== 10 || a.artboard.x !== 10) throw new Error('move ' + a.x);
    a.setArtboard({width: 150, background: 'black'});
    if (a.artboard.width !== 150 || a.artboard.background !== 'black') throw new Error('restyle');
    var paint = doc.addLayer('Paint');
    paint.fillRect(0, 0, 10, 10, '#ff0000');
    var wrapped = doc.artboardFromLayers([paint], 'Wrapped');
    if (!wrapped.isArtboard || wrapped.name !== 'Wrapped' || wrapped.children.length !== 1) throw new Error('wrap');
    var threw = false;
    try { doc.groupLayers([a], 'G'); } catch (e) { threw = true; }
    if (!threw) throw new Error('artboard grouped');
    threw = false;
    try { doc.addArtboard('x', {x: 0, y: 0, width: 0, height: 5}); } catch (e) { threw = true; }
    if (!threw) throw new Error('empty artboard accepted');
    var shift = doc.fitCanvasToArtboards();
    if (typeof shift.x !== 'number' || typeof shift.y !== 'number') throw new Error('shift');
    var files = doc.exportArtboards(%1, {format: 'png', prefix: 'S - ', artboards: [a, b]});
    if (files.length !== 2 || files[0].indexOf('S - Phone.png') < 0) throw new Error('files ' + files);
    threw = false;
    try { doc.exportArtboards(%1, {format: 'nope'}); } catch (e) { threw = true; }
    if (!threw) throw new Error('bad format accepted');
    doc.exportAs(%2);
    wrapped.setArtboard(null);
    if (wrapped.isArtboard || wrapped.artboard !== null) throw new Error('unset');
    console.log('artboards-ok ' + doc.artboards.length);
  )JS")
                                                 .arg(quoted(folder), quoted(pdf_path))));
  CHECK(scripting_support::backlog_contains(window, QStringLiteral("artboards-ok 2")));
  CHECK(QImage(folder + QStringLiteral("/S - Phone.png")).size() == QSize(150, 200));
  CHECK(QImage(folder + QStringLiteral("/S - Artboard 1.png")).pixelColor(40, 40) == QColor(0x33, 0x66, 0x99));
#if defined(PATCHY_HAVE_QT_PDF)
  QPdfDocument reader;
  CHECK(reader.load(pdf_path) == QPdfDocument::Error::None);
  CHECK(reader.pageCount() == 3);  // Wrapped, Artboard 1, Phone: top of the stack first
#endif
}


// The drag shows what the release does (docs/artboards.md, "Fast paths"): a red layer
// dragged from artboard A into artboard B draws over B's paper mid-drag and leaves A's
// paper clean, at 100% and 50% zoom (the preview-scaled document), with live patches and
// with the latched proxy; with Auto-Nest off it stays clipped to A, mid-drag and after.
void ui_artboard_drag_across_artboards_previews_the_release() {
  EnvironmentVariableRestorer restore_latch("PATCHY_MOVE_LIVE_LATCH_MS");
  struct Case {
    double zoom;
    bool proxy;
    bool auto_nest;
  };
  for (const auto& run : {Case{1.0, false, true}, Case{0.5, false, true}, Case{1.0, true, true},
                          Case{0.5, true, true}, Case{1.0, false, false}}) {
    if (run.proxy) {
      qputenv("PATCHY_MOVE_LIVE_LATCH_MS", QByteArray("0"));
    } else {
      qunsetenv("PATCHY_MOVE_LIVE_LATCH_MS");
    }
    auto document = board_document(360, 160);
    const auto board_a = add_board(document, "A", patchy::Rect{0, 0, 140, 140}, patchy::ArtboardBackground::White,
                                   QColor());
    const auto board_b = add_board(document, "B", patchy::Rect{200, 0, 140, 140}, patchy::ArtboardBackground::White,
                                   QColor());
    patchy::Layer red(document.allocate_layer_id(), "Red",
                      solid_pixels(24, 24, patchy::PixelFormat::rgba8(), QColor(220, 30, 30)));
    red.set_bounds(patchy::Rect{20, 20, 24, 24});
    const auto red_id = red.id();
    for (auto& layer : document.layers()) {
      if (layer.id() == board_a) {
        layer.add_child(std::move(red));
      }
    }

    patchy::ui::CanvasWidget canvas;
    canvas.resize(820, 420);
    canvas.set_document(&document);
    canvas.set_zoom(run.zoom);
    canvas.set_tool(patchy::ui::CanvasTool::Move);
    canvas.set_show_transform_controls(false);
    canvas.set_auto_select_layer(false);
    canvas.set_snap_enabled(false);
    canvas.set_artboard_auto_nest(run.auto_nest);
    canvas.set_selected_layer_ids({red_id});
    canvas.show();
    QApplication::processEvents();

    // From the red square's center (32, 32) to (252, 32): into B, 220 px right.
    const auto start = canvas.widget_position_for_document_point(QPoint(32, 32));
    const auto end = canvas.widget_position_for_document_point(QPoint(252, 32));
    send_mouse(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    for (int step = 1; step <= 6; ++step) {
      send_mouse(canvas, QEvent::MouseMove, start + (end - start) * step / 6, Qt::NoButton, Qt::LeftButton);
      QApplication::processEvents();
    }
    if (run.auto_nest) {
      // Visual QA: the mid-drag frame (test-artifacts/ui_artboard_drag_mid_*.png).
      save_image_artifact(std::string("ui_artboard_drag_mid_") + (run.zoom < 1.0 ? "50" : "100") +
                              (run.proxy ? "_proxy" : "_live"),
                          canvas.grab().toImage());
    }
    const auto mid_destination = canvas_pixel(canvas, QPoint(252, 32));
    const auto mid_origin = canvas_pixel(canvas, QPoint(32, 32));
    CHECK(color_close(mid_origin, QColor(Qt::white), 30));  // A's paper, the square gone
    if (run.auto_nest) {
      CHECK(color_close(mid_destination, QColor(220, 30, 30), 45));  // drawn on B, over its paper
    } else {
      CHECK(color_close(mid_destination, QColor(Qt::white), 30));  // still A's: clipped away
    }
    send_mouse(canvas, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    const auto* owner = patchy::artboard_owning_layer(std::as_const(document), red_id);
    CHECK(owner != nullptr && owner->id() == (run.auto_nest ? board_b : board_a));
    // The released frame matches the drag's last frame.
    const auto after = canvas_pixel(canvas, QPoint(252, 32));
    CHECK(color_close(after, mid_destination, 30));
  }
}


// Painting on an artboard that does not own the active layer (docs/artboards.md, "Painting
// across artboards"): the stroke moves to that artboard's topmost pixel layer instead of
// landing, clipped away, in the active one (Seth's 23-page PDF import). An artboard with
// no pixel layer gets an empty one inside the stroke's undo step.
void ui_artboard_paint_switches_to_the_clicked_artboards_layer() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto document = board_document(560, 200);
  const auto board_a = add_board(document, "A", patchy::Rect{0, 0, 150, 150}, patchy::ArtboardBackground::White,
                                 QColor());
  const auto board_b = add_board(document, "B", patchy::Rect{200, 0, 150, 150}, patchy::ArtboardBackground::White,
                                 QColor());
  const auto board_c = add_board(document, "C", patchy::Rect{400, 0, 150, 150}, patchy::ArtboardBackground::White,
                                 QColor());
  const auto add_art = [&document](patchy::LayerId board_id, const char* name, int x) {
    patchy::Layer art(document.allocate_layer_id(), name,
                      solid_pixels(150, 150, patchy::PixelFormat::rgba8(), QColor(Qt::transparent)));
    art.set_bounds(patchy::Rect{x, 0, 150, 150});
    const auto id = art.id();
    document.find_layer(board_id)->add_child(std::move(art));
    return id;
  };
  const auto art_a = add_art(board_a, "A Art", 0);
  const auto art_b = add_art(board_b, "B Art", 200);
  window.add_document_session(std::move(document), QStringLiteral("Paint boards"));
  QApplication::processEvents();
  using Access = patchy::ui::MainWindowTestAccess;
  Access::select_layers(window, {art_a}, art_a);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(220, 30, 30));
  canvas->set_brush_size(12);
  QApplication::processEvents();
  const auto& doc = std::as_const(Access::document(window));
  const auto undo_before = Access::active_session_undo_depth(window);

  canvas_drag(*canvas, QPoint(230, 75), QPoint(320, 75));
  CHECK(doc.active_layer_id() == art_b);
  CHECK(Access::active_session_undo_depth(window) == undo_before + 1);
  const auto* b_art = doc.find_layer(art_b);
  CHECK(b_art != nullptr && b_art->pixels().pixel(275 - 200, 75)[3] == 255);
  const auto* a_art = doc.find_layer(art_a);
  CHECK(a_art != nullptr && a_art->bounds() == (patchy::Rect{0, 0, 150, 150}));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(275, 75)), QColor(220, 30, 30), 45));

  // C has no layer: one is added on it, painted, and Undo takes both back.
  canvas_drag(*canvas, QPoint(430, 75), QPoint(520, 75));
  const auto* c = doc.find_layer(board_c);
  CHECK(c != nullptr && c->children().size() == 1U);
  if (c != nullptr && c->children().size() == 1U) {
    CHECK(doc.active_layer_id() == c->children().front().id());
    CHECK(c->children().front().bounds() == (patchy::Rect{400, 0, 150, 150}));
    CHECK(c->children().front().pixels().pixel(475 - 400, 75)[3] == 255);
  }
  CHECK(Access::active_session_undo_depth(window) == undo_before + 2);
  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  const auto& undone = std::as_const(Access::document(window));
  const auto* c_after = undone.find_layer(board_c);
  CHECK(c_after != nullptr && c_after->children().empty());
}

// Add Artboard sizes the new artboard from the saved new-artboard setting, never from the
// options-bar W/H, which mirror the active artboard, here the one last painted on
// (docs/legal-constraints.md, "Artboards").
void ui_artboard_add_button_uses_the_saved_size_not_the_last_edited_artboard() {
  SettingsValueRestorer width_restorer(QStringLiteral("tools/artboardWidth"));
  SettingsValueRestorer height_restorer(QStringLiteral("tools/artboardHeight"));
  patchy::ui::app_settings().setValue(QStringLiteral("tools/artboardWidth"), 120);
  patchy::ui::app_settings().setValue(QStringLiteral("tools/artboardHeight"), 90);
  patchy::ui::MainWindow window;
  show_window(window);
  auto document = board_document(420, 200);
  const auto board_a = add_board(document, "A", patchy::Rect{0, 0, 150, 150}, patchy::ArtboardBackground::White,
                                 QColor());
  const auto board_b = add_board(document, "B", patchy::Rect{200, 0, 200, 100}, patchy::ArtboardBackground::White,
                                 QColor());
  const auto add_art = [&document](patchy::LayerId board_id, const char* name, patchy::Rect rect) {
    patchy::Layer art(document.allocate_layer_id(), name,
                      solid_pixels(rect.width, rect.height, patchy::PixelFormat::rgba8(), QColor(Qt::transparent)));
    art.set_bounds(rect);
    const auto id = art.id();
    document.find_layer(board_id)->add_child(std::move(art));
    return id;
  };
  const auto art_a = add_art(board_a, "A Art", patchy::Rect{0, 0, 150, 150});
  const auto art_b = add_art(board_b, "B Art", patchy::Rect{200, 0, 200, 100});
  window.add_document_session(std::move(document), QStringLiteral("Add boards"));
  QApplication::processEvents();
  using Access = patchy::ui::MainWindowTestAccess;
  Access::select_layers(window, {art_a}, art_a);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(220, 30, 30));
  canvas->set_brush_size(12);
  QApplication::processEvents();
  const auto& doc = std::as_const(Access::document(window));

  // Painting on B makes it the active, last-edited artboard.
  canvas_drag(*canvas, QPoint(230, 50), QPoint(320, 50));
  CHECK(doc.active_layer_id() == art_b);
  require_hotkey_action(window, QStringLiteral("tools.artboard"))->trigger();
  QApplication::processEvents();
  auto* width = window.findChild<QSpinBox*>(QStringLiteral("artboardWidthSpin"));
  auto* height = window.findChild<QSpinBox*>(QStringLiteral("artboardHeightSpin"));
  auto* add = window.findChild<QPushButton*>(QStringLiteral("artboardAddButton"));
  CHECK(width != nullptr && height != nullptr && add != nullptr);
  if (width == nullptr || height == nullptr || add == nullptr) {
    return;
  }
  CHECK(width->value() == 200 && height->value() == 100);

  add->click();
  QApplication::processEvents();
  const auto artboards = patchy::document_artboards(std::as_const(Access::document(window)));
  CHECK(artboards.size() == 3U);
  const patchy::Layer* added = nullptr;
  for (const auto* artboard : artboards) {
    if (artboard->id() != board_a && artboard->id() != board_b) {
      added = artboard;
    }
  }
  CHECK(added != nullptr);
  if (added != nullptr) {
    CHECK(added->artboard()->rect.width == 120 && added->artboard()->rect.height == 90);
  }
}


// Save As to .pdf shows the one PDF Options dialog (docs/pdf.md) after the file name: the
// pages of this document only (no open-documents source), starting on its artboards; the
// whole-canvas choice writes a single page.
void ui_save_as_pdf_uses_the_pdf_options_dialog() {
  SettingsValueRestorer source_restorer(QStringLiteral("exportOptions/multiPagePdfSource"));
  QTemporaryDir temp;
  CHECK(temp.isValid());
  patchy::ui::MainWindow window;
  show_window(window);
  auto seed = board_document(300, 200);
  add_board(seed, "One", patchy::Rect{0, 0, 100, 100}, patchy::ArtboardBackground::White, QColor(0, 0, 255));
  add_board(seed, "Two", patchy::Rect{150, 0, 100, 60}, patchy::ArtboardBackground::White, QColor(255, 0, 0));
  window.add_document_session(std::move(seed), QStringLiteral("Boards"));
  QApplication::processEvents();
  const auto path = temp.filePath(QStringLiteral("boards.pdf"));
  auto seen = std::make_shared<bool>(false);
  accept_save_file_dialog_then(QStringLiteral("saveAsFileDialog"), path, [seen] {
    auto* dialog = find_top_level_dialog(QStringLiteral("multiPagePdfExportDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    CHECK(dialog->windowTitle() == QStringLiteral("PDF Options"));
    auto* documents = dialog->findChild<QRadioButton*>(QStringLiteral("multiPagePdfDocumentsRadio"));
    auto* artboards = dialog->findChild<QRadioButton*>(QStringLiteral("multiPagePdfArtboardsRadio"));
    auto* canvas = dialog->findChild<QRadioButton*>(QStringLiteral("multiPagePdfCanvasRadio"));
    auto* summary = dialog->findChild<QLabel*>(QStringLiteral("multiPagePdfSummaryLabel"));
    auto* save = dialog->findChild<QPushButton*>(QStringLiteral("multiPagePdfExportButton"));
    CHECK(documents != nullptr && artboards != nullptr && canvas != nullptr && summary != nullptr && save != nullptr);
    if (documents == nullptr || artboards == nullptr || canvas == nullptr || summary == nullptr || save == nullptr) {
      dialog->reject();
      return;
    }
    CHECK(!documents->isVisible());
    CHECK(artboards->isChecked());
    CHECK(summary->text().contains(QStringLiteral("2")));
    CHECK(save->text() == QStringLiteral("Save"));
    canvas->setChecked(true);
    QApplication::processEvents();
    CHECK(summary->text().contains(QStringLiteral("1")));
    *seen = true;
    save->click();
  });
  require_action(window, "fileSaveAsAction")->trigger();
  QApplication::processEvents();
  CHECK(*seen);
  CHECK(QFileInfo::exists(path));
#if defined(PATCHY_HAVE_QT_PDF)
  QPdfDocument reader;
  CHECK(reader.load(path) == QPdfDocument::Error::None);
  CHECK(reader.pageCount() == 1);
#endif
}


// A slow save shows the Saving dialog (docs/file-formats.md, "Save progress"): the PSD
// writer runs on a worker and names each artboard as it is written. The delay is forced to
// zero so this small document shows it too.
void ui_save_shows_progress_with_artboard_names() {
  EnvironmentVariableRestorer delay_restorer("PATCHY_SAVE_PROGRESS_DELAY_MS");
  qputenv("PATCHY_SAVE_PROGRESS_DELAY_MS", QByteArray("0"));
  QTemporaryDir temp;
  CHECK(temp.isValid());
  patchy::ui::MainWindow window;
  show_window(window);
  auto seed = board_document(4400, 1500);
  add_board(seed, "North", patchy::Rect{0, 0, 1400, 1400}, patchy::ArtboardBackground::White, QColor(0, 0, 255));
  add_board(seed, "South", patchy::Rect{1500, 0, 1400, 1400}, patchy::ArtboardBackground::White, QColor(255, 0, 0));
  add_board(seed, "West", patchy::Rect{3000, 0, 1400, 1400}, patchy::ArtboardBackground::White, QColor(0, 160, 0));
  window.add_document_session(std::move(seed), QStringLiteral("Boards"));
  QApplication::processEvents();

  QStringList labels;
  QTimer watch;
  watch.setInterval(1);
  QObject::connect(&watch, &QTimer::timeout, [&labels] {
    auto* dialog = qobject_cast<QProgressDialog*>(find_top_level_dialog(QStringLiteral("saveProgressDialog")));
    if (dialog != nullptr && dialog->isVisible() && !labels.contains(dialog->labelText())) {
      labels.push_back(dialog->labelText());
    }
  });
  watch.start();
  const auto path = temp.filePath(QStringLiteral("boards.psd"));
  CHECK(patchy::ui::MainWindowTestAccess::save_document_to_path(window, path));
  watch.stop();
  CHECK(QFileInfo::exists(path));
  CHECK(!labels.isEmpty());
  const bool named = std::any_of(labels.begin(), labels.end(), [](const QString& label) {
    return label.startsWith(QStringLiteral("Saving layer")) || label.startsWith(QStringLiteral("Compositing")) ||
           label.startsWith(QStringLiteral("Writing"));
  });
  CHECK(named);
  QApplication::processEvents();
  auto* left_open = find_top_level_dialog(QStringLiteral("saveProgressDialog"));
  CHECK(left_open == nullptr || !left_open->isVisible());
}


// The artboard feature-interaction audit (docs/artboards.md, "Feature interactions"):
// Photoshop's results, captured by scripts/dev/artboards/ps-artboard-audit.ps1 on the same
// scene, against Patchy's, in the script's one-line form.
std::string audit_describe(const patchy::Document& document) {
  const auto rect_text = [](patchy::Rect r) {
    return std::to_string(r.x) + "," + std::to_string(r.y) + "," + std::to_string(r.x + r.width) + "," +
           std::to_string(r.y + r.height);
  };
  std::string out = "canvas=" + std::to_string(document.width()) + "x" + std::to_string(document.height());
  for (auto it = document.layers().rbegin(); it != document.layers().rend(); ++it) {
    if (it->artboard().has_value()) {
      out += " | AB " + it->name() + "[" + rect_text(it->artboard()->rect) + "]{";
      bool first = true;
      for (auto child = it->children().rbegin(); child != it->children().rend(); ++child) {
        out += (first ? "" : ";") + child->name() + rect_text(child->bounds());
        first = false;
      }
      out += "}";
    } else {
      out += std::string(" | ") + (it->kind() == patchy::LayerKind::Group ? "GRP " : "LYR ") + it->name() + "[" +
             rect_text(it->bounds()) + "]";
    }
  }
  return out;
}

void ui_artboard_feature_interactions_match_photoshop() {
  using Access = patchy::ui::MainWindowTestAccess;
  const auto build = [](patchy::Document& doc, patchy::LayerId& a1, patchy::LayerId& b1, patchy::LayerId& board_b) {
    add_board(doc, "A", patchy::Rect{0, 0, 300, 300}, patchy::ArtboardBackground::White, QColor());
    board_b = add_board(doc, "B", patchy::Rect{400, 0, 300, 250}, patchy::ArtboardBackground::White, QColor());
    auto put = [&doc](const char* board, const char* name, QRect rect, QColor color) {
      patchy::Layer layer(doc.allocate_layer_id(), name,
                          solid_pixels(rect.width(), rect.height(), patchy::PixelFormat::rgba8(), color));
      layer.set_bounds(patchy::Rect{rect.x(), rect.y(), rect.width(), rect.height()});
      const auto id = layer.id();
      if (board == nullptr) {
        doc.add_layer(std::move(layer));
      } else {
        for (auto& top : doc.layers()) {
          if (top.name() == board) top.add_child(std::move(layer));
        }
      }
      return id;
    };
    a1 = put("A", "A1", QRect(20, 20, 100, 100), QColor(0, 0, 255));
    b1 = put("B", "B1", QRect(420, 20, 100, 100), QColor(255, 0, 0));
    put(nullptr, "Loose", QRect(800, 400, 100, 100), QColor(0, 160, 0));
  };
  struct Case {
    const char* name;
    std::function<std::string(patchy::ui::MainWindow&, patchy::LayerId, patchy::LayerId, patchy::LayerId)> run;
    // Pieces Photoshop's result shows that Patchy's must too.
    std::vector<std::string> expected;
  };
  const auto select_rect = [](patchy::ui::MainWindow& window, QRect rect) {
    auto* canvas = require_canvas(window);
    const auto& doc = std::as_const(Access::document(window));
    patchy::PixelBuffer coverage(doc.width(), doc.height(), patchy::PixelFormat::gray8());
    coverage.clear(0);
    for (int y = rect.top(); y <= rect.bottom(); ++y) {
      std::memset(coverage.pixel(rect.left(), y), 255, static_cast<std::size_t>(rect.width()));
    }
    canvas->apply_grayscale_to_selection(coverage);
    QApplication::processEvents();
  };
  const auto active_where = [](patchy::ui::MainWindow& window) {
    const auto& doc = std::as_const(Access::document(window));
    const auto id = doc.active_layer_id().value_or(0);
    const auto* layer = doc.find_layer(id);
    if (layer == nullptr) return std::string("none");
    const auto* owner = patchy::artboard_owning_layer(doc, id);
    const auto b = layer->bounds();
    return layer->name() + " in " + (owner != nullptr ? owner->name() : std::string("root")) + " [" +
           std::to_string(b.x) + "," + std::to_string(b.y) + "," + std::to_string(b.x + b.width) + "," +
           std::to_string(b.y + b.height) + "]";
  };
  const auto trigger = [](patchy::ui::MainWindow& window, const char* id) {
    require_hotkey_action(window, QString::fromLatin1(id))->trigger();
    QApplication::processEvents();
  };
  const std::vector<Case> cases = {
      {"canvas_grow_topleft", [](auto& w, auto, auto, auto) { Access::apply_canvas_size(w, patchy::Rect{0, 0, 1200, 800}); return std::string(); }, {"canvas=1200x800", "AB A[0,0,300,300]{A120,20,120,120}"}},
      {"canvas_grow_center", [](auto& w, auto, auto, auto) { Access::apply_canvas_size(w, patchy::Rect{-100, -100, 1200, 800}); return std::string(); }, {"canvas=1200x800", "AB A[100,100,400,400]{A1120,120,220,220}", "AB B[500,100,800,350]", "Loose[900,500,1000,600]"}},
      {"canvas_grow_bottomright", [](auto& w, auto, auto, auto) { Access::apply_canvas_size(w, patchy::Rect{-200, -200, 1200, 800}); return std::string(); }, {"AB A[200,200,500,500]{A1220,220,320,320}", "Loose[1000,600,1100,700]"}},
      {"canvas_shrink_center", [](auto& w, auto, auto, auto) { Access::apply_canvas_size(w, patchy::Rect{100, 100, 800, 400}); return std::string(); }, {"AB A[0,0,300,300]{A120,20,120,120}", "AB B[400,0,700,250]{B1420,20,520,120}"}},
      {"image_size_half", [](auto& w, auto, auto, auto) { Access::resize_document_image(w, 500, 300); return std::string(); }, {"canvas=500x300", "AB A[0,0,150,150]{A110,10,60,60}", "AB B[200,0,350,125]{B1210,10,260,60}", "Loose[400,200,450,250]"}},
      {"crop", [&](auto& w, auto, auto, auto) { select_rect(w, QRect(100, 50, 500, 350)); trigger(w, "image.crop_to_selection"); return std::string(); }, {"canvas=1000x600", "AB A[0,0,300,300]{A120,20,120,120}", "Loose[800,400,900,500]"}},
      {"rotate_90", [&](auto& w, auto, auto, auto) { trigger(w, "image.rotate_cw"); return std::string(); }, {"canvas=600x1000", "AB A[300,0,600,300]{A1480,20,580,120}", "AB B[350,400,600,700]{B1480,420,580,520}", "Loose[100,800,200,900]"}},
      {"flip_horizontal", [&](auto& w, auto, auto, auto) { trigger(w, "image.flip_horizontal"); return std::string(); }, {"AB A[700,0,1000,300]{A1880,20,980,120}", "AB B[300,0,600,250]{B1480,20,580,120}", "Loose[100,400,200,500]"}},
      {"flatten", [&](auto& w, auto, auto, auto) { trigger(w, "image.flatten"); return std::string(); }, {"canvas=1000x600 | LYR Background[0,0,1000,600]"}},
      {"merge_visible", [&](auto& w, auto, auto, auto) { trigger(w, "layer.merge_visible"); return std::string(); }, {}},
      {"new_layer_with_B1_active", [&](auto& w, auto, auto b1, auto) { Access::select_layers(w, {b1}, b1); trigger(w, "layer.new"); return "new " + active_where(w); }, {"note=new Layer 4 in B"}},
      {"new_layer_with_artboard_B_active", [&](auto& w, auto, auto, auto bb) { Access::select_layers(w, {bb}, bb); trigger(w, "layer.new"); return "new " + active_where(w); }, {"note=new Layer 4 in B"}},
      {"select_all_bounds", [&](auto& w, auto a1, auto, auto) {
         Access::select_layers(w, {a1}, a1);
         trigger(w, "select.all");
         const auto r = require_canvas(w)->selected_document_rect().value_or(QRect());
         return "selection [" + std::to_string(r.x()) + "," + std::to_string(r.y()) + "," +
                std::to_string(r.x() + r.width()) + "," + std::to_string(r.y() + r.height()) + "]";
       }, {"note=selection [0,0,300,300]"}},
      {"copy_paste_same_artboard", [&](auto& w, auto a1, auto, auto) {
         Access::select_layers(w, {a1}, a1); select_rect(w, QRect(20, 20, 100, 100));
         require_action(w, "editCopyAction")->trigger(); trigger(w, "select.deselect");
         trigger(w, "edit.paste"); return "pasted " + active_where(w); }, {"note=pasted Pasted Layer in A [100,100,200,200]"}},
      {"copy_from_A_paste_with_B1_active", [&](auto& w, auto a1, auto b1, auto) {
         Access::select_layers(w, {a1}, a1); select_rect(w, QRect(20, 20, 100, 100));
         require_action(w, "editCopyAction")->trigger(); trigger(w, "select.deselect"); Access::select_layers(w, {b1}, b1);
         trigger(w, "edit.paste"); return "pasted " + active_where(w); }, {"note=pasted Pasted Layer in B [500,75,600,175]"}},
      {"paste_in_place_with_B1_active", [&](auto& w, auto a1, auto b1, auto) {
         Access::select_layers(w, {a1}, a1); select_rect(w, QRect(20, 20, 100, 100));
         require_action(w, "editCopyAction")->trigger(); trigger(w, "select.deselect"); Access::select_layers(w, {b1}, b1);
         trigger(w, "edit.paste_in_place"); return "pasted " + active_where(w); }, {"note=pasted Pasted Layer in B [420,20,520,120]"}},
      // A marquee left active centers Paste on itself, on the artboard under it (GitHub issue 193).
      {"copy_paste_same_artboard_marquee_kept", [&](auto& w, auto a1, auto, auto) {
         Access::select_layers(w, {a1}, a1); select_rect(w, QRect(20, 20, 100, 100));
         require_action(w, "editCopyAction")->trigger(); trigger(w, "edit.paste"); return "pasted " + active_where(w); }, {"note=pasted Pasted Layer in A [20,20,120,120]"}},
      {"copy_from_A_paste_with_B1_active_marquee_kept", [&](auto& w, auto a1, auto b1, auto) {
         Access::select_layers(w, {a1}, a1); select_rect(w, QRect(20, 20, 100, 100));
         require_action(w, "editCopyAction")->trigger(); Access::select_layers(w, {b1}, b1);
         trigger(w, "edit.paste"); return "pasted " + active_where(w); }, {"note=pasted Pasted Layer in A [20,20,120,120]"}},
      {"paste_with_marquee_on_B", [&](auto& w, auto a1, auto b1, auto) {
         Access::select_layers(w, {a1}, a1); select_rect(w, QRect(20, 20, 100, 100));
         require_action(w, "editCopyAction")->trigger(); Access::select_layers(w, {b1}, b1);
         select_rect(w, QRect(550, 100, 100, 100));
         trigger(w, "edit.paste"); return "pasted " + active_where(w); }, {"note=pasted Pasted Layer in B [550,100,650,200]"}},
      {"paste_with_A1_active_marquee_off_artboards", [&](auto& w, auto a1, auto, auto) {
         Access::select_layers(w, {a1}, a1); select_rect(w, QRect(20, 20, 100, 100));
         require_action(w, "editCopyAction")->trigger(); select_rect(w, QRect(800, 400, 100, 100));
         trigger(w, "edit.paste"); return "pasted " + active_where(w); }, {"note=pasted Pasted Layer in root [800,400,900,500]"}},
      {"duplicate_artboard_B", [&](auto& w, auto, auto, auto bb) { Access::select_layers(w, {bb}, bb); trigger(w, "layer.duplicate"); return "duplicate " + active_where(w); }, {"AB B copy[800,0,1100,250]{B1820,20,920,120}"}},
  };
  for (const auto& entry : cases) {
    patchy::ui::MainWindow window;
    show_window(window);
    auto doc = board_document(1000, 600);
    patchy::LayerId a1 = 0;
    patchy::LayerId b1 = 0;
    patchy::LayerId board_b = 0;
    build(doc, a1, b1, board_b);
    window.add_document_session(std::move(doc), QStringLiteral("Audit"));
    QApplication::processEvents();
    const auto note = entry.run(window, a1, b1, board_b);
    QApplication::processEvents();
    const auto after = audit_describe(std::as_const(Access::document(window))) + (note.empty() ? "" : " | note=" + note);
    for (const auto& piece : entry.expected) {
      if (after.find(piece) == std::string::npos) {
        std::printf("  %s: expected \"%s\" in: %s\n", entry.name, piece.c_str(), after.c_str());
        CHECK(false);
      }
    }
  }
  QApplication::clipboard()->clear();
}

// Helpers for the artboard hover and label tests (GitHub issues 259 and 260).

// The Move tool's hover outline is a blue dashed ring on the first pixels outside the
// framed rect: counted on a band around the rect's bottom edge, which no child shares.
int outline_pixels_on_bottom_edge(const QImage& image, patchy::ui::CanvasWidget& canvas, patchy::Rect rect) {
  const auto bottom_left = canvas.widget_position_for_document_point(QPoint(rect.x, rect.y + rect.height));
  const auto bottom_right = canvas.widget_position_for_document_point(QPoint(rect.x + rect.width, rect.y + rect.height));
  const QRect band(bottom_left.x() - 3, bottom_left.y() - 3, bottom_right.x() - bottom_left.x() + 6, 7);
  return count_pixels_close(image, band, QColor(95, 170, 255), 18);
}

// Where an artboard's name label is drawn: above its top-left corner.
QRect label_region(patchy::ui::CanvasWidget& canvas, patchy::Rect rect) {
  const auto top_left = canvas.widget_position_for_document_point(QPoint(rect.x, rect.y));
  return QRect(top_left.x() - 1, top_left.y() - 22, 90, 21);
}

// Pixels of `ink` in the label region that stand out from the region's dominant color
// (the backing): a dark ink is not "found" on a dark pasteboard it nearly matches, so the
// count is the glyph pixels, not the backing.
int ink_pixels(const QImage& image, QRect region, QColor ink) {
  region = region.intersected(image.rect());
  std::map<QRgb, int> histogram;
  for (int y = region.top(); y <= region.bottom(); ++y) {
    for (int x = region.left(); x <= region.right(); ++x) {
      ++histogram[image.pixel(x, y)];
    }
  }
  QRgb backing = 0;
  int best = -1;
  for (const auto& [color, count] : histogram) {
    if (count > best) {
      best = count;
      backing = color;
    }
  }
  const QColor backing_color(backing);
  int count = 0;
  for (int y = region.top(); y <= region.bottom(); ++y) {
    for (int x = region.left(); x <= region.right(); ++x) {
      const auto color = image.pixelColor(x, y);
      const bool stands_out = std::abs(color.red() - backing_color.red()) > 60 ||
                              std::abs(color.green() - backing_color.green()) > 60 ||
                              std::abs(color.blue() - backing_color.blue()) > 60;
      if (stands_out && std::abs(color.red() - ink.red()) <= 36 && std::abs(color.green() - ink.green()) <= 36 &&
          std::abs(color.blue() - ink.blue()) <= 36) {
        ++count;
      }
    }
  }
  return count;
}

// Pixels that stand out from the region's dominant color, whatever they are: ink or its
// halo on a strip of pasteboard or paper.
int stand_out_pixels(const QImage& image, QRect region) {
  region = region.intersected(image.rect());
  std::map<QRgb, int> histogram;
  for (int y = region.top(); y <= region.bottom(); ++y) {
    for (int x = region.left(); x <= region.right(); ++x) {
      ++histogram[image.pixel(x, y)];
    }
  }
  QRgb backing = 0;
  int best = -1;
  for (const auto& [color, count] : histogram) {
    if (count > best) {
      best = count;
      backing = color;
    }
  }
  const QColor backing_color(backing);
  int count = 0;
  for (int y = region.top(); y <= region.bottom(); ++y) {
    for (int x = region.left(); x <= region.right(); ++x) {
      const auto color = image.pixelColor(x, y);
      if (std::abs(color.red() - backing_color.red()) > 60 || std::abs(color.green() - backing_color.green()) > 60 ||
          std::abs(color.blue() - backing_color.blue()) > 60) {
        ++count;
      }
    }
  }
  return count;
}

const char* scheme_label(patchy::ui::ColorSchemePreference preference) {
  return preference == patchy::ui::ColorSchemePreference::Light ? "light" : "dark";
}

// GitHub issue 259: with the Move tool, hovering an artboard's label, or (with
// Auto-Select) its empty area, frames the artboard with the hover outline and turns its
// label the selected color; a click there selects the artboard in the Layers panel. A
// layer inside the artboard keeps its own outline and pick, and Auto-Select off keeps
// dragging the selection from anywhere.
void ui_artboard_move_tool_hover_frames_artboard_and_click_selects_it() {
  for (const auto preference : {patchy::ui::ColorSchemePreference::Dark, patchy::ui::ColorSchemePreference::Light}) {
    ColorSchemeRestorer scheme_restorer(preference);
    patchy::ui::MainWindow window;
    show_window(window);
    auto seed = board_document(400, 300);
    const auto loose = add_solid_layer(seed, "Loose", QRect(300, 200, 40, 40), QColor(0, 160, 0));
    const patchy::Rect board_rect{20, 40, 200, 160};
    const auto board = add_board(seed, "Board", board_rect, patchy::ArtboardBackground::White, QColor(200, 0, 0));
    window.add_document_session(std::move(seed), QStringLiteral("Hover"));
    auto* canvas = require_canvas(window);
    canvas->set_zoom(1.0);
    require_hotkey_action(window, QStringLiteral("tools.move"))->trigger();
    canvas->set_auto_select_layer(true);
    canvas->set_show_transform_controls(false);
    MainWindowTestAccess::select_layers(window, {loose}, loose);
    QApplication::processEvents();
    const auto& document = [&window]() -> const patchy::Document& { return MainWindowTestAccess::document(window); };
    const patchy::Rect child_rect{20, 40, 100, 80};  // the board's art, its top-left quarter
    const auto hover = [&](QPoint document_point, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
      send_mouse(*canvas, QEvent::MouseMove, canvas->widget_position_for_document_point(document_point), Qt::NoButton,
                 Qt::NoButton, modifiers);
      return canvas->grab().toImage();
    };
    const auto click = [&](QPoint document_point) {
      const auto position = canvas->widget_position_for_document_point(document_point);
      send_mouse(*canvas, QEvent::MouseButtonPress, position, Qt::LeftButton, Qt::LeftButton);
      send_mouse(*canvas, QEvent::MouseButtonRelease, position, Qt::LeftButton, Qt::NoButton);
      QApplication::processEvents();
    };

    // The Artboard tool hint names the registry's current binding (Shift+V cycles the
    // Move flyout by default); it is shown on a selecting click, never on a hover.
    const auto hint_prefix = QStringLiteral("Switch to the Artboard tool");
    QString shortcut = patchy::ui::action_shortcut_text(require_hotkey_action(window, QStringLiteral("tools.artboard")));
    if (shortcut.isEmpty()) {
      shortcut = patchy::ui::action_shortcut_text(require_hotkey_action(window, QStringLiteral("tools.cycle.move")));
    }
    CHECK(!shortcut.isEmpty());
    const auto hint = QStringLiteral("Switch to the Artboard tool (%1) to add, resize or duplicate artboards").arg(shortcut);

    // The empty area frames the artboard and lights its label.
    const QPoint empty_area(180, 180);
    window.statusBar()->clearMessage();
    auto image = hover(empty_area);
    save_widget_artifact(std::string("ui_issue_259_hover_") + scheme_label(preference), window);
    CHECK(outline_pixels_on_bottom_edge(image, *canvas, board_rect) > 20);
    CHECK(count_pixels_close(image, label_region(*canvas, board_rect), patchy::ui::theme().canvas_artboard_selected, 40) > 3);
    CHECK(canvas->cursor().shape() == Qt::SizeAllCursor);
    CHECK(!window.statusBar()->currentMessage().startsWith(hint_prefix));

    // The art inside keeps its own outline: the artboard is not framed over it.
    image = hover(QPoint(40, 60));
    CHECK(outline_pixels_on_bottom_edge(image, *canvas, child_rect) > 20);
    CHECK(outline_pixels_on_bottom_edge(image, *canvas, board_rect) < 6);

    // The label frames the artboard with any setting.
    const auto label_point = canvas->widget_position_for_document_point(QPoint(board_rect.x, board_rect.y)) + QPoint(8, -8);
    send_mouse(*canvas, QEvent::MouseMove, label_point, Qt::NoButton, Qt::NoButton);
    image = canvas->grab().toImage();
    CHECK(outline_pixels_on_bottom_edge(image, *canvas, board_rect) > 20);

    // Off the artboard nothing is framed.
    image = hover(QPoint(260, 60));
    CHECK(outline_pixels_on_bottom_edge(image, *canvas, board_rect) < 6);

    // A click on the empty area selects the artboard in the Layers panel and shows the hint.
    click(empty_area);
    CHECK(document().active_layer_id().has_value() && *document().active_layer_id() == board);
    auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
    CHECK(layer_list != nullptr);
    if (layer_list != nullptr) {
      auto* row = require_layer_item(*layer_list, QStringLiteral("Board"));
      CHECK(row != nullptr && row->isSelected());
      CHECK(layer_list->selectedItems().size() == 1);
    }
    CHECK(window.statusBar()->currentMessage() == hint);
    save_widget_artifact(std::string("ui_issue_259_selected_") + scheme_label(preference), window);

    // The label click shows it too, and so does selecting the artboard's row in the panel.
    MainWindowTestAccess::select_layers(window, {loose}, loose);
    window.statusBar()->clearMessage();
    send_mouse(*canvas, QEvent::MouseButtonPress, label_point, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseButtonRelease, label_point, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    CHECK(document().active_layer_id().has_value() && *document().active_layer_id() == board);
    CHECK(window.statusBar()->currentMessage() == hint);
    MainWindowTestAccess::select_layers(window, {loose}, loose);
    window.statusBar()->clearMessage();
    if (layer_list != nullptr) {
      auto* row = require_layer_item(*layer_list, QStringLiteral("Board"));
      layer_list->clearSelection();
      layer_list->setCurrentItem(row);
      row->setSelected(true);
      QApplication::processEvents();
      CHECK(document().active_layer_id().has_value() && *document().active_layer_id() == board);
      CHECK(window.statusBar()->currentMessage() == hint);
    }
    // With the Artboard tool itself a selecting click shows no hint.
    MainWindowTestAccess::select_layers(window, {loose}, loose);
    require_hotkey_action(window, QStringLiteral("tools.artboard"))->trigger();
    QApplication::processEvents();
    window.statusBar()->clearMessage();
    click(empty_area);
    CHECK(document().active_layer_id().has_value() && *document().active_layer_id() == board);
    CHECK(!window.statusBar()->currentMessage().startsWith(hint_prefix));
    require_hotkey_action(window, QStringLiteral("tools.move"))->trigger();
    canvas->set_auto_select_layer(true);
    QApplication::processEvents();

    // Auto-Select off: the empty area is not a pick (a press there drags the selection).
    MainWindowTestAccess::select_layers(window, {loose}, loose);
    canvas->set_auto_select_layer(false);
    image = hover(empty_area);
    CHECK(outline_pixels_on_bottom_edge(image, *canvas, board_rect) < 6);
    click(empty_area);
    CHECK(document().active_layer_id().has_value() && *document().active_layer_id() == loose);
    // Ctrl previews and makes the pick with Auto-Select off too.
    image = hover(empty_area, Qt::ControlModifier);
    CHECK(outline_pixels_on_bottom_edge(image, *canvas, board_rect) > 20);
  }
}

// GitHub issue 260: an artboard's name is ink on whatever it sits on. Over the pasteboard
// it takes the scheme's pasteboard ink, over a neighbouring artboard's white paper the
// dark ink and over black paper the light ink, in both schemes; a user pasteboard color
// moves it the same way.
void ui_artboard_label_ink_follows_what_it_sits_on() {
  const patchy::Rect top_rect{20, 40, 200, 150};
  const patchy::Rect below_rect{20, 190, 200, 150};  // its label sits on Top's white paper
  const patchy::Rect dark_rect{300, 40, 180, 150};
  const patchy::Rect under_rect{300, 190, 180, 150};  // its label sits on Dark's black paper
  for (const auto preference : {patchy::ui::ColorSchemePreference::Dark, patchy::ui::ColorSchemePreference::Light}) {
    ColorSchemeRestorer scheme_restorer(preference);
    patchy::ui::MainWindow window;
    show_window(window);
    auto seed = board_document(520, 420);
    add_board(seed, "Under Board", under_rect, patchy::ArtboardBackground::White, QColor());
    add_board(seed, "Dark Board", dark_rect, patchy::ArtboardBackground::Black, QColor());
    add_board(seed, "Below Board", below_rect, patchy::ArtboardBackground::White, QColor());
    add_board(seed, "Top Board", top_rect, patchy::ArtboardBackground::White, QColor());
    window.add_document_session(std::move(seed), QStringLiteral("Labels"));
    auto* canvas = require_canvas(window);
    canvas->set_zoom(1.0);
    require_hotkey_action(window, QStringLiteral("tools.move"))->trigger();
    QApplication::processEvents();
    const auto& palette = patchy::ui::theme();
    const auto ink_pixels = [&](const QImage& image, patchy::Rect rect, QColor ink) {
      return ::ink_pixels(image, label_region(*canvas, rect), ink);
    };
    const bool light_pasteboard = preference == patchy::ui::ColorSchemePreference::Light;
    const auto pasteboard_ink = light_pasteboard ? palette.canvas_artboard_label_on_light : palette.canvas_artboard_label;

    auto image = canvas->grab().toImage();
    save_widget_artifact(std::string("ui_issue_260_labels_") + scheme_label(preference), window);
    CHECK(ink_pixels(image, top_rect, pasteboard_ink) > 6);
    CHECK(ink_pixels(image, dark_rect, pasteboard_ink) > 6);
    CHECK(ink_pixels(image, below_rect, palette.canvas_artboard_label_on_light) > 6);
    CHECK(ink_pixels(image, under_rect, palette.canvas_artboard_label) > 6);

    // A white pasteboard from the backdrop menu turns the pasteboard labels dark, a black
    // one light, whatever the scheme.
    canvas->set_backdrop_color_override(QColor(Qt::white));
    QApplication::processEvents();
    image = canvas->grab().toImage();
    CHECK(ink_pixels(image, top_rect, palette.canvas_artboard_label_on_light) > 6);
    canvas->set_backdrop_color_override(QColor(Qt::black));
    QApplication::processEvents();
    image = canvas->grab().toImage();
    CHECK(ink_pixels(image, top_rect, palette.canvas_artboard_label) > 6);
    canvas->set_backdrop_color_override(std::nullopt);
  }
}

// GitHub issue 260, Seth's review: a name hanging a few pixels over the board above it
// straddles that board's paper and the pasteboard. The glyph pixels over the paper take
// the dark ink, the part over the pasteboard still reads (a halo in the other ink where
// the pasteboard is dark), and the board above's outline never crosses the name: labels
// draw after every outline.
void ui_artboard_label_reads_across_a_neighbouring_boards_edge() {
  const patchy::Rect top_rect{20, 40, 200, 150};
  const patchy::Rect below_rect{20, 198, 200, 120};  // an 8 px gap, narrower than the label
  for (const auto preference : {patchy::ui::ColorSchemePreference::Dark, patchy::ui::ColorSchemePreference::Light}) {
    ColorSchemeRestorer scheme_restorer(preference);
    patchy::ui::MainWindow window;
    show_window(window);
    auto seed = board_document(520, 420);
    add_board(seed, "Hanging Name", below_rect, patchy::ArtboardBackground::White, QColor());
    add_board(seed, "Top Board", top_rect, patchy::ArtboardBackground::White, QColor());
    window.add_document_session(std::move(seed), QStringLiteral("Straddle"));
    auto* canvas = require_canvas(window);
    canvas->set_zoom(1.0);
    require_hotkey_action(window, QStringLiteral("tools.move"))->trigger();
    QApplication::processEvents();
    const auto& palette = patchy::ui::theme();
    const auto image = canvas->grab().toImage();
    save_widget_artifact(std::string("ui_issue_260_straddle_") + scheme_label(preference), window);

    const auto label = label_region(*canvas, below_rect);
    const int paper_bottom = canvas->widget_position_for_document_point(QPoint(20, 190)).y();  // first gap row
    // Over the paper: the dark ink.
    const QRect on_paper(label.left(), label.top(), label.width(), paper_bottom - 1 - label.top());
    CHECK(ink_pixels(image, on_paper, palette.canvas_artboard_label_on_light) > 6);
    // Over the gap: something readable, not dark ink lost on a dark pasteboard.
    const QRect in_gap(label.left(), paper_bottom, label.width(), 4);
    CHECK(stand_out_pixels(image, in_gap) > 4);
    // The upper board's bottom outline runs through the name and the name wins.
    const QRect outline_row(label.left(), paper_bottom - 1, label.width(), 1);
    CHECK(count_pixels_close(image, outline_row, palette.canvas_artboard_label_on_light, 36) > 2);
  }
}

// GitHub issue 260 on the reporter's file (ag-psd's artboards sample): "custom board" sits
// right under "white board", so its name is drawn on white paper and takes the dark ink,
// while the names over the pasteboard keep the pasteboard ink; and with the Move tool
// (GitHub issue 259) hovering the custom board's empty area frames it.
void ui_artboard_ag_psd_sample_labels_read_on_their_backing_if_available() {
  const auto path = patchy::test::source_root_path() / "local-test-fixtures" / "ag-psd" / "artboards" / "src.psd";
  patchy::test::skip_unless_exists(path);
  SettingsValueRestorer notes_setting(QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(QStringLiteral("imports/showPsdWarningsAndInfo"));
  for (const auto preference : {patchy::ui::ColorSchemePreference::Dark, patchy::ui::ColorSchemePreference::Light}) {
    ColorSchemeRestorer scheme_restorer(preference);
    patchy::ui::MainWindow window;
    show_window(window);
    MainWindowTestAccess::open_document_path(window, patchy::ui::to_qstring(path));
    QApplication::processEvents();
    auto* canvas = require_canvas(window);
    canvas->fit_to_view();
    require_hotkey_action(window, QStringLiteral("tools.move"))->trigger();
    canvas->set_auto_select_layer(true);
    canvas->set_show_transform_controls(false);
    QApplication::processEvents();
    const auto& document = MainWindowTestAccess::document(window);
    const patchy::Layer* white_board = nullptr;
    const patchy::Layer* custom_board = nullptr;
    for (const auto* artboard : patchy::document_artboards(document)) {
      if (artboard->name() == "white board") {
        white_board = artboard;
      } else if (artboard->name() == "custom board") {
        custom_board = artboard;
      }
    }
    CHECK(white_board != nullptr && custom_board != nullptr);
    if (white_board == nullptr || custom_board == nullptr) {
      return;
    }
    const auto& palette = patchy::ui::theme();
    const auto pasteboard_ink = preference == patchy::ui::ColorSchemePreference::Light
                                    ? palette.canvas_artboard_label_on_light
                                    : palette.canvas_artboard_label;
    // Fitted, the label sits in the pasteboard gap below "white board" and keeps the
    // pasteboard ink; at the reporter's zoom the gap is narrower than the label, which
    // then sits on the white paper and takes the dark ink.
    auto image = canvas->grab().toImage();
    CHECK(ink_pixels(image, label_region(*canvas, custom_board->artboard()->rect), pasteboard_ink) > 6);
    CHECK(ink_pixels(image, label_region(*canvas, white_board->artboard()->rect), pasteboard_ink) > 6);
    canvas->set_zoom_centered(0.1);
    QApplication::processEvents();
    image = canvas->grab().toImage();
    save_widget_artifact(std::string("ui_issue_260_ag_psd_") + scheme_label(preference), window);
    CHECK(canvas_content_rect(*canvas).contains(label_region(*canvas, custom_board->artboard()->rect)));
    CHECK(ink_pixels(image, label_region(*canvas, custom_board->artboard()->rect), palette.canvas_artboard_label_on_light) > 6);
    CHECK(ink_pixels(image, label_region(*canvas, white_board->artboard()->rect), pasteboard_ink) > 6);
    // The name hangs over the white board's bottom edge: its part in the pasteboard gap
    // still reads, and the white board's outline does not cross it.
    {
      const auto& white_rect = white_board->artboard()->rect;
      const auto& custom_rect = custom_board->artboard()->rect;
      const auto custom_label = label_region(*canvas, custom_rect);
      const int paper_bottom = canvas->widget_position_for_document_point(QPoint(white_rect.x, white_rect.y + white_rect.height)).y();
      const int custom_top = canvas->widget_position_for_document_point(QPoint(custom_rect.x, custom_rect.y)).y();
      CHECK(custom_label.top() < paper_bottom && paper_bottom < custom_top - 4);
      const QRect in_gap(custom_label.left(), paper_bottom, custom_label.width(), std::max(1, custom_top - 4 - paper_bottom));
      CHECK(stand_out_pixels(image, in_gap) > 4);
      const QRect outline_row(custom_label.left(), paper_bottom - 1, custom_label.width(), 1);
      CHECK(count_pixels_close(image, outline_row, palette.canvas_artboard_label_on_light, 36) > 2);
    }
    canvas->fit_to_view();
    QApplication::processEvents();

    const auto& rect = custom_board->artboard()->rect;
    send_mouse(*canvas, QEvent::MouseMove,
               canvas->widget_position_for_document_point(QPoint(rect.x + rect.width / 2, rect.y + rect.height / 2)),
               Qt::NoButton, Qt::NoButton);
    image = canvas->grab().toImage();
    save_widget_artifact(std::string("ui_issue_259_ag_psd_hover_") + scheme_label(preference), window);
    CHECK(outline_pixels_on_bottom_edge(image, *canvas, rect) > 20);
  }
}

}  // namespace

std::vector<patchy::test::TestCase> artboard_ui_tests() {
  return {
      {"ui_artboard_tool_creates_resizes_moves_and_undoes", ui_artboard_tool_creates_resizes_moves_and_undoes},
      {"ui_artboard_layer_commands_and_fit_canvas", ui_artboard_layer_commands_and_fit_canvas},
      {"ui_artboard_move_tool_moves_artboards_and_auto_nests", ui_artboard_move_tool_moves_artboards_and_auto_nests},
      {"ui_artboard_prevent_auto_nesting_keeps_layer_in_place", ui_artboard_prevent_auto_nesting_keeps_layer_in_place},
      {"ui_artboard_layers_panel_rows_and_properties_panel", ui_artboard_layers_panel_rows_and_properties_panel},
      {"ui_artboard_properties_fields_scrub_as_one_undo_step", ui_artboard_properties_fields_scrub_as_one_undo_step},
      {"ui_artboard_properties_scrub_previews_like_the_handle_drag",
       ui_artboard_properties_scrub_previews_like_the_handle_drag},
      {"ui_artboard_new_document_option", ui_artboard_new_document_option},
      {"ui_new_document_auto_accept_ignores_remembered_artboards",
       ui_new_document_auto_accept_ignores_remembered_artboards},
      {"ui_artboard_exports_to_files_and_print_pages", ui_artboard_exports_to_files_and_print_pages},
      {"ui_artboard_multipage_pdf_source", ui_artboard_multipage_pdf_source},
      {"ui_artboard_commands_and_hotkeys_registered", ui_artboard_commands_and_hotkeys_registered},
      {"ui_script_artboards_add_move_and_export", ui_script_artboards_add_move_and_export},
      {"ui_artboard_drag_across_artboards_previews_the_release", ui_artboard_drag_across_artboards_previews_the_release},
      {"ui_artboard_paint_switches_to_the_clicked_artboards_layer",
       ui_artboard_paint_switches_to_the_clicked_artboards_layer},
      {"ui_artboard_add_button_uses_the_saved_size_not_the_last_edited_artboard",
       ui_artboard_add_button_uses_the_saved_size_not_the_last_edited_artboard},
      {"ui_save_as_pdf_uses_the_pdf_options_dialog", ui_save_as_pdf_uses_the_pdf_options_dialog},
      {"ui_save_shows_progress_with_artboard_names", ui_save_shows_progress_with_artboard_names},
      {"ui_artboard_feature_interactions_match_photoshop", ui_artboard_feature_interactions_match_photoshop},
      {"ui_artboard_move_tool_hover_frames_artboard_and_click_selects_it",
       ui_artboard_move_tool_hover_frames_artboard_and_click_selects_it},
      {"ui_artboard_label_ink_follows_what_it_sits_on", ui_artboard_label_ink_follows_what_it_sits_on},
      {"ui_artboard_label_reads_across_a_neighbouring_boards_edge",
       ui_artboard_label_reads_across_a_neighbouring_boards_edge},
      {"ui_artboard_ag_psd_sample_labels_read_on_their_backing_if_available",
       ui_artboard_ag_psd_sample_labels_read_on_their_backing_if_available},
  };
}
