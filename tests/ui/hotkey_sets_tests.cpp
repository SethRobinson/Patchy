// Hotkey files (Export... / Import...) and the built-in shortcut sets on Preferences >
// Hotkeys (GitHub issue 633; docs/hotkey-sets.md).
#include "ui/app_settings.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/command_search.hpp"
#include "ui/gradient_library.hpp"
#include "ui/gradient_preset_popup.hpp"
#include "ui/hotkey_editor.hpp"
#include "ui/hotkey_files.hpp"
#include "ui/hotkey_registry.hpp"
#include "ui/hotkey_sets.hpp"
#include "ui/localization.hpp"
#include "ui/main_window.hpp"
#include "ui/qt_paths.hpp"
#include "ui/tool_modifier_style.hpp"
#include "test_harness.hpp"
#include "unicode_path_names.hpp"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QToolButton>
#include <QStyle>
#include <QStyleOptionComboBox>
#include <QSet>
#include <QStatusBar>
#include <QTest>
#include <QTimer>
#include <QVariant>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
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
    // OK on a GIMP-style or Krita-style set changes the canvas's tool modifiers too.
    patchy::ui::set_active_tool_modifier_style(tool_modifiers_);
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
  SettingsGroupRestorer sets_{QStringLiteral("hotkeySets")};
  patchy::ui::ToolModifierStyle tool_modifiers_{patchy::ui::active_tool_modifier_style()};
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
  // The page list shows the title in the current language.
  CHECK(select_preferences_page(dialog, QCoreApplication::translate("patchy::ui::MainWindow", "Hotkeys")));
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
    // Every shipped set says it is built in, so nobody expects to edit it in place.
    CHECK(combo->itemText(combo->findData(QStringLiteral("patchy"))) == QStringLiteral("Patchy default (built-in)"));
    CHECK(combo->itemText(combo->findData(QStringLiteral("photoshop"))) ==
          QStringLiteral("Photoshop-style (built-in)"));
    for (int index = 0; index < combo->count(); ++index) {
      CHECK(combo->itemText(index).endsWith(QStringLiteral(" (built-in)")));
    }
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

// The Shortcut set box leaves clear room around its widest entry ("Custom" included, which
// comes and goes) in English and in long-text languages, and the Import.../Export... buttons
// beside it keep their full width (GitHub issue 633 follow-up: the box barely fit its text).
void ui_hotkey_editor_shortcut_set_combo_has_room_for_its_entries() {
  HotkeyGroupRestorer restore;
  auto& manager = patchy::ui::LocalizationManager::instance();
  auto&& [window, canvas] = ShownWindowFixture{};
  for (const auto* language : {"en", "de", "fr", "ru"}) {
    CHECK(manager.set_language(QString::fromLatin1(language), false));
    QApplication::processEvents();
    bool drove = false;
    when_dialog_visible(QStringLiteral("patchyPreferencesDialog"), [&](QDialog& dialog) {
      (void)hotkey_panel(dialog);
      for (int pass = 0; pass < 4; ++pass) {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        QApplication::processEvents();
      }
      auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("hotkeySetCombo"));
      CHECK(combo != nullptr);
      const auto metrics = combo->fontMetrics();
      int widest = metrics.horizontalAdvance(
          QCoreApplication::translate("patchy::ui::HotkeyEditorPanel", "Custom"));
      for (int index = 0; index < combo->count(); ++index) {
        widest = std::max(widest, metrics.horizontalAdvance(combo->itemText(index)));
      }
      QStyleOptionComboBox option;
      option.initFrom(combo);
      option.rect = combo->rect();
      const auto field = combo->style()->subControlRect(QStyle::CC_ComboBox, &option,
                                                        QStyle::SC_ComboBoxEditField, combo);
      const int room = field.width() - widest;
      if (room < 2 * metrics.averageCharWidth()) {
        std::fprintf(stderr, "%s: the set box has %d px around its widest entry (%d px wide, field %d px)\n",
                     language, room, combo->width(), field.width());
      }
      CHECK(room >= 2 * metrics.averageCharWidth());
      // Nothing overlaps the box: the dialog is wide enough for the whole row.
      auto* rename = dialog.findChild<QPushButton*>(QStringLiteral("hotkeySetRenameButton"));
      CHECK(rename != nullptr);
      if (rename != nullptr && combo->geometry().right() >= rename->geometry().left()) {
        std::fprintf(stderr, "%s: the set box (right %d) runs under Rename... (left %d)\n", language,
                     combo->geometry().right(), rename->geometry().left());      }
      CHECK(rename != nullptr && combo->geometry().right() < rename->geometry().left());
      for (const auto* name :
           {"hotkeyImportButton", "hotkeyExportButton", "hotkeySetRenameButton", "hotkeySetDeleteButton"}) {
        auto* button = dialog.findChild<QPushButton*>(QString::fromLatin1(name));
        CHECK(button != nullptr);
        CHECK(button->width() >= button->sizeHint().width());
      }
      // The Show box beside the search (GitHub issue 676) shows its entry whole, and the search
      // row ends inside the page: Reset All is never cut off.
      auto* search = dialog.findChild<QLineEdit*>(QStringLiteral("hotkeySearchEdit"));
      auto* filter = dialog.findChild<QComboBox*>(QStringLiteral("hotkeyFilterCombo"));
      auto* reset_all = dialog.findChild<QPushButton*>(QStringLiteral("hotkeyResetAllButton"));
      auto* page_stack = dialog.findChild<QWidget*>(QStringLiteral("preferencesPageStack"));
      CHECK(search != nullptr && filter != nullptr && reset_all != nullptr && page_stack != nullptr);
      if (search != nullptr && filter != nullptr && reset_all != nullptr && page_stack != nullptr) {
        CHECK(filter->width() >= filter->sizeHint().width());
        CHECK(search->geometry().right() < filter->geometry().left());
        CHECK(filter->geometry().right() < reset_all->geometry().left());
        CHECK(reset_all->width() >= reset_all->sizeHint().width());
        const int reset_right = reset_all->mapTo(&dialog, QPoint(reset_all->width(), 0)).x();
        const int page_right = page_stack->mapTo(&dialog, QPoint(page_stack->width(), 0)).x();
        if (reset_right > page_right) {
          std::fprintf(stderr, "%s: Reset All ends at %d, past the page (%d)\n", language, reset_right, page_right);
        }
        CHECK(reset_right <= page_right);
      }
      if (std::string(language) == "de") {
        save_widget_artifact("hotkey_editor_shortcut_set_combo_de", dialog);
      }
      drove = true;
      dialog.reject();
    });
    require_action(window, "filePreferencesAction")->trigger();
    QApplication::processEvents();
    CHECK(drove);
  }
  CHECK(manager.set_language(QStringLiteral("en"), false));
  QApplication::processEvents();
}

// Export writes what the page shows; Import adds the file as a set of the user's own, named
// after the file (unknown ids skipped, reported on the page), and OK applies and stores it;
// Cancel keeps the shortcuts and sets as they were. Both remember the folder they used.
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
    CHECK(panel->user_shortcut_sets().size() == 1);
    drove = true;
    dialog.reject();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(drove);
  CHECK(window.hotkey_registry().overrides().isEmpty());
  CHECK(patchy::ui::load_user_hotkey_sets(window.hotkey_registry().commands()).empty());
  CHECK(QFileInfo(patchy::ui::hotkey_file_folder()) == QFileInfo(QFileInfo(import_path).absolutePath()));
  CHECK(first_shortcut(window, "file.new") == QKeySequence(Qt::CTRL | Qt::Key_N));

  drove = false;
  when_dialog_visible(QStringLiteral("patchyPreferencesDialog"), [&](QDialog& dialog) {
    auto* panel = hotkey_panel(dialog);
    // Something staged beforehand is replaced, not merged, and the built-in set keeps its keys.
    panel->apply_shortcut_set(QStringLiteral("krita"));
    const auto krita = panel->staged_overrides();
    const auto result = panel->import_from_file(import_path);
    CHECK(result.ok);
    CHECK(result.skipped_commands == 1);
    CHECK(panel->staged_overrides().size() == 2);
    auto* status = dialog.findChild<QLabel*>(QStringLiteral("hotkeyTransferStatus"));
    CHECK(status != nullptr && status->isVisible());
    CHECK(status->text().contains(QStringLiteral("shared.patchykeys")));
    auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("hotkeySetCombo"));
    CHECK(combo != nullptr);
    CHECK(combo->currentText() == QStringLiteral("shared"));
    CHECK(combo->currentData().toString() == panel->active_shortcut_set_id());
    CHECK(combo->count() == 6);  // four built-in sets, a separator, "shared"
    auto* rename = dialog.findChild<QPushButton*>(QStringLiteral("hotkeySetRenameButton"));
    auto* remove = dialog.findChild<QPushButton*>(QStringLiteral("hotkeySetDeleteButton"));
    CHECK(rename != nullptr && rename->isEnabled());
    CHECK(remove != nullptr && remove->isEnabled());
    // A second import of the same file gets a name of its own.
    CHECK(panel->import_from_file(import_path).ok);
    CHECK(combo->currentText() == QStringLiteral("shared 2"));
    panel->delete_shortcut_set(panel->active_shortcut_set_id());
    CHECK(panel->active_shortcut_set_id() == QStringLiteral("patchy"));
    CHECK(!rename->isEnabled() && !remove->isEnabled());
    panel->apply_shortcut_set(QStringLiteral("krita"));
    CHECK(same_overrides(panel->staged_overrides(), krita));
    panel->apply_shortcut_set(panel->user_shortcut_sets().front().id);
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

  // A file that is not a hotkey file leaves the page as it was; the stored set opens selected.
  drove = false;
  when_dialog_visible(QStringLiteral("patchyPreferencesDialog"), [&](QDialog& dialog) {
    auto* panel = hotkey_panel(dialog);
    CHECK(panel->user_shortcut_sets().size() == 1);
    CHECK(panel->active_shortcut_set_id() == panel->user_shortcut_sets().front().id);
    CHECK(panel->user_shortcut_sets().front().name == QStringLiteral("shared"));
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

// Built-in sets never change (Seth, November 5, 2026): changing a key in one moves the keys
// into the Custom set, the built-in set keeps its own, and a second edit of a built-in set
// asks before replacing what Custom holds. Rename names Custom (then a set like any import),
// Delete removes a set of the user's own, and both reach the stored sets on OK.
void ui_hotkey_editor_built_in_sets_stay_unchanged() {
  HotkeyGroupRestorer restore;
  auto&& [window, canvas] = ShownWindowFixture{};
  const auto& commands = window.hotkey_registry().commands();
  const auto photoshop =
      patchy::ui::hotkey_set_overrides(*patchy::ui::find_builtin_hotkey_set(u"photoshop"), commands);
  const auto gimp = patchy::ui::hotkey_set_overrides(*patchy::ui::find_builtin_hotkey_set(u"gimp"), commands);
  CHECK(!photoshop.isEmpty() && !gimp.isEmpty());
  // An edit through the page: the row's reset button puts one command back to Patchy's key.
  const auto reset_row = [](QDialog& dialog, const QString& id) {
    auto* reset = dialog.findChild<QToolButton*>(QStringLiteral("hotkeyRowReset.") + id);
    CHECK(reset != nullptr);
    if (reset != nullptr) {
      reset->click();
    }
    process_events_for(30);
  };
  HotkeyOverrideMap mine;
  bool drove = false;
  when_dialog_visible(QStringLiteral("patchyPreferencesDialog"), [&](QDialog& dialog) {
    auto* panel = hotkey_panel(dialog);
    auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("hotkeySetCombo"));
    CHECK(combo != nullptr);
    panel->apply_shortcut_set(QStringLiteral("photoshop"));
    reset_row(dialog, photoshop.constBegin().key());
    CHECK(panel->active_shortcut_set_id() == QStringLiteral("custom"));
    CHECK(combo->currentText() == QStringLiteral("Custom"));
    CHECK(panel->staged_overrides().size() == photoshop.size() - 1);
    mine = panel->staged_overrides();
    // The built-in set kept its keys, and the same edit again needs no question.
    panel->apply_shortcut_set(QStringLiteral("photoshop"));
    CHECK(same_overrides(panel->staged_overrides(), photoshop));
    reset_row(dialog, photoshop.constBegin().key());
    CHECK(panel->active_shortcut_set_id() == QStringLiteral("custom"));
    // A different edit of another built-in set asks first; Cancel undoes the edit.
    panel->apply_shortcut_set(QStringLiteral("gimp"));
    bool asked = false;
    when_dialog_visible(QStringLiteral("hotkeyCustomReplaceMessageBox"), [&](QDialog& box) {
      asked = true;
      auto* message = qobject_cast<QMessageBox*>(&box);
      auto* cancel = message != nullptr ? message->button(QMessageBox::Cancel) : nullptr;
      CHECK(cancel != nullptr);
      if (cancel != nullptr) {
        cancel->click();
      }
    });
    reset_row(dialog, gimp.constBegin().key());
    process_events_until([&] { return asked; });
    process_events_for(30);
    CHECK(asked);
    CHECK(panel->active_shortcut_set_id() == QStringLiteral("gimp"));
    CHECK(same_overrides(panel->staged_overrides(), gimp));
    // Rename: names that read like another set's are refused, in any letter case.
    CHECK(!panel->rename_shortcut_set(QStringLiteral("custom"), QStringLiteral("photoshop-STYLE")));
    CHECK(!panel->rename_shortcut_set(QStringLiteral("custom"), QStringLiteral("  ")));
    CHECK(panel->rename_shortcut_set(QStringLiteral("custom"), QStringLiteral("Mine")));
    CHECK(panel->user_shortcut_sets().size() == 1);
    CHECK(panel->user_shortcut_sets().front().id != QStringLiteral("custom"));
    panel->apply_shortcut_set(panel->user_shortcut_sets().front().id);
    CHECK(combo->currentText() == QStringLiteral("Mine"));
    CHECK(same_overrides(panel->staged_overrides(), mine));
    save_widget_artifact("hotkey_editor_own_set", dialog);
    drove = true;
    dialog.accept();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(drove);
  CHECK(same_overrides(window.hotkey_registry().overrides(), mine));

  // Stored: the page opens on "Mine"; Delete falls back to Patchy's defaults.
  drove = false;
  when_dialog_visible(QStringLiteral("patchyPreferencesDialog"), [&](QDialog& dialog) {
    auto* panel = hotkey_panel(dialog);
    CHECK(panel->user_shortcut_sets().size() == 1);
    CHECK(panel->active_shortcut_set_id() == panel->user_shortcut_sets().front().id);
    CHECK(panel->user_shortcut_sets().front().name == QStringLiteral("Mine"));
    panel->delete_shortcut_set(panel->active_shortcut_set_id());
    CHECK(panel->active_shortcut_set_id() == QStringLiteral("patchy"));
    CHECK(panel->staged_overrides().isEmpty());
    drove = true;
    dialog.accept();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(drove);
  CHECK(window.hotkey_registry().overrides().isEmpty());
  CHECK(patchy::ui::load_user_hotkey_sets(commands).empty());
}

// First Preset / Last Preset (GitHub issue 645): < and > (Shift+, and Shift+.) pick the ends of
// the list , and . step through: the brush Preset combo, the gradient presets for the Gradient
// tool. Tools without a preset list ignore them.
void ui_preset_first_last_shortcuts_pick_list_ends() {
  HotkeyGroupRestorer restore;
  SettingsValueRestorer saved_brush_preset(QStringLiteral("tools/brushPreset"));
  SettingsValueRestorer saved_gradient_use_custom(QStringLiteral("tools/gradientUseCustomStops"));
  SettingsValueRestorer saved_gradient_stops(QStringLiteral("tools/gradientStops"));
  patchy::ui::MainWindow window;
  window.add_document_session(patchy::Document(64, 48, patchy::PixelFormat::rgba8()), QStringLiteral("Presets"));
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* first = require_hotkey_action(window, QStringLiteral("brush.preset_first"));
  auto* last = require_hotkey_action(window, QStringLiteral("brush.preset_last"));
  CHECK(first->shortcut() == QKeySequence(Qt::SHIFT | Qt::Key_Comma));
  CHECK(last->shortcut() == QKeySequence(Qt::SHIFT | Qt::Key_Period));

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  QApplication::processEvents();
  auto* combo = window.findChild<QComboBox*>(QStringLiteral("brushPresetCombo"));
  CHECK(combo != nullptr);
  QList<int> presets;
  for (int index = 0; index < combo->count(); ++index) {
    const auto id = combo->itemData(index).toString();
    if (!id.isEmpty() && !id.startsWith(QStringLiteral("__"))) {
      presets << index;
    }
  }
  CHECK(presets.size() >= 3);
  combo->setCurrentIndex(presets[1]);
  QApplication::processEvents();
  last->trigger();  // past the separator and the Save/Manage commands
  CHECK(combo->currentIndex() == presets.back());
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Brush preset: ") + combo->itemText(presets.back()));
  last->trigger();  // stays there, no wrap
  CHECK(combo->currentIndex() == presets.back());
  first->trigger();
  CHECK(combo->currentIndex() == presets.front());
  CHECK(combo->property("lastBrushPresetId") == combo->itemData(presets.front()));

  // The keys on the canvas.
  canvas->setFocus();
  QApplication::processEvents();
  QTest::keyClick(canvas, Qt::Key_Period, Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(combo->currentIndex() == presets.back());
  QTest::keyClick(canvas, Qt::Key_Comma, Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(combo->currentIndex() == presets.front());

  // The Move tool has no preset list.
  require_action(window, "toolMoveAction")->trigger();
  QApplication::processEvents();
  last->trigger();
  CHECK(combo->currentIndex() == presets.front());

  // Gradient: the popup's order.
  require_action_by_text(window, QStringLiteral("Gradient"))->trigger();
  QApplication::processEvents();
  auto& library = window.gradient_library();
  library.restore_default_gradients();  // suite-order independence: repair any deleted builtins
  const auto order = patchy::ui::gradient_preset_popup_order(library);
  CHECK(order.size() >= 3);
  const auto expect_entry = [&](const QString& storage_id) {
    const auto* entry = library.find_entry(storage_id);
    CHECK(entry != nullptr);
    CHECK(canvas->gradient_stops().has_value());
    CHECK(window.statusBar()->currentMessage() ==
          QStringLiteral("Gradient preset: ") + patchy::ui::gradient_library_entry_display_name(*entry));
  };
  last->trigger();
  expect_entry(order.back());
  first->trigger();
  expect_entry(order.front());
}

// Keyboard Shortcuts (GitHub issue 645): Ctrl+Alt+Shift+K opens Preferences on its Hotkeys
// page. No menu row; Search Commands lists it.
void ui_keyboard_shortcuts_command_opens_hotkeys_page() {
  HotkeyGroupRestorer restore;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* action = require_hotkey_action(window, QStringLiteral("edit.keyboard_shortcuts"));
  CHECK(action->shortcut() == QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_K));
  CHECK(action->isEnabled());  // no document needed
  for (auto* menu : window.menuBar()->findChildren<QMenu*>()) {
    CHECK(!menu->actions().contains(action));
  }
  const auto entries = patchy::ui::collect_search_commands(window.menuBar(), window.hotkey_registry(), true);
  CHECK(std::any_of(entries.begin(), entries.end(), [](const patchy::ui::MenuCommandEntry& entry) {
    return entry.id == QStringLiteral("edit.keyboard_shortcuts") && entry.name == QStringLiteral("Keyboard Shortcuts") &&
           !entry.description.isEmpty();
  }));

  bool saw_dialog = false;
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    wait_for_preferences_start_page(*dialog);
    auto* list = preferences_page_list(*dialog);
    CHECK(list != nullptr && list->currentItem() != nullptr);
    if (list != nullptr && list->currentItem() != nullptr) {
      CHECK(list->currentItem()->text() == QStringLiteral("Hotkeys"));
    }
    auto* panel = dialog->findChild<patchy::ui::HotkeyEditorPanel*>(QStringLiteral("hotkeyEditorPanel"));
    CHECK(panel != nullptr && panel->isVisible());
    CHECK(dialog->findChild<QWidget*>(QStringLiteral("hotkeyRow.edit.keyboard_shortcuts")) != nullptr);
    saw_dialog = true;
    dialog->reject();
  }));
  action->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);
}

// A set carries its tool modifiers (GitHub issue 302): OK makes the canvas use the style of
// the set chosen and stores it, Custom made from GIMP-style keeps GIMP's, and a hotkey file
// carries it to the set an import makes. The page shows no line for it (Seth, November 8, 2026).
void ui_hotkey_editor_set_applies_tool_modifiers() {
  HotkeyGroupRestorer restore;
  auto&& [window, canvas] = ShownWindowFixture{};
  using patchy::ui::ToolModifierStyle;
  CHECK(patchy::ui::active_tool_modifier_style() == ToolModifierStyle::Patchy);
  const auto dir = std::filesystem::absolute(patchy::test::unicode_artifact_dir(u8"hotkey-tool-modifiers"));
  const auto export_path = patchy::ui::to_qstring(dir / "gimp-keys.patchykeys");
  const auto gimp = patchy::ui::hotkey_set_overrides(*patchy::ui::find_builtin_hotkey_set(u"gimp"),
                                                     window.hotkey_registry().commands());
  bool drove = false;
  when_dialog_visible(QStringLiteral("patchyPreferencesDialog"), [&](QDialog& dialog) {
    auto* panel = hotkey_panel(dialog);
    CHECK(dialog.findChild<QLabel*>(QStringLiteral("hotkeyToolModifiersLabel")) == nullptr);
    CHECK(panel->staged_tool_modifier_style() == ToolModifierStyle::Patchy);
    panel->apply_shortcut_set(QStringLiteral("krita"));
    CHECK(panel->staged_tool_modifier_style() == ToolModifierStyle::Krita);
    panel->apply_shortcut_set(QStringLiteral("photoshop"));
    CHECK(panel->staged_tool_modifier_style() == ToolModifierStyle::Patchy);
    panel->apply_shortcut_set(QStringLiteral("gimp"));
    CHECK(panel->staged_tool_modifier_style() == ToolModifierStyle::Gimp);
    // Nothing reaches the canvas before OK.
    CHECK(patchy::ui::active_tool_modifier_style() == ToolModifierStyle::Patchy);
    QString error;
    CHECK(panel->export_to_file(export_path, &error));
    // A hand edit of GIMP-style lands in Custom, which keeps GIMP's tool modifiers.
    auto* reset = dialog.findChild<QToolButton*>(QStringLiteral("hotkeyRowReset.") + gimp.constBegin().key());
    CHECK(reset != nullptr);
    if (reset != nullptr) {
      reset->click();
    }
    process_events_for(30);
    CHECK(panel->active_shortcut_set_id() == QStringLiteral("custom"));
    CHECK(panel->staged_tool_modifier_style() == ToolModifierStyle::Gimp);
    drove = true;
    dialog.accept();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(drove);
  CHECK(patchy::ui::active_tool_modifier_style() == ToolModifierStyle::Gimp);
  CHECK(patchy::ui::app_settings().value(QStringLiteral("hotkeySets/toolModifiers")).toString() ==
        QStringLiteral("gimp"));
  CHECK(patchy::ui::load_active_tool_modifier_style() == ToolModifierStyle::Gimp);
  const auto stored = patchy::ui::load_user_hotkey_sets(window.hotkey_registry().commands());
  CHECK(stored.size() == 1 && stored.front().tool_modifiers == ToolModifierStyle::Gimp);

  // The exported file names the style; a file without the key reads as Patchy's.
  {
    QFile file(export_path);
    CHECK(file.open(QIODevice::ReadOnly));
    const auto root = QJsonDocument::fromJson(file.readAll()).object();
    CHECK(root.value(QStringLiteral("toolModifiers")).toString() == QStringLiteral("gimp"));
  }
  CHECK(patchy::ui::hotkey_overrides_from_json(patchy::ui::hotkey_overrides_to_json({}),
                                               window.hotkey_registry().commands())
            .tool_modifiers == ToolModifierStyle::Patchy);
  CHECK(!patchy::ui::hotkey_overrides_to_json({}).contains("toolModifiers"));

  // Importing it makes a set with GIMP's tool modifiers; Patchy default puts Alt back on OK.
  drove = false;
  when_dialog_visible(QStringLiteral("patchyPreferencesDialog"), [&](QDialog& dialog) {
    auto* panel = hotkey_panel(dialog);
    panel->apply_shortcut_set(QStringLiteral("patchy"));
    CHECK(panel->staged_tool_modifier_style() == ToolModifierStyle::Patchy);
    const auto result = panel->import_from_file(export_path, QStringLiteral("From GIMP file"));
    CHECK(result.ok);
    CHECK(result.tool_modifiers == ToolModifierStyle::Gimp);
    CHECK(panel->staged_tool_modifier_style() == ToolModifierStyle::Gimp);
    panel->apply_shortcut_set(QStringLiteral("patchy"));
    drove = true;
    dialog.accept();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(drove);
  CHECK(patchy::ui::active_tool_modifier_style() == ToolModifierStyle::Patchy);
  CHECK(patchy::ui::app_settings().value(QStringLiteral("hotkeySets/toolModifiers")).toString() ==
        QStringLiteral("patchy"));
}

}  // namespace

std::vector<patchy::test::TestCase> hotkey_sets_tests() {
  return {
      {"ui_hotkey_file_round_trips_overrides", ui_hotkey_file_round_trips_overrides},
      {"ui_hotkey_file_rejects_malformed_files", ui_hotkey_file_rejects_malformed_files},
      {"ui_hotkey_file_writes_and_reads_unicode_paths", ui_hotkey_file_writes_and_reads_unicode_paths},
      {"ui_hotkey_sets_have_no_conflicts", ui_hotkey_sets_have_no_conflicts},
      {"ui_preset_first_last_shortcuts_pick_list_ends", ui_preset_first_last_shortcuts_pick_list_ends},
      {"ui_keyboard_shortcuts_command_opens_hotkeys_page", ui_keyboard_shortcuts_command_opens_hotkeys_page},
      {"ui_hotkey_editor_shortcut_set_combo_applies_set", ui_hotkey_editor_shortcut_set_combo_applies_set},
      {"ui_hotkey_editor_exports_and_imports_files", ui_hotkey_editor_exports_and_imports_files},
      {"ui_hotkey_editor_built_in_sets_stay_unchanged", ui_hotkey_editor_built_in_sets_stay_unchanged},
      {"ui_hotkey_editor_shortcut_set_combo_has_room_for_its_entries",
       ui_hotkey_editor_shortcut_set_combo_has_room_for_its_entries},
      {"ui_hotkey_editor_set_applies_tool_modifiers", ui_hotkey_editor_set_applies_tool_modifiers},
  };
}
