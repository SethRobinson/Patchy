// Automatic document recovery (docs/document-recovery.md): the on-demand and timed
// recovery writes, the (revision, state id) dirty rule, the save/close discards,
// the busy skip, orphan detection through the QLockFile liveness check, the
// reopen of a crashed instance's copies, Unicode paths, the Preferences row, and
// the atomic flat-image save.

#include "core/document.hpp"
#include "core/document_recovery_store.hpp"
#include "psd/psd_document_io.hpp"
#include "ui/app_settings.hpp"
#include "ui/document_recovery.hpp"
#include "ui/image_document_io.hpp"
#include "ui/main_window.hpp"
#include "ui/qt_paths.hpp"
#include "ui/script_engine.hpp"

#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include "unicode_path_names.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#ifdef Q_OS_WIN
#include <stdlib.h>
#endif

using patchy::test::kUnicodeCombinedStem;
using patchy::test::kUnicodeDirName;
using patchy::test::ui::ensure_artifact_dir;
using patchy::test::ui::find_top_level_dialog;
using patchy::test::ui::process_events_for;
using patchy::test::ui::process_events_until;
using patchy::test::ui::require_action;
using patchy::test::ui::SettingsValueRestorer;
using patchy::test::ui::show_window;

namespace {

QString q(std::u8string_view text) {
  return QString::fromUtf8(reinterpret_cast<const char*>(text.data()), static_cast<qsizetype>(text.size()));
}

QString nfc(const QString& text) { return text.normalized(QString::NormalizationForm_C); }

// qputenv takes local 8-bit bytes, which on Windows means the ANSI code page: a
// Unicode root would come back from qEnvironmentVariable (UTF-16 on Windows) as
// mojibake. The wide CRT call keeps every character.
void set_recovery_dir_env(const QString& root) {
#ifdef Q_OS_WIN
  CHECK(_wputenv_s(L"PATCHY_RECOVERY_DIR", root.toStdWString().c_str()) == 0);
#else
  qputenv("PATCHY_RECOVERY_DIR", root.toUtf8());
#endif
}

// A fresh recovery root for one test, exported through PATCHY_RECOVERY_DIR so every
// window built afterwards (and RecoveryInstanceFolder::recovery_root) uses it.
QString fresh_recovery_root(const QString& leaf, const QString& parent = QStringLiteral("recovery")) {
  ensure_artifact_dir();
  const auto root = QFileInfo(QStringLiteral("test-artifacts")).absoluteFilePath() + QLatin1Char('/') + parent +
                    QLatin1Char('/') + leaf;
  QDir(root).removeRecursively();
  CHECK(QDir().mkpath(root));
  set_recovery_dir_env(root);
  return root;
}

// Restores the interval override and the recovery preferences a test changes.
struct RecoveryEnvRestorer {
  SettingsValueRestorer enabled{QStringLiteral("recovery/enabled")};
  SettingsValueRestorer interval{QStringLiteral("recovery/intervalMinutes")};
  RecoveryEnvRestorer() { qunsetenv("PATCHY_RECOVERY_INTERVAL_MS"); }
  ~RecoveryEnvRestorer() { qunsetenv("PATCHY_RECOVERY_INTERVAL_MS"); }
};

bool run_script(patchy::ui::MainWindow& window, const QString& source) {
  auto& host = window.script_engine_host();
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("recovery-test-script");
  (void)host.run_source(source, std::move(options));
  QElapsedTimer timer;
  timer.start();
  while (host.run_active() && timer.elapsed() < 15000) {
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
  }
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
  CHECK(!host.run_active());
  return !host.last_run_had_error();
}

bool backlog_contains(patchy::ui::MainWindow& window, const QString& needle) {
  for (const auto& line : window.script_engine_host().message_backlog()) {
    if (line.contains(needle)) {
      return true;
    }
  }
  return false;
}

// Adds a modified document: a new 16x12 document filled with `color`.
void add_filled_document(patchy::ui::MainWindow& window, const char* color) {
  CHECK(run_script(window, QStringLiteral("var d=app.newDocument(16,12);d.activeLayer.fill('%1');")
                               .arg(QLatin1String(color))));
}

QColor psb_pixel(const QString& psb_path, int x, int y) {
  const auto document = patchy::psd::DocumentIo::read_file(patchy::ui::to_filesystem_path(psb_path));
  CHECK(!document.layers().empty());
  if (document.layers().empty()) {
    return {};
  }
  const auto* px = document.layers().front().pixels().pixel(x, y);
  return QColor(px[0], px[1], px[2]);
}

std::filesystem::path fs(const QString& path) { return patchy::ui::to_filesystem_path(path); }

QStringList list_names(const QString& dir) {
  auto names = QDir(dir).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
  for (auto& name : names) {
    name = nfc(name);
  }
  names.sort();
  return names;
}

patchy::Document small_document(const QColor& fill = QColor(20, 120, 220)) {
  QImage source(8, 6, QImage::Format_RGBA8888);
  source.fill(fill);
  source.setPixelColor(1, 0, QColor(90, 30, 180, 255));
  return patchy::ui::document_from_qimage(source, "Recovery");
}

// Seeds `<root>/<folder>` the way a crashed instance leaves it: the entries, plus a
// QLockFile whose pid is dead (Qt's own liveness check declares it stale).
void seed_orphan_folder(const QString& root, const QString& folder,
                        const std::vector<std::pair<std::int64_t, patchy::recovery::RecoveryEntry>>& entries,
                        const patchy::Document& document = small_document()) {
  const auto dir = fs(root + QLatin1Char('/') + folder);
  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document, patchy::psd::WriteOptions{true});
  for (const auto& [session_id, entry] : entries) {
    patchy::recovery::write_entry(dir, session_id, bytes, entry);
  }
  QFile lock(patchy::ui::to_qstring(dir / "lock"));
  CHECK(lock.open(QIODevice::WriteOnly | QIODevice::Truncate));
  // pid, application name, host name: an empty host name matches any machine.
  lock.write("4000000000\n\n\n");
}

QTabWidget* document_tabs(patchy::ui::MainWindow& window) {
  return window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
}

void answer_save_prompt(patchy::ui::MainWindow& window, QMessageBox::StandardButton button, bool& seen) {
  auto* timer = new QTimer(&window);
  timer->setInterval(10);
  QObject::connect(timer, &QTimer::timeout, &window, [&seen, timer, button] {
    auto* dialog = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("saveChangesMessageBox")));
    if (dialog == nullptr) {
      return;
    }
    seen = true;
    timer->stop();
    timer->deleteLater();
    dialog->button(button)->click();
  });
  timer->start();
}

}  // namespace

void ui_recovery_write_now_writes_psb_that_round_trips() {
  RecoveryEnvRestorer env;
  const auto root = fresh_recovery_root(QStringLiteral("write-now"));
  patchy::ui::MainWindow window;
  show_window(window);
  // Nothing is modified yet, so nothing is written and no folder appears.
  CHECK(window.write_recovery_now(true).isEmpty());
  CHECK(list_names(root).isEmpty());

  add_filled_document(window, "#204080");
  CHECK(run_script(window, QStringLiteral("console.log('written=' + JSON.stringify(patchy.recovery.writeNow()));"
                                          "console.log('dir=' + patchy.recovery.directory);")));
  CHECK(QDir::fromNativeSeparators(window.recovery_directory()).startsWith(root));
  CHECK(backlog_contains(window, QStringLiteral("dir=") + root));
  const auto entries = window.list_recovery_entries();
  CHECK(entries.size() == 1);
  if (entries.size() != 1) {
    return;
  }
  const auto psb_path = patchy::ui::to_qstring(patchy::recovery::document_path(fs(window.recovery_directory()),
                                                                              std::stoll(entries.front().file_stem)));
  CHECK(backlog_contains(window, QDir::fromNativeSeparators(psb_path)));
  CHECK(QFileInfo(psb_path).isFile());
  CHECK(psb_pixel(psb_path, 1, 0) == QColor(0x20, 0x40, 0x80));
  CHECK(entries.front().title == patchy::ui::MainWindowTestAccess::active_session_title(window).toStdString());
  CHECK(entries.front().original_path.empty());
  CHECK(entries.front().saved_at_unix_ms > 0);
  // The instance folder holds exactly the copy and its sidecar plus the lock.
  CHECK(list_names(window.recovery_directory()).size() == 3);
  // An unchanged state is not rewritten.
  CHECK(window.write_recovery_now(true).isEmpty());
  CHECK(run_script(window, QStringLiteral("var f=patchy.recovery.listFiles();"
                                          "if(f.length!==1)throw Error('listFiles ' + f.length);"
                                          "if(f[0].originalPath!=='')throw Error('path');"
                                          "if(!(f[0].savedAt>0))throw Error('savedAt');")));
}

void ui_recovery_skips_unchanged_session_and_rewrites_after_undo_then_edit() {
  RecoveryEnvRestorer env;
  fresh_recovery_root(QStringLiteral("dirty-rule"));
  patchy::ui::MainWindow window;
  show_window(window);
  add_filled_document(window, "#ff0000");
  const auto first = window.write_recovery_now(true);
  CHECK(first.size() == 1);
  CHECK(window.write_recovery_now(true).isEmpty());
  // Undo lands on the initial (saved) revision: unmodified, so nothing to write.
  CHECK(run_script(window, "app.activeDocument.undo();"));
  CHECK(!patchy::ui::MainWindowTestAccess::active_session_is_modified(window));
  CHECK(window.write_recovery_now(true).isEmpty());
  // Redo restores the exact state the copy holds (same revision AND state id).
  CHECK(run_script(window, "app.activeDocument.redo();"));
  CHECK(window.write_recovery_now(true).isEmpty());
  // Undo then a different edit: the revision number repeats, the state id does
  // not, so the copy must be rewritten with the new content.
  CHECK(run_script(window, "app.activeDocument.undo();app.activeDocument.activeLayer.fill('#00ff00');"));
  const auto rewritten = window.write_recovery_now(true);
  CHECK(rewritten.size() == 1);
  if (!rewritten.isEmpty()) {
    CHECK(rewritten.front() == first.front());
    CHECK(psb_pixel(rewritten.front(), 1, 0) == QColor(0, 255, 0));
  }
}

void ui_recovery_file_removed_on_save_and_on_close() {
  RecoveryEnvRestorer env;
  const auto root = fresh_recovery_root(QStringLiteral("save-close"));
  patchy::ui::MainWindow window;
  show_window(window);
  window.set_cli_automation_mode(true);
  add_filled_document(window, "#123456");
  CHECK(window.write_recovery_now(true).size() == 1);
  CHECK(window.list_recovery_entries().size() == 1);

  // A successful save makes the copy stale, so it goes.
  const auto saved_path = root + QStringLiteral("/saved.psd");
  CHECK(patchy::ui::MainWindowTestAccess::save_document_to_path(window, saved_path));
  CHECK(window.list_recovery_entries().empty());
  CHECK(window.write_recovery_now(true).isEmpty());

  // Modified again: a new copy, now carrying the file's path.
  CHECK(run_script(window, "app.activeDocument.activeLayer.fill('#654321');"));
  CHECK(window.write_recovery_now(true).size() == 1);
  const auto entries = window.list_recovery_entries();
  CHECK(entries.size() == 1);
  if (!entries.empty()) {
    CHECK(nfc(QString::fromStdString(entries.front().original_path)) == nfc(saved_path));
    CHECK(entries.front().title == "saved.psd");
  }

  // Closing the document (discarding its changes) drops the copy too.
  window.set_cli_automation_mode(false);
  auto* tabs = document_tabs(window);
  CHECK(tabs != nullptr);
  bool prompt_seen = false;
  answer_save_prompt(window, QMessageBox::No, prompt_seen);
  CHECK(patchy::ui::MainWindowTestAccess::close_document_tab(window, tabs->currentIndex()));
  QApplication::processEvents();
  CHECK(prompt_seen);
  CHECK(window.list_recovery_entries().empty());
}

void ui_recovery_timer_skips_while_modal_dialog_open() {
  RecoveryEnvRestorer env;
  fresh_recovery_root(QStringLiteral("timer"));
  qputenv("PATCHY_RECOVERY_INTERVAL_MS", "50");
  patchy::ui::MainWindow window;
  show_window(window);
  auto* timer = window.findChild<QTimer*>(QStringLiteral("documentRecoveryTimer"));
  CHECK(timer != nullptr);
  CHECK(timer != nullptr && timer->isActive() && timer->interval() == 50);

  // The dialog goes up BEFORE the document becomes dirty (a tick between the edit
  // and the show would write legitimately), and the edit is a menu action rather
  // than a script so nothing pumps events in between.
  QDialog dialog(&window);
  dialog.setWindowModality(Qt::ApplicationModal);
  dialog.show();
  QApplication::processEvents();
  CHECK(QApplication::activeModalWidget() == &dialog);
  require_action(window, "layerNewAction")->trigger();
  CHECK(patchy::ui::MainWindowTestAccess::active_session_is_modified(window));
  const auto layers_at_edit = patchy::ui::MainWindowTestAccess::document(window).layers().size();
  // A very coarse 50 ms timer fires about once a second; 1.5 s covers a tick.
  process_events_for(1500);
  CHECK(window.list_recovery_entries().empty());

  // Once it closes, the next tick writes.
  dialog.hide();
  QApplication::processEvents();
  CHECK(process_events_until([&] { return window.list_recovery_entries().size() == 1; }, 5000));

  // Disabling the preference stops the timer; a further edit is not written.
  CHECK(run_script(window, "patchy.recovery.enabled=false;if(patchy.recovery.enabled)throw Error('enabled');"));
  CHECK(timer != nullptr && !timer->isActive());
  require_action(window, "layerNewAction")->trigger();
  CHECK(patchy::ui::MainWindowTestAccess::document(window).layers().size() == layers_at_edit + 1);
  process_events_for(1500);
  const auto entries = window.list_recovery_entries();
  CHECK(entries.size() == 1);
  if (!entries.empty()) {
    const auto psb = patchy::ui::to_qstring(
        patchy::recovery::document_path(fs(window.recovery_directory()), std::stoll(entries.front().file_stem)));
    const auto copy = patchy::psd::DocumentIo::read_file(fs(psb));
    CHECK(copy.layers().size() == layers_at_edit);
  }
  CHECK(run_script(window, "patchy.recovery.enabled=true;"));
  CHECK(timer != nullptr && timer->isActive());
}

void ui_recovery_orphaned_folder_recovers_as_modified_document() {
  RecoveryEnvRestorer env;
  const auto root = fresh_recovery_root(QStringLiteral("orphans"));
  const auto original = root + QStringLiteral("/Poster.png");
  patchy::recovery::RecoveryEntry titled;
  titled.title = "Poster.png";
  titled.original_path = original.toStdString();
  titled.saved_at_unix_ms = 7;
  // Session 9 has no sidecar at all: a crash between the two writes.
  seed_orphan_folder(root, QStringLiteral("4000000000-1"), {{5, titled}});
  seed_orphan_folder(root, QStringLiteral("4000000000-2"), {{9, patchy::recovery::RecoveryEntry{}}});
  QFile::remove(root + QStringLiteral("/4000000000-2/9.recovery"));
  // An empty orphan folder (an instance that never wrote) is swept, not listed.
  CHECK(QDir().mkpath(root + QStringLiteral("/4000000000-3")));

  patchy::ui::MainWindow window;
  show_window(window);
  const auto orphans = window.list_orphaned_recovery();
  CHECK(orphans.size() == 2);
  CHECK(!QDir(root + QStringLiteral("/4000000000-3")).exists());
  CHECK(run_script(window, QStringLiteral(
                               "var o=patchy.recovery.listOrphaned();"
                               "console.log('orphans=' + o.map(function(e){return e.title + '|' + e.originalPath;}).join(';'));")));
  CHECK(backlog_contains(window, QStringLiteral("orphans=Poster.png|") + original + QStringLiteral(";|")));

  const auto before = patchy::ui::MainWindowTestAccess::session_count(window);
  CHECK(run_script(window, QStringLiteral(
                               "var docs=patchy.recovery.recoverAll();"
                               "console.log('recovered=' + docs.map(function(d){return d.name;}).join(';'));")));
  CHECK(backlog_contains(window, QStringLiteral("recovered=Poster.png (Recovered);Untitled (Recovered)")));
  CHECK(patchy::ui::MainWindowTestAccess::session_count(window) == before + 2);
  // The last recovered one is active: untitled, modified, no path.
  CHECK(patchy::ui::MainWindowTestAccess::active_session_is_modified(window));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_title(window) == QStringLiteral("Untitled (Recovered)"));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_path(window).isEmpty());
  CHECK(patchy::ui::MainWindowTestAccess::document(window).width() == 8);
  // The titled one keeps its original path so Save goes back to the user's file.
  CHECK(run_script(window, QStringLiteral("var d=app.documents.filter(function(d){return d.name==='Poster.png (Recovered)';})[0];"
                                          "if(!d)throw Error('missing');"
                                          "if(d.path!==%1)throw Error('path ' + d.path);")
                               .arg(QStringLiteral("'") + QDir::fromNativeSeparators(original) + QStringLiteral("'"))));
  // The copies moved under this instance; the orphan folders are gone; nothing is
  // left to recover, and the copies are not rewritten (their state is current).
  CHECK(window.list_orphaned_recovery().empty());
  CHECK(!QDir(root + QStringLiteral("/4000000000-1")).exists());
  CHECK(!QDir(root + QStringLiteral("/4000000000-2")).exists());
  CHECK(window.list_recovery_entries().size() == 2);
  CHECK(window.write_recovery_now(true).isEmpty());
  CHECK(window.discard_orphaned_recovery() == 0);
}

void ui_recovery_live_instance_is_not_reported_as_orphaned() {
  RecoveryEnvRestorer env;
  const auto root = fresh_recovery_root(QStringLiteral("liveness"));
  patchy::ui::MainWindow observer;
  show_window(observer);
  QString live_dir;
  {
    patchy::ui::MainWindow writer;
    show_window(writer);
    add_filled_document(writer, "#0000ff");
    CHECK(writer.write_recovery_now(true).size() == 1);
    live_dir = writer.recovery_directory();
    CHECK(QDir(live_dir).exists());
    // The writer is alive (its lock is held), so its folder is not an orphan.
    CHECK(observer.list_orphaned_recovery().empty());
    CHECK(run_script(observer, "if(patchy.recovery.listOrphaned().length!==0)throw Error('live orphan');"));
  }
  // Destroying the window is a normal exit: its folder is removed.
  CHECK(!QDir(live_dir).exists());
  CHECK(observer.list_orphaned_recovery().empty());
  // A folder whose lock names a dead process IS an orphan, and discardOrphaned drops it.
  seed_orphan_folder(root, QStringLiteral("4000000000-9"), {{2, patchy::recovery::RecoveryEntry{}}});
  CHECK(observer.list_orphaned_recovery().size() == 1);
  CHECK(run_script(observer, "if(patchy.recovery.discardOrphaned()!==1)throw Error('discard');"));
  CHECK(observer.list_orphaned_recovery().empty());
  CHECK(!QDir(root + QStringLiteral("/4000000000-9")).exists());
}

void ui_recovery_round_trips_unicode_paths() {
  RecoveryEnvRestorer env;
  const auto root = fresh_recovery_root(QStringLiteral("recovery"), q(kUnicodeDirName));
  const auto original = root + QLatin1Char('/') + q(kUnicodeCombinedStem) + QStringLiteral(".psd");
  QString psb_path;
  {
    patchy::ui::MainWindow window;
    show_window(window);
    window.set_cli_automation_mode(true);
    CHECK(patchy::ui::MainWindowTestAccess::save_document_to_path(window, original));
    CHECK(run_script(window, "app.activeDocument.activeLayer.fill('#336699');"));
    const auto written = window.write_recovery_now(true);
    CHECK(written.size() == 1);
    if (written.isEmpty()) {
      return;
    }
    psb_path = written.front();
    CHECK(nfc(QDir::fromNativeSeparators(psb_path)).startsWith(nfc(root)));
    CHECK(QFileInfo(psb_path).isFile());
    const auto entries = window.list_recovery_entries();
    CHECK(entries.size() == 1);
    if (!entries.empty()) {
      CHECK(nfc(QString::fromStdString(entries.front().original_path)) == nfc(original));
      CHECK(nfc(QString::fromStdString(entries.front().title)) == nfc(q(kUnicodeCombinedStem) + QStringLiteral(".psd")));
    }
    // Stage a crash: keep the copy as an orphan for the next window.
    patchy::recovery::RecoveryEntry entry = entries.empty() ? patchy::recovery::RecoveryEntry{} : entries.front();
    QFile file(psb_path);
    CHECK(file.open(QIODevice::ReadOnly));
    const auto bytes = file.readAll();
    patchy::recovery::write_entry(fs(root + QStringLiteral("/4000000000-7")), 3,
                                  std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(bytes.constData()),
                                                                static_cast<std::size_t>(bytes.size())),
                                  entry);
  }
  patchy::ui::MainWindow window;
  show_window(window);
  const auto recovered = window.recover_orphaned_documents();
  CHECK(recovered.size() == 1);
  CHECK(nfc(patchy::ui::MainWindowTestAccess::active_session_title(window)) ==
        nfc(q(kUnicodeCombinedStem) + QStringLiteral(".psd (Recovered)")));
  CHECK(nfc(patchy::ui::MainWindowTestAccess::active_session_path(window)) == nfc(original));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_is_modified(window));
  CHECK(patchy::ui::MainWindowTestAccess::document(window).width() == 1024);
  // The listing shows exactly the user's file, the instance folder, and the sweep
  // left no mojibake sibling behind.
  const auto names = list_names(root);
  CHECK(names.size() == 2);
  CHECK(names.contains(nfc(q(kUnicodeCombinedStem) + QStringLiteral(".psd"))));
  CHECK(names.contains(nfc(QFileInfo(window.recovery_directory()).fileName())));
}

void ui_flat_save_is_atomic_and_reports_failure() {
  ensure_artifact_dir();
  const auto dir = QFileInfo(QStringLiteral("test-artifacts")).absoluteFilePath() + QStringLiteral("/atomic-save");
  QDir(dir).removeRecursively();
  CHECK(QDir().mkpath(dir));
  patchy::ui::MainWindow window;
  show_window(window);
  window.set_cli_automation_mode(true);
  const auto png = dir + QStringLiteral("/a.png");
  const auto psd = dir + QStringLiteral("/b.psd");
  CHECK(patchy::ui::MainWindowTestAccess::save_document_to_path(window, png));
  CHECK(patchy::ui::MainWindowTestAccess::save_document_to_path(window, psd));
  const auto png_size = QFileInfo(png).size();
  const auto psd_size = QFileInfo(psd).size();
  CHECK(png_size > 0 && psd_size > 0);
  // Overwriting through the temporary-file rename leaves exactly the two files.
  CHECK(run_script(window, "app.activeDocument.activeLayer.fill('#ff8800');"));
  CHECK(patchy::ui::MainWindowTestAccess::save_document_to_path(window, png));
  CHECK(patchy::ui::MainWindowTestAccess::save_document_to_path(window, psd));
  CHECK(list_names(dir) == (QStringList{QStringLiteral("a.png"), QStringLiteral("b.psd")}));
  const auto rewritten = QImage(png);
  CHECK(rewritten.pixelColor(3, 3) == QColor(0xff, 0x88, 0x00));
  // A save into a missing folder fails and leaves the existing files untouched.
  CHECK(!patchy::ui::MainWindowTestAccess::save_document_to_path(window, dir + QStringLiteral("/missing/c.png")));
  CHECK(!patchy::ui::MainWindowTestAccess::save_document_to_path(window, dir + QStringLiteral("/missing/c.psd")));
  CHECK(list_names(dir) == (QStringList{QStringLiteral("a.png"), QStringLiteral("b.psd")}));
}

void ui_preferences_recovery_controls_persist() {
  RecoveryEnvRestorer env;
  fresh_recovery_root(QStringLiteral("preferences"));
  patchy::ui::set_stored_recovery_enabled(true);
  patchy::ui::set_stored_recovery_interval_minutes(10);
  patchy::ui::MainWindow window;
  show_window(window);
  auto* timer = window.findChild<QTimer*>(QStringLiteral("documentRecoveryTimer"));
  CHECK(timer != nullptr);
  CHECK(timer != nullptr && timer->isActive() && timer->interval() == 10 * 60 * 1000);

  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* check = dialog->findChild<QCheckBox*>(QStringLiteral("preferencesRecoveryEnabledCheck"));
    auto* combo = dialog->findChild<QComboBox*>(QStringLiteral("preferencesRecoveryIntervalCombo"));
    CHECK(check != nullptr && combo != nullptr);
    if (check == nullptr || combo == nullptr) {
      dialog->reject();
      return;
    }
    CHECK(check->isChecked());
    CHECK(combo->isEnabled());
    CHECK(combo->currentData().toInt() == 10);
    CHECK(combo->count() == static_cast<int>(patchy::ui::kRecoveryIntervalMinutes.size()));
    combo->setCurrentIndex(combo->findData(30));
    check->setChecked(false);
    CHECK(!combo->isEnabled());
    saw_dialog = true;
    dialog->accept();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);
  CHECK(!patchy::ui::stored_recovery_enabled());
  CHECK(patchy::ui::stored_recovery_interval_minutes() == 30);
  CHECK(timer != nullptr && !timer->isActive());

  // The scripting properties are the same settings and re-arm the timer.
  CHECK(run_script(window, "patchy.recovery.intervalMinutes=15;patchy.recovery.enabled=true;"
                           "if(patchy.recovery.intervalMinutes!==15)throw Error('interval');"));
  CHECK(timer != nullptr && timer->isActive() && timer->interval() == 15 * 60 * 1000);
  CHECK(!run_script(window, "patchy.recovery.intervalMinutes=7;"));
  CHECK(patchy::ui::stored_recovery_interval_minutes() == 15);
}

std::vector<patchy::test::TestCase> document_recovery_tests() {
  return {
      {"ui_recovery_write_now_writes_psb_that_round_trips", ui_recovery_write_now_writes_psb_that_round_trips},
      {"ui_recovery_skips_unchanged_session_and_rewrites_after_undo_then_edit",
       ui_recovery_skips_unchanged_session_and_rewrites_after_undo_then_edit},
      {"ui_recovery_file_removed_on_save_and_on_close", ui_recovery_file_removed_on_save_and_on_close},
      {"ui_recovery_timer_skips_while_modal_dialog_open", ui_recovery_timer_skips_while_modal_dialog_open},
      {"ui_recovery_orphaned_folder_recovers_as_modified_document",
       ui_recovery_orphaned_folder_recovers_as_modified_document},
      {"ui_recovery_live_instance_is_not_reported_as_orphaned", ui_recovery_live_instance_is_not_reported_as_orphaned},
      {"ui_recovery_round_trips_unicode_paths", ui_recovery_round_trips_unicode_paths},
      {"ui_flat_save_is_atomic_and_reports_failure", ui_flat_save_is_atomic_and_reports_failure},
      {"ui_preferences_recovery_controls_persist", ui_preferences_recovery_controls_persist},
  };
}
