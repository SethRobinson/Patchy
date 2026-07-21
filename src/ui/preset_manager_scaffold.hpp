#pragma once

#include <QString>
#include <QStringList>

#include <functional>
#include <initializer_list>

class QDialog;
class QHBoxLayout;
class QPushButton;
class QTreeWidget;
class QVBoxLayout;

namespace patchy::ui {

// Shared skeleton for the modal preset manager dialogs (Style, Pattern, and
// Brush Tip managers): tree on the left, a details/preview column on the
// right, the {leading..., stretch, Duplicate/Delete} action row, the
// Close + Use button box, and the Delete-key/selection plumbing. Every button,
// label, tooltip, and objectName is created by the calling dialog so the
// translation contexts and test-visible names stay in the per-dialog
// translation units. The gradient manager keeps its own layout (dark chrome,
// bottom-spanning action row) and shares only button_row().
class PresetManagerScaffold {
public:
  // Applies the object name, window title, and modality, and installs the
  // two-column layout on the dialog.
  PresetManagerScaffold(QDialog& dialog, const QString& object_name, const QString& title);

  // Adds the tree (stretch 1) and creates the right-hand details column
  // (stretch 2). Must be called before any right-column helper.
  void add_tree(QTreeWidget* tree, int minimum_width);
  [[nodiscard]] QVBoxLayout* right() const { return right_; }

  // {leading..., stretch, trailing...}; the caller attaches the returned row.
  [[nodiscard]] static QHBoxLayout* button_row(std::initializer_list<QPushButton*> leading,
                                               std::initializer_list<QPushButton*> trailing);
  void add_action_row(std::initializer_list<QPushButton*> leading,
                      std::initializer_list<QPushButton*> trailing);
  void add_restore_button_left(QPushButton* button);

  // Close + AcceptRole use button; Close rejects, the use button runs use.
  QPushButton* add_dialog_buttons(const QString& use_label, const QString& use_object_name,
                                  std::function<void()> use);

  void connect_selection_changed(std::function<void()> refresh);
  // The delete button and a Del shortcut scoped to the tree both run delete_selected.
  void add_delete_plumbing(QPushButton* delete_button, std::function<void()> delete_selected);

  // The style/pattern managers' diff-identical Use behavior: exactly one
  // selected entry that exists in the library accepts the dialog and stores
  // its id in chosen_id.
  [[nodiscard]] std::function<void()> single_selection_accept(
      std::function<QStringList()> selected_ids,
      std::function<bool(const QString& id)> library_has_entry, QString& chosen_id) const;

private:
  QDialog& dialog_;
  QHBoxLayout* main_layout_{nullptr};
  QVBoxLayout* right_{nullptr};
  QTreeWidget* tree_{nullptr};
};

}  // namespace patchy::ui
