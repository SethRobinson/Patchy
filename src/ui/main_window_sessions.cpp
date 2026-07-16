// MainWindow's document session/tab/float-window management, split out of
// main_window.cpp: session add/activate/close paths, the float-window
// machinery (float/dock/tear-off/consolidate/tile/cascade and drag-to-dock),
// and the session lookup/title/modified helpers. Pure function moves from
// main_window.cpp; behavior must stay identical.

#include "ui/main_window.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/custom_shape_library.hpp"

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
#include "ui/sprite_sheet_dialog.hpp"
#include "ui/tile_preview_window.hpp"
#include "ui/warp_text_dialog.hpp"
#include "ui/qt_geometry.hpp"
#include "ui/start_panel.hpp"
#include "ui/splash_dialog.hpp"
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

void MainWindow::add_document_session(Document document, QString title, QString path) {
  // The thumbnail cache is scoped to the active document; layer ids restart
  // per document, so entries must never leak across tabs.
  layer_thumbnail_cache_.clear();
  channel_thumbnail_cache_.clear();
  auto session = std::make_unique<DocumentSession>();
  session->session_id = next_session_id_++;
  session->document = std::move(document);
  if (session->document.guides().empty() && session->document.grid_settings().horizontal_cycle_32 == 576 &&
      session->document.grid_settings().vertical_cycle_32 == 576) {
    session->document.grid_settings().horizontal_cycle_32 = view_grid_spacing_32_;
    session->document.grid_settings().vertical_cycle_32 = view_grid_spacing_32_;
  }
  session->title = std::move(title);
  session->path = std::move(path);
  collect_initially_collapsed_layer_groups(session->document.layers(), session->collapsed_layer_groups);
  session->canvas = new CanvasWidget(document_tabs_);
  session->canvas->setAcceptDrops(true);
  session->canvas->installEventFilter(this);
  configure_canvas(session->canvas);
  session->canvas->set_document(&session->document);
  const bool used_default_tool_settings = canvas_ == nullptr;
  if (!used_default_tool_settings) {
    session->canvas->set_brush_size(canvas_->brush_size());
    session->canvas->set_brush_opacity(canvas_->brush_opacity());
    session->canvas->set_brush_flow(canvas_->brush_flow());
    session->canvas->set_brush_softness(canvas_->brush_softness());
    session->canvas->set_brush_build_up(canvas_->brush_build_up());
    session->canvas->set_wand_tolerance(canvas_->wand_tolerance());
    session->canvas->set_wand_contiguous(canvas_->wand_contiguous());
    session->canvas->set_wand_sample_all_layers(canvas_->wand_sample_all_layers());
    session->canvas->set_magnetic_lasso_width(canvas_->magnetic_lasso_width());
    session->canvas->set_magnetic_lasso_edge_contrast(canvas_->magnetic_lasso_edge_contrast());
    session->canvas->set_magnetic_lasso_frequency(canvas_->magnetic_lasso_frequency());
    session->canvas->set_clone_aligned(canvas_->clone_aligned());
    session->canvas->set_healing_diffusion(current_healing_diffusion_);
    session->canvas->set_local_adjustment_strength(current_local_adjustment_strength_);
    session->canvas->set_local_tone_range(current_local_tone_range_);
    session->canvas->set_local_protect_tones(current_local_protect_tones_);
    session->canvas->set_sponge_mode(current_sponge_mode_);
    session->canvas->set_sponge_vibrance(current_sponge_vibrance_);
    session->canvas->set_gradient_method(canvas_->gradient_method());
    session->canvas->set_gradient_reverse(canvas_->gradient_reverse());
    session->canvas->set_gradient_opacity(canvas_->gradient_opacity());
    session->canvas->set_gradient_stops(canvas_->gradient_stops());
    session->canvas->set_show_transform_controls(canvas_->show_transform_controls());
  } else if (const auto* preset = find_brush_preset(default_startup_brush_preset_id()); preset != nullptr) {
    apply_brush_preset(*session->canvas, *preset);
  }
  if (move_auto_select_check_ != nullptr) {
    session->canvas->set_auto_select_layer(move_auto_select_check_->isChecked());
  }
  if (move_show_transform_controls_check_ != nullptr) {
    session->canvas->set_show_transform_controls(move_show_transform_controls_check_->isChecked());
  }
  if (clone_aligned_check_ != nullptr) {
    session->canvas->set_clone_aligned(clone_aligned_check_->isChecked());
  }
  session->canvas->set_healing_diffusion(current_healing_diffusion_);
  session->canvas->set_local_adjustment_strength(current_local_adjustment_strength_);
  session->canvas->set_local_tone_range(current_local_tone_range_);
  session->canvas->set_local_protect_tones(current_local_protect_tones_);
  session->canvas->set_sponge_mode(current_sponge_mode_);
  session->canvas->set_sponge_vibrance(current_sponge_vibrance_);
  session->canvas->set_mixer_wet(current_mixer_wet_);
  session->canvas->set_mixer_load(current_mixer_load_);
  session->canvas->set_mixer_mix(current_mixer_mix_);
  session->canvas->set_mixer_flow(current_mixer_flow_);
  apply_pattern_stamp_settings_to_canvas(session->canvas);
  apply_selection_modes_to_canvas(session->canvas);
  session->canvas->set_tool(current_tool_);
  session->canvas->set_marquee_style(current_marquee_style_);
  session->canvas->set_marquee_fixed_size(current_marquee_width_, current_marquee_height_);
  session->canvas->set_marquee_corner_radius(current_marquee_corner_radius_);
  session->canvas->set_selection_feather_radius(current_selection_feather_radius_);
  session->canvas->set_selection_antialias(current_selection_antialias_);
  session->canvas->set_fill_shapes(current_fill_shapes_);
  session->canvas->set_shape_corner_radius(current_shape_corner_radius_);
  session->canvas->set_shape_style(current_shape_style_);
  session->canvas->set_shape_fixed_size(current_shape_width_, current_shape_height_);
  session->canvas->set_vector_tool_mode(current_vector_tool_mode_);
  if (auto* sides = findChild<QSpinBox*>(QStringLiteral("polygonSidesSpin")); sides != nullptr) {
    session->canvas->set_polygon_sides(sides->value());
  }
  if (auto* inset = findChild<QSpinBox*>(QStringLiteral("polygonStarInsetSpin")); inset != nullptr) {
    session->canvas->set_polygon_star_inset(inset->value());
  }
  if (custom_shape_combo_ != nullptr) {
    const auto shape_id = custom_shape_combo_->currentData().toString();
    const auto* shape_entry =
        shape_id.isEmpty() ? nullptr : custom_shape_library().find_entry_by_shape_id(shape_id);
    session->canvas->set_custom_shape_path(
        shape_entry != nullptr ? std::make_shared<const patchy::VectorPath>(shape_entry->path)
                               : std::shared_ptr<const patchy::VectorPath>{});
  }
  apply_canvas_aid_settings(session->canvas);

  auto* canvas = session->canvas;
  const auto tab_title = session->title;
  sessions_.push_back(std::move(session));
  const auto tab_index = document_tabs_->addTab(canvas, tab_title);
  document_tabs_->setCurrentIndex(tab_index);
  // Unreachable while the preview-dialog edit lock is held: every document
  // creation entry point (File > New/Open, open_document_path, drag & drop,
  // scanner import) refuses up front, so this tail may assume the new session
  // owns activation. configure_canvas still births canvases edit-locked as
  // defense in depth.
  canvas_ = canvas;
  pending_layer_thumbnail_refresh_ = false;
  canvas_->setFocus(Qt::OtherFocusReason);
  refresh_options_bar();
  if (used_default_tool_settings) {
    if (brush_preset_combo_ != nullptr) {
      const auto preset_index = brush_preset_combo_->findData(default_startup_brush_preset_id());
      if (preset_index >= 0) {
        brush_preset_combo_->setCurrentIndex(preset_index);
      }
    }
    sync_brush_controls_from_canvas();
  }
  refresh_layer_list();
  refresh_layer_controls();
  refresh_channel_panel();
  refresh_document_info();  update_undo_redo_actions();
  update_document_action_state();
  refresh_document_tab_titles();
  if (startup_tool_settings_pending_) {
    // Startup opens with no document, so the one-time settings load waits for the
    // first canvas; the options bar then re-reads the stored values it applied.
    startup_tool_settings_pending_ = false;
    load_tool_settings();
    sync_tool_option_controls_from_canvas();
  }
  update_start_panel_visibility();
}

void MainWindow::update_start_panel_visibility() {
  if (start_panel_ == nullptr || document_tabs_ == nullptr) {
    return;
  }
  const bool show = sessions_.empty();
  if (show) {
    start_panel_->set_recent_files(recent_files_);
    start_panel_->setGeometry(document_tabs_->rect());
    start_panel_->raise();
  }
  start_panel_->setVisible(show);
}

void MainWindow::activate_document_tab(int index) {
  auto* canvas = index >= 0 && document_tabs_ != nullptr
                     ? dynamic_cast<CanvasWidget*>(document_tabs_->widget(index))
                     : nullptr;
  activate_document_canvas(canvas);
}

void MainWindow::activate_document_canvas(CanvasWidget* canvas) {
  if (preview_dialog_edit_locked() && canvas != preview_dialog_edit_lock_canvas_) {
    if (auto* locked_canvas = preview_dialog_edit_lock_canvas_.data(); locked_canvas != nullptr) {
      if (auto* locked_session = session_for_canvas(locked_canvas);
          locked_session != nullptr && locked_session->float_window != nullptr) {
        locked_session->float_window->raise();
        locked_session->float_window->activateWindow();
      } else if (const auto locked_index = document_tabs_ != nullptr ? document_tabs_->indexOf(locked_canvas) : -1;
                 locked_index >= 0) {
        QSignalBlocker blocker(document_tabs_);
        document_tabs_->setCurrentIndex(locked_index);
      }
    }
    show_preview_dialog_edit_lock_message();
    return;
  }
  const auto canvas_changed = canvas != canvas_;
  if (canvas_changed) {
    // Settle any open inline text edit while the OUTGOING canvas is still active:
    // the commit rasterizes into session(), so it must run before canvas_ moves.
    // User-driven tab clicks already committed via the focus change, but
    // programmatic switches (File > Open, float/dock) reach here mid-edit.
    finish_active_text_editor();
    // Brush settings are application-wide: capture the outgoing canvas's live
    // values so the incoming canvas (whose copies may be stale) inherits them.
    stash_active_brush_settings();
    // Layer ids restart per document, so the active-document thumbnail cache
    // cannot survive a document switch.
    layer_thumbnail_cache_.clear();
    channel_thumbnail_cache_.clear();
  }
  if (canvas == nullptr || session_for_canvas(canvas) == nullptr) {
    canvas_ = nullptr;
    refresh_options_bar();
    refresh_layer_list();
    refresh_layer_controls();
    refresh_channel_panel();
    refresh_document_info();
    refresh_color_buttons();
    if (quick_mask_action_ != nullptr) {
      const QSignalBlocker blocker(quick_mask_action_);
      quick_mask_action_->setChecked(false);
    }
    update_undo_redo_actions();
    update_document_action_state();
    refresh_document_window_title();
    update_start_panel_visibility();
    return;
  }
  canvas_ = canvas;
  pending_layer_thumbnail_refresh_ = false;
  apply_selection_modes_to_canvas(canvas_);
  canvas_->set_tool(current_tool_);
  canvas_->set_marquee_style(current_marquee_style_);
  canvas_->set_marquee_fixed_size(current_marquee_width_, current_marquee_height_);
  canvas_->set_marquee_corner_radius(current_marquee_corner_radius_);
  canvas_->set_selection_feather_radius(current_selection_feather_radius_);
  canvas_->set_selection_antialias(current_selection_antialias_);
  canvas_->set_fill_shapes(current_fill_shapes_);
  canvas_->set_shape_corner_radius(current_shape_corner_radius_);
  canvas_->set_shape_style(current_shape_style_);
  canvas_->set_shape_fixed_size(current_shape_width_, current_shape_height_);
  canvas_->set_healing_diffusion(current_healing_diffusion_);
  canvas_->set_local_adjustment_strength(current_local_adjustment_strength_);
  canvas_->set_local_tone_range(current_local_tone_range_);
  canvas_->set_local_protect_tones(current_local_protect_tones_);
  canvas_->set_sponge_mode(current_sponge_mode_);
  canvas_->set_sponge_vibrance(current_sponge_vibrance_);
  canvas_->set_mixer_wet(current_mixer_wet_);
  canvas_->set_mixer_load(current_mixer_load_);
  canvas_->set_mixer_mix(current_mixer_mix_);
  canvas_->set_mixer_flow(current_mixer_flow_);
  apply_pattern_stamp_settings_to_canvas(canvas_);
  if (canvas_changed) {
    apply_active_brush_settings_to_canvas();
    sync_brush_controls_from_canvas();
  }
  apply_canvas_aid_settings(canvas_);
  canvas_->setFocus(Qt::OtherFocusReason);
  refresh_options_bar();
  refresh_layer_list();
  refresh_layer_controls();
  refresh_channel_panel();
  refresh_document_info();
  refresh_color_buttons();
  if (quick_mask_action_ != nullptr) {
    const QSignalBlocker blocker(quick_mask_action_);
    quick_mask_action_->setChecked(canvas_->quick_mask_active());
  }
  canvas_->refresh_info_display();
  update_undo_redo_actions();
  update_document_action_state();
  refresh_document_window_title();
}

bool MainWindow::close_document_tab(int index) {
  if (document_tabs_ == nullptr || index < 0 || index >= document_tabs_->count()) {
    return false;
  }
  auto* target_session = session_for_canvas(dynamic_cast<CanvasWidget*>(document_tabs_->widget(index)));
  return target_session != nullptr && close_document_session(*target_session);
}

bool MainWindow::close_active_document() {
  auto* target_session = active_session();
  return target_session != nullptr && close_document_session(*target_session);
}

bool MainWindow::close_document_session(DocumentSession& target_session) {
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return false;
  }
  // Everything below re-resolves the session by id: the text-editor commit and the
  // smart-object child recursion both run arbitrary UI code (dialogs, nested
  // closes) that can erase sessions_ entries.
  const auto target_id = target_session.session_id;
  // Commit any in-progress inline text edit while its canvas is still active:
  // the pending text belongs in the save-changes decision below, and an editor
  // that survives into removeTab() auto-commits on the focus change mid
  // teardown, after activation has already moved canvas_.
  finish_active_text_editor();
  auto* live_session = session_with_id(target_id);
  if (live_session == nullptr) {
    return false;
  }
  // Closing a document whose Edit Smart Object Contents tabs are still open would
  // orphan their commit target, so resolve the children first (each modified child
  // gets its own save prompt; Save commits into this still-open parent). Children
  // are held as ids, not pointers: closing one child can recursively close another
  // (nested smart objects), and a stale pointer must never be revisited.
  if (const auto children = open_smart_object_child_sessions(target_id); !children.empty()) {
    std::vector<std::int64_t> child_ids;
    child_ids.reserve(children.size());
    for (const auto* child : children) {
      child_ids.push_back(child->session_id);
    }
    const auto title = live_session->title.isEmpty() ? tr("Untitled") : live_session->title;
    const auto answer = show_warning_message(
        this, tr("Close smart object contents?"),
        tr("%1 has smart object contents open for editing. Close those tabs too?").arg(title),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes,
        QStringLiteral("closeSmartObjectChildrenMessageBox"));
    if (answer != QMessageBox::Yes) {
      return false;
    }
    for (const auto child_id : child_ids) {
      if (auto* child = session_with_id(child_id); child != nullptr && !close_document_session(*child)) {
        return false;
      }
    }
    auto* refreshed = session_with_id(target_id);
    return refreshed != nullptr && close_document_session(*refreshed);
  }
  if (!confirm_close_session(*live_session)) {
    return false;
  }
  auto* canvas = live_session->canvas;
  auto* float_window = live_session->float_window;
  live_session->float_window = nullptr;
  if (float_window == nullptr && document_tabs_ != nullptr) {
    if (const auto tab_index = document_tabs_->indexOf(canvas); tab_index >= 0) {
      // Blocked so removeTab's currentChanged cannot activate the neighbor while
      // the dying session is still in sessions_; the fallback activation below is
      // the close's single activation.
      const QSignalBlocker blocker(document_tabs_);
      document_tabs_->removeTab(tab_index);
    }
  }
  if (canvas_ == canvas) {
    // Brush settings are application-wide; capture the closing canvas's live
    // values while it is still canvas_ (the blocked removeTab means no
    // activation ran to stash them), then drop the pointer before the delete so
    // the fallback activation below starts from a clean slate.
    stash_active_brush_settings();
    canvas_ = nullptr;
  }
  if (float_window != nullptr) {
    // The window may be inside its own closeEvent right now: detach the canvas,
    // then release the window asynchronously (never a synchronous delete).
    (void)float_window->take_canvas();
    float_window->hide();
    float_window->deleteLater();
  }
  // Destroy the canvas BEFORE erasing the session: take_canvas's reparent and
  // the delete both deliver hide/focus-out events synchronously, and those
  // handlers (stroke commits, quick-mask finish, magnetic-lasso cancel) walk
  // canvas->document_, which is owned by the session. Erasing first left a
  // narrow use-after-free window on the freed Document.
  delete canvas;
  // Re-resolve after the teardown events above: they run arbitrary handlers.
  const auto found = std::find_if(sessions_.begin(), sessions_.end(), [live_session](const auto& candidate) {
    return candidate.get() == live_session;
  });
  if (found != sessions_.end()) {
    sessions_.erase(found);
  }
  if (canvas_ == nullptr) {
    // activate_document_session (not _canvas) so a floated successor's window is
    // also raised: the main window would otherwise show an empty tab area while
    // the newly active document sits buried or minimized.
    if (auto* fallback = session_for_canvas(fallback_active_canvas()); fallback != nullptr) {
      activate_document_session(*fallback);
    } else {
      activate_document_canvas(nullptr);
    }
  } else {
    // A background document closed; the active one keeps activation and only
    // re-runs its (idempotent) activation to refresh panel/action state.
    activate_document_canvas(canvas_);
  }
  refresh_document_tab_titles();
  if (sessions_.empty() && statusBar() != nullptr) {
    statusBar()->showMessage(tr("No document"));
  }
  return true;
}

void MainWindow::close_other_document_tabs(int index) {
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
  if (document_tabs_ == nullptr || index < 0 || index >= document_tabs_->count()) {
    return;
  }
  auto* keep_session = session_for_canvas(dynamic_cast<CanvasWidget*>(document_tabs_->widget(index)));
  if (keep_session == nullptr) {
    return;
  }
  // Sessions, not tab indexes: "others" includes documents floated into their own
  // windows. Ids are snapshotted first because closing mutates sessions_.
  const auto keep_id = keep_session->session_id;
  std::vector<std::int64_t> other_ids;
  other_ids.reserve(sessions_.size());
  for (const auto& candidate : sessions_) {
    if (candidate->session_id != keep_id) {
      other_ids.push_back(candidate->session_id);
    }
  }
  for (auto other_it = other_ids.rbegin(); other_it != other_ids.rend(); ++other_it) {
    auto* candidate = session_with_id(*other_it);
    if (candidate != nullptr && !close_document_session(*candidate)) {
      break;
    }
  }
  if (auto* keep = session_with_id(keep_id); keep != nullptr) {
    activate_document_session(*keep);
  }
}

void MainWindow::close_all_document_tabs() {
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
  std::vector<std::int64_t> session_ids;
  session_ids.reserve(sessions_.size());
  for (const auto& candidate : sessions_) {
    session_ids.push_back(candidate->session_id);
  }
  for (auto id_it = session_ids.rbegin(); id_it != session_ids.rend(); ++id_it) {
    auto* candidate = session_with_id(*id_it);
    if (candidate != nullptr && !close_document_session(*candidate)) {
      break;
    }
  }
}

void MainWindow::float_document_session(DocumentSession& target_session) {
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
  if (target_session.float_window != nullptr) {
    activate_document_session(target_session);
    return;
  }
  auto* canvas = target_session.canvas;
  if (canvas == nullptr || document_tabs_ == nullptr) {
    return;
  }
  // A live inline text edit commits into the ACTIVE session; settle it before
  // its editor's canvas is reparented out from under it.
  if (canvas == canvas_) {
    finish_active_text_editor();
  }
  const auto source_size = canvas->size();
  target_session.floated_from_tab_index = document_tabs_->indexOf(canvas);
  if (target_session.floated_from_tab_index >= 0) {
    // Blocked: removeTab's currentChanged would activate the neighbor tab and
    // stash/apply tool state against it mid-float. The float window activation
    // below is the single activation this operation performs.
    const QSignalBlocker blocker(document_tabs_);
    document_tabs_->removeTab(target_session.floated_from_tab_index);
  }
  int existing_floats = 0;
  for (const auto& candidate : sessions_) {
    if (candidate->float_window != nullptr) {
      ++existing_floats;
    }
  }
  auto* float_window = new DocumentFloatWindow(*this, canvas);
  target_session.float_window = float_window;
  // Registered shortcuts stay window-scoped (an application scope would leak
  // into modal dialogs); associating the actions with the float makes Qt's
  // WindowShortcut context match while the float is the active window. All
  // register_hotkey calls happen during construction, so this snapshot is
  // complete; a hotkey registered after floats exist would need registry-level
  // window association instead.
  for (const auto& command : hotkey_registry_.commands()) {
    if (command.action != nullptr) {
      float_window->addAction(command.action);
    }
  }
  auto float_size = source_size.expandedTo(QSize(320, 240));
  auto float_position =
      document_workspace_global().topLeft() + QPoint(24 + 32 * existing_floats, 24 + 32 * existing_floats);
  if (auto* target_screen = screen(); target_screen != nullptr) {
    const auto available = target_screen->availableGeometry();
    float_size = float_size.boundedTo(available.size() * 0.9);
    // std::max keeps clamp's lo <= hi even on degenerate screen geometry
    // (remote desktop, monitor hot-unplug), where lo > hi is undefined behavior.
    float_position.setX(std::clamp(float_position.x(), available.left(),
                                   std::max(available.left(), available.right() - float_size.width())));
    float_position.setY(std::clamp(float_position.y(), available.top(),
                                   std::max(available.top(), available.bottom() - float_size.height())));
  }
  float_window->resize(float_size);
  float_window->move(float_position);
  refresh_document_tab_titles();
  // Shows/raises the float and activates its canvas directly: offscreen and
  // headless platforms may never deliver the WindowActivate event.
  activate_document_session(target_session);
}

void MainWindow::tear_off_document_tab(int index, QPoint global_position) {
  if (document_tabs_ == nullptr) {
    return;
  }
  auto* target_session = session_for_canvas(dynamic_cast<CanvasWidget*>(document_tabs_->widget(index)));
  if (target_session == nullptr) {
    return;
  }
  if (auto* tab_bar = document_tabs_->tabBar(); tab_bar != nullptr) {
    // End QTabBar's internal move-drag cleanly before its tab disappears.
    QMouseEvent release(QEvent::MouseButtonRelease, tab_bar->mapFromGlobal(global_position),
                        global_position, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(tab_bar, &release);
  }
  float_document_session(*target_session);
  auto* float_window = target_session->float_window;
  if (float_window == nullptr) {
    return;  // refused (preview-dialog edit lock)
  }
  // Title bar under the cursor, then let the OS continue the drag in the same
  // motion. startSystemMove is a no-op on platforms without it (offscreen);
  // the window then simply stays where it was placed.
  float_window->move(global_position - QPoint(float_window->width() / 4, 12));
  if (auto* handle = float_window->windowHandle(); handle != nullptr) {
    handle->startSystemMove();
  }
}

void MainWindow::dock_document_session(DocumentSession& target_session) {
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
  auto* float_window = target_session.float_window;
  if (float_window == nullptr || document_tabs_ == nullptr) {
    return;
  }
  if (target_session.canvas == canvas_) {
    finish_active_text_editor();
  }
  auto* canvas = float_window->take_canvas();
  target_session.float_window = nullptr;
  float_window->hide();
  float_window->deleteLater();
  const auto tab_index = std::clamp(target_session.floated_from_tab_index, 0, document_tabs_->count());
  target_session.floated_from_tab_index = -1;
  const auto was_active = canvas == canvas_;
  {
    // Blocked for the same reason as float_document_session: inserting must not
    // route activation through whichever tab happens to become current.
    const QSignalBlocker blocker(document_tabs_);
    document_tabs_->insertTab(tab_index, canvas, session_display_title(target_session));
    if (was_active) {
      document_tabs_->setCurrentIndex(tab_index);
    }
  }
  if (was_active) {
    activate_document_canvas(canvas);
  }
  refresh_document_tab_titles();
  update_document_action_state();
}

void MainWindow::float_active_document() {
  if (auto* target_session = active_session(); target_session != nullptr) {
    float_document_session(*target_session);
  }
}

void MainWindow::dock_active_document() {
  if (auto* target_session = active_session(); target_session != nullptr) {
    dock_document_session(*target_session);
  }
}

void MainWindow::consolidate_all_to_tabs() {
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
  auto* active_canvas = canvas_;
  for (const auto& target_session : sessions_) {
    if (target_session->float_window != nullptr) {
      dock_document_session(*target_session);
    }
  }
  if (auto* active_session = session_for_canvas(active_canvas); active_session != nullptr) {
    activate_document_session(*active_session);
  }
}

void MainWindow::float_all_documents() {
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
  auto* active_canvas = canvas_;
  for (const auto& target_session : sessions_) {
    if (target_session->float_window == nullptr) {
      float_document_session(*target_session);
    }
  }
  if (auto* target_session = session_for_canvas(active_canvas); target_session != nullptr) {
    activate_document_session(*target_session);
  }
}

namespace {

// Places a top-level window so its FRAME fills `cell` (setGeometry positions the
// client area; without the frame compensation, tiled windows overlap by their
// title-bar and border widths). Offscreen platforms report no frame, so the
// adjustment degrades to setGeometry(cell).
void set_frame_geometry(QWidget& window, const QRect& cell) {
  const auto frame = window.frameGeometry();
  const auto client = window.geometry();
  const QPoint top_left_delta = client.topLeft() - frame.topLeft();
  const QSize frame_extra(frame.width() - client.width(), frame.height() - client.height());
  window.setGeometry(QRect(cell.topLeft() + top_left_delta,
                           QSize(std::max(120, cell.width() - frame_extra.width()),
                                 std::max(90, cell.height() - frame_extra.height()))));
}

}  // namespace

QRect MainWindow::document_workspace_global() const {
  if (document_tabs_ != nullptr && !isMinimized()) {
    const QRect workspace(document_tabs_->mapToGlobal(QPoint(0, 0)), document_tabs_->size());
    if (workspace.width() >= 200 && workspace.height() >= 150) {
      return workspace;
    }
  }
  return screen() != nullptr ? screen()->availableGeometry() : QRect(0, 0, 1280, 800);
}

void MainWindow::tile_float_windows() {
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
  if (sessions_.empty()) {
    return;
  }
  if (isMinimized()) {
    // Arranging documents around an invisible workspace is meaningless; restore
    // first so the workspace rect below is real.
    showNormal();
  }
  float_all_documents();
  std::vector<DocumentFloatWindow*> floats;
  floats.reserve(sessions_.size());
  for (const auto& target_session : sessions_) {
    if (target_session->float_window != nullptr) {
      floats.push_back(target_session->float_window);
    }
  }
  if (floats.empty()) {
    return;
  }
  const auto available = document_workspace_global();
  const auto count = static_cast<int>(floats.size());
  const auto columns = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(count))));
  const auto rows = (count + columns - 1) / columns;
  const auto cell_width = available.width() / columns;
  const auto cell_height = available.height() / rows;
  for (int index = 0; index < count; ++index) {
    const auto column = index % columns;
    const auto row = index / columns;
    const QRect cell(available.left() + column * cell_width, available.top() + row * cell_height, cell_width,
                     cell_height);
    set_frame_geometry(*floats[static_cast<std::size_t>(index)], cell);
    floats[static_cast<std::size_t>(index)]->raise();
  }
  // Keep the active document's window on top of the arrangement.
  if (auto* target_session = active_session(); target_session != nullptr) {
    activate_document_session(*target_session);
  }
}

void MainWindow::cascade_float_windows() {
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
  if (sessions_.empty()) {
    return;
  }
  if (isMinimized()) {
    showNormal();
  }
  float_all_documents();
  const auto available = document_workspace_global();
  const QSize window_size(available.width() * 3 / 5, available.height() * 3 / 5);
  constexpr int kCascadeStep = 36;
  int step = 0;
  for (const auto& target_session : sessions_) {
    auto* float_window = target_session->float_window;
    if (float_window == nullptr) {
      continue;
    }
    auto position = available.topLeft() + QPoint(kCascadeStep * step, kCascadeStep * step);
    // Wrap back to the origin once the stagger would push the window off screen.
    if (position.x() + window_size.width() > available.right() ||
        position.y() + window_size.height() > available.bottom()) {
      step = 0;
      position = available.topLeft();
    }
    set_frame_geometry(*float_window, QRect(position, window_size));
    float_window->raise();
    ++step;
  }
  if (auto* target_session = active_session(); target_session != nullptr) {
    activate_document_session(*target_session);
  }
}

bool MainWindow::handle_float_window_close_request(DocumentFloatWindow* window) {
  auto* target_session = session_for_float_window(window);
  if (target_session == nullptr) {
    return true;
  }
  return close_document_session(*target_session);
}

void MainWindow::handle_float_window_activated(DocumentFloatWindow* window) {
  if (shutting_down_) {
    return;
  }
  auto* target_session = session_for_float_window(window);
  if (target_session == nullptr || target_session->canvas == nullptr) {
    return;
  }
  if (target_session->canvas != canvas_) {
    activate_document_canvas(target_session->canvas);
  }
  if (target_session->canvas == canvas_) {
    // Windows resets the cursor on window re-activation; same fix as the main
    // window's ActivationChange handling.
    target_session->canvas->refresh_tool_cursor();
  }
}

void MainWindow::handle_float_window_drag_moved(DocumentFloatWindow* window) {
  if (shutting_down_ || window == nullptr || !window->isVisible() || preview_dialog_edit_locked()) {
    set_float_dock_highlight_visible(false);
    return;
  }
  // Only a USER drag holds the left button through the move; programmatic moves
  // (creation cascade, Tile/Cascade) never arm the dock check.
  if ((QGuiApplication::mouseButtons() & Qt::LeftButton) == 0) {
    set_float_dock_highlight_visible(false);
    return;
  }
  auto* target_session = session_for_float_window(window);
  if (target_session == nullptr) {
    set_float_dock_highlight_visible(false);
    return;
  }
  float_dock_candidate_session_id_ = target_session->session_id;
  if (float_dock_check_timer_ == nullptr) {
    float_dock_check_timer_ = new QTimer(this);
    float_dock_check_timer_->setSingleShot(true);
    float_dock_check_timer_->setInterval(150);
    connect(float_dock_check_timer_, &QTimer::timeout, this, [this] {
      auto* candidate = session_with_id(float_dock_candidate_session_id_);
      if (candidate == nullptr || candidate->float_window == nullptr) {
        set_float_dock_highlight_visible(false);
        return;
      }
      if ((QGuiApplication::mouseButtons() & Qt::LeftButton) != 0) {
        // Still dragging (holding still); keep the affordance as the last move
        // left it and check again once the stream of moveEvents stops.
        float_dock_check_timer_->start();
        return;
      }
      set_float_dock_highlight_visible(false);
      maybe_dock_float_at(candidate->float_window, QCursor::pos());
    });
  }
  float_dock_check_timer_->start();
  // Live affordance: the tab strip lights while releasing here would dock.
  update_float_dock_highlight(QCursor::pos());
}

void MainWindow::update_float_dock_highlight(QPoint global_position) {
  set_float_dock_highlight_visible(float_dock_zone_global().contains(global_position));
}

void MainWindow::set_float_dock_highlight_visible(bool visible) {
  if (!visible) {
    if (float_dock_highlight_ != nullptr) {
      float_dock_highlight_->hide();
    }
    return;
  }
  if (document_tabs_ == nullptr) {
    return;
  }
  if (float_dock_highlight_ == nullptr) {
    float_dock_highlight_ = new QWidget(document_tabs_);
    float_dock_highlight_->setObjectName(QStringLiteral("floatDockHighlight"));
    // Purely a visual affordance: it must never intercept the pointer.
    float_dock_highlight_->setAttribute(Qt::WA_TransparentForMouseEvents);
    const auto accent = palette().color(QPalette::Highlight);
    float_dock_highlight_->setStyleSheet(
        QStringLiteral("#floatDockHighlight { background: rgba(%1, %2, %3, 70); border: 2px solid rgb(%1, %2, %3); }")
            .arg(accent.red())
            .arg(accent.green())
            .arg(accent.blue()));
  }
  const auto zone = float_dock_zone_global();
  float_dock_highlight_->setGeometry(QRect(document_tabs_->mapFromGlobal(zone.topLeft()), zone.size()));
  float_dock_highlight_->raise();
  float_dock_highlight_->show();
}

QRect MainWindow::float_dock_zone_global() const {
  if (document_tabs_ == nullptr) {
    return QRect();
  }
  if (auto* tab_bar = document_tabs_->tabBar(); tab_bar != nullptr && tab_bar->count() > 0) {
    const auto top_left = tab_bar->mapToGlobal(QPoint(0, 0));
    // Full tab-widget width: a drop right of the last tab should still dock.
    return QRect(document_tabs_->mapToGlobal(QPoint(0, 0)).x(), top_left.y() - 8, document_tabs_->width(),
                 tab_bar->height() + 16);
  }
  // No tabs left (everything floated): the tab widget's top strip is the target.
  return QRect(document_tabs_->mapToGlobal(QPoint(0, 0)), QSize(document_tabs_->width(), 48));
}

void MainWindow::maybe_dock_float_at(DocumentFloatWindow* window, QPoint global_position) {
  if (preview_dialog_edit_locked()) {
    return;
  }
  if (!float_dock_zone_global().contains(global_position)) {
    return;
  }
  if (auto* target_session = session_for_float_window(window); target_session != nullptr) {
    dock_document_session(*target_session);
  }
}

MainWindow::DocumentSession* MainWindow::session_for_float_window(DocumentFloatWindow* window) noexcept {
  if (window == nullptr) {
    return nullptr;
  }
  for (const auto& candidate : sessions_) {
    if (candidate->float_window == window) {
      return candidate.get();
    }
  }
  return nullptr;
}

CanvasWidget* MainWindow::fallback_active_canvas() noexcept {
  if (document_tabs_ != nullptr) {
    if (auto* canvas = dynamic_cast<CanvasWidget*>(document_tabs_->currentWidget()); canvas != nullptr) {
      return canvas;
    }
  }
  for (auto candidate = sessions_.rbegin(); candidate != sessions_.rend(); ++candidate) {
    if ((*candidate)->float_window != nullptr && (*candidate)->canvas != nullptr) {
      return (*candidate)->canvas;
    }
  }
  return nullptr;
}

bool MainWindow::any_document_floated() const noexcept {
  return std::any_of(sessions_.begin(), sessions_.end(),
                     [](const auto& candidate) { return candidate->float_window != nullptr; });
}

void MainWindow::show_document_tab_context_menu(const QPoint& position) {
  if (document_tabs_ == nullptr) {
    return;
  }
  auto* tab_bar = document_tabs_->findChild<QTabBar*>();
  if (tab_bar == nullptr) {
    return;
  }

  const auto tab_index = tab_bar->tabAt(position);
  if (tab_index < 0 || tab_index >= document_tabs_->count()) {
    return;
  }

  QMenu menu(tab_bar);
  menu.setObjectName(QStringLiteral("documentTabContextMenu"));
  auto* close_action = menu.addAction(tr("Close"));
  auto* close_others_action = menu.addAction(tr("Close Others"));
  auto* close_all_action = menu.addAction(tr("Close All"));
  auto* float_action = menu.addAction(tr("Float in Window"));
  menu.addSeparator();
  auto* reopen_action = menu.addAction(tr("Reopen Document"));
#if defined(Q_OS_WIN)
  auto* reveal_action = menu.addAction(tr("Reveal in Explorer"));
#elif defined(Q_OS_MACOS)
  auto* reveal_action = menu.addAction(tr("Reveal in Finder"));
#else
  auto* reveal_action = menu.addAction(tr("Show in File Manager"));
#endif
  auto* copy_path_action = menu.addAction(tr("Copy File Path"));
  close_action->setObjectName(QStringLiteral("documentTabCloseAction"));
  close_others_action->setObjectName(QStringLiteral("documentTabCloseOthersAction"));
  close_all_action->setObjectName(QStringLiteral("documentTabCloseAllAction"));
  float_action->setObjectName(QStringLiteral("documentTabFloatAction"));
  reopen_action->setObjectName(QStringLiteral("documentTabReopenAction"));
  reveal_action->setObjectName(QStringLiteral("documentTabRevealAction"));
  copy_path_action->setObjectName(QStringLiteral("documentTabCopyPathAction"));
  close_others_action->setEnabled(sessions_.size() > 1);
  // The file actions need a disk path; never-saved documents (and embedded
  // smart-object child tabs) have none.
  const auto* target_session =
      session_for_canvas(dynamic_cast<CanvasWidget*>(document_tabs_->widget(tab_index)));
  const auto document_path = target_session != nullptr ? target_session->path : QString();
  const auto session_id = target_session != nullptr ? target_session->session_id : 0;
  const bool has_path = !document_path.isEmpty();
  reopen_action->setEnabled(has_path);
  reveal_action->setEnabled(has_path);
  copy_path_action->setEnabled(has_path);

  connect(close_action, &QAction::triggered, this, [this, tab_index] { close_document_tab(tab_index); });
  connect(close_others_action, &QAction::triggered, this,
          [this, tab_index] { close_other_document_tabs(tab_index); });
  connect(close_all_action, &QAction::triggered, this, [this] { close_all_document_tabs(); });
  connect(float_action, &QAction::triggered, this, [this, tab_index] {
    if (document_tabs_ == nullptr) {
      return;
    }
    if (auto* target_session = session_for_canvas(dynamic_cast<CanvasWidget*>(document_tabs_->widget(tab_index)));
        target_session != nullptr) {
      float_document_session(*target_session);
    }
  });
  connect(reopen_action, &QAction::triggered, this, [this, session_id] {
    if (auto* reopen_session = session_with_id(session_id); reopen_session != nullptr) {
      reopen_document_session(*reopen_session);
    }
  });
  connect(reveal_action, &QAction::triggered, this, [this, document_path] {
    reveal_path_in_file_explorer(document_path, /*is_file=*/true);
  });
  connect(copy_path_action, &QAction::triggered, this, [this, document_path] {
    QApplication::clipboard()->setText(QDir::toNativeSeparators(document_path));
    statusBar()->showMessage(tr("File path copied"));
  });
  menu.exec(tab_bar->mapToGlobal(position));
}

bool MainWindow::confirm_close_session(DocumentSession& target_session) {
  if (!session_is_modified(target_session)) {
    return true;
  }

  const auto title = target_session.title.isEmpty() ? tr("Untitled") : target_session.title;
  const auto answer = show_warning_message(this, tr("Save changes?"),
                                           tr("Save changes to %1 before closing?").arg(title),
                                           QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
                                           QMessageBox::Yes, QStringLiteral("saveChangesMessageBox"));
  if (answer == QMessageBox::Cancel) {
    return false;
  }
  if (answer == QMessageBox::No) {
    return true;
  }
  return maybe_save_session(target_session);
}

bool MainWindow::maybe_save_session(DocumentSession& target_session) {
  const auto found = std::find_if(sessions_.begin(), sessions_.end(), [&target_session](const auto& candidate) {
    return candidate.get() == &target_session;
  });
  if (found == sessions_.end()) {
    return false;
  }

  // save_document() saves the ACTIVE session, so the target must be activated
  // first (raising its float window when it lives in one).
  activate_document_session(target_session);
  return save_document() && !session_is_modified(target_session);
}

bool MainWindow::session_is_modified(const DocumentSession& target_session) const noexcept {
  return target_session.revision != target_session.saved_revision;
}

QString MainWindow::session_display_title(const DocumentSession& target_session) const {
  auto title = target_session.title.isEmpty() ? tr("Untitled") : target_session.title;
  if (session_is_modified(target_session)) {
    title.append(QStringLiteral("*"));
  }
  return title;
}

void MainWindow::refresh_document_tab_titles() {
  if (document_tabs_ == nullptr) {
    return;
  }
  for (int index = 0; index < document_tabs_->count(); ++index) {
    auto* canvas = dynamic_cast<CanvasWidget*>(document_tabs_->widget(index));
    const auto* target_session = session_for_canvas(canvas);
    if (target_session == nullptr) {
      continue;
    }
    document_tabs_->setTabText(index, session_display_title(*target_session));
  }
  for (const auto& target_session : sessions_) {
    if (target_session->float_window != nullptr) {
      target_session->float_window->setWindowTitle(session_display_title(*target_session));
    }
  }
  refresh_document_window_title();
}

void MainWindow::refresh_document_window_title() {
  if (!has_active_document()) {
#ifdef Q_OS_MACOS
    setWindowFilePath(QString());
    setWindowModified(false);
#endif
    setWindowTitle(QStringLiteral("Patchy"));
    return;
  }

  const auto& active_session = session();
  auto title = active_session.title.isEmpty() ? tr("Untitled") : active_session.title;
  const bool modified = session_is_modified(active_session);
#ifdef Q_OS_MACOS
  // macOS titlebar conventions: windowFilePath drives the document proxy icon and
  // Cmd-click path menu (empty for never-saved documents), windowModified draws the
  // dirty dot in the close button, and Qt substitutes the [*] placeholder per-platform.
  setWindowFilePath(active_session.path);
  setWindowTitle(title + QStringLiteral("[*]"));
  setWindowModified(modified);
#else
  if (modified) {
    title.append(QStringLiteral("*"));
  }
  setWindowTitle(title);
#endif
}

void MainWindow::set_session_saved(DocumentSession& target_session) {
  target_session.saved_revision = target_session.revision;
  refresh_document_tab_titles();  update_undo_redo_actions();
  refresh_document_info();
}

void MainWindow::mark_session_modified(DocumentSession& target_session) {
  ++target_session.revision;
  refresh_document_tab_titles();
  update_undo_redo_actions();
  refresh_document_info();
}

MainWindow::DocumentSession* MainWindow::session_for_canvas(CanvasWidget* canvas) noexcept {
  if (canvas == nullptr) {
    return nullptr;
  }
  const auto found = std::find_if(sessions_.begin(), sessions_.end(), [canvas](const auto& candidate) {
    return candidate->canvas == canvas;
  });
  return found == sessions_.end() ? nullptr : found->get();
}

const MainWindow::DocumentSession* MainWindow::session_for_canvas(CanvasWidget* canvas) const noexcept {
  if (canvas == nullptr) {
    return nullptr;
  }
  const auto found = std::find_if(sessions_.begin(), sessions_.end(), [canvas](const auto& candidate) {
    return candidate->canvas == canvas;
  });
  return found == sessions_.end() ? nullptr : found->get();
}

MainWindow::DocumentSession* MainWindow::session_with_id(std::int64_t session_id) noexcept {
  const auto found = std::find_if(sessions_.begin(), sessions_.end(), [session_id](const auto& candidate) {
    return candidate->session_id == session_id;
  });
  return found == sessions_.end() ? nullptr : found->get();
}

std::vector<MainWindow::DocumentSession*> MainWindow::open_smart_object_child_sessions(
    std::int64_t parent_session_id) {
  std::vector<DocumentSession*> children;
  for (const auto& candidate : sessions_) {
    if (candidate->smart_object_link.has_value() &&
        candidate->smart_object_link->parent_session_id == parent_session_id) {
      children.push_back(candidate.get());
    }
  }
  return children;
}

void MainWindow::activate_document_session(DocumentSession& target_session) {
  if (target_session.float_window != nullptr) {
    if (target_session.float_window->isMinimized()) {
      // show() is a no-op on a minimized window; the user must SEE the document
      // this activation is about (e.g. its save prompt during Close All).
      target_session.float_window->showNormal();
    } else {
      target_session.float_window->show();
    }
    target_session.float_window->raise();
    target_session.float_window->activateWindow();
  } else if (document_tabs_ != nullptr) {
    if (const auto index = document_tabs_->indexOf(target_session.canvas); index >= 0) {
      // Blocked so the explicit call below is the single activation (an unblocked
      // setCurrentIndex would run the whole panel-refresh pass twice).
      const QSignalBlocker blocker(document_tabs_);
      document_tabs_->setCurrentIndex(index);
    }
  }
  activate_document_canvas(target_session.canvas);
}

Document& MainWindow::document() {
  auto* target_session = active_session();
  if (target_session == nullptr) {
    throw std::logic_error("No active document");
  }
  return target_session->document;
}

const Document& MainWindow::document() const {
  const auto* target_session = active_session();
  if (target_session == nullptr) {
    throw std::logic_error("No active document");
  }
  return target_session->document;
}

MainWindow::DocumentSession& MainWindow::session() {
  auto* target_session = active_session();
  if (target_session == nullptr) {
    throw std::logic_error("No active document session");
  }
  return *target_session;
}

const MainWindow::DocumentSession& MainWindow::session() const {
  const auto* target_session = active_session();
  if (target_session == nullptr) {
    throw std::logic_error("No active document session");
  }
  return *target_session;
}

bool MainWindow::has_active_document() const noexcept {
  return active_session() != nullptr;
}

}  // namespace patchy::ui
