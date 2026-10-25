#pragma once

// MainWindowTestAccess: declarations here, bodies in ui_test_access.cpp so an accessor
// edit recompiles one TU instead of every UI test. It is
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
  static Document& document(MainWindow& window);

  static std::vector<LayerId> selected_layer_ids(const MainWindow& window);

  // The High Quality Object Selection model offer (main_window_ai_selection.cpp).
  static bool offer_high_quality_object_selection_model(MainWindow& window);

  static void show_preferences(MainWindow& window);
  // Preferences > Watermarks' checks on open still running (docs/watermarks.md).
  static std::size_t watermark_checks_running(MainWindow& window);
  static int document_tab_count(MainWindow& window);

  // The one-time High Quality offer stays away offscreen; a test that drives it opts in.
  static void allow_high_quality_offer_offscreen(MainWindow& window);

  // One-click update seams (docs/auto-update.md): treat this copy as installer-made (or
  // not), download into a scratch folder, and capture the installer launch instead of
  // starting a process.
  static void set_self_update_override(MainWindow& window, std::optional<bool> value);
  static void set_update_download_folder(MainWindow& window, const QString& folder);
  static void set_update_installer_launcher(MainWindow& window,
                                            std::function<UpdateLaunchResult(const UpdateInstallerLaunch&)> launcher);
  static bool update_install_pending(const MainWindow& window);
  // Captures the application exit a settled update close requests instead of ending the
  // suite's event loop.
  static void set_application_exit_hook(MainWindow& window, std::function<void()> hook);

  // Help > Add Patchy to Applications Menu (GitHub issue 226): the AppImage's own share
  // folder (a test copy of the shipped desktop file and icons), which also keeps the
  // desktop cache tools from running.
  static void set_appimage_bundled_share(MainWindow& window, const QString& share);
  static bool offer_appimage_menu_entry(MainWindow& window);

  // The historical startup document (startup now opens an empty workspace):
  // show_window creates one so pre-existing tests keep their canvas.
  static std::uint64_t gmic_draft_previews_shown(const MainWindow& window);
  // True once a full-quality preview of the filter has completed this session.
  static bool gmic_full_preview_finished(const MainWindow& window, const std::string& filter_id);
  static Rect gmic_last_draft_bounds(const MainWindow& window);

  static void create_default_document(MainWindow& window);

  static void create_document(MainWindow& window, std::int32_t width, std::int32_t height);

  static CanvasWidget* canvas(MainWindow& window);

  static void open_document_path(MainWindow& window, QString path);

  static int open_folder_path(MainWindow& window, const QString& directory);

  static bool accept_open_file_drag(MainWindow& window, QDropEvent* event);

  static bool open_dropped_files(MainWindow& window, QDropEvent* event);

  static std::optional<QStringList> export_document_sessions_to_folder(
      MainWindow& window, const std::vector<std::int64_t>& session_ids, const QString& folder,
      const QString& extension, const ImageSequenceNaming& naming, ExportDocumentsExistingFiles existing_files);

  // File > Export > Artboards to Files without its dialog (docs/artboards.md).
  static std::optional<QStringList> export_artboards_to_folder(MainWindow& window, const std::vector<LayerId>& ids,
                                                               const QString& folder, const QString& extension,
                                                               const ImageSequenceNaming& naming,
                                                               ExportDocumentsExistingFiles existing_files);

  // The Layers panel selection, as a click (and Ctrl-clicks) would make it.
  static void select_layers(MainWindow& window, const std::vector<LayerId>& ids, LayerId active);

  static std::int64_t session_id(MainWindow& window, std::size_t index);

  static void refresh_document_info(MainWindow& window);

  // The ruler unit preference (view/rulerUnits) as the Preferences dialog or the
  // ruler right-click would set it; it persists, so pair it with a
  // SettingsValueRestorer.
  static void set_ruler_unit(MainWindow& window, MeasurementUnit unit);

  static MeasurementUnit ruler_unit(const MainWindow& window);

  static void levels_dialog(MainWindow& window);

  static bool apply_text_warp(MainWindow& window, Layer& layer, const TextWarp& warp);

  static void request_warp_text_dialog(MainWindow& window);

  static void toggle_text_bold_face(MainWindow& window);

  // Deterministic offscreen substitute for a Type-tool canvas click: the canvas
  // press/release pair funnels into exactly this call, but synthetic clicks sent
  // while another window (e.g. a non-modal dialog) is active lose the in-flight
  // drag state to a focus bounce the offscreen platform invents.
  static std::optional<bool> resolve_pdf_layer_choice(MainWindow& window, bool for_export);
  static void add_text_at(MainWindow& window, QPoint document_point);
  static void lay_out_raster_less_imported_text(MainWindow& window, Document& document);

  static void record_text_layout_metrics_for_reopened_text(MainWindow& window, Document& document);

  static void edit_active_shape_appearance(MainWindow& window);

  // Options-bar paint mirrors (the popup pickers' backing state) and the
  // live-edit application, callable without driving the modal manager dialogs.
  static patchy::VectorFill& current_vector_fill(MainWindow& window);

  static patchy::VectorFill& current_vector_stroke_paint(MainWindow& window);

  // What the next shape gets from the options bar (stroke switch and width included).
  static patchy::VectorShapeContent current_shape_appearance(const MainWindow& window);

  static bool apply_options_bar_appearance(MainWindow& window);

  static void update_vector_swatch_icons(MainWindow& window);

  static CustomShapeLibrary& custom_shape_library(MainWindow& window);

  // Holds back the pattern preload of windows constructed while set (released when the
  // window asks for its library, is destroyed, or the hold is cleared), so a test can drive
  // a window before the startup worker lands (docs/startup-performance.md).
  static void hold_pattern_preload(bool hold);
  static bool pattern_library_adopted(const MainWindow& window);

  static ImageSaveOptions image_save_defaults(MainWindow& window);

  static StressReport run_stress_scenario(MainWindow& window, const StressTestOptions& options);

  static void import_from_scanner(MainWindow& window);

  static void photocopy_from_scanner(MainWindow& window);

  static void import_and_divide_from_scanner(MainWindow& window);

  static void divide_current_document_photos(MainWindow& window);

  static std::optional<QStringList> save_divided_photos_to_folder(
      MainWindow& window, const std::vector<PixelBuffer>& photos,
      const DocumentPrintSettings& print_settings, const QString& folder, const QString& prefix,
      const QString& extension, DividePhotosExistingFiles existing_files);

  static void set_ruler_unit_preference(MainWindow& window, MeasurementUnit unit);

  static void open_smart_object_contents(MainWindow& window);

  static void replace_smart_object_contents_with_path(MainWindow& window, const QString& path);

  static void place_embedded_file_with_path(MainWindow& window, const QString& path);

  // File > Place Linked without the file dialog (docs/smart-object-editing.md).
  static void place_linked_file_with_path(MainWindow& window, const QString& path);

  // File > Import > Files as Layers without the file dialog (docs/import.md).
  static void import_files_as_layers_with_paths(MainWindow& window, const QStringList& paths);

  // Image > Canvas Size with `frame` as the new canvas (the dialog's apply).
  static void apply_canvas_size(MainWindow& window, Rect frame);
  // Image > Image Size (the dialog's apply), Bicubic.
  static bool resize_document_image(MainWindow& window, int width, int height);
  static void paste_clipboard(MainWindow& window);

  static bool define_custom_shape_from_svg_path(MainWindow& window, const QString& path);

  static void relink_smart_object_contents_with_path(MainWindow& window, const QString& path);

  static std::optional<QString> relink_missing_smart_object_source(MainWindow& window, const std::string& uuid,
                                                                   const QString& path);

  static void show_missing_linked_files_dialog(MainWindow& window);

  static QString relink_dialog_initial_path(const MainWindow& window);

  static void show_layer_context_menu(MainWindow& window, QPoint position);

  static void refresh_layer_ui(MainWindow& window);

  static void set_right_dock_stack_width(MainWindow& window, int width);

  // The right-hand panel column (panel_column.hpp).
  static PanelColumn* panel_column(MainWindow& window);
  // Opens, raises and expands a panel, as the Window menu does for a closed one.
  static void show_panel(MainWindow& window, const QString& id);
  // A panel's section widget: the panel itself, or the whole tab group for a tab.
  static QWidget* panel_section(MainWindow& window, const QString& id);
  // The saved panel layout without the offscreen gate (panel_layout_persists), so a test
  // can write one window's layout and read it into the next before that one is shown.
  static void write_panel_layout(MainWindow& window);
  static void read_panel_layout(MainWindow& window);
  static bool panel_layout_persists();
  static int right_dock_pinned_width(MainWindow& window);
  static bool right_dock_width_follows_content(MainWindow& window);

  // The width the column opens at and snaps to, the Layers footer on one row.
  static int right_dock_content_minimum_width(MainWindow& window);

  // The narrowest column width at which the Layers panel fits whole, its rows
  // wrapped (GitHub issue 199); below it the panel scrolls sideways.
  static int right_dock_compact_width(MainWindow& window);

  // The client height that keeps the whole frame on the window's screen (GitHub issue 108).
  static int available_window_height(MainWindow& window);
  // Stands in a shorter screen than the offscreen platform's; 0 restores the real one.
  static void set_available_window_height(MainWindow& window, int height);
  static void fit_right_dock_floors_to_screen(MainWindow& window);

  static void update_document_action_state(MainWindow& window);

  static void refresh_paths_panel(MainWindow& window);

  static void set_round_brush_session(MainWindow& window, BrushDynamics dynamics,
                                      double base_angle_degrees, double base_roundness);

  static bool save_document(MainWindow& window);

  static int cli_append_text_to_text_layers(MainWindow& window, const QString& suffix);

  // No options: the save resolves format options itself (for PDF, the layer preference).
  static void refresh_proof_display(MainWindow& window);
  static bool save_document_to_path(MainWindow& window, QString path);
  static bool save_document_to_path(MainWindow& window, QString path, ImageSaveOptions options);
  static bool save_document_to_path(MainWindow& window, QString path, std::optional<ImageSaveOptions> options,
                                    MainWindow::SaveToPathPolicy policy);
  // A save whose flatten question the caller already answered (Save As's own dialogs).
  // An Export copy (export_copy policy): the session keeps its path and modified state.
  static bool export_document_to_path(MainWindow& window, QString path, ImageSaveOptions options);
  static bool save_document_to_path_flatten_confirmed(MainWindow& window, QString path, ImageSaveOptions options);

  static QString active_session_path(MainWindow& window);

  static QString active_session_title(MainWindow& window);

  static bool register_legacy_plugin_path(MainWindow& window, const QString& path, QStringList* report);

  static bool close_document_tab(MainWindow& window, int index);

  static void float_active_document(MainWindow& window);

  static void dock_active_document(MainWindow& window);

  static void consolidate_all_to_tabs(MainWindow& window);

  // Deterministic offscreen substitute for OS window activation: the exact entry
  // point float WindowActivate / canvas FocusIn wiring funnels into.
  static void activate_canvas(MainWindow& window, CanvasWidget* canvas);

  // Deterministic substitute for the drag-settle timer path: the timer ends in
  // exactly this call with QCursor::pos().
  static void dock_float_at(MainWindow& window, QWidget* float_window, QPoint global_position);

  static QRect float_dock_zone(MainWindow& window);

  // Deterministic substitute for the per-moveEvent highlight update, which reads
  // QCursor::pos() in production.
  static void float_dock_feedback_at(MainWindow& window, QPoint global_position);

  static std::size_t session_count(MainWindow& window);

  // The menu-bar region that acts as the window title bar (shared by the Win32
  // HTCAPTION hit test and the Qt-level drag fallback), in menu-bar coordinates.
  static bool title_bar_drag_area_contains(const MainWindow& window, QPoint menu_bar_position);

  // Makes the next use_custom_window_chrome() re-read the Linux "Use the system title
  // bar" preference (the app reads it once per process).
  static void reset_window_chrome_gate_cache();
  static void set_global_menu_bar_probe(std::optional<bool> global);

  // Sessions in creation order (the multi-page PDF open adds one per page).
  static Document& session_document(MainWindow& window, std::size_t index);

  static QString session_title(MainWindow& window, std::size_t index);

  static bool session_cmyk_proof(MainWindow& window, std::size_t index);

  // Starts the inline editor on a text layer of the active document, the way a Type-tool
  // click on it does.
  static void edit_text_layer(MainWindow& window, LayerId id);

  static void activate_session(MainWindow& window, std::size_t index);

  static bool active_session_is_floated(MainWindow& window);

  static Document* document_for_canvas(MainWindow& window, CanvasWidget* canvas);

  static std::int64_t session_id_for_canvas(MainWindow& window, CanvasWidget* canvas);

  static std::ptrdiff_t undo_depth_for_canvas(MainWindow& window, CanvasWidget* canvas);

  static std::size_t active_session_undo_depth(MainWindow& window);

  static void undo(MainWindow& window);

  // True while Layers-panel blend mode changes are still merging into one undo entry.
  static bool layer_blend_edit_pending(const MainWindow& window);

  // Layer > Arrange > Align / Distribute entry points and their persisted
  // Align To choice (docs/alignment.md).
  static void align_selected_layers(MainWindow& window, AlignEdge edge);

  static void distribute_selected_layers(MainWindow& window, DistributeMode mode);

  static bool align_to_canvas(MainWindow& window);

  static void save_tool_settings(MainWindow& window);

  static void redo(MainWindow& window);

  static std::size_t active_session_redo_depth(MainWindow& window);

  // Per-stroke history (core/stroke_patch.hpp): whether the newest undo entry is a
  // stroke patch rather than a whole-document snapshot, its bytes, and the switch that
  // keeps strokes on the snapshot path so the two can be compared.
  static bool top_undo_entry_is_stroke_patch(MainWindow& window);
  static std::size_t top_undo_entry_patch_bytes(MainWindow& window);
  static void set_stroke_patch_history_enabled(MainWindow& window, bool enabled);
  // The retained-bytes cache is refreshed at push time (before a stroke detaches
  // its layer) and after a patch shrink; this recomputes it from the live stacks.
  static std::size_t recompute_history_retained_bytes(MainWindow& window);

  // Context menus are painful to drive offscreen (precedent:
  // show_layer_context_menu); tests invoke the action directly.
  static void open_history_state_as_new_document(MainWindow& window, std::int64_t state_id);

  static bool active_session_is_modified(MainWindow& window);

  // Treats the active document as just saved, so a test can see whether a later step marks it modified.
  static void mark_active_session_saved(MainWindow& window);

  // Preferences > Tools "Use Esc to commit text" without driving the dialog.
  static void set_escape_commits_text(MainWindow& window, bool on);

  static bool active_session_is_smart_object_child(MainWindow& window);
};

}  // namespace patchy::ui
