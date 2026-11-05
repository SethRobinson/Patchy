#pragma once

// Per-document undo state, rotation, stroke compaction, and retained-byte accounting.
// Canvas invalidation and the History panel remain the window's responsibility.
#include "core/document.hpp"
#include "core/stroke_patch.hpp"
#include "ui/canvas_types.hpp"
#include <QRegion>
#include <QString>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace patchy::ui {

struct DocumentSession;
class DocumentSessionStore;

// The document region that can render differently between two states of one
// document, or nullopt when only a full refresh is safe (structure changed,
// canvas resized, or a document-wide rendering input like the editing palette,
// grid, or guides differs). Layer revisions are app-globally unique, so equal
// revision + equal visibility means identical rendering for that layer. The
// region holds each changed layer's effect bounds; callers add the document's
// effect padding for effects of enclosing groups. Undo/redo and the canvas's
// region-only preview refreshes (GitHub #601) use it.
[[nodiscard]] std::optional<QRegion> history_restore_changed_region(const Document& before, const Document& after);

struct HistoryRefreshRegion {
  bool full{false};
  QRegion region;
};

class SessionHistory {
public:
  struct State {
    // The whole document at this point in history, or an empty Document when
    // `patch` holds the state instead.
    Document document;
    // A paint stroke's record (core/stroke_patch.hpp): this state equals the
    // next newer state (or the live document) with the patch's `before`
    // pixels written back. Undo/redo write the patch into the live document
    // in place instead of swapping documents; the memory budget counts its
    // bytes. Eviction from the stack front is always safe because a patch
    // depends only on newer states.
    std::optional<StrokePatch> patch;
    [[nodiscard]] bool is_stroke_patch() const noexcept { return patch.has_value(); }
    std::int64_t revision{0};
    // Selection state at this point in history, so undo/redo restores the
    // selection alongside the pixels (and selection-only edits are undoable).
    SelectionSnapshot selection;
    // Action that produced this state (History panel row text). The label a
    // push receives names the upcoming edit, so it becomes the label of the
    // NEXT state, not of the snapshot being stored.
    QString label;
    // Monotonic per-session row identity; panel rows reference states by id
    // because cap eviction from the stack front shifts vector indices.
    std::int64_t state_id{0};
  };
  static constexpr std::size_t kMaxUndoStates = 40;
  static constexpr std::size_t kMinUndoStatesUnderPressure = 3;

  [[nodiscard]] const std::vector<State>& undo_stack() const noexcept { return undo_stack_; }
  [[nodiscard]] const std::vector<State>& redo_stack() const noexcept { return redo_stack_; }
  [[nodiscard]] const QString& current_state_label() const noexcept { return current_state_label_; }
  [[nodiscard]] std::int64_t current_state_id() const noexcept { return current_state_id_; }
  [[nodiscard]] bool selection_move_coalescing() const noexcept { return selection_move_coalescing_; }
  void set_selection_move_coalescing(bool value) noexcept { selection_move_coalescing_ = value; }

  void record_push(State state, QString action_label);
  void initialize(QString initial_label);
  void rotate(Document& document, std::int64_t& revision, bool backward,
              SelectionSnapshot& live_selection, HistoryRefreshRegion& changed);
  [[nodiscard]] Document materialize(const Document& document, bool in_undo_stack, std::size_t index) const;
  [[nodiscard]] bool shrink_top_to_stroke_patch(const Document& document, StrokePatch::Target target,
                                               LayerId layer_id, ChannelId channel_id,
                                               const QRect& document_dirty);
  [[nodiscard]] std::size_t retained_bytes(const Document& document) const;
  void evict_oldest_undo();
  void trim_undo(std::size_t keep);

private:
  std::vector<State> undo_stack_;
  std::vector<State> redo_stack_;
  bool selection_move_coalescing_{false};
  QString current_state_label_;
  std::int64_t current_state_id_{0};
  std::int64_t next_history_state_id_{1};
};

struct HistoryBudgetResult {
  std::size_t retained_bytes{0};
  bool active_session_evicted{false};
};
[[nodiscard]] HistoryBudgetResult enforce_session_history_budget(
    DocumentSessionStore& sessions, std::size_t budget, const DocumentSession* active_session);

}  // namespace patchy::ui
