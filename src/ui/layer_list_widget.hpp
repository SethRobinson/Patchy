#pragma once

#include "core/layer_tree.hpp"

#include <QItemSelectionModel>
#include <QListWidget>
#include <QPoint>
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
class QObject;

namespace photoslop::ui {

inline constexpr int kLayerIdRole = Qt::UserRole;
inline constexpr int kLayerDepthRole = Qt::UserRole + 1;
inline constexpr int kLayerIsGroupRole = Qt::UserRole + 2;
inline constexpr int kLayerGroupExpandedRole = Qt::UserRole + 3;

enum class LayerCtrlClickTarget {
  ContentThumbnail,
  MaskThumbnail
};

class LayerListWidget final : public QListWidget {
public:
  using QListWidget::QListWidget;

  void set_drop_finished_callback(std::function<void()> callback);
  void set_ctrl_click_callback(std::function<void(QListWidgetItem*, LayerCtrlClickTarget)> callback);
  void set_thumbnail_click_callback(std::function<void(QListWidgetItem*, LayerCtrlClickTarget)> callback);
  [[nodiscard]] bool drop_in_progress() const noexcept;
  [[nodiscard]] std::optional<LayerDropRequest> take_drop_request();

protected:
  bool event(QEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;
  void setSelection(const QRect& rect, QItemSelectionModel::SelectionFlags command) override;
  bool viewportEvent(QEvent* event) override;
  void startDrag(Qt::DropActions supported_actions) override;
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dragMoveEvent(QDragMoveEvent* event) override;
  void dragLeaveEvent(QDragLeaveEvent* event) override;
  void dropEvent(QDropEvent* event) override;

private:
  [[nodiscard]] std::optional<LayerCtrlClickTarget> ctrl_click_target(QListWidgetItem* item,
                                                                      QPoint viewport_pos) const;
  void toggle_ctrl_selection(QListWidgetItem* item);
  void select_range_to_item(QListWidgetItem* target_item);
  void set_single_drag_item(QListWidgetItem* item);
  [[nodiscard]] std::vector<LayerId> selected_layer_ids_top_to_bottom() const;
  [[nodiscard]] QListWidgetItem* item_for_layer_id(LayerId id) const;
  [[nodiscard]] LayerDropPosition inferred_drop_position(QListWidgetItem* target_item,
                                                         QPoint viewport_position) const;
  void set_layer_row_buttons_drag_active(bool active);
  void keep_drag_anchor_selected();
  [[nodiscard]] bool drag_selection_locked() const noexcept;

  bool drop_in_progress_{false};
  bool drop_event_uses_viewport_coordinates_{true};
  bool row_widget_drag_candidate_{false};
  QPoint drag_start_position_{};
  std::optional<LayerId> drag_anchor_layer_id_;
  std::vector<LayerId> dragged_layer_ids_;
  std::optional<LayerDropRequest> pending_drop_request_;
  std::function<void()> drop_finished_callback_;
  std::function<void(QListWidgetItem*, LayerCtrlClickTarget)> ctrl_click_callback_;
  std::function<void(QListWidgetItem*, LayerCtrlClickTarget)> thumbnail_click_callback_;
};

}  // namespace photoslop::ui
