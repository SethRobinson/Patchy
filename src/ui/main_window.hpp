#pragma once

#include "core/adjustment_layer.hpp"
#include "core/document.hpp"
#include "filters/filter_registry.hpp"
#include "formats/format_registry.hpp"
#include "plugins/plugin_host.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/image_document_io.hpp"

#include <QByteArray>
#include <QColor>
#include <QKeySequence>
#include <QListWidget>
#include <QMainWindow>
#include <QPageLayout>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QStringList>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <set>
#include <utility>
#include <vector>

class QAction;
class QActionGroup;
class QCheckBox;
class QCloseEvent;
class QComboBox;
class QDialog;
class QDoubleSpinBox;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QEvent;
class QFontComboBox;
class QImage;
class QLabel;
class QMenu;
class QPushButton;
class QShowEvent;
class QSlider;
class QSpinBox;
class QTabWidget;
class QTextEdit;
class QToolBar;
class QToolButton;

namespace patchy::ui {

struct UpdateInfo;

class MainWindow final : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(QWidget* parent = nullptr);
  void add_document_session(Document document, QString title, QString path = {});
  void show_update_available(const UpdateInfo& update);

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;
  bool nativeEvent(const QByteArray& event_type, void* message, qintptr* result) override;
  void changeEvent(QEvent* event) override;
  void closeEvent(QCloseEvent* event) override;
  void showEvent(QShowEvent* event) override;
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dragMoveEvent(QDragMoveEvent* event) override;
  void dropEvent(QDropEvent* event) override;

private:
  struct DocumentSession {
    struct HistoryState {
      Document document;
      std::int64_t revision{0};
    };

    Document document;
    QString title;
    QString path;
    std::optional<ImageSaveOptions> image_save_options;
    QString image_save_options_path;
    QString image_save_options_extension;
    CanvasWidget* canvas{nullptr};
    std::vector<HistoryState> undo_stack;
    std::vector<HistoryState> redo_stack;
    std::set<LayerId> collapsed_layer_groups;
    std::int64_t revision{0};
    std::int64_t saved_revision{0};
  };

  struct ClipboardPayload {
    PixelBuffer pixels;
    QPoint origin;
    std::vector<Layer> layers_top_to_bottom;
  };

  void create_actions();
  void configure_window_chrome();
  void position_window_chrome_controls();
  void ensure_native_resizable_frame();
  bool handle_right_dock_resize_event(QObject* watched, QEvent* event);
  void update_right_dock_resize_handle_geometry(QWidget* host);
  void set_right_dock_stack_width(int width);
  bool handle_window_resize_event(QObject* watched, QEvent* event);
  void update_window_resize_cursor(Qt::Edges edges);
  void clear_window_resize_cursor();
  void resize_window_from_global_point(QPoint global_position);
  void create_docks();
  void create_swatches_dock();
  void configure_canvas(CanvasWidget* canvas);
  void activate_document_tab(int index);
  void close_document_tab(int index);
  [[nodiscard]] bool confirm_close_session(DocumentSession& target_session);
  [[nodiscard]] bool maybe_save_session(DocumentSession& target_session);
  void refresh_document_tab_titles();
  void refresh_document_window_title();
  void set_session_saved(DocumentSession& target_session);
  void mark_session_modified(DocumentSession& target_session);
  [[nodiscard]] bool session_is_modified(const DocumentSession& target_session) const noexcept;
  [[nodiscard]] DocumentSession* session_for_canvas(CanvasWidget* canvas) noexcept;
  [[nodiscard]] const DocumentSession* session_for_canvas(CanvasWidget* canvas) const noexcept;
  [[nodiscard]] Document& document();
  [[nodiscard]] const Document& document() const;
  [[nodiscard]] DocumentSession& session();
  [[nodiscard]] const DocumentSession& session() const;
  [[nodiscard]] bool has_active_document() const noexcept;
  void reset_document(std::int32_t width, std::int32_t height, QColor background, QString history_label);
  void create_clipboard_document(const QImage& image, QString history_label);
  void create_new_document();
  void resize_image_dialog();
  void resize_canvas_dialog();
  void open_document();
  void open_document_path(QString path);
  bool accept_open_file_drag(QDropEvent* event);
  bool open_dropped_files(QDropEvent* event);
  bool save_document();
  bool save_document_as();
  bool save_document_to_path(QString path, std::optional<ImageSaveOptions> image_options = std::nullopt);
  void export_flat_image();
  void page_setup();
  void print_document();
  void show_preferences();
  void new_guide_dialog();
  void new_guide_layout_dialog();
  void clear_guides();
  void clear_selected_guides();
  void apply_canvas_aid_settings(CanvasWidget* canvas) const;
  void load_view_settings();
  void save_view_settings() const;
  void scan_legacy_plugins();
  void load_bundled_legacy_plugins();
  bool register_legacy_plugin_path(const QString& path, QStringList* report = nullptr);
  void add_legacy_plugin_action(const PluginDescriptor& descriptor);
  void run_legacy_plugin(QString identifier);
  void cut_selection();
  void copy_selection();
  void copy_merged();
  void paste_clipboard();
  void clear_system_clipboard();
  void set_system_clipboard_image(const QImage& image);
  void clear_internal_clipboard_on_external_change();
  void transform_active_layer_dialog();
  void add_text_at(QPoint document_point, QRect requested_text_box = {});
  void cancel_text_editor(QTextEdit* editor, std::optional<LayerId> layer_id);
  void commit_text_editor(QTextEdit* editor, QPoint document_point, std::optional<LayerId> layer_id);
  void finish_active_text_editor();
  void apply_filter(const QString& identifier);
  void populate_new_adjustment_layer_menu(QMenu* menu, const QString& object_name_prefix = {});
  void new_levels_adjustment_layer();
  void levels_dialog();
  void apply_levels_adjustment(int black_input, int white_input, int gamma_percent, bool allow_identity = false);
  void new_curves_adjustment_layer();
  void curves_dialog();
  void apply_curves_adjustment(int shadow_output, int midtone_output, int highlight_output,
                               bool allow_identity = false);
  void new_hue_saturation_adjustment_layer();
  void hue_saturation_dialog();
  void apply_hue_saturation_adjustment(int hue_shift, int saturation_delta, int lightness_delta,
                                       bool allow_identity = false);
  void new_color_balance_adjustment_layer();
  void color_balance_dialog();
  void apply_color_balance_adjustment(int cyan_red, int magenta_green, int yellow_blue,
                                      bool allow_identity = false);
  [[nodiscard]] Layer build_adjustment_layer(QString label, const AdjustmentSettings& settings);
  void update_adjustment_layer_preview(QString label, const AdjustmentSettings& settings, bool enabled,
                                       std::optional<LayerId>& preview_id,
                                       std::optional<LayerId> restore_active_layer);
  void remove_adjustment_layer_preview(std::optional<LayerId>& preview_id,
                                       std::optional<LayerId> restore_active_layer);
  void create_adjustment_layer(QString label, const AdjustmentSettings& settings);
  void edit_active_adjustment_layer();
  void add_layer();
  void create_layer_folder();
  void create_layer_folder_from_layers(std::vector<LayerId> ids);
  void layer_via_copy();
  void layer_via_cut();
  void add_layer_mask_from_selection();
  void delete_active_layer_mask();
  void set_active_layer_mask_linked(bool linked);
  void set_active_layer_mask_disabled(bool disabled);
  void invert_active_layer_mask();
  void apply_active_layer_mask();
  void duplicate_active_layer();
  void duplicate_layers(std::vector<LayerId> ids);
  void rename_active_layer();
  void edit_active_layer_style();
  void copy_active_layer_style();
  void paste_layer_style_to_selected_layers();
  void delete_selected_layer_styles();
  void refresh_layer_style_action_states();
  void rasterize_active_layers();
  void rasterize_active_layer_styles();
  void delete_active_layer();
  void delete_layers(std::vector<LayerId> ids);
  void move_active_layer(int direction);
  void handle_layer_drop();
  void reorder_layers_from_list();
  void toggle_layer_folder_expanded(LayerId id);
  void reveal_layer_in_layer_list(LayerId id);
  void set_layer_visibility_from_item(QListWidgetItem* item);
  void show_layer_context_menu(QPoint position);
  bool handle_layer_action_button_drag_event(QObject* watched, QEvent* event);
  void merge_visible_to_new_layer();
  void merge_selected_to_new_layer();
  void fill_active_layer();
  void fill_active_layer_with_color(QColor color, QString label);
  void clear_active_layer();
  void stroke_selection();
  void expand_selection_dialog();
  void contract_selection_dialog();
  void border_selection_dialog();
  void flip_active_layer_horizontal();
  void flip_active_layer_vertical();
  void crop_to_selection();
  void rotate_canvas_clockwise();
  void rotate_canvas_counterclockwise();
  [[nodiscard]] std::vector<LayerId> selected_layer_ids() const;
  [[nodiscard]] std::vector<LayerId> selected_or_active_layer_ids() const;
  void set_active_layer_from_selection();
  void set_active_layer_opacity(int value);
  void set_active_layer_blend(int index);
  void set_active_layer_visible(bool visible);
  void set_layer_lock_state(LayerId id, bool locked);
  void set_active_layer_lock(bool locked);
  void set_active_layer_lock_transparency(bool locked);
  [[nodiscard]] bool layer_id_is_effectively_locked(LayerId id) const;
  [[nodiscard]] std::vector<LayerId> unlocked_layer_ids(std::vector<LayerId> ids) const;
  bool show_locked_layer_message_if_all_locked(const std::vector<LayerId>& requested_ids,
                                               const std::vector<LayerId>& unlocked_ids);
  void undo();
  void redo();
  void push_undo_snapshot(QString label);
  void refresh_layer_list();
  void refresh_layer_thumbnails();
  void refresh_layer_controls();
  void refresh_document_info();
  void update_canvas_info(CanvasInfoState info);
  void choose_primary_color();
  void choose_secondary_color();
  void choose_text_color();
  void show_color_panel(bool foreground);
  void swap_colors();
  void default_colors();
  void refresh_color_buttons();
  void refresh_text_color_button();
  void edit_gradient_stops();
  void refresh_gradient_controls_from_canvas();
  [[nodiscard]] QColor current_text_color() const;
  void load_tool_settings();
  void save_tool_settings() const;
  void sync_text_options_from_active_editor();
  void apply_text_family_to_active_editor();
  void apply_text_size_to_active_editor();
  void apply_text_bold_to_active_editor();
  void apply_text_italic_to_active_editor();
  void apply_text_color_to_active_editor();
  void apply_primary_color_to_active_text_editor(QColor color);
  void apply_text_smoothing_to_active_editor();
  void apply_text_alignment_to_active_editor(Qt::Alignment alignment);
  void sync_text_alignment_buttons_from_editor();
  void relayout_text_editor(QTextEdit* editor, bool allow_point_auto_expand);
  void update_text_editor_handles(QTextEdit* editor);
  void remove_text_editor_handles(QTextEdit* editor);
  QWidget* text_editor_resize_handle_at(QPoint canvas_position) const;
  bool handle_text_editor_resize_event(QWidget* handle, QTextEdit* editor, QEvent* event);
  bool handle_text_editor_transform_overlay_event(QTextEdit* editor, QEvent* event);
  void mark_text_editor_changed(QTextEdit* editor);
  void schedule_text_editor_preview(QTextEdit* editor);
  void update_text_editor_preview(QTextEdit* editor);
  void remove_text_editor_preview(QTextEdit* editor);
  void update_text_editor_transform_overlay(QTextEdit* editor);
  void remove_text_editor_transform_overlay(QTextEdit* editor);
  void handle_canvas_view_changed(CanvasWidget* canvas);
  [[nodiscard]] bool is_text_option_widget(QWidget* widget) const;
  void apply_transform_controls_from_ui();
  void sync_transform_controls_from_canvas();
  void register_option_action(QAction* action, std::initializer_list<CanvasTool> tools);
  void register_retranslation(std::function<void()> callback);
  void retranslate_ui();
  void retranslate_bound_children();
  void retranslate_blend_combo();
  void retranslate_brush_preset_combo();
  void refresh_language_actions();
  void refresh_options_bar();
  void register_document_action(QAction* action);
  void register_document_widget(QWidget* widget);
  void update_document_action_state();
  void sync_brush_controls_from_canvas();
  void load_recent_files();
  void save_recent_files() const;
  void add_recent_file(QString path);
  void rebuild_recent_files_menu();
  void show_recent_file_context_menu(const QPoint& position);
  void open_recent_document(QString path);
  QAction* add_tool_action(QToolBar* palette, QActionGroup* group, QString label, CanvasTool tool,
                           QKeySequence shortcut);
  void update_history(QString label);
  void update_file_path_actions();
  void update_undo_redo_actions();
  void show_about();

  QTabWidget* document_tabs_{nullptr};
  std::vector<std::unique_ptr<DocumentSession>> sessions_;
  CanvasWidget* canvas_{nullptr};
  QListWidget* layer_list_{nullptr};
  QSlider* opacity_slider_{nullptr};
  QSpinBox* opacity_spin_{nullptr};
  QComboBox* blend_combo_{nullptr};
  QCheckBox* visible_check_{nullptr};
  QCheckBox* lock_transparency_check_{nullptr};
  QAction* selection_new_mode_action_{nullptr};
  QAction* selection_add_mode_action_{nullptr};
  QAction* selection_subtract_mode_action_{nullptr};
  QAction* selection_intersect_mode_action_{nullptr};
  QPushButton* primary_color_button_{nullptr};
  QPushButton* secondary_color_button_{nullptr};
  QDialog* color_dialog_{nullptr};
  QCheckBox* move_auto_select_check_{nullptr};
  QCheckBox* move_show_transform_controls_check_{nullptr};
  QComboBox* transform_reference_combo_{nullptr};
  QDoubleSpinBox* transform_x_spin_{nullptr};
  QDoubleSpinBox* transform_y_spin_{nullptr};
  QDoubleSpinBox* transform_scale_x_spin_{nullptr};
  QDoubleSpinBox* transform_scale_y_spin_{nullptr};
  QPushButton* transform_link_scale_button_{nullptr};
  QDoubleSpinBox* transform_rotation_spin_{nullptr};
  QComboBox* transform_interpolation_combo_{nullptr};
  QPushButton* transform_apply_button_{nullptr};
  QPushButton* transform_cancel_button_{nullptr};
  QCheckBox* clone_aligned_check_{nullptr};
  QCheckBox* wand_contiguous_check_{nullptr};
  QCheckBox* wand_sample_all_layers_check_{nullptr};
  QComboBox* brush_preset_combo_{nullptr};
  QComboBox* gradient_method_combo_{nullptr};
  QSpinBox* gradient_opacity_spin_{nullptr};
  QSlider* gradient_opacity_slider_{nullptr};
  QCheckBox* gradient_reverse_check_{nullptr};
  QPushButton* gradient_preview_button_{nullptr};
  QPushButton* gradient_edit_stops_button_{nullptr};
  QFontComboBox* text_font_combo_{nullptr};
  QDoubleSpinBox* text_size_spin_{nullptr};
  QPushButton* text_bold_button_{nullptr};
  QPushButton* text_italic_button_{nullptr};
  QComboBox* text_smoothing_combo_{nullptr};
  QPushButton* text_color_button_{nullptr};
  QPushButton* text_align_left_button_{nullptr};
  QPushButton* text_align_center_button_{nullptr};
  QPushButton* text_align_right_button_{nullptr};
  QListWidget* history_list_{nullptr};
  QLabel* document_info_label_{nullptr};
  QLabel* active_layer_info_label_{nullptr};
  QLabel* active_layer_geometry_label_{nullptr};
  QLabel* active_layer_mask_label_{nullptr};
  QLabel* active_layer_adjustment_label_{nullptr};
  QLabel* active_layer_text_label_{nullptr};
  QLabel* active_tool_info_label_{nullptr};
  QLabel* canvas_info_label_{nullptr};
  QAction* undo_action_{nullptr};
  QAction* redo_action_{nullptr};
  QAction* view_rulers_action_{nullptr};
  QAction* view_grid_action_{nullptr};
  QAction* view_guides_action_{nullptr};
  QAction* view_snap_action_{nullptr};
  QAction* view_lock_guides_action_{nullptr};
  QAction* view_snap_guides_action_{nullptr};
  QAction* view_snap_grid_action_{nullptr};
  QAction* view_snap_document_action_{nullptr};
  QAction* view_snap_layers_action_{nullptr};
  QAction* view_snap_selection_action_{nullptr};
  QAction* layer_blending_options_action_{nullptr};
  QAction* layer_copy_style_action_{nullptr};
  QAction* layer_paste_style_action_{nullptr};
  QAction* layer_delete_style_action_{nullptr};
  QAction* layer_rasterize_action_{nullptr};
  QAction* layer_rasterize_layer_style_action_{nullptr};
  QAction* delete_layer_mask_action_{nullptr};
  QAction* link_layer_mask_action_{nullptr};
  QAction* disable_layer_mask_action_{nullptr};
  QAction* invert_layer_mask_action_{nullptr};
  QAction* apply_layer_mask_action_{nullptr};
  QAction* move_tool_action_{nullptr};
  QAction* type_tool_action_{nullptr};
  QAction* language_english_action_{nullptr};
  QAction* language_japanese_action_{nullptr};
  std::vector<QAction*> document_actions_;
  std::vector<QWidget*> document_widgets_;
  QWidget* window_chrome_controls_{nullptr};
  QToolButton* maximize_button_{nullptr};
  QMenu* legacy_plugins_menu_{nullptr};
  QMenu* recent_files_menu_{nullptr};
  FilterRegistry filters_;
  FormatRegistry formats_;
  PluginHost plugin_host_;
  QPageLayout print_page_layout_;
  std::optional<ClipboardPayload> clipboard_;
  std::optional<LayerStyle> layer_style_clipboard_;
  std::optional<QByteArray> patchy_system_clipboard_signature_;
  QStringList recent_files_;
  CanvasTool current_tool_{CanvasTool::Brush};
  CanvasWidget::SelectionMode current_selection_mode_{CanvasWidget::SelectionMode::Replace};
  CanvasWidget::MarqueeStyle current_marquee_style_{CanvasWidget::MarqueeStyle::Normal};
  int current_marquee_width_{1024};
  int current_marquee_height_{768};
  int current_selection_feather_radius_{0};
  bool current_selection_antialias_{true};
  bool view_rulers_visible_{false};
  bool view_grid_visible_{false};
  bool view_guides_visible_{true};
  bool view_guides_locked_{false};
  bool view_snap_enabled_{true};
  bool view_snap_to_guides_{true};
  bool view_snap_to_grid_{true};
  bool view_snap_to_document_{true};
  bool view_snap_to_layers_{true};
  bool view_snap_to_selection_{true};
  int view_grid_spacing_32_{576};
  int view_grid_subdivisions_{4};
  int view_grid_style_{0};
  QColor view_grid_color_{78, 154, 255, 105};
  QColor view_guide_color_{255, 70, 180, 230};
  std::vector<std::pair<QAction*, std::vector<CanvasTool>>> option_actions_;
  std::vector<QAction*> transform_option_actions_;
  std::vector<std::function<void()>> retranslation_callbacks_;
  bool updating_transform_controls_{false};
  bool updating_layer_controls_{false};
  bool updating_layer_list_{false};
  bool right_dock_resizing_{false};
  QPoint right_dock_resize_start_global_;
  int right_dock_resize_start_width_{0};
  bool native_resizable_frame_applied_{false};
  bool chrome_resizing_{false};
  bool chrome_resize_cursor_active_{false};
  Qt::Edges chrome_resize_edges_;
  QPoint chrome_resize_start_global_;
  QRect chrome_resize_start_geometry_;
  bool chrome_dragging_{false};
  QPoint chrome_drag_position_;
};

}  // namespace patchy::ui
