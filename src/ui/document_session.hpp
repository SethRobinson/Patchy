#pragma once

// One open document and everything the window tracks about it: history, save state,
// view/canvas binding, float-window placement, and Smart Object parentage. Owned by
// MainWindow's session list (docs/code-organization.md, "Session lifetime"); other
// code refers to a session by `session_id`, never by pointer, because the list erases
// on tab close.

#include "core/document.hpp"
#include "core/layer.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/image_document_io.hpp"

#include <QString>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace patchy::ui {

class DocumentFloatWindow;

struct DocumentSession {
  struct HistoryState {
    Document document;
    std::int64_t revision{0};
    // Selection state at this point in history, so undo/redo restores the
    // selection alongside the pixels (and selection-only edits are undoable).
    CanvasWidget::SelectionSnapshot selection;
    // Action that produced this state (History panel row text). The label a
    // push receives names the upcoming edit, so it becomes the label of the
    // NEXT state, not of the snapshot being stored.
    QString label;
    // Monotonic per-session row identity; panel rows reference states by id
    // because cap eviction from the stack front shifts vector indices.
    std::int64_t state_id{0};
  };

  Document document;
  QString title;
  QString path;
  // Added in the background (SessionActivation::Background): its canvas has never been
  // shown, so it has no real size to fit to yet. The first activation fits it.
  bool fit_view_on_first_activation{false};
  std::optional<ImageSaveOptions> image_save_options;
  QString image_save_options_path;
  QString image_save_options_extension;
  // Stable identity for cross-session references (ids, not pointers: the session list
  // erases on tab close, so pointers into it must never be stored).
  std::int64_t session_id{0};
  // Present on an Edit Smart Object Contents child tab: which session and source
  // uuid a Save commits back into. `external` marks a linked-file child (a normal
  // disk-backed session whose Save writes the file first, then refreshes the
  // parent's previews).
  struct SmartObjectLink {
    std::int64_t parent_session_id{0};
    std::string source_uuid;
    bool external{false};
    // Edit Contents regenerates PSD UUIDs. Retain the lineage so parent
    // history navigation can reconnect the open child to a restored source.
    std::vector<std::string> source_uuid_history{};
  };
  std::optional<SmartObjectLink> smart_object_link;
  CanvasWidget* canvas{nullptr};
  // Non-null while the document is floated in its own top-level window; the
  // canvas lives inside it instead of the tab widget. The window is a
  // MainWindow child (widget tree owns it); the session only points at it.
  DocumentFloatWindow* float_window{nullptr};
  // Tab position to restore on Dock to Tabs (clamped; -1 when never floated).
  int floated_from_tab_index{-1};
  std::vector<HistoryState> undo_stack;
  std::vector<HistoryState> redo_stack;
  std::set<LayerId> collapsed_layer_groups;
  // Alt-click eye isolation. `saved` is the pre-isolation visibility snapshot
  // (pre-order); `applied` is the state right after isolating, so any outside
  // visibility change invalidates the restore and the next Alt-click starts a
  // fresh isolation instead.
  struct VisibilityIsolation {
    LayerId isolated_id{0};
    std::vector<std::pair<LayerId, bool>> saved;
    std::vector<std::pair<LayerId, bool>> applied;
  };
  std::optional<VisibilityIsolation> visibility_isolation;
  std::int64_t revision{0};
  std::int64_t saved_revision{0};
  // True when the top undo entry is a coalescable selection move, so the next
  // move in the run merges into it instead of pushing a new entry.
  bool selection_move_coalescing{false};
  // Label and id of the live document's state (the action that produced it);
  // pushes hand these to the stored snapshot and take fresh ones.
  QString current_state_label;
  std::int64_t current_state_id{0};
  std::int64_t next_history_state_id{1};
  static constexpr std::size_t kMaxUndoStates = 40;
  // The history byte budget never evicts a session below this many undo
  // states, even when a single snapshot exceeds the whole budget: a floor of
  // recent undo beats strict memory bounds for giant documents.
  static constexpr std::size_t kMinUndoStatesUnderPressure = 3;
};

}  // namespace patchy::ui
