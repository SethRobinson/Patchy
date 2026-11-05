#pragma once

#include "ui/hotkey_files.hpp"
#include "ui/hotkey_registry.hpp"

#include <QHash>
#include <QString>
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
  // Replaces the staged set with the file's on success; leaves it alone on failure.
  HotkeyFileReadResult import_from_file(const QString& path);
  // "patchy" restores the defaults; otherwise a builtin_hotkey_sets() id.
  void apply_shortcut_set(const QString& set_id);
  [[nodiscard]] const HotkeyOverrideMap& staged_overrides() const noexcept { return staged_; }

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;
  void changeEvent(QEvent* event) override;

private:
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
  };

  QWidget* build_set_row(QWidget* parent);
  void export_clicked();
  void import_clicked();
  void shortcut_set_chosen(int index);
  void sync_set_combo();
  void retranslate_set_combo();
  void fit_set_combo_width();
  void show_transfer_status(const QString& text);
  void build_rows(QMenuBar* menu_bar);
  QFrame* add_category_panel(const QString& title, bool dimmed);
  Row& add_command_row(QFrame* panel, const HotkeyCommand* command, const QString& label);
  void add_built_in_row(QFrame* panel, const QString& keys, const QString& description);
  void refresh_rows();
  void filter_rows();
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
  QComboBox* set_combo_{nullptr};
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
