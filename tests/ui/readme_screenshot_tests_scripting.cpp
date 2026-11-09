#include "core/layer_metadata.hpp"
#include "ui/image_document_io.hpp"
#include "ui/app_settings.hpp"
#include "test_harness.hpp"
#include "local_psd_fixtures.hpp"
#include <QAbstractItemView>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QMetaObject>
#include <QIODevice>
#include <QPushButton>
#include <QTimer>
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
#include <QPoint>
#include <QString>
// README screenshots (shot_readme_*), part 2: the scripting-era scenes
// (Script Manager + Breakout, script options dialogs, Affinity import).
// The group-wide story lives in the aggregator (readme_screenshot_tests.cpp);
// helpers shared with the other part live in readme_screenshot_test_support.
#include "ui/main_window.hpp"
#include "ui/script_editor_dialog.hpp"
#include "ui/script_engine.hpp"
#include <QPlainTextEdit>
#include <QStandardPaths>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include "readme_screenshot_test_support.hpp"

namespace {

using namespace patchy::test::ui;

struct StandardPathsTestMode {
  StandardPathsTestMode() { QStandardPaths::setTestModeEnabled(true); }
  ~StandardPathsTestMode() { QStandardPaths::setTestModeEnabled(false); }
};

QTreeWidgetItem* find_child_item(QTreeWidgetItem* parent, const QString& text) {
  for (int i = 0; i < parent->childCount(); ++i) {
    if (parent->child(i)->text(0) == text) {
      return parent->child(i);
    }
  }
  return nullptr;
}

void wait_for_run_end(patchy::ui::ScriptEngineHost& host, int timeout_ms = 15000) {
  QElapsedTimer timer;
  timer.start();
  while (host.run_active() && timer.elapsed() < timeout_ms) {
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
  }
  // One extra turn so the coalesced refresh flush lands.
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
}

// --- Breakout (scripts/bundled/Games/breakout.js) ---------------------------------

// The sprite layers move each frame; reading their bounds through the const
// document is how the autopilot sees the game state.
std::optional<patchy::Rect> breakout_layer_bounds(patchy::ui::MainWindow& window, const char* name) {
  const auto& document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  for (const auto& layer : document.layers()) {
    if (layer.name() == name) {
      return layer.bounds();
    }
  }
  return std::nullopt;
}

// Opaque pixels left on the Bricks layer (-1 without one).
long long breakout_bricks_opaque(patchy::ui::MainWindow& window) {
  const auto& document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  for (const auto& layer : document.layers()) {
    if (layer.name() == "Bricks") {
      const auto data = layer.pixels().data();
      long long opaque = 0;
      for (std::size_t i = 3; i < data.size(); i += 4) {
        opaque += data[i] != 0 ? 1 : 0;
      }
      return opaque;
    }
  }
  return -1;
}

constexpr long long kBreakoutBrickPixels = 44 * 14;  // breakout.js BRICK_W x BRICK_H

// Loads the bundled Breakout from the Script Manager's tree and runs it, the way a user
// starts it. Returns its controller window (null when something is missing).
QDialog* start_bundled_breakout(patchy::ui::MainWindow& window, patchy::ui::ScriptEditorDialog& dialog) {
  auto* tree = dialog.findChild<QTreeWidget*>(QStringLiteral("scriptEditorTree"));
  auto* code = dialog.findChild<QPlainTextEdit*>(QStringLiteral("scriptEditorCode"));
  auto* console_pane = dialog.findChild<QPlainTextEdit*>(QStringLiteral("scriptEditorConsole"));
  auto* run_button = dialog.findChild<QPushButton*>(QStringLiteral("scriptEditorRunButton"));
  auto* stop_button = dialog.findChild<QPushButton*>(QStringLiteral("scriptEditorStopButton"));
  auto* status = dialog.findChild<QLabel*>(QStringLiteral("scriptEditorStatusLabel"));
  CHECK(tree != nullptr && code != nullptr && console_pane != nullptr);
  CHECK(run_button != nullptr && stop_button != nullptr && status != nullptr);

  QTreeWidgetItem* bundled_root = nullptr;
  for (int i = 0; i < tree->topLevelItemCount(); ++i) {
    if (tree->topLevelItem(i)->text(0) == QStringLiteral("Bundled")) {
      bundled_root = tree->topLevelItem(i);
    }
  }
  CHECK(bundled_root != nullptr);
  auto* games_folder = find_child_item(bundled_root, QStringLiteral("Games"));
  CHECK(games_folder != nullptr);
  auto* breakout_item = find_child_item(games_folder, QStringLiteral("Breakout"));
  CHECK(breakout_item != nullptr);
  CHECK(!breakout_item->icon(0).isNull());
  tree->setCurrentItem(breakout_item);
  QMetaObject::invokeMethod(tree, "itemActivated", Qt::DirectConnection,
                            Q_ARG(QTreeWidgetItem*, breakout_item), Q_ARG(int, 0));
  QApplication::processEvents();
  CHECK(code->toPlainText().contains(QStringLiteral("Breakout")));

  auto& host = window.script_engine_host();
  run_button->click();  // synchronous phase builds the playfield document
  QApplication::processEvents();
  CHECK(host.run_active());
  CHECK(status->text().startsWith(QStringLiteral("Running")));
  CHECK(stop_button->isEnabled());
  CHECK(!run_button->isEnabled());
  CHECK(console_pane->toPlainText().contains(QStringLiteral("Breakout is playing")));
  close_untitled_start_tab(window);

  auto* pad = window.findChild<QDialog*>(QStringLiteral("scriptCanvasWindowDialog"));
  CHECK(pad != nullptr);
  return pad;
}

// Holds Space on the controller window (isKeyDown latches until release, so the ball
// launches now and relaunches at once after a lost life) and steers the paddle under the
// ball so the rally survives.
class BreakoutAutopilot {
public:
  BreakoutAutopilot(patchy::ui::MainWindow& window, QDialog& pad) : window_(window), pad_(pad) {
    send_key_press(pad_, Qt::Key_Space);
  }
  ~BreakoutAutopilot() {
    if (left_down_) {
      send_key_release(pad_, Qt::Key_Left);
    }
    if (right_down_) {
      send_key_release(pad_, Qt::Key_Right);
    }
    send_key_release(pad_, Qt::Key_Space);
  }
  BreakoutAutopilot(const BreakoutAutopilot&) = delete;
  BreakoutAutopilot& operator=(const BreakoutAutopilot&) = delete;

  void steer() {
    const auto ball = breakout_layer_bounds(window_, "Ball");
    const auto paddle = breakout_layer_bounds(window_, "Paddle");
    if (!ball.has_value() || !paddle.has_value()) {
      return;
    }
    const int offset = (ball->x + 3) - (paddle->x + 32);  // ball center vs paddle center
    const bool want_left = offset < -6;
    const bool want_right = offset > 6;
    if (want_left != left_down_) {
      want_left ? send_key_press(pad_, Qt::Key_Left) : send_key_release(pad_, Qt::Key_Left);
      left_down_ = want_left;
    }
    if (want_right != right_down_) {
      want_right ? send_key_press(pad_, Qt::Key_Right) : send_key_release(pad_, Qt::Key_Right);
      right_down_ = want_right;
    }
  }

private:
  patchy::ui::MainWindow& window_;
  QDialog& pad_;
  bool left_down_{false};
  bool right_down_{false};
};

void stop_breakout(patchy::ui::MainWindow& window) {
  auto& host = window.script_engine_host();
  host.stop_active_run();
  wait_for_run_end(host);
  CHECK(!host.run_active());
}

// The Script Manager running the bundled Breakout: the game plays on a real
// document canvas (bricks/paddle/ball are layers, several bricks already
// knocked out) while the manager shows breakout.js loaded from the tree, its
// console line, and the live run status - spinner, elapsed time, and the
// enabled stop button - with the controller window parked below.
void shot_readme_script_manager() {
  EnvironmentVariableRestorer sound_restorer("PATCHY_NO_SOUND");
  qputenv("PATCHY_NO_SOUND", "1");
  // Isolate the user scripts folder so a developer-machine shadow override
  // can never relabel the bundled Breakout row.
  const StandardPathsTestMode test_paths;
  patchy::ui::MainWindow window;
  show_readme_shot_window(window);

  patchy::ui::ScriptEditorDialog dialog(window, window.script_engine_host());
  dialog.show();
  QApplication::processEvents();
  auto* pad = start_bundled_breakout(window, dialog);
  if (pad == nullptr) {
    return;
  }
  const auto full_bricks = breakout_bricks_opaque(window);
  CHECK(full_bricks > 0);

  // The shot should read mid-game: lives left, ball in flight, gaps knocked in the wall.
  // Six cleared bricks plus the ball and paddle sitting in the playfield's right side (the
  // part the Script Manager overlay leaves visible) is the composition target; one
  // cleared brick is the hard CHECK floor.
  const auto composition_ready = [&] {
    if (breakout_bricks_opaque(window) > full_bricks - 6 * kBreakoutBrickPixels) {
      return false;
    }
    const auto ball = breakout_layer_bounds(window, "Ball");
    const auto paddle = breakout_layer_bounds(window, "Paddle");
    return ball.has_value() && paddle.has_value() && ball->x >= 240 && ball->y >= 90 &&
           ball->y <= 310 && paddle->x >= 230;
  };
  {
    BreakoutAutopilot autopilot(window, *pad);
    QElapsedTimer play_timer;
    play_timer.start();
    while (play_timer.elapsed() < 25000 && !composition_ready()) {
      autopilot.steer();
      process_events_for(30);
    }
    CHECK(breakout_bricks_opaque(window) <= full_bricks - kBreakoutBrickPixels);
    CHECK(window.script_engine_host().run_active());

    const QPoint dialog_offset(48, 108);
    dialog.resize(600, 788);
    dialog.move(window.geometry().topLeft() + dialog_offset);
    const QPoint pad_offset(1040, 630);
    pad->move(window.geometry().topLeft() + pad_offset);
    QApplication::processEvents();
    reset_readme_status_bar(window);
    auto base = window.grab().toImage();
    draw_readme_overlay(base, dialog.grab().toImage(), dialog_offset);
    draw_readme_overlay(base, pad->grab().toImage(), pad_offset);
    save_readme_shot("shot_readme_script_manager", base);
  }
  stop_breakout(window);
  dialog.close();
  QApplication::processEvents();
}

// An animated WebP of the bundled Breakout being played (breakout.webp): the bricks,
// paddle and ball are live layers on the document canvas, the Layers panel lists them, and
// the controller window with its score floats over the panel column. The game starts from
// the Script Manager, which then steps aside. Each frame records the real time it took, so
// the animation plays at the game's own speed (breakout.js moves by elapsed time).
void shot_readme_breakout() {
  EnvironmentVariableRestorer sound_restorer("PATCHY_NO_SOUND");
  qputenv("PATCHY_NO_SOUND", "1");
  const StandardPathsTestMode test_paths;
  patchy::ui::MainWindow window;
  show_readme_shot_window(window);
  // Smaller than the stills, so the playfield is a larger share of each frame and the file
  // stays small; still 16:10, so it pairs with the other cards.
  window.resize(1280, 800);
  QApplication::processEvents();

  patchy::ui::ScriptEditorDialog dialog(window, window.script_engine_host());
  dialog.show();
  QApplication::processEvents();
  auto* pad = start_bundled_breakout(window, dialog);
  if (pad == nullptr) {
    return;
  }
  dialog.hide();
  // Without the Color group the layer list is tall enough for the controller to sit under
  // the four game layers.
  if (auto* column = patchy::ui::MainWindowTestAccess::panel_column(window)) {
    column->set_panel_visible(QStringLiteral("colorDock"), false);
    column->set_panel_visible(QStringLiteral("paletteDock"), false);
  }
  QApplication::processEvents();
  require_action(window, "viewFitOnScreenAction")->trigger();
  const auto full_bricks = breakout_bricks_opaque(window);
  CHECK(full_bricks > 0);

  // The controller sits over the empty lower part of the layer list.
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  if (layer_list == nullptr) {
    stop_breakout(window);
    return;
  }
  const QRect list_rect(layer_list->mapTo(&window, QPoint(0, 0)), layer_list->size());
  const QPoint pad_offset(list_rect.right() - pad->width() - 14, list_rect.bottom() - pad->height() - 14);
  pad->move(window.geometry().topLeft() + pad_offset);
  reset_readme_status_bar(window);

  // Twelve seconds of play from the launch, at about 25 frames a second. The writer encodes
  // each frame as the next arrives, so memory holds one frame, not the whole capture.
  constexpr qint64 kCaptureMs = 12000;
  ReadmeAnimationWriter writer("shot_readme_breakout", window.size(), 88);
  {
    BreakoutAutopilot autopilot(window, *pad);
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < kCaptureMs) {
      autopilot.steer();
      process_events_for(25);
      QImage base = window.grab().toImage();
      draw_readme_overlay(base, pad->grab().toImage(), pad_offset);
      writer.add(base, clock.elapsed());
    }
  }
  CHECK(writer.frame_count() > 120);
  CHECK(breakout_bricks_opaque(window) <= full_bricks - 4 * kBreakoutBrickPixels);
  CHECK(window.script_engine_host().run_active());
  stop_breakout(window);
  // A short hold on the last frame before the loop starts over.
  writer.finish(800);
  dialog.close();
  QApplication::processEvents();
}

// The bundled Duotone effect's options dialog - instructions naming the
// active layer, two color pickers, and a contrast slider - floating over the
// photo it already remapped: pass 1 runs unattended exactly like
// --run-script (showOptions applies its defaults with no dialog), pass 2
// re-runs in GUI mode and is cancelled after the capture.
void shot_readme_script_options() {
  const auto photo_path = patchy::test::local_psd_fixture_path("akiko_cycling_okinawa.jpg");
  patchy::test::skip_unless_exists(photo_path);
  QImage photo(QString::fromStdString(photo_path.string()));
  CHECK(!photo.isNull());
  patchy::ui::MainWindow window;
  show_readme_shot_window(window);
  window.add_document_session(patchy::ui::document_from_qimage(photo, "akiko_cycling_okinawa"),
                              QStringLiteral("akiko_cycling_okinawa.jpg"));
  QApplication::processEvents();
  close_untitled_start_tab(window);
  require_action(window, "viewFitOnScreenAction")->trigger();
  QApplication::processEvents();
  auto* canvas = require_canvas(window);

  const auto duotone_path = QDir(patchy::ui::MainWindow::bundled_scripts_directory())
                                .absoluteFilePath(QStringLiteral("Effects/duotone.js"));
  CHECK(QFile::exists(duotone_path));
  QFile script_file(duotone_path);
  CHECK(script_file.open(QIODevice::ReadOnly | QIODevice::Text));
  const auto source = QString::fromUtf8(script_file.readAll());

  const QPoint photo_sample(photo.width() / 2, photo.height() / 2);
  const auto before_color = canvas_pixel(*canvas, photo_sample);

  auto& host = window.script_engine_host();
  {
    patchy::ui::ScriptEngineHost::RunOptions options;
    options.name = QStringLiteral("Duotone");
    options.path = duotone_path;
    options.unattended = true;
    (void)host.run_source(source, std::move(options));
    wait_for_run_end(host);
    CHECK(!host.run_active());
    CHECK(!host.last_run_had_error());
  }
  CHECK(process_events_until(
      [&] { return !color_close(canvas_pixel(*canvas, photo_sample), before_color, 8); }, 10000));

  bool captured = false;
  QTimer::singleShot(0, guarded([&] {
    auto* form = find_top_level_dialog(QStringLiteral("scriptFormDialog"));
    CHECK(form != nullptr);
    CHECK(form->findChild<QLabel*>(QStringLiteral("scriptFormDescription")) != nullptr);
    const QPoint form_offset(1010, 290);
    form->move(window.geometry().topLeft() + form_offset);
    process_events_for(120);
    reset_readme_status_bar(window);
    auto base = window.grab().toImage();
    draw_readme_overlay(base, form->grab().toImage(), form_offset);
    save_readme_shot("shot_readme_script_options", base);
    captured = true;
    form->reject();  // cancelled showOptions ends the run without a second remap
  }));
  {
    patchy::ui::ScriptEngineHost::RunOptions options;
    options.name = QStringLiteral("Duotone");
    options.path = duotone_path;
    (void)host.run_source(source, std::move(options));
  }
  QApplication::processEvents();
  CHECK(captured);
  wait_for_run_end(host);
  CHECK(!host.run_active());
}

// Depth-first walk over every imported text layer (Affinity text arrives as
// metadata-tagged text layers, so layer_is_text is the discriminator, not
// LayerKind). Layers come bottom-to-top, so the last hit is the topmost;
// each hit records the folder chain leading to it, name last.
struct ReadmeTextHit {
  const patchy::Layer* layer{nullptr};
  std::vector<QString> chain;
};

void readme_collect_text_layers(const std::vector<patchy::Layer>& layers,
                                std::vector<QString>& chain, std::vector<ReadmeTextHit>& hits) {
  for (const auto& layer : layers) {
    if (layer.kind() == patchy::LayerKind::Group) {
      chain.push_back(QString::fromStdString(layer.name()));
      readme_collect_text_layers(layer.children(), chain, hits);
      chain.pop_back();
      continue;
    }
    if (patchy::layer_is_text(layer)) {
      auto with_name = chain;
      with_name.push_back(QString::fromStdString(layer.name()));
      hits.push_back({&layer, std::move(with_name)});
    }
  }
}

// An Affinity Photo document (the Dink Smallwood HD quick-tips screen) opened
// straight into layers: rasters, editable text with per-run styling, and the
// layer panel showing the imported structure with a text row selected.
void shot_readme_affinity_import() {
  const auto path = patchy::test::local_format_fixture_path("af-spike/corpus", "tips.af");
  patchy::test::skip_unless_exists(path);
  SettingsValueRestorer notices_restorer(QStringLiteral("imports/showPsdWarningsAndInfo"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("imports/showPsdWarningsAndInfo"), false);
    settings.sync();
  }
  patchy::ui::MainWindow window;
  show_readme_shot_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(window,
                                                       QString::fromStdString(path.string()));
  QApplication::processEvents();
  close_untitled_start_tab(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(document.width() > 0);
  CHECK(!std::as_const(document).layers().empty());
  patchy::ui::MainWindowTestAccess::set_right_dock_stack_width(window, 380);
  require_action(window, "viewFitOnScreenAction")->trigger();
  QApplication::processEvents();

  std::vector<QString> chain;
  std::vector<ReadmeTextHit> text_hits;
  readme_collect_text_layers(std::as_const(document).layers(), chain, text_hits);
  CHECK(!text_hits.empty());
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  // Expand the topmost folder that holds text so the group-to-folder story is
  // visible in the panel, then select the topmost text layer itself (the
  // document title) so a text row reads as selected.
  for (auto it = text_hits.rbegin(); it != text_hits.rend(); ++it) {
    if (it->chain.size() >= 2) {
      for (std::size_t depth = 0; depth + 1 < it->chain.size(); ++depth) {
        expand_layer_folder_row(*layer_list, it->chain[depth]);
      }
      break;
    }
  }
  const auto& top_text = text_hits.back();
  for (std::size_t depth = 0; depth + 1 < top_text.chain.size(); ++depth) {
    expand_layer_folder_row(*layer_list, top_text.chain[depth]);
  }
  auto* text_item = require_layer_item(*layer_list, top_text.chain.back());
  layer_list->clearSelection();
  layer_list->setCurrentItem(text_item);
  text_item->setSelected(true);
  layer_list->scrollToItem(text_item, QAbstractItemView::PositionAtTop);
  QApplication::processEvents();

  reset_readme_status_bar(window);
  save_readme_shot("shot_readme_affinity_import", window.grab().toImage());
}

}  // namespace

std::vector<patchy::test::TestCase> readme_screenshot_tests_part2() {
  return {
      {"shot_readme_script_manager", shot_readme_script_manager},
      {"shot_readme_breakout", shot_readme_breakout},
      {"shot_readme_script_options", shot_readme_script_options},
      {"shot_readme_affinity_import", shot_readme_affinity_import},
  };
}
