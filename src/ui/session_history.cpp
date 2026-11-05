#include "ui/session_history.hpp"

#include "core/document_memory.hpp"
#include "core/layer_render_utils.hpp"
#include "ui/document_session_store.hpp"
#include "ui/qt_geometry.hpp"
#include <algorithm>
#include <optional>
#include <utility>

namespace patchy::ui {
namespace {
// One paint-order entry per layer for the undo/redo render diff. The id
// sequence doubles as a structure detector: any add/remove/reorder/regroup
// makes the sequences diverge and the caller falls back to a full refresh.
struct LayerRenderSignature {
  LayerId id{};
  std::uint64_t render_revision{0};
  bool visible{false};  // set_visible deliberately does not bump revisions
  Rect effect_bounds{};
};

void collect_layer_render_signatures(const std::vector<Layer>& layers, std::vector<LayerRenderSignature>& out) {
  for (const auto& layer : layers) {
    out.push_back(LayerRenderSignature{layer.id(), layer.render_revision(), layer.visible(),
                                       layer_render_bounds(layer)});
    collect_layer_render_signatures(layer.children(), out);
  }
}

}  // namespace

std::optional<QRegion> history_restore_changed_region(const Document& before, const Document& after) {
  if (before.width() != after.width() || before.height() != after.height()) {
    return std::nullopt;
  }
  const auto palette_revision = [](const Document& document) {
    return document.palette_editing().has_value() ? document.palette_editing()->palette_revision : 0ULL;
  };
  if (palette_revision(before) != palette_revision(after)) {
    return std::nullopt;
  }
  if (before.grid_settings().horizontal_cycle_32 != after.grid_settings().horizontal_cycle_32 ||
      before.grid_settings().vertical_cycle_32 != after.grid_settings().vertical_cycle_32) {
    return std::nullopt;
  }
  if (before.guides().size() != after.guides().size() ||
      !std::equal(before.guides().begin(), before.guides().end(), after.guides().begin(),
                  [](const DocumentGuide& a, const DocumentGuide& b) {
                    return a.orientation == b.orientation && a.position_32 == b.position_32;
                  })) {
    return std::nullopt;
  }

  std::vector<LayerRenderSignature> signatures_before;
  std::vector<LayerRenderSignature> signatures_after;
  collect_layer_render_signatures(before.layers(), signatures_before);
  collect_layer_render_signatures(after.layers(), signatures_after);
  if (signatures_before.size() != signatures_after.size()) {
    return std::nullopt;
  }
  QRegion changed;
  for (std::size_t i = 0; i < signatures_before.size(); ++i) {
    const auto& a = signatures_before[i];
    const auto& b = signatures_after[i];
    if (a.id != b.id) {
      return std::nullopt;
    }
    if (a.render_revision == b.render_revision && a.visible == b.visible) {
      continue;
    }
    if (a.effect_bounds.empty() || b.effect_bounds.empty()) {
      // A changed layer without usable bounds (e.g. an adjustment layer, whose
      // reach is everything below it) can't be region-diffed - full refresh.
      return std::nullopt;
    }
    changed += to_qrect(a.effect_bounds);
    changed += to_qrect(b.effect_bounds);
  }
  return changed;
}

void SessionHistory::record_push(State state, QString action_label) {
  // The stored snapshot is the PRE-edit document, so it takes over the label
  // and id of the state it captures; the live document becomes a fresh state
  // named after the action that is about to (or just did) mutate it.
  state.label = std::move(current_state_label_);
  state.state_id = current_state_id_;
  undo_stack_.push_back(std::move(state));
  if (undo_stack_.size() > kMaxUndoStates) {
    undo_stack_.erase(undo_stack_.begin());
  }
  redo_stack_.clear();
  selection_move_coalescing_ = false;
  current_state_label_ = std::move(action_label);
  current_state_id_ = next_history_state_id_++;
}

void SessionHistory::initialize(QString initial_label) {
  undo_stack_.clear();
  redo_stack_.clear();
  selection_move_coalescing_ = false;
  current_state_label_ = std::move(initial_label);
  current_state_id_ = next_history_state_id_++;
}

void SessionHistory::rotate(Document& document, std::int64_t& revision, bool backward,
                            SelectionSnapshot& live_selection, HistoryRefreshRegion& changed) {
  auto& from = backward ? undo_stack_ : redo_stack_;
  auto& to = backward ? redo_stack_ : undo_stack_;
  auto& restored = from.back();
  if (restored.is_stroke_patch()) {
    // The live document stays in place: the stroke's pixels are written back
    // (undo) or forward (redo). The entry crosses stacks carrying the patch,
    // which now describes the hop in the other direction.
    const auto& live_document = std::as_const(document);
    const auto effect_rect_of = [&live_document](const StrokePatch& patch) -> std::optional<QRect> {
      if (patch.target == StrokePatch::Target::DocumentChannel) {
        return QRect();  // saved channels never composite
      }
      const auto* layer = live_document.find_layer(patch.layer_id);
      if (layer == nullptr) {
        return std::nullopt;
      }
      return to_qrect(layer_render_bounds(*layer));
    };
    const auto bounds_before = effect_rect_of(*restored.patch);
    const bool applied = apply_stroke_patch(document, *restored.patch, backward);
    const auto bounds_after = effect_rect_of(*restored.patch);
    if (!applied || !bounds_before.has_value() || !bounds_after.has_value()) {
      changed.full = true;
    } else if (restored.patch->target != StrokePatch::Target::DocumentChannel) {
      // document_changed(QRegion) outsets by the document's effect padding, so
      // the patch rect alone covers styles; alpha-dependent effect bounds that
      // moved with the pixels are covered by their before/after union.
      changed.region += to_qrect(restored.patch->document_rect);
      if (*bounds_before != *bounds_after) {
        changed.region += *bounds_before;
        changed.region += *bounds_after;
      }
    }
    State moved;
    moved.patch = std::move(restored.patch);
    moved.revision = revision;
    moved.selection = std::move(live_selection);
    moved.label = std::move(current_state_label_);
    moved.state_id = current_state_id_;
    revision = restored.revision;
    live_selection = std::move(restored.selection);
    current_state_label_ = std::move(restored.label);
    current_state_id_ = restored.state_id;
    from.pop_back();
    to.push_back(std::move(moved));
    selection_move_coalescing_ = false;
    return;
  }
  // The 32-bit preview exposure is a view setting: history keeps the current one.
  const auto view_exposure = document.color_state().view_exposure_stops;
  // Braced-init evaluation is left to right, so the document moves out before
  // revision/label are read; both history hops are moves (a copy here is a
  // full multi-hundred-MB Document duplication on large canvases).
  to.push_back(State{
      std::move(document), std::nullopt, revision, std::move(live_selection),
      std::move(current_state_label_), current_state_id_});
  document = std::move(restored.document);
  document.color_state().view_exposure_stops = view_exposure;
  revision = restored.revision;
  live_selection = std::move(restored.selection);
  current_state_label_ = std::move(restored.label);
  current_state_id_ = restored.state_id;
  from.pop_back();
  selection_move_coalescing_ = false;
  // The parked pre-rotation document sits intact at to.back(); diff it against
  // the restored one for the partial repaint.
  if (!changed.full) {
    const auto region = history_restore_changed_region(to.back().document, document);
    if (!region.has_value()) {
      changed.full = true;
    } else {
      changed.region += *region;
    }
  }
}

Document SessionHistory::materialize(const Document& document, bool in_undo_stack, std::size_t index) const {
  const auto& stack = in_undo_stack ? undo_stack_ : redo_stack_;
  if (!stack[index].is_stroke_patch()) {
    return stack[index].document;
  }
  // In both stacks a higher index is nearer the present (redo_stack.back() is
  // the nearest future), and a patch is relative to the entry right above it,
  // or to the live document at the top. Start from the nearest whole-document
  // base above `index` and write the patches back down to it: an undo entry
  // holds the older pixels in `before`, a redo entry the newer ones in `after`
  // (filled by the undo that moved it there).
  std::size_t base = index + 1;
  while (base < stack.size() && stack[base].is_stroke_patch()) {
    ++base;
  }
  Document result = base < stack.size() ? stack[base].document : document;
  for (std::size_t k = base; k-- > index;) {
    auto patch = *stack[k].patch;  // a copy: the pixel buffers are copy-on-write
    static_cast<void>(apply_stroke_patch(result, patch, /*restore_before=*/in_undo_stack));
  }
  return result;
}

bool SessionHistory::shrink_top_to_stroke_patch(const Document& document, StrokePatch::Target target,
                                               LayerId layer_id, ChannelId channel_id,
                                               const QRect& document_dirty) {
  auto& entry = undo_stack_.back();
  if (entry.is_stroke_patch() || document_dirty.isEmpty()) {
    return false;
  }
  const Rect dirty{document_dirty.x(), document_dirty.y(), document_dirty.width(),
                   document_dirty.height()};
  auto patch = capture_stroke_patch(entry.document, document, target,
                                    layer_id, channel_id, dirty);
  if (!patch.has_value()) {
    return false;
  }
  entry.patch = std::move(patch);
  entry.document = Document{};
  return true;
}

std::size_t SessionHistory::retained_bytes(const Document& document) const {
  PixelStorageSet live;
  collect_pixel_storage(document, live);
  PixelStorageSet seen;
  std::size_t bytes = 0;
  const auto state_bytes = [&](const State& state) {
    if (state.is_stroke_patch()) {
      return state.patch->byte_size();
    }
    return accumulate_unique_pixel_bytes(state.document, live, seen);
  };
  for (const auto& state : undo_stack_) {
    bytes += state_bytes(state);
  }
  for (const auto& state : redo_stack_) {
    bytes += state_bytes(state);
  }
  return bytes;
}

void SessionHistory::evict_oldest_undo() {
  undo_stack_.erase(undo_stack_.begin());
}

void SessionHistory::trim_undo(std::size_t keep) {
  if (undo_stack_.size() > keep) {
    undo_stack_.erase(undo_stack_.begin(), undo_stack_.end() - static_cast<std::ptrdiff_t>(keep));
  }
  redo_stack_.clear();
}

HistoryBudgetResult enforce_session_history_budget(DocumentSessionStore& sessions, std::size_t budget,
                                                   const DocumentSession* active_session) {
  std::vector<std::pair<DocumentSession*, std::size_t>> totals;
  totals.reserve(sessions.size());
  std::size_t total = 0;
  for (const auto& session_ptr : sessions) {
    const auto bytes = session_ptr->history.retained_bytes(session_ptr->document);
    totals.emplace_back(session_ptr.get(), bytes);
    total += bytes;
  }
  if (total <= budget) {
    return {total, false};
  }
  bool active_session_evicted = false;
  while (total > budget) {
    // Oldest state of the session retaining the most bytes goes first; > (not
    // >=) keeps the earlier tab on ties. An eviction can free zero marginal
    // bytes (storage still shared with newer states), but the loop terminates
    // because the total state count strictly decreases.
    auto largest = totals.end();
    for (auto it = totals.begin(); it != totals.end(); ++it) {
      if (it->first->history.undo_stack().size() <= SessionHistory::kMinUndoStatesUnderPressure) {
        continue;
      }
      if (largest == totals.end() || it->second > largest->second) {
        largest = it;
      }
    }
    if (largest == totals.end()) {
      break;
    }
    largest->first->history.evict_oldest_undo();
    if (largest->first == active_session) {
      active_session_evicted = true;
    }
    const auto recomputed = largest->first->history.retained_bytes(largest->first->document);
    total = total - largest->second + recomputed;
    largest->second = recomputed;
  }
  return {total, active_session_evicted};
}

}  // namespace patchy::ui
