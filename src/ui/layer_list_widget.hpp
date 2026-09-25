#pragma once

#include "core/layer_tree.hpp"

#include <QAbstractNativeEventFilter>
#include <QBasicTimer>
#include <QByteArray>
#include <QItemSelectionModel>
#include <QListWidget>
#include <QPoint>
#include <QPointer>
#include <QRect>
#include <QStringList>

#include <cstddef>
#include <functional>
#include <cstdint>
#include <optional>
#include <vector>

class QEvent;
class QDragEnterEvent;
class QDragLeaveEvent;
class QDragMoveEvent;
class QDropEvent;
class QLabel;
class QLineEdit;
class QListWidgetItem;
class QMimeData;
class QMouseEvent;
class QObject;
class QPaintEvent;
class QResizeEvent;
class QScrollBar;
class QTimerEvent;
class QWheelEvent;

namespace patchy::ui {

inline constexpr int kLayerIdRole = Qt::UserRole;
inline constexpr int kLayerDepthRole = Qt::UserRole + 1;
inline constexpr int kLayerIsGroupRole = Qt::UserRole + 2;
inline constexpr int kLayerGroupExpandedRole = Qt::UserRole + 3;
inline constexpr const char* kLayerDragMimeType = "application/x-patchy-layer-ids";
// The drag's source document as "<pid>:<session id>": layer ids restart per
// document, so a drop on another document resolves them against this session,
// and a drag from another Patchy process never matches.
inline constexpr const char* kLayerDragSourceSessionMimeType = "application/x-patchy-layer-source-session";

[[nodiscard]] QByteArray layer_ids_to_mime_data(const std::vector<LayerId>& ids);
[[nodiscard]] std::vector<LayerId> layer_ids_from_mime_data(const QMimeData* mime_data);
[[nodiscard]] QByteArray layer_drag_source_session_to_mime_data(std::int64_t session_id);
// nullopt when absent, malformed, or written by another process.
[[nodiscard]] std::optional<std::int64_t> layer_drag_source_session_from_mime_data(const QMimeData* mime_data);

enum class LayerCtrlClickTarget {
  ContentThumbnail,
  MaskThumbnail,
  SmartFilterMaskThumbnail,
  VectorMaskThumbnail
};

class LayerListWidget final : public QListWidget, public QAbstractNativeEventFilter {
public:
  explicit LayerListWidget(QWidget* parent = nullptr);

  void set_drop_finished_callback(std::function<void()> callback);
  // Stamped on every drag's mime data (kLayerDragSourceSessionMimeType) so a
  // drop on another document can resolve the ids; 0 means no document.
  void set_drag_source_session_id(std::int64_t session_id);
  // Photoshop's Alt-hover/Alt-click on the boundary between two rows: can_toggle
  // decides whether the clip cursor shows for (upper, lower); toggle clips or
  // releases the upper layer.
  void set_clip_boundary_callbacks(std::function<bool(LayerId, LayerId)> can_toggle,
                                   std::function<void(LayerId)> toggle);
  // Photoshop's Alt-click on a visibility eye: hide every other layer, or
  // restore the pre-isolation state on the second Alt-click.
  void set_visibility_isolate_callback(std::function<void(LayerId)> callback);
  // Photoshop's eye sweep: press an eye and drag along the column; every eye
  // crossed adopts the state the first toggle produced.
  void set_visibility_sweep_callback(std::function<void(LayerId, bool)> callback);
  void set_ctrl_click_callback(std::function<void(QListWidgetItem*, LayerCtrlClickTarget)> callback);
  void set_thumbnail_click_callback(
      std::function<void(QListWidgetItem*, LayerCtrlClickTarget, Qt::KeyboardModifiers)> callback);
  void set_item_double_click_callback(std::function<void(QListWidgetItem*)> callback);
  // Double-click on the content thumbnail specifically; the rest of the row
  // keeps the plain item double-click behavior.
  void set_content_thumbnail_double_click_callback(std::function<void(QListWidgetItem*)> callback);
  void set_smart_filter_double_click_callback(std::function<void(QListWidgetItem*, std::size_t)> callback);
  // Photoshop's in-place rename: a double-click on the row's name label (or
  // begin_inline_rename from the Rename command) swaps the label for a line
  // edit. Return or focus loss commits through this callback with the row's
  // layer id and the trimmed text; Escape cancels. The callback rebuilds the
  // rows, so it runs deferred.
  void set_inline_rename_callback(std::function<void(LayerId, const QString&)> callback);
  // False when the item has no row widget or name label (no editor opened).
  bool begin_inline_rename(QListWidgetItem* item);
  [[nodiscard]] bool inline_rename_active() const noexcept;
  // Drops an open editor without committing; refresh_layer_list calls it
  // before clearing the rows so a rebuild never commits a half-typed name.
  void cancel_inline_rename();
  [[nodiscard]] QListWidgetItem* item_for_layer_id(LayerId id) const;
  [[nodiscard]] bool drop_in_progress() const noexcept;
  // Blocks drag reordering while the layer name filter hides rows; a reorder
  // would silently move the filtered-out layers sitting between visible ones.
  void set_drag_blocked(bool blocked);
  // Invoked once per refused drag attempt so the owner can explain why.
  void set_drag_blocked_callback(std::function<void()> callback);
  // Plain Escape with the list focused (Select > Deselect Layers).
  void set_escape_callback(std::function<void()> callback);
  [[nodiscard]] std::optional<LayerDropRequest> take_drop_request();
  // OS file drops (Files as Layers, docs/import.md): the owner maps a drag's
  // mime data to the local files that could become layers; an empty list
  // refuses the drag, so text and other foreign drags never reach the reorder
  // path. Such a drop records a LayerFileDropRequest at the insertion target
  // the preview showed, then fires the drop-finished callback like a layer drop.
  struct LayerFileDropRequest {
    QStringList paths;
    std::optional<LayerId> target_layer_id;
    LayerDropPosition position{LayerDropPosition::OnViewport};
  };
  void set_file_drop_paths_callback(std::function<QStringList(const QMimeData*)> callback);
  [[nodiscard]] std::optional<LayerFileDropRequest> take_file_drop_request();
  void refresh_row_widths();
  bool handle_drag_wheel_at_global_position(QPoint global_position, int primary_delta);

protected:
  bool event(QEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;
  bool nativeEventFilter(const QByteArray& event_type, void* message, qintptr* result) override;
  void setSelection(const QRect& rect, QItemSelectionModel::SelectionFlags command) override;
  bool viewportEvent(QEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override;
  void startDrag(Qt::DropActions supported_actions) override;
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dragMoveEvent(QDragMoveEvent* event) override;
  void dragLeaveEvent(QDragLeaveEvent* event) override;
  void dropEvent(QDropEvent* event) override;
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void scrollContentsBy(int dx, int dy) override;
  void timerEvent(QTimerEvent* event) override;

private:
  struct DropTarget {
    std::optional<LayerId> layer_id;
    LayerDropPosition position{LayerDropPosition::OnViewport};
  };

  struct ClipBoundaryHit {
    LayerId upper{0};
    LayerId lower{0};
  };
  [[nodiscard]] std::optional<ClipBoundaryHit> clip_boundary_hit(QPoint viewport_position) const;
  void update_clip_boundary_cursor(QWidget* hover_widget, QPoint viewport_position,
                                   Qt::KeyboardModifiers modifiers);
  bool handle_clip_boundary_press(QPoint viewport_position, Qt::KeyboardModifiers modifiers);
  void clear_clip_boundary_cursor();
  bool handle_visibility_eye_press(QWidget* eye_button, const QMouseEvent* event);
  bool handle_visibility_sweep_move(QPoint viewport_position, const QMouseEvent* event);
  void end_visibility_sweep();
  [[nodiscard]] std::optional<LayerCtrlClickTarget> ctrl_click_target(QListWidgetItem* item,
                                                                      QPoint viewport_pos) const;
  void toggle_ctrl_selection(QListWidgetItem* item);
  void select_range_to_item(QListWidgetItem* target_item, bool additive = false);
  void begin_single_drag_item(QListWidgetItem* item);
  void set_single_drag_item(QListWidgetItem* item);
  void set_current_item_preserving_scroll(QListWidgetItem* item, QItemSelectionModel::SelectionFlags command);
  void finish_pending_single_select();
  [[nodiscard]] std::vector<LayerId> selected_layer_ids_top_to_bottom() const;
  void finish_inline_rename(bool commit);
  [[nodiscard]] QListWidgetItem* parent_item_for(QListWidgetItem* item) const;
  [[nodiscard]] DropTarget drop_target_at(QPoint viewport_position) const;
  // A drop's position in viewport coordinates whether it arrived at the list or
  // its viewport (drop_event_uses_viewport_coordinates_).
  [[nodiscard]] QPoint drop_viewport_position(const QDropEvent& event) const;
  [[nodiscard]] bool is_layer_drag(const QMimeData* mime_data) const;
  // CopyAction when the source offers it (the cursor shows the copy badge), else
  // the proposed action; the window's file drop does the same.
  static void accept_file_drag(QDropEvent* event);
  [[nodiscard]] LayerDropPosition inferred_drop_position(QListWidgetItem* target_item,
                                                         QPoint viewport_position) const;
  [[nodiscard]] int row_content_left(QListWidgetItem* item) const;
  [[nodiscard]] bool item_accepts_on_drop(QListWidgetItem* item) const;
  void update_drop_preview(QPoint viewport_position);
  void clear_drop_preview();
  QWidget* ensure_insertion_indicator();
  QWidget* ensure_folder_highlight_indicator();
  void update_drop_preview_widgets();
  void update_auto_scroll(QPoint viewport_position);
  void stop_auto_scroll();
  void apply_auto_scroll_step();
  void install_drag_wheel_hook();
  void remove_drag_wheel_hook();
  [[nodiscard]] bool wheel_event_targets_list(QObject* watched, const QWheelEvent& event) const;
  [[nodiscard]] bool global_position_targets_list(QPoint global_position) const;
  bool scroll_by_wheel_delta(int primary_delta, bool pixel_delta);
  [[nodiscard]] int wheel_scroll_row_height() const;
  bool handle_wheel_event(QWheelEvent* event);
  void set_layer_row_buttons_drag_active(bool active);
  void keep_drag_anchor_selected();
  [[nodiscard]] bool drag_selection_locked() const noexcept;
  bool handle_item_double_click(QListWidgetItem* item, QPoint viewport_pos);
  [[nodiscard]] QScrollBar* scroll_bar_at_global_position(QPoint global_position) const;
  bool redirect_mouse_event_to_scroll_bar(QObject* watched, QMouseEvent* event);
  void install_scroll_bar_container_event_filters();
  void schedule_row_viewport_mask_update();
  void update_row_viewport_masks();

  bool drop_in_progress_{false};
  bool drag_blocked_{false};
  // After a refused startDrag the button is still down and the base view would
  // turn the leftover moves into a drag-selection sweep; swallow them instead.
  bool suppress_drag_select_until_release_{false};
  bool drop_event_uses_viewport_coordinates_{true};
  bool updating_row_widths_{false};
  bool row_mask_update_pending_{false};
  QPointer<QScrollBar> active_redirect_scroll_bar_;
  QPoint scroll_bar_drag_start_global_;
  int scroll_bar_drag_start_value_{0};
  int scroll_bar_drag_travel_{0};
  bool row_widget_drag_candidate_{false};
  bool pending_single_select_on_release_{false};
  bool visibility_sweep_active_{false};
  bool visibility_sweep_target_visible_{false};
  int visibility_sweep_min_x_{0};
  int visibility_sweep_max_x_{0};
  QPoint visibility_sweep_last_viewport_pos_{};
  std::vector<LayerId> visibility_swept_ids_;
  QBasicTimer auto_scroll_timer_;
  int auto_scroll_direction_{0};
  int wheel_pixel_remainder_{0};
  QPoint drag_start_position_{};
  QPoint last_drag_viewport_position_{};
  std::optional<LayerId> drag_anchor_layer_id_;
  std::vector<LayerId> dragged_layer_ids_;
  std::int64_t drag_source_session_id_{0};
  std::optional<DropTarget> drop_preview_;
  QWidget* insertion_indicator_{nullptr};
  QWidget* folder_highlight_indicator_{nullptr};
  std::optional<LayerDropRequest> pending_drop_request_;
  std::function<QStringList(const QMimeData*)> file_drop_paths_callback_;
  // The current external drag's usable files, computed once at enter (the
  // check may open files) and cleared at leave or drop.
  QStringList file_drag_paths_;
  std::optional<LayerFileDropRequest> pending_file_drop_request_;
  std::function<void()> drop_finished_callback_;
  std::function<void()> drag_blocked_callback_;
  std::function<void()> escape_callback_;
  std::function<bool(LayerId, LayerId)> clip_boundary_can_toggle_;
  std::function<void(LayerId)> clip_boundary_toggle_;
  std::function<void(LayerId)> visibility_isolate_callback_;
  std::function<void(LayerId, bool)> visibility_sweep_callback_;
  QPointer<QWidget> clip_cursor_widget_;
  std::function<void(QListWidgetItem*, LayerCtrlClickTarget)> ctrl_click_callback_;
  std::function<void(QListWidgetItem*, LayerCtrlClickTarget, Qt::KeyboardModifiers)> thumbnail_click_callback_;
  std::function<void(QListWidgetItem*)> item_double_click_callback_;
  std::function<void(QListWidgetItem*)> content_thumbnail_double_click_callback_;
  std::function<void(QListWidgetItem*, std::size_t)> smart_filter_double_click_callback_;
  std::function<void(LayerId, const QString&)> inline_rename_callback_;
  QPointer<QLineEdit> inline_rename_edit_;
  QPointer<QLabel> inline_rename_label_;
  LayerId inline_rename_layer_id_{0};
  bool inline_rename_finishing_{false};
};

}  // namespace patchy::ui
