#pragma once

#include "ui/preset_library.hpp"

#include <QElapsedTimer>
#include <QString>
#include <QStringList>

#include <functional>
#include <initializer_list>
#include <memory>

class QDialog;
class QHBoxLayout;
class QProgressDialog;
class QPushButton;
class QWidget;
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
  // (stretch 2). Must be called before any right-column helper. The Shapes
  // Manager passes a container holding its List/Grid PresetBrowserView.
  void add_tree(QWidget* tree, int minimum_width);
  // The same, with `above` (a filter field) over the tree in the left column; the selection
  // and Delete plumbing still bind to the tree alone, so Delete in the field edits text.
  void add_tree(QWidget* tree, int minimum_width, QWidget* above);
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

  // QTreeWidget trees only; other browsers report selection changes themselves.
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
  QWidget* tree_{nullptr};
};

// The progress dialog a manager shows while it imports one preset file (GitHub issue
// 741): a large .asl or .pat takes seconds to read, decode and store, all on the GUI
// thread. It shows at once for a file of kShowAtOnceBytes or more, because reading and
// decoding that blocks before the first item; otherwise only once the import has run
// for kShowAfterMs, so a small import never flashes it. Cancel stops between two items
// and keeps what was stored. The texts come from the calling dialog, like the
// scaffold's.
class PresetImportProgressDialog {
public:
  static constexpr qint64 kShowAtOnceBytes = 8LL * 1024LL * 1024LL;
  static constexpr qint64 kShowAfterMs = 400;

  // `step_label(item, total)` names the item about to be stored, counted from 1.
  PresetImportProgressDialog(QWidget* parent, const QString& object_name, const QString& title,
                             const QString& reading_label, const QString& cancel_label,
                             std::function<QString(int item, int total)> step_label,
                             qint64 file_bytes);
  ~PresetImportProgressDialog();
  PresetImportProgressDialog(const PresetImportProgressDialog&) = delete;
  PresetImportProgressDialog& operator=(const PresetImportProgressDialog&) = delete;

  // The callback for the library's import; valid while this object lives.
  [[nodiscard]] PresetImportStep step();
  [[nodiscard]] bool cancelled() const;

private:
  void show_now();

  std::unique_ptr<QProgressDialog> dialog_;
  std::function<QString(int, int)> step_label_;
  QElapsedTimer running_;
  qint64 file_bytes_{0};
};

}  // namespace patchy::ui
