#pragma once

#include <QCollator>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <vector>

class QListWidget;
class QPushButton;
class QWidget;

namespace patchy::ui {

// The ordered, multi-selectable list of open documents that the Export Multi-Page
// PDF and Export Documents to Folder dialogs share: one row per session, picked with
// the standard selection gestures (click, Shift-click, Ctrl-click) like the Import PDF
// page list, and Select All / Move Up / Move Down / Auto Sort / Reverse beside it. The
// dialogs own the semantics (what the selected order means); this owns the widgets
// and the sort.

// One open document as the list shows it.
struct DocumentOrderEntry {
  QString title;
  std::int64_t session_id{0};
};

struct DocumentOrderControls {
  QWidget* row{nullptr};  // list plus the button column, ready to add to a layout
  QListWidget* list{nullptr};
  QPushButton* select_all{nullptr};
  QPushButton* move_up{nullptr};
  QPushButton* move_down{nullptr};
  QPushButton* auto_sort{nullptr};
  QPushButton* reverse{nullptr};
};

// Numeric-aware, case-insensitive: "Page 2" sorts before "Page 10". Shared with the
// image-sequence file ordering (sorted_sequence_paths).
[[nodiscard]] QCollator natural_name_collator();

// Stable natural sort of titles (ties keep their input order).
[[nodiscard]] QStringList natural_sorted_titles(QStringList titles);

// Builds the control. Object names are `object_prefix` + "DocumentsList",
// "SelectAllButton", "MoveUpButton", "MoveDownButton", "AutoSortButton",
// "ReverseButton". Every row starts selected with its session id in Qt::UserRole; the
// active session's row is the current item. The Move buttons shift every selected row
// by one (a run already against the edge stays put); Auto Sort and Reverse reorder
// every row (selection travels with the rows). Each click and selection change
// re-syncs the buttons' enabled state; a dialog that shows a summary connects its own
// refresh to the buttons' `clicked` and the list's `itemSelectionChanged` as well.
[[nodiscard]] DocumentOrderControls build_document_order_controls(QWidget* parent, const QString& object_prefix,
                                                                  const std::vector<DocumentOrderEntry>& entries,
                                                                  std::int64_t active_session_id);

// Session ids of the selected rows, top to bottom.
[[nodiscard]] std::vector<std::int64_t> selected_session_ids(const QListWidget& list);

// Enables the list and buttons as a unit (false grays everything); with true, the
// Move buttons follow the selection (no Move Up while the top row is selected and
// nothing selected can move, and so on).
void sync_document_order_controls(const DocumentOrderControls& controls, bool enabled);

// The reorder operations, exposed for tests: rows move as whole items so selection,
// the current item, and user data travel with them.
void move_selected_list_items(QListWidget& list, int delta);
void sort_list_items_naturally(QListWidget& list);
void reverse_list_items(QListWidget& list);

}  // namespace patchy::ui
