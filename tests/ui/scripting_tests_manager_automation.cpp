#include <QClipboard>
#include <QStatusBar>
#include "core/document.hpp"
#include <QImageReader>
#include "ui/canvas_widget.hpp"
#include "ui/ai_control_paths.hpp"
#include "ui/ai_setup_dialog.hpp"
#include "ui/localization.hpp"
#include "ui/main_window.hpp"
#include "ui/script_editor_dialog.hpp"
#include <QFileInfo>
#include "ui/script_engine.hpp"
#include "ui/script_folders.hpp"
#include "ui/sound_effects.hpp"
#include "ui/theme_manager.hpp"
#include "test_harness.hpp"
#include "ui/ui_test_access.hpp"
#include "ui_test_support.hpp"
#include "unicode_path_names.hpp"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFontMetrics>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBrowser>
#include <QTimer>
#include <span>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include <vector>
#include "scripting_test_support.hpp"

namespace {
using namespace patchy::test::ui;
using namespace patchy::test::ui::scripting_support;

// The New button (the folder context menu's New Script entry calls the same
// path) seeds the editor with the starter template - header directives plus a
// hello console.log - left unmodified so browsing away never prompts. The
// template must actually run: Run executes it against the startup document.
void ui_script_manager_new_button_inserts_template() {
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::ScriptEditorDialog dialog(window, window.script_engine_host());
  dialog.show();
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  auto* code = dialog.findChild<QPlainTextEdit*>(QStringLiteral("scriptEditorCode"));
  auto* console_pane = dialog.findChild<QPlainTextEdit*>(QStringLiteral("scriptEditorConsole"));
  auto* new_button = dialog.findChild<QPushButton*>(QStringLiteral("scriptEditorNewButton"));
  auto* run_button = dialog.findChild<QPushButton*>(QStringLiteral("scriptEditorRunButton"));
  auto* file_label = dialog.findChild<QLabel*>(QStringLiteral("scriptEditorFileLabel"));
  CHECK(code != nullptr && console_pane != nullptr && new_button != nullptr &&
        run_button != nullptr && file_label != nullptr);
  new_button->click();
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  CHECK(code->toPlainText().contains(QStringLiteral("// @name ")));
  CHECK(code->toPlainText().contains(QStringLiteral("// @description ")));
  CHECK(code->toPlainText().contains(QStringLiteral("// @author ")));
  CHECK(code->toPlainText().contains(QStringLiteral("console.log")));
  CHECK(!code->document()->isModified());
  CHECK(file_label->text() == QStringLiteral("untitled.js"));
  run_button->click();
  wait_for_run_end(window.script_engine_host());
  CHECK(console_pane->toPlainText().contains(QStringLiteral("Hi!")));
  CHECK(console_pane->toPlainText().contains(QStringLiteral("1024x768")));
  dialog.close();
}

// The script browser model: @name/@window header parsing, sidecar icon
// resolution with the user-over-bundled override, display-name sorting, and
// the Set Icon write helpers (script_folders.hpp).
void ui_script_metadata_icons_and_write_target() {
  QTemporaryDir temp;
  CHECK(temp.isValid());
  const QDir root(temp.path());
  CHECK(root.mkpath(QStringLiteral("bundled/Games")));
  CHECK(root.mkpath(QStringLiteral("user/Games")));
  const auto bundled_root = root.absoluteFilePath(QStringLiteral("bundled"));
  const auto user_root = root.absoluteFilePath(QStringLiteral("user"));
  auto write_file = [](const QString& path, const QByteArray& content) {
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(content);
  };
  write_file(root.absoluteFilePath(QStringLiteral("bundled/Games/zap.js")),
             "// @name Aardvark Attack\n"
             "// @description Aardvarks attack the\n"
             "// @description whole canvas.\n"
             "// @author Seth A. Robinson\n"
             "// @window\nvar x = 1;\n");
  write_file(root.absoluteFilePath(QStringLiteral("bundled/Games/alpha.js")),
             "var y = 2;\n");
  QImage red(32, 16, QImage::Format_RGBA8888);
  red.fill(Qt::red);

  // Header parsing: @name/@description (repeated lines join)/@author/@window;
  // directives stop at the first non-comment line.
  const auto meta =
      patchy::ui::read_script_metadata(root.absoluteFilePath(QStringLiteral("bundled/Games/zap.js")));
  CHECK(meta.name == QStringLiteral("Aardvark Attack"));
  CHECK(meta.description == QStringLiteral("Aardvarks attack the whole canvas."));
  CHECK(meta.author == QStringLiteral("Seth A. Robinson"));
  CHECK(meta.opens_window);
  write_file(root.absoluteFilePath(QStringLiteral("bundled/Games/late.js")),
             "var z = 3;\n// @name Too Late\n// @window\n");
  const auto late =
      patchy::ui::read_script_metadata(root.absoluteFilePath(QStringLiteral("bundled/Games/late.js")));
  CHECK(late.name.isEmpty());
  CHECK(!late.opens_window);

  // Scan: display-name fallback, display-name sort ("Aardvark Attack" sorts
  // before the fallback-named "alpha" despite zap.js > alpha.js), and the
  // bundled sidecar icon.
  write_file(root.absoluteFilePath(QStringLiteral("bundled/Games/alpha.png")), "not-a-real-png");
  auto scan = patchy::ui::scan_scripts(bundled_root, user_root);
  CHECK(scan.bundled.size() == 1);
  const auto& games = scan.bundled[0].children;
  CHECK(games.size() == 3);
  CHECK(games[0].display_name == QStringLiteral("Aardvark Attack"));
  CHECK(games[0].opens_window);
  CHECK(games[0].icon_path.isEmpty());
  CHECK(games[1].display_name == QStringLiteral("alpha"));
  CHECK(!games[1].opens_window);
  CHECK(games[1].icon_path ==
        QDir(bundled_root).absoluteFilePath(QStringLiteral("Games/alpha.png")));

  // A user icon at the same relative path overrides the bundled one WITHOUT
  // the .js being overridden (what Set Icon writes).
  const auto target = patchy::ui::script_icon_write_target(
      user_root, QStringLiteral("Games/alpha.js"));
  CHECK(target == QDir(user_root).absoluteFilePath(QStringLiteral("Games/alpha.png")));
  CHECK(patchy::ui::write_script_icon(red, target));
  const QImage written(target);
  CHECK(written.width() == 128 && written.height() == 128);
  scan = patchy::ui::scan_scripts(bundled_root, user_root);
  const auto& games_after = scan.bundled[0].children;
  CHECK(games_after[1].display_name == QStringLiteral("alpha"));
  CHECK(!games_after[1].is_override);
  CHECK(games_after[1].icon_path == target);
  // The unreadable bundled "png" falls back to the generic painted icon; the
  // real user icon is used as-is.
  CHECK(!patchy::ui::script_entry_icon(games_after[1]).isNull());
}

// Set Icon from Current Window, end to end minus the literal right-click: the
// handler captures the active document's composite and writes the 64x64
// user-folder icon PNG that then overrides the bundled one.
void ui_script_manager_set_icon_from_document() {
  const StandardPathsTestMode test_paths;
  remove_test_scratch_dir(patchy::ui::MainWindow::user_scripts_directory());
  patchy::ui::MainWindow window;
  show_window(window);  // opens the historical startup document
  patchy::ui::ScriptEditorDialog dialog(window, window.script_engine_host());
  dialog.show();
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  auto* tree = dialog.findChild<QTreeWidget*>(QStringLiteral("scriptEditorTree"));
  auto* console = dialog.findChild<QPlainTextEdit*>(QStringLiteral("scriptEditorConsole"));
  CHECK(tree != nullptr && console != nullptr);
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
  QMetaObject::invokeMethod(&dialog, "set_script_icon_from_window", Qt::DirectConnection,
                            Q_ARG(QTreeWidgetItem*, breakout_item));
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  const auto icon_path = QDir(patchy::ui::MainWindow::user_scripts_directory())
                             .absoluteFilePath(QStringLiteral("Games/breakout.png"));
  CHECK(QFile::exists(icon_path));
  const QImage written(icon_path);
  CHECK(written.width() == 128 && written.height() == 128);
  CHECK(console->toPlainText().contains(QStringLiteral("Saved icon to")));
  // The refreshed tree resolves the user icon for the (unmodified) bundled
  // script.
  bundled_root = nullptr;
  for (int i = 0; i < tree->topLevelItemCount(); ++i) {
    if (tree->topLevelItem(i)->text(0) == QStringLiteral("Bundled")) {
      bundled_root = tree->topLevelItem(i);
    }
  }
  CHECK(bundled_root != nullptr);
  games_folder = find_child_item(bundled_root, QStringLiteral("Games"));
  CHECK(games_folder != nullptr);
  breakout_item = find_child_item(games_folder, QStringLiteral("Breakout"));
  CHECK(breakout_item != nullptr);
  CHECK(breakout_item->data(0, Qt::UserRole + 1).toString().isEmpty());  // still not "modified"
  dialog.close();
  remove_test_scratch_dir(patchy::ui::MainWindow::user_scripts_directory());
}

// showOptions: --script-arg values override the field defaults (coerced by
// type; a bare flag token turns a checkbox on) and an unattended run answers
// without any dialog - app.alert logs instead of showing, the forwarded-CLI
// contract.
void ui_script_show_options_unattended_merges_args() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto& host = window.script_engine_host();
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("options-test");
  options.unattended = true;
  options.args = QStringList{QStringLiteral("scale=7.5"), QStringLiteral("on=false"),
                             QStringLiteral("mode=jpg"), QStringLiteral("out=C:/tmp/x"),
                             QStringLiteral("flag")};
  const auto source = QStringLiteral(R"JS(
    var r = patchy.ui.showOptions({title: 'T', description: 'unused when unattended',
      fields: [
        {key: 'scale', type: 'number', value: 42, min: 0, max: 100},
        {key: 'on', type: 'checkbox', value: true},
        {key: 'flag', type: 'checkbox', value: false},
        {key: 'mode', type: 'choice', value: 'png', choices: ['png', 'jpg']},
        {key: 'out', type: 'folder', value: ''},
        {key: 'label', type: 'text', value: 'hi'}]});
    console.log('opt=' + r.scale + ',' + r.on + ',' + r.flag + ',' + r.mode + ',' +
                r.out + ',' + r.label);
    app.alert('quiet');
  )JS");
  (void)host.run_source(source, std::move(options));
  wait_for_run_end(host);
  CHECK(!host.last_run_had_error());
  CHECK(backlog_contains(window, QStringLiteral("opt=7.5,false,true,jpg,C:/tmp/x,hi")));
  CHECK(backlog_contains(window, QStringLiteral("[alert] quiet")));
}

// The GUI path of showOptions: the dialog carries the description label and a
// folder row with its Browse button, args still pre-fill the fields, and OK
// returns the values.
void ui_script_show_options_dialog_description_and_folder() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto& host = window.script_engine_host();
  bool accepted = false;
  bool saw_description = false;
  bool saw_browse = false;
  // Repeating dismisser (the import-notices pattern): accept the form dialog
  // once it appears inside the script's nested exec loop.
  auto* dismisser = new QTimer(&window);
  QObject::connect(dismisser, &QTimer::timeout, &window,
                   [&accepted, &saw_description, &saw_browse] {
                     for (auto* widget : QApplication::topLevelWidgets()) {
                       auto* dialog = qobject_cast<QDialog*>(widget);
                       if (dialog != nullptr && dialog->isVisible() &&
                           dialog->objectName() == QStringLiteral("scriptFormDialog")) {
                         saw_description =
                             dialog->findChild<QLabel*>(QStringLiteral("scriptFormDescription")) !=
                             nullptr;
                         saw_browse = dialog->findChild<QPushButton*>(QStringLiteral(
                                          "scriptFormField_outBrowse")) != nullptr;
                         if (!accepted) {
                           save_widget_artifact("script_options_dialog", *dialog);
                         }
                         accepted = true;
                         dialog->accept();
                       }
                     }
                   });
  dismisser->start(50);
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("options-gui");
  options.args = QStringList{QStringLiteral("out=D:/pictures")};
  (void)host.run_source(QStringLiteral(R"JS(
    var r = patchy.ui.showOptions({title: 'T', description: 'Pick things.', fields: [
      {key: 'out', type: 'folder', value: ''},
      {key: 'n', type: 'number', value: 3, min: 0, max: 9}]});
    console.log('gui=' + (r ? r.out + ',' + r.n : 'null'));
  )JS"),
                         std::move(options));
  wait_for_run_end(host);
  dismisser->stop();
  dismisser->deleteLater();
  CHECK(accepted);
  CHECK(saw_description);
  CHECK(saw_browse);
  CHECK(backlog_contains(window, QStringLiteral("gui=D:/pictures,3")));
}

// A synchronous burst blocking the GUI past the (env-shortened) threshold
// shows the canvas processing overlay automatically and drops it when the
// burst ends; script timers defer instead of re-entering the mid-evaluation
// engine while the pump processes events.
void ui_script_busy_overlay_and_timer_guard() {
  const EnvironmentVariableRestorer restore_busy_delay("PATCHY_SCRIPT_BUSY_DELAY_MS");
  qputenv("PATCHY_SCRIPT_BUSY_DELAY_MS", "0");
  {
    patchy::ui::MainWindow window;
    show_window(window);
    auto* canvas = window.findChild<patchy::ui::CanvasWidget*>();
    CHECK(canvas != nullptr);
    const auto overlays_before = canvas->render_cache_diagnostics().processing_overlays_shown;
    CHECK(run_script(window, QStringLiteral(R"JS(
      var doc = app.activeDocument;
      var layer = doc.addLayer('busy');
      var ticks = 0;
      setTimeout(function () { ticks++; console.log('tick=' + ticks); }, 0);
      var start = Date.now();
      while (Date.now() - start < 200) {
        layer.fillRect(0, 0, 2, 2, '#00ff00');
      }
      console.log('sync-done ticks=' + ticks);
    )JS")));
    // The 0ms timer must NOT have fired inside the synchronous burst (the
    // pump processes events mid-evaluation; the guard defers it)...
    CHECK(backlog_contains(window, QStringLiteral("sync-done ticks=0")));
    // ...but it still fires right afterwards.
    CHECK(backlog_contains(window, QStringLiteral("tick=1")));
    CHECK(canvas->render_cache_diagnostics().processing_overlays_shown > overlays_before);
    CHECK(!canvas->processing_overlay_visible());
  }
}

// The hover card (driven directly; real hover timing needs a live pointer):
// shows beside a script row with the metadata details painted in.
void ui_script_manager_hover_card_shows_details() {
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::ScriptEditorDialog dialog(window, window.script_engine_host());
  dialog.show();
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  auto* tree = dialog.findChild<QTreeWidget*>(QStringLiteral("scriptEditorTree"));
  CHECK(tree != nullptr);
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
  // Breakout ships @description and @author, so the card has real content.
  CHECK(!breakout_item->data(0, Qt::UserRole + 6).toString().isEmpty());
  CHECK(breakout_item->data(0, Qt::UserRole + 7).toString() ==
        QStringLiteral("Seth A. Robinson"));
  QMetaObject::invokeMethod(&dialog, "show_script_hover_card", Qt::DirectConnection,
                            Q_ARG(QTreeWidgetItem*, breakout_item));
  auto* card = dialog.findChild<QWidget*>(QStringLiteral("scriptHoverCard"));
  CHECK(card != nullptr);
  CHECK(card->isVisible());
  CHECK(card->height() > 120);  // icon + name + author + description + footer
  save_widget_artifact("script_hover_card", *card);
  dialog.close();
}

void ui_script_include_bundled_root_and_is_main() {
  patchy::ui::MainWindow window;
  show_window(window);
  // No script path (editor-style run): the include resolves through the
  // bundled scripts root. The included file defines its function but must not
  // run its standalone branch (patchy.isMainScript() is false inside it), and
  // its own top-level OPTIONS block must not clobber the includer's OPTIONS
  // (the Breakout frozen-paddle bug: fancy-background's OPTIONS replaced
  // paddleSpeed/lives with undefined).
  CHECK(run_script(window, QStringLiteral(R"JS(
    var OPTIONS = { marker: 42 };
    console.log('main=' + patchy.isMainScript());
    include('Effects/fancy-background.js');
    console.log('have-fn=' + (typeof drawFancyBackground === 'function'));
    console.log('options-marker=' + OPTIONS.marker);
    var stray = app.activeDocument.findLayer('Fancy Background');
    console.log('ran-standalone=' + (stray !== undefined));
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("main=true")));
  CHECK(backlog_contains(window, QStringLiteral("have-fn=true")));
  CHECK(backlog_contains(window, QStringLiteral("options-marker=42")));
  CHECK(backlog_contains(window, QStringLiteral("ran-standalone=false")));
}

// The tone synth's WAV container: canonical 44-byte RIFF/WAVE header for
// 44100 Hz 16-bit mono PCM, sample count from the duration, amplitude scaling
// from the volume, and clamped extremes still produce a bounded valid file.
void ui_sound_build_tone_wav_shape() {
  const auto u16 = [](const QByteArray& wav, int offset) {
    return static_cast<int>(static_cast<unsigned char>(wav[offset])) |
           (static_cast<int>(static_cast<unsigned char>(wav[offset + 1])) << 8);
  };
  const auto u32 = [&u16](const QByteArray& wav, int offset) {
    return static_cast<quint32>(u16(wav, offset)) |
           (static_cast<quint32>(u16(wav, offset + 2)) << 16);
  };
  const auto peak = [](const QByteArray& wav) {
    int max = 0;
    for (int i = 44; i + 1 < wav.size(); i += 2) {
      const auto sample = static_cast<qint16>(
          static_cast<unsigned char>(wav[i]) |
          (static_cast<unsigned char>(wav[i + 1]) << 8));
      max = std::max(max, std::abs(static_cast<int>(sample)));
    }
    return max;
  };

  const auto wav = patchy::ui::build_tone_wav(440.0, 100, 0.5, patchy::ui::ToneWave::Sine);
  CHECK(wav.startsWith("RIFF"));
  CHECK(wav.mid(8, 4) == QByteArrayLiteral("WAVE"));
  CHECK(wav.mid(12, 4) == QByteArrayLiteral("fmt "));
  CHECK(u16(wav, 20) == 1);       // PCM
  CHECK(u16(wav, 22) == 1);       // mono
  CHECK(u32(wav, 24) == 44100u);  // sample rate
  CHECK(u16(wav, 34) == 16);      // bits per sample
  CHECK(wav.mid(36, 4) == QByteArrayLiteral("data"));
  CHECK(u32(wav, 40) == 44100u / 10 * 2);  // 100 ms of 16-bit samples
  CHECK(wav.size() == 44 + 44100 / 10 * 2);

  const auto loud = patchy::ui::build_tone_wav(440.0, 100, 1.0, patchy::ui::ToneWave::Sine);
  CHECK(peak(loud) > peak(wav) * 3 / 2);  // volume scales amplitude
  const auto silent = patchy::ui::build_tone_wav(440.0, 100, 0.0, patchy::ui::ToneWave::Sine);
  CHECK(peak(silent) == 0);
  const auto square = patchy::ui::build_tone_wav(440.0, 100, 0.5, patchy::ui::ToneWave::Square);
  CHECK(square.mid(44) != wav.mid(44));

  const auto clamped =
      patchy::ui::build_tone_wav(999999.0, 999999, 9.0, patchy::ui::ToneWave::Square);
  CHECK(clamped.size() == 44 + 44100 * 4 * 2);  // duration caps at 4 s
}

// patchy.ui.playTone/playSound through the script API: PATCHY_NO_SOUND keeps
// the suite silent while the full path (clamps, include-style resolution, WAV
// validation, error text naming the file) still runs.
void ui_script_play_tone_and_sound_offscreen() {
  const EnvironmentVariableRestorer restore_no_sound("PATCHY_NO_SOUND");
  qputenv("PATCHY_NO_SOUND", "1");
  patchy::ui::MainWindow window;
  show_window(window);
  QTemporaryDir temp;
  CHECK(temp.isValid());
  const auto wav_path = temp.filePath(QStringLiteral("blip.wav"));
  {
    QFile file(wav_path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(patchy::ui::build_tone_wav(880.0, 30, 0.4, patchy::ui::ToneWave::Square));
  }
  const auto not_wav_path = temp.filePath(QStringLiteral("not-a-wav.wav"));
  {
    QFile file(not_wav_path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write("plain text");
  }
  CHECK(run_script(window, QStringLiteral(R"JS(
    patchy.ui.playTone(880);
    patchy.ui.playTone(60, 40, 0.2, "square");
    patchy.ui.playTone(-500, 999999, 42);  // clamped, still fine
    patchy.ui.playSound('%1');
    var missing = '';
    try { patchy.ui.playSound('no-such-file.wav'); } catch (e) { missing = String(e); }
    var invalid = '';
    try { patchy.ui.playSound('%2'); } catch (e) { invalid = String(e); }
    console.log('missing-error=' + missing);
    console.log('invalid-error=' + invalid);
    console.log('sound-ok');
  )JS")
                             .arg(QString(wav_path).replace(QLatin1Char('\\'), QLatin1Char('/')))
                             .arg(QString(not_wav_path)
                                      .replace(QLatin1Char('\\'), QLatin1Char('/')))));
  CHECK(backlog_contains(window, QStringLiteral("sound-ok")));
  CHECK(backlog_contains(window, QStringLiteral("missing-error=Error: playSound")));
  CHECK(backlog_contains(window, QStringLiteral("no-such-file.wav")));
  CHECK(backlog_contains(window, QStringLiteral("not a .wav file")));
}

void ui_script_fancy_background_runs_standalone() {
  patchy::ui::MainWindow window;
  show_window(window);
  // Small document first so the pixel loop stays fast.
  CHECK(run_script(window, QStringLiteral("app.newDocument(96, 64);")));
  auto& host = window.script_engine_host();
  const auto path = QDir(patchy::ui::MainWindow::bundled_scripts_directory())
                        .absoluteFilePath(QStringLiteral("Effects/fancy-background.js"));
  // Unattended (the forwarded-CLI contract): showOptions answers with its
  // defaults instead of blocking on the options dialog.
  CHECK(host.run_file(path, {}, /*unattended=*/true));
  wait_for_run_end(host);
  CHECK(!host.last_run_had_error());
  CHECK(layer_named(patchy::ui::MainWindowTestAccess::document(window), "Fancy Background") !=
        nullptr);
}

void ui_script_dialog_pickers_listfiles_args_cli_defaults() {
  patchy::ui::MainWindow window;
  show_window(window);
  window.set_cli_automation_mode(true);
  QTemporaryDir temp;
  CHECK(temp.isValid());
  for (const auto* name : {"a.png", "b.png", "c.txt"}) {
    QFile file(temp.filePath(QString::fromLatin1(name)));
    CHECK(file.open(QIODevice::WriteOnly));
    file.write("x");
  }
  auto& host = window.script_engine_host();
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("cli-defaults");
  options.args = QStringList{QStringLiteral("name=World"), QStringLiteral("flag")};
  const auto source = QStringLiteral(R"JS(
    var r = patchy.ui.showDialog({title: 'T', fields: [
      {key: 'scale', type: 'number', value: 42, min: 0, max: 100},
      {key: 'on', type: 'checkbox', value: true},
      {key: 'mode', type: 'choice', value: 'jpg', choices: ['png', 'jpg']},
      {key: 'label', type: 'text', value: 'hi'}]});
    console.log('dlg=' + r.scale + ',' + r.on + ',' + r.mode + ',' + r.label);
    console.log('folder=[' + app.chooseFolder('pick') + ']');
    var files = patchy.io.listFiles('%1', '*.png');
    console.log('files=' + files.join(','));
    console.log('args=' + patchy.args.name + ',' + ('flag' in patchy.args));
  )JS")
                          .arg(QDir(temp.path()).absolutePath());
  (void)host.run_source(source, std::move(options));
  wait_for_run_end(host);
  CHECK(!host.last_run_had_error());
  CHECK(backlog_contains(window, QStringLiteral("dlg=42,true,jpg,hi")));
  CHECK(backlog_contains(window, QStringLiteral("folder=[]")));
  CHECK(backlog_contains(window, QStringLiteral("files=a.png,b.png")));
  CHECK(backlog_contains(window, QStringLiteral("args=World,true")));
}

void ui_script_run_command_triggers_actions() {
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    console.log('ids=' + (app.commandIds().indexOf('select.all') >= 0));
    console.log('unknown=' + app.runCommand('no.such.command'));
    console.log('ran=' + app.runCommand('select.all'));
    console.log('sel=' + doc.selection.exists);
    app.runCommand('select.deselect');
    console.log('sel-after=' + doc.selection.exists);
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("ids=true")));
  CHECK(backlog_contains(window, QStringLiteral("unknown=false")));
  CHECK(backlog_contains(window, QStringLiteral("ran=true")));
  CHECK(backlog_contains(window, QStringLiteral("sel=true")));
  CHECK(backlog_contains(window, QStringLiteral("sel-after=false")));
}

// The @cli header directive and the command builder behind the Script
// Manager's C:\ button: tokens append verbatim (repeated lines join), the
// fallback covers scripts with no metadata at all, and paths stay quoted.
void ui_script_cli_directive_and_example_command() {
  QTemporaryDir temp;
  CHECK(temp.isValid());
  const QDir root(temp.path());
  auto write_file = [](const QString& path, const QByteArray& content) {
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(content);
  };
  const auto args_path = root.absoluteFilePath(QStringLiteral("args.js"));
  write_file(args_path,
             "// @name Args Demo\n"
             "// @cli --script-arg folder=C:\\photos\n"
             "// @cli --script-arg out=C:\\photos\\web example.png\n"
             "var x = 1;\n");
  const auto meta = patchy::ui::read_script_metadata(args_path);
  CHECK(meta.cli_example ==
        QStringLiteral(
            "--script-arg folder=C:\\photos --script-arg out=C:\\photos\\web example.png"));

  const auto command = patchy::ui::script_cli_example_command(
      QStringLiteral("C:/Program Files/Patchy/patchy.exe"), args_path, meta);
  CHECK(command.startsWith(QLatin1Char('"')));
  CHECK(command.contains(QStringLiteral("patchy.exe\" --run-script \"")));
  CHECK(command.contains(QStringLiteral("--script-arg folder=C:\\photos")));
  CHECK(command.endsWith(QStringLiteral("example.png")));

  // A spaced exe path forces quotes, and the shells then genuinely differ:
  // the PowerShell flavor puts the call operator in front.
  const auto powershell = patchy::ui::script_cli_example_command(
      QStringLiteral("C:/Program Files/Patchy/patchy.exe"), args_path, meta,
      patchy::ui::CliShell::PowerShell);
  CHECK(powershell == QStringLiteral("& ") + command);

  // A plain exe path stays unquoted - the one form Command Prompt,
  // PowerShell, and batch files all run as pasted - so both flavors match.
  const auto universal = patchy::ui::script_cli_example_command(
      QStringLiteral("D:/tools/patchy.exe"), args_path, meta);
  CHECK(!universal.startsWith(QLatin1Char('"')));
  CHECK(universal.contains(QStringLiteral("patchy.exe --run-script \"")));
  CHECK(universal ==
        patchy::ui::script_cli_example_command(QStringLiteral("D:/tools/patchy.exe"), args_path,
                                               meta, patchy::ui::CliShell::PowerShell));

  // No @cli: active-document scripts get the example.png placeholder, @window
  // scripts the bare command, and empty metadata never throws.
  const patchy::ui::ScriptMetadata plain;
  const auto fallback = patchy::ui::script_cli_example_command(
      QStringLiteral("patchy.exe"), root.absoluteFilePath(QStringLiteral("plain.js")), plain);
  CHECK(fallback.endsWith(QStringLiteral(" example.png")));
  patchy::ui::ScriptMetadata windowed;
  windowed.opens_window = true;
  const auto bare = patchy::ui::script_cli_example_command(
      QStringLiteral("patchy.exe"), root.absoluteFilePath(QStringLiteral("game.js")), windowed);
  CHECK(bare.endsWith(QStringLiteral(".js\"")));

  // The scan carries the directive to the tree/menu consumers.
  const auto entries = patchy::ui::scan_script_folder(root.absolutePath());
  CHECK(entries.size() == 1);
  CHECK(entries[0].cli_example == meta.cli_example);
}

// The C:\ toolbar button: disabled until a script is selected, then pops the
// example dialog whose command comes from the script's @cli directive, and
// Copy puts the exact command on the clipboard.
void ui_script_manager_cli_example_dialog() {
  const StandardPathsTestMode test_paths;
  remove_test_scratch_dir(patchy::ui::MainWindow::user_scripts_directory());
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::ScriptEditorDialog dialog(window, window.script_engine_host());
  dialog.show();
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  auto* tree = dialog.findChild<QTreeWidget*>(QStringLiteral("scriptEditorTree"));
  auto* cli_button = dialog.findChild<QPushButton*>(QStringLiteral("scriptEditorCliButton"));
  CHECK(tree != nullptr && cli_button != nullptr);
  CHECK(!cli_button->isEnabled());

  QTreeWidgetItem* bundled_root = nullptr;
  for (int i = 0; i < tree->topLevelItemCount(); ++i) {
    if (tree->topLevelItem(i)->text(0) == QStringLiteral("Bundled")) {
      bundled_root = tree->topLevelItem(i);
    }
  }
  CHECK(bundled_root != nullptr);
  auto* utilities = find_child_item(bundled_root, QStringLiteral("Utilities"));
  CHECK(utilities != nullptr);
  auto* batch_export = find_child_item(utilities, QStringLiteral("Batch Export"));
  CHECK(batch_export != nullptr);
  tree->setCurrentItem(batch_export);
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  CHECK(cli_button->isEnabled());

  cli_button->click();
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  auto* example = dialog.findChild<QDialog*>(QStringLiteral("scriptEditorCliDialog"));
  CHECK(example != nullptr);
  CHECK(example->isVisible());
  auto* command_box =
      example->findChild<QPlainTextEdit*>(QStringLiteral("scriptEditorCliCommand"));
  CHECK(command_box != nullptr);
  const auto command = command_box->toPlainText();
  CHECK(command.contains(QStringLiteral("--run-script")));
  CHECK(command.contains(QStringLiteral("batch-export.js")));
  CHECK(command.contains(QStringLiteral("--script-output result.txt")));
  CHECK(command.contains(QStringLiteral("--script-arg folder=C:\\photos")));
  // The test binary's path has no spaces, so one universal line serves every
  // shell: unquoted exe token, no separate PowerShell box.
  CHECK(!command.startsWith(QLatin1Char('"')));
  CHECK(example->findChild<QPlainTextEdit*>(
            QStringLiteral("scriptEditorCliCommandPowerShell")) == nullptr);
  save_widget_artifact("script_cli_example_dialog", *example);
  auto* copy_button =
      example->findChild<QPushButton*>(QStringLiteral("scriptEditorCliCopyButton"));
  CHECK(copy_button != nullptr);
  copy_button->click();
  CHECK(QGuiApplication::clipboard()->text() == command);
  example->close();
}

// Help opens the bundled scripting guide in the markdown viewer (the same
// file README links), and the Help > Scripting Guide menu action reuses the
// one instance. The first open parks in run_non_modal_dialog's nested loop,
// so a timer inspects and closes it from inside.
void ui_script_scripting_guide_opens_from_help() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto guide_path = patchy::ui::MainWindow::bundled_scripts_directory() +
                          QStringLiteral("/scripting-guide.md");
  CHECK(QFile::exists(guide_path));

  patchy::ui::ScriptEditorDialog dialog(window, window.script_engine_host());
  dialog.show();
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  auto* help_button = dialog.findChild<QPushButton*>(QStringLiteral("scriptEditorHelpButton"));
  CHECK(help_button != nullptr);
  bool saw_guide = false;
  QString guide_text;
  auto* dismisser = new QTimer(&window);
  QObject::connect(dismisser, &QTimer::timeout, &window, [&] {
    auto* viewer = window.findChild<QDialog*>(QStringLiteral("markdownViewerDialog"));
    if (viewer == nullptr || !viewer->isVisible()) {
      return;
    }
    if (auto* browser =
            viewer->findChild<QTextBrowser*>(QStringLiteral("markdownViewerBrowser"))) {
      guide_text = browser->document()->toPlainText();
    }
    if (!saw_guide) {
      save_widget_artifact("scripting_guide_viewer", *viewer);
    }
    saw_guide = true;
    // The guide closes like the other Help dialogs: a Close button at the
    // bottom and the chrome's X in the title bar (GitHub issue 107).
    CHECK(viewer->findChild<QToolButton*>(QStringLiteral("dialogChromeCloseButton")) != nullptr);
    auto* close_button = viewer->findChild<QPushButton*>(QStringLiteral("markdownViewerCloseButton"));
    CHECK(close_button != nullptr);
    if (close_button != nullptr) {
      close_button->click();
    } else {
      viewer->close();
    }
  });
  dismisser->start(30);
  help_button->click();
  dismisser->stop();
  CHECK(saw_guide);
  CHECK(guide_text.contains(QStringLiteral("Patchy Scripting Guide")));
  CHECK(guide_text.contains(QStringLiteral("--run-script")));
  CHECK(guide_text.contains(QStringLiteral("--headless")));
  CHECK(guide_text.contains(QStringLiteral("@cli")));

  // Reopening (here via the Help menu action) reuses the single hidden
  // instance and returns immediately - no second nested loop.
  auto* action = window.findChild<QAction*>(QStringLiteral("helpScriptingGuideAction"));
  CHECK(action != nullptr);
  action->trigger();
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  const auto viewers = window.findChildren<QDialog*>(QStringLiteral("markdownViewerDialog"));
  CHECK(viewers.size() == 1);
  CHECK(viewers[0]->isVisible());
  viewers[0]->close();
}

// Help > Set up AI Control opens the paste-into-your-assistant dialog. The
// release layout has the connector, the assembled skill, and its setup.md next
// to the test binary, so the text must carry all three real paths and no NOT
// FOUND marker, and Copy must put exactly that text on the clipboard.
void ui_ai_setup_dialog_opens_from_help() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto paths = patchy::ui::resolve_ai_control_paths();
  CHECK(!paths.connector_path.isEmpty());
  CHECK(!paths.skill_directory.isEmpty());
  CHECK(!paths.setup_document_path.isEmpty());
  CHECK(QFileInfo::exists(paths.connector_path));
  CHECK(QFileInfo::exists(paths.skill_directory + QStringLiteral("/SKILL.md")));

  auto* action = patchy::test::ui::require_action(window, "helpAiSetupAction");
  CHECK(action->isVisible());
  bool saw_dialog = false;
  QString blurb;
  QString status;
  QGuiApplication::clipboard()->clear();
  auto* dismisser = new QTimer(&window);
  QObject::connect(dismisser, &QTimer::timeout, &window, [&] {
    auto* dialog = window.findChild<QDialog*>(QStringLiteral("aiSetupDialog"));
    if (dialog == nullptr || !dialog->isVisible()) {
      return;
    }
    if (auto* text = dialog->findChild<QPlainTextEdit*>(QStringLiteral("aiSetupBlurbText"))) {
      blurb = text->toPlainText();
      CHECK(text->isReadOnly());
    }
    if (auto* label = dialog->findChild<QLabel*>(QStringLiteral("aiSetupStatusLabel"))) {
      status = label->text();
    }
    auto* copy = dialog->findChild<QPushButton*>(QStringLiteral("aiSetupCopyButton"));
    CHECK(copy != nullptr);
    if (copy != nullptr) {
      copy->click();
    }
    auto* open_skill =
        dialog->findChild<QPushButton*>(QStringLiteral("aiSetupOpenSkillFolderButton"));
    CHECK(open_skill != nullptr && open_skill->isEnabled());
    if (!saw_dialog) {
      save_widget_artifact("ai_setup_dialog", *dialog);
    }
    saw_dialog = true;
    dialog->close();
  });
  dismisser->start(30);
  action->trigger();
  dismisser->stop();
  CHECK(saw_dialog);

  CHECK(blurb.contains(QDir::toNativeSeparators(paths.connector_path)));
  CHECK(blurb.contains(QDir::toNativeSeparators(paths.skill_directory)));
  CHECK(blurb.contains(QDir::toNativeSeparators(paths.setup_document_path)));
  CHECK(blurb.contains(QString::fromLatin1(patchy::ui::kAiControlSetupUrl)));
  CHECK(blurb.contains(QStringLiteral("\"get_info\"")));
  CHECK(blurb.contains(QStringLiteral("named \"patchy\"")));
  CHECK(!blurb.contains(QStringLiteral("NOT FOUND (expected")));
  CHECK(!blurb.contains(QChar(0x2014)));
  CHECK(QGuiApplication::clipboard()->text() == blurb);
  CHECK(status.contains(QDir::toNativeSeparators(paths.connector_path).toHtmlEscaped()));
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("copied")));

  // Reopening reuses the one hidden instance; no second nested loop.
  action->trigger();
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  const auto dialogs = window.findChildren<QDialog*>(QStringLiteral("aiSetupDialog"));
  CHECK(dialogs.size() == 1);
  CHECK(dialogs[0]->isVisible());
  dialogs[0]->close();
}

// The blurb builder handles a build without the kit (source tree, old zip) and
// the Flatpak sandbox, where the connector is reached through `flatpak run`.
void ui_ai_setup_blurb_reports_missing_and_flatpak_forms() {
  const auto url = QString::fromLatin1(patchy::ui::kAiControlSetupUrl);

  const auto missing = patchy::ui::ai_setup_blurb_text(patchy::ui::AiControlPaths{});
  CHECK(missing.contains(QStringLiteral("Command: NOT FOUND (expected")));
  CHECK(missing.contains(QStringLiteral("   NOT FOUND (expected")));
  CHECK(missing.contains(QStringLiteral("not installed; use the online copy")));
  CHECK(missing.contains(url));
  CHECK(!missing.contains(QChar(0x2014)));

  patchy::ui::AiControlPaths flatpak;
  flatpak.flatpak = true;
  const auto sandboxed = patchy::ui::ai_setup_blurb_text(flatpak);
  CHECK(sandboxed.contains(QStringLiteral("flatpak run --command=patchy-mcp com.rtsoft.patchy")));
  CHECK(sandboxed.contains(QStringLiteral("/app/share/patchy/ai/patchy-control")));
  CHECK(sandboxed.contains(QStringLiteral("flatpak run --command=cp com.rtsoft.patchy /app/share/patchy/ai/patchy-control/SKILL.md")));
  CHECK(sandboxed.contains(url));
  CHECK(!sandboxed.contains(QStringLiteral("NOT FOUND (expected")));
  CHECK(!sandboxed.contains(QChar(0x2014)));

  // An AppImage: the connector is the .AppImage file with the patchy-mcp argument (its
  // AppRun dispatches), never the per-launch /tmp/.mount_* binary, and the skill folder in
  // that mount is flagged as temporary.
  patchy::ui::AiControlPaths appimage;
  appimage.appimage_path = QStringLiteral("/home/someone/Apps/PatchyLinux.AppImage");
  appimage.connector_path = QStringLiteral("/tmp/.mount_PatchyAbc/usr/bin/patchy-mcp");
  appimage.skill_directory = QStringLiteral("/tmp/.mount_PatchyAbc/usr/share/patchy/ai/patchy-control");
  appimage.setup_document_path = appimage.skill_directory + QStringLiteral("/references/setup.md");
  const auto appimage_blurb = patchy::ui::ai_setup_blurb_text(appimage);
  CHECK(appimage_blurb.contains(
      QStringLiteral("Command: %1 patchy-mcp").arg(QDir::toNativeSeparators(QStringLiteral("\"/home/someone/Apps/PatchyLinux.AppImage\"")))));
  CHECK(!appimage_blurb.contains(QStringLiteral("usr/bin/patchy-mcp")));
  CHECK(appimage_blurb.contains(QStringLiteral("disappears when Patchy closes")));
  CHECK(appimage_blurb.contains(QStringLiteral("Local (only while Patchy is open)")));
  CHECK(!appimage_blurb.contains(QStringLiteral("NOT FOUND (expected")));
  CHECK(!appimage_blurb.contains(QChar(0x2014)));

  // A resolved layout quotes native paths so folders with spaces survive a paste.
  patchy::ui::AiControlPaths resolved;
  resolved.connector_path = QStringLiteral("/opt/My Apps/patchy-mcp");
  resolved.skill_directory = QStringLiteral("/opt/My Apps/ai/patchy-control");
  resolved.setup_document_path = resolved.skill_directory + QStringLiteral("/references/setup.md");
  const auto text = patchy::ui::ai_setup_blurb_text(resolved);
  CHECK(text.contains(QLatin1Char('"') + QDir::toNativeSeparators(resolved.connector_path) +
                      QLatin1Char('"')));
  CHECK(text.contains(QLatin1Char('"') + QDir::toNativeSeparators(resolved.skill_directory) +
                      QLatin1Char('"')));
  CHECK(!text.contains(QStringLiteral("NOT FOUND (expected")));

  CHECK(text.contains(QStringLiteral("use no connector arguments")));
  CHECK(text.contains(QStringLiteral("Reuse an existing matching Patchy connection")));
  CHECK(text.contains(QStringLiteral("without reinstalling")));
}

void ui_ai_setup_examples_keep_installation_prompt_stable() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto resolved = patchy::ui::resolve_ai_control_paths();
  const auto text = patchy::ui::ai_setup_blurb_text(resolved);
  patchy::ui::AiSetupDialog dialog(resolved, &window);
  dialog.show();
  QApplication::processEvents();
  CHECK(!dialog.findChild<QComboBox*>(QStringLiteral("aiSetupModeComboBox")));
  auto* examples = dialog.findChild<QComboBox*>(QStringLiteral("aiSetupExamplesComboBox"));
  auto* example_text = dialog.findChild<QPlainTextEdit*>(QStringLiteral("aiSetupExampleText"));
  auto* example_copy = dialog.findChild<QPushButton*>(QStringLiteral("aiSetupExampleCopyButton"));
  auto* setup_text = dialog.findChild<QPlainTextEdit*>(QStringLiteral("aiSetupBlurbText"));
  auto* copy = dialog.findChild<QPushButton*>(QStringLiteral("aiSetupCopyButton"));
  CHECK(examples && example_text && example_copy && setup_text && copy);
  CHECK(examples->count() >= 5);
  CHECK(example_text->isReadOnly());
  CHECK(QFontMetrics(setup_text->font()).height() > QFontMetrics(window.font()).height());
  CHECK(example_text->font() == setup_text->font());
  QStringList prompts;
  for (int i = 0; i < examples->count(); ++i) {
    examples->setCurrentIndex(i);
    const auto prompt = example_text->toPlainText();
    CHECK(!prompt.isEmpty() && !prompts.contains(prompt));
    prompts.append(prompt);
    example_copy->click();
    CHECK(QGuiApplication::clipboard()->text() == prompt);
    CHECK(dialog.blurb_text() == text);
    copy->click();
    CHECK(QGuiApplication::clipboard()->text() == text);
  }
  const auto all = prompts.join(QLatin1Char('\n'));
  CHECK(all.contains(QStringLiteral("document I have open")));
  CHECK(all.contains(QStringLiteral("visible Patchy window")));
  CHECK(all.contains(QStringLiteral("background, without opening a window")));
  examples->setCurrentIndex(2);
  QTest::qWait(1250);
  CHECK(copy->text() == QStringLiteral("Copy Setup Prompt"));
  CHECK(example_copy->text() == QStringLiteral("Copy Example Prompt"));
  save_widget_artifact("ai_setup_examples", dialog);
  auto& theme = patchy::ui::ThemeManager::instance();
  auto& language = patchy::ui::LocalizationManager::instance();
  const auto old_theme = theme.preference();
  const auto old_language = language.current_language();
  const auto restore = qScopeGuard([&] {
    theme.set_preference(old_theme, false);
    (void)language.set_language(old_language, false);
  });
  theme.set_preference(patchy::ui::ColorSchemePreference::Light, false);
  QApplication::processEvents();
  save_widget_artifact("ai_setup_examples_light", dialog);
  dialog.close();
  CHECK(language.set_language(QStringLiteral("ja"), false));
  patchy::ui::AiSetupDialog japanese(resolved, &window);
  japanese.show();
  QApplication::processEvents();
  auto* japanese_copy = japanese.findChild<QPushButton*>(QStringLiteral("aiSetupExampleCopyButton"));
  CHECK(japanese_copy && japanese_copy->text() != QStringLiteral("Copy Example Prompt"));
  save_widget_artifact("ai_setup_examples_ja", japanese);
  japanese.close();
  CHECK(dialog.blurb_text() == text);
}

// patchy.ui.zoom / fitOnScreen: the documented view controls (percent, active
// document, status-bar clamping), including the connector sessions that refuse
// app.runCommand('view.fit_on_screen').
void ui_script_ui_view_zoom() {
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral(R"JS(
    patchy.ui.setWindowSize(1000, 700);
    patchy.ui.zoom = 50;
    console.log('half=' + patchy.ui.zoom);
    patchy.ui.zoom = 100000;
    console.log('max=' + patchy.ui.zoom);
    patchy.ui.zoom = 0.001;
    console.log('min=' + patchy.ui.zoom);
    patchy.ui.fitOnScreen();
    var fit = patchy.ui.zoom;
    var doc = app.activeDocument;
    console.log('fits=' + (fit > 0 && doc.width * fit / 100 <= 1000 && doc.height * fit / 100 <= 700));
    patchy.ui.zoom = 200;
    console.log('after=' + patchy.ui.zoom);
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("half=50")));
  CHECK(backlog_contains(window, QStringLiteral("max=12800")));
  CHECK(backlog_contains(window, QStringLiteral("min=0.01")));
  CHECK(backlog_contains(window, QStringLiteral("fits=true")));
  CHECK(backlog_contains(window, QStringLiteral("after=200")));
  CHECK(!run_script(window, QStringLiteral("patchy.ui.zoom = NaN;")));
  CHECK(!run_script(window, QStringLiteral("patchy.ui.zoom = -5;")));
  CHECK(!run_script(window, QStringLiteral("patchy.ui.zoom = 0;")));
  // A failed assignment leaves the view alone.
  CHECK(run_script(window, QStringLiteral("console.log('kept=' + patchy.ui.zoom);")));
  CHECK(backlog_contains(window, QStringLiteral("kept=200")));

  patchy::ui::MainWindow connector;
  show_window(connector);
  connector.set_cli_automation_mode(true);
  connector.script_engine_host().set_connector_mode(true);
  CHECK(run_script(connector, QStringLiteral(R"JS(
    app.documents.forEach(function (d) { d.close(); });
    console.log('empty=' + patchy.ui.zoom);
  )JS")));
  CHECK(backlog_contains(connector, QStringLiteral("empty=0")));
  CHECK(!run_script(connector, QStringLiteral("patchy.ui.zoom = 100;")));
  CHECK(!run_script(connector, QStringLiteral("patchy.ui.fitOnScreen();")));
  CHECK(run_script(connector, QStringLiteral(R"JS(
    var doc = app.newDocument(64, 64);
    patchy.ui.zoom = 200;
    console.log('connector=' + patchy.ui.zoom);
    patchy.ui.fitOnScreen();
    console.log('connectorFit=' + (patchy.ui.zoom > 200));
  )JS")));
  CHECK(backlog_contains(connector, QStringLiteral("connector=200")));
  CHECK(backlog_contains(connector, QStringLiteral("connectorFit=true")));
  CHECK(!run_script(connector, QStringLiteral("app.runCommand('view.fit_on_screen');")));
}

void ui_script_ui_staging_apis() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto capture_path = QStringLiteral("test-artifacts/ui_script_capture_window.png");
  QFile::remove(capture_path);
  CHECK(run_script(window, QStringLiteral(R"JS(
    patchy.ui.setWindowSize(1200, 800);
    patchy.ui.setSidePanelWidth(420);
    patchy.ui.setStatusMessage('Staged by script');
    if (!patchy.ui.captureWindow('test-artifacts/ui_script_capture_window.png')) {
      throw new Error('captureWindow returned false');
    }
  )JS")));
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Staged by script"));
  CHECK(window.width() == 1200);
  CHECK(window.height() == 800);
  auto* panel_column = patchy::ui::MainWindowTestAccess::panel_column(window);
  CHECK(panel_column != nullptr);
  // 420 stays above the measured column minimum, which tracks the localized
  // blend/opacity row and would clamp a smaller request.
  CHECK(panel_column->width() == 420);
  const QImage captured(capture_path);
  CHECK(!captured.isNull());
  CHECK(captured.width() == 1200);
  CHECK(captured.height() == 800);
  // An empty path throws instead of writing nowhere.
  CHECK(!run_script(window, QStringLiteral("patchy.ui.captureWindow('');")));
}

void ui_script_ui_side_panels_expand_collapse_close() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* column = patchy::ui::MainWindowTestAccess::panel_column(window);
  CHECK(column != nullptr);
  const auto properties = QStringLiteral("propertiesDock");
  auto* section = column->section_widget(properties);
  CHECK(section != nullptr);
  // Every panel but the Layers group starts collapsed: a title row only.
  CHECK(!column->panel_expanded(properties));
  const auto collapsed_height = section->height();

  CHECK(run_script(window, QStringLiteral(R"JS(
    function state(name) {
      var s = patchy.ui.panelState(name);
      return s.open + ',' + s.front + ',' + s.expanded;
    }
    console.log('before=' + state('properties'));
    patchy.ui.expandPanel('properties');
    console.log('after=' + state('properties'));
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("before=true,true,false")));
  CHECK(backlog_contains(window, QStringLiteral("after=true,true,true")));
  CHECK(column->panel_expanded(properties));
  QApplication::processEvents();
  auto* page = column->panel_page(properties);
  CHECK(page != nullptr && page->isVisible());
  CHECK(section->height() > collapsed_height + 40);

  // A background tab comes to the front of its group; a closed panel reopens.
  CHECK(run_script(window, QStringLiteral(R"JS(
    patchy.ui.expandPanel('channels');
    patchy.ui.closePanel('history');
    console.log('channels=' + patchy.ui.panelState('channels').front +
                ' layers=' + patchy.ui.panelState('layers').front +
                ' history=' + patchy.ui.panelState('history').open);
    patchy.ui.expandPanel('history');
    patchy.ui.collapsePanel('properties');
    var h = patchy.ui.panelState('history');
    console.log('reopened=' + h.open + ',' + h.expanded +
                ' collapsed=' + patchy.ui.panelState('properties').expanded);
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("channels=true layers=false history=false")));
  CHECK(backlog_contains(window, QStringLiteral("reopened=true,true collapsed=false")));
  CHECK(column->panel_in_front(QStringLiteral("channelsDock")));
  CHECK(column->panel_visible(QStringLiteral("historyDock")));
  CHECK(column->panel_expanded(QStringLiteral("historyDock")));
  CHECK(!column->panel_expanded(properties));

  // Names are exact permanent identifiers: unknown or differently cased ones throw.
  CHECK(!run_script(window, QStringLiteral("patchy.ui.expandPanel('Properties');")));
  CHECK(backlog_contains(window, QStringLiteral("Unknown panel \"Properties\"")));
  CHECK(!run_script(window, QStringLiteral("patchy.ui.panelState('character');")));
  CHECK(!run_script(window, QStringLiteral("patchy.ui.closePanel('');")));
  CHECK(!run_script(window, QStringLiteral("patchy.ui.collapsePanel('propertiesDock');")));
  CHECK(!column->panel_expanded(properties));
  CHECK(column->panel_visible(properties));
}

// The README/showcase flow: a --run-script file run unattended (the --headless CLI
// path) expands Properties and captures the window with it open.
void ui_script_ui_expand_panel_in_cli_capture() {
  patchy::ui::MainWindow window;
  show_window(window);
  window.set_cli_automation_mode(true);
  QTemporaryDir temp;
  CHECK(temp.isValid());
  const auto capture_path = QStringLiteral("test-artifacts/ui_script_expand_panel_capture.png");
  QFile::remove(capture_path);
  const auto write_script = [&](const QString& name, const QByteArray& source) {
    const auto path = temp.filePath(name);
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(source);
    return path;
  };
  const auto read_output = [&](const QString& script_path) {
    const auto output_path = script_path + QStringLiteral(".out.txt");
    window.run_script_command(script_path, output_path);
    wait_for_run_end(window.script_engine_host());
    QElapsedTimer timer;
    timer.start();
    while (!QFile::exists(output_path) && timer.elapsed() < 5000) {
      QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
    }
    QFile output(output_path);
    CHECK(output.open(QIODevice::ReadOnly | QIODevice::Text));
    return QString::fromUtf8(output.readAll());
  };

  const auto shot = read_output(write_script(QStringLiteral("expand-properties.js"), R"JS(
    var doc = app.newDocument(640, 480);
    doc.addArtboard('Phone', {x: 40, y: 40, width: 240, height: 400});
    patchy.ui.setWindowSize(1280, 800);
    patchy.ui.expandPanel('properties');
    var s = patchy.ui.panelState('properties');
    console.log('expanded=' + s.expanded);
    if (!patchy.ui.captureWindow('test-artifacts/ui_script_expand_panel_capture.png')) {
      throw new Error('captureWindow returned false');
    }
  )JS"));
  CHECK(shot.contains(QStringLiteral("expanded=true")));
  CHECK(shot.endsWith(QStringLiteral("[done]\n")));
  auto* column = patchy::ui::MainWindowTestAccess::panel_column(window);
  CHECK(column != nullptr && column->panel_expanded(QStringLiteral("propertiesDock")));
  const QImage captured(capture_path);
  CHECK(!captured.isNull());
  CHECK(captured.width() == 1280);

  const auto failed = read_output(write_script(QStringLiteral("unknown-panel.js"), "patchy.ui.expandPanel('swatches');\n"));
  CHECK(failed.contains(QStringLiteral("[error]")));
  CHECK(failed.contains(QStringLiteral("Unknown panel \"swatches\"")));
  CHECK(failed.endsWith(QStringLiteral("[failed]\n")));
}

void ui_script_active_layer_setter_reveals_row() {
  patchy::Document document(48, 36, patchy::PixelFormat::rgba8());
  document.add_layer(patchy::Layer(document.allocate_layer_id(), "Base", patchy::PixelBuffer{}));
  patchy::Layer folder(document.allocate_layer_id(), "Folder", patchy::LayerKind::Group);
  folder.add_child(patchy::Layer(document.allocate_layer_id(), "Nested", patchy::PixelBuffer{}));
  document.add_layer(std::move(folder));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Reveal"));
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  // Collapse the folder through its disclosure button (the user path).
  auto* folder_item = patchy::test::ui::require_layer_item(*layer_list, QStringLiteral("Folder"));
  auto* folder_widget = layer_list->itemWidget(folder_item);
  CHECK(folder_widget != nullptr);
  auto* disclosure = folder_widget->findChild<QToolButton*>(QStringLiteral("layerFolderDisclosureButton"));
  CHECK(disclosure != nullptr);
  CHECK(disclosure->isChecked());
  disclosure->click();
  QApplication::processEvents();
  QApplication::processEvents();
  CHECK(patchy::test::ui::find_layer_item(*layer_list, QStringLiteral("Nested")) == nullptr);

  // The activeLayer setter reveals the row like a user's click would: the
  // collapsed ancestor expands and the row becomes the current selection.
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    doc.activeLayer = doc.findLayer('Nested');
  )JS")));
  auto* nested_item = patchy::test::ui::find_layer_item(*layer_list, QStringLiteral("Nested"));
  CHECK(nested_item != nullptr);
  CHECK(layer_list->currentItem() == nested_item);
  const auto& active_document = patchy::ui::MainWindowTestAccess::document(window);
  const auto* nested_layer = std::as_const(active_document).layers().back().children().empty()
                                 ? nullptr
                                 : &std::as_const(active_document).layers().back().children().front();
  CHECK(nested_layer != nullptr);
  CHECK(active_document.active_layer_id() == nested_layer->id());
}

void ui_script_webp_animation_export_preserves_document_and_validates_options() {
  patchy::ui::MainWindow window;
  show_window(window);
  window.set_cli_automation_mode(true);
  QTemporaryDir directory;
  CHECK(directory.isValid());
  const auto stem = patchy::test::kUnicodePathStems[0];
  const auto base = directory.path() + QLatin1Char('/') +
      QString::fromUtf8(reinterpret_cast<const char*>(stem.data()), static_cast<qsizetype>(stem.size()));
  auto& host = window.script_engine_host();
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("webp-export");
  options.args = QStringList{QStringLiteral("base=") + base};
  const auto source = QStringLiteral(R"JS(
    var d = app.newDocument(16, 12);
    d.activeLayer.name = 'Bottom 0.067s'; d.activeLayer.fill('#ff0000');
    var top = d.addLayer('Top pixel'); top.fill('#00ff00');
    d.groupLayers([top], 'Top 0.033s');
    var hidden = d.addLayer('Hidden'); hidden.fill('#0000ff'); hidden.visible = false;
    var oldPath = d.path, oldModified = d.modified;
    var p = patchy.args.base;
    if (!d.exportAnimatedWebp(p+'.webp', {lossless:true,loopCount:3})) throw Error('export');
    if (d.path !== oldPath || d.modified !== oldModified) throw Error('source changed');
    var size = patchy.io.fileSize(p+'.webp');
    var invalid = [{quality:-1},{quality:101},{quality:1.2},{frameDelayMs:16777216},
      {frameDelayMs:NaN},{loopCount:65536},{loopCount:-1},{lossless:1},{typo:1},null,[]];
    invalid.forEach(function(o) {
      var threw=false;
      try { d.exportAnimatedWebp(p+'.webp',o); } catch(e) { threw=true; }
      if(!threw) throw Error('accepted invalid options');
    });
    if (patchy.io.fileSize(p+'.webp') !== size) throw Error('failed export changed destination');
    var threw=false;
    try { d.exportAnimatedWebp(p+'.png'); } catch(e) { threw=true; }
    if(!threw) throw Error('accepted wrong extension');
    if (!d.exportAnimatedWebp(p+'-inherited.webp', {quality:100})) throw Error('inherited export');
    if (!d.exportAs(p+'-flat.webp')) throw Error('flat export');
  )JS");
  (void)host.run_source(source, std::move(options));
  wait_for_run_end(host);
  CHECK(!host.run_active());
  if (host.last_run_had_error()) for (const auto& line : host.message_backlog()) std::cerr << line.toStdString() << '\n';
  CHECK(!host.last_run_had_error());
  for (const auto& suffix : {QStringLiteral(".webp"), QStringLiteral("-inherited.webp")}) {
    QImageReader reader(base + suffix);
    CHECK(reader.imageCount() == 2);
    CHECK(reader.loopCount() == 2);
    CHECK(reader.read().pixelColor(0, 0) == QColor(Qt::green)); CHECK(reader.nextImageDelay() == 33);
    CHECK(reader.read().pixelColor(0, 0) == QColor(Qt::red)); CHECK(reader.nextImageDelay() == 67);
  }
  QImageReader still(base + QStringLiteral("-flat.webp"));
  CHECK(still.imageCount() == 1);
}

void ui_script_io_round_trips_unicode_path() {
  // The patchy.io probes plus saveAs/open on a Unicode, special-character path. The
  // directory comes in through --script-arg style args; the file name is built in the
  // script with JS escapes so the test is independent of the arg parser.
  patchy::ui::MainWindow window;
  show_window(window);
  window.set_cli_automation_mode(true);
  patchy::test::ui::ensure_artifact_dir();
  const auto dir = QFileInfo(QStringLiteral("test-artifacts")).absoluteFilePath() + QLatin1Char('/') +
                   QString::fromUtf8(reinterpret_cast<const char*>(patchy::test::kUnicodeDirName.data()),
                                     static_cast<qsizetype>(patchy::test::kUnicodeDirName.size())) +
                   QStringLiteral("/script-io");
  remove_test_scratch_dir(dir);
  CHECK(!QDir(dir).exists());

  auto& host = window.script_engine_host();
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("unicode-io");
  options.args = QStringList{QStringLiteral("dir=") + dir};
  const auto source = QStringLiteral(R"JS(
    var dir = patchy.args.dir;
    var name = 'せす café 🎨 #1 50% %20 &and \'q\' [x] ! ; = @ (v2).psd';
    var sub = dir + '/サブ sub';
    console.log('mkdir=' + patchy.io.makeDir(sub));
    var p = sub + '/' + name;
    console.log('before=' + patchy.io.fileExists(p) + ',' + patchy.io.fileSize(p) + ',' + patchy.io.deleteFile(p));
    console.log('saved=' + app.activeDocument.saveAs(p));
    console.log('after=' + patchy.io.fileExists(p) + ',' + (patchy.io.fileSize(p) > 0));
    console.log('listed=' + patchy.io.listFiles(sub, '*.psd').join('|'));
    var reopened = app.open(p);
    console.log('path_ok=' + (reopened.path.slice(-name.length) === name));
    console.log('deleted=' + patchy.io.deleteFile(p) + ',' + patchy.io.fileExists(p) + ',' + patchy.io.fileSize(p));
    console.log('dir_delete=' + patchy.io.deleteFile(sub));
  )JS");
  (void)host.run_source(source, std::move(options));
  wait_for_run_end(host);
  CHECK(!host.last_run_had_error());
  CHECK(backlog_contains(window, QStringLiteral("mkdir=true")));
  CHECK(backlog_contains(window, QStringLiteral("before=false,-1,false")));
  CHECK(backlog_contains(window, QStringLiteral("saved=true")));
  CHECK(backlog_contains(window, QStringLiteral("after=true,true")));
  CHECK(backlog_contains(window, QStringLiteral("listed=せす café")));
  CHECK(backlog_contains(window, QStringLiteral("path_ok=true")));
  CHECK(backlog_contains(window, QStringLiteral("deleted=true,false,-1")));
  CHECK(backlog_contains(window, QStringLiteral("dir_delete=false")));
  CHECK(QDir(dir + QStringLiteral("/サブ sub")).exists());
}

// patchy.io.writeTextFile replaces the file whole and reports failure to the script: a
// target that cannot be written throws (an existing file there is kept), and a
// successful write leaves only the file behind.
void ui_script_io_write_text_file_replaces_atomically_and_throws() {
  patchy::ui::MainWindow window;
  show_window(window);
  window.set_cli_automation_mode(true);
  patchy::test::ui::ensure_artifact_dir();
  const auto dir = QFileInfo(QStringLiteral("test-artifacts")).absoluteFilePath() + QStringLiteral("/script-io-text");
  remove_test_scratch_dir(dir);
  CHECK(QDir().mkpath(dir));

  auto& host = window.script_engine_host();
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("write-text-file");
  options.args = QStringList{QStringLiteral("dir=") + dir};
  const auto source = QStringLiteral(R"JS(
    var dir = patchy.args.dir;
    var p = dir + '/notes.txt';
    patchy.io.writeTextFile(p, 'first');
    patchy.io.writeTextFile(p, 'second, longer');
    console.log('text=' + patchy.io.readTextFile(p));
    var blocked = dir + '/blocked.txt';
    patchy.io.makeDir(blocked);
    var threw = '';
    try { patchy.io.writeTextFile(blocked, 'x'); } catch (e) { threw = String(e); }
    console.log('threw=' + (threw.indexOf('Could not write') >= 0));
    console.log('files=' + patchy.io.listFiles(dir).join('|'));
  )JS");
  (void)host.run_source(source, std::move(options));
  wait_for_run_end(host);
  CHECK(!host.last_run_had_error());
  CHECK(backlog_contains(window, QStringLiteral("text=second, longer")));
  CHECK(backlog_contains(window, QStringLiteral("threw=true")));
  CHECK(backlog_contains(window, QStringLiteral("files=notes.txt")));
  auto names = QDir(dir).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
  names.sort();
  CHECK(names == (QStringList{QStringLiteral("blocked.txt"), QStringLiteral("notes.txt")}));
}

// Save goes through a replacement write. When the file cannot be replaced (a folder now
// sits where the script was) the save fails, says so in the console, and leaves the
// editor modified so the edit is not mistaken for saved; once the path is writable
// again the same Save lands, and the folder holds only the script, no temporary.
void ui_script_editor_save_failure_keeps_file_and_modified_flag() {
  const StandardPathsTestMode test_paths;
  const auto user_dir = patchy::ui::MainWindow::user_scripts_directory();
  remove_test_scratch_dir(user_dir);
  const auto folder = user_dir + QStringLiteral("/Mine");
  CHECK(QDir().mkpath(folder));
  const auto path = folder + QStringLiteral("/keep.js");
  {
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("console.log('v1');\n");
  }
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::ScriptEditorDialog dialog(window, window.script_engine_host());
  dialog.show();
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  auto* code = dialog.findChild<QPlainTextEdit*>(QStringLiteral("scriptEditorCode"));
  auto* console_pane = dialog.findChild<QPlainTextEdit*>(QStringLiteral("scriptEditorConsole"));
  auto* save_button = dialog.findChild<QPushButton*>(QStringLiteral("scriptEditorSaveButton"));
  CHECK(code != nullptr && console_pane != nullptr && save_button != nullptr);
  dialog.open_script(path);
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  CHECK(code->toPlainText().contains(QStringLiteral("v1")));
  code->textCursor().insertText(QStringLiteral("// edited\n"));
  CHECK(code->document()->isModified());

  // A folder in the script's place: the replacement cannot land.
  CHECK(QFile::remove(path));
  CHECK(QDir().mkpath(path + QStringLiteral("/inner")));
  save_button->click();
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  CHECK(code->document()->isModified());
  CHECK(console_pane->toPlainText().contains(QStringLiteral("Could not write")));
  CHECK(QDir(path + QStringLiteral("/inner")).exists());
  CHECK(QDir(folder).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden) ==
        QStringList{QStringLiteral("keep.js")});

  CHECK(QDir(path).rmdir(QStringLiteral("inner")));
  CHECK(QDir(folder).rmdir(QStringLiteral("keep.js")));
  save_button->click();
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  CHECK(!code->document()->isModified());
  QFile saved(path);
  CHECK(saved.open(QIODevice::ReadOnly));
  const auto text = QString::fromUtf8(saved.readAll());
  CHECK(text.contains(QStringLiteral("// edited")) && text.contains(QStringLiteral("v1")));
  CHECK(QDir(folder).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden) ==
        QStringList{QStringLiteral("keep.js")});
  dialog.close();
}

void ui_script_unattended_normalizes_forms_and_guards_commands() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto& host = window.script_engine_host();
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("unattended-regressions");
  options.unattended = true;
  CHECK(host.run_source(QStringLiteral(R"JS(
    function check(ok, message) { if (!ok) throw new Error(message); }
    var doc = app.activeDocument;
    doc.addLayer('Keep me').fill('#aabbcc');
    check(!app.runCommand('edit.undo'), 'undo must not alter the running transaction');
    check(!app.runCommand('edit.redo'), 'redo must not alter the running transaction');
    check(!app.runCommand('file.quit'), 'quit must not tear down the engine');
    app.runCommand('file.close');
    check(app.activeDocument.id === doc.id, 'modified document must stay open');
    var r = patchy.ui.showDialog({fields:[
      {key:'mode', type:'choice', choices:['first','second'], value:1},
      {key:'color', type:'color', value:'RED'},
      {key:'number', type:'number', min:2, max:5, value:99},
      {key:'text', type:'text'}, {key:'checked', type:'checkbox'}]});
    check(r.mode === 'second', 'choice index must normalize to text');
    check(r.color === '#ff0000', 'color must normalize');
    check(r.number === 5 && r.text === '' && r.checked === false, 'defaults and limits');
    var rejected = false;
    try { patchy.ui.showDialog({fields:[{type:'text'}]}); } catch(e) { rejected = true; }
    check(rejected, 'missing field key must throw');
    rejected = false;
    try { app.open('this-file-does-not-exist.psd'); } catch(e) { rejected = true; }
    check(rejected, 'failed open must throw without prompting');
    console.log('unattended-complete');
  )JS"), std::move(options)));
  wait_for_run_end(host);
  CHECK(!host.run_active());
  CHECK(!host.last_run_had_error());
  CHECK(backlog_contains(window, QStringLiteral("unattended-complete")));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == 1);
}

void ui_script_geometry_rgb_fill_and_empty_text_regressions() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  patchy::PixelBuffer rgb(4, 3, patchy::PixelFormat::rgb8());
  std::fill(rgb.data().begin(), rgb.data().end(), std::uint8_t{90});
  document.add_layer(patchy::Layer(document.allocate_layer_id(), "RgbPhoto", std::move(rgb)));
  CHECK(run_script(window, QStringLiteral(R"JS(
    function check(ok, message) { if (!ok) throw new Error(message); }
    var doc = app.activeDocument, layer = doc.findLayer('RgbPhoto');
    layer.fillRect(1, 1, 1, 1, '#ff0000');
    var p = new Uint8Array(layer.getPixels().data);
    check(p[0] === 90 && p[3] === 255 && p[20] === 255 && p[21] === 0, 'RGB promotion');
    layer.fill('#00ff00');
    p = new Uint8Array(layer.getPixels().data);
    check(p[0] === 0 && p[1] === 255 && p[3] === 255, 'RGB fill');
    var rejected = false;
    try { layer.moveTo(2147483648, 0); } catch(e) { rejected = true; }
    check(rejected && layer.x === 0, 'move overflow must throw without mutation');
    doc.selection.selectRect(5000, 5000, 20, 20);
    check(!doc.selection.exists, 'outside selection must be empty');
    doc.selection.selectRect(-3, -5, 10, 10);
    var b = doc.selection.bounds;
    check(b.x === 0 && b.y === 0 && b.width === 7 && b.height === 5, 'selection must clip');
    doc.selection.deselect();
    var text = doc.addTextLayer('Clear me', {x:20,y:20,size:24});
    text.text = '';
    check(text.text === '', 'empty text must commit');
    var pixels = new Uint8Array(text.getPixels().data);
    for (var i=3;i<pixels.length;i+=4) check(pixels[i] === 0, 'empty text has no ink');
  )JS")));
}

void ui_script_layer_duplicate_to_document() {
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral(R"JS(
    var a=app.newDocument(64,48);
    var mark=a.activeLayer; mark.name='Mark';
    var b=app.newDocument(64,48);
    var copy=mark.duplicate(b);
    if(copy.name!=='Mark')throw Error('name '+copy.name);
    if(b.layers.length!==2)throw Error('b layers '+b.layers.length);
    if(a.layers.length!==1)throw Error('a layers '+a.layers.length);
    if(b.activeLayer.name!=='Mark')throw Error('active '+b.activeLayer.name);
    if(copy.x!==mark.x||copy.y!==mark.y)throw Error('position '+copy.x+','+copy.y);
    var same=mark.duplicate(a);
    if(a.layers.length!==2||same.name!=='Mark copy')throw Error('same-document path '+same.name);
    mark.duplicate();
    if(a.layers.length!==3)throw Error('no-argument path');
    var threw=false; try{mark.duplicate('nope');}catch(e){threw=true;}
    if(!threw)throw Error('bad target accepted');
    console.log('dup-ok '+b.canUndo+' '+a.canUndo);
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("dup-ok true true")));
}

}  // namespace

std::vector<patchy::test::TestCase> scripting_tests_part3() {
  return {
      {"ui_script_manager_new_button_inserts_template", ui_script_manager_new_button_inserts_template},
      {"ui_script_metadata_icons_and_write_target", ui_script_metadata_icons_and_write_target},
      {"ui_script_manager_set_icon_from_document", ui_script_manager_set_icon_from_document},
      {"ui_script_show_options_unattended_merges_args", ui_script_show_options_unattended_merges_args},
      {"ui_script_show_options_dialog_description_and_folder", ui_script_show_options_dialog_description_and_folder},
      {"ui_script_busy_overlay_and_timer_guard", ui_script_busy_overlay_and_timer_guard},
      {"ui_script_manager_hover_card_shows_details", ui_script_manager_hover_card_shows_details},
      {"ui_script_include_bundled_root_and_is_main", ui_script_include_bundled_root_and_is_main},
      {"ui_sound_build_tone_wav_shape", ui_sound_build_tone_wav_shape},
      {"ui_script_play_tone_and_sound_offscreen", ui_script_play_tone_and_sound_offscreen},
      {"ui_script_fancy_background_runs_standalone", ui_script_fancy_background_runs_standalone},
      {"ui_script_dialog_pickers_listfiles_args_cli_defaults", ui_script_dialog_pickers_listfiles_args_cli_defaults},
      {"ui_script_run_command_triggers_actions", ui_script_run_command_triggers_actions},
      {"ui_script_cli_directive_and_example_command", ui_script_cli_directive_and_example_command},
      {"ui_script_manager_cli_example_dialog", ui_script_manager_cli_example_dialog},
      {"ui_script_scripting_guide_opens_from_help", ui_script_scripting_guide_opens_from_help},
      {"ui_ai_setup_dialog_opens_from_help", ui_ai_setup_dialog_opens_from_help},
      {"ui_ai_setup_blurb_reports_missing_and_flatpak_forms", ui_ai_setup_blurb_reports_missing_and_flatpak_forms},
      {"ui_ai_setup_examples_keep_installation_prompt_stable", ui_ai_setup_examples_keep_installation_prompt_stable},
      {"ui_script_ui_view_zoom", ui_script_ui_view_zoom},
      {"ui_script_ui_staging_apis", ui_script_ui_staging_apis},
      {"ui_script_ui_side_panels_expand_collapse_close", ui_script_ui_side_panels_expand_collapse_close},
      {"ui_script_ui_expand_panel_in_cli_capture", ui_script_ui_expand_panel_in_cli_capture},
      {"ui_script_active_layer_setter_reveals_row", ui_script_active_layer_setter_reveals_row},
      {"ui_script_webp_animation_export_preserves_document_and_validates_options", ui_script_webp_animation_export_preserves_document_and_validates_options},
      {"ui_script_io_round_trips_unicode_path", ui_script_io_round_trips_unicode_path},
      {"ui_script_io_write_text_file_replaces_atomically_and_throws", ui_script_io_write_text_file_replaces_atomically_and_throws},
      {"ui_script_editor_save_failure_keeps_file_and_modified_flag", ui_script_editor_save_failure_keeps_file_and_modified_flag},
      {"ui_script_unattended_normalizes_forms_and_guards_commands", ui_script_unattended_normalizes_forms_and_guards_commands},
      {"ui_script_geometry_rgb_fill_and_empty_text_regressions", ui_script_geometry_rgb_fill_and_empty_text_regressions},
      {"ui_script_layer_duplicate_to_document", ui_script_layer_duplicate_to_document},
  };
}
