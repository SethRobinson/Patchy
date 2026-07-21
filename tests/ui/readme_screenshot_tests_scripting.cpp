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

  const auto bricks_opaque = [&window]() -> long long {
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
  };
  const auto full_bricks = bricks_opaque();
  CHECK(full_bricks > 0);
  constexpr long long kBrickPixels = 44 * 14;  // breakout.js BRICK_W x BRICK_H

  // The sprite layers move each frame; reading their bounds through the const
  // document is how the autopilot below sees the game state.
  const auto layer_bounds = [&window](const char* name) -> std::optional<patchy::Rect> {
    const auto& document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
    for (const auto& layer : document.layers()) {
      if (layer.name() == name) {
        return layer.bounds();
      }
    }
    return std::nullopt;
  };

  // Hold Space on the controller window (isKeyDown latches until release, so
  // the ball launches now and relaunches instantly after a lost life) and
  // steer the paddle under the ball so the rally survives - the shot should
  // read mid-game: lives left, ball in flight, gaps knocked in the wall. Six
  // cleared bricks plus the ball and paddle sitting in the playfield's right
  // side (the part the Script Manager overlay leaves visible) is the
  // composition target; one cleared brick is the hard CHECK floor.
  const auto composition_ready = [&] {
    if (bricks_opaque() > full_bricks - 6 * kBrickPixels) {
      return false;
    }
    const auto ball = layer_bounds("Ball");
    const auto paddle = layer_bounds("Paddle");
    return ball.has_value() && paddle.has_value() && ball->x >= 240 && ball->y >= 90 &&
           ball->y <= 310 && paddle->x >= 230;
  };
  send_key_press(*pad, Qt::Key_Space);
  bool left_down = false;
  bool right_down = false;
  QElapsedTimer play_timer;
  play_timer.start();
  while (play_timer.elapsed() < 25000 && !composition_ready()) {
    const auto ball = layer_bounds("Ball");
    const auto paddle = layer_bounds("Paddle");
    if (ball.has_value() && paddle.has_value()) {
      const int offset = (ball->x + 3) - (paddle->x + 32);  // ball center vs paddle center
      const bool want_left = offset < -6;
      const bool want_right = offset > 6;
      if (want_left != left_down) {
        want_left ? send_key_press(*pad, Qt::Key_Left) : send_key_release(*pad, Qt::Key_Left);
        left_down = want_left;
      }
      if (want_right != right_down) {
        want_right ? send_key_press(*pad, Qt::Key_Right) : send_key_release(*pad, Qt::Key_Right);
        right_down = want_right;
      }
    }
    process_events_for(30);
  }
  if (left_down) {
    send_key_release(*pad, Qt::Key_Left);
  }
  if (right_down) {
    send_key_release(*pad, Qt::Key_Right);
  }
  CHECK(bricks_opaque() <= full_bricks - kBrickPixels);
  CHECK(host.run_active());

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

  send_key_release(*pad, Qt::Key_Space);
  host.stop_active_run();
  wait_for_run_end(host);
  CHECK(!host.run_active());
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
  if (!std::filesystem::exists(photo_path)) {
    std::cout << "[SKIP] akiko_cycling_okinawa fixture missing: " << photo_path.string() << '\n';
    return;
  }
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
  QTimer::singleShot(0, [&] {
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
  });
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
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] af-spike tips fixture missing: " << path.string() << '\n';
    return;
  }
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
      {"shot_readme_script_options", shot_readme_script_options},
      {"shot_readme_affinity_import", shot_readme_affinity_import},
  };
}
