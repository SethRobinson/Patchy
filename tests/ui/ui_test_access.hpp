#pragma once

// MainWindowTestAccess, moved verbatim from tests/ui_visual_tests.cpp. It is
// befriended BY NAME in src/ui/main_window.hpp (friend class MainWindowTestAccess
// inside namespace patchy::ui), so the qualified name
// patchy::ui::MainWindowTestAccess must not change.

#include "ui/custom_shape_library.hpp"
#include "ui/divide_photos_dialog.hpp"
#include "ui/document_float_window.hpp"
#include "ui/export_documents_folder_dialog.hpp"
#include "ui/main_window.hpp"
#include "ui/panel_column.hpp"

#include <QPoint>
#include <QRect>
#include <QString>
#include <QStringList>

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace patchy::ui {

class MainWindowTestAccess {
public:
  static Document& document(MainWindow& window) {
    return window.document();
  }

  static std::vector<LayerId> selected_layer_ids(const MainWindow& window) {
    return window.selected_layer_ids();
  }

  // The High Quality Object Selection model offer (main_window_ai_selection.cpp).
  static bool offer_high_quality_object_selection_model(MainWindow& window) {
    return window.offer_high_quality_object_selection_model();
  }

  static void show_preferences(MainWindow& window) { window.show_preferences(); }
  // Preferences > Watermarks' checks on open still running (docs/watermarks.md).
  static std::size_t watermark_checks_running(MainWindow& window) { return window.watermark_checks_.size(); }
  static int document_tab_count(MainWindow& window) { return window.document_tabs_->count(); }

  // The one-time High Quality offer stays away offscreen; a test that drives it opts in.
  static void allow_high_quality_offer_offscreen(MainWindow& window) {
    window.high_quality_offer_in_offscreen_tests_ = true;
  }

  // One-click update seams (docs/auto-update.md): treat this copy as installer-made (or
  // not), download into a scratch folder, and capture the installer launch instead of
  // starting a process.
  static void set_self_update_override(MainWindow& window, std::optional<bool> value) {
    window.self_update_override_for_tests_ = value;
  }
  static void set_update_download_folder(MainWindow& window, const QString& folder) {
    window.update_download_folder_for_tests_ = folder;
  }
  static void set_update_installer_launcher(MainWindow& window,
                                            std::function<UpdateLaunchResult(const UpdateInstallerLaunch&)> launcher) {
    window.update_installer_launcher_for_tests_ = std::move(launcher);
  }
  static bool update_install_pending(const MainWindow& window) { return window.pending_update_install_.has_value(); }
  // Captures the application exit a settled update close requests instead of ending the
  // suite's event loop.
  static void set_application_exit_hook(MainWindow& window, std::function<void()> hook) {
    window.application_exit_hook_for_tests_ = std::move(hook);
  }

  // Help > Add Patchy to Applications Menu (GitHub issue 226): the AppImage's own share
  // folder (a test copy of the shipped desktop file and icons), which also keeps the
  // desktop cache tools from running.
  static void set_appimage_bundled_share(MainWindow& window, const QString& share) {
    window.appimage_bundled_share_for_tests_ = share;
  }
  static bool offer_appimage_menu_entry(MainWindow& window) { return window.offer_appimage_menu_entry(); }

  // The historical startup document (startup now opens an empty workspace):
  // show_window creates one so pre-existing tests keep their canvas.
  static std::uint64_t gmic_draft_previews_shown(const MainWindow& window) {
    return window.gmic_draft_previews_shown_;
  }
  // True once a full-quality preview of the filter has completed this session.
  static bool gmic_full_preview_finished(const MainWindow& window, const std::string& filter_id) {
    return window.gmic_full_ms_per_megapixel_.count(filter_id) != 0;
  }
  static Rect gmic_last_draft_bounds(const MainWindow& window) {
    return window.gmic_last_draft_bounds_;
  }

  static void create_default_document(MainWindow& window) {
    window.reset_document(1024, 768, Qt::white, QStringLiteral("New document"));
  }

  static void create_document(MainWindow& window, std::int32_t width, std::int32_t height) {
    window.reset_document(width, height, Qt::white, QStringLiteral("New document"));
  }

  static CanvasWidget* canvas(MainWindow& window) {
    return window.canvas_;
  }

  static void open_document_path(MainWindow& window, QString path) {
    window.open_document_path(std::move(path));
  }

  static int open_folder_path(MainWindow& window, const QString& directory) {
    return window.open_folder_path(directory);
  }

  static bool accept_open_file_drag(MainWindow& window, QDropEvent* event) {
    return window.accept_open_file_drag(event);
  }

  static bool open_dropped_files(MainWindow& window, QDropEvent* event) {
    return window.open_dropped_files(event);
  }

  static std::optional<QStringList> export_document_sessions_to_folder(
      MainWindow& window, const std::vector<std::int64_t>& session_ids, const QString& folder,
      const QString& extension, const ImageSequenceNaming& naming, ExportDocumentsExistingFiles existing_files) {
    return window.export_document_sessions_to_folder(session_ids, folder, extension, naming, existing_files);
  }

  // File > Export > Artboards to Files without its dialog (docs/artboards.md).
  static std::optional<QStringList> export_artboards_to_folder(MainWindow& window, const std::vector<LayerId>& ids,
                                                               const QString& folder, const QString& extension,
                                                               const ImageSequenceNaming& naming,
                                                               ExportDocumentsExistingFiles existing_files) {
    return window.export_artboards_to_folder(std::as_const(window.document()), ids, folder, extension, naming,
                                             existing_files);
  }

  // The Layers panel selection, as a click (and Ctrl-clicks) would make it.
  static void select_layers(MainWindow& window, const std::vector<LayerId>& ids, LayerId active) {
    window.select_layers_in_layer_list(ids, active);
  }

  static std::int64_t session_id(MainWindow& window, std::size_t index) {
    return window.sessions_.at(index)->session_id;
  }

  static void refresh_document_info(MainWindow& window) {
    window.refresh_document_info();
  }

  // The ruler unit preference (view/rulerUnits) as the Preferences dialog or the
  // ruler right-click would set it; it persists, so pair it with a
  // SettingsValueRestorer.
  static void set_ruler_unit(MainWindow& window, MeasurementUnit unit) {
    window.set_ruler_unit_preference(unit);
  }

  static MeasurementUnit ruler_unit(const MainWindow& window) {
    return window.ruler_unit_;
  }

  static void levels_dialog(MainWindow& window) {
    window.levels_dialog();
  }

  static bool apply_text_warp(MainWindow& window, Layer& layer, const TextWarp& warp) {
    return window.apply_text_warp_to_layer(layer, warp);
  }

  static void request_warp_text_dialog(MainWindow& window) {
    window.request_warp_text_dialog();
  }

  static void toggle_text_bold_face(MainWindow& window) {
    window.toggle_text_bold_face();
  }

  // Deterministic offscreen substitute for a Type-tool canvas click: the canvas
  // press/release pair funnels into exactly this call, but synthetic clicks sent
  // while another window (e.g. a non-modal dialog) is active lose the in-flight
  // drag state to a focus bounce the offscreen platform invents.
  static std::optional<bool> resolve_pdf_layer_choice(MainWindow& window, bool for_export) {
    return window.resolve_pdf_layer_choice(for_export, /*allow_prompt*/ true);
  }
  static void add_text_at(MainWindow& window, QPoint document_point) {
    window.add_text_at(document_point, QRect());
  }
  static void lay_out_raster_less_imported_text(MainWindow& window, Document& document) {
    window.lay_out_raster_less_imported_text(document);
  }

  static void record_text_layout_metrics_for_reopened_text(MainWindow& window, Document& document) {
    window.record_text_layout_metrics_for_reopened_text(document);
  }

  static void edit_active_shape_appearance(MainWindow& window) {
    window.edit_active_shape_appearance();
  }

  // Options-bar paint mirrors (the popup pickers' backing state) and the
  // live-edit application, callable without driving the modal manager dialogs.
  static patchy::VectorFill& current_vector_fill(MainWindow& window) {
    return window.current_vector_fill_;
  }

  static patchy::VectorFill& current_vector_stroke_paint(MainWindow& window) {
    return window.current_vector_stroke_paint_;
  }

  // What the next shape gets from the options bar (stroke switch and width included).
  static patchy::VectorShapeContent current_shape_appearance(const MainWindow& window) {
    return window.current_shape_appearance_content();
  }

  static bool apply_options_bar_appearance(MainWindow& window) {
    return window.apply_options_bar_appearance_to_active_shape();
  }

  static void update_vector_swatch_icons(MainWindow& window) {
    window.update_vector_swatch_icons();
  }

  static CustomShapeLibrary& custom_shape_library(MainWindow& window) {
    return window.custom_shape_library();
  }

  // Holds back the pattern preload of windows constructed while set (released when the
  // window asks for its library, is destroyed, or the hold is cleared), so a test can drive
  // a window before the startup worker lands (docs/startup-performance.md).
  static void hold_pattern_preload(bool hold) { MainWindow::hold_pattern_preload_for_tests(hold); }
  static bool pattern_library_adopted(const MainWindow& window) { return window.pattern_library_ != nullptr; }

  static ImageSaveOptions image_save_defaults(MainWindow& window) {
    return window.image_save_defaults_for_document();
  }

  static StressReport run_stress_scenario(MainWindow& window, const StressTestOptions& options) {
    return window.run_stress_test_scenario(options);
  }

  static void import_from_scanner(MainWindow& window) {
    window.import_from_scanner();
  }

  static void photocopy_from_scanner(MainWindow& window) {
    window.photocopy_from_scanner();
  }

  static void import_and_divide_from_scanner(MainWindow& window) {
    window.import_and_divide_from_scanner();
  }

  static void divide_current_document_photos(MainWindow& window) {
    window.divide_current_document_photos();
  }

  static std::optional<QStringList> save_divided_photos_to_folder(
      MainWindow& window, const std::vector<PixelBuffer>& photos,
      const DocumentPrintSettings& print_settings, const QString& folder, const QString& prefix,
      const QString& extension, DividePhotosExistingFiles existing_files) {
    return window.save_divided_photos_to_folder(photos, print_settings, folder, prefix, extension,
                                                existing_files);
  }

  static void set_ruler_unit_preference(MainWindow& window, MeasurementUnit unit) {
    window.set_ruler_unit_preference(unit);
  }

  static void open_smart_object_contents(MainWindow& window) {
    window.open_smart_object_contents();
  }

  static void replace_smart_object_contents_with_path(MainWindow& window, const QString& path) {
    window.replace_smart_object_contents_with_path(path);
  }

  static void place_embedded_file_with_path(MainWindow& window, const QString& path) {
    window.place_embedded_file_with_path(path);
  }

  // File > Place Linked without the file dialog (docs/smart-object-editing.md).
  static void place_linked_file_with_path(MainWindow& window, const QString& path) {
    window.place_linked_file_with_path(path);
  }

  // File > Import > Files as Layers without the file dialog (docs/import.md).
  static void import_files_as_layers_with_paths(MainWindow& window, const QStringList& paths) {
    window.import_files_as_layers_with_paths(paths);
  }

  // Image > Canvas Size with `frame` as the new canvas (the dialog's apply).
  static void apply_canvas_size(MainWindow& window, Rect frame) {
    window.apply_canvas_size(frame, QColor(Qt::white), false, false, QStringLiteral("Canvas size"));
  }
  // Image > Image Size (the dialog's apply), Bicubic.
  static bool resize_document_image(MainWindow& window, int width, int height) {
    return window.resize_document_image(window.session(), width, height, ResampleMethod::Bicubic);
  }
  static void paste_clipboard(MainWindow& window) {
    window.paste_clipboard();
  }

  static bool define_custom_shape_from_svg_path(MainWindow& window, const QString& path) {
    return window.define_custom_shape_from_svg_path(path);
  }

  static void relink_smart_object_contents_with_path(MainWindow& window, const QString& path) {
    window.relink_smart_object_contents_with_path(path);
  }

  static std::optional<QString> relink_missing_smart_object_source(MainWindow& window, const std::string& uuid,
                                                                   const QString& path) {
    return window.relink_missing_smart_object_source(uuid, path);
  }

  static void show_missing_linked_files_dialog(MainWindow& window) {
    window.show_missing_linked_files_dialog(false);
  }

  static QString relink_dialog_initial_path(const MainWindow& window) {
    return window.relink_dialog_initial_path();
  }

  static void show_layer_context_menu(MainWindow& window, QPoint position) {
    window.show_layer_context_menu(position);
  }

  static void refresh_layer_ui(MainWindow& window) {
    window.refresh_layer_list();
    window.refresh_layer_controls();
  }

  static void set_right_dock_stack_width(MainWindow& window, int width) {
    window.set_right_dock_stack_width(width);
  }

  // The right-hand panel column (panel_column.hpp).
  static PanelColumn* panel_column(MainWindow& window) { return window.panel_column_; }
  // Opens, raises and expands a panel, as the Window menu does for a closed one.
  static void show_panel(MainWindow& window, const QString& id) { window.show_panel(id); }
  // A panel's section widget: the panel itself, or the whole tab group for a tab.
  static QWidget* panel_section(MainWindow& window, const QString& id) {
    return window.panel_column_ != nullptr ? window.panel_column_->section_widget(id) : nullptr;
  }
  // The saved panel layout without the offscreen gate (panel_layout_persists), so a test
  // can write one window's layout and read it into the next before that one is shown.
  static void write_panel_layout(MainWindow& window) { window.write_panel_layout(); }
  static void read_panel_layout(MainWindow& window) { window.read_panel_layout(); }
  static bool panel_layout_persists() { return MainWindow::panel_layout_persists(); }
  static int right_dock_pinned_width(MainWindow& window) { return window.right_dock_pinned_width_; }
  static bool right_dock_width_follows_content(MainWindow& window) {
    return window.right_dock_width_follows_minimum_;
  }

  // The width the column opens at and snaps to, the Layers footer on one row.
  static int right_dock_content_minimum_width(MainWindow& window) {
    window.update_right_dock_minimum_width();
    return window.right_dock_minimum_width_;
  }

  // The narrowest column width at which the Layers panel fits whole, its rows
  // wrapped (GitHub issue 199); below it the panel scrolls sideways.
  static int right_dock_compact_width(MainWindow& window) {
    window.update_right_dock_minimum_width();
    return window.right_dock_compact_width_;
  }

  // The client height that keeps the whole frame on the window's screen (GitHub issue 108).
  static int available_window_height(MainWindow& window) { return window.available_window_height(); }
  // Stands in a shorter screen than the offscreen platform's; 0 restores the real one.
  static void set_available_window_height(MainWindow& window, int height) {
    window.available_window_height_override_ = height > 0 ? height : -1;
  }
  static void fit_right_dock_floors_to_screen(MainWindow& window) { window.fit_right_dock_floors_to_screen(); }

  static void update_document_action_state(MainWindow& window) {
    window.update_document_action_state();
  }

  static void refresh_paths_panel(MainWindow& window) {
    window.refresh_paths_panel();
  }

  static void set_round_brush_session(MainWindow& window, BrushDynamics dynamics,
                                      double base_angle_degrees, double base_roundness) {
    window.round_brush_dynamics_ = std::move(dynamics);
    window.round_brush_base_angle_degrees_ = base_angle_degrees;
    window.round_brush_base_roundness_ = base_roundness;
  }

  static bool save_document(MainWindow& window) {
    return window.save_document();
  }

  static int cli_append_text_to_text_layers(MainWindow& window, const QString& suffix) {
    return window.cli_append_text_to_text_layers(suffix);
  }

  // No options: the save resolves format options itself (for PDF, the layer preference).
  static void refresh_proof_display(MainWindow& window) { window.refresh_proof_display(); }
  static bool save_document_to_path(MainWindow& window, QString path) {
    return window.save_document_to_path(std::move(path));
  }
  static bool save_document_to_path(MainWindow& window, QString path, ImageSaveOptions options) {
    return window.save_document_to_path(std::move(path), std::move(options));
  }
  static bool save_document_to_path(MainWindow& window, QString path, std::optional<ImageSaveOptions> options,
                                    MainWindow::SaveToPathPolicy policy) {
    return window.save_document_to_path(std::move(path), std::move(options), policy);
  }
  // A save whose flatten question the caller already answered (Save As's own dialogs).
  // An Export copy (export_copy policy): the session keeps its path and modified state.
  static bool export_document_to_path(MainWindow& window, QString path, ImageSaveOptions options) {
    return window.save_document_to_path(std::move(path), std::move(options),
                                        MainWindow::SaveToPathPolicy{/*flatten_confirmed=*/true, /*scripted=*/false,
                                                                     /*export_copy=*/true});
  }
  static bool save_document_to_path_flatten_confirmed(MainWindow& window, QString path, ImageSaveOptions options) {
    return window.save_document_to_path(std::move(path), std::move(options),
                                        MainWindow::SaveToPathPolicy{/*flatten_confirmed=*/true, /*scripted=*/false,
                                                                     /*export_copy=*/false});
  }

  static QString active_session_path(MainWindow& window) {
    return window.session().path;
  }

  static QString active_session_title(MainWindow& window) {
    return window.session().title;
  }

  static bool register_legacy_plugin_path(MainWindow& window, const QString& path, QStringList* report) {
    return window.register_legacy_plugin_path(path, report);
  }

  static bool close_document_tab(MainWindow& window, int index) {
    return window.close_document_tab(index);
  }

  static void float_active_document(MainWindow& window) {
    window.float_active_document();
  }

  static void dock_active_document(MainWindow& window) {
    window.dock_active_document();
  }

  static void consolidate_all_to_tabs(MainWindow& window) {
    window.consolidate_all_to_tabs();
  }

  // Deterministic offscreen substitute for OS window activation: the exact entry
  // point float WindowActivate / canvas FocusIn wiring funnels into.
  static void activate_canvas(MainWindow& window, CanvasWidget* canvas) {
    window.activate_document_canvas(canvas);
  }

  // Deterministic substitute for the drag-settle timer path: the timer ends in
  // exactly this call with QCursor::pos().
  static void dock_float_at(MainWindow& window, QWidget* float_window, QPoint global_position) {
    window.maybe_dock_float_at(qobject_cast<DocumentFloatWindow*>(float_window), global_position);
  }

  static QRect float_dock_zone(MainWindow& window) {
    return window.float_dock_zone_global();
  }

  // Deterministic substitute for the per-moveEvent highlight update, which reads
  // QCursor::pos() in production.
  static void float_dock_feedback_at(MainWindow& window, QPoint global_position) {
    window.update_float_dock_highlight(global_position);
  }

  static std::size_t session_count(MainWindow& window) {
    return window.sessions_.size();
  }

  // The menu-bar region that acts as the window title bar (shared by the Win32
  // HTCAPTION hit test and the Qt-level drag fallback), in menu-bar coordinates.
  static bool title_bar_drag_area_contains(const MainWindow& window, QPoint menu_bar_position) {
    return window.title_bar_drag_area_contains(menu_bar_position);
  }

  // Makes the next use_custom_window_chrome() re-read the Linux "Use the system title
  // bar" preference (the app reads it once per process).
  static void reset_window_chrome_gate_cache() { MainWindow::reset_window_chrome_gate_cache(); }
  static void set_global_menu_bar_probe(std::optional<bool> global) {
    MainWindow::set_global_menu_bar_probe_for_testing(global);
  }

  // Sessions in creation order (the multi-page PDF open adds one per page).
  static Document& session_document(MainWindow& window, std::size_t index) {
    return window.sessions_.at(index)->document;
  }

  static QString session_title(MainWindow& window, std::size_t index) {
    return window.sessions_.at(index)->title;
  }

  static bool session_cmyk_proof(MainWindow& window, std::size_t index) {
    return window.sessions_.at(index)->cmyk_proof;
  }

  // Starts the inline editor on a text layer of the active document, the way a Type-tool
  // click on it does.
  static void edit_text_layer(MainWindow& window, LayerId id) { window.edit_text_layer(id); }

  static void activate_session(MainWindow& window, std::size_t index) {
    window.activate_document_session(*window.sessions_.at(index));
  }

  static bool active_session_is_floated(MainWindow& window) {
    return window.session().float_window != nullptr;
  }

  static Document* document_for_canvas(MainWindow& window, CanvasWidget* canvas) {
    auto* session = window.session_for_canvas(canvas);
    return session == nullptr ? nullptr : &session->document;
  }

  static std::int64_t session_id_for_canvas(MainWindow& window, CanvasWidget* canvas) {
    auto* session = window.session_for_canvas(canvas);
    return session == nullptr ? 0 : session->session_id;
  }

  static std::ptrdiff_t undo_depth_for_canvas(MainWindow& window, CanvasWidget* canvas) {
    auto* session = window.session_for_canvas(canvas);
    return session == nullptr ? -1 : static_cast<std::ptrdiff_t>(session->history.undo_stack().size());
  }

  static std::size_t active_session_undo_depth(MainWindow& window) {
    return window.session().history.undo_stack().size();
  }

  static void undo(MainWindow& window) {
    window.undo();
  }

  // True while Layers-panel blend mode changes are still merging into one undo entry.
  static bool layer_blend_edit_pending(const MainWindow& window) {
    return window.pending_layer_blend_edit_active_;
  }

  // Layer > Arrange > Align / Distribute entry points and their persisted
  // Align To choice (docs/alignment.md).
  static void align_selected_layers(MainWindow& window, AlignEdge edge) {
    window.align_selected_layers(edge);
  }

  static void distribute_selected_layers(MainWindow& window, DistributeMode mode) {
    window.distribute_selected_layers(mode);
  }

  static bool align_to_canvas(MainWindow& window) {
    return window.align_to_canvas();
  }

  static void save_tool_settings(MainWindow& window) {
    window.save_tool_settings();
  }

  static void redo(MainWindow& window) {
    window.redo();
  }

  static std::size_t active_session_redo_depth(MainWindow& window) {
    return window.session().history.redo_stack().size();
  }

  // Per-stroke history (core/stroke_patch.hpp): whether the newest undo entry is a
  // stroke patch rather than a whole-document snapshot, its bytes, and the switch that
  // keeps strokes on the snapshot path so the two can be compared.
  static bool top_undo_entry_is_stroke_patch(MainWindow& window) {
    const auto& stack = window.session().history.undo_stack();
    return !stack.empty() && stack.back().is_stroke_patch();
  }
  static std::size_t top_undo_entry_patch_bytes(MainWindow& window) {
    const auto& stack = window.session().history.undo_stack();
    return !stack.empty() && stack.back().is_stroke_patch() ? stack.back().patch->byte_size() : 0;
  }
  static void set_stroke_patch_history_enabled(MainWindow& window, bool enabled) {
    window.stroke_patch_history_enabled_ = enabled;
  }
  // The retained-bytes cache is refreshed at push time (before a stroke detaches
  // its layer) and after a patch shrink; this recomputes it from the live stacks.
  static std::size_t recompute_history_retained_bytes(MainWindow& window) {
    window.enforce_history_memory_budget(window.session());
    return window.history_retained_bytes();
  }

  // Context menus are painful to drive offscreen (precedent:
  // show_layer_context_menu); tests invoke the action directly.
  static void open_history_state_as_new_document(MainWindow& window, std::int64_t state_id) {
    window.open_history_state_as_new_document(state_id);
  }

  static bool active_session_is_modified(MainWindow& window) {
    return window.session_is_modified(window.session());
  }

  // Treats the active document as just saved, so a test can see whether a later step marks it modified.
  static void mark_active_session_saved(MainWindow& window) {
    window.session().saved_revision = window.session().revision;
  }

  // Preferences > Tools "Use Esc to commit text" without driving the dialog.
  static void set_escape_commits_text(MainWindow& window, bool on) {
    window.escape_commits_text_ = on;
  }

  static bool active_session_is_smart_object_child(MainWindow& window) {
    return window.session().smart_object_link.has_value();
  }
};

}  // namespace patchy::ui
