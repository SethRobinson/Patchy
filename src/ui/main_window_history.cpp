// MainWindow's undo/redo history: undo, redo, both push_undo_snapshot
// overloads, push_selection_history, the History panel rebuild
// (refresh_history_panel/initialize_session_history), update_undo_redo_actions,
// and the anonymous-namespace render-diff helpers (LayerRenderSignature,
// history_restore_changed_region, apply_history_render_refresh) that decide how
// much of the canvas an undo/redo restore must recomposite.

#include "ui/canvas_types.hpp"
#include "core/smart_filter.hpp"
#include "ui/main_window.hpp"
#include "core/worker_budget.hpp"
#include <QStatusBar>
#include "ui/canvas_widget.hpp"
#include "ui/main_window_shared.hpp"

#include "core/document_memory.hpp"
#include "core/smart_object.hpp"
#include "core/stroke_patch.hpp"
#include "ui/background_workers.hpp"
#include "ui/brush_tip_manager_dialog.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/document_float_window.hpp"
#include "ui/color_panel.hpp"
#include "ui/layer_style_dialog.hpp"
#include "ui/layer_list_widget.hpp"
#include "ui/memory_info.hpp"
#include "ui/session_history.hpp"
#include "ui/scanner_import.hpp"
#include "ui/tile_preview_window.hpp"
#include "ui/ui_profile.hpp"
#include "ui/warp_text_dialog.hpp"
#include "ui/theme_palette.hpp"

#include <QListWidgetItem>
#include <QMenu>
#include <QPoint>
#include <QRegion>
#include <QString>
#include <QVariant>
#include <Qt>
#include <QtGlobal>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <future>
#include <iterator>
#include <memory>
#include <optional>
#include <ratio>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>


// Icon resources live in the static patchy_ui library; force registration before first use.
int qInitResources_icons();

namespace patchy::ui {

namespace {

int undo_snapshot_test_delay_ms() noexcept {
  bool ok = false;
  const auto value = qEnvironmentVariableIntValue("PATCHY_UNDO_SNAPSHOT_TEST_DELAY_MS", &ok);
  return ok ? std::max(0, value) : 0;
}

// Above this, a partial refresh loses to the full-invalidation path (which
// keeps the stale frame on screen and recomposites in the background - see
// CanvasWidget::should_defer_full_refresh_to_async).
constexpr std::int64_t kHistoryPartialRefreshAreaLimit = 8'000'000;

void apply_history_render_refresh(CanvasWidget* canvas, const std::optional<QRegion>& changed) {
  if (canvas == nullptr) {
    return;
  }
  if (!changed.has_value()) {
    canvas->document_changed();
    return;
  }
  if (changed->isEmpty()) {
    // Pixels are identical (e.g. a selection-only step): nothing to recomposite.
    return;
  }
  std::int64_t area = 0;
  for (const auto& rect : *changed) {
    area += static_cast<std::int64_t>(rect.width()) * static_cast<std::int64_t>(rect.height());
  }
  if (area >= kHistoryPartialRefreshAreaLimit) {
    canvas->document_changed();
    return;
  }
  canvas->document_changed(*changed);
}

}  // namespace

void MainWindow::record_history_push(DocumentSession& target_session,
                                     DocumentSession::HistoryState state, QString action_label) {
  // Any push ends the stroke whose snapshot was waiting to shrink: the Begin
  // event that follows a stroke's own push re-arms it right after this.
  pending_stroke_patch_.reset();
  target_session.history.record_push(std::move(state), std::move(action_label));
  enforce_history_memory_budget(target_session);
}

void MainWindow::enforce_history_memory_budget(const DocumentSession& push_target) {
  const auto result = enforce_session_history_budget(sessions_, history_memory_budget_bytes(), active_session());
  last_history_retained_bytes_ = result.retained_bytes;
  // Push sites refresh the panel themselves when the push targets the ACTIVE
  // session; eviction that reaches the active session from a background push
  // must refresh it here, or the panel keeps rows whose states are gone.
  if (result.active_session_evicted && &push_target != active_session()) {
    refresh_history_panel();
  }
}

void MainWindow::rotate_history_state(DocumentSession& target_session, bool backward,
                                      SelectionSnapshot& live_selection, HistoryRefreshRegion& changed) {
  target_session.history.rotate(target_session.document, target_session.revision, backward, live_selection, changed);
}

void MainWindow::apply_history_restore_tail(DocumentSession& active_session,
                                            const HistoryRefreshRegion& changed,
                                            CanvasWidget::SelectionSnapshot restored_selection,
                                            const QString& status_message) {
  for (const auto& child : sessions_) {
    if (!child->smart_object_link.has_value() ||
        child->smart_object_link->parent_session_id != active_session.session_id) {
      continue;
    }
    auto& link = *child->smart_object_link;
    const auto& sources = std::as_const(active_session.document).metadata().smart_objects;
    if (sources.find(link.source_uuid) == nullptr) {
      for (const auto& uuid : link.source_uuid_history) {
        if (sources.find(uuid) != nullptr) {
          link.source_uuid = uuid;
          break;
        }
      }
    }
  }
  // Rotations never touch the canvas, so reading the mask edit target here is
  // equivalent to capturing it before them.
  const auto restore_smart_filter_mask_owner =
      canvas_->smart_filter_mask_owner_id();
  const auto restore_smart_filter_mask_mode = canvas_->mask_display_mode();
  const auto changed_region = changed.full ? std::nullopt : std::optional<QRegion>(changed.region);
  const bool normal_composite_unchanged = changed_region.has_value() && changed_region->isEmpty();
  canvas_->set_document_for_history_restore(&active_session.document, normal_composite_unchanged);
  if (restore_smart_filter_mask_owner.has_value()) {
    const auto* restored_layer = std::as_const(active_session.document)
                                     .find_layer(*restore_smart_filter_mask_owner);
    const auto* restored_stack = restored_layer != nullptr
                                     ? restored_layer->smart_filter_stack()
                                     : nullptr;
    auto restored_pixels =
        restored_stack != nullptr &&
                restored_stack->support == SmartFilterStackSupport::Supported
            ? materialize_smart_filter_mask(
                  restored_stack->mask, active_session.document.width(),
                  active_session.document.height())
            : std::nullopt;
    if (restored_pixels.has_value()) {
      static_cast<void>(canvas_->set_smart_filter_mask_edit_target(
          *restore_smart_filter_mask_owner, std::move(*restored_pixels),
          restore_smart_filter_mask_mode));
    }
  }
  canvas_->apply_selection_snapshot(restored_selection);
  refresh_layer_list();
  refresh_layer_controls();
  refresh_channel_panel();
  // Paths live in the Document and are restored with it; the old undo/redo
  // tails forgot this refresh, leaving the Paths panel stale after an undo.
  refresh_paths_panel();
  update_document_action_state();
  apply_history_render_refresh(canvas_, changed_region);
  refresh_palette_panel();
  schedule_palette_compliance_check();
  statusBar()->showMessage(status_message);
  refresh_history_panel();
  update_undo_redo_actions();
  refresh_document_tab_titles();
}

void MainWindow::undo() {
  if (canvas_ == nullptr || canvas_->pointer_gesture_active()) {
    return;
  }
  // A pending Free Transform or Warp owns Undo (GitHub issue 225): it steps back
  // through the session's drags. With no step left, an unchanged session cancels;
  // one still holding a stage from before a mode switch stays, so Undo never
  // reaches the document history behind an open transform.
  if (canvas_->path_transform_active()) {
    // The path transform box writes its live fill un-armed: Undo cancels the
    // session (restoring the path) instead of reaching the history behind it.
    canvas_->cancel_path_transform();
    update_undo_redo_actions();
    return;
  }
  if (canvas_->free_transform_active() || canvas_->warp_transform_active()) {
    if (!canvas_->undo_transform_session_step() && !canvas_->transform_session_has_changes()) {
      if (canvas_->warp_transform_active()) {
        canvas_->cancel_warp_transform();
      } else {
        canvas_->cancel_free_transform();
      }
    }
    refresh_options_bar();
    update_undo_redo_actions();
    return;
  }
  finish_pending_shape_appearance_edit();
  finish_pending_layer_opacity_edit();
  finish_pending_layer_fill_opacity_edit();
  finish_pending_layer_blend_edit();
  auto& active_session = session();
  if (active_session.history.undo_stack().empty()) {
    return;
  }
  auto live_selection = canvas_->capture_selection_snapshot();
  HistoryRefreshRegion changed;
  rotate_history_state(active_session, /*backward=*/true, live_selection, changed);
  apply_history_restore_tail(active_session, changed, std::move(live_selection), tr("Undo"));
}

void MainWindow::redo() {
  if (canvas_ == nullptr || canvas_->pointer_gesture_active()) {
    return;
  }
  if (canvas_->path_transform_active()) {
    return;  // nothing to redo inside a path transform session
  }
  if (canvas_->free_transform_active() || canvas_->warp_transform_active()) {
    canvas_->redo_transform_session_step();
    refresh_options_bar();
    update_undo_redo_actions();
    return;
  }
  finish_pending_shape_appearance_edit();
  finish_pending_layer_opacity_edit();
  finish_pending_layer_fill_opacity_edit();
  finish_pending_layer_blend_edit();
  auto& active_session = session();
  if (active_session.history.redo_stack().empty()) {
    return;
  }
  auto live_selection = canvas_->capture_selection_snapshot();
  HistoryRefreshRegion changed;
  rotate_history_state(active_session, /*backward=*/false, live_selection, changed);
  apply_history_restore_tail(active_session, changed, std::move(live_selection), tr("Redo"));
}

void MainWindow::push_undo_snapshot(QString label) {
  push_undo_snapshot(session(), std::move(label));
}

void MainWindow::push_undo_snapshot(DocumentSession& target_session, QString label) {
  const bool target_is_active = &target_session == active_session();
  if (target_is_active) {
    // The pending layer-opacity edit belongs to the ACTIVE document; a snapshot
    // fired by a background canvas must not flush (and split) its coalesced run.
    finish_pending_shape_appearance_edit();
    finish_pending_layer_opacity_edit();
    finish_pending_layer_fill_opacity_edit();
    finish_pending_layer_blend_edit();
  }
  const auto started = std::chrono::steady_clock::now();
  auto& active_session = target_session;
  const auto snapshot_revision = active_session.revision;
  const auto make_snapshot = [&active_session] {
    if (const auto delay = undo_snapshot_test_delay_ms(); delay > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    }
    return active_session.document;
  };
  std::future<Document> snapshot_future;
#if defined(Q_OS_WASM) && defined(__EMSCRIPTEN_PTHREADS__)
  // A dry pre-spawned pool would leave launch_async waiting on a lazy Worker
  // spawn, which a blocked path cannot rely on (docs/wasm.md); copy inline.
  if (patchy::idle_prespawned_pool_workers() < 1) {
    std::promise<Document> inline_snapshot;
    inline_snapshot.set_value(make_snapshot());
    snapshot_future = inline_snapshot.get_future();
  } else
#endif
  {
    snapshot_future = launch_async(make_snapshot);
  }
  if (active_session.canvas != nullptr) {
    active_session.canvas->wait_for_processing_operation([&snapshot_future] {
      return snapshot_future.wait_for(std::chrono::milliseconds(16)) == std::future_status::ready;
    });
  } else {
    snapshot_future.wait();
  }
  // Capture the selection that is active right now (before the edit mutates the
  // document) so undoing this edit also restores the selection it ran against.
  // The snapshot comes from the session's OWN canvas: an edit fired by a
  // non-active canvas must not record the active document's selection.
  auto snapshot_selection = active_session.canvas != nullptr ? active_session.canvas->capture_selection_snapshot()
                                                             : CanvasWidget::SelectionSnapshot{};
  record_history_push(
      active_session,
      DocumentSession::HistoryState{snapshot_future.get(), std::nullopt, snapshot_revision,
                                    std::move(snapshot_selection), {}, 0},
      label);
  mark_session_modified(active_session);
  // The History panel and status bar mirror the ACTIVE session; an edit landing
  // in a background session keeps its undo stack but must not inject its label
  // into the panel the user is looking at.
  if (target_is_active) {
    refresh_history_panel();
    statusBar()->showMessage(label);
  }
  update_undo_redo_actions();
  const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
  log_ui_profile("push_undo_snapshot", elapsed, label.toStdString());
}

void MainWindow::push_selection_history(DocumentSession& target_session, QString label,
                                        CanvasWidget::SelectionSnapshot before, bool coalesce) {
  const bool target_is_active = &target_session == active_session();
  if (target_is_active) {
    finish_pending_shape_appearance_edit();
    finish_pending_layer_opacity_edit();
    finish_pending_layer_fill_opacity_edit();
    finish_pending_layer_blend_edit();
  }
  auto& active_session = target_session;
  // A run of moves/nudges collapses into one undo step: once the first move has
  // pushed an entry holding the pre-run position, later moves leave the live
  // selection updated but add no new entry, so undo returns to where the run
  // began and redo lands on the final position. Any non-coalescing edit (below,
  // or push_undo_snapshot) clears the flag and ends the run.
  if (coalesce && active_session.history.selection_move_coalescing() && !active_session.history.undo_stack().empty()) {
    if (target_is_active) {
      statusBar()->showMessage(label);
    }
    return;
  }
  // A selection-only edit leaves the pixels untouched, so the entry holds the
  // current document together with the pre-edit selection. The document is not
  // flagged modified for save purposes (a mere selection is not unsaved work),
  // but the change still joins the undo/redo history.
  record_history_push(
      active_session,
      DocumentSession::HistoryState{active_session.document, std::nullopt, active_session.revision,
                                    std::move(before), {}, 0},
      label);
  active_session.history.set_selection_move_coalescing(coalesce);
  // Panel/status mirror the active session only (see push_undo_snapshot).
  if (target_is_active) {
    refresh_history_panel();
    statusBar()->showMessage(label);
  }
  update_undo_redo_actions();
}

void MainWindow::refresh_history_panel() {
  if (history_list_ != nullptr) {
    history_list_->clear();
    if (const auto* active = active_session(); active != nullptr) {
      const auto add_row = [this](const QString& label, std::int64_t state_id, bool future) {
        auto* item = new QListWidgetItem(label.isEmpty() ? tr("Edit") : label, history_list_);
        item->setData(Qt::UserRole, QVariant::fromValue<qlonglong>(state_id));
        if (future) {
          item->setForeground(theme().history_future_text);
        }
      };
      for (const auto& state : active->history.undo_stack()) {
        add_row(state.label, state.state_id, false);
      }
      add_row(active->history.current_state_label(), active->history.current_state_id(), false);
      history_list_->setCurrentRow(static_cast<int>(active->history.undo_stack().size()));
      // redo_stack.back() is the nearest future state, so the reverse walk
      // lists the future in chronological order below the current row.
      for (auto it = active->history.redo_stack().rbegin(); it != active->history.redo_stack().rend(); ++it) {
        add_row(it->label, it->state_id, true);
      }
      history_list_->scrollToItem(history_list_->currentItem());
    }
  }
  refresh_document_info();
}

void MainWindow::handle_history_row_clicked(QListWidgetItem* item) {
  if (item == nullptr) {
    return;
  }
  jump_to_history_state(item->data(Qt::UserRole).toLongLong());
}

void MainWindow::jump_to_history_state(std::int64_t state_id) {
  if (canvas_ == nullptr || canvas_->pointer_gesture_active()) {
    return;
  }
  auto* active = active_session();
  if (active == nullptr) {
    return;
  }
  if (preview_dialog_edit_locked()) {
    // The list is disabled while the lock is held (register_document_widget),
    // so this is defense in depth; the rebuild undoes Qt's click highlight.
    show_preview_dialog_edit_lock_message();
    refresh_history_panel();
    return;
  }
  // Flushing a pending opacity edit can push a history entry (which clears the
  // redo stack), so it must happen before the clicked id is resolved.
  finish_pending_shape_appearance_edit();
  finish_pending_layer_opacity_edit();
  finish_pending_layer_fill_opacity_edit();
  finish_pending_layer_blend_edit();
  if (state_id == active->history.current_state_id()) {
    refresh_history_panel();
    return;
  }
  const auto steps_to_reach = [state_id](const std::vector<DocumentSession::HistoryState>& stack)
      -> std::optional<std::size_t> {
    for (std::size_t index = stack.size(); index-- > 0;) {
      if (stack[index].state_id == state_id) {
        return stack.size() - index;
      }
    }
    return std::nullopt;
  };
  bool backward = true;
  std::size_t steps = 0;
  if (const auto backward_steps = steps_to_reach(active->history.undo_stack()); backward_steps.has_value()) {
    steps = *backward_steps;
  } else if (const auto forward_steps = steps_to_reach(active->history.redo_stack()); forward_steps.has_value()) {
    backward = false;
    steps = *forward_steps;
  } else {
    // Stale row: the state was evicted or discarded between press and release.
    refresh_history_panel();
    return;
  }
  auto live_selection = canvas_->capture_selection_snapshot();
  // Each rotation adds its own changed area, so a multi-step jump still
  // repaints partially (the union of the steps).
  HistoryRefreshRegion changed;
  for (std::size_t step = 0; step < steps; ++step) {
    rotate_history_state(*active, backward, live_selection, changed);
  }
  apply_history_restore_tail(*active, changed, std::move(live_selection), active->history.current_state_label());
}

void MainWindow::show_history_context_menu(const QPoint& position) {
  if (history_list_ == nullptr || !has_active_document() || preview_dialog_edit_locked()) {
    return;
  }
  auto* item = history_list_->itemAt(position);
  if (item == nullptr) {
    return;
  }
  QMenu menu(history_list_);
  auto* new_document_action = menu.addAction(tr("New Document From This State"));
  hide_menu_action_icons(&menu);
  auto* chosen = menu.exec(history_list_->viewport()->mapToGlobal(position));
  if (chosen == new_document_action) {
    open_history_state_as_new_document(item->data(Qt::UserRole).toLongLong());
  }
}

void MainWindow::open_history_state_as_new_document(std::int64_t state_id) {
  auto* active = active_session();
  if (active == nullptr) {
    return;
  }
  if (preview_dialog_edit_locked()) {
    // add_document_session's activation tail must never run under the lock
    // (see the comment in its body).
    show_preview_dialog_edit_lock_message();
    return;
  }
  Document copy;
  QString state_label;
  if (state_id == active->history.current_state_id()) {
    // The live document may carry an uncommitted transform; settle it so the
    // spawned document matches what the canvas shows (copy_merged precedent).
    if (canvas_ != nullptr) {
      canvas_->finish_free_transform();
    }
    copy = active->document;
    state_label = active->history.current_state_label();
  } else {
    const auto find_state = [state_id](const std::vector<DocumentSession::HistoryState>& stack)
        -> std::optional<std::size_t> {
      for (std::size_t index = 0; index < stack.size(); ++index) {
        if (stack[index].state_id == state_id) {
          return index;
        }
      }
      return std::nullopt;
    };
    bool in_undo_stack = true;
    auto index = find_state(active->history.undo_stack());
    if (!index.has_value()) {
      in_undo_stack = false;
      index = find_state(active->history.redo_stack());
    }
    if (!index.has_value()) {
      refresh_history_panel();
      return;
    }
    copy = materialize_history_state(*active, in_undo_stack, *index);
    state_label = (in_undo_stack ? active->history.undo_stack() : active->history.redo_stack())[*index].label;
  }
  auto title = state_label.isEmpty() ? tr("Untitled-%1").arg(sessions_.size() + 1) : state_label;
  add_document_session(std::move(copy), title, QString(), tr("New document"));
  apply_opening_view(session());
  // No backing file: closing must warn about unsaved changes.
  mark_session_modified(session());
  statusBar()->showMessage(tr("Created new document from \"%1\"").arg(title));
}

void MainWindow::handle_paint_stroke_history_event(CanvasWidget* canvas,
                                                   const CanvasWidget::PaintStrokeHistoryEvent& event) {
  using Phase = CanvasWidget::PaintStrokeHistoryEvent::Phase;
  auto* target_session = session_for_canvas(canvas);
  if (event.phase == Phase::Begin) {
    pending_stroke_patch_.reset();
    // Stroke Path owns one snapshot for its whole run of synthetic strokes;
    // its strokes keep the full entry.
    if (!stroke_patch_history_enabled_ || scripted_stroke_undo_suppressed_ || target_session == nullptr ||
        target_session->history.undo_stack().empty() || target_session->history.undo_stack().back().is_stroke_patch()) {
      return;
    }
    PendingStrokePatch pending;
    pending.session_id = target_session->session_id;
    pending.snapshot_state_id = target_session->history.undo_stack().back().state_id;
    pending.live_state_id = target_session->history.current_state_id();
    switch (event.target) {
      case CanvasWidget::LayerEditTarget::Content:
        pending.target = StrokePatch::Target::LayerContent;
        break;
      case CanvasWidget::LayerEditTarget::Mask:
        pending.target = StrokePatch::Target::LayerMask;
        break;
      case CanvasWidget::LayerEditTarget::DocumentChannel:
        pending.target = StrokePatch::Target::DocumentChannel;
        break;
      default:
        return;
    }
    pending.layer_id = event.layer_id;
    pending.channel_id = event.channel_id;
    pending_stroke_patch_ = pending;
    return;
  }
  if (!pending_stroke_patch_.has_value()) {
    return;
  }
  const auto pending = *pending_stroke_patch_;
  pending_stroke_patch_.reset();
  // The snapshot to shrink must still be the top undo entry and the live state
  // must still be the stroke's: anything pushed, undone or jumped since leaves
  // the full snapshot alone.
  if (target_session == nullptr || target_session->session_id != pending.session_id ||
      target_session->history.undo_stack().empty() || !target_session->history.redo_stack().empty() ||
      target_session->history.undo_stack().back().state_id != pending.snapshot_state_id ||
      target_session->history.current_state_id() != pending.live_state_id) {
    return;
  }
  if (!target_session->history.shrink_top_to_stroke_patch(std::as_const(target_session->document),
      pending.target, pending.layer_id, pending.channel_id, event.document_dirty)) {
    return;
  }
  // The retained-bytes cache (and any eviction decision) reflects the shrink.
  enforce_history_memory_budget(*target_session);
}

Document MainWindow::materialize_history_state(const DocumentSession& target_session, bool in_undo_stack,
                                               std::size_t index) const {
  return target_session.history.materialize(target_session.document, in_undo_stack, index);
}

void MainWindow::initialize_session_history(DocumentSession& target_session, QString initial_label) {
  target_session.history.initialize(std::move(initial_label));
  if (&target_session == active_session()) {
    refresh_history_panel();
  }
}

void MainWindow::update_undo_redo_actions() {
  if (preview_dialog_edit_locked()) {
    if (undo_action_ != nullptr) {
      undo_action_->setEnabled(false);
    }
    if (redo_action_ != nullptr) {
      redo_action_->setEnabled(false);
    }
    return;
  }
  const auto* current_session = active_session();
  if (current_session == nullptr) {
    if (undo_action_ != nullptr) {
      undo_action_->setEnabled(false);
    }
    if (redo_action_ != nullptr) {
      redo_action_->setEnabled(false);
    }
    return;
  }
  // During a pending transform Undo always has something to do (a step back, or
  // cancelling an unchanged session) and Redo follows the session's own steps.
  const bool transform_session =
      canvas_ != nullptr && (canvas_->free_transform_active() || canvas_->warp_transform_active());
  if (undo_action_ != nullptr) {
    undo_action_->setEnabled(transform_session || !current_session->history.undo_stack().empty());
  }
  if (redo_action_ != nullptr) {
    redo_action_->setEnabled(transform_session ? canvas_->transform_session_redo_step_count() > 0
                                               : !current_session->history.redo_stack().empty());
  }
}

}  // namespace patchy::ui
