#pragma once

#include "core/adjustment_layer.hpp"
#include "core/document.hpp"
#include "core/text_warp.hpp"
#include "filters/filter_registry.hpp"
#include "formats/format_registry.hpp"
#include "plugins/plugin_host.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/channel_panel.hpp"
#include "ui/hotkey_registry.hpp"
#include "ui/image_document_io.hpp"
#include "ui/stress_test.hpp"

#include <QByteArray>
#include <QColor>
#include <QDialog>
#include <QKeySequence>
#include <QListWidget>
#include <QMainWindow>
#include <QPageLayout>
#include <QPixmap>
#include <QPoint>
#include <QPointer>
#include <QRect>
#include <QString>
#include <QStringList>
#include <array>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

class QAction;
class QActionGroup;
class QCheckBox;
class QCloseEvent;
class QComboBox;
class QDialog;
class QDockWidget;
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
class QTimer;

namespace patchy::ui {

struct HueSaturationSettings;
struct LevelsSettings;
struct UpdateInfo;
class BrushDynamicsButton;
class BrushTipLibrary;
class BrushTipPicker;
class DocumentFloatWindow;
class PalettePanel;
class PatternLibrary;
class StyleLibrary;
class ZoomPercentEdit;
class ZoomStatusBar;

class MainWindow final : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow() override;
  // True only where Patchy draws its own window frame (Windows). macOS/Linux use the
  // native frame: no frameless flag, no chrome buttons, no edge-resize machinery.
  [[nodiscard]] static bool use_custom_window_chrome();
  // Photoshop-mac convention: two-finger scroll pans and pinch zooms, so plain wheel
  // zooming defaults OFF on macOS; Windows/Linux keep wheel-zooms-on. Users flip it in
  // Preferences > Canvas either way (the setting key is shared across platforms).
#ifdef Q_OS_MACOS
  static constexpr bool kWheelZoomsDefault = false;
#else
  static constexpr bool kWheelZoomsDefault = true;
#endif
  void add_document_session(Document document, QString title, QString path = {});
  void open_command_line_files(const QStringList& paths);
  // Bring this already-running window to the foreground and open the files a second launch handed off
  // via the single-instance channel (see src/app/main.cpp).
  void activate_for_second_instance(const QStringList& paths);
  // Save a PNG grab of this window — or a named child widget, optionally cropped to a
  // sub-rect of it — for external verification tooling (the `--screenshot` flag in
  // src/app/main.cpp). Returns false when the widget name is unknown or the save fails.
  bool save_debug_screenshot(const QString& file_path, const QString& widget_name = {},
                             const QRect& region = {});
  void show_update_available(const UpdateInfo& update);
  void refresh_native_frame_after_overlay();
  [[nodiscard]] const HotkeyRegistry& hotkey_registry() const noexcept { return hotkey_registry_; }
  [[nodiscard]] BrushTipLibrary& brush_tip_library();
  [[nodiscard]] PatternLibrary& pattern_library();
  [[nodiscard]] StyleLibrary& style_library();
  void set_active_brush_tip(const QString& tip_id, bool announce);
  void define_brush_tip_from_selection();
  [[nodiscard]] QImage capture_brush_tip_define_source() const;
  // Profiling stress test, CLI entry (`patchy --stress-test=<preset>`): defers the
  // run until the event loop starts, writes the report, and exits the application
  // with 0 (success) or 1 (failure). See main_window_stress_test.cpp.
  void start_cli_stress_test(StressTestOptions options);

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
      // Selection state at this point in history, so undo/redo restores the
      // selection alongside the pixels (and selection-only edits are undoable).
      CanvasWidget::SelectionSnapshot selection;
    };

    Document document;
    QString title;
    QString path;
    std::optional<ImageSaveOptions> image_save_options;
    QString image_save_options_path;
    QString image_save_options_extension;
    // Stable identity for cross-session references (ids, not pointers: sessions_
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
    std::int64_t revision{0};
    std::int64_t saved_revision{0};
    // True when the top undo entry is a coalescable selection move, so the next
    // move in the run merges into it instead of pushing a new entry.
    bool selection_move_coalescing{false};
  };

  struct ClipboardPayload {
    PixelBuffer pixels;
    QPoint origin;
    std::vector<Layer> layers_top_to_bottom;
    // Sources referenced by copied smart-object layers so a cross-document paste can
    // adopt them into the target's store (shared_ptr payloads: copies are cheap).
    std::vector<SmartObjectSource> smart_object_sources;
    // Pattern tiles referenced by copied layers' styles (Pattern Overlay / Bevel
    // Texture), adopted the same way on paste (implicitly shared pixels).
    std::vector<PatternResource> pattern_resources;
  };

  // Defaults match the Round startup preset (brush_presets.cpp); load_tool_settings()
  // re-derives them from the preset on every launch.
  struct BrushToolSettings {
    int size{12};
    int opacity{100};
    int softness{20};
  };

  class PreviewDialogEditLock {
  public:
    explicit PreviewDialogEditLock(MainWindow& window) noexcept;
    PreviewDialogEditLock(const PreviewDialogEditLock&) = delete;
    PreviewDialogEditLock& operator=(const PreviewDialogEditLock&) = delete;
    PreviewDialogEditLock(PreviewDialogEditLock&& other) noexcept;
    PreviewDialogEditLock& operator=(PreviewDialogEditLock&& other) = delete;
    ~PreviewDialogEditLock();

    void release() noexcept;

  private:
    MainWindow* window_{nullptr};
  };

  friend class MainWindowTestAccess;
  // Drives the profiling stress test through MainWindow's private API
  // (main_window_stress_test.cpp).
  friend class StressTestRunner;
  // Hosts a floated document's canvas; forwards close/activate/drag events back
  // into the private session machinery (document_float_window.cpp).
  friend class DocumentFloatWindow;

  // Preferences entry for the stress test: warning dialog, close-all, run,
  // results dialog. The scenario core shared with the CLI path lives in
  // run_stress_test_scenario.
  void run_stress_test_interactive(StressPreset preset);
  [[nodiscard]] StressReport run_stress_test_scenario(const StressTestOptions& options);

  void create_actions();
  void configure_window_chrome();
  void position_window_chrome_controls();
  void ensure_native_resizable_frame();
  void resync_native_frame_geometry();
  void restore_maximized_under_cursor(QPoint global_cursor);
  void restore_window_from_maximize();
  void clamp_window_to_available_screen();
  void save_window_geometry() const;
  bool restore_window_geometry();
  bool handle_right_dock_resize_event(QObject* watched, QEvent* event);
  void update_right_dock_resize_handle_geometry(QWidget* host);
  void set_right_dock_stack_width(int width);
  bool handle_window_resize_event(QObject* watched, QEvent* event);
  void update_window_resize_cursor(Qt::Edges edges);
  void clear_window_resize_cursor();
  bool handle_spacebar_canvas_pan_event(QObject* watched, QEvent* event);
  [[nodiscard]] bool spacebar_canvas_pan_target_in_window(QWidget* widget) const noexcept;
  [[nodiscard]] bool spacebar_canvas_pan_target_is_canvas(QWidget* widget) const noexcept;
  [[nodiscard]] bool spacebar_canvas_pan_blocked_by_text_input(QWidget* widget) const noexcept;
  void update_spacebar_canvas_pan_cursor(Qt::CursorShape cursor);
  void clear_spacebar_canvas_pan_cursor();
  void reset_spacebar_canvas_pan();
  void update_pen_cursor_override(QObject* watched, QEvent* event);
  void set_canvas_pen_cursor_override(bool active);
  void update_pen_hover_tooltip(QObject* watched, QEvent* event);
  void show_pen_hover_tooltip();
  void cancel_pen_hover_tooltip();
  void resize_window_from_global_point(QPoint global_position);
  void set_window_screen_size(QSize physical_size);
  void create_docks();
  void create_palette_dock();
  // Palette (indexed) mode plumbing. Every document-palette mutation goes through
  // these so the undo snapshot, the revision bump (app-globally unique values,
  // keying the canvas LUT cache), the indexed_palette export mirror, and the UI
  // refreshes stay in lockstep.
  [[nodiscard]] static std::uint64_t next_palette_revision() noexcept;
  [[nodiscard]] std::vector<RgbColor> displayed_palette_colors();
  void set_document_palette(std::vector<RgbColor> colors, const QString& undo_label, const QString& status_message);
  void apply_palette_entry_color(int index, RgbColor color, bool remap_pixels, const QString& undo_label);
  void edit_palette_entry(int index);
  void swap_palette_entries(int from_index, int to_index);
  void copy_selected_palette_color();
  void paste_clipboard_color_to_palette();
  // Save-dialog defaults, adjusted for palette-mode documents: BMP preselects an
  // exact indexed encoding at the palette's depth so a plain OK writes the
  // document palette verbatim. persist_image_save_defaults keeps those automatic
  // choices out of the global defaults so RGB documents are unaffected.
  [[nodiscard]] ImageSaveOptions image_save_defaults_for_document();
  void persist_image_save_defaults(const ImageSaveOptions& options);
  void add_palette_entry_from_foreground();
  void remove_palette_entry(int index);
  void extract_palette_from_image();
  void load_palette_from_file();
  void save_palette_to_file();
  void convert_document_to_indexed();
  void convert_document_to_rgb();
  void snap_layers_to_palette(bool active_layer_only);
  void refresh_palette_panel();
  void refresh_palette_mode_chip();
  void maybe_offer_indexed_palette_adoption();
  void schedule_palette_compliance_check();
  void run_palette_compliance_check();
  void configure_canvas(CanvasWidget* canvas);
  void activate_document_tab(int index);
  // Makes `canvas` the active document (canvas_). The only writer of canvas_ after
  // construction; every activation source (tab switch, float window, canvas focus)
  // funnels through here so text-editor settle and panel refresh stay consistent.
  void activate_document_canvas(CanvasWidget* canvas);
  bool close_document_tab(int index);
  bool close_document_session(DocumentSession& target_session);
  bool close_active_document();
  void close_other_document_tabs(int index);
  void close_all_document_tabs();
  // Float in Window / Dock to Tabs: moves the session's canvas between the tab
  // widget and its own DocumentFloatWindow. Refused while the preview-dialog
  // edit lock is held.
  void float_document_session(DocumentSession& target_session);
  // Tab tear-off gesture: floats the tab's document at the cursor and hands the
  // drag to the OS (startSystemMove) so the window keeps following in one motion.
  void tear_off_document_tab(int index, QPoint global_position);
  void dock_document_session(DocumentSession& target_session);
  void float_active_document();
  void dock_active_document();
  void consolidate_all_to_tabs();
  void float_all_documents();
  // Photoshop's arrange semantics: both float every document first, then lay the
  // float windows out over the document workspace (grid / staggered stack).
  void tile_float_windows();
  void cascade_float_windows();
  // The region Tile/Cascade (and new floats) arrange windows over: the tab-widget
  // area in global coordinates, so the tool palette, options bar, and panels stay
  // visible. Falls back to the screen's work area when the workspace is degenerate.
  [[nodiscard]] QRect document_workspace_global() const;
  bool handle_float_window_close_request(DocumentFloatWindow* window);
  void handle_float_window_activated(DocumentFloatWindow* window);
  // A float window moved: if the user is dragging it (left button held), arm the
  // dock-on-drop check. Programmatic moves (creation, tile, cascade) never arm.
  void handle_float_window_drag_moved(DocumentFloatWindow* window);
  // The strip a dragged float docks into when dropped there: the tab bar's global
  // rect (or the tab widget's top strip when no tabs are left).
  [[nodiscard]] QRect float_dock_zone_global() const;
  void maybe_dock_float_at(DocumentFloatWindow* window, QPoint global_position);
  // "Release here to dock" affordance: lights the tab strip while a dragged
  // float hovers the dock zone.
  void update_float_dock_highlight(QPoint global_position);
  void set_float_dock_highlight_visible(bool visible);
  [[nodiscard]] DocumentSession* session_for_float_window(DocumentFloatWindow* window) noexcept;
  // Successor for canvas_ after a close: the current tab's canvas, else the most
  // recent floated session, else null (null iff sessions_ is empty).
  [[nodiscard]] CanvasWidget* fallback_active_canvas() noexcept;
  [[nodiscard]] bool any_document_floated() const noexcept;
  void show_document_tab_context_menu(const QPoint& position);
  [[nodiscard]] bool confirm_close_session(DocumentSession& target_session);
  [[nodiscard]] bool maybe_save_session(DocumentSession& target_session);
  void refresh_document_tab_titles();
  void refresh_document_window_title();
  void set_session_saved(DocumentSession& target_session);
  void mark_session_modified(DocumentSession& target_session);
  [[nodiscard]] bool session_is_modified(const DocumentSession& target_session) const noexcept;
  // Display title shared by tab text and float-window titles: "Untitled" fallback
  // plus the modified '*' suffix.
  [[nodiscard]] QString session_display_title(const DocumentSession& target_session) const;
  [[nodiscard]] DocumentSession* session_for_canvas(CanvasWidget* canvas) noexcept;
  [[nodiscard]] const DocumentSession* session_for_canvas(CanvasWidget* canvas) const noexcept;
  [[nodiscard]] DocumentSession* session_with_id(std::int64_t session_id) noexcept;
  [[nodiscard]] std::vector<DocumentSession*> open_smart_object_child_sessions(std::int64_t parent_session_id);
  void activate_document_session(DocumentSession& target_session);
  // The active document's session (resolved through canvas_), or null when no
  // document is open. session()/document() are the throwing conveniences.
  [[nodiscard]] DocumentSession* active_session() noexcept { return session_for_canvas(canvas_); }
  [[nodiscard]] const DocumentSession* active_session() const noexcept { return session_for_canvas(canvas_); }
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
  void import_from_scanner();
  void import_sprite_sheet();
  void export_sprite_sheet();
  void set_tile_preview_visible(bool visible, QAction* toggle_action);
  bool accept_open_file_drag(QDropEvent* event);
  bool open_dropped_files(QDropEvent* event);
  bool save_document();
  bool save_document_as();
  // flatten_confirmed: the caller already ran confirm_flatten_layers_for_save() for this
  // save, so the layers-will-be-flattened prompt is skipped (never pass true without
  // asking first).
  bool save_document_to_path(QString path, std::optional<ImageSaveOptions> image_options = std::nullopt,
                             bool flatten_confirmed = false);
  bool confirm_flatten_layers_for_save();
  void export_flat_image();
  void page_setup();
  void print_document();
  void show_preferences();
  void new_guide_dialog();
  void new_guide_layout_dialog();
  void clear_guides();
  void clear_selected_guides();
  void apply_canvas_aid_settings(CanvasWidget* canvas) const;
  void apply_pen_input_settings(CanvasWidget* canvas) const;
  void load_pen_input_settings();
  void save_pen_input_settings() const;
  void activate_tool(CanvasTool tool);
  void handle_pen_button_action(PenButtonAction action);
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
  void warp_transform_active_layer();
  void add_text_at(QPoint document_point, QRect requested_text_box = {});
  void cancel_text_editor(QTextEdit* editor, std::optional<LayerId> layer_id);
  void commit_text_editor(QTextEdit* editor, QPoint document_point, std::optional<LayerId> layer_id);
  bool commit_active_text_editor();
  bool cancel_active_text_editor();
  void finish_active_text_editor();
  void apply_filter(const QString& identifier);
  void populate_new_adjustment_layer_menu(QMenu* menu, const QString& object_name_prefix = {});
  void new_levels_adjustment_layer();
  void levels_dialog();
  void apply_levels_adjustment(const LevelsSettings& settings, bool allow_identity = false);
  void new_curves_adjustment_layer();
  void curves_dialog();
  void apply_curves_adjustment(const CurvesAdjustment& curves, bool allow_identity = false);
  void new_hue_saturation_adjustment_layer();
  void hue_saturation_dialog();
  void apply_hue_saturation_adjustment(const HueSaturationSettings& hue_saturation, bool allow_identity = false);
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
  void add_layer_mask();
  void delete_active_layer_mask();
  void set_active_layer_mask_linked(bool linked);
  void set_layer_edit_target_ui(CanvasWidget::LayerEditTarget target, bool announce);
  void set_channel_edit_target(ChannelPanel::RowKind kind, ChannelId id, bool overlay, bool announce = true);
  void set_mask_overlay_shown(bool shown);
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
  void export_smart_object_contents();
  void open_smart_object_contents();
  bool commit_smart_object_child_session(DocumentSession& child_session);
  void refresh_external_smart_object_after_save(DocumentSession& child_session);
  void update_smart_object_content();
  void relink_smart_object_contents();
  void relink_smart_object_contents_with_path(const QString& path);
  void embed_linked_smart_object();
  void refresh_smart_object_layers_for_source(Document& target_document, const std::string& source_uuid,
                                              const QImage& rendered_image, double content_dpi,
                                              bool include_external_locked);
  void replace_smart_object_contents();
  void replace_smart_object_contents_with_path(const QString& path);
  void convert_to_smart_object();
  void new_smart_object_via_copy();
  void place_embedded_file();
  void place_embedded_file_with_path(const QString& path);
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
  void merge_down();
  void fill_active_layer();
  void fill_active_layer_with_color(QColor color, QString label);
  void clear_active_layer();
  void stroke_selection();
  void apply_brush_tip_to_canvas(CanvasWidget* canvas);
  void import_brush_tips_from_abr();
  void open_brush_tip_manager();
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
  void apply_pending_layer_opacity();
  void finish_pending_layer_opacity_edit();
  void reset_pending_layer_opacity_edit();
  void set_active_layer_blend(int index);
  void set_active_layer_visible(bool visible);
  void set_layer_lock_flag_state(LayerId id, LayerLockFlags flag, bool locked);
  void set_active_layer_lock_flag(LayerLockFlags flag, bool locked);
  void set_active_layer_lock_all(bool locked);
  void toggle_active_layer_clipping();
  void refresh_layer_clipping_action_state();
  void set_layer_mask_view_shown(bool shown);
  [[nodiscard]] LayerLockFlags layer_id_effective_lock_flags(LayerId id) const;
  [[nodiscard]] LayerLockFlags layer_id_ancestor_lock_flags(LayerId id) const;
  [[nodiscard]] bool layer_id_locks_image_pixels(LayerId id) const;
  [[nodiscard]] bool layer_id_locks_position(LayerId id) const;
  [[nodiscard]] std::vector<LayerId> layer_ids_without_image_pixel_lock(std::vector<LayerId> ids) const;
  bool show_pixel_lock_message_if_all_locked(const std::vector<LayerId>& requested_ids,
                                             const std::vector<LayerId>& editable_ids);
  void undo();
  void redo();
  void push_undo_snapshot(QString label);
  // Session-targeted overload: canvas edit callbacks resolve their OWNING session at
  // fire time, so an edit on a non-active canvas (or an async completion landing after
  // the active document changed) never snapshots the wrong document. The no-session
  // signature above means "the active session".
  void push_undo_snapshot(DocumentSession& target_session, QString label);
  // Push an undo entry for a selection-only edit, holding the pre-edit selection
  // `before` against the current (unchanged) document. When `coalesce` is true
  // and the previous entry was also a coalescing move, the new state merges into
  // it (a run of moves/nudges is a single undo step). Session-targeted only: the
  // one caller is the canvas selection-history callback, which must never default
  // to the active session.
  void push_selection_history(DocumentSession& target_session, QString label,
                              CanvasWidget::SelectionSnapshot before, bool coalesce = false);
  // Mirror the given effective combine mode onto the Options-bar mode buttons
  // (used for both committed modes and the live Shift/Alt override).
  void update_selection_mode_buttons(CanvasWidget::SelectionMode mode);
  // Apply the stored per-tool combine modes to a (new) canvas.
  void apply_selection_modes_to_canvas(CanvasWidget* canvas);
  void refresh_layer_list();
  void refresh_layer_thumbnails();
  // Revision-keyed thumbnail pixmaps for the ACTIVE document's layer rows.
  // refresh_layer_list() destroys and rebuilds every row widget, so without
  // this cache each rebuild (add layer, undo, reorder...) re-rendered every
  // layer's thumbnail from its full pixel buffer. Safe because layer revisions
  // are app-globally unique (core/layer.cpp) - a value can never name two
  // different contents. Cleared on document switches; pruned against the layer
  // tree by refresh_layer_thumbnails.
  struct LayerThumbnailCacheEntry {
    std::uint64_t content_revision{0};
    QPixmap content;
    std::uint64_t mask_revision{0};
    QPixmap mask;
  };
  [[nodiscard]] QPixmap cached_layer_content_thumbnail(const Layer& layer);
  [[nodiscard]] QPixmap cached_layer_mask_thumbnail(const Layer& layer);
  void refresh_layer_controls();
  void refresh_channel_panel();
  [[nodiscard]] QPixmap cached_channel_thumbnail(const DocumentChannel& channel);
  void create_alpha_channel();
  void save_selection_as_channel();
  void load_channel_as_selection();
  void load_channel_as_selection(ChannelPanel::RowKind kind, ChannelId id);
  void rename_active_channel();
  void invert_active_channel();
  void delete_active_channel();
  void reorder_channels_from_panel(std::vector<ChannelId> order);
  [[nodiscard]] DocumentChannel* selected_panel_channel() noexcept;
  [[nodiscard]] const DocumentChannel* selected_panel_channel() const noexcept;
  void refresh_edit_target_chip();
  void restore_channel_target_after_document_reset(CanvasWidget::LayerEditTarget target,
                                                   std::optional<ChannelId> channel_id,
                                                   CanvasWidget::MaskDisplayMode display_mode);
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
  // Restart the debounce so the live tool-option sliders flush to disk once,
  // after the drag settles, instead of on every intermediate value.
  void schedule_save_tool_settings();
  [[nodiscard]] BrushToolSettings& active_stored_brush_settings();
  void stash_active_brush_settings();
  void apply_active_brush_settings_to_canvas();
  void set_eraser_brush_settings_active(bool active);
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
  // Photoshop's Warp Text: opens the style/bend/distortion dialog for the active
  // text layer (committing any open inline edit first) with live preview; OK is one
  // undo step, Cancel restores the pre-dialog pixels and metadata.
  void request_warp_text_dialog();
  // Photoshop's Character panel (leading / tracking / glyph scales) for the ACTIVE
  // inline editor session: applies live to the selection (whole text when nothing is
  // selected) and stays exempt from the editor's focus-loss auto-commit.
  void open_text_character_dialog();
  void sync_text_character_dialog_from_editor();
  void apply_text_character_leading_to_active_editor();
  void apply_text_character_tracking_to_active_editor();
  void apply_text_character_glyph_scales_to_active_editor();
  // Re-renders a text layer with `warp` applied (identity = unwarped) and refreshes
  // the warp/transform/raster-status metadata. Returns false when the layer's text
  // cannot be rendered.
  bool apply_text_warp_to_layer(Layer& layer, const patchy::TextWarp& warp);
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
  std::optional<LayerId> take_provisional_text_layer(QTextEdit* editor);
  void update_text_editor_transform_overlay(QTextEdit* editor);
  void remove_text_editor_transform_overlay(QTextEdit* editor);
  void handle_canvas_view_changed(CanvasWidget* canvas);
  [[nodiscard]] bool is_text_option_widget(QWidget* widget) const;
  void apply_transform_controls_from_ui();
  void sync_transform_controls_from_canvas();
  void register_option_action(QWidget* widget, std::initializer_list<CanvasTool> tools);
  void register_retranslation(std::function<void()> callback);
  void retranslate_ui();
  void retranslate_bound_children();
  void retranslate_blend_combo();
  void retranslate_brush_preset_combo();
  void refresh_language_actions();
  void refresh_options_bar();
  void register_document_action(QAction* action);
  void register_document_widget(QWidget* widget);
  void register_hotkey(QAction* action, QString id, QList<QKeySequence> default_shortcuts, QString category = {});
  void register_hotkey(QAction* action, QString id, QKeySequence default_shortcut = {}, QString category = {});
  void update_document_action_state();
  [[nodiscard]] PreviewDialogEditLock lock_preview_dialog_edits();
  void begin_preview_dialog_edit_lock();
  void end_preview_dialog_edit_lock();
  [[nodiscard]] bool preview_dialog_edit_locked() const noexcept;
  [[nodiscard]] bool document_action_enabled_during_preview_lock(const QAction* action) const;
  bool show_preview_dialog_edit_lock_message();
  void sync_brush_controls_from_canvas();
  void load_recent_files();
  void save_recent_files() const;
  void add_recent_file(QString path);
  void rebuild_recent_files_menu();
  void load_recent_folders();
  void save_recent_folders() const;
  void add_recent_folder(QString dir);
  void rebuild_recent_folders_menu();
  void configure_recent_files_context_menu(QMenu* menu);
  void show_recent_file_context_menu(const QPoint& position);
  void show_recent_file_context_menu(QMenu* menu, const QPoint& position);
  void reveal_path_in_file_explorer(const QString& path, bool is_file);
  void open_recent_document(QString path);
  QAction* add_tool_action(QToolBar* palette, QActionGroup* group, QString label, CanvasTool tool,
                           QKeySequence shortcut);
  void update_history(QString label);
  void update_undo_redo_actions();
  void show_about();

  QTabWidget* document_tabs_{nullptr};
  std::vector<std::unique_ptr<DocumentSession>> sessions_;
  std::int64_t next_session_id_{1};
  // The ACTIVE document's canvas, the single source of truth for "current document"
  // (session()/document() resolve through it). Writers: activate_document_canvas (every
  // activation source funnels through it) plus add_document_session's new-document tail;
  // never derive the active document from document_tabs_'s current tab, which is wrong
  // once a document floats in its own window.
  CanvasWidget* canvas_{nullptr};
  std::unordered_map<LayerId, LayerThumbnailCacheEntry> layer_thumbnail_cache_;
  struct ChannelThumbnailCacheEntry {
    std::uint64_t content_revision{0};
    QPixmap thumbnail;
  };
  std::unordered_map<ChannelId, ChannelThumbnailCacheEntry> channel_thumbnail_cache_;
  bool swallow_next_canvas_left_press_{false};
  QListWidget* layer_list_{nullptr};
  ChannelPanel* channel_panel_{nullptr};
  QDockWidget* channel_dock_{nullptr};
  QSlider* opacity_slider_{nullptr};
  QSpinBox* opacity_spin_{nullptr};
  QTimer* layer_opacity_apply_timer_{nullptr};
  QTimer* layer_opacity_idle_timer_{nullptr};
  QTimer* tool_settings_save_timer_{nullptr};
  QComboBox* blend_combo_{nullptr};
  QCheckBox* visible_check_{nullptr};
  QToolButton* lock_transparent_pixels_button_{nullptr};
  QToolButton* lock_image_pixels_button_{nullptr};
  QToolButton* lock_position_button_{nullptr};
  QToolButton* lock_all_button_{nullptr};
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
  // Shared session trio: warp-mode toggle + apply + cancel, shown for BOTH the
  // free-transform and warp sessions (Photoshop's options-bar layout).
  QPushButton* transform_warp_mode_button_{nullptr};
  QPushButton* transform_apply_button_{nullptr};
  QPushButton* transform_cancel_button_{nullptr};
  QComboBox* warp_style_combo_{nullptr};
  QDoubleSpinBox* warp_bend_spin_{nullptr};
  QCheckBox* clone_aligned_check_{nullptr};
  QCheckBox* wand_contiguous_check_{nullptr};
  QCheckBox* wand_sample_all_layers_check_{nullptr};
  QCheckBox* quick_select_sample_all_layers_check_{nullptr};
  QCheckBox* quick_select_enhance_edge_check_{nullptr};
  QComboBox* brush_preset_combo_{nullptr};
  BrushTipLibrary* brush_tip_library_{nullptr};
  PatternLibrary* pattern_library_{nullptr};
  StyleLibrary* style_library_{nullptr};
  BrushTipPicker* brush_tip_picker_{nullptr};
  BrushDynamicsButton* brush_dynamics_button_{nullptr};
  QString active_brush_tip_id_;
  // Session-only dynamics for the procedural Round brush. Deliberately never persisted: every
  // launch starts with a plain Round brush, so a weird leftover setup cannot confuse anyone.
  patchy::BrushDynamics round_brush_dynamics_{};
  double round_brush_base_angle_degrees_{0.0};
  double round_brush_base_roundness_{100.0};
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
  QPushButton* text_warp_button_{nullptr};
  // Character panel (leading / tracking / glyph scales) for the live editor session;
  // the dialog and its controls are exempt from the focus-loss auto-commit via
  // is_text_option_widget, unlike Warp which commits first.
  QPushButton* text_character_button_{nullptr};
  QPointer<QDialog> text_character_dialog_;
  QCheckBox* text_character_auto_leading_{nullptr};
  QDoubleSpinBox* text_character_leading_spin_{nullptr};
  QSpinBox* text_character_tracking_spin_{nullptr};
  QSpinBox* text_character_h_scale_spin_{nullptr};
  QSpinBox* text_character_v_scale_spin_{nullptr};
  // Session apply/cancel for the inline text editor (Photoshop's options-bar
  // commit/cancel); visible only while an editor is open, managed by
  // refresh_options_bar(), never registered as per-tool option widgets.
  QPushButton* text_apply_button_{nullptr};
  QPushButton* text_cancel_button_{nullptr};
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
  QAction* layer_clipping_mask_action_{nullptr};
  QAction* layer_convert_smart_object_action_{nullptr};
  QAction* layer_smart_object_edit_action_{nullptr};
  QAction* layer_smart_object_replace_action_{nullptr};
  QAction* layer_smart_object_export_action_{nullptr};
  QAction* layer_smart_object_via_copy_action_{nullptr};
  QAction* layer_smart_object_update_action_{nullptr};
  QAction* layer_smart_object_relink_action_{nullptr};
  QAction* layer_smart_object_embed_action_{nullptr};
  // A discoverable alias for Rasterize in the Smart Objects menus.
  QAction* layer_smart_object_to_normal_action_{nullptr};
  QAction* delete_layer_mask_action_{nullptr};
  QAction* link_layer_mask_action_{nullptr};
  QAction* disable_layer_mask_action_{nullptr};
  QAction* invert_layer_mask_action_{nullptr};
  QAction* apply_layer_mask_action_{nullptr};
  QAction* edit_layer_mask_action_{nullptr};
  QAction* mask_overlay_action_{nullptr};
  QAction* view_layer_mask_action_{nullptr};
  QAction* channel_new_action_{nullptr};
  QAction* channel_save_selection_action_{nullptr};
  QAction* channel_load_selection_action_{nullptr};
  QAction* channel_rename_action_{nullptr};
  QAction* channel_invert_action_{nullptr};
  QAction* channel_delete_action_{nullptr};
  QToolButton* mask_edit_mode_chip_{nullptr};
  // Palette (indexed) mode UI: dock panel, status chip, advisory compliance scan.
  PalettePanel* palette_panel_{nullptr};
  QDockWidget* palette_dock_{nullptr};
  QToolButton* palette_mode_chip_{nullptr};
  QTimer* palette_compliance_timer_{nullptr};
  bool palette_compliance_clean_{true};
  QAction* image_mode_rgb_action_{nullptr};
  QAction* image_mode_indexed_action_{nullptr};
  QAction* snap_image_to_palette_action_{nullptr};
  QAction* snap_layer_to_palette_action_{nullptr};
  ZoomStatusBar* zoom_status_bar_{nullptr};
  ZoomPercentEdit* zoom_status_edit_{nullptr};
  QAction* move_tool_action_{nullptr};
  QAction* type_tool_action_{nullptr};
  QActionGroup* tool_action_group_{nullptr};
  QAction* language_english_action_{nullptr};
  QAction* language_japanese_action_{nullptr};
  QAction* float_document_action_{nullptr};
  QAction* dock_document_action_{nullptr};
  QAction* consolidate_tabs_action_{nullptr};
  QAction* float_all_action_{nullptr};
  QAction* tile_windows_action_{nullptr};
  QAction* cascade_windows_action_{nullptr};
  // Tab tear-off drag state: the pressed tab and where the press happened.
  int tab_tear_press_index_{-1};
  QPoint tab_tear_press_global_;
  // Dock-on-drop: moveEvents during a user drag (re)arm this timer; when it fires
  // with the button released and the cursor in the dock zone, the float docks.
  // The candidate is a session id (the stable identity; the window may die first).
  QTimer* float_dock_check_timer_{nullptr};
  std::int64_t float_dock_candidate_session_id_{0};
  // Translucent mouse-transparent overlay over the dock zone (lazily created
  // child of document_tabs_); visible only while a float drag hovers the zone.
  QWidget* float_dock_highlight_{nullptr};
  std::vector<QAction*> document_actions_;
  std::vector<QWidget*> document_widgets_;
  HotkeyRegistry hotkey_registry_;
  int preview_dialog_edit_lock_depth_{0};
  bool scanner_import_active_{false};
  QPointer<QDialog> tile_preview_window_;
  // Canvas that owns the open preview dialog; activation of any other canvas is
  // refused while the lock is held (canvas identity, not tab index: the locked
  // document may live in a float window).
  QPointer<CanvasWidget> preview_dialog_edit_lock_canvas_;
  QWidget* window_chrome_controls_{nullptr};
  QToolButton* maximize_button_{nullptr};
  QMenu* legacy_plugins_menu_{nullptr};
  QMenu* recent_files_menu_{nullptr};
  QMenu* recent_folders_menu_{nullptr};
  FilterRegistry filters_;
  PluginHost plugin_host_;
  QPageLayout print_page_layout_;
  std::optional<ClipboardPayload> clipboard_;
  struct LayerStyleClipboard {
    LayerStyle style;
    std::optional<LayerBlendIf> blend_if;
    // Pattern tiles the copied style references, so pasting into another
    // document can embed them there too.
    std::vector<PatternResource> patterns;
  };
  std::optional<LayerStyleClipboard> layer_style_clipboard_;
  std::optional<QByteArray> patchy_system_clipboard_signature_;
  std::vector<LayerId> pending_layer_opacity_ids_;
  QStringList recent_files_;
  QStringList recent_folders_;
  std::optional<int> pending_layer_opacity_value_;
  CanvasTool current_tool_{CanvasTool::Brush};
  CanvasTool tool_before_eraser_toggle_{CanvasTool::Brush};
  // The current canvas holds the live size/opacity/softness for one settings
  // group; the other group's values wait here. The eraser is its own group so
  // it keeps settings independent of the other painting tools.
  BrushToolSettings stored_paint_brush_settings_{};
  BrushToolSettings stored_eraser_brush_settings_{};
  bool eraser_brush_settings_active_{false};
  // Combine mode per selection tool (indexed by CanvasWidget::selection_tool_index),
  // persisted across documents; each selection tool keeps its own mode.
  std::array<CanvasWidget::SelectionMode, CanvasWidget::kSelectionToolCount> selection_modes_{
      CanvasWidget::SelectionMode::Replace, CanvasWidget::SelectionMode::Replace,
      CanvasWidget::SelectionMode::Replace, CanvasWidget::SelectionMode::Replace,
      CanvasWidget::SelectionMode::Replace, CanvasWidget::SelectionMode::Replace};
  CanvasWidget::MarqueeStyle current_marquee_style_{CanvasWidget::MarqueeStyle::Normal};
  int current_marquee_width_{1024};
  int current_marquee_height_{768};
  int current_marquee_corner_radius_{0};
  int current_selection_feather_radius_{0};
  bool current_selection_antialias_{true};
  bool current_fill_shapes_{false};
  int current_shape_corner_radius_{0};
  CanvasWidget::MarqueeStyle current_shape_style_{CanvasWidget::MarqueeStyle::Normal};
  int current_shape_width_{1024};
  int current_shape_height_{768};
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
  CanvasWidget::PenInputSettings pen_input_settings_{};
  bool wheel_zooms_{kWheelZoomsDefault};
  std::vector<std::pair<QWidget*, std::vector<CanvasTool>>> option_actions_;
  std::vector<QWidget*> transform_option_actions_;
  std::vector<QWidget*> warp_option_actions_;
  std::vector<QWidget*> transform_session_actions_;
  QWidget* options_flow_container_{nullptr};
  std::vector<std::function<void()>> retranslation_callbacks_;
  bool updating_transform_controls_{false};
  bool updating_layer_controls_{false};
  bool updating_layer_list_{false};
  bool pending_layer_opacity_edit_active_{false};
  bool right_dock_resizing_{false};
  QPoint right_dock_resize_start_global_;
  int right_dock_resize_start_width_{0};
  bool spacebar_canvas_pan_down_{false};
  bool spacebar_canvas_pan_dragging_{false};
  bool spacebar_canvas_pan_cursor_active_{false};
  bool canvas_pen_cursor_active_{false};
  QTimer* pen_hover_tooltip_timer_{nullptr};
  QPointer<QWidget> pen_hover_tooltip_widget_;
  QPoint pen_hover_tooltip_global_pos_;
  bool native_resizable_frame_applied_{false};
  bool native_frame_geometry_resynced_{false};
  bool pending_layer_thumbnail_refresh_{false};
  bool pending_channel_thumbnail_refresh_{false};
  bool chrome_resizing_{false};
  bool chrome_resize_cursor_active_{false};
  Qt::Edges chrome_resize_edges_;
  QPoint chrome_resize_start_global_;
  QRect chrome_resize_start_geometry_;
  bool chrome_dragging_{false};
  QPoint chrome_drag_position_;
  // Set at the top of ~MainWindow, before members are destroyed. Teardown-time focus
  // changes (the window close delivers a focus-out while child widgets are still alive)
  // otherwise run the inline-text-editor commit path against destroyed members. Read
  // only as an early-out guard; a bool stays trivially readable through teardown.
  bool shutting_down_{false};
};

}  // namespace patchy::ui
