#pragma once

#include "ui/hotkey_files.hpp"
#include "ui/hotkey_registry.hpp"
#include "ui/hotkey_sets.hpp"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <optional>
#include <vector>

class QComboBox;
class QFrame;
class QHBoxLayout;
class QKeySequence;
class QLabel;
class QLineEdit;
class QMenuBar;
class QPushButton;
class QScrollArea;
class QVBoxLayout;
class QToolButton;

namespace patchy::ui {

class HotkeyCaptureEdit;

// Keys the canvas (or the editor dialog itself) consumes positionally; binding them
// app-wide would break core interactions, so the capture field refuses them and a
// script's @hotkey directive naming one is ignored.
[[nodiscard]] bool is_reserved_binding_key(int key, Qt::KeyboardModifiers modifiers);

// The Hotkeys tab of the preferences dialog. Edits are staged locally and
// only reach the registry (settings + live actions) through commit().
class HotkeyEditorPanel final : public QWidget {
  Q_OBJECT

public:
  HotkeyEditorPanel(HotkeyRegistry& registry, QMenuBar* menu_bar, QWidget* parent = nullptr);

  void commit();
  // Fills the search field (Script Manager's "Assign Hotkey..." lands on the script's row).
  void set_search_text(const QString& text);

  // What Export..., Import... and the "Shortcut set" combo run once the user has picked a
  // file or confirmed (tests and automation call these directly). Like every edit on the
  // page, an import or a set only stages: OK applies it.
  [[nodiscard]] bool export_to_file(const QString& path, QString* error);
  // Adds the file's shortcuts as a set of the user's own named `set_name` (empty: the file's
  // name, made unique) and selects it; changes nothing on failure.
  HotkeyFileReadResult import_from_file(const QString& path, const QString& set_name = QString());
  // kPatchyHotkeySetId, a builtin_hotkey_sets() id or a user set id.
  void apply_shortcut_set(const QString& set_id);
  // Rename... and Delete for the user's own sets. Renaming Custom makes it a named set. False
  // when the name is empty or another set has it (ignoring letter case).
  bool rename_shortcut_set(const QString& set_id, const QString& name);
  void delete_shortcut_set(const QString& set_id);
  [[nodiscard]] const HotkeyOverrideMap& staged_overrides() const noexcept { return staged_; }
  [[nodiscard]] const QString& active_shortcut_set_id() const noexcept { return active_set_id_; }
  [[nodiscard]] const std::vector<UserHotkeySet>& user_shortcut_sets() const noexcept { return user_sets_; }
  // The tool modifiers of the set shown, which OK makes the canvas use (GitHub issue 302).
  [[nodiscard]] ToolModifierStyle staged_tool_modifier_style() const;

  // What the "Show" combo limits the list to (GitHub issue 676). Not saved between sessions.
  enum class ListFilter { All, MenuCommands, WithShortcuts, Tools, OtherCommands };

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;
  void changeEvent(QEvent* event) override;

private:
  // Where a row came from: the menu bar, the Tools category or another category outside the
  // menus, or the fixed built-in canvas keys.
  enum class RowSource { Menu, Tools, Other, BuiltIn };

  struct Row {
    const HotkeyCommand* command{nullptr};  // nullptr for built-in reference rows
    QWidget* container{nullptr};
    QLabel* name_label{nullptr};
    QWidget* chips_host{nullptr};
    QHBoxLayout* chips_layout{nullptr};
    QToolButton* reset_button{nullptr};
    QLabel* note_label{nullptr};
    QFrame* panel{nullptr};
    QString label;
    QString search_haystack;
    RowSource source{RowSource::BuiltIn};
    // The row's keys for the shortcut-only search: one token list per chord (GitHub issue 676).
    QList<QStringList> key_chords;
  };

  QWidget* build_set_row(QWidget* parent);
  void export_clicked();
  void import_clicked();
  void rename_clicked();
  void delete_clicked();
  void shortcut_set_chosen(int index);
  void load_shortcut_sets();
  void sync_set_combo();
  void note_staged_change();
  void rebuild_set_combo();
  void put_staged_in_custom_set();
  [[nodiscard]] ToolModifierStyle set_tool_modifier_style(const QString& set_id) const;
  [[nodiscard]] std::optional<QString> ask_set_name(const QString& title, const QString& initial,
                                                    const QString& except_id);
  // A built-in set's name without the "(built-in)" the combo adds; empty for other ids.
  [[nodiscard]] QString shortcut_set_name(const QString& set_id) const;
  [[nodiscard]] QString user_set_display_name(const UserHotkeySet& set) const;
  [[nodiscard]] bool is_builtin_set(const QString& set_id) const;
  [[nodiscard]] HotkeyOverrideMap builtin_set_overrides(const QString& set_id) const;
  [[nodiscard]] UserHotkeySet* find_user_set(const QString& set_id);
  [[nodiscard]] QString next_user_set_id() const;
  [[nodiscard]] bool set_name_taken(const QString& name, const QString& except_id) const;
  [[nodiscard]] QString unique_set_name(const QString& base) const;
  void fit_set_combo_width();
  void show_transfer_status(const QString& text);
  void build_rows(QMenuBar* menu_bar);
  QFrame* add_category_panel(const QString& title, bool dimmed);
  Row& add_command_row(QFrame* panel, const HotkeyCommand* command, const QString& label);
  void add_built_in_row(QFrame* panel, const QString& keys, const QString& description);
  void refresh_rows();
  void filter_rows();
  void fill_filter_combo();
  void update_search_placeholder();
  [[nodiscard]] ListFilter list_filter() const;
  [[nodiscard]] bool row_matches_filter(const Row& row, ListFilter filter) const;
  void begin_capture(const QString& id, int slot);
  void end_capture_widget();
  void handle_capture_commit(QKeySequence sequence);
  void handle_capture_cancel();
  void handle_capture_clear();
  void assign_sequence(const QString& id, int slot, const QKeySequence& sequence);
  void show_conflict_banner(Row& row, const QString& id, int slot, const QKeySequence& sequence,
                            const QString& owner_id);
  void remove_conflict_banner();
  void stage_override(const QString& id, QList<QKeySequence> shortcuts);
  [[nodiscard]] Row* find_row(const QString& id);
  [[nodiscard]] QString display_label(const QString& id) const;
  [[nodiscard]] QString category_display_name(const QString& category_key) const;

  HotkeyRegistry& registry_;
  HotkeyOverrideMap staged_;
  HotkeyResolution resolution_;
  std::vector<Row> rows_;
  QHash<QFrame*, QString> panel_titles_;
  QLineEdit* search_edit_{nullptr};
  QComboBox* filter_combo_{nullptr};
  // The user's own sets and the set shown, staged like the keys: OK stores them.
  std::vector<UserHotkeySet> user_sets_;
  QString active_set_id_;
  QComboBox* set_combo_{nullptr};
  QPushButton* rename_set_button_{nullptr};
  QPushButton* delete_set_button_{nullptr};
  QLabel* transfer_status_{nullptr};
  QWidget* header_{nullptr};
  QScrollArea* list_scroll_{nullptr};
  QVBoxLayout* list_layout_{nullptr};
  HotkeyCaptureEdit* capture_edit_{nullptr};
  QFrame* conflict_banner_{nullptr};
  struct PendingCapture {
    QString id;
    int slot{-1};
  };
  std::optional<PendingCapture> capture_;
};

}  // namespace patchy::ui
