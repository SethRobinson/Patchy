// Hotkey files (Export... / Import...) and the built-in shortcut sets on Preferences >
// Hotkeys (GitHub issue 633; docs/hotkey-sets.md).
#include "ui/app_settings.hpp"
#include "ui/hotkey_editor.hpp"
#include "ui/hotkey_files.hpp"
#include "ui/hotkey_registry.hpp"
#include "ui/hotkey_sets.hpp"
#include "ui/main_window.hpp"
#include "ui/qt_paths.hpp"
#include "test_harness.hpp"
#include "unicode_path_names.hpp"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QSet>
#include <QVariant>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <vector>
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

namespace {

using namespace patchy::test::ui;
using patchy::ui::HotkeyCommand;
using patchy::ui::HotkeyOverrideMap;

// Keeps the user's hotkeys out of (and restored after) a test that changes them.
class HotkeyGroupRestorer {
public:
  HotkeyGroupRestorer() {
    auto settings = patchy::ui::app_settings();
    settings.beginGroup(QStringLiteral("hotkeys"));
    for (const auto& key : settings.childKeys()) {
      saved_.insert(key, settings.value(key));
    }
    settings.endGroup();
    settings.remove(QStringLiteral("hotkeys"));
    settings.sync();
  }
  ~HotkeyGroupRestorer() {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("hotkeys"));
    settings.beginGroup(QStringLiteral("hotkeys"));
    for (auto it = saved_.constBegin(); it != saved_.constEnd(); ++it) {
      settings.setValue(it.key(), it.value());
    }
    settings.endGroup();
    settings.sync();
  }
  HotkeyGroupRestorer(const HotkeyGroupRestorer&) = delete;
  HotkeyGroupRestorer& operator=(const HotkeyGroupRestorer&) = delete;

private:
  QHash<QString, QVariant> saved_;
};

std::vector<HotkeyCommand> sample_commands() {
  std::vector<HotkeyCommand> commands;
  commands.push_back({QStringLiteral("file.new"), QString(), nullptr, {QKeySequence(QStringLiteral("Ctrl+N"))}});
  commands.push_back({QStringLiteral("view.grid"), QString(), nullptr, {QKeySequence(QStringLiteral("Ctrl+'"))}});
  commands.push_back({QStringLiteral("edit.undo"), QString(), nullptr, {QKeySequence(QStringLiteral("Ctrl+Z"))}});
  commands.push_back({QStringLiteral("view.hide_panels"), QString(), nullptr, {QKeySequence(QStringLiteral("Tab"))}});
  return commands;
}

bool same_overrides(const HotkeyOverrideMap& a, const HotkeyOverrideMap& b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (auto it = a.constBegin(); it != a.constEnd(); ++it) {
    if (!b.contains(it.key()) || !patchy::ui::hotkey_shortcut_lists_equivalent(it.value(), b.value(it.key()))) {
      return false;
    }
  }
  return true;
}

void ui_hotkey_file_round_trips_overrides() {
  HotkeyOverrideMap overrides;
  // Ctrl+; and Ctrl+, carry the separators a joined string would trip on.
  overrides.insert(QStringLiteral("file.new"),
                   {QKeySequence(QStringLiteral("Ctrl+;")), QKeySequence(QStringLiteral("Ctrl+Shift+,"))});
  overrides.insert(QStringLiteral("edit.undo"), {});  // deliberately unbound
  const auto bytes = patchy::ui::hotkey_overrides_to_json(overrides);
  const auto root = QJsonDocument::fromJson(bytes).object();
  CHECK(root.value(QStringLiteral("format")).toString() == QStringLiteral("patchy-hotkeys"));
  CHECK(root.value(QStringLiteral("version")).toInt() == 1);
  const auto result = patchy::ui::hotkey_overrides_from_json(bytes, sample_commands());
  CHECK(result.ok);
  CHECK(result.skipped_commands == 0);
  CHECK(result.skipped_keys == 0);
  CHECK(same_overrides(result.overrides, overrides));
  CHECK(result.overrides.value(QStringLiteral("edit.undo")).isEmpty());

  // Unknown commands and unusable keys are counted and dropped; the rest still imports.
  const QByteArray mixed = R"({"format": "patchy-hotkeys", "version": 1, "shortcuts": {
      "file.new": ["Ctrl+F9", "Space", "NotAKey+Q"],
      "view.grid": "Shift+F3",
      "view.hide_panels": ["Tab"],
      "edit.undo": ["Return"],
      "tools.not_in_patchy": ["Ctrl+K"],
      "script.gone": []}, "comment": "keys a later version adds are ignored"})";
  const auto partial = patchy::ui::hotkey_overrides_from_json(mixed, sample_commands());
  CHECK(partial.ok);
  CHECK(partial.skipped_commands == 2);
  CHECK(partial.skipped_keys == 3);  // Space, the unknown key name, Return
  CHECK(partial.overrides.value(QStringLiteral("file.new")) ==
        QList<QKeySequence>{QKeySequence(QStringLiteral("Ctrl+F9"))});
  CHECK(partial.overrides.value(QStringLiteral("view.grid")) ==
        QList<QKeySequence>{QKeySequence(QStringLiteral("Shift+F3"))});
  // Tab is reserved for the canvas, but it is this command's own default.
  CHECK(partial.overrides.value(QStringLiteral("view.hide_panels")) ==
        QList<QKeySequence>{QKeySequence(QStringLiteral("Tab"))});
  // A command whose only key was refused keeps its default instead of losing it.
  CHECK(!partial.overrides.contains(QStringLiteral("edit.undo")));
}

void ui_hotkey_file_rejects_malformed_files() {
  const auto commands = sample_commands();
  for (const QByteArray& bytes : {
           QByteArray(),
           QByteArray("not json at all"),
           QByteArray(R"(["file.new"])"),
           QByteArray(R"({"format": "patchy-theme", "version": 1, "shortcuts": {}})"),
           QByteArray(R"({"version": 1, "shortcuts": {}})"),
           QByteArray(R"({"format": "patchy-hotkeys", "shortcuts": {}})"),
           QByteArray(R"({"format": "patchy-hotkeys", "version": 0, "shortcuts": {}})"),
           QByteArray(R"({"format": "patchy-hotkeys", "version": 1})"),
           QByteArray(R"({"format": "patchy-hotkeys", "version": 1, "shortcuts": ["Ctrl+N"]})"),
           QByteArray(R"({"format": "patchy-hotkeys", "version": 1, "shortcuts": {"file.new": ["Ctrl+N"])"),
       }) {
    const auto result = patchy::ui::hotkey_overrides_from_json(bytes, commands);
    CHECK(!result.ok);
    CHECK(!result.error.isEmpty());
    CHECK(result.overrides.isEmpty());
  }
  // A missing file fails the same way, with its own message.
  const auto missing = patchy::ui::read_hotkey_file(QStringLiteral("test-artifacts/no-such-hotkeys.patchykeys"), commands);
  CHECK(!missing.ok);
  CHECK(!missing.error.isEmpty());
}

void ui_hotkey_file_writes_and_reads_unicode_paths() {
  const auto dir = std::filesystem::absolute(patchy::test::unicode_artifact_dir(u8"hotkey-files"));
  const auto file = dir / patchy::test::unicode_path_piece(
                              std::u8string(patchy::test::kUnicodeCombinedStem) + u8".patchykeys");
  HotkeyOverrideMap overrides;
  overrides.insert(QStringLiteral("view.grid"), {QKeySequence(QStringLiteral("Ctrl+Alt+'"))});
  QString error;
  CHECK(patchy::ui::write_hotkey_file(patchy::ui::to_qstring(file), overrides, &error));
  CHECK(error.isEmpty());
  CHECK(patchy::test::directory_holds_only(dir, {file}));
  const auto result = patchy::ui::read_hotkey_file(patchy::ui::to_qstring(file), sample_commands());
  CHECK(result.ok);
  CHECK(same_overrides(result.overrides, overrides));
  // Writing again replaces the file whole (no stray temporary beside it).
  overrides.insert(QStringLiteral("file.new"), {});
  CHECK(patchy::ui::write_hotkey_file(patchy::ui::to_qstring(file), overrides, &error));
  CHECK(patchy::test::directory_holds_only(dir, {file}));
  CHECK(same_overrides(patchy::ui::read_hotkey_file(patchy::ui::to_qstring(file), sample_commands()).overrides,
                       overrides));
}

// Every set must resolve like the defaults: no key on two commands (Qt treats an ambiguous
// application shortcut as dead), every id a real command, no key the canvas reserves.
void ui_hotkey_sets_have_no_conflicts() {
  HotkeyGroupRestorer restore;
  patchy::ui::MainWindow window;
  const auto& commands = window.hotkey_registry().commands();
  CHECK(patchy::ui::builtin_hotkey_sets().size() == 3);
  QSet<QString> set_ids;
  for (const auto& set : patchy::ui::builtin_hotkey_sets()) {
    const auto set_id = QString::fromLatin1(set.id);
    CHECK(!set_ids.contains(set_id));
    set_ids.insert(set_id);
    QSet<QString> entry_ids;
    for (const auto& entry : set.entries) {
      const auto id = QString::fromLatin1(entry.id);
      const auto* command = window.hotkey_registry().find_command(id);
      if (command == nullptr) {
        std::fprintf(stderr, "set %s names an unknown command %s\n", set.id, entry.id);
      }
      CHECK(command != nullptr);
      if (entry_ids.contains(id)) {
        std::fprintf(stderr, "set %s lists %s twice\n", set.id, entry.id);
      }
      CHECK(!entry_ids.contains(id));
      entry_ids.insert(id);
    }
    const auto overrides = patchy::ui::hotkey_set_overrides(set, commands);
    CHECK(overrides.size() > 10);
    for (auto it = overrides.constBegin(); it != overrides.constEnd(); ++it) {
      for (const auto& sequence : it.value()) {
        const auto combination = sequence[0];
        const bool reserved = patchy::ui::is_reserved_binding_key(
            static_cast<int>(combination.key()), combination.keyboardModifiers());
        // A reserved key is fine only where one of Patchy's own defaults proves it works as a
        // shortcut (Shift+Backspace fills); otherwise the canvas would take it.
        const bool default_somewhere =
            std::any_of(commands.begin(), commands.end(),
                        [&](const HotkeyCommand& other) { return other.default_shortcuts.contains(sequence); });
        if (sequence.count() != 1 || (reserved && !default_somewhere)) {
          std::fprintf(stderr, "set %s gives %s the unusable key %s\n", set.id, it.key().toUtf8().constData(),
                       sequence.toString(QKeySequence::PortableText).toUtf8().constData());
          CHECK(false);
        }
      }
    }
    const auto resolved = patchy::ui::resolve_hotkey_assignments(commands, overrides);
    for (const auto& suppression : resolved.suppressions) {
      std::fprintf(stderr, "set %s: %s is on %s and %s\n", set.id,
                   suppression.sequence.toString(QKeySequence::PortableText).toUtf8().constData(),
                   suppression.id.toUtf8().constData(), suppression.winner_id.toUtf8().constData());
    }
    CHECK(resolved.suppressions.empty());
    CHECK(patchy::ui::matching_hotkey_set_id(overrides, commands) == set_id);
    // Exporting a set and importing the file gives the set back whole.
    const auto reread = patchy::ui::hotkey_overrides_from_json(patchy::ui::hotkey_overrides_to_json(overrides), commands);
    CHECK(reread.ok);
    CHECK(reread.skipped_commands == 0);
    CHECK(reread.skipped_keys == 0);
    CHECK(same_overrides(reread.overrides, overrides));
    CHECK(patchy::ui::find_builtin_hotkey_set(set_id) == &set);
  }
  CHECK(!set_ids.contains(QString::fromLatin1(patchy::ui::kPatchyHotkeySetId)));
  CHECK(patchy::ui::matching_hotkey_set_id({}, commands) == QString::fromLatin1(patchy::ui::kPatchyHotkeySetId));
}

QKeySequence first_shortcut(patchy::ui::MainWindow& window, const char* id) {
  const auto* command = window.hotkey_registry().find_command(QString::fromLatin1(id));
  CHECK(command != nullptr && command->action != nullptr);
  return command->action->shortcut();
}

patchy::ui::HotkeyEditorPanel* hotkey_panel(QDialog& dialog) {
  CHECK(select_preferences_page(dialog, QStringLiteral("Hotkeys")));
  QApplication::processEvents();
  auto* panel = dialog.findChild<patchy::ui::HotkeyEditorPanel*>(QStringLiteral("hotkeyEditorPanel"));
  CHECK(panel != nullptr);
  return panel;
}

// Picking a set in the combo stages it (OK applies it), the combo names it on the next
// visit, a hand edit turns it to "Custom", and Patchy (default) clears every override.
void ui_hotkey_editor_shortcut_set_combo_applies_set() {
  HotkeyGroupRestorer restore;
  auto&& [window, canvas] = ShownWindowFixture{};
  const auto* gimp = patchy::ui::find_builtin_hotkey_set(u"gimp");
  CHECK(gimp != nullptr);
  const auto gimp_overrides = patchy::ui::hotkey_set_overrides(*gimp, window.hotkey_registry().commands());
  bool drove = false;
  when_dialog_visible(QStringLiteral("patchyPreferencesDialog"), [&](QDialog& dialog) {
    auto* panel = hotkey_panel(dialog);
    auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("hotkeySetCombo"));
    CHECK(combo != nullptr);
    CHECK(combo->count() == 4);
    CHECK(combo->currentData().toString() == QStringLiteral("patchy"));
    const int gimp_index = combo->findData(QStringLiteral("gimp"));
    CHECK(gimp_index > 0);
    combo->setCurrentIndex(gimp_index);
    emit combo->activated(gimp_index);
    process_events_until([&] { return !panel->staged_overrides().isEmpty(); });
    CHECK(same_overrides(panel->staged_overrides(), gimp_overrides));
    CHECK(combo->currentData().toString() == QStringLiteral("gimp"));
    drove = true;
    dialog.accept();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(drove);
  CHECK(same_overrides(window.hotkey_registry().overrides(), gimp_overrides));
  for (auto it = gimp_overrides.constBegin(); it != gimp_overrides.constEnd(); ++it) {
    const auto* command = window.hotkey_registry().find_command(it.key());
    CHECK(command != nullptr && command->action != nullptr);
    CHECK(patchy::ui::hotkey_shortcut_lists_equivalent(command->action->shortcuts(), it.value()));
  }

  drove = false;
  when_dialog_visible(QStringLiteral("patchyPreferencesDialog"), [&](QDialog& dialog) {
    auto* panel = hotkey_panel(dialog);
    auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("hotkeySetCombo"));
    CHECK(combo != nullptr);
    CHECK(combo->currentData().toString() == QStringLiteral("gimp"));
    // Photoshop-style straight from GIMP-style: no question, nothing hand-made is lost.
    panel->apply_shortcut_set(QStringLiteral("photoshop"));
    CHECK(combo->currentData().toString() == QStringLiteral("photoshop"));
    // Chips rebuilt on a shown page appear through a queued show.
    QApplication::processEvents();
    QApplication::processEvents();
    save_widget_artifact("hotkey_editor_shortcut_sets", dialog);
    panel->apply_shortcut_set(QStringLiteral("patchy"));
    CHECK(panel->staged_overrides().isEmpty());
    CHECK(combo->currentData().toString() == QStringLiteral("patchy"));
    CHECK(combo->count() == 4);
    drove = true;
    dialog.accept();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(drove);
  CHECK(window.hotkey_registry().overrides().isEmpty());
  CHECK(first_shortcut(window, "file.new") == QKeySequence(Qt::CTRL | Qt::Key_N));
}

// Export writes what the page shows; Import replaces it (unknown ids skipped, reported on
// the page) and OK applies it; Cancel keeps the shortcuts as they were.
void ui_hotkey_editor_exports_and_imports_files() {
  HotkeyGroupRestorer restore;
  auto&& [window, canvas] = ShownWindowFixture{};
  const auto dir = std::filesystem::absolute(patchy::test::unicode_artifact_dir(u8"hotkey-editor-files"));
  const auto import_path = patchy::ui::to_qstring(dir / "shared.patchykeys");
  const auto export_path = patchy::ui::to_qstring(dir / "exported.patchykeys");
  {
    QFile file(import_path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(R"({"format": "patchy-hotkeys", "version": 1, "shortcuts": {
        "file.new": ["Ctrl+F9"], "edit.undo": [], "tools.from_a_later_patchy": ["Ctrl+K"]}})");
  }

  // Cancel first: the import only stages.
  bool drove = false;
  when_dialog_visible(QStringLiteral("patchyPreferencesDialog"), [&](QDialog& dialog) {
    auto* panel = hotkey_panel(dialog);
    const auto result = panel->import_from_file(import_path);
    CHECK(result.ok);
    CHECK(result.skipped_commands == 1);
    drove = true;
    dialog.reject();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(drove);
  CHECK(window.hotkey_registry().overrides().isEmpty());
  CHECK(first_shortcut(window, "file.new") == QKeySequence(Qt::CTRL | Qt::Key_N));

  drove = false;
  when_dialog_visible(QStringLiteral("patchyPreferencesDialog"), [&](QDialog& dialog) {
    auto* panel = hotkey_panel(dialog);
    // Something staged beforehand is replaced, not merged.
    panel->apply_shortcut_set(QStringLiteral("krita"));
    const auto result = panel->import_from_file(import_path);
    CHECK(result.ok);
    CHECK(result.skipped_commands == 1);
    CHECK(panel->staged_overrides().size() == 2);
    auto* status = dialog.findChild<QLabel*>(QStringLiteral("hotkeyTransferStatus"));
    CHECK(status != nullptr && status->isVisible());
    CHECK(status->text().contains(QStringLiteral("shared.patchykeys")));
    auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("hotkeySetCombo"));
    CHECK(combo != nullptr);
    CHECK(combo->currentData().toString().isEmpty());  // Custom
    CHECK(combo->count() == 5);
    QString error;
    CHECK(panel->export_to_file(export_path, &error));
    drove = true;
    dialog.accept();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(drove);
  CHECK(first_shortcut(window, "file.new") == QKeySequence(Qt::CTRL | Qt::Key_F9));
  const auto* undo = window.hotkey_registry().find_command(QStringLiteral("edit.undo"));
  CHECK(undo != nullptr && undo->action != nullptr && undo->action->shortcuts().isEmpty());
  const auto exported = patchy::ui::read_hotkey_file(export_path, window.hotkey_registry().commands());
  CHECK(exported.ok);
  CHECK(same_overrides(exported.overrides, window.hotkey_registry().overrides()));

  // A file that is not a hotkey file leaves the page as it was.
  drove = false;
  when_dialog_visible(QStringLiteral("patchyPreferencesDialog"), [&](QDialog& dialog) {
    auto* panel = hotkey_panel(dialog);
    const auto before = panel->staged_overrides();
    {
      QFile file(import_path);
      CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("{\"format\": \"something else\"}");
    }
    const auto result = panel->import_from_file(import_path);
    CHECK(!result.ok);
    CHECK(same_overrides(panel->staged_overrides(), before));
    drove = true;
    dialog.reject();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(drove);
}

}  // namespace

std::vector<patchy::test::TestCase> hotkey_sets_tests() {
  return {
      {"ui_hotkey_file_round_trips_overrides", ui_hotkey_file_round_trips_overrides},
      {"ui_hotkey_file_rejects_malformed_files", ui_hotkey_file_rejects_malformed_files},
      {"ui_hotkey_file_writes_and_reads_unicode_paths", ui_hotkey_file_writes_and_reads_unicode_paths},
      {"ui_hotkey_sets_have_no_conflicts", ui_hotkey_sets_have_no_conflicts},
      {"ui_hotkey_editor_shortcut_set_combo_applies_set", ui_hotkey_editor_shortcut_set_combo_applies_set},
      {"ui_hotkey_editor_exports_and_imports_files", ui_hotkey_editor_exports_and_imports_files},
  };
}
