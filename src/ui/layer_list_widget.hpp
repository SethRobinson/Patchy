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

#include <functional>
#include <optional>
#include <vector>

class QEvent;
class QDragEnterEvent;
class QDragLeaveEvent;
class QDragMoveEvent;
class QDropEvent;
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

[[nodiscard]] QByteArray layer_ids_to_mime_data(const std::vector<LayerId>& ids);
[[nodiscard]] std::vector<LayerId> layer_ids_from_mime_data(const QMimeData* mime_data);

enum class LayerCtrlClickTarget {
  ContentThumbnail,
  MaskThumbnail
};

class LayerListWidget final : public QListWidget, public QAbstractNativeEventFilter {
public:
  explicit LayerListWidget(QWidget* parent = nullptr);

  void set_drop_finished_callback(std::function<void()> callback);
  void set_ctrl_click_callback(std::function<void(QListWidgetItem*, LayerCtrlClickTarget)> callback);
  void set_thumbnail_click_callback(std::function<void(QListWidgetItem*, LayerCtrlClickTarget)> callback);
  void set_item_double_click_callback(std::function<void(QListWidgetItem*)> callback);
  [[nodiscard]] bool drop_in_progress() const noexcept;
  [[nodiscard]] std::optional<LayerDropRequest> take_drop_request();
  void refresh_row_widths();
  bool handle_drag_wheel_at_global_position(QPoint global_position, int primary_delta);

protected:
  bool event(QEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;
  bool nativeEventFilter(const QByteArray& event_type, void* message, qintptr* result) override;
  void setSelection(const QRect& rect, QItemSelectionModel::SelectionFlags command) override;
  bool viewportEvent(QEvent* event) override;
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

  [[nodiscard]] std::optional<LayerCtrlClickTarget> ctrl_click_target(QListWidgetItem* item,
                                                                      QPoint viewport_pos) const;
  void toggle_ctrl_selection(QListWidgetItem* item);
  void select_range_to_item(QListWidgetItem* target_item);
  void begin_single_drag_item(QListWidgetItem* item);
  void set_single_drag_item(QListWidgetItem* item);
  void set_current_item_preserving_scroll(QListWidgetItem* item, QItemSelectionModel::SelectionFlags command);
  void finish_pending_single_select();
  [[nodiscard]] std::vector<LayerId> selected_layer_ids_top_to_bottom() const;
  [[nodiscard]] QListWidgetItem* item_for_layer_id(LayerId id) const;
  [[nodiscard]] QListWidgetItem* parent_item_for(QListWidgetItem* item) const;
  [[nodiscard]] DropTarget drop_target_at(QPoint viewport_position) const;
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
  bool handle_wheel_event(QWheelEvent* event);
  void set_layer_row_buttons_drag_active(bool active);
  void keep_drag_anchor_selected();
  [[nodiscard]] bool drag_selection_locked() const noexcept;
  bool handle_item_double_click(QListWidgetItem* item);
  [[nodiscard]] QScrollBar* scroll_bar_at_global_position(QPoint global_position) const;
  bool redirect_mouse_event_to_scroll_bar(QObject* watched, QMouseEvent* event);
  void install_scroll_bar_container_event_filters();
  void schedule_row_viewport_mask_update();
  void update_row_viewport_masks();

  bool drop_in_progress_{false};
  bool drop_event_uses_viewport_coordinates_{true};
  bool updating_row_widths_{false};
  bool row_mask_update_pending_{false};
  QPointer<QScrollBar> active_redirect_scroll_bar_;
  QPoint scroll_bar_drag_start_global_;
  int scroll_bar_drag_start_value_{0};
  int scroll_bar_drag_travel_{0};
  bool row_widget_drag_candidate_{false};
  bool pending_single_select_on_release_{false};
  QBasicTimer auto_scroll_timer_;
  int auto_scroll_direction_{0};
  QPoint drag_start_position_{};
  QPoint last_drag_viewport_position_{};
  std::optional<LayerId> drag_anchor_layer_id_;
  std::vector<LayerId> dragged_layer_ids_;
  std::optional<DropTarget> drop_preview_;
  QWidget* insertion_indicator_{nullptr};
  QWidget* folder_highlight_indicator_{nullptr};
  std::optional<LayerDropRequest> pending_drop_request_;
  std::function<void()> drop_finished_callback_;
  std::function<void(QListWidgetItem*, LayerCtrlClickTarget)> ctrl_click_callback_;
  std::function<void(QListWidgetItem*, LayerCtrlClickTarget)> thumbnail_click_callback_;
  std::function<void(QListWidgetItem*)> item_double_click_callback_;
};

}  // namespace patchy::ui
