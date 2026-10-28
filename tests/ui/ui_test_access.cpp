#include "ui_test_access.hpp"

namespace patchy::ui {

Document& MainWindowTestAccess::document(MainWindow& window) {
  return window.document();
}

std::vector<LayerId> MainWindowTestAccess::selected_layer_ids(const MainWindow& window) {
  return window.selected_layer_ids();
}

bool MainWindowTestAccess::offer_high_quality_object_selection_model(MainWindow& window) {
  return window.offer_high_quality_object_selection_model();
}

void MainWindowTestAccess::show_preferences(MainWindow& window) { window.show_preferences(); }

std::size_t MainWindowTestAccess::watermark_checks_running(MainWindow& window) { return window.watermark_checks_.size(); }

int MainWindowTestAccess::document_tab_count(MainWindow& window) { return window.document_tabs_->count(); }

void MainWindowTestAccess::allow_high_quality_offer_offscreen(MainWindow& window) {
  window.high_quality_offer_in_offscreen_tests_ = true;
}

void MainWindowTestAccess::set_self_update_override(MainWindow& window, std::optional<bool> value) {
  window.self_update_override_for_tests_ = value;
}

void MainWindowTestAccess::set_update_download_folder(MainWindow& window, const QString& folder) {
  window.update_download_folder_for_tests_ = folder;
}

void MainWindowTestAccess::set_update_installer_launcher(MainWindow& window,
                                          std::function<UpdateLaunchResult(const UpdateInstallerLaunch&)> launcher) {
  window.update_installer_launcher_for_tests_ = std::move(launcher);
}

bool MainWindowTestAccess::update_install_pending(const MainWindow& window) { return window.pending_update_install_.has_value(); }

void MainWindowTestAccess::set_application_exit_hook(MainWindow& window, std::function<void()> hook) {
  window.application_exit_hook_for_tests_ = std::move(hook);
}

void MainWindowTestAccess::set_appimage_bundled_share(MainWindow& window, const QString& share) {
  window.appimage_bundled_share_for_tests_ = share;
}

bool MainWindowTestAccess::offer_appimage_menu_entry(MainWindow& window) { return window.offer_appimage_menu_entry(); }

std::uint64_t MainWindowTestAccess::gmic_draft_previews_shown(const MainWindow& window) {
  return window.gmic_draft_previews_shown_;
}

bool MainWindowTestAccess::gmic_full_preview_finished(const MainWindow& window, const std::string& filter_id) {
  return window.gmic_full_ms_per_megapixel_.count(filter_id) != 0;
}

Rect MainWindowTestAccess::gmic_last_draft_bounds(const MainWindow& window) {
  return window.gmic_last_draft_bounds_;
}

void MainWindowTestAccess::create_default_document(MainWindow& window) {
  window.reset_document(1024, 768, Qt::white, QStringLiteral("New document"));
}

void MainWindowTestAccess::create_document(MainWindow& window, std::int32_t width, std::int32_t height) {
  window.reset_document(width, height, Qt::white, QStringLiteral("New document"));
}

CanvasWidget* MainWindowTestAccess::canvas(MainWindow& window) {
  return window.canvas_;
}

void MainWindowTestAccess::open_document_path(MainWindow& window, QString path) {
  window.open_document_path(std::move(path));
}

int MainWindowTestAccess::open_folder_path(MainWindow& window, const QString& directory) {
  return window.open_folder_path(directory);
}

bool MainWindowTestAccess::accept_open_file_drag(MainWindow& window, QDropEvent* event) {
  return window.accept_open_file_drag(event);
}

bool MainWindowTestAccess::open_dropped_files(MainWindow& window, QDropEvent* event) {
  return window.open_dropped_files(event);
}

std::optional<QStringList> MainWindowTestAccess::export_document_sessions_to_folder(
    MainWindow& window, const std::vector<std::int64_t>& session_ids, const QString& folder,
    const QString& extension, const ImageSequenceNaming& naming, ExportDocumentsExistingFiles existing_files) {
  return window.export_document_sessions_to_folder(session_ids, folder, extension, naming, existing_files);
}

std::optional<QStringList> MainWindowTestAccess::export_artboards_to_folder(MainWindow& window, const std::vector<LayerId>& ids,
                                                             const QString& folder, const QString& extension,
                                                             const ImageSequenceNaming& naming,
                                                             ExportDocumentsExistingFiles existing_files) {
  return window.export_artboards_to_folder(std::as_const(window.document()), ids, folder, extension, naming,
                                           existing_files);
}

void MainWindowTestAccess::select_layers(MainWindow& window, const std::vector<LayerId>& ids, LayerId active) {
  window.select_layers_in_layer_list(ids, active);
}

std::int64_t MainWindowTestAccess::session_id(MainWindow& window, std::size_t index) {
  return window.sessions_.at(index)->session_id;
}

void MainWindowTestAccess::refresh_document_info(MainWindow& window) {
  window.refresh_document_info();
}

void MainWindowTestAccess::set_ruler_unit(MainWindow& window, MeasurementUnit unit) {
  window.set_ruler_unit_preference(unit);
}

MeasurementUnit MainWindowTestAccess::ruler_unit(const MainWindow& window) {
  return window.ruler_unit_;
}

void MainWindowTestAccess::levels_dialog(MainWindow& window) {
  window.levels_dialog();
}

bool MainWindowTestAccess::apply_text_warp(MainWindow& window, Layer& layer, const TextWarp& warp) {
  return window.apply_text_warp_to_layer(layer, warp);
}

void MainWindowTestAccess::request_warp_text_dialog(MainWindow& window) {
  window.request_warp_text_dialog();
}

void MainWindowTestAccess::toggle_text_bold_face(MainWindow& window) {
  window.toggle_text_bold_face();
}

std::optional<bool> MainWindowTestAccess::resolve_pdf_layer_choice(MainWindow& window, bool for_export) {
  return window.resolve_pdf_layer_choice(for_export, /*allow_prompt*/ true);
}

void MainWindowTestAccess::add_text_at(MainWindow& window, QPoint document_point) {
  window.add_text_at(document_point, QRect());
}

void MainWindowTestAccess::lay_out_raster_less_imported_text(MainWindow& window, Document& document) {
  window.lay_out_raster_less_imported_text(document);
}

void MainWindowTestAccess::record_text_layout_metrics_for_reopened_text(MainWindow& window, Document& document) {
  window.record_text_layout_metrics_for_reopened_text(document);
}

void MainWindowTestAccess::edit_active_shape_appearance(MainWindow& window) {
  window.edit_active_shape_appearance();
}

patchy::VectorFill& MainWindowTestAccess::current_vector_fill(MainWindow& window) {
  return window.current_vector_fill_;
}

patchy::VectorFill& MainWindowTestAccess::current_vector_stroke_paint(MainWindow& window) {
  return window.current_vector_stroke_paint_;
}

patchy::VectorShapeContent MainWindowTestAccess::current_shape_appearance(const MainWindow& window) {
  return window.current_shape_appearance_content();
}

bool MainWindowTestAccess::apply_options_bar_appearance(MainWindow& window) {
  return window.apply_options_bar_appearance_to_active_shape();
}

void MainWindowTestAccess::update_vector_swatch_icons(MainWindow& window) {
  window.update_vector_swatch_icons();
}

CustomShapeLibrary& MainWindowTestAccess::custom_shape_library(MainWindow& window) {
  return window.custom_shape_library();
}

void MainWindowTestAccess::hold_pattern_preload(bool hold) { MainWindow::hold_pattern_preload_for_tests(hold); }

bool MainWindowTestAccess::pattern_library_adopted(const MainWindow& window) { return window.pattern_library_ != nullptr; }

ImageSaveOptions MainWindowTestAccess::image_save_defaults(MainWindow& window) {
  return window.image_save_defaults_for_document();
}

StressReport MainWindowTestAccess::run_stress_scenario(MainWindow& window, const StressTestOptions& options) {
  return window.run_stress_test_scenario(options);
}

void MainWindowTestAccess::import_from_scanner(MainWindow& window) {
  window.import_from_scanner();
}

void MainWindowTestAccess::photocopy_from_scanner(MainWindow& window) {
  window.photocopy_from_scanner();
}

void MainWindowTestAccess::import_and_divide_from_scanner(MainWindow& window) {
  window.import_and_divide_from_scanner();
}

void MainWindowTestAccess::divide_current_document_photos(MainWindow& window) {
  window.divide_current_document_photos();
}

std::optional<QStringList> MainWindowTestAccess::save_divided_photos_to_folder(
    MainWindow& window, const std::vector<PixelBuffer>& photos,
    const DocumentPrintSettings& print_settings, const QString& folder, const QString& prefix,
    const QString& extension, DividePhotosExistingFiles existing_files) {
  return window.save_divided_photos_to_folder(photos, print_settings, folder, prefix, extension,
                                              existing_files);
}

void MainWindowTestAccess::set_ruler_unit_preference(MainWindow& window, MeasurementUnit unit) {
  window.set_ruler_unit_preference(unit);
}

void MainWindowTestAccess::open_smart_object_contents(MainWindow& window) {
  window.open_smart_object_contents();
}

void MainWindowTestAccess::replace_smart_object_contents_with_path(MainWindow& window, const QString& path) {
  window.replace_smart_object_contents_with_path(path);
}

void MainWindowTestAccess::place_embedded_file_with_path(MainWindow& window, const QString& path) {
  window.place_embedded_file_with_path(path);
}

void MainWindowTestAccess::place_linked_file_with_path(MainWindow& window, const QString& path) {
  window.place_linked_file_with_path(path);
}

void MainWindowTestAccess::import_files_as_layers_with_paths(MainWindow& window, const QStringList& paths) {
  window.import_files_as_layers_with_paths(paths);
}

void MainWindowTestAccess::apply_canvas_size(MainWindow& window, Rect frame) {
  window.apply_canvas_size(frame, QColor(Qt::white), false, false, QStringLiteral("Canvas size"));
}

bool MainWindowTestAccess::resize_document_image(MainWindow& window, int width, int height) {
  return window.resize_document_image(window.session(), width, height, ResampleMethod::Bicubic);
}

void MainWindowTestAccess::paste_clipboard(MainWindow& window) {
  window.paste_clipboard();
}

bool MainWindowTestAccess::define_custom_shape_from_svg_path(MainWindow& window, const QString& path) {
  return window.define_custom_shape_from_svg_path(path);
}

void MainWindowTestAccess::relink_smart_object_contents_with_path(MainWindow& window, const QString& path) {
  window.relink_smart_object_contents_with_path(path);
}

std::optional<QString> MainWindowTestAccess::relink_missing_smart_object_source(MainWindow& window, const std::string& uuid,
                                                                 const QString& path) {
  return window.relink_missing_smart_object_source(uuid, path);
}

void MainWindowTestAccess::show_missing_linked_files_dialog(MainWindow& window) {
  window.show_missing_linked_files_dialog(false);
}

QString MainWindowTestAccess::relink_dialog_initial_path(const MainWindow& window) {
  return window.relink_dialog_initial_path();
}

void MainWindowTestAccess::show_layer_context_menu(MainWindow& window, QPoint position) {
  window.show_layer_context_menu(position);
}

void MainWindowTestAccess::refresh_layer_ui(MainWindow& window) {
  window.refresh_layer_list();
  window.refresh_layer_controls();
}

void MainWindowTestAccess::set_layer_thumbnail_size(MainWindow& window, LayerThumbnailSize size) {
  window.set_layer_thumbnail_size(size);
}

void MainWindowTestAccess::set_right_dock_stack_width(MainWindow& window, int width) {
  window.set_right_dock_stack_width(width);
}

PanelColumn* MainWindowTestAccess::panel_column(MainWindow& window) { return window.panel_column_; }

void MainWindowTestAccess::show_panel(MainWindow& window, const QString& id) { window.show_panel(id); }

QWidget* MainWindowTestAccess::panel_section(MainWindow& window, const QString& id) {
  return window.panel_column_ != nullptr ? window.panel_column_->section_widget(id) : nullptr;
}

void MainWindowTestAccess::write_panel_layout(MainWindow& window) { window.write_panel_layout(); }

void MainWindowTestAccess::read_panel_layout(MainWindow& window) { window.read_panel_layout(); }

bool MainWindowTestAccess::panel_layout_persists() { return MainWindow::panel_layout_persists(); }

int MainWindowTestAccess::right_dock_pinned_width(MainWindow& window) { return window.right_dock_pinned_width_; }

bool MainWindowTestAccess::right_dock_width_follows_content(MainWindow& window) {
  return window.right_dock_width_follows_minimum_;
}

int MainWindowTestAccess::right_dock_content_minimum_width(MainWindow& window) {
  window.update_right_dock_minimum_width();
  return window.right_dock_minimum_width_;
}

int MainWindowTestAccess::right_dock_compact_width(MainWindow& window) {
  window.update_right_dock_minimum_width();
  return window.right_dock_compact_width_;
}

int MainWindowTestAccess::available_window_height(MainWindow& window) { return window.available_window_height(); }

void MainWindowTestAccess::set_available_window_height(MainWindow& window, int height) {
  window.available_window_height_override_ = height > 0 ? height : -1;
}

void MainWindowTestAccess::fit_right_dock_floors_to_screen(MainWindow& window) { window.fit_right_dock_floors_to_screen(); }

void MainWindowTestAccess::update_document_action_state(MainWindow& window) {
  window.update_document_action_state();
}

void MainWindowTestAccess::refresh_paths_panel(MainWindow& window) {
  window.refresh_paths_panel();
}

void MainWindowTestAccess::set_round_brush_session(MainWindow& window, BrushDynamics dynamics,
                                    double base_angle_degrees, double base_roundness) {
  window.round_brush_dynamics_ = std::move(dynamics);
  window.round_brush_base_angle_degrees_ = base_angle_degrees;
  window.round_brush_base_roundness_ = base_roundness;
}

bool MainWindowTestAccess::save_document(MainWindow& window) {
  return window.save_document();
}

int MainWindowTestAccess::cli_append_text_to_text_layers(MainWindow& window, const QString& suffix) {
  return window.cli_append_text_to_text_layers(suffix);
}

void MainWindowTestAccess::refresh_proof_display(MainWindow& window) { window.refresh_proof_display(); }

bool MainWindowTestAccess::save_document_to_path(MainWindow& window, QString path) {
  return window.save_document_to_path(std::move(path));
}

bool MainWindowTestAccess::save_document_to_path(MainWindow& window, QString path, ImageSaveOptions options) {
  return window.save_document_to_path(std::move(path), std::move(options));
}

bool MainWindowTestAccess::save_document_to_path(MainWindow& window, QString path, std::optional<ImageSaveOptions> options,
                                  MainWindow::SaveToPathPolicy policy) {
  return window.save_document_to_path(std::move(path), std::move(options), policy);
}

bool MainWindowTestAccess::export_document_to_path(MainWindow& window, QString path, ImageSaveOptions options) {
  return window.save_document_to_path(std::move(path), std::move(options),
                                      MainWindow::SaveToPathPolicy{/*flatten_confirmed=*/true, /*scripted=*/false,
                                                                   /*export_copy=*/true});
}

bool MainWindowTestAccess::save_document_to_path_flatten_confirmed(MainWindow& window, QString path, ImageSaveOptions options) {
  return window.save_document_to_path(std::move(path), std::move(options),
                                      MainWindow::SaveToPathPolicy{/*flatten_confirmed=*/true, /*scripted=*/false,
                                                                   /*export_copy=*/false});
}

QString MainWindowTestAccess::active_session_path(MainWindow& window) {
  return window.session().path;
}

QString MainWindowTestAccess::active_session_title(MainWindow& window) {
  return window.session().title;
}

bool MainWindowTestAccess::register_legacy_plugin_path(MainWindow& window, const QString& path, QStringList* report) {
  return window.register_legacy_plugin_path(path, report);
}

bool MainWindowTestAccess::close_document_tab(MainWindow& window, int index) {
  return window.close_document_tab(index);
}

void MainWindowTestAccess::float_active_document(MainWindow& window) {
  window.float_active_document();
}

void MainWindowTestAccess::dock_active_document(MainWindow& window) {
  window.dock_active_document();
}

void MainWindowTestAccess::consolidate_all_to_tabs(MainWindow& window) {
  window.consolidate_all_to_tabs();
}

void MainWindowTestAccess::activate_canvas(MainWindow& window, CanvasWidget* canvas) {
  window.activate_document_canvas(canvas);
}

void MainWindowTestAccess::dock_float_at(MainWindow& window, QWidget* float_window, QPoint global_position) {
  window.maybe_dock_float_at(qobject_cast<DocumentFloatWindow*>(float_window), global_position);
}

QRect MainWindowTestAccess::float_dock_zone(MainWindow& window) {
  return window.float_dock_zone_global();
}

void MainWindowTestAccess::float_dock_feedback_at(MainWindow& window, QPoint global_position) {
  window.update_float_dock_highlight(global_position);
}

std::size_t MainWindowTestAccess::session_count(MainWindow& window) {
  return window.sessions_.size();
}

void MainWindowTestAccess::restore_window_from_maximize(MainWindow& window) {
  window.restore_window_from_maximize();
}

bool MainWindowTestAccess::title_bar_drag_area_contains(const MainWindow& window, QPoint menu_bar_position) {
  return window.title_bar_drag_area_contains(menu_bar_position);
}

void MainWindowTestAccess::reset_window_chrome_gate_cache() { MainWindow::reset_window_chrome_gate_cache(); }

void MainWindowTestAccess::set_global_menu_bar_probe(std::optional<bool> global) {
  MainWindow::set_global_menu_bar_probe_for_testing(global);
}

Document& MainWindowTestAccess::session_document(MainWindow& window, std::size_t index) {
  return window.sessions_.at(index)->document;
}

QString MainWindowTestAccess::session_title(MainWindow& window, std::size_t index) {
  return window.sessions_.at(index)->title;
}

bool MainWindowTestAccess::session_cmyk_proof(MainWindow& window, std::size_t index) {
  return window.sessions_.at(index)->cmyk_proof;
}

void MainWindowTestAccess::edit_text_layer(MainWindow& window, LayerId id) { window.edit_text_layer(id); }

void MainWindowTestAccess::activate_session(MainWindow& window, std::size_t index) {
  window.activate_document_session(*window.sessions_.at(index));
}

bool MainWindowTestAccess::active_session_is_floated(MainWindow& window) {
  return window.session().float_window != nullptr;
}

Document* MainWindowTestAccess::document_for_canvas(MainWindow& window, CanvasWidget* canvas) {
  auto* session = window.session_for_canvas(canvas);
  return session == nullptr ? nullptr : &session->document;
}

std::int64_t MainWindowTestAccess::session_id_for_canvas(MainWindow& window, CanvasWidget* canvas) {
  auto* session = window.session_for_canvas(canvas);
  return session == nullptr ? 0 : session->session_id;
}

std::ptrdiff_t MainWindowTestAccess::undo_depth_for_canvas(MainWindow& window, CanvasWidget* canvas) {
  auto* session = window.session_for_canvas(canvas);
  return session == nullptr ? -1 : static_cast<std::ptrdiff_t>(session->history.undo_stack().size());
}

std::size_t MainWindowTestAccess::active_session_undo_depth(MainWindow& window) {
  return window.session().history.undo_stack().size();
}

void MainWindowTestAccess::undo(MainWindow& window) {
  window.undo();
}

bool MainWindowTestAccess::layer_blend_edit_pending(const MainWindow& window) {
  return window.pending_layer_blend_edit_active_;
}

void MainWindowTestAccess::align_selected_layers(MainWindow& window, AlignEdge edge) {
  window.align_selected_layers(edge);
}

void MainWindowTestAccess::distribute_selected_layers(MainWindow& window, DistributeMode mode) {
  window.distribute_selected_layers(mode);
}

bool MainWindowTestAccess::align_to_canvas(MainWindow& window) {
  return window.align_to_canvas();
}

void MainWindowTestAccess::save_tool_settings(MainWindow& window) {
  window.save_tool_settings();
}

void MainWindowTestAccess::redo(MainWindow& window) {
  window.redo();
}

std::size_t MainWindowTestAccess::active_session_redo_depth(MainWindow& window) {
  return window.session().history.redo_stack().size();
}

bool MainWindowTestAccess::top_undo_entry_is_stroke_patch(MainWindow& window) {
  const auto& stack = window.session().history.undo_stack();
  return !stack.empty() && stack.back().is_stroke_patch();
}

std::size_t MainWindowTestAccess::top_undo_entry_patch_bytes(MainWindow& window) {
  const auto& stack = window.session().history.undo_stack();
  return !stack.empty() && stack.back().is_stroke_patch() ? stack.back().patch->byte_size() : 0;
}

void MainWindowTestAccess::set_stroke_patch_history_enabled(MainWindow& window, bool enabled) {
  window.stroke_patch_history_enabled_ = enabled;
}

std::size_t MainWindowTestAccess::recompute_history_retained_bytes(MainWindow& window) {
  window.enforce_history_memory_budget(window.session());
  return window.history_retained_bytes();
}

void MainWindowTestAccess::open_history_state_as_new_document(MainWindow& window, std::int64_t state_id) {
  window.open_history_state_as_new_document(state_id);
}

bool MainWindowTestAccess::active_session_is_modified(MainWindow& window) {
  return window.session_is_modified(window.session());
}

void MainWindowTestAccess::mark_active_session_saved(MainWindow& window) {
  window.session().saved_revision = window.session().revision;
}

void MainWindowTestAccess::set_escape_commits_text(MainWindow& window, bool on) {
  window.escape_commits_text_ = on;
}

bool MainWindowTestAccess::active_session_is_smart_object_child(MainWindow& window) {
  return window.session().smart_object_link.has_value();
}

}  // namespace patchy::ui
