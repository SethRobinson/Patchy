#pragma once

#include "ui/hotkey_registry.hpp"

#include <QHash>
#include <QString>
#include <QWidget>
#include <optional>
#include <vector>

class QFrame;
class QHBoxLayout;
class QKeySequence;
class QLabel;
class QLineEdit;
class QMenuBar;
class QToolButton;

namespace patchy::ui {

class HotkeyCaptureEdit;

// The Hotkeys tab of the preferences dialog. Edits are staged locally and
// only reach the registry (settings + live actions) through commit().
class HotkeyEditorPanel final : public QWidget {
  Q_OBJECT

public:
  HotkeyEditorPanel(HotkeyRegistry& registry, QMenuBar* menu_bar, QWidget* parent = nullptr);

  void commit();

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
  HotkeyCaptureEdit* capture_edit_{nullptr};
  QFrame* conflict_banner_{nullptr};
  struct PendingCapture {
    QString id;
    int slot{-1};
  };
  std::optional<PendingCapture> capture_;
};

}  // namespace patchy::ui
