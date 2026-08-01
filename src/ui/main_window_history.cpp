// MainWindow's undo/redo history: undo, redo, both push_undo_snapshot
// overloads, push_selection_history, the History panel rebuild
// (refresh_history_panel/initialize_session_history), update_undo_redo_actions,
// and the anonymous-namespace render-diff helpers (LayerRenderSignature,
// history_restore_changed_region, apply_history_render_refresh) that decide how
// much of the canvas an undo/redo restore must recomposite.

#include "ui/main_window.hpp"
#include "ui/main_window_shared.hpp"

#include "core/blend_math.hpp"
#include "core/layer_metadata.hpp"
#include "core/smart_object.hpp"
#include "core/text_warp.hpp"
#include "core/warp_mesh.hpp"
#include "core/layer_render_utils.hpp"
#include "core/layer_tree.hpp"
#include "core/palette_presets.hpp"
#include "core/pattern_presets.hpp"
#include "core/pixel_tools.hpp"
#include "core/worker_budget.hpp"
#include "formats/palette_io.hpp"
#include "filters/builtin_filters.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/heif_document_io.hpp"
#include "formats/raw_document_io.hpp"
#include "plugins/legacy_photoshop_adapter.hpp"
#include "psd/psd_document_io.hpp"
#include "psd/psd_filter_effects.hpp"
#include "psd/psd_smart_objects.hpp"
#include "ui/action_icons.hpp"
#include "ui/app_settings.hpp"
#include "ui/background_workers.hpp"
#include "render/compositor.hpp"
#include "ui/blend_mode_ui.hpp"
#include "ui/brush_dynamics_popup.hpp"
#include "ui/brush_presets.hpp"
#include "ui/brush_tip_library.hpp"
#include "ui/brush_tip_manager_dialog.hpp"
#include "ui/brush_tip_picker.hpp"
#include "ui/default_brush_tips.hpp"
#include "ui/compatibility_report.hpp"
#include "ui/image_document_io.hpp"
#include "ui/image_save_options_dialog.hpp"
#include "ui/raw_develop_dialog.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/gradient_stops_editor.hpp"
#include "ui/gradient_library.hpp"
#include "ui/gradient_manager_dialog.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/document_float_window.hpp"
#include "ui/font_picker.hpp"
#include "ui/hotkey_editor.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/color_panel.hpp"
#include "ui/layer_style_dialog.hpp"
#include "ui/layer_list_widget.hpp"
#include "ui/localization.hpp"
#include "ui/measurement_units.hpp"
#include "ui/palette_convert_dialog.hpp"
#include "ui/palette_panel.hpp"
#include "ui/pattern_library.hpp"
#include "ui/photo_pattern_presets.hpp"
#include "ui/style_library.hpp"
#include "ui/print_dialog.hpp"
#include "ui/smart_object_render.hpp"
#include "ui/scanner_import.hpp"
#include "ui/image_sequence_dialog.hpp"
#include "ui/sprite_sheet_dialog.hpp"
#include "ui/tile_preview_window.hpp"
#include "ui/warp_text_dialog.hpp"
#include "ui/qt_geometry.hpp"
#include "ui/splash_dialog.hpp"
#include "ui/theme_palette.hpp"
#include "ui/update_checker.hpp"
#include "ui/zoom_status_bar.hpp"
#include "support/string_utils.hpp"

#include <QAbstractItemView>
#include <QAbstractItemModel>
#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QBrush>
#include <QBuffer>
#include <QButtonGroup>
#include <QByteArray>
#include <QDateTime>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QCursor>
#include <QColorSpace>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLayout>
#include <QResizeEvent>
#include <QIcon>
#include <QImageReader>
#include <QInputDialog>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLabel>
#include <QKeySequence>
#include <QListWidget>
#include <QLinearGradient>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QMessageBox>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPushButton>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPolygon>
#include <QPointer>
#include <QProcess>
#include <QProgressDialog>
#include <QRegion>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QScopeGuard>
#include <QSettings>
#include <QShowEvent>
#include <QStandardPaths>
#include <QStandardItem>
#include <QStyledItemDelegate>
#include <QMutex>
#include <QRawFont>
#include <QTextCharFormat>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextLayout>
#include <QTextOption>
#include <QSignalBlocker>
#include <QSize>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QStringList>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleOption>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QToolTip>
#include <QTransform>
#include <QUrl>
#include <QVariant>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <functional>
#include <future>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <tchar.h>
#include <tpcshrd.h>
#endif

#ifndef PATCHY_VERSION
#define PATCHY_VERSION "0.0.0"
#endif

// Icon resources live in the static patchy_ui library; force registration before first use.
int qInitResources_icons();

namespace patchy::ui {

namespace {

int undo_snapshot_test_delay_ms() noexcept {
  bool ok = false;
  const auto value = qEnvironmentVariableIntValue("PATCHY_UNDO_SNAPSHOT_TEST_DELAY_MS", &ok);
  return ok ? std::max(0, value) : 0;
}

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

// The document region that can render differently between two history states,
// or nullopt when only a full refresh is safe (structure changed, canvas
// resized, or a document-wide rendering input like the editing palette, grid,
// or guides differs). Layer revisions are app-globally unique, so equal
// revision + equal visibility means identical rendering for that layer.
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
  // The stored snapshot is the PRE-edit document, so it takes over the label
  // and id of the state it captures; the live document becomes a fresh state
  // named after the action that is about to (or just did) mutate it.
  state.label = std::move(target_session.current_state_label);
  state.state_id = target_session.current_state_id;
  target_session.undo_stack.push_back(std::move(state));
  if (target_session.undo_stack.size() > DocumentSession::kMaxUndoStates) {
    target_session.undo_stack.erase(target_session.undo_stack.begin());
  }
  target_session.redo_stack.clear();
  target_session.selection_move_coalescing = false;
  target_session.current_state_label = std::move(action_label);
  target_session.current_state_id = target_session.next_history_state_id++;
}

void MainWindow::rotate_history_state(DocumentSession& target_session, bool backward,
                                      CanvasWidget::SelectionSnapshot& live_selection) {
  auto& from = backward ? target_session.undo_stack : target_session.redo_stack;
  auto& to = backward ? target_session.redo_stack : target_session.undo_stack;
  // Braced-init evaluation is left to right, so the document moves out before
  // revision/label are read; both history hops are moves (a copy here is a
  // full multi-hundred-MB Document duplication on large canvases).
  to.push_back(DocumentSession::HistoryState{
      std::move(target_session.document), target_session.revision, std::move(live_selection),
      std::move(target_session.current_state_label), target_session.current_state_id});
  auto& restored = from.back();
  target_session.document = std::move(restored.document);
  target_session.revision = restored.revision;
  live_selection = std::move(restored.selection);
  target_session.current_state_label = std::move(restored.label);
  target_session.current_state_id = restored.state_id;
  from.pop_back();
  target_session.selection_move_coalescing = false;
}

void MainWindow::apply_history_restore_tail(DocumentSession& active_session,
                                            const Document& before_document,
                                            CanvasWidget::SelectionSnapshot restored_selection,
                                            const QString& status_message) {
  // Rotations never touch the canvas, so reading the mask edit target here is
  // equivalent to capturing it before them.
  const auto restore_smart_filter_mask_owner =
      canvas_->smart_filter_mask_owner_id();
  const auto restore_smart_filter_mask_mode = canvas_->mask_display_mode();
  const auto changed_region =
      history_restore_changed_region(before_document, active_session.document);
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
  finish_pending_layer_opacity_edit();
  finish_pending_layer_fill_opacity_edit();
  auto& active_session = session();
  if (active_session.undo_stack.empty()) {
    return;
  }
  auto live_selection = canvas_->capture_selection_snapshot();
  rotate_history_state(active_session, /*backward=*/true, live_selection);
  // The pre-undo document now sits intact at redo_stack.back(); the tail diffs
  // it against the restored document for the partial repaint.
  apply_history_restore_tail(active_session, active_session.redo_stack.back().document,
                             std::move(live_selection), tr("Undo"));
}

void MainWindow::redo() {
  finish_pending_layer_opacity_edit();
  finish_pending_layer_fill_opacity_edit();
  auto& active_session = session();
  if (active_session.redo_stack.empty()) {
    return;
  }
  auto live_selection = canvas_->capture_selection_snapshot();
  rotate_history_state(active_session, /*backward=*/false, live_selection);
  apply_history_restore_tail(active_session, active_session.undo_stack.back().document,
                             std::move(live_selection), tr("Redo"));
}

void MainWindow::push_undo_snapshot(QString label) {
  push_undo_snapshot(session(), std::move(label));
}

void MainWindow::push_undo_snapshot(DocumentSession& target_session, QString label) {
  const bool target_is_active = &target_session == active_session();
  if (target_is_active) {
    // The pending layer-opacity edit belongs to the ACTIVE document; a snapshot
    // fired by a background canvas must not flush (and split) its coalesced run.
    finish_pending_layer_opacity_edit();
    finish_pending_layer_fill_opacity_edit();
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
      DocumentSession::HistoryState{snapshot_future.get(), snapshot_revision, std::move(snapshot_selection), {}, 0},
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
    finish_pending_layer_opacity_edit();
    finish_pending_layer_fill_opacity_edit();
  }
  auto& active_session = target_session;
  // A run of moves/nudges collapses into one undo step: once the first move has
  // pushed an entry holding the pre-run position, later moves leave the live
  // selection updated but add no new entry, so undo returns to where the run
  // began and redo lands on the final position. Any non-coalescing edit (below,
  // or push_undo_snapshot) clears the flag and ends the run.
  if (coalesce && active_session.selection_move_coalescing && !active_session.undo_stack.empty()) {
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
      DocumentSession::HistoryState{active_session.document, active_session.revision, std::move(before), {}, 0},
      label);
  active_session.selection_move_coalescing = coalesce;
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
      for (const auto& state : active->undo_stack) {
        add_row(state.label, state.state_id, false);
      }
      add_row(active->current_state_label, active->current_state_id, false);
      history_list_->setCurrentRow(static_cast<int>(active->undo_stack.size()));
      // redo_stack.back() is the nearest future state, so the reverse walk
      // lists the future in chronological order below the current row.
      for (auto it = active->redo_stack.rbegin(); it != active->redo_stack.rend(); ++it) {
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
  finish_pending_layer_opacity_edit();
  finish_pending_layer_fill_opacity_edit();
  if (state_id == active->current_state_id) {
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
  if (const auto backward_steps = steps_to_reach(active->undo_stack); backward_steps.has_value()) {
    steps = *backward_steps;
  } else if (const auto forward_steps = steps_to_reach(active->redo_stack); forward_steps.has_value()) {
    backward = false;
    steps = *forward_steps;
  } else {
    // Stale row: the state was evicted or discarded between press and release.
    refresh_history_panel();
    return;
  }
  auto live_selection = canvas_->capture_selection_snapshot();
  for (std::size_t step = 0; step < steps; ++step) {
    rotate_history_state(*active, backward, live_selection);
  }
  // The pre-jump document sits intact where the first rotation parked it in the
  // opposite stack, `steps` entries below the top; the tail diffs it against
  // the restored document so a multi-step jump still repaints partially.
  const auto& opposite_stack = backward ? active->redo_stack : active->undo_stack;
  apply_history_restore_tail(*active, opposite_stack[opposite_stack.size() - steps].document,
                             std::move(live_selection), active->current_state_label);
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
  if (state_id == active->current_state_id) {
    // The live document may carry an uncommitted transform; settle it so the
    // spawned document matches what the canvas shows (copy_merged precedent).
    if (canvas_ != nullptr) {
      canvas_->finish_free_transform();
    }
    copy = active->document;
    state_label = active->current_state_label;
  } else {
    const auto find_state = [state_id](const std::vector<DocumentSession::HistoryState>& stack)
        -> const DocumentSession::HistoryState* {
      for (const auto& state : stack) {
        if (state.state_id == state_id) {
          return &state;
        }
      }
      return nullptr;
    };
    const auto* state = find_state(active->undo_stack);
    if (state == nullptr) {
      state = find_state(active->redo_stack);
    }
    if (state == nullptr) {
      refresh_history_panel();
      return;
    }
    copy = state->document;
    state_label = state->label;
  }
  auto title = state_label.isEmpty() ? tr("Untitled-%1").arg(sessions_.size() + 1) : state_label;
  add_document_session(std::move(copy), title, QString(), tr("New document"));
  statusBar()->showMessage(tr("Created new document from \"%1\"").arg(title));
}

void MainWindow::initialize_session_history(DocumentSession& target_session, QString initial_label) {
  target_session.undo_stack.clear();
  target_session.redo_stack.clear();
  target_session.selection_move_coalescing = false;
  target_session.current_state_label = std::move(initial_label);
  target_session.current_state_id = target_session.next_history_state_id++;
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
  if (undo_action_ != nullptr) {
    undo_action_->setEnabled(!current_session->undo_stack.empty());
  }
  if (redo_action_ != nullptr) {
    redo_action_->setEnabled(!current_session->redo_stack.empty());
  }
}

}  // namespace patchy::ui
