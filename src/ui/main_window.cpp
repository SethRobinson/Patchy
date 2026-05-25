#include "ui/main_window.hpp"

#include "core/pixel_tools.hpp"
#include "filters/builtin_filters.hpp"
#include "plugins/legacy_photoshop_adapter.hpp"
#include "psd/psd_document_io.hpp"
#include "render/compositor.hpp"
#include "ui/image_document_io.hpp"

#include <QAbstractItemView>
#include <QAbstractItemModel>
#include <QAbstractSpinBox>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QBrush>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontComboBox>
#include <QFontMetrics>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QImageReader>
#include <QImageWriter>
#include <QInputDialog>
#include <QItemSelection>
#include <QLabel>
#include <QKeySequence>
#include <QListWidget>
#include <QLinearGradient>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPushButton>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygon>
#include <QPointer>
#include <QScrollBar>
#include <QShortcut>
#include <QSettings>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextDocument>
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
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVariant>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

#ifndef PHOTOSLOP_VERSION
#define PHOTOSLOP_VERSION "0.0.0"
#endif

namespace photoslop::ui {

namespace {

constexpr int kLayerIdRole = Qt::UserRole;
constexpr int kLayerDepthRole = Qt::UserRole + 1;
constexpr int kLayerIsGroupRole = Qt::UserRole + 2;
constexpr int kLayerGroupExpandedRole = Qt::UserRole + 3;

enum class LayerDropPosition {
  OnItem,
  AboveItem,
  BelowItem,
  OnViewport
};

struct LayerDropRequest {
  std::vector<LayerId> layer_ids_top_to_bottom;
  std::optional<LayerId> target_layer_id;
  LayerDropPosition position{LayerDropPosition::OnViewport};
};

class LayerListWidget final : public QListWidget {
public:
  using QListWidget::QListWidget;

  void set_drop_finished_callback(std::function<void()> callback) {
    drop_finished_callback_ = std::move(callback);
  }

  void set_ctrl_click_callback(std::function<void(QListWidgetItem*)> callback) {
    ctrl_click_callback_ = std::move(callback);
  }

  [[nodiscard]] bool drop_in_progress() const noexcept {
    return drop_in_progress_;
  }

  [[nodiscard]] std::optional<LayerDropRequest> take_drop_request() {
    auto request = std::move(pending_drop_request_);
    pending_drop_request_.reset();
    return request;
  }

protected:
  bool event(QEvent* event) override {
    if (event->type() == QEvent::Drop) {
      drop_event_uses_viewport_coordinates_ = false;
      dropEvent(static_cast<QDropEvent*>(event));
      drop_event_uses_viewport_coordinates_ = true;
      return event->isAccepted();
    }
    return QListWidget::event(event);
  }

  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() == QEvent::MouseButtonPress) {
      auto* mouse_event = static_cast<QMouseEvent*>(event);
      auto* widget = qobject_cast<QWidget*>(watched);
      if (widget != nullptr && mouse_event->button() == Qt::LeftButton &&
          (mouse_event->modifiers() & Qt::ControlModifier) != 0) {
        const auto viewport_pos = viewport()->mapFromGlobal(widget->mapToGlobal(mouse_event->pos()));
        auto* item = itemAt(viewport_pos);
        if (item != nullptr && widget->objectName() == QStringLiteral("layerVisibilityCheck") && ctrl_click_callback_) {
          ctrl_click_callback_(item);
          event->accept();
          return true;
        }
        if (item != nullptr) {
          toggle_ctrl_selection(item);
          event->accept();
          return true;
        }
      } else if (widget != nullptr && mouse_event->button() == Qt::LeftButton &&
                 (mouse_event->modifiers() & Qt::ShiftModifier) != 0 &&
                 widget->objectName() != QStringLiteral("layerVisibilityCheck")) {
        const auto viewport_pos = viewport()->mapFromGlobal(widget->mapToGlobal(mouse_event->pos()));
        if (auto* item = itemAt(viewport_pos); item != nullptr) {
          select_range_to_item(item);
          event->accept();
          return true;
        }
      } else if (widget != nullptr && mouse_event->button() == Qt::LeftButton &&
                 (mouse_event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) == 0 &&
                 widget->objectName() != QStringLiteral("layerVisibilityCheck")) {
        const auto viewport_pos = viewport()->mapFromGlobal(widget->mapToGlobal(mouse_event->pos()));
        if (auto* item = itemAt(viewport_pos); item != nullptr) {
          set_single_drag_item(item);
          drag_start_position_ = viewport_pos;
          row_widget_drag_candidate_ = true;
          event->accept();
          return true;
        }
      }
    } else if (event->type() == QEvent::MouseMove) {
      auto* mouse_event = static_cast<QMouseEvent*>(event);
      auto* widget = qobject_cast<QWidget*>(watched);
      if (row_widget_drag_candidate_ && widget != nullptr && (mouse_event->buttons() & Qt::LeftButton) != 0) {
        const auto viewport_pos = viewport()->mapFromGlobal(widget->mapToGlobal(mouse_event->pos()));
        if ((viewport_pos - drag_start_position_).manhattanLength() >= QApplication::startDragDistance()) {
          row_widget_drag_candidate_ = false;
          startDrag(Qt::MoveAction);
          event->accept();
          return true;
        }
      }
    } else if (event->type() == QEvent::MouseButtonRelease) {
      row_widget_drag_candidate_ = false;
      drag_anchor_layer_id_.reset();
    }
    return QListWidget::eventFilter(watched, event);
  }

  void setSelection(const QRect& rect, QItemSelectionModel::SelectionFlags command) override {
    if (drag_selection_locked()) {
      keep_drag_anchor_selected();
      return;
    }
    QListWidget::setSelection(rect, command);
  }

  bool viewportEvent(QEvent* event) override {
    if (event->type() == QEvent::Drop) {
      drop_event_uses_viewport_coordinates_ = true;
      dropEvent(static_cast<QDropEvent*>(event));
      return event->isAccepted();
    }
    if (event->type() == QEvent::MouseButtonPress) {
      auto* mouse_event = static_cast<QMouseEvent*>(event);
      if (mouse_event->button() == Qt::LeftButton && (mouse_event->modifiers() & Qt::ControlModifier) != 0) {
        auto* item = itemAt(mouse_event->pos());
        if (item != nullptr && ctrl_click_callback_ && visibility_hit(item, mouse_event->pos())) {
          ctrl_click_callback_(item);
          event->accept();
          return true;
        }
      } else if (mouse_event->button() == Qt::LeftButton && (mouse_event->modifiers() & Qt::ShiftModifier) != 0) {
        if (auto* item = itemAt(mouse_event->pos()); item != nullptr) {
          select_range_to_item(item);
          event->accept();
          return true;
        }
      } else if (mouse_event->button() == Qt::LeftButton &&
                 (mouse_event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) == 0) {
        if (auto* item = itemAt(mouse_event->pos()); item != nullptr) {
          set_single_drag_item(item);
          drag_start_position_ = mouse_event->pos();
          row_widget_drag_candidate_ = true;
          event->accept();
          return true;
        }
      }
    } else if (event->type() == QEvent::MouseMove) {
      auto* mouse_event = static_cast<QMouseEvent*>(event);
      if (row_widget_drag_candidate_ && (mouse_event->buttons() & Qt::LeftButton) != 0) {
        if ((mouse_event->pos() - drag_start_position_).manhattanLength() >= QApplication::startDragDistance()) {
          row_widget_drag_candidate_ = false;
          startDrag(Qt::MoveAction);
          event->accept();
          return true;
        }
      }
    } else if (event->type() == QEvent::MouseButtonRelease) {
      row_widget_drag_candidate_ = false;
      drag_anchor_layer_id_.reset();
    } else if (event->type() == QEvent::Leave) {
      if ((QApplication::mouseButtons() & Qt::LeftButton) == 0) {
        row_widget_drag_candidate_ = false;
        drag_anchor_layer_id_.reset();
      }
    }
    return QListWidget::viewportEvent(event);
  }

  bool visibility_hit(QListWidgetItem* item, QPoint viewport_pos) const {
    auto* row = itemWidget(item);
    if (row == nullptr) {
      return false;
    }
    auto* visibility = row->findChild<QToolButton*>(QStringLiteral("layerVisibilityCheck"));
    if (visibility == nullptr) {
      return false;
    }
    return visibility->geometry().contains(row->mapFrom(viewport(), viewport_pos));
  }

  void toggle_ctrl_selection(QListWidgetItem* item) {
    if (item == nullptr) {
      return;
    }
    row_widget_drag_candidate_ = false;
    drag_anchor_layer_id_.reset();
    const auto selected = !item->isSelected();
    item->setSelected(selected);
    if (selected) {
      setCurrentItem(item);
    }
  }

  void select_range_to_item(QListWidgetItem* target_item) {
    if (target_item == nullptr || selectionModel() == nullptr || model() == nullptr) {
      return;
    }
    row_widget_drag_candidate_ = false;
    drag_anchor_layer_id_.reset();

    const auto target_row = row(target_item);
    const auto anchor_row = currentRow() >= 0 ? currentRow() : target_row;
    const auto first_row = std::min(anchor_row, target_row);
    const auto last_row = std::max(anchor_row, target_row);
    const auto target_index = model()->index(target_row, 0);
    const QItemSelection range(model()->index(first_row, 0), model()->index(last_row, 0));
    selectionModel()->setCurrentIndex(target_index, QItemSelectionModel::NoUpdate);
    selectionModel()->select(range, QItemSelectionModel::ClearAndSelect);
    viewport()->update();
  }

  void set_single_drag_item(QListWidgetItem* item) {
    if (item == nullptr) {
      return;
    }
    drag_anchor_layer_id_ = static_cast<LayerId>(item->data(kLayerIdRole).toULongLong());
    const auto list_updates_enabled = updatesEnabled();
    const auto viewport_updates_enabled = viewport()->updatesEnabled();
    setUpdatesEnabled(false);
    viewport()->setUpdatesEnabled(false);
    setCurrentItem(item, QItemSelectionModel::ClearAndSelect);
    viewport()->setUpdatesEnabled(viewport_updates_enabled);
    setUpdatesEnabled(list_updates_enabled);
    viewport()->update();
  }

  void startDrag(Qt::DropActions supported_actions) override {
    if (drag_anchor_layer_id_.has_value()) {
      if (auto* anchor = item_for_layer_id(*drag_anchor_layer_id_); anchor != nullptr) {
        set_single_drag_item(anchor);
      }
    }
    dragged_layer_ids_ = selected_layer_ids_top_to_bottom();
    if (dragged_layer_ids_.empty()) {
      drag_anchor_layer_id_.reset();
      return;
    }

    set_layer_row_buttons_drag_active(true);
    QDrag drag(this);
    drag.setMimeData(mimeData(selectedItems()));
    drag.exec(supported_actions, Qt::MoveAction);
    set_layer_row_buttons_drag_active(false);
    dragged_layer_ids_.clear();
    drag_anchor_layer_id_.reset();
  }

  void dragEnterEvent(QDragEnterEvent* event) override {
    keep_drag_anchor_selected();
    event->setDropAction(Qt::MoveAction);
    event->accept();
  }

  void dragMoveEvent(QDragMoveEvent* event) override {
    keep_drag_anchor_selected();
    event->setDropAction(Qt::MoveAction);
    event->accept();
  }

  void dragLeaveEvent(QDragLeaveEvent* event) override {
    keep_drag_anchor_selected();
    event->accept();
  }

  void dropEvent(QDropEvent* event) override {
    auto ids = dragged_layer_ids_.empty() ? selected_layer_ids_top_to_bottom() : dragged_layer_ids_;
    if (!ids.empty()) {
      const auto event_position = event->position().toPoint();
      auto position =
          drop_event_uses_viewport_coordinates_ ? event_position : viewport()->mapFrom(this, event_position);
      auto* target_item = itemAt(position);
      if (target_item == nullptr) {
        const auto viewport_position = viewport()->mapFrom(this, event_position);
        if (viewport()->rect().contains(viewport_position)) {
          position = viewport_position;
          target_item = itemAt(position);
        }
      }
      pending_drop_request_ = LayerDropRequest{
          std::move(ids),
          target_item != nullptr
              ? std::optional<LayerId>{static_cast<LayerId>(target_item->data(kLayerIdRole).toULongLong())}
              : std::nullopt,
          inferred_drop_position(target_item, position)};
      drop_in_progress_ = true;
      event->setDropAction(Qt::MoveAction);
      event->accept();
      drop_in_progress_ = false;
    } else {
      drop_in_progress_ = true;
      QListWidget::dropEvent(event);
      drop_in_progress_ = false;
    }
    if (drop_finished_callback_) {
      QTimer::singleShot(0, this, [this] {
        if (drop_finished_callback_) {
          drop_finished_callback_();
        }
      });
    }
  }

private:
  [[nodiscard]] std::vector<LayerId> selected_layer_ids_top_to_bottom() const {
    std::vector<LayerId> ids;
    ids.reserve(static_cast<std::size_t>(selectedItems().size()));
    for (int row = 0; row < count(); ++row) {
      const auto* layer_item = item(row);
      if (layer_item != nullptr && layer_item->isSelected()) {
        ids.push_back(static_cast<LayerId>(layer_item->data(kLayerIdRole).toULongLong()));
      }
    }
    return ids;
  }

  [[nodiscard]] QListWidgetItem* item_for_layer_id(LayerId id) const {
    for (int row = 0; row < count(); ++row) {
      auto* layer_item = item(row);
      if (layer_item != nullptr && static_cast<LayerId>(layer_item->data(kLayerIdRole).toULongLong()) == id) {
        return layer_item;
      }
    }
    return nullptr;
  }

  [[nodiscard]] LayerDropPosition inferred_drop_position(QListWidgetItem* target_item,
                                                         QPoint viewport_position) const {
    if (target_item == nullptr) {
      return LayerDropPosition::OnViewport;
    }

    const auto rect = visualItemRect(target_item);
    const auto edge_band = target_item->data(kLayerIsGroupRole).toBool() ? 5 : std::max(4, rect.height() / 4);
    if (viewport_position.y() < rect.top() + edge_band) {
      return LayerDropPosition::AboveItem;
    }
    if (viewport_position.y() >= rect.bottom() - edge_band) {
      return LayerDropPosition::BelowItem;
    }
    return LayerDropPosition::OnItem;
  }

  void set_layer_row_buttons_drag_active(bool active) {
    for (auto* button : findChildren<QToolButton*>()) {
      if (button->objectName() != QStringLiteral("layerFolderDisclosureButton") &&
          button->objectName() != QStringLiteral("layerVisibilityCheck")) {
        continue;
      }
      button->setProperty("layerDragActive", active);
      button->style()->unpolish(button);
      button->style()->polish(button);
    }
    viewport()->update();
  }

  void keep_drag_anchor_selected() {
    if (!drag_anchor_layer_id_.has_value()) {
      return;
    }
    auto* anchor = item_for_layer_id(*drag_anchor_layer_id_);
    if (anchor == nullptr) {
      return;
    }
    const auto selected = selectedItems();
    if (selected.size() == 1 && selected.front() == anchor && currentItem() == anchor) {
      return;
    }
    set_single_drag_item(anchor);
  }

  [[nodiscard]] bool drag_selection_locked() const noexcept {
    return drag_anchor_layer_id_.has_value() && (row_widget_drag_candidate_ || !dragged_layer_ids_.empty());
  }

  bool drop_in_progress_{false};
  bool drop_event_uses_viewport_coordinates_{true};
  bool row_widget_drag_candidate_{false};
  QPoint drag_start_position_{};
  std::optional<LayerId> drag_anchor_layer_id_;
  std::vector<LayerId> dragged_layer_ids_;
  std::optional<LayerDropRequest> pending_drop_request_;
  std::function<void()> drop_finished_callback_;
  std::function<void(QListWidgetItem*)> ctrl_click_callback_;
};

class ColorGradientField final : public QWidget {
public:
  explicit ColorGradientField(QWidget* parent = nullptr) : QWidget(parent) {
    setObjectName(QStringLiteral("colorGradientField"));
    setMinimumSize(280, 150);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setCursor(Qt::CrossCursor);
  }

  void set_color(QColor color) {
    color = color.toHsv();
    if (color.hue() >= 0) {
      hue_ = color.hue();
    }
    value_ = std::clamp(color.value(), 0, 255);
    update();
  }

  std::function<void(QColor)> color_chosen;

protected:
  void paintEvent(QPaintEvent* /*event*/) override {
    QPainter painter(this);
    const auto field = rect().adjusted(1, 1, -2, -2);
    if (field.isEmpty()) {
      return;
    }

    for (int x = field.left(); x <= field.right(); ++x) {
      const auto hue = static_cast<int>(std::round(
          359.0 * static_cast<double>(x - field.left()) / static_cast<double>(std::max(1, field.width() - 1))));
      QLinearGradient gradient(QPointF(x, field.top()), QPointF(x, field.bottom()));
      gradient.setColorAt(0.0, QColor::fromHsv(hue, 255, 255));
      gradient.setColorAt(1.0, QColor::fromHsv(hue, 255, 0));
      painter.fillRect(QRect(x, field.top(), 1, field.height()), gradient);
    }

    painter.setPen(QColor(150, 158, 170));
    painter.drawRect(field);

    const auto marker_x =
        field.left() + static_cast<int>(std::round(static_cast<double>(hue_) * field.width() / 359.0));
    const auto marker_y =
        field.top() + static_cast<int>(std::round(static_cast<double>(255 - value_) * field.height() / 255.0));
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(12, 14, 18), 3));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QPoint(marker_x, marker_y), 6, 6);
    painter.setPen(QPen(QColor(250, 252, 255), 1));
    painter.drawEllipse(QPoint(marker_x, marker_y), 6, 6);
  }

  void mousePressEvent(QMouseEvent* event) override {
    choose_at(event->pos());
  }

  void mouseMoveEvent(QMouseEvent* event) override {
    if ((event->buttons() & Qt::LeftButton) != 0) {
      choose_at(event->pos());
    }
  }

private:
  void choose_at(QPoint point) {
    const auto field = rect().adjusted(1, 1, -2, -2);
    if (field.isEmpty()) {
      return;
    }
    const auto x = std::clamp(point.x(), field.left(), field.right());
    const auto y = std::clamp(point.y(), field.top(), field.bottom());
    hue_ = static_cast<int>(std::round(
        359.0 * static_cast<double>(x - field.left()) / static_cast<double>(std::max(1, field.width() - 1))));
    value_ = 255 - static_cast<int>(std::round(
                       255.0 * static_cast<double>(y - field.top()) /
                       static_cast<double>(std::max(1, field.height() - 1))));
    const auto color = QColor::fromHsv(hue_, 255, std::clamp(value_, 0, 255));
    if (color_chosen) {
      color_chosen(color);
    }
    update();
  }

  int hue_{0};
  int value_{255};
};

PixelBuffer make_solid_pixels(std::int32_t width, std::int32_t height, QColor color, PixelFormat format) {
  PixelBuffer pixels(width, height, format);
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(color.red());
      px[1] = static_cast<std::uint8_t>(color.green());
      px[2] = static_cast<std::uint8_t>(color.blue());
      if (format.channels >= 4) {
        px[3] = static_cast<std::uint8_t>(color.alpha());
      }
    }
  }
  return pixels;
}

QString blend_mode_name(BlendMode mode) {
  switch (mode) {
    case BlendMode::PassThrough:
      return QObject::tr("Pass Through");
    case BlendMode::Normal:
      return QObject::tr("Normal");
    case BlendMode::Multiply:
      return QObject::tr("Multiply");
    case BlendMode::Screen:
      return QObject::tr("Screen");
    case BlendMode::Overlay:
      return QObject::tr("Overlay");
    case BlendMode::Darken:
      return QObject::tr("Darken");
    case BlendMode::Lighten:
      return QObject::tr("Lighten");
    case BlendMode::ColorDodge:
      return QObject::tr("Color Dodge");
    case BlendMode::ColorBurn:
      return QObject::tr("Color Burn");
    case BlendMode::HardLight:
      return QObject::tr("Hard Light");
    case BlendMode::SoftLight:
      return QObject::tr("Soft Light");
    case BlendMode::Difference:
      return QObject::tr("Difference");
    case BlendMode::LinearBurn:
      return QObject::tr("Linear Burn");
    case BlendMode::PinLight:
      return QObject::tr("Pin Light");
    case BlendMode::Saturation:
      return QObject::tr("Saturation");
    case BlendMode::Luminosity:
      return QObject::tr("Luminosity");
  }
  return QObject::tr("Normal");
}

QString tool_name(CanvasTool tool) {
  switch (tool) {
    case CanvasTool::Move:
      return QObject::tr("Move");
    case CanvasTool::Marquee:
      return QObject::tr("Marquee");
    case CanvasTool::Lasso:
      return QObject::tr("Lasso");
    case CanvasTool::MagicWand:
      return QObject::tr("Magic Wand");
    case CanvasTool::Brush:
      return QObject::tr("Brush");
    case CanvasTool::Eraser:
      return QObject::tr("Eraser");
    case CanvasTool::Gradient:
      return QObject::tr("Gradient");
    case CanvasTool::Line:
      return QObject::tr("Line");
    case CanvasTool::Rectangle:
      return QObject::tr("Rectangle");
    case CanvasTool::Ellipse:
      return QObject::tr("Ellipse");
    case CanvasTool::Fill:
      return QObject::tr("Fill");
    case CanvasTool::Eyedropper:
      return QObject::tr("Eyedropper");
    case CanvasTool::Text:
      return QObject::tr("Type");
    case CanvasTool::Pan:
      return QObject::tr("Pan");
    case CanvasTool::Zoom:
      return QObject::tr("Zoom");
  }
  return QObject::tr("Tool");
}

QString color_button_style(QColor color) {
  const auto text = color.lightness() < 128 ? QStringLiteral("white") : QStringLiteral("black");
  return QStringLiteral(R"(
    QPushButton {
      background: rgb(%1, %2, %3);
      color: %4;
      border: 1px solid #f0f0f0;
      border-radius: 0;
      min-width: 26px;
      max-width: 26px;
      min-height: 24px;
      max-height: 24px;
      font-weight: 700;
      padding: 0;
    }
    QPushButton:hover {
      border-color: #4aa3ff;
    }
  )")
      .arg(color.red())
      .arg(color.green())
      .arg(color.blue())
      .arg(text);
}

QString swatch_button_style(QColor color, bool large = false) {
  return QStringLiteral(
             "QPushButton { background: rgb(%1, %2, %3); border: 1px solid #747b86; border-radius: 2px; min-width: %4px; "
             "min-height: %4px; max-width: %4px; max-height: %4px; }"
             "QPushButton:hover { border: 2px solid #63a6ff; }")
      .arg(color.red())
      .arg(color.green())
      .arg(color.blue())
      .arg(large ? 30 : 24);
}

QString inline_text_editor_style(QColor color, int pixel_size) {
  return QStringLiteral(
             "QTextEdit { background: transparent; color: rgb(%1, %2, %3); "
             "border: 1px dashed #63a8ff; padding: 0; font-size: %4px; } "
             "QTextEdit QWidget { background: transparent; } "
             "QTextEdit::selection { background: rgba(49, 116, 190, 130); }")
      .arg(color.red())
      .arg(color.green())
      .arg(color.blue())
      .arg(pixel_size);
}

QIcon simple_icon(QString text, QColor accent = QColor(220, 226, 235)) {
  QPixmap pixmap(32, 32);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(QPen(accent, 2.2));
  painter.setBrush(Qt::NoBrush);

  if (text == QStringLiteral("new")) {
    painter.drawRect(QRect(9, 6, 14, 20));
    painter.drawLine(16, 11, 16, 21);
    painter.drawLine(11, 16, 21, 16);
  } else if (text == QStringLiteral("dir")) {
    QPainterPath folder_path(QPointF(6.0, 11.0));
    folder_path.lineTo(13.0, 11.0);
    folder_path.lineTo(15.5, 8.0);
    folder_path.lineTo(25.0, 8.0);
    folder_path.lineTo(25.0, 24.0);
    folder_path.lineTo(6.0, 24.0);
    folder_path.closeSubpath();
    painter.drawPath(folder_path);
    painter.drawLine(6, 13, 25, 13);
  } else if (text == QStringLiteral("dup")) {
    painter.drawRect(QRect(7, 10, 13, 15));
    painter.drawRect(QRect(12, 6, 13, 15));
  } else if (text == QStringLiteral("RN")) {
    painter.drawLine(QPointF(9.0, 23.0), QPointF(22.0, 10.0));
    painter.drawLine(QPointF(18.0, 8.0), QPointF(24.0, 14.0));
    painter.drawLine(QPointF(8.0, 24.0), QPointF(13.0, 22.5));
    painter.drawLine(QPointF(7.0, 25.0), QPointF(9.0, 20.0));
  } else if (text == QStringLiteral("trash")) {
    painter.drawLine(9, 10, 23, 10);
    painter.drawRect(QRect(11, 11, 10, 15));
    painter.drawLine(13, 15, 13, 23);
    painter.drawLine(19, 15, 19, 23);
  } else if (text == QStringLiteral("fill")) {
    painter.drawPolygon(QPolygon({QPoint(9, 10), QPoint(21, 15), QPoint(15, 25), QPoint(5, 18)}));
    painter.setBrush(accent);
    painter.drawEllipse(QPoint(25, 24), 3, 3);
  } else if (text == QStringLiteral("clear")) {
    painter.drawRect(QRect(8, 8, 16, 16));
    painter.drawLine(8, 24, 24, 8);
  } else if (text == QStringLiteral("swap")) {
    painter.drawLine(8, 11, 23, 11);
    painter.drawLine(23, 11, 19, 7);
    painter.drawLine(23, 11, 19, 15);
    painter.drawLine(24, 21, 9, 21);
    painter.drawLine(9, 21, 13, 17);
    painter.drawLine(9, 21, 13, 25);
  } else if (text == QStringLiteral("default")) {
    painter.setBrush(Qt::black);
    painter.drawRect(QRect(7, 7, 13, 13));
    painter.setBrush(Qt::white);
    painter.drawRect(QRect(13, 13, 13, 13));
  } else if (text == QStringLiteral("zoomIn") || text == QStringLiteral("zoomOut")) {
    painter.drawEllipse(QRect(7, 7, 14, 14));
    painter.drawLine(18, 18, 26, 26);
    painter.drawLine(11, 14, 17, 14);
    if (text == QStringLiteral("zoomIn")) {
      painter.drawLine(14, 11, 14, 17);
    }
  } else if (text == QStringLiteral("fit")) {
    painter.drawRect(QRect(7, 9, 18, 14));
    painter.drawLine(7, 9, 12, 9);
    painter.drawLine(7, 9, 7, 14);
    painter.drawLine(25, 23, 20, 23);
    painter.drawLine(25, 23, 25, 18);
  } else if (text == QStringLiteral("crop")) {
    painter.drawLine(10, 5, 10, 23);
    painter.drawLine(5, 20, 23, 20);
    painter.drawLine(15, 9, 27, 9);
    painter.drawLine(22, 9, 22, 27);
  } else if (text == QStringLiteral("rotate")) {
    painter.drawArc(QRect(7, 7, 18, 18), 30 * 16, 280 * 16);
    painter.drawLine(22, 6, 26, 7);
    painter.drawLine(22, 6, 23, 11);
  } else if (text == QStringLiteral("merge")) {
    painter.drawRect(QRect(8, 8, 15, 10));
    painter.drawRect(QRect(11, 14, 15, 10));
    painter.drawLine(10, 26, 24, 26);
  } else if (text == QStringLiteral("stroke")) {
    QPen dashed(accent, 2.0);
    dashed.setStyle(Qt::DashLine);
    painter.setPen(dashed);
    painter.drawRect(QRect(8, 8, 16, 16));
  } else {
    painter.setPen(accent);
    auto font = painter.font();
    font.setPixelSize(14);
    font.setBold(true);
    painter.setFont(font);
    painter.drawText(pixmap.rect(), Qt::AlignCenter, text.left(2).toUpper());
  }

  return QIcon(pixmap);
}

QString clean_action_text(const QAction* action) {
  if (action == nullptr) {
    return {};
  }
  auto label = action->text();
  label.remove(QLatin1Char('&'));
  return label.trimmed();
}

QString action_shortcut_text(const QAction* action) {
  if (action == nullptr) {
    return {};
  }
  QStringList shortcut_labels;
  for (const auto& shortcut : action->shortcuts()) {
    if (!shortcut.isEmpty()) {
      shortcut_labels << shortcut.toString(QKeySequence::NativeText);
    }
  }
  return shortcut_labels.join(QStringLiteral(", "));
}

void refresh_action_tooltip(QAction* action) {
  if (action == nullptr || action->isSeparator()) {
    return;
  }
  const auto label = clean_action_text(action);
  const auto shortcut = action_shortcut_text(action);
  action->setToolTip(shortcut.isEmpty() ? label : QObject::tr("%1 (%2)").arg(label, shortcut));
}

void apply_action_shortcut(QAction* action, QKeySequence shortcut) {
  action->setShortcut(shortcut);
  action->setShortcutContext(Qt::ApplicationShortcut);
  refresh_action_tooltip(action);
}

bool layer_locks_transparent_pixels(const Layer& layer);

void configure_toolbar_spinbox(QSpinBox* spin, int width) {
  spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
  spin->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  spin->setFixedWidth(width);
}

void configure_dialog_spinbox(QSpinBox* spin, int width = 92) {
  spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
  spin->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  spin->setMinimumWidth(width);
  spin->setMinimumHeight(24);
}

QString layer_visibility_indicator_text(bool visible) {
  return visible ? QStringLiteral("✓") : QString();
}

QString layer_visibility_tooltip(bool visible) {
  return visible ? QObject::tr("Layer visible. Click to hide.") : QObject::tr("Layer hidden. Click to show.");
}

class MouseDoubleClickFilter final : public QObject {
public:
  MouseDoubleClickFilter(std::function<void()> callback, QObject* parent)
      : QObject(parent), callback_(std::move(callback)) {}

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() == QEvent::MouseButtonDblClick) {
      auto* mouse_event = static_cast<QMouseEvent*>(event);
      if (mouse_event->button() == Qt::LeftButton) {
        if (callback_) {
          callback_();
        }
        mouse_event->accept();
        return true;
      }
    }
    return QObject::eventFilter(watched, event);
  }

private:
  std::function<void()> callback_;
};

class CheckGlyphBox final : public QCheckBox {
public:
  explicit CheckGlyphBox(const QString& text, QWidget* parent = nullptr) : QCheckBox(text, parent) {
    setMinimumHeight(24);
  }

  QSize sizeHint() const override {
    const auto text_width = fontMetrics().horizontalAdvance(text());
    const auto minimum = objectName() == QStringLiteral("shapeFillCheck") ? 58 : 92;
    return QSize(std::max(minimum, text_width + 34), 24);
  }

protected:
  void paintEvent(QPaintEvent* event) override {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const bool framed = objectName() == QStringLiteral("moveAutoSelectCheck") ||
                        objectName() == QStringLiteral("selectionAntiAliasCheck") ||
                        objectName() == QStringLiteral("shapeFillCheck");
    if (framed) {
      painter.fillRect(rect(), QColor(41, 41, 41));
      painter.setPen(QPen(QColor(23, 23, 23), 1));
      painter.drawRect(rect().adjusted(0, 0, -1, -1));
      painter.setPen(QPen(QColor(93, 93, 93), 1));
      painter.drawLine(rect().topLeft(), rect().topRight());
    }

    const QRect box(7, (height() - 14) / 2, 14, 14);
    painter.setBrush(isChecked() ? QColor(20, 115, 230) : QColor(31, 31, 31));
    painter.setPen(QPen(isChecked() ? QColor(156, 207, 255) : QColor(120, 120, 120), 1));
    painter.drawRect(box);
    if (isChecked()) {
      painter.setPen(QPen(QColor(255, 255, 255), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      painter.drawLine(QPointF(box.left() + 3.0, box.center().y() + 0.5), QPointF(box.left() + 6.0, box.bottom() - 3.0));
      painter.drawLine(QPointF(box.left() + 6.0, box.bottom() - 3.0), QPointF(box.right() - 2.0, box.top() + 3.0));
    }

    painter.setPen(isEnabled() ? QColor(240, 240, 240) : QColor(145, 145, 145));
    painter.drawText(QRect(box.right() + 7, 0, width() - box.right() - 10, height()), Qt::AlignVCenter | Qt::AlignLeft,
                     text());
  }
};

void install_collapsible_dock_title(QDockWidget* dock,
                                    QWidget* content,
                                    const QString& object_prefix,
                                    int expanded_minimum_height = 0) {
  constexpr int kRightDockMinimumWidth = 280;
  dock->setMinimumWidth(kRightDockMinimumWidth);
  content->setMinimumWidth(kRightDockMinimumWidth - 18);
  if (expanded_minimum_height > 0) {
    dock->setMinimumHeight(expanded_minimum_height);
  }

  auto* title = new QWidget(dock);
  title->setObjectName(object_prefix + QStringLiteral("DockTitle"));
  title->setMinimumWidth(kRightDockMinimumWidth - 18);
  auto* layout = new QHBoxLayout(title);
  layout->setContentsMargins(7, 3, 7, 3);
  layout->setSpacing(6);

  auto* toggle = new QToolButton(title);
  toggle->setObjectName(object_prefix + QStringLiteral("DockCollapseButton"));
  toggle->setProperty("dockCollapseButton", true);
  toggle->setAutoRaise(false);
  toggle->setCheckable(true);
  toggle->setChecked(true);
  toggle->setText(QStringLiteral("v"));
  toggle->setFixedSize(18, 18);
  toggle->setToolTip(QObject::tr("Collapse panel"));
  layout->addWidget(toggle);

  auto* label = new QLabel(dock->windowTitle(), title);
  label->setObjectName(object_prefix + QStringLiteral("DockTitleLabel"));
  layout->addWidget(label, 1);

  QObject::connect(toggle, &QToolButton::toggled, dock,
                   [dock, content, toggle, expanded_minimum_height](bool expanded) {
    content->setVisible(expanded);
    toggle->setText(expanded ? QStringLiteral("v") : QStringLiteral(">"));
    toggle->setToolTip(expanded ? QObject::tr("Collapse panel") : QObject::tr("Expand panel"));
    const auto collapsed_height = dock->titleBarWidget()->sizeHint().height() + 8;
    if (expanded_minimum_height > 0) {
      dock->setMinimumHeight(expanded ? expanded_minimum_height : collapsed_height);
    }
    dock->setMaximumHeight(expanded ? QWIDGETSIZE_MAX : collapsed_height);
    dock->updateGeometry();
  });

  dock->setTitleBarWidget(title);
}

void restyle_layer_rows(QListWidget* list) {
  if (list == nullptr) {
    return;
  }
  for (int row = 0; row < list->count(); ++row) {
    auto* item = list->item(row);
    auto* row_widget = list->itemWidget(item);
    if (row_widget == nullptr) {
      continue;
    }
    row_widget->setStyleSheet(item->isSelected()
                                  ? QStringLiteral("QWidget#layerRowWidget { background: #3a414a; }")
                                  : QStringLiteral("QWidget#layerRowWidget { background: #242628; }"));
    if (auto* name = row_widget->findChild<QLabel*>(QStringLiteral("layerRowName")); name != nullptr) {
      auto font = name->font();
      font.setBold(item == list->currentItem());
      name->setFont(font);
    }
  }
}

std::size_t layer_descendant_count(const Layer& layer) {
  std::size_t count = layer.children().size();
  for (const auto& child : layer.children()) {
    count += layer_descendant_count(child);
  }
  return count;
}

std::size_t layer_tree_count(const std::vector<Layer>& layers) {
  std::size_t count = layers.size();
  for (const auto& layer : layers) {
    count += layer_tree_count(layer.children());
  }
  return count;
}

void collect_layer_group_ids(const std::vector<Layer>& layers, std::set<LayerId>& ids) {
  for (const auto& layer : layers) {
    if (layer.kind() == LayerKind::Group) {
      ids.insert(layer.id());
    }
    collect_layer_group_ids(layer.children(), ids);
  }
}

void collect_initially_collapsed_layer_groups(const std::vector<Layer>& layers, std::set<LayerId>& ids) {
  for (const auto& layer : layers) {
    if (layer.kind() == LayerKind::Group) {
      if (const auto found = layer.metadata().find("photoslop.layer_group_expanded");
          found != layer.metadata().end() && found->second == "false") {
        ids.insert(layer.id());
      }
    }
    collect_initially_collapsed_layer_groups(layer.children(), ids);
  }
}

bool collect_layer_ancestor_groups(const std::vector<Layer>& layers, LayerId id, std::vector<LayerId>& ancestors) {
  for (const auto& layer : layers) {
    if (layer.id() == id) {
      return true;
    }
    if (collect_layer_ancestor_groups(layer.children(), id, ancestors)) {
      if (layer.kind() == LayerKind::Group) {
        ancestors.push_back(layer.id());
      }
      return true;
    }
  }
  return false;
}

const Layer* find_layer_in_tree(const std::vector<Layer>& layers, LayerId id) {
  for (const auto& layer : layers) {
    if (layer.id() == id) {
      return &layer;
    }
    if (const auto* found = find_layer_in_tree(layer.children(), id); found != nullptr) {
      return found;
    }
  }
  return nullptr;
}

Layer* find_layer_in_tree(std::vector<Layer>& layers, LayerId id) {
  for (auto& layer : layers) {
    if (layer.id() == id) {
      return &layer;
    }
    if (auto* found = find_layer_in_tree(layer.children(), id); found != nullptr) {
      return found;
    }
  }
  return nullptr;
}

bool layer_contains_descendant(const Layer& layer, LayerId id) {
  for (const auto& child : layer.children()) {
    if (child.id() == id || layer_contains_descendant(child, id)) {
      return true;
    }
  }
  return false;
}

std::vector<LayerId> root_drop_layer_ids(const std::vector<Layer>& layers,
                                         const std::vector<LayerId>& ids_top_to_bottom) {
  std::vector<LayerId> roots;
  std::set<LayerId> seen;
  for (const auto id : ids_top_to_bottom) {
    if (id == 0 || seen.contains(id)) {
      continue;
    }
    const auto* layer = find_layer_in_tree(layers, id);
    if (layer == nullptr) {
      return {};
    }

    bool has_selected_ancestor = false;
    for (const auto possible_ancestor_id : ids_top_to_bottom) {
      if (possible_ancestor_id == id || possible_ancestor_id == 0) {
        continue;
      }
      const auto* possible_ancestor = find_layer_in_tree(layers, possible_ancestor_id);
      if (possible_ancestor != nullptr && layer_contains_descendant(*possible_ancestor, id)) {
        has_selected_ancestor = true;
        break;
      }
    }

    if (!has_selected_ancestor) {
      roots.push_back(id);
      seen.insert(id);
    }
  }
  return roots;
}

bool target_is_inside_moved_layers(const std::vector<Layer>& layers, const std::vector<LayerId>& moving_ids,
                                   LayerId target_id) {
  for (const auto id : moving_ids) {
    const auto* moving = find_layer_in_tree(layers, id);
    if (moving != nullptr && (moving->id() == target_id || layer_contains_descendant(*moving, target_id))) {
      return true;
    }
  }
  return false;
}

std::optional<Layer> take_layer_from_tree(std::vector<Layer>& layers, LayerId id) {
  const auto found = std::find_if(layers.begin(), layers.end(), [id](const Layer& layer) {
    return layer.id() == id;
  });
  if (found != layers.end()) {
    auto layer = std::move(*found);
    layers.erase(found);
    return layer;
  }

  for (auto& layer : layers) {
    if (auto child = take_layer_from_tree(layer.children(), id); child.has_value()) {
      return child;
    }
  }
  return std::nullopt;
}

struct LayerSiblingLocation {
  std::vector<Layer>* siblings{nullptr};
  std::size_t index{0};
};

std::optional<LayerSiblingLocation> find_layer_location(std::vector<Layer>& layers, LayerId id) {
  for (std::size_t index = 0; index < layers.size(); ++index) {
    if (layers[index].id() == id) {
      return LayerSiblingLocation{&layers, index};
    }
    if (auto found = find_layer_location(layers[index].children(), id); found.has_value()) {
      return found;
    }
  }
  return std::nullopt;
}

void insert_layers_bottom_to_top(std::vector<Layer>& siblings, std::size_t insert_index,
                                 std::vector<Layer>& moved_top_to_bottom) {
  insert_index = std::min(insert_index, siblings.size());
  auto destination = siblings.begin() + static_cast<std::ptrdiff_t>(insert_index);
  for (auto it = moved_top_to_bottom.rbegin(); it != moved_top_to_bottom.rend(); ++it) {
    destination = siblings.insert(destination, std::move(*it));
    ++destination;
  }
}

std::vector<std::pair<LayerId, LayerId>> layer_tree_signature(const std::vector<Layer>& layers,
                                                             LayerId parent_id = 0) {
  std::vector<std::pair<LayerId, LayerId>> signature;
  std::function<void(const std::vector<Layer>&, LayerId)> append =
      [&](const std::vector<Layer>& current_layers, LayerId current_parent_id) {
    for (const auto& layer : current_layers) {
      signature.emplace_back(layer.id(), current_parent_id);
      append(layer.children(), layer.id());
    }
  };
  append(layers, parent_id);
  return signature;
}

bool move_layers_for_drop(std::vector<Layer>& layers, const LayerDropRequest& request) {
  auto moving_ids = root_drop_layer_ids(layers, request.layer_ids_top_to_bottom);
  if (moving_ids.empty()) {
    return false;
  }
  if (request.target_layer_id.has_value() &&
      target_is_inside_moved_layers(layers, moving_ids, *request.target_layer_id)) {
    return false;
  }

  std::vector<Layer> moved_top_to_bottom;
  moved_top_to_bottom.reserve(moving_ids.size());
  for (const auto id : moving_ids) {
    auto moved = take_layer_from_tree(layers, id);
    if (!moved.has_value()) {
      return false;
    }
    moved_top_to_bottom.push_back(std::move(*moved));
  }

  if (!request.target_layer_id.has_value()) {
    insert_layers_bottom_to_top(layers, 0, moved_top_to_bottom);
    return true;
  }

  if (request.position == LayerDropPosition::OnItem) {
    if (auto* target = find_layer_in_tree(layers, *request.target_layer_id);
        target != nullptr && target->kind() == LayerKind::Group) {
      insert_layers_bottom_to_top(target->children(), target->children().size(), moved_top_to_bottom);
      return true;
    }
  }

  auto target_location = find_layer_location(layers, *request.target_layer_id);
  if (!target_location.has_value() || target_location->siblings == nullptr) {
    return false;
  }

  const auto insert_index =
      request.position == LayerDropPosition::BelowItem ? target_location->index : target_location->index + 1U;
  insert_layers_bottom_to_top(*target_location->siblings, insert_index, moved_top_to_bottom);
  return true;
}

QWidget* make_layer_row_widget(const Layer& layer, QListWidgetItem* item, QWidget* parent, int depth = 0,
                               bool ancestors_visible = true, bool group_expanded = true,
                               std::function<void(LayerId)> toggle_group_expanded = {}) {
  auto* row = new QWidget(parent);
  row->setObjectName(QStringLiteral("layerRowWidget"));
  row->setAttribute(Qt::WA_StyledBackground, true);
  auto* list_parent = dynamic_cast<LayerListWidget*>(parent);
  if (list_parent != nullptr) {
    row->installEventFilter(list_parent);
  }
  auto* layout = new QHBoxLayout(row);
  layout->setContentsMargins(8 + std::max(0, depth) * 18, 5, 8, 5);
  layout->setSpacing(10);

  if (layer.kind() == LayerKind::Group) {
    auto* disclosure = new QToolButton(row);
    disclosure->setObjectName(QStringLiteral("layerFolderDisclosureButton"));
    disclosure->setCheckable(true);
    disclosure->setChecked(group_expanded);
    disclosure->setText(group_expanded ? QStringLiteral("v") : QStringLiteral(">"));
    disclosure->setToolButtonStyle(Qt::ToolButtonTextOnly);
    disclosure->setFixedSize(18, 20);
    disclosure->setEnabled(!layer.children().empty());
    disclosure->setToolTip(layer.children().empty()
                               ? QObject::tr("Folder is empty")
                               : group_expanded ? QObject::tr("Collapse folder") : QObject::tr("Expand folder"));
    QObject::connect(disclosure, &QToolButton::clicked, row,
                     [parent, id = layer.id(), toggle_group_expanded = std::move(toggle_group_expanded)] {
      if (toggle_group_expanded) {
        QTimer::singleShot(0, parent, [id, toggle_group_expanded] { toggle_group_expanded(id); });
      }
    });
    layout->addWidget(disclosure, 0, Qt::AlignVCenter);
  } else {
    auto* disclosure_spacer = new QWidget(row);
    disclosure_spacer->setFixedSize(18, 20);
    if (list_parent != nullptr) {
      disclosure_spacer->installEventFilter(list_parent);
    }
    layout->addWidget(disclosure_spacer, 0, Qt::AlignVCenter);
  }

  auto* visibility = new QToolButton(row);
  visibility->setObjectName(QStringLiteral("layerVisibilityCheck"));
  visibility->setCheckable(true);
  visibility->setChecked(layer.visible());
  visibility->setText(layer_visibility_indicator_text(layer.visible()));
  visibility->setToolTip(layer_visibility_tooltip(layer.visible()));
  visibility->setToolButtonStyle(Qt::ToolButtonTextOnly);
  visibility->setFixedSize(20, 20);
  visibility->setEnabled(ancestors_visible);
  if (list_parent != nullptr) {
    visibility->installEventFilter(list_parent);
  }
  layout->addWidget(visibility, 0, Qt::AlignVCenter);

  auto* text_column = new QVBoxLayout();
  text_column->setContentsMargins(0, 0, 0, 0);
  text_column->setSpacing(0);
  layout->addLayout(text_column, 1);

  const auto display_name = layer.kind() == LayerKind::Group
                                ? QObject::tr("[Folder] %1").arg(QString::fromStdString(layer.name()))
                                : QString::fromStdString(layer.name());
  auto* name = new QLabel(display_name, row);
  name->setObjectName(QStringLiteral("layerRowName"));
  name->setTextFormat(Qt::PlainText);
  name->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  name->setEnabled(ancestors_visible && layer.visible());
  if (list_parent != nullptr) {
    name->installEventFilter(list_parent);
  }
  text_column->addWidget(name);

  const auto mode = blend_mode_name(layer.blend_mode());
  const auto lock = layer_locks_transparent_pixels(layer) ? QObject::tr(" locked") : QString();
  const auto effects = !layer.layer_style().empty() ? QObject::tr(" fx") : QString();
  const auto dimensions = layer.kind() == LayerKind::Pixel
                              ? QObject::tr("%1 x %2").arg(layer.bounds().width).arg(layer.bounds().height)
                              : QObject::tr("folder, %1 layers").arg(layer_descendant_count(layer));
  auto* details = new QLabel(QObject::tr("%1  %2%  %3%4%5")
                                 .arg(mode)
                                 .arg(static_cast<int>(std::round(layer.opacity() * 100.0F)))
                                 .arg(dimensions)
                                 .arg(lock)
                                 .arg(effects),
                             row);
  details->setObjectName(QStringLiteral("layerRowDetails"));
  details->setTextFormat(Qt::PlainText);
  details->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  details->setMinimumWidth(0);
  details->setEnabled(ancestors_visible && layer.visible());
  if (list_parent != nullptr) {
    details->installEventFilter(list_parent);
  }
  text_column->addWidget(details);

  QObject::connect(visibility, &QToolButton::toggled, row, [item, visibility](bool checked) {
    visibility->setText(layer_visibility_indicator_text(checked));
    visibility->setToolTip(layer_visibility_tooltip(checked));
    if (item != nullptr && item->checkState() != (checked ? Qt::Checked : Qt::Unchecked)) {
      item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    }
  });
  return row;
}

QString open_file_filter() {
  return QObject::tr("Supported Files (*.psd *.psb *.png *.jpg *.jpeg *.bmp *.tif *.tiff *.webp);;"
                     "Photoshop Documents (*.psd *.psb);;"
                     "Images (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.webp);;"
                     "All Files (*.*)");
}

QString save_file_filter() {
  return QObject::tr("Photoshop Document (*.psd);;"
                     "PNG Image (*.png);;"
                     "JPEG Image (*.jpg *.jpeg);;"
                     "Bitmap Image (*.bmp);;"
                     "TIFF Image (*.tif *.tiff);;"
                     "WebP Image (*.webp)");
}

QString export_image_filter() {
  return QObject::tr("PNG Image (*.png);;"
                     "JPEG Image (*.jpg *.jpeg);;"
                     "Bitmap Image (*.bmp);;"
                     "TIFF Image (*.tif *.tiff);;"
                     "WebP Image (*.webp)");
}

QString extension_for_path(const QString& path) {
  return QFileInfo(path).suffix().toLower();
}

bool is_photoshop_document_extension(const QString& extension) {
  return extension == QStringLiteral("psd") || extension == QStringLiteral("psb");
}

QString path_with_default_extension(QString path, const QString& selected_filter) {
  if (!QFileInfo(path).suffix().isEmpty()) {
    return path;
  }

  if (selected_filter.contains(QStringLiteral("PNG"))) {
    return path + QStringLiteral(".png");
  }
  if (selected_filter.contains(QStringLiteral("JPEG"))) {
    return path + QStringLiteral(".jpg");
  }
  if (selected_filter.contains(QStringLiteral("Bitmap"))) {
    return path + QStringLiteral(".bmp");
  }
  if (selected_filter.contains(QStringLiteral("TIFF"))) {
    return path + QStringLiteral(".tif");
  }
  if (selected_filter.contains(QStringLiteral("WebP"))) {
    return path + QStringLiteral(".webp");
  }
  return path + QStringLiteral(".psd");
}

std::string legacy_plugin_kind_name(LegacyPhotoshopPluginKind kind) {
  switch (kind) {
    case LegacyPhotoshopPluginKind::Filter8bf:
      return "filter";
    case LegacyPhotoshopPluginKind::Format8bi:
      return "file-format";
    case LegacyPhotoshopPluginKind::Automation8li:
      return "automation";
    case LegacyPhotoshopPluginKind::Unknown:
      return "unknown";
  }
  return "unknown";
}

QDialog* create_photoslop_color_panel(QWidget* parent, QColor initial, const QString& title,
                                      std::function<void(QColor)> color_changed) {
  initial.setAlpha(255);
  auto* dialog = new QDialog(parent);
  dialog->setObjectName(QStringLiteral("photoslopColorDialog"));
  dialog->setWindowTitle(title);
  dialog->setModal(false);
  dialog->setWindowModality(Qt::NonModal);
  dialog->setAttribute(Qt::WA_DeleteOnClose, true);
  auto* layout = new QVBoxLayout(dialog);
  auto* preview = new QLabel(dialog);
  preview->setObjectName(QStringLiteral("colorPreview"));
  preview->setFixedHeight(44);
  layout->addWidget(preview);
  auto* gradient_field = new ColorGradientField(dialog);
  gradient_field->set_color(initial);
  layout->addWidget(gradient_field);

  auto* grid = new QGridLayout();
  layout->addLayout(grid);

  auto add_channel = [&](int row, const QString& label, int value, const QString& object_prefix) {
    auto* slider = new QSlider(Qt::Horizontal, dialog);
    auto* spin = new QSpinBox(dialog);
    slider->setRange(0, 255);
    spin->setRange(0, 255);
    slider->setValue(value);
    spin->setValue(value);
    slider->setObjectName(object_prefix + QStringLiteral("Slider"));
    spin->setObjectName(object_prefix + QStringLiteral("Spin"));
    configure_dialog_spinbox(spin, 58);
    grid->addWidget(new QLabel(label, dialog), row, 0);
    grid->addWidget(slider, row, 1);
    grid->addWidget(spin, row, 2);
    QObject::connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
    QObject::connect(spin, &QSpinBox::valueChanged, slider, &QSlider::setValue);
    return spin;
  };

  auto* red = add_channel(0, QObject::tr("R"), initial.red(), QStringLiteral("colorRed"));
  auto* green = add_channel(1, QObject::tr("G"), initial.green(), QStringLiteral("colorGreen"));
  auto* blue = add_channel(2, QObject::tr("B"), initial.blue(), QStringLiteral("colorBlue"));
  const auto current_color = [red, green, blue] { return QColor(red->value(), green->value(), blue->value()); };
  const auto update_preview = [preview, gradient_field, current_color] {
    const auto color = current_color();
    preview->setStyleSheet(QStringLiteral("QLabel { background: rgb(%1, %2, %3); border: 1px solid #9aa4b2; }")
                               .arg(color.red())
                               .arg(color.green())
                               .arg(color.blue()));
    gradient_field->set_color(color);
  };
  auto changed_callback = std::make_shared<std::function<void(QColor)>>(std::move(color_changed));
  const auto apply_color = [current_color, update_preview, changed_callback] {
    update_preview();
    auto color = current_color();
    color.setAlpha(255);
    if (*changed_callback) {
      (*changed_callback)(color);
    }
  };
  QObject::connect(red, qOverload<int>(&QSpinBox::valueChanged), dialog, [apply_color](int) { apply_color(); });
  QObject::connect(green, qOverload<int>(&QSpinBox::valueChanged), dialog, [apply_color](int) { apply_color(); });
  QObject::connect(blue, qOverload<int>(&QSpinBox::valueChanged), dialog, [apply_color](int) { apply_color(); });
  gradient_field->color_chosen = [red, green, blue](QColor color) {
    red->setValue(color.red());
    green->setValue(color.green());
    blue->setValue(color.blue());
  };
  update_preview();

  auto* swatches = new QGridLayout();
  layout->addLayout(swatches);
  const std::vector<QColor> colors = {Qt::black,       Qt::white,       QColor(220, 20, 40), QColor(255, 140, 0),
                                      QColor(255, 220, 0), QColor(30, 160, 80), QColor(0, 150, 220), QColor(50, 90, 220),
                                      QColor(140, 70, 220), QColor(230, 60, 170), QColor(128, 128, 128), QColor(245, 248, 252)};
  int index = 0;
  for (const auto& color : colors) {
    auto* button = new QPushButton(dialog);
    button->setObjectName(QStringLiteral("colorDialogSwatch"));
    button->setStyleSheet(swatch_button_style(color, true));
    swatches->addWidget(button, index / 6, index % 6);
    QObject::connect(button, &QPushButton::clicked, dialog, [red, green, blue, color] {
      red->setValue(color.red());
      green->setValue(color.green());
      blue->setValue(color.blue());
    });
    ++index;
  }

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
  QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::close);
  return dialog;
}

struct NewDocumentSettings {
  std::int32_t width{1024};
  std::int32_t height{768};
  QColor background{Qt::white};
};

struct NewLayerSettings {
  QString name;
  int opacity{100};
  BlendMode blend_mode{BlendMode::Normal};
};

struct LayerStyleSettings {
  int opacity{100};
  BlendMode blend_mode{BlendMode::Normal};
  LayerStyle style;
};

struct CanvasSizeSettings {
  std::int32_t width{0};
  std::int32_t height{0};
};

struct TextToolSettings {
  QString text;
  QString family;
  int size{36};
  bool bold{false};
  bool italic{false};
};

class TextCommitFilter final : public QObject {
public:
  TextCommitFilter(QPointer<QTextEdit> editor, std::function<void()> commit, QObject* parent)
      : QObject(parent), editor_(std::move(editor)), commit_(std::move(commit)) {}

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if ((watched == editor_ || (editor_ != nullptr && watched == editor_->viewport())) &&
        event->type() == QEvent::FocusOut) {
      QTimer::singleShot(0, this, [this] {
        if (editor_ != nullptr && !editor_->hasFocus() && !editor_->viewport()->hasFocus()) {
          commit_();
        }
      });
    }
    return QObject::eventFilter(watched, event);
  }

private:
  QPointer<QTextEdit> editor_;
  std::function<void()> commit_;
};

struct LayerTransformSettings {
  std::int32_t x{0};
  std::int32_t y{0};
  std::int32_t width{1};
  std::int32_t height{1};
};

struct HueSaturationSettings {
  int hue_shift{0};
  int saturation_delta{0};
  int lightness_delta{0};
};

struct LevelsSettings {
  int black_input{0};
  int white_input{255};
  int gamma_percent{100};
};

struct CurvesSettings {
  int shadow_output{0};
  int midtone_output{128};
  int highlight_output{255};
};

struct ColorBalanceSettings {
  int cyan_red{0};
  int magenta_green{0};
  int yellow_blue{0};
};

void add_blend_mode_items(QComboBox* combo) {
  combo->addItem(blend_mode_name(BlendMode::Normal), static_cast<int>(BlendMode::Normal));
  combo->addItem(blend_mode_name(BlendMode::Multiply), static_cast<int>(BlendMode::Multiply));
  combo->addItem(blend_mode_name(BlendMode::Screen), static_cast<int>(BlendMode::Screen));
  combo->addItem(blend_mode_name(BlendMode::Overlay), static_cast<int>(BlendMode::Overlay));
  combo->addItem(blend_mode_name(BlendMode::Darken), static_cast<int>(BlendMode::Darken));
  combo->addItem(blend_mode_name(BlendMode::Lighten), static_cast<int>(BlendMode::Lighten));
  combo->addItem(blend_mode_name(BlendMode::ColorDodge), static_cast<int>(BlendMode::ColorDodge));
  combo->addItem(blend_mode_name(BlendMode::ColorBurn), static_cast<int>(BlendMode::ColorBurn));
  combo->addItem(blend_mode_name(BlendMode::HardLight), static_cast<int>(BlendMode::HardLight));
  combo->addItem(blend_mode_name(BlendMode::SoftLight), static_cast<int>(BlendMode::SoftLight));
  combo->addItem(blend_mode_name(BlendMode::Difference), static_cast<int>(BlendMode::Difference));
  combo->addItem(blend_mode_name(BlendMode::LinearBurn), static_cast<int>(BlendMode::LinearBurn));
  combo->addItem(blend_mode_name(BlendMode::PinLight), static_cast<int>(BlendMode::PinLight));
  combo->addItem(blend_mode_name(BlendMode::Saturation), static_cast<int>(BlendMode::Saturation));
  combo->addItem(blend_mode_name(BlendMode::Luminosity), static_cast<int>(BlendMode::Luminosity));
}

LayerStyleGradient default_layer_style_gradient() {
  LayerStyleGradient gradient;
  gradient.angle_degrees = 90.0F;
  gradient.scale = 1.0F;
  gradient.color_stops.push_back(GradientColorStop{0.0F, RgbColor{255, 255, 255}});
  gradient.color_stops.push_back(GradientColorStop{1.0F, RgbColor{32, 32, 32}});
  gradient.alpha_stops.push_back(GradientAlphaStop{0.0F, 1.0F});
  gradient.alpha_stops.push_back(GradientAlphaStop{1.0F, 1.0F});
  return gradient;
}

LayerDropShadow default_drop_shadow() {
  LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.color = RgbColor{0, 0, 0};
  shadow.opacity = 0.75F;
  shadow.angle_degrees = 120.0F;
  shadow.distance = 5.0F;
  shadow.spread = 0.0F;
  shadow.size = 5.0F;
  return shadow;
}

LayerOuterGlow default_outer_glow() {
  LayerOuterGlow glow;
  glow.enabled = true;
  glow.blend_mode = BlendMode::Screen;
  glow.color = RgbColor{255, 255, 190};
  glow.opacity = 0.75F;
  glow.spread = 0.0F;
  glow.size = 5.0F;
  return glow;
}

LayerGradientFill default_gradient_fill() {
  LayerGradientFill fill;
  fill.enabled = true;
  fill.blend_mode = BlendMode::Normal;
  fill.opacity = 1.0F;
  fill.gradient = default_layer_style_gradient();
  return fill;
}

LayerStroke default_stroke() {
  LayerStroke stroke;
  stroke.enabled = true;
  stroke.blend_mode = BlendMode::Normal;
  stroke.color = RgbColor{0, 0, 0};
  stroke.opacity = 1.0F;
  stroke.size = 3.0F;
  stroke.position = LayerStrokePosition::Outside;
  return stroke;
}

LayerBevelEmboss default_bevel_emboss() {
  LayerBevelEmboss bevel;
  bevel.enabled = true;
  bevel.highlight_blend_mode = BlendMode::Screen;
  bevel.highlight_color = RgbColor{255, 255, 255};
  bevel.highlight_opacity = 0.75F;
  bevel.shadow_blend_mode = BlendMode::Multiply;
  bevel.shadow_color = RgbColor{0, 0, 0};
  bevel.shadow_opacity = 0.75F;
  bevel.angle_degrees = 120.0F;
  bevel.altitude_degrees = 30.0F;
  bevel.depth = 1.0F;
  bevel.size = 5.0F;
  bevel.direction_up = true;
  return bevel;
}

QListWidgetItem* add_layer_style_category(QListWidget* list, const QString& text, bool checkable, bool checked) {
  auto* item = new QListWidgetItem(text, list);
  if (checkable) {
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
  } else {
    item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
  }
  return item;
}

std::optional<NewDocumentSettings> request_new_document_settings(QWidget* parent) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("photoslopNewDocumentDialog"));
  dialog.setWindowTitle(QObject::tr("New Document"));
  auto* layout = new QVBoxLayout(&dialog);
  auto* form = new QFormLayout();
  layout->addLayout(form);

  auto* preset = new QComboBox(&dialog);
  preset->setObjectName(QStringLiteral("newDocumentPresetCombo"));
  preset->addItem(QObject::tr("Photoslop Default"), QSize(1024, 768));
  preset->addItem(QObject::tr("HD 1920 x 1080"), QSize(1920, 1080));
  preset->addItem(QObject::tr("Square 2048"), QSize(2048, 2048));
  preset->addItem(QObject::tr("Print Letter 300ppi"), QSize(2550, 3300));
  form->addRow(QObject::tr("Preset"), preset);

  auto* width = new QSpinBox(&dialog);
  width->setObjectName(QStringLiteral("newDocumentWidthSpin"));
  width->setRange(1, 30000);
  width->setValue(1024);
  configure_dialog_spinbox(width);
  auto* height = new QSpinBox(&dialog);
  height->setObjectName(QStringLiteral("newDocumentHeightSpin"));
  height->setRange(1, 30000);
  height->setValue(768);
  configure_dialog_spinbox(height);
  form->addRow(QObject::tr("Width"), width);
  form->addRow(QObject::tr("Height"), height);

  auto* background = new QComboBox(&dialog);
  background->setObjectName(QStringLiteral("newDocumentBackgroundCombo"));
  background->addItem(QObject::tr("White"), QColor(Qt::white));
  background->addItem(QObject::tr("Black"), QColor(Qt::black));
  background->addItem(QObject::tr("Transparent"), QColor(0, 0, 0, 0));
  form->addRow(QObject::tr("Background"), background);

  QObject::connect(preset, &QComboBox::currentIndexChanged, &dialog, [preset, width, height](int index) {
    const auto size = preset->itemData(index).toSize();
    if (size.isValid()) {
      width->setValue(size.width());
      height->setValue(size.height());
    }
  });

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  if (dialog.exec() != QDialog::Accepted) {
    return std::nullopt;
  }
  return NewDocumentSettings{width->value(), height->value(), background->currentData().value<QColor>()};
}

std::optional<NewLayerSettings> request_new_layer_settings(QWidget* parent, int layer_number) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("photoslopNewLayerDialog"));
  dialog.setWindowTitle(QObject::tr("New Layer"));
  auto* layout = new QVBoxLayout(&dialog);
  auto* form = new QFormLayout();
  layout->addLayout(form);

  auto* name = new QLineEdit(QObject::tr("Layer %1").arg(layer_number), &dialog);
  name->setObjectName(QStringLiteral("newLayerNameEdit"));
  form->addRow(QObject::tr("Name"), name);

  auto* blend = new QComboBox(&dialog);
  blend->setObjectName(QStringLiteral("newLayerBlendCombo"));
  blend->addItem(blend_mode_name(BlendMode::Normal), static_cast<int>(BlendMode::Normal));
  blend->addItem(blend_mode_name(BlendMode::Multiply), static_cast<int>(BlendMode::Multiply));
  blend->addItem(blend_mode_name(BlendMode::Screen), static_cast<int>(BlendMode::Screen));
  blend->addItem(blend_mode_name(BlendMode::Overlay), static_cast<int>(BlendMode::Overlay));
  blend->addItem(blend_mode_name(BlendMode::Darken), static_cast<int>(BlendMode::Darken));
  blend->addItem(blend_mode_name(BlendMode::Lighten), static_cast<int>(BlendMode::Lighten));
  blend->addItem(blend_mode_name(BlendMode::ColorDodge), static_cast<int>(BlendMode::ColorDodge));
  blend->addItem(blend_mode_name(BlendMode::ColorBurn), static_cast<int>(BlendMode::ColorBurn));
  blend->addItem(blend_mode_name(BlendMode::HardLight), static_cast<int>(BlendMode::HardLight));
  blend->addItem(blend_mode_name(BlendMode::SoftLight), static_cast<int>(BlendMode::SoftLight));
  blend->addItem(blend_mode_name(BlendMode::Difference), static_cast<int>(BlendMode::Difference));
  blend->addItem(blend_mode_name(BlendMode::LinearBurn), static_cast<int>(BlendMode::LinearBurn));
  blend->addItem(blend_mode_name(BlendMode::PinLight), static_cast<int>(BlendMode::PinLight));
  blend->addItem(blend_mode_name(BlendMode::Saturation), static_cast<int>(BlendMode::Saturation));
  blend->addItem(blend_mode_name(BlendMode::Luminosity), static_cast<int>(BlendMode::Luminosity));
  form->addRow(QObject::tr("Mode"), blend);

  auto* opacity = new QSpinBox(&dialog);
  opacity->setObjectName(QStringLiteral("newLayerOpacitySpin"));
  opacity->setRange(0, 100);
  opacity->setValue(100);
  configure_dialog_spinbox(opacity, 72);
  form->addRow(QObject::tr("Opacity"), opacity);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  if (dialog.exec() != QDialog::Accepted || name->text().trimmed().isEmpty()) {
    return std::nullopt;
  }
  return NewLayerSettings{name->text().trimmed(), opacity->value(), static_cast<BlendMode>(blend->currentData().toInt())};
}

std::optional<LayerStyleSettings> request_layer_style_settings(
    QWidget* parent, const Layer& layer, std::function<void(const LayerStyleSettings&)> preview_changed = {}) {
  auto style = layer.layer_style();
  auto shadow = style.drop_shadows.empty() ? default_drop_shadow() : style.drop_shadows.front();
  auto outer_glow = style.outer_glows.empty() ? default_outer_glow() : style.outer_glows.front();
  auto gradient = style.gradient_fills.empty() ? default_gradient_fill() : style.gradient_fills.front();
  auto stroke = style.strokes.empty() ? default_stroke() : style.strokes.front();
  auto bevel = style.bevels.empty() ? default_bevel_emboss() : style.bevels.front();
  if (gradient.gradient.color_stops.empty()) {
    gradient.gradient = default_layer_style_gradient();
  }
  if (stroke.uses_gradient && stroke.gradient.color_stops.empty()) {
    stroke.gradient = default_layer_style_gradient();
  }

  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("photoslopLayerStyleDialog"));
  dialog.setWindowTitle(QObject::tr("Layer Style"));
  dialog.resize(760, 480);
  auto* root = new QVBoxLayout(&dialog);

  auto* name_row = new QHBoxLayout();
  name_row->addWidget(new QLabel(QObject::tr("Name:"), &dialog));
  auto* name = new QLineEdit(QString::fromStdString(layer.name()), &dialog);
  name->setObjectName(QStringLiteral("layerStyleLayerNameEdit"));
  name->setReadOnly(true);
  name_row->addWidget(name, 1);
  root->addLayout(name_row);

  auto* body = new QHBoxLayout();
  root->addLayout(body, 1);

  auto* categories = new QListWidget(&dialog);
  categories->setObjectName(QStringLiteral("layerStyleCategoryList"));
  categories->setMinimumWidth(210);
  categories->setMaximumWidth(230);
  add_layer_style_category(categories, QObject::tr("Blending Options"), false, true);
  auto* bevel_item = add_layer_style_category(categories, QObject::tr("Bevel & Emboss"), true,
                                             !style.bevels.empty() && style.bevels.front().enabled);
  auto* stroke_item =
      add_layer_style_category(categories, QObject::tr("Stroke"), true, !style.strokes.empty() && style.strokes.front().enabled);
  auto* gradient_item =
      add_layer_style_category(categories, QObject::tr("Gradient Overlay"), true,
                               !style.gradient_fills.empty() && style.gradient_fills.front().enabled);
  auto* outer_glow_item =
      add_layer_style_category(categories, QObject::tr("Outer Glow"), true,
                               !style.outer_glows.empty() && style.outer_glows.front().enabled);
  auto* shadow_item =
      add_layer_style_category(categories, QObject::tr("Drop Shadow"), true,
                               !style.drop_shadows.empty() && style.drop_shadows.front().enabled);
  auto install_category_checkbox = [&dialog, categories](QListWidgetItem* item, const QString& object_name) {
    auto* check = new QCheckBox(item->text(), categories);
    check->setObjectName(object_name);
    check->setChecked(item->checkState() == Qt::Checked);
    check->setMinimumHeight(26);
    check->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    item->setSizeHint(QSize(0, 28));
    categories->setItemWidget(item, check);
    QObject::connect(check, &QCheckBox::toggled, &dialog, [categories, item](bool checked) {
      item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
      categories->setCurrentItem(item);
    });
    QObject::connect(categories, &QListWidget::itemChanged, &dialog, [item, check](QListWidgetItem* changed) {
      if (changed != item) {
        return;
      }
      QSignalBlocker blocker(check);
      check->setChecked(changed->checkState() == Qt::Checked);
    });
  };
  install_category_checkbox(bevel_item, QStringLiteral("layerStyleBevelEmbossCategoryCheck"));
  install_category_checkbox(stroke_item, QStringLiteral("layerStyleStrokeCategoryCheck"));
  install_category_checkbox(gradient_item, QStringLiteral("layerStyleGradientOverlayCategoryCheck"));
  install_category_checkbox(outer_glow_item, QStringLiteral("layerStyleOuterGlowCategoryCheck"));
  install_category_checkbox(shadow_item, QStringLiteral("layerStyleDropShadowCategoryCheck"));
  categories->setCurrentRow(0);
  body->addWidget(categories);

  auto* controls = new QStackedWidget(&dialog);
  controls->setObjectName(QStringLiteral("layerStyleOptionsStack"));
  body->addWidget(controls, 1);
  auto make_page = [controls](const QString& object_name) {
    auto* page = new QWidget(controls);
    page->setObjectName(object_name);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    controls->addWidget(page);
    return layout;
  };

  auto* blending_layout = make_page(QStringLiteral("layerStyleBlendingPage"));
  auto* bevel_layout = make_page(QStringLiteral("layerStyleBevelEmbossPage"));
  auto* stroke_layout = make_page(QStringLiteral("layerStyleStrokePage"));
  auto* gradient_layout = make_page(QStringLiteral("layerStyleGradientOverlayPage"));
  auto* outer_glow_layout = make_page(QStringLiteral("layerStyleOuterGlowPage"));
  auto* shadow_layout = make_page(QStringLiteral("layerStyleDropShadowPage"));

  auto* blending_group = new QGroupBox(QObject::tr("Blending Options"), controls);
  auto* blending_form = new QFormLayout(blending_group);
  auto* blend = new QComboBox(blending_group);
  blend->setObjectName(QStringLiteral("layerStyleBlendModeCombo"));
  add_blend_mode_items(blend);
  blend->setCurrentIndex(std::max(0, blend->findData(static_cast<int>(layer.blend_mode()))));
  blending_form->addRow(QObject::tr("Blend Mode"), blend);
  auto* opacity = new QSpinBox(blending_group);
  opacity->setObjectName(QStringLiteral("layerStyleOpacitySpin"));
  opacity->setRange(0, 100);
  opacity->setValue(static_cast<int>(std::round(layer.opacity() * 100.0F)));
  opacity->setSuffix(QStringLiteral("%"));
  configure_dialog_spinbox(opacity, 72);
  blending_form->addRow(QObject::tr("Opacity"), opacity);
  blending_layout->addWidget(blending_group);

  auto* preview_check = new QCheckBox(QObject::tr("Preview"), controls);
  preview_check->setObjectName(QStringLiteral("layerStylePreviewCheck"));
  preview_check->setChecked(style.effects_visible);
  blending_layout->addWidget(preview_check);
  blending_layout->addStretch(1);

  auto* bevel_group = new QGroupBox(QObject::tr("Bevel & Emboss"), controls);
  auto* bevel_form = new QFormLayout(bevel_group);
  auto* bevel_size = new QSpinBox(bevel_group);
  bevel_size->setObjectName(QStringLiteral("layerStyleBevelSizeSpin"));
  bevel_size->setRange(1, 250);
  bevel_size->setValue(static_cast<int>(std::round(bevel.size)));
  configure_dialog_spinbox(bevel_size, 72);
  bevel_form->addRow(QObject::tr("Size"), bevel_size);
  auto* bevel_depth = new QSpinBox(bevel_group);
  bevel_depth->setObjectName(QStringLiteral("layerStyleBevelDepthSpin"));
  bevel_depth->setRange(1, 1000);
  bevel_depth->setValue(static_cast<int>(std::round(bevel.depth * 100.0F)));
  bevel_depth->setSuffix(QStringLiteral("%"));
  configure_dialog_spinbox(bevel_depth, 72);
  bevel_form->addRow(QObject::tr("Depth"), bevel_depth);
  auto* bevel_angle = new QSpinBox(bevel_group);
  bevel_angle->setObjectName(QStringLiteral("layerStyleBevelAngleSpin"));
  bevel_angle->setRange(-180, 180);
  bevel_angle->setValue(static_cast<int>(std::round(bevel.angle_degrees)));
  configure_dialog_spinbox(bevel_angle, 72);
  bevel_form->addRow(QObject::tr("Angle"), bevel_angle);
  auto* bevel_altitude = new QSpinBox(bevel_group);
  bevel_altitude->setObjectName(QStringLiteral("layerStyleBevelAltitudeSpin"));
  bevel_altitude->setRange(0, 90);
  bevel_altitude->setValue(static_cast<int>(std::round(bevel.altitude_degrees)));
  configure_dialog_spinbox(bevel_altitude, 72);
  bevel_form->addRow(QObject::tr("Altitude"), bevel_altitude);
  auto* bevel_direction = new QComboBox(bevel_group);
  bevel_direction->setObjectName(QStringLiteral("layerStyleBevelDirectionCombo"));
  bevel_direction->addItem(QObject::tr("Up"), true);
  bevel_direction->addItem(QObject::tr("Down"), false);
  bevel_direction->setCurrentIndex(bevel.direction_up ? 0 : 1);
  bevel_form->addRow(QObject::tr("Direction"), bevel_direction);
  auto* bevel_highlight_opacity = new QSpinBox(bevel_group);
  bevel_highlight_opacity->setObjectName(QStringLiteral("layerStyleBevelHighlightOpacitySpin"));
  bevel_highlight_opacity->setRange(0, 100);
  bevel_highlight_opacity->setValue(static_cast<int>(std::round(bevel.highlight_opacity * 100.0F)));
  bevel_highlight_opacity->setSuffix(QStringLiteral("%"));
  configure_dialog_spinbox(bevel_highlight_opacity, 72);
  bevel_form->addRow(QObject::tr("Highlight Opacity"), bevel_highlight_opacity);
  auto* bevel_shadow_opacity = new QSpinBox(bevel_group);
  bevel_shadow_opacity->setObjectName(QStringLiteral("layerStyleBevelShadowOpacitySpin"));
  bevel_shadow_opacity->setRange(0, 100);
  bevel_shadow_opacity->setValue(static_cast<int>(std::round(bevel.shadow_opacity * 100.0F)));
  bevel_shadow_opacity->setSuffix(QStringLiteral("%"));
  configure_dialog_spinbox(bevel_shadow_opacity, 72);
  bevel_form->addRow(QObject::tr("Shadow Opacity"), bevel_shadow_opacity);
  bevel_layout->addWidget(bevel_group);
  bevel_layout->addStretch(1);

  auto* stroke_group = new QGroupBox(QObject::tr("Stroke"), controls);
  auto* stroke_form = new QFormLayout(stroke_group);
  auto* stroke_size = new QSpinBox(stroke_group);
  stroke_size->setObjectName(QStringLiteral("layerStyleStrokeSizeSpin"));
  stroke_size->setRange(1, 250);
  stroke_size->setValue(static_cast<int>(std::round(stroke.size)));
  configure_dialog_spinbox(stroke_size, 72);
  stroke_form->addRow(QObject::tr("Size"), stroke_size);
  auto* stroke_opacity = new QSpinBox(stroke_group);
  stroke_opacity->setObjectName(QStringLiteral("layerStyleStrokeOpacitySpin"));
  stroke_opacity->setRange(0, 100);
  stroke_opacity->setValue(static_cast<int>(std::round(stroke.opacity * 100.0F)));
  stroke_opacity->setSuffix(QStringLiteral("%"));
  configure_dialog_spinbox(stroke_opacity, 72);
  stroke_form->addRow(QObject::tr("Opacity"), stroke_opacity);
  auto* stroke_color_row = new QWidget(stroke_group);
  auto* stroke_color_layout = new QHBoxLayout(stroke_color_row);
  stroke_color_layout->setContentsMargins(0, 0, 0, 0);
  stroke_color_layout->setSpacing(6);
  auto make_color_spin = [stroke_group, stroke_color_layout](const QString& object_name, std::uint8_t value) {
    auto* spin = new QSpinBox(stroke_group);
    spin->setObjectName(object_name);
    spin->setRange(0, 255);
    spin->setValue(value);
    configure_dialog_spinbox(spin, 54);
    stroke_color_layout->addWidget(spin);
    return spin;
  };
  auto* stroke_red = make_color_spin(QStringLiteral("layerStyleStrokeRedSpin"), stroke.color.red);
  auto* stroke_green = make_color_spin(QStringLiteral("layerStyleStrokeGreenSpin"), stroke.color.green);
  auto* stroke_blue = make_color_spin(QStringLiteral("layerStyleStrokeBlueSpin"), stroke.color.blue);
  auto* stroke_color_preview = new QLabel(stroke_group);
  stroke_color_preview->setObjectName(QStringLiteral("layerStyleStrokeColorPreview"));
  stroke_color_preview->setFixedSize(28, 22);
  stroke_color_layout->addWidget(stroke_color_preview);
  stroke_color_layout->addStretch(1);
  auto update_stroke_color_preview = [stroke_color_preview, stroke_red, stroke_green, stroke_blue] {
    stroke_color_preview->setStyleSheet(QStringLiteral("QLabel { background: rgb(%1, %2, %3); border: 1px solid #9aa4b2; }")
                                            .arg(stroke_red->value())
                                            .arg(stroke_green->value())
                                            .arg(stroke_blue->value()));
  };
  update_stroke_color_preview();
  stroke_form->addRow(QObject::tr("Color RGB"), stroke_color_row);
  auto* stroke_position = new QComboBox(stroke_group);
  stroke_position->setObjectName(QStringLiteral("layerStyleStrokePositionCombo"));
  stroke_position->addItem(QObject::tr("Outside"), static_cast<int>(LayerStrokePosition::Outside));
  stroke_position->addItem(QObject::tr("Inside"), static_cast<int>(LayerStrokePosition::Inside));
  stroke_position->addItem(QObject::tr("Center"), static_cast<int>(LayerStrokePosition::Center));
  stroke_position->setCurrentIndex(std::max(0, stroke_position->findData(static_cast<int>(stroke.position))));
  stroke_form->addRow(QObject::tr("Position"), stroke_position);
  stroke_layout->addWidget(stroke_group);
  stroke_layout->addStretch(1);

  auto* gradient_group = new QGroupBox(QObject::tr("Gradient Overlay"), controls);
  auto* gradient_form = new QFormLayout(gradient_group);
  auto* gradient_opacity = new QSpinBox(gradient_group);
  gradient_opacity->setObjectName(QStringLiteral("layerStyleGradientOpacitySpin"));
  gradient_opacity->setRange(0, 100);
  gradient_opacity->setValue(static_cast<int>(std::round(gradient.opacity * 100.0F)));
  gradient_opacity->setSuffix(QStringLiteral("%"));
  configure_dialog_spinbox(gradient_opacity, 72);
  gradient_form->addRow(QObject::tr("Opacity"), gradient_opacity);
  auto* gradient_angle = new QSpinBox(gradient_group);
  gradient_angle->setObjectName(QStringLiteral("layerStyleGradientAngleSpin"));
  gradient_angle->setRange(-180, 180);
  gradient_angle->setValue(static_cast<int>(std::round(gradient.gradient.angle_degrees)));
  configure_dialog_spinbox(gradient_angle, 72);
  gradient_form->addRow(QObject::tr("Angle"), gradient_angle);
  auto* gradient_scale = new QSpinBox(gradient_group);
  gradient_scale->setObjectName(QStringLiteral("layerStyleGradientScaleSpin"));
  gradient_scale->setRange(1, 1000);
  gradient_scale->setValue(static_cast<int>(std::round(gradient.gradient.scale * 100.0F)));
  gradient_scale->setSuffix(QStringLiteral("%"));
  configure_dialog_spinbox(gradient_scale, 72);
  gradient_form->addRow(QObject::tr("Scale"), gradient_scale);
  auto* gradient_stops = new QTableWidget(0, 4, gradient_group);
  gradient_stops->setObjectName(QStringLiteral("layerStyleGradientStopsTable"));
  gradient_stops->setHorizontalHeaderLabels(
      {QObject::tr("Location"), QObject::tr("R"), QObject::tr("G"), QObject::tr("B")});
  gradient_stops->verticalHeader()->setVisible(false);
  gradient_stops->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  gradient_stops->setSelectionBehavior(QAbstractItemView::SelectRows);
  gradient_stops->setSelectionMode(QAbstractItemView::SingleSelection);
  gradient_stops->setMinimumHeight(128);
  auto set_stop_item = [gradient_stops](int row, int column, int value) {
    auto* item = new QTableWidgetItem(QString::number(value));
    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    gradient_stops->setItem(row, column, item);
  };
  auto add_gradient_stop_row = [gradient_stops, &set_stop_item](float location, RgbColor color) {
    const auto row = gradient_stops->rowCount();
    gradient_stops->insertRow(row);
    set_stop_item(row, 0, static_cast<int>(std::round(std::clamp(location, 0.0F, 1.0F) * 100.0F)));
    set_stop_item(row, 1, color.red);
    set_stop_item(row, 2, color.green);
    set_stop_item(row, 3, color.blue);
  };
  for (const auto& stop : gradient.gradient.color_stops) {
    add_gradient_stop_row(stop.location, stop.color);
  }
  if (gradient_stops->rowCount() == 0) {
    const auto fallback = default_layer_style_gradient();
    for (const auto& stop : fallback.color_stops) {
      add_gradient_stop_row(stop.location, stop.color);
    }
  }
  gradient_form->addRow(QObject::tr("Color Stops"), gradient_stops);
  auto* gradient_stop_buttons = new QWidget(gradient_group);
  auto* gradient_stop_button_layout = new QHBoxLayout(gradient_stop_buttons);
  gradient_stop_button_layout->setContentsMargins(0, 0, 0, 0);
  gradient_stop_button_layout->setSpacing(6);
  auto* add_gradient_stop = new QPushButton(QObject::tr("Add Stop"), gradient_stop_buttons);
  add_gradient_stop->setObjectName(QStringLiteral("layerStyleGradientAddStopButton"));
  auto* remove_gradient_stop = new QPushButton(QObject::tr("Remove Stop"), gradient_stop_buttons);
  remove_gradient_stop->setObjectName(QStringLiteral("layerStyleGradientRemoveStopButton"));
  gradient_stop_button_layout->addWidget(add_gradient_stop);
  gradient_stop_button_layout->addWidget(remove_gradient_stop);
  gradient_stop_button_layout->addStretch(1);
  gradient_form->addRow(QString(), gradient_stop_buttons);
  gradient_layout->addWidget(gradient_group);
  gradient_layout->addStretch(1);

  auto* outer_glow_group = new QGroupBox(QObject::tr("Outer Glow"), controls);
  auto* outer_glow_form = new QFormLayout(outer_glow_group);
  auto* outer_glow_blend = new QComboBox(outer_glow_group);
  outer_glow_blend->setObjectName(QStringLiteral("layerStyleOuterGlowBlendModeCombo"));
  add_blend_mode_items(outer_glow_blend);
  outer_glow_blend->setCurrentIndex(std::max(0, outer_glow_blend->findData(static_cast<int>(outer_glow.blend_mode))));
  outer_glow_form->addRow(QObject::tr("Blend Mode"), outer_glow_blend);
  auto* outer_glow_opacity = new QSpinBox(outer_glow_group);
  outer_glow_opacity->setObjectName(QStringLiteral("layerStyleOuterGlowOpacitySpin"));
  outer_glow_opacity->setRange(0, 100);
  outer_glow_opacity->setValue(static_cast<int>(std::round(outer_glow.opacity * 100.0F)));
  outer_glow_opacity->setSuffix(QStringLiteral("%"));
  configure_dialog_spinbox(outer_glow_opacity, 72);
  outer_glow_form->addRow(QObject::tr("Opacity"), outer_glow_opacity);
  auto* outer_glow_size = new QSpinBox(outer_glow_group);
  outer_glow_size->setObjectName(QStringLiteral("layerStyleOuterGlowSizeSpin"));
  outer_glow_size->setRange(0, 1000);
  outer_glow_size->setValue(static_cast<int>(std::round(outer_glow.size)));
  configure_dialog_spinbox(outer_glow_size, 72);
  outer_glow_form->addRow(QObject::tr("Size"), outer_glow_size);
  auto* outer_glow_spread = new QSpinBox(outer_glow_group);
  outer_glow_spread->setObjectName(QStringLiteral("layerStyleOuterGlowSpreadSpin"));
  outer_glow_spread->setRange(0, 100);
  outer_glow_spread->setValue(static_cast<int>(std::round(outer_glow.spread)));
  outer_glow_spread->setSuffix(QStringLiteral("%"));
  configure_dialog_spinbox(outer_glow_spread, 72);
  outer_glow_form->addRow(QObject::tr("Spread"), outer_glow_spread);
  auto* outer_glow_color_row = new QWidget(outer_glow_group);
  auto* outer_glow_color_layout = new QHBoxLayout(outer_glow_color_row);
  outer_glow_color_layout->setContentsMargins(0, 0, 0, 0);
  outer_glow_color_layout->setSpacing(6);
  auto add_outer_glow_color_spin = [outer_glow_group, outer_glow_color_layout](const QString& object_name,
                                                                               std::uint8_t value) {
    auto* spin = new QSpinBox(outer_glow_group);
    spin->setObjectName(object_name);
    spin->setRange(0, 255);
    spin->setValue(value);
    configure_dialog_spinbox(spin, 54);
    outer_glow_color_layout->addWidget(spin);
    return spin;
  };
  auto* outer_glow_red = add_outer_glow_color_spin(QStringLiteral("layerStyleOuterGlowRedSpin"), outer_glow.color.red);
  auto* outer_glow_green =
      add_outer_glow_color_spin(QStringLiteral("layerStyleOuterGlowGreenSpin"), outer_glow.color.green);
  auto* outer_glow_blue =
      add_outer_glow_color_spin(QStringLiteral("layerStyleOuterGlowBlueSpin"), outer_glow.color.blue);
  auto* outer_glow_color_preview = new QLabel(outer_glow_group);
  outer_glow_color_preview->setObjectName(QStringLiteral("layerStyleOuterGlowColorPreview"));
  outer_glow_color_preview->setFixedSize(28, 22);
  outer_glow_color_layout->addWidget(outer_glow_color_preview);
  outer_glow_color_layout->addStretch(1);
  auto update_outer_glow_color_preview = [outer_glow_color_preview, outer_glow_red, outer_glow_green,
                                          outer_glow_blue] {
    outer_glow_color_preview
        ->setStyleSheet(QStringLiteral("QLabel { background: rgb(%1, %2, %3); border: 1px solid #9aa4b2; }")
                            .arg(outer_glow_red->value())
                            .arg(outer_glow_green->value())
                            .arg(outer_glow_blue->value()));
  };
  update_outer_glow_color_preview();
  outer_glow_form->addRow(QObject::tr("Color RGB"), outer_glow_color_row);
  outer_glow_layout->addWidget(outer_glow_group);
  outer_glow_layout->addStretch(1);

  auto* shadow_group = new QGroupBox(QObject::tr("Drop Shadow"), controls);
  auto* shadow_form = new QFormLayout(shadow_group);
  auto* shadow_opacity = new QSpinBox(shadow_group);
  shadow_opacity->setObjectName(QStringLiteral("layerStyleDropShadowOpacitySpin"));
  shadow_opacity->setRange(0, 100);
  shadow_opacity->setValue(static_cast<int>(std::round(shadow.opacity * 100.0F)));
  shadow_opacity->setSuffix(QStringLiteral("%"));
  configure_dialog_spinbox(shadow_opacity, 72);
  shadow_form->addRow(QObject::tr("Opacity"), shadow_opacity);
  auto* shadow_angle = new QSpinBox(shadow_group);
  shadow_angle->setObjectName(QStringLiteral("layerStyleDropShadowAngleSpin"));
  shadow_angle->setRange(-180, 180);
  shadow_angle->setValue(static_cast<int>(std::round(shadow.angle_degrees)));
  configure_dialog_spinbox(shadow_angle, 72);
  shadow_form->addRow(QObject::tr("Angle"), shadow_angle);
  auto* shadow_distance = new QSpinBox(shadow_group);
  shadow_distance->setObjectName(QStringLiteral("layerStyleDropShadowDistanceSpin"));
  shadow_distance->setRange(0, 1000);
  shadow_distance->setValue(static_cast<int>(std::round(shadow.distance)));
  configure_dialog_spinbox(shadow_distance, 72);
  shadow_form->addRow(QObject::tr("Distance"), shadow_distance);
  auto* shadow_size = new QSpinBox(shadow_group);
  shadow_size->setObjectName(QStringLiteral("layerStyleDropShadowSizeSpin"));
  shadow_size->setRange(0, 1000);
  shadow_size->setValue(static_cast<int>(std::round(shadow.size)));
  configure_dialog_spinbox(shadow_size, 72);
  shadow_form->addRow(QObject::tr("Size"), shadow_size);
  auto* shadow_spread = new QSpinBox(shadow_group);
  shadow_spread->setObjectName(QStringLiteral("layerStyleDropShadowSpreadSpin"));
  shadow_spread->setRange(0, 100);
  shadow_spread->setValue(static_cast<int>(std::round(shadow.spread)));
  shadow_spread->setSuffix(QStringLiteral("%"));
  configure_dialog_spinbox(shadow_spread, 72);
  shadow_form->addRow(QObject::tr("Spread"), shadow_spread);
  shadow_layout->addWidget(shadow_group);
  shadow_layout->addStretch(1);

  QObject::connect(categories, &QListWidget::currentRowChanged, controls, &QStackedWidget::setCurrentIndex);
  controls->setCurrentIndex(categories->currentRow());

  auto build_current_settings = [&]() {
    LayerStyle result = style;
    result.effects_visible = preview_check->isChecked();
    const auto shadow_enabled = shadow_item->checkState() == Qt::Checked;
    const auto outer_glow_enabled = outer_glow_item->checkState() == Qt::Checked;
    const auto gradient_enabled = gradient_item->checkState() == Qt::Checked;
    const auto stroke_enabled = stroke_item->checkState() == Qt::Checked;
    const auto bevel_enabled = bevel_item->checkState() == Qt::Checked;

    if (bevel_enabled || !result.bevels.empty()) {
      if (result.bevels.empty()) {
        result.bevels.push_back(default_bevel_emboss());
      }
      auto& target = result.bevels.front();
      target.enabled = bevel_enabled;
      target.size = static_cast<float>(bevel_size->value());
      target.depth = static_cast<float>(bevel_depth->value()) / 100.0F;
      target.angle_degrees = static_cast<float>(bevel_angle->value());
      target.altitude_degrees = static_cast<float>(bevel_altitude->value());
      target.direction_up = bevel_direction->currentData().toBool();
      target.highlight_opacity = static_cast<float>(bevel_highlight_opacity->value()) / 100.0F;
      target.shadow_opacity = static_cast<float>(bevel_shadow_opacity->value()) / 100.0F;
    } else {
      result.bevels.clear();
    }

    if (outer_glow_enabled || !result.outer_glows.empty()) {
      if (result.outer_glows.empty()) {
        result.outer_glows.push_back(default_outer_glow());
      }
      auto& target = result.outer_glows.front();
      target.enabled = outer_glow_enabled;
      target.blend_mode = static_cast<BlendMode>(outer_glow_blend->currentData().toInt());
      target.opacity = static_cast<float>(outer_glow_opacity->value()) / 100.0F;
      target.size = static_cast<float>(outer_glow_size->value());
      target.spread = static_cast<float>(outer_glow_spread->value());
      target.color = RgbColor{static_cast<std::uint8_t>(outer_glow_red->value()),
                              static_cast<std::uint8_t>(outer_glow_green->value()),
                              static_cast<std::uint8_t>(outer_glow_blue->value())};
    } else {
      result.outer_glows.clear();
    }

    if (shadow_enabled || !result.drop_shadows.empty()) {
      if (result.drop_shadows.empty()) {
        result.drop_shadows.push_back(default_drop_shadow());
      }
      auto& target = result.drop_shadows.front();
      target.enabled = shadow_enabled;
      target.opacity = static_cast<float>(shadow_opacity->value()) / 100.0F;
      target.angle_degrees = static_cast<float>(shadow_angle->value());
      target.distance = static_cast<float>(shadow_distance->value());
      target.size = static_cast<float>(shadow_size->value());
      target.spread = static_cast<float>(shadow_spread->value());
    } else {
      result.drop_shadows.clear();
    }

    if (gradient_enabled || !result.gradient_fills.empty()) {
      if (result.gradient_fills.empty()) {
        result.gradient_fills.push_back(default_gradient_fill());
      }
      auto& target = result.gradient_fills.front();
      target.enabled = gradient_enabled;
      target.opacity = static_cast<float>(gradient_opacity->value()) / 100.0F;
      if (target.gradient.color_stops.empty()) {
        target.gradient = default_layer_style_gradient();
      }
      target.gradient.angle_degrees = static_cast<float>(gradient_angle->value());
      target.gradient.scale = static_cast<float>(gradient_scale->value()) / 100.0F;
      target.gradient.color_stops.clear();
      for (int row = 0; row < gradient_stops->rowCount(); ++row) {
        auto cell_value = [gradient_stops, row](int column, int fallback) {
          const auto* item = gradient_stops->item(row, column);
          bool ok = false;
          const auto value = item == nullptr ? fallback : item->text().toInt(&ok);
          return ok ? value : fallback;
        };
        target.gradient.color_stops.push_back(GradientColorStop{
            std::clamp(static_cast<float>(cell_value(0, row == 0 ? 0 : 100)) / 100.0F, 0.0F, 1.0F),
            RgbColor{static_cast<std::uint8_t>(std::clamp(cell_value(1, 255), 0, 255)),
                     static_cast<std::uint8_t>(std::clamp(cell_value(2, 255), 0, 255)),
                     static_cast<std::uint8_t>(std::clamp(cell_value(3, 255), 0, 255))}});
      }
      if (target.gradient.color_stops.empty()) {
        target.gradient.color_stops = default_layer_style_gradient().color_stops;
      }
      std::sort(target.gradient.color_stops.begin(), target.gradient.color_stops.end(),
                [](const GradientColorStop& lhs, const GradientColorStop& rhs) {
                  return lhs.location < rhs.location;
                });
    } else {
      result.gradient_fills.clear();
    }

    if (stroke_enabled || !result.strokes.empty()) {
      if (result.strokes.empty()) {
        result.strokes.push_back(default_stroke());
      }
      auto& target = result.strokes.front();
      target.enabled = stroke_enabled;
      target.size = static_cast<float>(stroke_size->value());
      target.opacity = static_cast<float>(stroke_opacity->value()) / 100.0F;
      target.color = RgbColor{static_cast<std::uint8_t>(stroke_red->value()),
                              static_cast<std::uint8_t>(stroke_green->value()),
                              static_cast<std::uint8_t>(stroke_blue->value())};
      target.position = static_cast<LayerStrokePosition>(stroke_position->currentData().toInt());
    } else {
      result.strokes.clear();
    }

    return LayerStyleSettings{opacity->value(), static_cast<BlendMode>(blend->currentData().toInt()),
                              std::move(result)};
  };

  auto emit_preview = [&] {
    update_stroke_color_preview();
    update_outer_glow_color_preview();
    if (preview_changed) {
      preview_changed(build_current_settings());
    }
  };
  auto table_cell_value = [gradient_stops](int row, int column, int fallback) {
    const auto* item = gradient_stops->item(row, column);
    bool ok = false;
    const auto value = item == nullptr ? fallback : item->text().toInt(&ok);
    return ok ? value : fallback;
  };
  QObject::connect(categories, &QListWidget::itemChanged, &dialog, [&emit_preview](QListWidgetItem*) { emit_preview(); });
  QObject::connect(blend, &QComboBox::currentIndexChanged, &dialog, [&emit_preview](int) { emit_preview(); });
  QObject::connect(opacity, qOverload<int>(&QSpinBox::valueChanged), &dialog, [&emit_preview](int) { emit_preview(); });
  QObject::connect(preview_check, &QCheckBox::toggled, &dialog, [&emit_preview](bool) { emit_preview(); });
  for (auto* spin : {bevel_size, bevel_depth, bevel_angle, bevel_altitude, bevel_highlight_opacity,
                     bevel_shadow_opacity, stroke_size, stroke_opacity, stroke_red, stroke_green, stroke_blue,
                     gradient_opacity, gradient_angle, gradient_scale, outer_glow_opacity, outer_glow_size,
                     outer_glow_spread, outer_glow_red, outer_glow_green, outer_glow_blue, shadow_opacity,
                     shadow_angle, shadow_distance, shadow_size, shadow_spread}) {
    QObject::connect(spin, qOverload<int>(&QSpinBox::valueChanged), &dialog, [&emit_preview](int) { emit_preview(); });
  }
  QObject::connect(bevel_direction, &QComboBox::currentIndexChanged, &dialog, [&emit_preview](int) { emit_preview(); });
  QObject::connect(outer_glow_blend, &QComboBox::currentIndexChanged, &dialog,
                   [&emit_preview](int) { emit_preview(); });
  QObject::connect(stroke_position, &QComboBox::currentIndexChanged, &dialog, [&emit_preview](int) { emit_preview(); });
  QObject::connect(gradient_stops, &QTableWidget::itemChanged, &dialog, [&emit_preview](QTableWidgetItem*) {
    emit_preview();
  });
  QObject::connect(add_gradient_stop, &QPushButton::clicked, &dialog, [&] {
    const QSignalBlocker blocker(gradient_stops);
    const auto source_row = std::clamp(gradient_stops->currentRow(), 0, std::max(0, gradient_stops->rowCount() - 1));
    const auto location =
        std::clamp(table_cell_value(source_row, 0, 50) + (gradient_stops->rowCount() > 0 ? 10 : 0), 0, 100);
    const auto red = table_cell_value(source_row, 1, 255);
    const auto green = table_cell_value(source_row, 2, 255);
    const auto blue = table_cell_value(source_row, 3, 255);
    add_gradient_stop_row(static_cast<float>(location) / 100.0F,
                          RgbColor{static_cast<std::uint8_t>(std::clamp(red, 0, 255)),
                                   static_cast<std::uint8_t>(std::clamp(green, 0, 255)),
                                   static_cast<std::uint8_t>(std::clamp(blue, 0, 255))});
    gradient_stops->setCurrentCell(gradient_stops->rowCount() - 1, 0);
    emit_preview();
  });
  QObject::connect(remove_gradient_stop, &QPushButton::clicked, &dialog, [&] {
    if (gradient_stops->rowCount() <= 2) {
      return;
    }
    const QSignalBlocker blocker(gradient_stops);
    const auto row = std::clamp(gradient_stops->currentRow(), 0, gradient_stops->rowCount() - 1);
    gradient_stops->removeRow(row);
    gradient_stops->setCurrentCell(std::min(row, gradient_stops->rowCount() - 1), 0);
    emit_preview();
  });

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  root->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  if (dialog.exec() != QDialog::Accepted) {
    return std::nullopt;
  }

  return build_current_settings();
}

std::optional<CanvasSizeSettings> request_canvas_size_settings(QWidget* parent, std::int32_t current_width,
                                                               std::int32_t current_height) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("photoslopCanvasSizeDialog"));
  dialog.setWindowTitle(QObject::tr("Canvas Size"));
  auto* layout = new QVBoxLayout(&dialog);
  auto* form = new QFormLayout();
  layout->addLayout(form);

  auto* width = new QSpinBox(&dialog);
  width->setObjectName(QStringLiteral("canvasSizeWidthSpin"));
  width->setRange(1, 30000);
  width->setValue(current_width);
  configure_dialog_spinbox(width);
  auto* height = new QSpinBox(&dialog);
  height->setObjectName(QStringLiteral("canvasSizeHeightSpin"));
  height->setRange(1, 30000);
  height->setValue(current_height);
  configure_dialog_spinbox(height);
  form->addRow(QObject::tr("Width"), width);
  form->addRow(QObject::tr("Height"), height);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  if (dialog.exec() != QDialog::Accepted) {
    return std::nullopt;
  }
  return CanvasSizeSettings{width->value(), height->value()};
}

std::optional<LayerTransformSettings> request_layer_transform_settings(QWidget* parent, Rect current) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("photoslopTransformDialog"));
  dialog.setWindowTitle(QObject::tr("Free Transform"));
  auto* layout = new QVBoxLayout(&dialog);
  auto* form = new QFormLayout();
  layout->addLayout(form);

  auto make_spin = [&dialog](const QString& object_name, int value, int minimum, int maximum) {
    auto* spin = new QSpinBox(&dialog);
    spin->setObjectName(object_name);
    spin->setRange(minimum, maximum);
    spin->setValue(value);
    configure_dialog_spinbox(spin);
    return spin;
  };

  auto* x = make_spin(QStringLiteral("transformXSpin"), current.x, -30000, 30000);
  auto* y = make_spin(QStringLiteral("transformYSpin"), current.y, -30000, 30000);
  auto* width = make_spin(QStringLiteral("transformWidthSpin"), current.width, 1, 30000);
  auto* height = make_spin(QStringLiteral("transformHeightSpin"), current.height, 1, 30000);
  form->addRow(QObject::tr("X"), x);
  form->addRow(QObject::tr("Y"), y);
  form->addRow(QObject::tr("W"), width);
  form->addRow(QObject::tr("H"), height);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  if (dialog.exec() != QDialog::Accepted) {
    return std::nullopt;
  }
  return LayerTransformSettings{x->value(), y->value(), width->value(), height->value()};
}

std::optional<LevelsSettings> request_levels_settings(QWidget* parent) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("photoslopLevelsDialog"));
  dialog.setWindowTitle(QObject::tr("Levels"));
  auto* layout = new QVBoxLayout(&dialog);
  auto* form = new QFormLayout();
  layout->addLayout(form);

  const auto make_row = [&dialog, form](const QString& label, const QString& object_prefix, int minimum, int maximum,
                                        int value) {
    auto* container = new QWidget(&dialog);
    auto* row = new QHBoxLayout(container);
    row->setContentsMargins(0, 0, 0, 0);
    auto* slider = new QSlider(Qt::Horizontal, container);
    auto* spin = new QSpinBox(container);
    slider->setRange(minimum, maximum);
    spin->setRange(minimum, maximum);
    slider->setValue(value);
    spin->setValue(value);
    slider->setObjectName(object_prefix + QStringLiteral("Slider"));
    spin->setObjectName(object_prefix + QStringLiteral("Spin"));
    configure_dialog_spinbox(spin, 72);
    row->addWidget(slider, 1);
    row->addWidget(spin);
    QObject::connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
    QObject::connect(spin, &QSpinBox::valueChanged, slider, &QSlider::setValue);
    form->addRow(label, container);
    return spin;
  };

  auto* black = make_row(QObject::tr("Black Input"), QStringLiteral("levelsBlackInput"), 0, 254, 0);
  auto* white = make_row(QObject::tr("White Input"), QStringLiteral("levelsWhiteInput"), 1, 255, 255);
  auto* gamma = make_row(QObject::tr("Gamma"), QStringLiteral("levelsGamma"), 10, 999, 100);
  gamma->setSuffix(QStringLiteral("%"));

  QObject::connect(black, &QSpinBox::valueChanged, &dialog, [black, white](int value) {
    if (white->value() <= value) {
      white->setValue(std::min(255, value + 1));
    }
  });
  QObject::connect(white, &QSpinBox::valueChanged, &dialog, [black, white](int value) {
    if (black->value() >= value) {
      black->setValue(std::max(0, value - 1));
    }
  });

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  if (dialog.exec() != QDialog::Accepted) {
    return std::nullopt;
  }
  return LevelsSettings{black->value(), white->value(), gamma->value()};
}

std::optional<CurvesSettings> request_curves_settings(QWidget* parent) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("photoslopCurvesDialog"));
  dialog.setWindowTitle(QObject::tr("Curves"));
  auto* layout = new QVBoxLayout(&dialog);
  auto* form = new QFormLayout();
  layout->addLayout(form);

  const auto make_row = [&dialog, form](const QString& label, const QString& object_prefix, int value) {
    auto* container = new QWidget(&dialog);
    auto* row = new QHBoxLayout(container);
    row->setContentsMargins(0, 0, 0, 0);
    auto* slider = new QSlider(Qt::Horizontal, container);
    auto* spin = new QSpinBox(container);
    slider->setRange(0, 255);
    spin->setRange(0, 255);
    slider->setValue(value);
    spin->setValue(value);
    slider->setObjectName(object_prefix + QStringLiteral("Slider"));
    spin->setObjectName(object_prefix + QStringLiteral("Spin"));
    configure_dialog_spinbox(spin, 72);
    row->addWidget(slider, 1);
    row->addWidget(spin);
    QObject::connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
    QObject::connect(spin, &QSpinBox::valueChanged, slider, &QSlider::setValue);
    form->addRow(label, container);
    return spin;
  };

  auto* shadow = make_row(QObject::tr("Shadows Output"), QStringLiteral("curvesShadowOutput"), 0);
  auto* midtone = make_row(QObject::tr("Midtones Output"), QStringLiteral("curvesMidtoneOutput"), 128);
  auto* highlight = make_row(QObject::tr("Highlights Output"), QStringLiteral("curvesHighlightOutput"), 255);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  if (dialog.exec() != QDialog::Accepted) {
    return std::nullopt;
  }
  return CurvesSettings{shadow->value(), midtone->value(), highlight->value()};
}

std::optional<HueSaturationSettings> request_hue_saturation_settings(QWidget* parent) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("photoslopHueSaturationDialog"));
  dialog.setWindowTitle(QObject::tr("Hue/Saturation"));
  auto* layout = new QVBoxLayout(&dialog);
  auto* form = new QFormLayout();
  layout->addLayout(form);

  const auto make_adjustment_row = [&dialog, form](const QString& label, const QString& object_prefix, int minimum,
                                                   int maximum) {
    auto* container = new QWidget(&dialog);
    auto* row = new QHBoxLayout(container);
    row->setContentsMargins(0, 0, 0, 0);
    auto* slider = new QSlider(Qt::Horizontal, container);
    auto* spin = new QSpinBox(container);
    slider->setRange(minimum, maximum);
    spin->setRange(minimum, maximum);
    slider->setValue(0);
    spin->setValue(0);
    slider->setObjectName(object_prefix + QStringLiteral("Slider"));
    spin->setObjectName(object_prefix + QStringLiteral("Spin"));
    configure_dialog_spinbox(spin, 72);
    row->addWidget(slider, 1);
    row->addWidget(spin);
    QObject::connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
    QObject::connect(spin, &QSpinBox::valueChanged, slider, &QSlider::setValue);
    form->addRow(label, container);
    return spin;
  };

  auto* hue = make_adjustment_row(QObject::tr("Hue"), QStringLiteral("hueSaturationHue"), -180, 180);
  auto* saturation =
      make_adjustment_row(QObject::tr("Saturation"), QStringLiteral("hueSaturationSaturation"), -100, 100);
  auto* lightness =
      make_adjustment_row(QObject::tr("Lightness"), QStringLiteral("hueSaturationLightness"), -100, 100);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  if (dialog.exec() != QDialog::Accepted) {
    return std::nullopt;
  }
  return HueSaturationSettings{hue->value(), saturation->value(), lightness->value()};
}

std::optional<ColorBalanceSettings> request_color_balance_settings(QWidget* parent) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("photoslopColorBalanceDialog"));
  dialog.setWindowTitle(QObject::tr("Color Balance"));
  auto* layout = new QVBoxLayout(&dialog);
  auto* form = new QFormLayout();
  layout->addLayout(form);

  const auto make_balance_row = [&dialog, form](const QString& label, const QString& object_prefix) {
    auto* container = new QWidget(&dialog);
    auto* row = new QHBoxLayout(container);
    row->setContentsMargins(0, 0, 0, 0);
    auto* slider = new QSlider(Qt::Horizontal, container);
    auto* spin = new QSpinBox(container);
    slider->setRange(-100, 100);
    spin->setRange(-100, 100);
    slider->setValue(0);
    spin->setValue(0);
    slider->setObjectName(object_prefix + QStringLiteral("Slider"));
    spin->setObjectName(object_prefix + QStringLiteral("Spin"));
    configure_dialog_spinbox(spin, 72);
    row->addWidget(slider, 1);
    row->addWidget(spin);
    QObject::connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
    QObject::connect(spin, &QSpinBox::valueChanged, slider, &QSlider::setValue);
    form->addRow(label, container);
    return spin;
  };

  auto* cyan_red = make_balance_row(QObject::tr("Cyan / Red"), QStringLiteral("colorBalanceCyanRed"));
  auto* magenta_green =
      make_balance_row(QObject::tr("Magenta / Green"), QStringLiteral("colorBalanceMagentaGreen"));
  auto* yellow_blue = make_balance_row(QObject::tr("Yellow / Blue"), QStringLiteral("colorBalanceYellowBlue"));

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  if (dialog.exec() != QDialog::Accepted) {
    return std::nullopt;
  }
  return ColorBalanceSettings{cyan_red->value(), magenta_green->value(), yellow_blue->value()};
}

QString photoshop_style() {
  return QStringLiteral(R"(
    QMainWindow, QMenuBar, QMenu, QDockWidget, QWidget {
      background: #262626;
      color: #e6e6e6;
      font-size: 12px;
    }
    QMenuBar {
      background: #535353;
      color: #f0f0f0;
      border-bottom: 1px solid #343434;
      min-height: 31px;
    }
    QMenuBar::item {
      background: transparent;
      padding: 7px 13px;
      margin: 0 1px;
    }
    QMenuBar::item:selected, QMenu::item:selected {
      background: #3a3a3a;
    }
    QMenu {
      background: #3a3a3a;
      border: 1px solid #1f1f1f;
    }
    QMenu::item {
      padding: 7px 34px 7px 24px;
    }
    QMenu::separator {
      height: 1px;
      background: #555555;
      margin: 4px 6px;
    }
    QToolBar {
      background: #3b3b3b;
      border: 0;
      border-bottom: 1px solid #292929;
      spacing: 2px;
      padding: 3px;
    }
    QToolButton {
      background: transparent;
      border: 1px solid transparent;
      border-radius: 0;
      padding: 3px;
      min-width: 26px;
      min-height: 26px;
    }
    QToolButton:hover {
      background: #4a4a4a;
      border-color: #696969;
    }
    QToolButton:checked {
      background: #2f75bd;
      border-color: #6bb3ff;
    }
    QToolBar#toolPalette {
      background: #535353;
      border-right: 1px solid #202020;
      border-bottom: 0;
      padding: 3px 4px;
      spacing: 1px;
    }
    QToolBar#toolPalette QToolButton {
      min-width: 28px;
      max-width: 28px;
      min-height: 24px;
      max-height: 24px;
      padding: 1px;
    }
    QWidget#toolPaletteSpacer {
      background: #535353;
    }
    QToolBar#Options {
      background: #3d3d3d;
      min-height: 38px;
      max-height: 38px;
      border-top: 1px solid #5a5a5a;
      border-bottom: 1px solid #292929;
      spacing: 5px;
      padding: 4px 7px;
    }
    QToolBar#Options QLabel {
      color: #e1e1e1;
      padding-left: 5px;
      padding-right: 2px;
    }
    QToolBar#Options QLabel[optionLabel="true"] {
      background: #262626;
      border: 1px solid #171717;
      border-right: 0;
      border-top-color: #5d5d5d;
      color: #f0f0f0;
      min-height: 24px;
      max-height: 24px;
      padding: 0 7px;
    }
    QToolBar#Options QSpinBox, QToolBar#Options QComboBox, QToolBar#Options QFontComboBox {
      min-height: 24px;
      max-height: 24px;
      padding-left: 4px;
      background: #292929;
      border: 1px solid #171717;
      border-top-color: #5d5d5d;
    }
    QWidget#selectionFeatherGroup {
      background: #292929;
      border: 1px solid #171717;
      border-top-color: #5d5d5d;
      min-height: 24px;
      max-height: 24px;
    }
    QWidget#selectionFeatherGroup QLabel {
      background: #262626;
      border: 0;
      border-right: 1px solid #171717;
      color: #f0f0f0;
      min-height: 24px;
      max-height: 24px;
      padding: 0 8px;
    }
    QWidget#selectionFeatherGroup QSpinBox {
      background: #292929;
      border: 0;
      min-height: 24px;
      max-height: 24px;
      padding-left: 6px;
    }
    QToolBar#Options QCheckBox {
      color: #f0f0f0;
      min-height: 24px;
      max-height: 24px;
      padding-left: 6px;
      padding-right: 8px;
      spacing: 6px;
    }
    QToolBar#Options QCheckBox#selectionAntiAliasCheck {
      background: #292929;
      border: 1px solid #171717;
      border-top-color: #5d5d5d;
      padding-left: 7px;
      padding-right: 10px;
    }
    QToolBar#Options QCheckBox::indicator {
      width: 14px;
      height: 14px;
      background: #1f1f1f;
      border: 1px solid #777777;
    }
    QToolBar#Options QCheckBox::indicator:hover {
      border-color: #9ccfff;
    }
    QToolBar#Options QCheckBox::indicator:checked {
      background: #1473e6;
      border-color: #9ccfff;
    }
    QToolBar#Options QSlider::groove:horizontal {
      height: 4px;
      background: #1c1c1c;
      border: 1px solid #555555;
    }
    QToolBar#Options QSlider::sub-page:horizontal {
      background: #1473e6;
      border: 1px solid #5aa9ff;
    }
    QToolBar#Options QSlider::handle:horizontal {
      background: #c9d0d8;
      border: 1px solid #101010;
      width: 10px;
      margin: -5px 0;
    }
    QToolBar#Options QPushButton {
      min-height: 24px;
      max-height: 24px;
      background: #303030;
      border: 1px solid #171717;
      border-top-color: #5d5d5d;
      padding: 1px 7px;
    }
    QToolBar#Options QPushButton:checked {
      background: #1667b7;
      border-color: #63adff;
      color: #ffffff;
    }
    QDockWidget::title {
      background: #323232;
      padding: 5px;
      border-bottom: 1px solid #202020;
    }
    QWidget#historyDockTitle, QWidget#swatchesDockTitle, QWidget#propertiesDockTitle, QWidget#infoDockTitle,
    QWidget#layersDockTitle {
      background: #2f3032;
      border-top: 1px solid #45474b;
      border-bottom: 1px solid #1b1c1e;
    }
    QWidget#historyDockTitle QLabel, QWidget#swatchesDockTitle QLabel, QWidget#propertiesDockTitle QLabel,
    QWidget#infoDockTitle QLabel, QWidget#layersDockTitle QLabel {
      color: #f0f0f0;
      font-weight: 600;
    }
    QToolButton[dockCollapseButton="true"] {
      background: transparent;
      color: #cfd3d8;
      border: 1px solid transparent;
      border-radius: 0;
      padding: 0;
      min-width: 18px;
      max-width: 18px;
      min-height: 18px;
      max-height: 18px;
      font-weight: 700;
    }
    QToolButton[dockCollapseButton="true"]:hover {
      background: #3b3d40;
      border-color: #5b5e63;
    }
    QToolButton[dockCollapseButton="true"]:checked {
      background: transparent;
      color: #cfd3d8;
      border-color: transparent;
    }
    QListWidget, QComboBox, QSpinBox, QSlider, QLineEdit, QTextEdit {
      background: #2b2b2b;
      color: #e6e6e6;
      border: 1px solid #5a5a5a;
      selection-background-color: #3a414a;
      min-height: 20px;
    }
    QListWidget::item {
      min-height: 48px;
      padding: 0;
      border-bottom: 1px solid #202225;
    }
    QListWidget::item:selected {
      background: #3a414a;
      color: #f4f6f8;
      border: 1px solid #67717d;
    }
    QListWidget#layerList::item {
      color: transparent;
    }
    QListWidget#layerList::item:selected {
      color: transparent;
    }
    QListWidget::indicator {
      width: 0;
      height: 0;
      max-width: 0;
      max-height: 0;
      background: transparent;
      border: 0;
      margin: 0;
    }
    QListWidget::indicator:checked {
      background: transparent;
      border: 0;
    }
    QListWidget#layerStyleCategoryList::item {
      min-height: 24px;
      padding: 4px 6px;
      border-bottom: 1px solid #3b3b3b;
    }
    QListWidget#layerStyleCategoryList::indicator {
      width: 0;
      height: 0;
      max-width: 0;
      max-height: 0;
      margin: 0;
      background: transparent;
      border: 0;
    }
    QListWidget#layerStyleCategoryList::indicator:checked {
      background: transparent;
      border: 0;
    }
    QLabel#layerRowName {
      color: #f0f3f8;
      font-size: 12px;
    }
    QLabel#layerRowDetails {
      color: #aeb6c2;
      font-size: 10px;
    }
    QLabel#canvasInfoLabel, QLabel#documentInfoLabel {
      color: #d7dde6;
      line-height: 130%;
    }
    QWidget#layersPanel {
      background: #28292b;
    }
    QListWidget#layerList {
      min-height: 300px;
    }
    QToolButton#layerFolderDisclosureButton {
      background: transparent;
      color: #d9e0ea;
      border: 1px solid transparent;
      border-radius: 3px;
      padding: 0;
      font-size: 10px;
      font-weight: 700;
      min-width: 18px;
      max-width: 18px;
      min-height: 20px;
      max-height: 20px;
    }
    QToolButton#layerFolderDisclosureButton:hover {
      border-color: #6f7b88;
      background: #30343a;
    }
    QToolButton#layerFolderDisclosureButton[layerDragActive="true"]:hover {
      border-color: transparent;
      background: transparent;
    }
    QToolButton#layerFolderDisclosureButton:disabled {
      color: transparent;
      border-color: transparent;
      background: transparent;
    }
    QToolButton#layerVisibilityCheck {
      background: #24272b;
      color: #f2f6fb;
      border: 1px solid #6d747d;
      border-radius: 3px;
      padding: 0;
      font-size: 12px;
      font-weight: 700;
      min-width: 20px;
      max-width: 20px;
      min-height: 20px;
      max-height: 20px;
    }
    QToolButton#layerVisibilityCheck:hover {
      border-color: #d5e8ff;
    }
    QToolButton#layerVisibilityCheck[layerDragActive="true"]:hover {
      border-color: #6d747d;
      background: #24272b;
    }
    QToolButton#layerVisibilityCheck:checked {
      background: #2e3f50;
      border-color: #9ccfff;
    }
    QToolButton#layerVisibilityCheck[layerDragActive="true"]:checked:hover {
      background: #2e3f50;
      border-color: #9ccfff;
    }
    QToolButton#layerVisibilityCheck:!checked {
      background: #24272b;
      border-color: #7b858f;
      color: transparent;
    }
    QPushButton {
      background: #3a3a3a;
      color: #e6e6e6;
      border: 1px solid #666666;
      border-radius: 0;
      padding: 4px 8px;
    }
    QPushButton:hover {
      background: #4a4a4a;
      border-color: #8a8a8a;
    }
    QPushButton[layerActionButton="true"] {
      padding: 0;
      min-width: 40px;
      max-width: 40px;
      min-height: 34px;
      max-height: 34px;
    }
    QStatusBar {
      background: #252525;
      color: #cfcfcf;
    }
    QCheckBox, QLabel {
      color: #e1e1e1;
    }
    QCheckBox::indicator {
      width: 12px;
      height: 12px;
    }
    QTabWidget::pane {
      border-top: 1px solid #5c5c5c;
    }
    QTabBar::tab {
      background: #3f3f3f;
      color: #e1e1e1;
      border: 1px solid #2b2b2b;
      padding: 5px 12px;
      min-height: 20px;
    }
    QTabBar::tab:selected {
      background: #2b2b2b;
      border-bottom-color: #2b2b2b;
    }
  )");
}

QRect to_qrect(Rect rect) {
  return QRect(rect.x, rect.y, rect.width, rect.height);
}

Rect to_core_rect(QRect rect) {
  rect = rect.normalized();
  return Rect{rect.x(), rect.y(), rect.width(), rect.height()};
}

Rect expand_rect(Rect rect, int amount) {
  if (rect.empty() || amount <= 0) {
    return rect;
  }
  return Rect{rect.x - amount, rect.y - amount, rect.width + amount * 2, rect.height + amount * 2};
}

int layer_style_render_padding(const LayerStyle& style) {
  if (!style.effects_visible || style.empty()) {
    return 0;
  }

  int padding = 0;
  constexpr double kRadiansPerDegree = 3.14159265358979323846 / 180.0;
  for (const auto& shadow : style.drop_shadows) {
    if (!shadow.enabled || shadow.opacity <= 0.0F) {
      continue;
    }
    const auto radians = (180.0 - static_cast<double>(shadow.angle_degrees)) * kRadiansPerDegree;
    const auto offset_x = static_cast<int>(std::lround(std::cos(radians) * shadow.distance));
    const auto offset_y = static_cast<int>(std::lround(std::sin(radians) * shadow.distance));
    const auto blur_radius = std::max(0, static_cast<int>(std::lround(shadow.size * 0.5F)));
    const auto spread_radius =
        std::max(0, static_cast<int>(std::lround(shadow.size * std::clamp(shadow.spread / 100.0F, 0.0F, 1.0F))));
    padding = std::max(padding, std::abs(offset_x) + std::abs(offset_y) + blur_radius * 3 + spread_radius + 2);
  }
  for (const auto& glow : style.outer_glows) {
    if (!glow.enabled || glow.opacity <= 0.0F || glow.size <= 0.0F) {
      continue;
    }
    const auto blur_radius = std::max(0, static_cast<int>(std::lround(glow.size * 0.5F)));
    padding = std::max(padding, blur_radius * 3 + 2);
  }
  for (const auto& stroke : style.strokes) {
    if (stroke.enabled && stroke.opacity > 0.0F && stroke.size > 0.0F) {
      padding = std::max(padding, std::max(1, static_cast<int>(std::ceil(stroke.size))) + 1);
    }
  }
  return padding;
}

Rect layer_render_bounds(const Layer& layer) {
  if (layer.kind() == LayerKind::Group) {
    Rect bounds;
    for (const auto& child : layer.children()) {
      bounds = unite_rect(bounds, layer_render_bounds(child));
    }
    return bounds;
  }
  return expand_rect(layer.bounds(), layer_style_render_padding(layer.layer_style()));
}

EditColor edit_color(QColor color) {
  return EditColor{static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()),
                   static_cast<std::uint8_t>(color.blue()), static_cast<std::uint8_t>(std::max(1, color.alpha()))};
}

EditOptions edit_options(CanvasWidget& canvas) {
  EditOptions options;
  options.primary = edit_color(canvas.primary_color());
  options.secondary = edit_color(canvas.secondary_color());
  options.brush_size = canvas.brush_size();
  if (canvas.selected_document_rect().has_value()) {
    options.selection = to_core_rect(*canvas.selected_document_rect());
    const auto region = canvas.selected_document_region();
    options.selection_mask = [region](std::int32_t x, std::int32_t y) { return region.contains(QPoint(x, y)); };
  }
  return options;
}

bool layer_locks_transparent_pixels(const Layer& layer) {
  const auto found = layer.metadata().find("photoslop.lock_transparent_pixels");
  return found != layer.metadata().end() && found->second == "true";
}

void set_layer_locks_transparent_pixels(Layer& layer, bool locked) {
  if (locked) {
    layer.metadata()["photoslop.lock_transparent_pixels"] = "true";
  } else {
    layer.metadata().erase("photoslop.lock_transparent_pixels");
  }
}

QRegion expanded_region(const QRegion& region, int pixels, QRect bounds) {
  if (region.isEmpty() || pixels <= 0) {
    return region.intersected(bounds);
  }
  pixels = std::clamp(pixels, 0, 250);
  QRegion expanded;
  for (int dy = -pixels; dy <= pixels; ++dy) {
    for (int dx = -pixels; dx <= pixels; ++dx) {
      expanded = expanded.united(region.translated(dx, dy));
    }
  }
  return expanded.intersected(bounds);
}

QRegion selection_outline_region(const QRegion& selection, int thickness, QRect bounds) {
  if (selection.isEmpty()) {
    return {};
  }
  QRegion outline;
  outline = outline.united(selection.subtracted(selection.translated(1, 0)));
  outline = outline.united(selection.subtracted(selection.translated(-1, 0)));
  outline = outline.united(selection.subtracted(selection.translated(0, 1)));
  outline = outline.united(selection.subtracted(selection.translated(0, -1)));
  return expanded_region(outline, std::max(0, thickness / 2), bounds);
}

Rect intersect_copy_rect(Rect a, Rect b) {
  const auto left = std::max(a.x, b.x);
  const auto top = std::max(a.y, b.y);
  const auto right = std::min(a.x + a.width, b.x + b.width);
  const auto bottom = std::min(a.y + a.height, b.y + b.height);
  return Rect{left, top, std::max(0, right - left), std::max(0, bottom - top)};
}

QImage image_from_pixels(const PixelBuffer& pixels) {
  QImage image(pixels.width(), pixels.height(), QImage::Format_RGBA8888);
  image.fill(Qt::transparent);
  if (pixels.empty() || pixels.format().bit_depth != BitDepth::UInt8 || pixels.format().channels < 3) {
    return image;
  }

  for (int y = 0; y < pixels.height(); ++y) {
    for (int x = 0; x < pixels.width(); ++x) {
      const auto* px = pixels.pixel(x, y);
      image.setPixelColor(x, y, QColor(px[0], px[1], px[2], pixels.format().channels >= 4 ? px[3] : 255));
    }
  }
  return image;
}

PixelBuffer pixels_from_image_rgba(const QImage& image) {
  const auto converted = image.convertToFormat(QImage::Format_RGBA8888);
  PixelBuffer pixels(converted.width(), converted.height(), PixelFormat::rgba8());
  for (int y = 0; y < converted.height(); ++y) {
    for (int x = 0; x < converted.width(); ++x) {
      const auto color = converted.pixelColor(x, y);
      auto* px = pixels.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(color.red());
      px[1] = static_cast<std::uint8_t>(color.green());
      px[2] = static_cast<std::uint8_t>(color.blue());
      px[3] = static_cast<std::uint8_t>(color.alpha());
    }
  }
  return pixels;
}

PixelBuffer copy_pixels_from_layer(const Layer& layer, Rect document_rect, const QRegion& selection = {}) {
  const auto& source = layer.pixels();
  PixelBuffer copied(document_rect.width, document_rect.height, PixelFormat::rgba8());
  copied.clear(0);
  if (source.empty() || source.format().bit_depth != BitDepth::UInt8 || source.format().channels < 3) {
    return copied;
  }

  const auto bounds = layer.bounds();
  for (std::int32_t y = 0; y < document_rect.height; ++y) {
    for (std::int32_t x = 0; x < document_rect.width; ++x) {
      const auto sx = document_rect.x + x - bounds.x;
      const auto sy = document_rect.y + y - bounds.y;
      if (!selection.isEmpty() && !selection.contains(QPoint(document_rect.x + x, document_rect.y + y))) {
        continue;
      }
      if (sx < 0 || sy < 0 || sx >= source.width() || sy >= source.height()) {
        continue;
      }
      const auto* src = source.pixel(sx, sy);
      auto* dst = copied.pixel(x, y);
      dst[0] = src[0];
      dst[1] = src[1];
      dst[2] = src[2];
      dst[3] = source.format().channels >= 4 ? src[3] : 255;
    }
  }
  return copied;
}

void apply_selection_mask(PixelBuffer& pixels, Rect document_rect, const QRegion& selection) {
  if (selection.isEmpty() || pixels.empty() || pixels.format().bit_depth != BitDepth::UInt8 ||
      pixels.format().channels < 4) {
    return;
  }

  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      if (!selection.contains(QPoint(document_rect.x + x, document_rect.y + y))) {
        pixels.pixel(x, y)[3] = 0;
      }
    }
  }
}

struct LayerCopyPixels {
  PixelBuffer pixels;
  QPoint origin;
  Rect document_rect;
  std::vector<LayerId> source_layer_ids;
};

bool has_visible_pixels(const PixelBuffer& pixels) {
  if (pixels.empty() || pixels.format().bit_depth != BitDepth::UInt8) {
    return false;
  }
  if (pixels.format().channels < 4) {
    return pixels.format().channels >= 3;
  }
  const auto channels = pixels.format().channels;
  for (std::size_t index = 3; index < pixels.data().size(); index += channels) {
    if (pixels.data()[index] != 0) {
      return true;
    }
  }
  return false;
}

std::optional<LayerCopyPixels> collect_layer_copy_pixels(const Document& document, const std::vector<LayerId>& ids,
                                                         const CanvasWidget& canvas) {
  if (ids.empty()) {
    return std::nullopt;
  }

  const std::set<LayerId> selected(ids.begin(), ids.end());
  std::vector<const Layer*> layers_to_copy;
  for (const auto& layer : document.layers()) {
    if (!selected.contains(layer.id()) || layer.kind() != LayerKind::Pixel || !layer.visible()) {
      continue;
    }
    layers_to_copy.push_back(&layer);
  }
  if (layers_to_copy.empty()) {
    return std::nullopt;
  }

  Rect copy_rect;
  if (canvas.selected_document_rect().has_value()) {
    copy_rect = to_core_rect(*canvas.selected_document_rect());
  } else {
    for (const auto* layer : layers_to_copy) {
      copy_rect = unite_rect(copy_rect, layer->bounds());
    }
  }
  copy_rect = intersect_copy_rect(copy_rect, Rect::from_size(document.width(), document.height()));
  if (copy_rect.empty()) {
    return std::nullopt;
  }

  PixelBuffer copied;
  if (layers_to_copy.size() == 1U) {
    copied = copy_pixels_from_layer(*layers_to_copy.front(), copy_rect, canvas.selected_document_region());
  } else {
    Document selected_document(document.width(), document.height(), document.format());
    for (const auto* layer : layers_to_copy) {
      selected_document.add_layer(*layer);
    }
    const auto image =
        qimage_from_document(selected_document, true).copy(QRect(copy_rect.x, copy_rect.y, copy_rect.width, copy_rect.height));
    copied = pixels_from_image_rgba(image);
    apply_selection_mask(copied, copy_rect, canvas.selected_document_region());
  }

  if (!has_visible_pixels(copied)) {
    return std::nullopt;
  }

  LayerCopyPixels payload{std::move(copied), QPoint(copy_rect.x, copy_rect.y), copy_rect, {}};
  payload.source_layer_ids.reserve(layers_to_copy.size());
  for (const auto* layer : layers_to_copy) {
    payload.source_layer_ids.push_back(layer->id());
  }
  return payload;
}

PixelBuffer scale_pixels_nearest(const PixelBuffer& source, std::int32_t width, std::int32_t height) {
  PixelBuffer scaled(width, height, source.format());
  if (source.empty() || width <= 0 || height <= 0) {
    return scaled;
  }

  const auto channels = source.format().channels;
  for (std::int32_t y = 0; y < height; ++y) {
    const auto sy = std::clamp(static_cast<std::int32_t>((static_cast<std::int64_t>(y) * source.height()) / height), 0,
                               source.height() - 1);
    for (std::int32_t x = 0; x < width; ++x) {
      const auto sx = std::clamp(static_cast<std::int32_t>((static_cast<std::int64_t>(x) * source.width()) / width), 0,
                                 source.width() - 1);
      const auto* src = source.pixel(sx, sy);
      auto* dst = scaled.pixel(x, y);
      std::copy(src, src + channels, dst);
    }
  }
  return scaled;
}

PixelBuffer render_text_pixels(const TextToolSettings& settings, QColor color, std::int32_t max_width) {
  QFont font(settings.family);
  font.setPixelSize(std::max(1, settings.size));
  font.setBold(settings.bold);
  font.setItalic(settings.italic);

  const auto text_width = std::max(64, max_width);
  QTextDocument document;
  document.setDocumentMargin(0);
  document.setDefaultFont(font);
  QTextOption option;
  option.setWrapMode(QTextOption::NoWrap);
  document.setDefaultTextOption(option);
  document.setPlainText(settings.text);
  document.setTextWidth(text_width);
  QTextCursor cursor(&document);
  cursor.select(QTextCursor::Document);
  QTextCharFormat format;
  format.setForeground(QBrush(color));
  cursor.mergeCharFormat(format);

  const auto size = document.size();
  QImage image(std::max(1, static_cast<int>(std::ceil(size.width())) + 2),
               std::max(1, static_cast<int>(std::ceil(size.height())) + 2), QImage::Format_RGBA8888);
  image.fill(Qt::transparent);

  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setRenderHint(QPainter::TextAntialiasing);
  document.drawContents(&painter, QRectF(0, 0, image.width(), image.height()));
  painter.end();
  return pixels_from_image_rgba(image);
}

QIcon tool_icon(CanvasTool tool) {
  QPixmap pixmap(32, 32);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  QPen pen(QColor(235, 238, 242), 2.4);
  painter.setPen(pen);
  painter.setBrush(Qt::NoBrush);

  switch (tool) {
    case CanvasTool::Move:
      painter.drawLine(16, 5, 16, 27);
      painter.drawLine(5, 16, 27, 16);
      painter.drawLine(16, 5, 12, 9);
      painter.drawLine(16, 5, 20, 9);
      painter.drawLine(27, 16, 23, 12);
      painter.drawLine(27, 16, 23, 20);
      break;
    case CanvasTool::Marquee:
      pen.setStyle(Qt::DashLine);
      painter.setPen(pen);
      painter.drawRect(QRect(7, 7, 18, 18));
      break;
    case CanvasTool::Lasso: {
      QPainterPath loop;
      loop.moveTo(8, 17);
      loop.cubicTo(8, 8, 22, 5, 26, 13);
      loop.cubicTo(30, 22, 17, 28, 10, 23);
      loop.cubicTo(5, 20, 5, 16, 8, 17);
      painter.setPen(QPen(QColor(235, 238, 242), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      painter.drawPath(loop);
      QPainterPath tail;
      tail.moveTo(13, 23);
      tail.cubicTo(15, 28, 21, 30, 27, 27);
      tail.moveTo(20, 25);
      tail.cubicTo(23, 26, 26, 28, 28, 30);
      painter.drawPath(tail);
      painter.setBrush(QColor(235, 238, 242));
      painter.drawEllipse(QPointF(13.5, 23.0), 1.7, 1.7);
      break;
    }
    case CanvasTool::MagicWand:
      painter.setPen(QPen(QColor(245, 248, 252), 3.0, Qt::SolidLine, Qt::RoundCap));
      painter.drawLine(8, 25, 21, 12);
      painter.setPen(QPen(QColor(80, 170, 255), 2.0, Qt::SolidLine, Qt::RoundCap));
      painter.drawLine(20, 5, 20, 10);
      painter.drawLine(20, 14, 20, 19);
      painter.drawLine(13, 12, 18, 12);
      painter.drawLine(22, 12, 27, 12);
      painter.drawLine(15, 7, 18, 10);
      painter.drawLine(22, 14, 25, 17);
      painter.setBrush(QColor(80, 170, 255));
      painter.setPen(Qt::NoPen);
      painter.drawEllipse(QPoint(8, 25), 3, 3);
      break;
    case CanvasTool::Brush:
      painter.save();
      painter.translate(16, 16);
      painter.rotate(-42);
      painter.setPen(Qt::NoPen);
      painter.setBrush(QColor(210, 150, 75));
      painter.drawRoundedRect(QRectF(-3.0, -13.0, 6.0, 17.0), 2.0, 2.0);
      painter.setBrush(QColor(235, 238, 242));
      painter.drawRoundedRect(QRectF(-4.0, 1.0, 8.0, 9.0), 2.0, 2.0);
      painter.setBrush(QColor(45, 150, 255));
      painter.drawPolygon(QPolygon({QPoint(-4, 9), QPoint(4, 9), QPoint(0, 15)}));
      painter.restore();
      break;
    case CanvasTool::Eraser:
      painter.setBrush(QColor(235, 238, 242));
      painter.drawPolygon(QPolygon({QPoint(8, 21), QPoint(18, 11), QPoint(25, 18), QPoint(15, 28)}));
      painter.setPen(QPen(QColor(36, 38, 41), 2));
      painter.drawLine(13, 16, 20, 23);
      break;
    case CanvasTool::Gradient: {
      QLinearGradient gradient(6, 24, 26, 8);
      gradient.setColorAt(0.0, QColor(245, 248, 252));
      gradient.setColorAt(1.0, QColor(45, 150, 255));
      painter.setPen(QPen(QBrush(gradient), 4));
      painter.drawLine(7, 23, 25, 9);
      painter.setPen(QPen(QColor(245, 248, 252), 1.5));
      painter.setBrush(QColor(45, 150, 255));
      painter.drawRect(QRect(19, 5, 8, 8));
      painter.setBrush(QColor(245, 248, 252));
      painter.drawRect(QRect(5, 20, 8, 8));
      break;
    }
    case CanvasTool::Fill:
      painter.drawPolygon(QPolygon({QPoint(10, 10), QPoint(21, 16), QPoint(14, 25), QPoint(6, 17)}));
      painter.setBrush(QColor(235, 238, 242));
      painter.drawEllipse(QPoint(24, 25), 3, 3);
      break;
    case CanvasTool::Line:
      painter.drawLine(8, 24, 24, 8);
      break;
    case CanvasTool::Rectangle:
      painter.drawRect(QRect(7, 9, 18, 14));
      break;
    case CanvasTool::Ellipse:
      painter.drawEllipse(QRect(7, 8, 18, 16));
      break;
    case CanvasTool::Eyedropper:
      painter.drawLine(11, 22, 23, 10);
      painter.drawRect(QRect(20, 7, 5, 5));
      painter.drawLine(8, 25, 13, 20);
      break;
    case CanvasTool::Text:
      painter.setFont(QFont(QStringLiteral("Arial"), 20, QFont::Bold));
      painter.drawText(pixmap.rect(), Qt::AlignCenter, QStringLiteral("T"));
      break;
    case CanvasTool::Pan: {
      QPainterPath path;
      path.moveTo(10, 24);
      path.lineTo(10, 12);
      path.quadTo(12, 9, 14, 12);
      path.lineTo(14, 18);
      path.lineTo(16, 10);
      path.quadTo(18, 8, 20, 11);
      path.lineTo(19, 18);
      path.lineTo(22, 13);
      path.quadTo(25, 12, 25, 16);
      path.lineTo(22, 26);
      path.closeSubpath();
      painter.drawPath(path);
      break;
    }
    case CanvasTool::Zoom:
      painter.setPen(QPen(QColor(235, 238, 242), 2.4, Qt::SolidLine, Qt::RoundCap));
      painter.drawEllipse(QRect(7, 7, 14, 14));
      painter.drawLine(18, 18, 26, 26);
      painter.drawLine(11, 14, 17, 14);
      painter.drawLine(14, 11, 14, 17);
      break;
  }
  return QIcon(pixmap);
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  register_builtin_filters(filters_);
  register_builtin_formats(formats_);

  document_tabs_ = new QTabWidget(this);
  document_tabs_->setObjectName(QStringLiteral("documentTabs"));
  document_tabs_->setDocumentMode(true);
  document_tabs_->setTabsClosable(true);
  document_tabs_->setMovable(true);
  setCentralWidget(document_tabs_);
  connect(document_tabs_, &QTabWidget::currentChanged, this, [this](int index) { activate_document_tab(index); });
  connect(document_tabs_, &QTabWidget::tabCloseRequested, this, [this](int index) { close_document_tab(index); });
  reset_document(1024, 768, Qt::white, tr("New document"));

  create_actions();
  load_recent_files();
  rebuild_recent_files_menu();
  load_bundled_legacy_plugins();
  create_docks();
  refresh_layer_list();
  refresh_layer_controls();
  update_undo_redo_actions();

  setWindowTitle(QStringLiteral("Photoslop"));
  resize(1280, 860);
  setStyleSheet(photoshop_style());
  statusBar()->showMessage(tr("Ready"));
}

void MainWindow::create_actions() {
  auto* file_menu = menuBar()->addMenu(tr("&File"));
  auto* edit_menu = menuBar()->addMenu(tr("&Edit"));
  auto* image_menu = menuBar()->addMenu(tr("&Image"));
  auto* layer_menu = menuBar()->addMenu(tr("&Layer"));
  auto* type_menu = menuBar()->addMenu(tr("&Type"));
  auto* select_menu = menuBar()->addMenu(tr("&Select"));
  auto* filter_menu = menuBar()->addMenu(tr("&Filter"));
  auto* plugins_menu = menuBar()->addMenu(tr("&Plugins"));
  auto* view_menu = menuBar()->addMenu(tr("&View"));
  auto* window_menu = menuBar()->addMenu(tr("&Window"));
  auto* help_menu = menuBar()->addMenu(tr("&Help"));

  auto* new_action = file_menu->addAction(tr("&New"));
  auto* open_action = file_menu->addAction(tr("&Open..."));
  recent_files_menu_ = file_menu->addMenu(tr("Open &Recent"));
  recent_files_menu_->setObjectName(QStringLiteral("fileOpenRecentMenu"));
  auto* save_action = file_menu->addAction(tr("&Save"));
  auto* save_as_action = file_menu->addAction(tr("Save &As..."));
  auto* export_flat_action = file_menu->addAction(tr("Export &Flat Image..."));
  file_menu->addSeparator();
  auto* quit_action = file_menu->addAction(tr("&Quit"));
  new_action->setObjectName(QStringLiteral("fileNewAction"));
  open_action->setObjectName(QStringLiteral("fileOpenAction"));
  save_action->setObjectName(QStringLiteral("fileSaveAction"));
  save_as_action->setObjectName(QStringLiteral("fileSaveAsAction"));
  export_flat_action->setObjectName(QStringLiteral("fileExportFlatAction"));
  new_action->setIcon(style()->standardIcon(QStyle::SP_FileIcon));
  open_action->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
  save_action->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
  save_as_action->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
  export_flat_action->setIcon(style()->standardIcon(QStyle::SP_DriveHDIcon));
  apply_action_shortcut(new_action, QKeySequence(Qt::CTRL | Qt::Key_N));
  apply_action_shortcut(open_action, QKeySequence(Qt::CTRL | Qt::Key_O));
  apply_action_shortcut(save_action, QKeySequence(Qt::CTRL | Qt::Key_S));
  apply_action_shortcut(save_as_action, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
  apply_action_shortcut(quit_action, QKeySequence(Qt::CTRL | Qt::Key_Q));

  connect(new_action, &QAction::triggered, this, [this] { create_new_document(); });
  connect(open_action, &QAction::triggered, this, [this] { open_document(); });
  connect(save_action, &QAction::triggered, this, [this] { save_document(); });
  connect(save_as_action, &QAction::triggered, this, [this] { save_document_as(); });
  connect(export_flat_action, &QAction::triggered, this, [this] { export_flat_image(); });
  connect(quit_action, &QAction::triggered, this, &QWidget::close);

  undo_action_ = edit_menu->addAction(tr("&Undo"));
  redo_action_ = edit_menu->addAction(tr("&Redo"));
  undo_action_->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));
  redo_action_->setIcon(style()->standardIcon(QStyle::SP_ArrowForward));
  apply_action_shortcut(undo_action_, QKeySequence(Qt::CTRL | Qt::Key_Z));
  apply_action_shortcut(redo_action_, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z));
  connect(undo_action_, &QAction::triggered, this, [this] { undo(); });
  connect(redo_action_, &QAction::triggered, this, [this] { redo(); });
  edit_menu->addSeparator();
  auto* cut_action = edit_menu->addAction(tr("Cu&t"));
  auto* copy_action = edit_menu->addAction(tr("&Copy"));
  auto* copy_merged_action = edit_menu->addAction(tr("Copy Merged"));
  auto* paste_action = edit_menu->addAction(tr("&Paste"));
  auto* transform_action = edit_menu->addAction(tr("Free &Transform..."));
  cut_action->setObjectName(QStringLiteral("editCutAction"));
  copy_action->setObjectName(QStringLiteral("editCopyAction"));
  copy_merged_action->setObjectName(QStringLiteral("editCopyMergedAction"));
  paste_action->setObjectName(QStringLiteral("editPasteAction"));
  transform_action->setObjectName(QStringLiteral("editFreeTransformAction"));
  cut_action->setIcon(simple_icon(QStringLiteral("CT")));
  copy_action->setIcon(simple_icon(QStringLiteral("CP")));
  copy_merged_action->setIcon(simple_icon(QStringLiteral("CM")));
  paste_action->setIcon(simple_icon(QStringLiteral("PS")));
  transform_action->setIcon(simple_icon(QStringLiteral("TR")));
  apply_action_shortcut(cut_action, QKeySequence(Qt::CTRL | Qt::Key_X));
  apply_action_shortcut(copy_action, QKeySequence(Qt::CTRL | Qt::Key_C));
  apply_action_shortcut(copy_merged_action, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C));
  apply_action_shortcut(paste_action, QKeySequence(Qt::CTRL | Qt::Key_V));
  apply_action_shortcut(transform_action, QKeySequence(Qt::CTRL | Qt::Key_T));
  connect(cut_action, &QAction::triggered, this, [this] { cut_selection(); });
  connect(copy_action, &QAction::triggered, this, [this] { copy_selection(); });
  connect(copy_merged_action, &QAction::triggered, this, [this] { copy_merged(); });
  connect(paste_action, &QAction::triggered, this, [this] { paste_clipboard(); });
  connect(transform_action, &QAction::triggered, this, [this] { transform_active_layer_dialog(); });
  edit_menu->addSeparator();
  auto* select_all_action = edit_menu->addAction(tr("Select &All"));
  auto* clear_selection_action = edit_menu->addAction(tr("&Clear Selection"));
  auto* reselect_action = edit_menu->addAction(tr("&Reselect"));
  auto* inverse_selection_action = edit_menu->addAction(tr("&Inverse"));
  auto* grow_selection_action = new QAction(tr("&Grow"), this);
  auto* similar_selection_action = new QAction(tr("Simi&lar"), this);
  auto* expand_selection_action = new QAction(tr("&Expand..."), this);
  auto* contract_selection_action = new QAction(tr("Con&tract..."), this);
  auto* border_selection_action = new QAction(tr("&Border..."), this);
  auto* layer_transparency_action = new QAction(tr("Load Layer &Transparency"), this);
  auto* stroke_selection_action = edit_menu->addAction(tr("&Stroke Selection"));
  select_all_action->setObjectName(QStringLiteral("editSelectAllAction"));
  clear_selection_action->setObjectName(QStringLiteral("editDeselectAction"));
  reselect_action->setObjectName(QStringLiteral("selectReselectAction"));
  inverse_selection_action->setObjectName(QStringLiteral("selectInverseAction"));
  grow_selection_action->setObjectName(QStringLiteral("selectGrowAction"));
  similar_selection_action->setObjectName(QStringLiteral("selectSimilarAction"));
  expand_selection_action->setObjectName(QStringLiteral("selectExpandAction"));
  contract_selection_action->setObjectName(QStringLiteral("selectContractAction"));
  border_selection_action->setObjectName(QStringLiteral("selectBorderAction"));
  layer_transparency_action->setObjectName(QStringLiteral("selectLayerTransparencyAction"));
  stroke_selection_action->setObjectName(QStringLiteral("editStrokeSelectionAction"));
  select_all_action->setIcon(simple_icon(QStringLiteral("SA")));
  clear_selection_action->setIcon(simple_icon(QStringLiteral("DS")));
  reselect_action->setIcon(simple_icon(QStringLiteral("RS")));
  inverse_selection_action->setIcon(simple_icon(QStringLiteral("INV")));
  grow_selection_action->setIcon(simple_icon(QStringLiteral("GR")));
  similar_selection_action->setIcon(simple_icon(QStringLiteral("SIM")));
  expand_selection_action->setIcon(simple_icon(QStringLiteral("EXP")));
  contract_selection_action->setIcon(simple_icon(QStringLiteral("CTR")));
  border_selection_action->setIcon(simple_icon(QStringLiteral("BD")));
  layer_transparency_action->setIcon(simple_icon(QStringLiteral("AL")));
  stroke_selection_action->setIcon(simple_icon(QStringLiteral("stroke")));
  apply_action_shortcut(select_all_action, QKeySequence(Qt::CTRL | Qt::Key_A));
  apply_action_shortcut(clear_selection_action, QKeySequence(Qt::CTRL | Qt::Key_D));
  apply_action_shortcut(reselect_action, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D));
  apply_action_shortcut(inverse_selection_action, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I));
  connect(select_all_action, &QAction::triggered, this, [this] { canvas_->select_all(); });
  connect(clear_selection_action, &QAction::triggered, this, [this] { canvas_->clear_selection(); });
  connect(reselect_action, &QAction::triggered, this, [this] { canvas_->reselect(); });
  connect(inverse_selection_action, &QAction::triggered, this, [this] { canvas_->invert_selection(); });
  connect(grow_selection_action, &QAction::triggered, this, [this] { canvas_->grow_selection(); });
  connect(similar_selection_action, &QAction::triggered, this, [this] { canvas_->select_similar_to_selection(); });
  connect(expand_selection_action, &QAction::triggered, this, [this] { expand_selection_dialog(); });
  connect(contract_selection_action, &QAction::triggered, this, [this] { contract_selection_dialog(); });
  connect(border_selection_action, &QAction::triggered, this, [this] { border_selection_dialog(); });
  connect(layer_transparency_action, &QAction::triggered, this, [this] { canvas_->select_active_layer_opaque_pixels(); });
  connect(stroke_selection_action, &QAction::triggered, this, [this] { stroke_selection(); });
  select_menu->addAction(select_all_action);
  select_menu->addAction(clear_selection_action);
  select_menu->addAction(reselect_action);
  select_menu->addAction(inverse_selection_action);
  select_menu->addAction(grow_selection_action);
  select_menu->addAction(similar_selection_action);
  select_menu->addAction(expand_selection_action);
  select_menu->addAction(contract_selection_action);
  select_menu->addAction(border_selection_action);
  select_menu->addAction(layer_transparency_action);
  select_menu->addSeparator();
  select_menu->addAction(stroke_selection_action);

  auto* add_layer_action = layer_menu->addAction(tr("&New Layer"));
  auto* add_folder_action = layer_menu->addAction(tr("New &Folder"));
  auto* layer_via_copy_action = layer_menu->addAction(tr("Layer Via &Copy"));
  auto* layer_via_cut_action = layer_menu->addAction(tr("Layer Via Cu&t"));
  layer_menu->addSeparator();
  layer_blending_options_action_ = layer_menu->addAction(tr("&Blending Options..."));
  layer_menu->addSeparator();
  auto* duplicate_layer_action = layer_menu->addAction(tr("&Duplicate Layer"));
  auto* merge_visible_action = layer_menu->addAction(tr("Merge &Visible to New Layer"));
  merge_visible_action->setObjectName(QStringLiteral("layerMergeVisibleAction"));
  auto* merge_selected_action = layer_menu->addAction(tr("&Merge Selected to New Layer"));
  merge_selected_action->setObjectName(QStringLiteral("layerMergeSelectedAction"));
  auto* rename_layer_action = layer_menu->addAction(tr("&Rename Layer..."));
  auto* delete_layer_action = layer_menu->addAction(tr("&Delete Layer"));
  layer_menu->addSeparator();
  auto* fill_layer_action = layer_menu->addAction(tr("&Fill Layer / Selection"));
  auto* fill_background_action = layer_menu->addAction(tr("Fill With &Background Color"));
  auto* clear_layer_action = layer_menu->addAction(tr("&Clear Layer / Selection"));
  layer_menu->addSeparator();
  auto* flip_h_action = layer_menu->addAction(tr("Flip Layer &Horizontal"));
  auto* flip_v_action = layer_menu->addAction(tr("Flip Layer &Vertical"));
  layer_menu->addSeparator();
  auto* layer_up_action = layer_menu->addAction(tr("Move Layer &Up"));
  auto* layer_down_action = layer_menu->addAction(tr("Move Layer &Down"));
  add_layer_action->setObjectName(QStringLiteral("layerNewAction"));
  add_folder_action->setObjectName(QStringLiteral("layerNewFolderAction"));
  layer_via_copy_action->setObjectName(QStringLiteral("layerViaCopyAction"));
  layer_via_cut_action->setObjectName(QStringLiteral("layerViaCutAction"));
  layer_blending_options_action_->setObjectName(QStringLiteral("layerBlendingOptionsAction"));
  duplicate_layer_action->setObjectName(QStringLiteral("layerDuplicateAction"));
  delete_layer_action->setObjectName(QStringLiteral("layerDeleteAction"));
  fill_layer_action->setObjectName(QStringLiteral("layerFillForegroundAction"));
  fill_background_action->setObjectName(QStringLiteral("layerFillBackgroundAction"));
  clear_layer_action->setObjectName(QStringLiteral("layerClearAction"));
  add_layer_action->setIcon(simple_icon(QStringLiteral("new")));
  add_folder_action->setIcon(simple_icon(QStringLiteral("dir"), QColor(245, 205, 105)));
  layer_via_copy_action->setIcon(simple_icon(QStringLiteral("copy")));
  layer_via_cut_action->setIcon(simple_icon(QStringLiteral("cut"), QColor(255, 185, 120)));
  layer_blending_options_action_->setIcon(simple_icon(QStringLiteral("fx"), QColor(170, 210, 255)));
  duplicate_layer_action->setIcon(simple_icon(QStringLiteral("dup")));
  merge_visible_action->setIcon(simple_icon(QStringLiteral("merge")));
  merge_selected_action->setIcon(simple_icon(QStringLiteral("merge"), QColor(160, 220, 255)));
  rename_layer_action->setIcon(simple_icon(QStringLiteral("RN")));
  delete_layer_action->setIcon(simple_icon(QStringLiteral("trash")));
  fill_layer_action->setIcon(simple_icon(QStringLiteral("fill")));
  fill_background_action->setIcon(simple_icon(QStringLiteral("fill"), QColor(160, 190, 255)));
  clear_layer_action->setIcon(simple_icon(QStringLiteral("clear")));
  flip_h_action->setIcon(simple_icon(QStringLiteral("FH")));
  flip_v_action->setIcon(simple_icon(QStringLiteral("FV")));
  layer_up_action->setIcon(style()->standardIcon(QStyle::SP_ArrowUp));
  layer_down_action->setIcon(style()->standardIcon(QStyle::SP_ArrowDown));
  apply_action_shortcut(add_layer_action, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N));
  apply_action_shortcut(layer_via_copy_action, QKeySequence(Qt::CTRL | Qt::Key_J));
  apply_action_shortcut(layer_via_cut_action, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_J));
  apply_action_shortcut(merge_visible_action, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));
  apply_action_shortcut(merge_selected_action, QKeySequence(Qt::CTRL | Qt::Key_E));
  apply_action_shortcut(fill_layer_action, QKeySequence(Qt::ALT | Qt::Key_Backspace));
  apply_action_shortcut(fill_background_action, QKeySequence(Qt::CTRL | Qt::Key_Backspace));
  apply_action_shortcut(clear_layer_action, QKeySequence(Qt::Key_Delete));
  connect(add_layer_action, &QAction::triggered, this, [this] { add_layer(); });
  connect(add_folder_action, &QAction::triggered, this, [this] { create_layer_folder(); });
  connect(layer_via_copy_action, &QAction::triggered, this, [this] { layer_via_copy(); });
  connect(layer_via_cut_action, &QAction::triggered, this, [this] { layer_via_cut(); });
  connect(layer_blending_options_action_, &QAction::triggered, this, [this] { edit_active_layer_style(); });
  connect(duplicate_layer_action, &QAction::triggered, this, [this] { duplicate_active_layer(); });
  connect(merge_visible_action, &QAction::triggered, this, [this] { merge_visible_to_new_layer(); });
  connect(merge_selected_action, &QAction::triggered, this, [this] { merge_selected_to_new_layer(); });
  connect(rename_layer_action, &QAction::triggered, this, [this] { rename_active_layer(); });
  connect(delete_layer_action, &QAction::triggered, this, [this] { delete_active_layer(); });
  connect(fill_layer_action, &QAction::triggered, this, [this] { fill_active_layer(); });
  connect(fill_background_action, &QAction::triggered, this, [this] {
    fill_active_layer_with_color(canvas_->secondary_color(), tr("Fill background"));
  });
  connect(clear_layer_action, &QAction::triggered, this, [this] { clear_active_layer(); });
  connect(flip_h_action, &QAction::triggered, this, [this] { flip_active_layer_horizontal(); });
  connect(flip_v_action, &QAction::triggered, this, [this] { flip_active_layer_vertical(); });
  connect(layer_up_action, &QAction::triggered, this, [this] { move_active_layer(1); });
  connect(layer_down_action, &QAction::triggered, this, [this] { move_active_layer(-1); });

  auto* adjustments_menu = image_menu->addMenu(tr("&Adjustments"));
  adjustments_menu->setObjectName(QStringLiteral("imageAdjustmentsMenu"));
  const auto add_adjustment_action = [this, adjustments_menu](const QString& label, const QString& object_name,
                                                              const QString& identifier,
                                                              const QKeySequence& shortcut = {}) {
    auto* action = adjustments_menu->addAction(label);
    action->setObjectName(object_name);
    action->setIcon(simple_icon(label.left(3).toUpper()));
    if (!shortcut.isEmpty()) {
      apply_action_shortcut(action, shortcut);
    }
    connect(action, &QAction::triggered, this, [this, identifier] { apply_filter(identifier); });
    return action;
  };
  add_adjustment_action(tr("&Invert"), QStringLiteral("imageAdjustInvertAction"),
                        QStringLiteral("photoslop.filters.invert"), QKeySequence(Qt::CTRL | Qt::Key_I));
  auto* levels_action = adjustments_menu->addAction(tr("&Levels..."));
  levels_action->setObjectName(QStringLiteral("imageAdjustLevelsAction"));
  levels_action->setIcon(simple_icon(QStringLiteral("LVL")));
  apply_action_shortcut(levels_action, QKeySequence(Qt::CTRL | Qt::Key_L));
  connect(levels_action, &QAction::triggered, this, [this] { levels_dialog(); });
  auto* curves_action = adjustments_menu->addAction(tr("&Curves..."));
  curves_action->setObjectName(QStringLiteral("imageAdjustCurvesAction"));
  curves_action->setIcon(simple_icon(QStringLiteral("CRV")));
  apply_action_shortcut(curves_action, QKeySequence(Qt::CTRL | Qt::Key_M));
  connect(curves_action, &QAction::triggered, this, [this] { curves_dialog(); });
  auto* hue_saturation_action = adjustments_menu->addAction(tr("&Hue/Saturation..."));
  hue_saturation_action->setObjectName(QStringLiteral("imageAdjustHueSaturationAction"));
  hue_saturation_action->setIcon(simple_icon(QStringLiteral("HSL")));
  apply_action_shortcut(hue_saturation_action, QKeySequence(Qt::CTRL | Qt::Key_U));
  connect(hue_saturation_action, &QAction::triggered, this, [this] { hue_saturation_dialog(); });
  auto* color_balance_action = adjustments_menu->addAction(tr("Color &Balance..."));
  color_balance_action->setObjectName(QStringLiteral("imageAdjustColorBalanceAction"));
  color_balance_action->setIcon(simple_icon(QStringLiteral("CB")));
  apply_action_shortcut(color_balance_action, QKeySequence(Qt::CTRL | Qt::Key_B));
  connect(color_balance_action, &QAction::triggered, this, [this] { color_balance_dialog(); });
  add_adjustment_action(tr("&Desaturate"), QStringLiteral("imageAdjustDesaturateAction"),
                        QStringLiteral("photoslop.filters.desaturate"),
                        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_U));
  add_adjustment_action(tr("Auto &Contrast"), QStringLiteral("imageAdjustAutoContrastAction"),
                        QStringLiteral("photoslop.filters.auto_contrast"),
                        QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_L));
  adjustments_menu->addSeparator();
  add_adjustment_action(tr("&Brightness +24"), QStringLiteral("imageAdjustBrightnessAction"),
                        QStringLiteral("photoslop.filters.brightness_plus"));
  add_adjustment_action(tr("Contrast +25%"), QStringLiteral("imageAdjustContrastAction"),
                        QStringLiteral("photoslop.filters.contrast_plus"));
  add_adjustment_action(tr("&Threshold"), QStringLiteral("imageAdjustThresholdAction"),
                        QStringLiteral("photoslop.filters.threshold"));
  add_adjustment_action(tr("&Posterize"), QStringLiteral("imageAdjustPosterizeAction"),
                        QStringLiteral("photoslop.filters.posterize"));
  image_menu->addSeparator();

  auto* canvas_size_action = image_menu->addAction(tr("&Canvas Size..."));
  canvas_size_action->setObjectName(QStringLiteral("imageCanvasSizeAction"));
  auto* crop_action = image_menu->addAction(tr("&Crop to Selection"));
  crop_action->setObjectName(QStringLiteral("imageCropToSelectionAction"));
  image_menu->addSeparator();
  auto* rotate_cw_action = image_menu->addAction(tr("Rotate 90 &Clockwise"));
  auto* rotate_ccw_action = image_menu->addAction(tr("Rotate 90 Counterclockwise"));
  rotate_cw_action->setObjectName(QStringLiteral("imageRotateClockwiseAction"));
  rotate_ccw_action->setObjectName(QStringLiteral("imageRotateCounterclockwiseAction"));
  canvas_size_action->setIcon(simple_icon(QStringLiteral("CS")));
  crop_action->setIcon(simple_icon(QStringLiteral("crop")));
  rotate_cw_action->setIcon(simple_icon(QStringLiteral("rotate")));
  rotate_ccw_action->setIcon(simple_icon(QStringLiteral("rotate")));
  apply_action_shortcut(canvas_size_action, QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_C));
  apply_action_shortcut(crop_action, QKeySequence(Qt::Key_C));
  apply_action_shortcut(rotate_cw_action, QKeySequence(Qt::CTRL | Qt::Key_BracketRight));
  apply_action_shortcut(rotate_ccw_action, QKeySequence(Qt::CTRL | Qt::Key_BracketLeft));
  connect(canvas_size_action, &QAction::triggered, this, [this] { resize_canvas_dialog(); });
  connect(crop_action, &QAction::triggered, this, [this] { crop_to_selection(); });
  connect(rotate_cw_action, &QAction::triggered, this, [this] { rotate_canvas_clockwise(); });
  connect(rotate_ccw_action, &QAction::triggered, this, [this] { rotate_canvas_counterclockwise(); });

  for (const auto& filter : filters_.filters()) {
    auto* action = filter_menu->addAction(QString::fromStdString(filter.display_name));
    const auto identifier = QString::fromStdString(filter.identifier);
    connect(action, &QAction::triggered, this, [this, identifier] { apply_filter(identifier); });
  }

  auto* scan_legacy_plugins_action = plugins_menu->addAction(tr("&Scan Legacy Photoshop Plug-ins..."));
  scan_legacy_plugins_action->setObjectName(QStringLiteral("pluginsScanLegacyAction"));
  scan_legacy_plugins_action->setIcon(simple_icon(QStringLiteral("8BF")));
  connect(scan_legacy_plugins_action, &QAction::triggered, this, [this] { scan_legacy_plugins(); });
  legacy_plugins_menu_ = plugins_menu->addMenu(tr("Legacy Photoshop Plug-ins"));
  legacy_plugins_menu_->setObjectName(QStringLiteral("legacyPluginsMenu"));

  auto* zoom_in = view_menu->addAction(tr("Zoom &In"));
  auto* zoom_out = view_menu->addAction(tr("Zoom &Out"));
  auto* fit_on_screen = view_menu->addAction(tr("&Fit on Screen"));
  auto* zoom_reset = view_menu->addAction(tr("&Actual Pixels"));
  auto* selection_edges_action = view_menu->addAction(tr("Show Selection &Edges"));
  zoom_in->setObjectName(QStringLiteral("viewZoomInAction"));
  zoom_out->setObjectName(QStringLiteral("viewZoomOutAction"));
  fit_on_screen->setObjectName(QStringLiteral("viewFitOnScreenAction"));
  zoom_reset->setObjectName(QStringLiteral("viewActualPixelsAction"));
  selection_edges_action->setObjectName(QStringLiteral("viewToggleSelectionEdgesAction"));
  zoom_in->setIcon(simple_icon(QStringLiteral("zoomIn")));
  zoom_out->setIcon(simple_icon(QStringLiteral("zoomOut")));
  fit_on_screen->setIcon(simple_icon(QStringLiteral("fit")));
  zoom_reset->setIcon(simple_icon(QStringLiteral("1x")));
  selection_edges_action->setIcon(simple_icon(QStringLiteral("SE")));
  zoom_in->setShortcuts({QKeySequence::ZoomIn, QKeySequence(Qt::CTRL | Qt::Key_Equal)});
  zoom_out->setShortcut(QKeySequence::ZoomOut);
  zoom_in->setShortcutContext(Qt::ApplicationShortcut);
  zoom_out->setShortcutContext(Qt::ApplicationShortcut);
  refresh_action_tooltip(zoom_in);
  refresh_action_tooltip(zoom_out);
  apply_action_shortcut(fit_on_screen, QKeySequence(Qt::CTRL | Qt::Key_0));
  apply_action_shortcut(zoom_reset, QKeySequence(Qt::CTRL | Qt::Key_1));
  apply_action_shortcut(selection_edges_action, QKeySequence(Qt::CTRL | Qt::Key_H));
  connect(zoom_in, &QAction::triggered, this, [this] { canvas_->set_zoom(canvas_->zoom() * 1.25); });
  connect(zoom_out, &QAction::triggered, this, [this] { canvas_->set_zoom(canvas_->zoom() * 0.8); });
  connect(fit_on_screen, &QAction::triggered, this, [this] { canvas_->fit_to_view(); });
  connect(zoom_reset, &QAction::triggered, this, [this] { canvas_->set_zoom(1.0); });
  connect(selection_edges_action, &QAction::triggered, this, [this] {
    if (canvas_ != nullptr) {
      canvas_->toggle_selection_edges_visible();
    }
  });

  auto* about_action = help_menu->addAction(tr("&About Photoslop"));
  connect(about_action, &QAction::triggered, this, [this] { show_about(); });

  auto* tool_palette = new QToolBar(tr("Tool Palette"), this);
  tool_palette->setObjectName(QStringLiteral("toolPalette"));
  tool_palette->setOrientation(Qt::Vertical);
  tool_palette->setMovable(false);
  tool_palette->setFloatable(false);
  tool_palette->setAllowedAreas(Qt::LeftToolBarArea);
  tool_palette->setToolButtonStyle(Qt::ToolButtonIconOnly);
  tool_palette->setIconSize(QSize(20, 20));
  tool_palette->setFixedWidth(43);
  addToolBar(Qt::LeftToolBarArea, tool_palette);

  auto* tool_group = new QActionGroup(this);
  tool_group->setExclusive(true);
  move_tool_action_ = add_tool_action(tool_palette, tool_group, tr("Move"), CanvasTool::Move, QKeySequence(Qt::Key_V));
  add_tool_action(tool_palette, tool_group, tr("Marquee"), CanvasTool::Marquee, QKeySequence(Qt::Key_M));
  add_tool_action(tool_palette, tool_group, tr("Lasso"), CanvasTool::Lasso, QKeySequence(Qt::Key_L));
  add_tool_action(tool_palette, tool_group, tr("Magic Wand"), CanvasTool::MagicWand, QKeySequence(Qt::Key_W));
  add_tool_action(tool_palette, tool_group, tr("Brush"), CanvasTool::Brush, QKeySequence(Qt::Key_B))->setChecked(true);
  add_tool_action(tool_palette, tool_group, tr("Eraser"), CanvasTool::Eraser, QKeySequence(Qt::Key_E));
  add_tool_action(tool_palette, tool_group, tr("Gradient"), CanvasTool::Gradient, QKeySequence(Qt::Key_G));
  add_tool_action(tool_palette, tool_group, tr("Fill"), CanvasTool::Fill, QKeySequence(Qt::SHIFT | Qt::Key_G));
  auto* shape_menu = new QMenu(tr("Shape Tools"), tool_palette);
  shape_menu->setObjectName(QStringLiteral("shapeToolMenu"));
  const auto create_shape_action = [this, tool_group, shape_menu](const QString& label, CanvasTool tool,
                                                                  QKeySequence shortcut) {
    auto* action = new QAction(label, this);
    action->setIcon(tool_icon(tool));
    action->setCheckable(true);
    action->setData(static_cast<int>(tool));
    apply_action_shortcut(action, shortcut);
    tool_group->addAction(action);
    shape_menu->addAction(action);
    addAction(action);
    return action;
  };
  auto* line_tool_action =
      create_shape_action(tr("Line"), CanvasTool::Line, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_U));
  auto* rect_tool_action = create_shape_action(tr("Rect"), CanvasTool::Rectangle, QKeySequence(Qt::Key_U));
  auto* ellipse_tool_action =
      create_shape_action(tr("Ellipse"), CanvasTool::Ellipse, QKeySequence(Qt::SHIFT | Qt::Key_U));
  auto* shape_tool_button = new QToolButton(tool_palette);
  shape_tool_button->setObjectName(QStringLiteral("shapeToolButton"));
  shape_tool_button->setToolButtonStyle(Qt::ToolButtonIconOnly);
  shape_tool_button->setPopupMode(QToolButton::DelayedPopup);
  shape_tool_button->setMenu(shape_menu);
  shape_tool_button->setDefaultAction(rect_tool_action);
  shape_tool_button->setToolTip(rect_tool_action->toolTip());
  tool_palette->addWidget(shape_tool_button);
  for (auto* action : {line_tool_action, rect_tool_action, ellipse_tool_action}) {
    connect(action, &QAction::triggered, shape_tool_button, [shape_tool_button, shape_menu, action] {
      shape_tool_button->setDefaultAction(action);
      shape_tool_button->setMenu(shape_menu);
      shape_tool_button->setToolTip(action->toolTip());
    });
  }
  add_tool_action(tool_palette, tool_group, tr("Pick"), CanvasTool::Eyedropper, QKeySequence(Qt::Key_I));
  auto* type_tool_action =
      add_tool_action(tool_palette, tool_group, tr("Type"), CanvasTool::Text, QKeySequence(Qt::Key_T));
  add_tool_action(tool_palette, tool_group, tr("Hand"), CanvasTool::Pan, QKeySequence(Qt::Key_H));
  auto* zoom_tool_action = add_tool_action(tool_palette, tool_group, tr("Zoom"), CanvasTool::Zoom, QKeySequence(Qt::Key_Z));
  if (auto* zoom_button = qobject_cast<QToolButton*>(tool_palette->widgetForAction(zoom_tool_action));
      zoom_button != nullptr) {
    zoom_button->setObjectName(QStringLiteral("zoomToolButton"));
    zoom_button->installEventFilter(new MouseDoubleClickFilter(
        [this] {
          if (canvas_ != nullptr) {
            canvas_->set_zoom(1.0);
            refresh_document_info();
            statusBar()->showMessage(tr("Actual Pixels"));
          }
        },
        zoom_button));
  }
  connect(tool_group, &QActionGroup::triggered, this, [this](QAction* action) {
    const auto selected = static_cast<CanvasTool>(action->data().toInt());
    if (selected != CanvasTool::Text) {
      finish_active_text_editor();
    }
    current_tool_ = selected;
    canvas_->set_tool(selected);
    refresh_options_bar();
    statusBar()->showMessage(tool_name(selected));
  });
  type_menu->addAction(type_tool_action);

  auto* palette_spacer = new QWidget(tool_palette);
  palette_spacer->setObjectName(QStringLiteral("toolPaletteSpacer"));
  palette_spacer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
  tool_palette->addWidget(palette_spacer);
  tool_palette->addSeparator();
  auto* default_colors_action = tool_palette->addAction(tr("Default Colors"));
  auto* swap_colors_action = tool_palette->addAction(tr("Swap Colors"));
  default_colors_action->setObjectName(QStringLiteral("colorDefaultAction"));
  swap_colors_action->setObjectName(QStringLiteral("colorSwapAction"));
  default_colors_action->setIcon(simple_icon(QStringLiteral("D")));
  swap_colors_action->setIcon(simple_icon(QStringLiteral("X")));
  apply_action_shortcut(default_colors_action, QKeySequence(Qt::Key_D));
  apply_action_shortcut(swap_colors_action, QKeySequence(Qt::Key_X));
  primary_color_button_ = new QPushButton(tr("FG"), tool_palette);
  secondary_color_button_ = new QPushButton(tr("BG"), tool_palette);
  primary_color_button_->setObjectName(QStringLiteral("foregroundColorButton"));
  secondary_color_button_->setObjectName(QStringLiteral("backgroundColorButton"));
  primary_color_button_->setToolTip(tr("Foreground color"));
  secondary_color_button_->setToolTip(tr("Background color"));
  tool_palette->addWidget(primary_color_button_);
  tool_palette->addWidget(secondary_color_button_);
  connect(primary_color_button_, &QPushButton::clicked, this, [this] { choose_primary_color(); });
  connect(secondary_color_button_, &QPushButton::clicked, this, [this] { choose_secondary_color(); });
  connect(swap_colors_action, &QAction::triggered, this, [this] { swap_colors(); });
  connect(default_colors_action, &QAction::triggered, this, [this] { default_colors(); });

  auto* toolbar = new QToolBar(tr("Options"), this);
  toolbar->setObjectName(QStringLiteral("Options"));
  toolbar->setMovable(false);
  toolbar->setFloatable(false);
  toolbar->setAllowedAreas(Qt::TopToolBarArea);
  toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
  toolbar->setIconSize(QSize(18, 18));
  addToolBar(Qt::TopToolBarArea, toolbar);
  option_actions_.clear();
  const auto add_option_separator = [this, toolbar](std::initializer_list<CanvasTool> tools) {
    register_option_action(toolbar->addSeparator(), tools);
  };
  const auto add_option_action = [this, toolbar](const QIcon& icon, const QString& text,
                                                 std::initializer_list<CanvasTool> tools) {
    auto* action = toolbar->addAction(icon, text);
    register_option_action(action, tools);
    return action;
  };
  const auto add_option_widget = [this, toolbar](QWidget* widget, std::initializer_list<CanvasTool> tools) {
    auto* action = toolbar->addWidget(widget);
    register_option_action(action, tools);
    return action;
  };
  const auto add_option_label = [toolbar, add_option_widget](const QString& text,
                                                             std::initializer_list<CanvasTool> tools) {
    auto* label = new QLabel(text, toolbar);
    label->setProperty("optionLabel", true);
    label->setAlignment(Qt::AlignVCenter);
    return add_option_widget(label, tools);
  };

  move_auto_select_check_ = new CheckGlyphBox(tr("Auto-Select"), toolbar);
  move_auto_select_check_->setObjectName(QStringLiteral("moveAutoSelectCheck"));
  move_auto_select_check_->setToolTip(tr("Automatically select the clicked layer while using Move"));
  move_auto_select_check_->setChecked(canvas_->auto_select_layer());
  add_option_widget(move_auto_select_check_, {CanvasTool::Move});
  connect(move_auto_select_check_, &QCheckBox::toggled, this, [this](bool checked) {
    if (canvas_ != nullptr) {
      canvas_->set_auto_select_layer(checked);
    }
  });

  auto* selection_new = add_option_action(simple_icon(QStringLiteral("N")), tr("New Selection"),
                                          {CanvasTool::Marquee, CanvasTool::Lasso, CanvasTool::MagicWand});
  selection_new->setObjectName(QStringLiteral("selectionNewModeAction"));
  auto* selection_add = add_option_action(simple_icon(QStringLiteral("+")), tr("Add to Selection"),
                                          {CanvasTool::Marquee, CanvasTool::Lasso, CanvasTool::MagicWand});
  selection_add->setObjectName(QStringLiteral("selectionAddModeAction"));
  auto* selection_subtract = add_option_action(simple_icon(QStringLiteral("-")), tr("Subtract from Selection"),
                                               {CanvasTool::Marquee, CanvasTool::Lasso, CanvasTool::MagicWand});
  selection_subtract->setObjectName(QStringLiteral("selectionSubtractModeAction"));
  auto* selection_intersect = add_option_action(simple_icon(QStringLiteral("Ix")), tr("Intersect Selection"),
                                                {CanvasTool::Marquee, CanvasTool::Lasso, CanvasTool::MagicWand});
  selection_intersect->setObjectName(QStringLiteral("selectionIntersectModeAction"));
  selection_new_mode_action_ = selection_new;
  selection_add_mode_action_ = selection_add;
  selection_subtract_mode_action_ = selection_subtract;
  selection_intersect_mode_action_ = selection_intersect;
  auto* selection_mode_group = new QActionGroup(this);
  selection_mode_group->setExclusive(true);
  const auto configure_selection_mode_action = [selection_mode_group](QAction* action) {
    action->setCheckable(true);
    selection_mode_group->addAction(action);
  };
  configure_selection_mode_action(selection_new);
  configure_selection_mode_action(selection_add);
  configure_selection_mode_action(selection_subtract);
  configure_selection_mode_action(selection_intersect);
  selection_new->setChecked(true);
  const auto set_selection_mode = [this](CanvasWidget::SelectionMode mode) {
    current_selection_mode_ = mode;
    if (canvas_ != nullptr) {
      canvas_->set_selection_mode(mode);
    }
    refresh_options_bar();
  };
  connect(selection_new, &QAction::triggered, this,
          [set_selection_mode] { set_selection_mode(CanvasWidget::SelectionMode::Replace); });
  connect(selection_add, &QAction::triggered, this,
          [set_selection_mode] { set_selection_mode(CanvasWidget::SelectionMode::Add); });
  connect(selection_subtract, &QAction::triggered, this,
          [set_selection_mode] { set_selection_mode(CanvasWidget::SelectionMode::Subtract); });
  connect(selection_intersect, &QAction::triggered, this,
          [set_selection_mode] { set_selection_mode(CanvasWidget::SelectionMode::Intersect); });
  add_option_separator({CanvasTool::Marquee, CanvasTool::Lasso, CanvasTool::MagicWand});

  auto* feather_group = new QWidget(toolbar);
  feather_group->setObjectName(QStringLiteral("selectionFeatherGroup"));
  auto* feather_layout = new QHBoxLayout(feather_group);
  feather_layout->setContentsMargins(0, 0, 0, 0);
  feather_layout->setSpacing(0);
  auto* feather_label = new QLabel(tr("Feather:"), feather_group);
  feather_label->setAlignment(Qt::AlignCenter);
  feather_layout->addWidget(feather_label);
  auto* feather = new QSpinBox(feather_group);
  feather->setObjectName(QStringLiteral("selectionFeatherSpin"));
  feather->setRange(0, 250);
  feather->setSuffix(QStringLiteral(" px"));
  configure_toolbar_spinbox(feather, 64);
  feather_layout->addWidget(feather);
  add_option_widget(feather_group, {CanvasTool::Marquee, CanvasTool::Lasso, CanvasTool::MagicWand});
  auto* anti_alias = new CheckGlyphBox(tr("Anti-alias"), toolbar);
  anti_alias->setObjectName(QStringLiteral("selectionAntiAliasCheck"));
  anti_alias->setChecked(true);
  add_option_widget(anti_alias, {CanvasTool::Marquee, CanvasTool::Lasso, CanvasTool::MagicWand});
  add_option_label(tr("Style:"), {CanvasTool::Marquee});
  auto* style_combo = new QComboBox(toolbar);
  style_combo->setObjectName(QStringLiteral("selectionStyleCombo"));
  style_combo->addItems({tr("Normal"), tr("Fixed Ratio"), tr("Fixed Size")});
  style_combo->setCurrentText(tr("Normal"));
  style_combo->setFixedWidth(92);
  add_option_widget(style_combo, {CanvasTool::Marquee});
  add_option_label(tr("Width:"), {CanvasTool::Marquee});
  auto* fixed_width = new QSpinBox(toolbar);
  fixed_width->setObjectName(QStringLiteral("selectionFixedWidthSpin"));
  fixed_width->setRange(1, 30000);
  fixed_width->setValue(document().width());
  fixed_width->setSuffix(QStringLiteral(" px"));
  configure_toolbar_spinbox(fixed_width, 78);
  add_option_widget(fixed_width, {CanvasTool::Marquee});
  add_option_label(tr("Height:"), {CanvasTool::Marquee});
  auto* fixed_height = new QSpinBox(toolbar);
  fixed_height->setObjectName(QStringLiteral("selectionFixedHeightSpin"));
  fixed_height->setRange(1, 30000);
  fixed_height->setValue(document().height());
  fixed_height->setSuffix(QStringLiteral(" px"));
  configure_toolbar_spinbox(fixed_height, 78);
  add_option_widget(fixed_height, {CanvasTool::Marquee});
  const auto apply_marquee_settings = [this, style_combo, fixed_width, fixed_height] {
    switch (style_combo->currentIndex()) {
      case 1:
        current_marquee_style_ = CanvasWidget::MarqueeStyle::FixedRatio;
        break;
      case 2:
        current_marquee_style_ = CanvasWidget::MarqueeStyle::FixedSize;
        break;
      default:
        current_marquee_style_ = CanvasWidget::MarqueeStyle::Normal;
        break;
    }
    current_marquee_width_ = fixed_width->value();
    current_marquee_height_ = fixed_height->value();
    if (canvas_ != nullptr) {
      canvas_->set_marquee_style(current_marquee_style_);
      canvas_->set_marquee_fixed_size(current_marquee_width_, current_marquee_height_);
    }
  };
  connect(style_combo, &QComboBox::currentIndexChanged, this, [apply_marquee_settings](int) {
    apply_marquee_settings();
  });
  connect(fixed_width, &QSpinBox::valueChanged, this, [apply_marquee_settings](int) {
    apply_marquee_settings();
  });
  connect(fixed_height, &QSpinBox::valueChanged, this, [apply_marquee_settings](int) {
    apply_marquee_settings();
  });
  add_option_separator({CanvasTool::Marquee, CanvasTool::Lasso, CanvasTool::MagicWand});

  add_option_label(tr("Size:"), {CanvasTool::Brush, CanvasTool::Eraser, CanvasTool::Line, CanvasTool::Rectangle,
                                  CanvasTool::Ellipse});
  auto* brush_size = new QSpinBox(toolbar);
  brush_size->setObjectName(QStringLiteral("brushSizeSpin"));
  brush_size->setRange(1, 256);
  brush_size->setValue(canvas_->brush_size());
  configure_toolbar_spinbox(brush_size, 46);
  add_option_widget(brush_size,
                    {CanvasTool::Brush, CanvasTool::Eraser, CanvasTool::Line, CanvasTool::Rectangle,
                     CanvasTool::Ellipse});
  auto* brush_size_slider = new QSlider(Qt::Horizontal, toolbar);
  brush_size_slider->setObjectName(QStringLiteral("brushSizeSlider"));
  brush_size_slider->setRange(1, 256);
  brush_size_slider->setValue(canvas_->brush_size());
  brush_size_slider->setFixedWidth(150);
  brush_size_slider->setToolTip(tr("Brush size"));
  add_option_widget(brush_size_slider,
                    {CanvasTool::Brush, CanvasTool::Eraser, CanvasTool::Line, CanvasTool::Rectangle,
                     CanvasTool::Ellipse});
  add_option_label(tr("Opacity:"), {CanvasTool::Brush, CanvasTool::Eraser, CanvasTool::Line, CanvasTool::Rectangle,
                                     CanvasTool::Ellipse});
  auto* brush_opacity = new QSpinBox(toolbar);
  brush_opacity->setObjectName(QStringLiteral("brushOpacitySpin"));
  brush_opacity->setRange(1, 100);
  brush_opacity->setValue(canvas_->brush_opacity());
  brush_opacity->setSuffix(QStringLiteral("%"));
  configure_toolbar_spinbox(brush_opacity, 52);
  add_option_widget(brush_opacity,
                    {CanvasTool::Brush, CanvasTool::Eraser, CanvasTool::Line, CanvasTool::Rectangle,
                     CanvasTool::Ellipse});
  auto* brush_opacity_slider = new QSlider(Qt::Horizontal, toolbar);
  brush_opacity_slider->setObjectName(QStringLiteral("brushOpacitySlider"));
  brush_opacity_slider->setRange(1, 100);
  brush_opacity_slider->setValue(canvas_->brush_opacity());
  brush_opacity_slider->setFixedWidth(120);
  brush_opacity_slider->setToolTip(tr("Brush opacity"));
  add_option_widget(brush_opacity_slider,
                    {CanvasTool::Brush, CanvasTool::Eraser, CanvasTool::Line, CanvasTool::Rectangle,
                     CanvasTool::Ellipse});
  connect(brush_size, &QSpinBox::valueChanged, brush_size_slider, &QSlider::setValue);
  connect(brush_size_slider, &QSlider::valueChanged, brush_size, &QSpinBox::setValue);
  connect(brush_size, &QSpinBox::valueChanged, this, [this](int value) { canvas_->set_brush_size(value); });
  connect(brush_opacity, &QSpinBox::valueChanged, brush_opacity_slider, &QSlider::setValue);
  connect(brush_opacity_slider, &QSlider::valueChanged, brush_opacity, &QSpinBox::setValue);
  connect(brush_opacity, &QSpinBox::valueChanged, this, [this](int value) { canvas_->set_brush_opacity(value); });
  auto* brush_smaller_action = new QAction(tr("Brush Smaller"), this);
  auto* brush_larger_action = new QAction(tr("Brush Larger"), this);
  auto* brush_much_smaller_action = new QAction(tr("Brush Much Smaller"), this);
  auto* brush_much_larger_action = new QAction(tr("Brush Much Larger"), this);
  brush_smaller_action->setObjectName(QStringLiteral("brushSmallerAction"));
  brush_larger_action->setObjectName(QStringLiteral("brushLargerAction"));
  brush_much_smaller_action->setObjectName(QStringLiteral("brushMuchSmallerAction"));
  brush_much_larger_action->setObjectName(QStringLiteral("brushMuchLargerAction"));
  apply_action_shortcut(brush_smaller_action, QKeySequence(Qt::Key_BracketLeft));
  apply_action_shortcut(brush_larger_action, QKeySequence(Qt::Key_BracketRight));
  apply_action_shortcut(brush_much_smaller_action, QKeySequence(Qt::SHIFT | Qt::Key_BracketLeft));
  apply_action_shortcut(brush_much_larger_action, QKeySequence(Qt::SHIFT | Qt::Key_BracketRight));
  addAction(brush_smaller_action);
  addAction(brush_larger_action);
  addAction(brush_much_smaller_action);
  addAction(brush_much_larger_action);
  connect(brush_smaller_action, &QAction::triggered, brush_size,
          [brush_size] { brush_size->setValue(std::max(1, brush_size->value() - 1)); });
  connect(brush_larger_action, &QAction::triggered, brush_size,
          [brush_size] { brush_size->setValue(std::min(256, brush_size->value() + 1)); });
  connect(brush_much_smaller_action, &QAction::triggered, brush_size,
          [brush_size] { brush_size->setValue(std::max(1, brush_size->value() - 10)); });
  connect(brush_much_larger_action, &QAction::triggered, brush_size,
          [brush_size] { brush_size->setValue(std::min(256, brush_size->value() + 10)); });

  add_option_label(tr("Tol:"), {CanvasTool::MagicWand});
  auto* wand_tolerance = new QSpinBox(toolbar);
  wand_tolerance->setObjectName(QStringLiteral("wandToleranceSpin"));
  wand_tolerance->setRange(0, 255);
  wand_tolerance->setValue(canvas_->wand_tolerance());
  configure_toolbar_spinbox(wand_tolerance, 46);
  add_option_widget(wand_tolerance, {CanvasTool::MagicWand});
  connect(wand_tolerance, &QSpinBox::valueChanged, this, [this](int value) { canvas_->set_wand_tolerance(value); });

  auto* fill_shapes = new CheckGlyphBox(tr("Fill"), toolbar);
  fill_shapes->setObjectName(QStringLiteral("shapeFillCheck"));
  add_option_widget(fill_shapes, {CanvasTool::Rectangle, CanvasTool::Ellipse});
  connect(fill_shapes, &QCheckBox::toggled, this, [this](bool checked) { canvas_->set_fill_shapes(checked); });

  add_option_label(tr("Font:"), {CanvasTool::Text});
  text_font_combo_ = new QFontComboBox(toolbar);
  text_font_combo_->setObjectName(QStringLiteral("textFontCombo"));
  text_font_combo_->setCurrentFont(font());
  text_font_combo_->setFixedWidth(210);
  add_option_widget(text_font_combo_, {CanvasTool::Text});
  add_option_label(tr("Size:"), {CanvasTool::Text});
  text_size_spin_ = new QSpinBox(toolbar);
  text_size_spin_->setObjectName(QStringLiteral("textSizeSpin"));
  text_size_spin_->setRange(6, 300);
  text_size_spin_->setValue(48);
  text_size_spin_->setSuffix(QStringLiteral(" px"));
  configure_toolbar_spinbox(text_size_spin_, 62);
  add_option_widget(text_size_spin_, {CanvasTool::Text});
  text_bold_button_ = new QPushButton(tr("B"), toolbar);
  text_bold_button_->setObjectName(QStringLiteral("textBoldButton"));
  text_bold_button_->setCheckable(true);
  text_bold_button_->setToolTip(tr("Bold"));
  text_bold_button_->setFixedSize(30, 26);
  QFont bold_button_font = text_bold_button_->font();
  bold_button_font.setBold(true);
  text_bold_button_->setFont(bold_button_font);
  add_option_widget(text_bold_button_, {CanvasTool::Text});
  text_italic_button_ = new QPushButton(tr("I"), toolbar);
  text_italic_button_->setObjectName(QStringLiteral("textItalicButton"));
  text_italic_button_->setCheckable(true);
  text_italic_button_->setToolTip(tr("Italic"));
  text_italic_button_->setFixedSize(30, 26);
  QFont italic_button_font = text_italic_button_->font();
  italic_button_font.setItalic(true);
  text_italic_button_->setFont(italic_button_font);
  add_option_widget(text_italic_button_, {CanvasTool::Text});
  connect(text_font_combo_, &QFontComboBox::currentFontChanged, this,
          [this](const QFont&) { apply_text_options_to_active_editor(); });
  connect(text_size_spin_, &QSpinBox::valueChanged, this,
          [this](int) { apply_text_options_to_active_editor(); });
  connect(text_bold_button_, &QPushButton::toggled, this,
          [this](bool) { apply_text_options_to_active_editor(); });
  connect(text_italic_button_, &QPushButton::toggled, this,
          [this](bool) { apply_text_options_to_active_editor(); });

  window_menu->addAction(tool_palette->toggleViewAction());
  window_menu->addAction(toolbar->toggleViewAction());
  refresh_options_bar();
  refresh_color_buttons();

  update_undo_redo_actions();
}

void MainWindow::create_docks() {
  auto* layers_dock = new QDockWidget(tr("Layers"), this);
  layers_dock->setObjectName(QStringLiteral("layersDock"));
  layers_dock->setMinimumHeight(500);
  auto* layers_panel = new QWidget(layers_dock);
  layers_panel->setObjectName(QStringLiteral("layersPanel"));
  layers_panel->setMinimumHeight(440);
  auto* layers_layout = new QVBoxLayout(layers_panel);
  layers_layout->setContentsMargins(6, 6, 6, 6);
  layers_layout->setSpacing(6);

  auto* layer_list = new LayerListWidget(layers_panel);
  layer_list->set_drop_finished_callback([this] { handle_layer_drop(); });
  layer_list->set_ctrl_click_callback([this](QListWidgetItem* item) {
    if (canvas_ == nullptr || item == nullptr) {
      return;
    }
    const auto id = static_cast<LayerId>(item->data(kLayerIdRole).toULongLong());
    canvas_->select_layer_opaque_pixels(id);
  });
  layer_list_ = layer_list;
  layer_list_->setObjectName(QStringLiteral("layerList"));
  layer_list_->setMinimumWidth(250);
  layer_list_->setMinimumHeight(300);
  layer_list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  layer_list_->setDragEnabled(true);
  layer_list_->setAcceptDrops(true);
  layer_list_->setDropIndicatorShown(true);
  layer_list_->setDragDropOverwriteMode(false);
  layer_list_->setDefaultDropAction(Qt::MoveAction);
  layer_list_->setDragDropMode(QAbstractItemView::InternalMove);
  layer_list_->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(layer_list_, &QListWidget::itemSelectionChanged, this, [this] { set_active_layer_from_selection(); });
  connect(layer_list_, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
    set_layer_visibility_from_item(item);
  });
  connect(layer_list_, &QListWidget::customContextMenuRequested, this,
          [this](const QPoint& position) { show_layer_context_menu(position); });
  connect(layer_list_->model(), &QAbstractItemModel::rowsMoved, this, [this] {
    if (updating_layer_list_) {
      return;
    }
    if (const auto* list = dynamic_cast<const LayerListWidget*>(layer_list_); list != nullptr && list->drop_in_progress()) {
      return;
    }
    QTimer::singleShot(0, this, [this] { reorder_layers_from_list(); });
  });

  auto* layer_control_grid = new QGridLayout();
  layer_control_grid->setContentsMargins(0, 0, 0, 0);
  layer_control_grid->setHorizontalSpacing(6);
  layer_control_grid->setVerticalSpacing(4);
  auto* mode_label = new QLabel(tr("Mode"), layers_panel);
  layer_control_grid->addWidget(mode_label, 0, 0);
  blend_combo_ = new QComboBox(layers_panel);
  blend_combo_->addItem(blend_mode_name(BlendMode::Normal), static_cast<int>(BlendMode::Normal));
  blend_combo_->addItem(blend_mode_name(BlendMode::Multiply), static_cast<int>(BlendMode::Multiply));
  blend_combo_->addItem(blend_mode_name(BlendMode::Screen), static_cast<int>(BlendMode::Screen));
  blend_combo_->addItem(blend_mode_name(BlendMode::Overlay), static_cast<int>(BlendMode::Overlay));
  blend_combo_->addItem(blend_mode_name(BlendMode::Darken), static_cast<int>(BlendMode::Darken));
  blend_combo_->addItem(blend_mode_name(BlendMode::Lighten), static_cast<int>(BlendMode::Lighten));
  blend_combo_->addItem(blend_mode_name(BlendMode::ColorDodge), static_cast<int>(BlendMode::ColorDodge));
  blend_combo_->addItem(blend_mode_name(BlendMode::ColorBurn), static_cast<int>(BlendMode::ColorBurn));
  blend_combo_->addItem(blend_mode_name(BlendMode::HardLight), static_cast<int>(BlendMode::HardLight));
  blend_combo_->addItem(blend_mode_name(BlendMode::SoftLight), static_cast<int>(BlendMode::SoftLight));
  blend_combo_->addItem(blend_mode_name(BlendMode::Difference), static_cast<int>(BlendMode::Difference));
  blend_combo_->addItem(blend_mode_name(BlendMode::LinearBurn), static_cast<int>(BlendMode::LinearBurn));
  blend_combo_->addItem(blend_mode_name(BlendMode::PinLight), static_cast<int>(BlendMode::PinLight));
  blend_combo_->addItem(blend_mode_name(BlendMode::Saturation), static_cast<int>(BlendMode::Saturation));
  blend_combo_->addItem(blend_mode_name(BlendMode::Luminosity), static_cast<int>(BlendMode::Luminosity));
  blend_combo_->setObjectName(QStringLiteral("layerBlendModeCombo"));
  layer_control_grid->addWidget(blend_combo_, 0, 1, 1, 2);
  connect(blend_combo_, &QComboBox::currentIndexChanged, this, [this](int index) { set_active_layer_blend(index); });

  auto* opacity_label = new QLabel(tr("Opacity"), layers_panel);
  layer_control_grid->addWidget(opacity_label, 1, 0);
  opacity_slider_ = new QSlider(Qt::Horizontal, layers_panel);
  opacity_slider_->setRange(0, 100);
  opacity_slider_->setValue(100);
  layer_control_grid->addWidget(opacity_slider_, 1, 1);
  opacity_spin_ = new QSpinBox(layers_panel);
  opacity_spin_->setObjectName(QStringLiteral("layerOpacitySpin"));
  opacity_spin_->setRange(0, 100);
  opacity_spin_->setSuffix(QStringLiteral("%"));
  opacity_spin_->setButtonSymbols(QAbstractSpinBox::NoButtons);
  opacity_spin_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  opacity_spin_->setFixedWidth(54);
  layer_control_grid->addWidget(opacity_spin_, 1, 2);
  connect(opacity_slider_, &QSlider::valueChanged, opacity_spin_, &QSpinBox::setValue);
  connect(opacity_spin_, &QSpinBox::valueChanged, opacity_slider_, &QSlider::setValue);
  connect(opacity_spin_, &QSpinBox::valueChanged, this, [this](int value) { set_active_layer_opacity(value); });
  layers_layout->addLayout(layer_control_grid);
  layers_layout->addWidget(layer_list_, 1);

  auto* layer_buttons = new QHBoxLayout();
  layer_buttons->setContentsMargins(0, 0, 0, 0);
  layer_buttons->setSpacing(10);
  auto* add_button = new QPushButton(layers_panel);
  auto* add_folder_button = new QPushButton(layers_panel);
  auto* duplicate_button = new QPushButton(layers_panel);
  auto* rename_button = new QPushButton(layers_panel);
  auto* delete_button = new QPushButton(layers_panel);
  add_folder_button->setObjectName(QStringLiteral("layerNewFolderButton"));
  add_button->setIcon(simple_icon(QStringLiteral("new")));
  add_folder_button->setIcon(simple_icon(QStringLiteral("dir"), QColor(245, 205, 105)));
  duplicate_button->setIcon(simple_icon(QStringLiteral("dup")));
  rename_button->setIcon(simple_icon(QStringLiteral("RN")));
  delete_button->setIcon(simple_icon(QStringLiteral("trash")));
  add_button->setToolTip(tr("New Layer"));
  add_folder_button->setToolTip(tr("New Folder"));
  duplicate_button->setToolTip(tr("Duplicate Layer"));
  rename_button->setToolTip(tr("Rename Layer"));
  delete_button->setToolTip(tr("Delete Layer"));
  for (auto* button : {add_button, add_folder_button, duplicate_button, rename_button, delete_button}) {
    button->setProperty("layerActionButton", true);
    button->setIconSize(QSize(24, 24));
    button->setFixedSize(40, 34);
  }
  layer_buttons->addWidget(add_button);
  layer_buttons->addWidget(add_folder_button);
  layer_buttons->addWidget(duplicate_button);
  layer_buttons->addWidget(rename_button);
  layer_buttons->addWidget(delete_button);
  layer_buttons->addStretch(1);
  layers_layout->addLayout(layer_buttons);
  connect(add_button, &QPushButton::clicked, this, [this] { add_layer(); });
  connect(add_folder_button, &QPushButton::clicked, this, [this] { create_layer_folder(); });
  connect(duplicate_button, &QPushButton::clicked, this, [this] { duplicate_active_layer(); });
  connect(rename_button, &QPushButton::clicked, this, [this] { rename_active_layer(); });
  connect(delete_button, &QPushButton::clicked, this, [this] { delete_active_layer(); });

  lock_transparency_check_ = new QCheckBox(tr("Lock transparent pixels"), layers_panel);
  lock_transparency_check_->setObjectName(QStringLiteral("layerLockTransparencyCheck"));
  lock_transparency_check_->setToolTip(tr("Preserve existing transparency while painting, filling, or erasing"));
  layers_layout->addWidget(lock_transparency_check_);
  connect(lock_transparency_check_, &QCheckBox::toggled, this,
          [this](bool checked) { set_active_layer_lock_transparency(checked); });

  layers_dock->setWidget(layers_panel);
  install_collapsible_dock_title(layers_dock, layers_panel, QStringLiteral("layers"), 500);
  addDockWidget(Qt::RightDockWidgetArea, layers_dock);

  auto* history_dock = new QDockWidget(tr("History"), this);
  history_dock->setObjectName(QStringLiteral("historyDock"));
  history_list_ = new QListWidget(history_dock);
  history_dock->setWidget(history_list_);
  install_collapsible_dock_title(history_dock, history_list_, QStringLiteral("history"));
  addDockWidget(Qt::RightDockWidgetArea, history_dock);

  auto* properties_dock = new QDockWidget(tr("Properties"), this);
  properties_dock->setObjectName(QStringLiteral("propertiesDock"));
  auto* properties_panel = new QWidget(properties_dock);
  auto* properties_layout = new QVBoxLayout(properties_panel);
  document_info_label_ = new QLabel(properties_panel);
  document_info_label_->setObjectName(QStringLiteral("documentInfoLabel"));
  document_info_label_->setWordWrap(true);
  properties_layout->addWidget(document_info_label_);
  properties_layout->addStretch(1);
  properties_dock->setWidget(properties_panel);
  install_collapsible_dock_title(properties_dock, properties_panel, QStringLiteral("properties"));
  addDockWidget(Qt::RightDockWidgetArea, properties_dock);

  auto* info_dock = new QDockWidget(tr("Info"), this);
  info_dock->setObjectName(QStringLiteral("infoDock"));
  auto* info_panel = new QWidget(info_dock);
  auto* info_layout = new QVBoxLayout(info_panel);
  info_layout->setContentsMargins(8, 8, 8, 8);
  canvas_info_label_ = new QLabel(info_panel);
  canvas_info_label_->setObjectName(QStringLiteral("canvasInfoLabel"));
  canvas_info_label_->setText(tr("X: -\nY: -\nRGB: -\nRect: -"));
  canvas_info_label_->setWordWrap(true);
  info_layout->addWidget(canvas_info_label_);
  info_layout->addStretch(1);
  info_dock->setWidget(info_panel);
  install_collapsible_dock_title(info_dock, info_panel, QStringLiteral("info"));
  addDockWidget(Qt::RightDockWidgetArea, info_dock);

  create_swatches_dock();
}

void MainWindow::create_swatches_dock() {
  auto* swatches_dock = new QDockWidget(tr("Swatches"), this);
  swatches_dock->setObjectName(QStringLiteral("swatchesDock"));
  auto* swatches_panel = new QWidget(swatches_dock);
  auto* swatches_layout = new QGridLayout(swatches_panel);
  swatches_layout->setContentsMargins(8, 8, 8, 8);
  swatches_layout->setSpacing(5);

  const std::vector<QColor> swatches = {
      QColor(0, 0, 0),       QColor(255, 255, 255), QColor(220, 20, 40),  QColor(255, 140, 0),
      QColor(255, 220, 0),   QColor(30, 160, 80),   QColor(0, 150, 220),  QColor(50, 90, 220),
      QColor(140, 70, 220),  QColor(230, 60, 170),  QColor(110, 70, 35),  QColor(128, 128, 128),
      QColor(35, 40, 48),    QColor(245, 248, 252), QColor(80, 200, 180), QColor(255, 105, 105),
  };

  int index = 0;
  for (const auto& color : swatches) {
    auto* button = new QPushButton(swatches_panel);
    button->setObjectName(QStringLiteral("swatchButton"));
    button->setToolTip(tr("Set foreground color"));
    button->setStyleSheet(swatch_button_style(color));
    swatches_layout->addWidget(button, index / 4, index % 4);
    connect(button, &QPushButton::clicked, this, [this, color] {
      canvas_->set_primary_color(color);
      refresh_color_buttons();
      statusBar()->showMessage(tr("Foreground color changed"));
    });
    ++index;
  }

  swatches_dock->setWidget(swatches_panel);
  install_collapsible_dock_title(swatches_dock, swatches_panel, QStringLiteral("swatches"));
  addDockWidget(Qt::RightDockWidgetArea, swatches_dock);
}

void MainWindow::configure_canvas(CanvasWidget* canvas) {
  canvas->setObjectName(QStringLiteral("canvas"));
  canvas->set_before_edit_callback([this](QString label) { push_undo_snapshot(std::move(label)); });
  canvas->set_color_picked_callback([this, canvas](QColor color) {
    canvas->set_primary_color(color);
    refresh_color_buttons();
    statusBar()->showMessage(tr("Picked color"));
  });
  canvas->set_text_requested_callback([this](QPoint point) { add_text_at(point); });
  canvas->set_active_layer_changed_callback([this](LayerId layer_id) {
    reveal_layer_in_layer_list(layer_id);
    refresh_layer_controls();
  });
  canvas->set_status_callback([this](QString message) { statusBar()->showMessage(message); });
  canvas->set_info_callback([this](CanvasInfoState info) { update_canvas_info(std::move(info)); });
}

void MainWindow::add_document_session(Document document, QString title, QString path) {
  auto session = std::make_unique<DocumentSession>();
  session->document = std::move(document);
  session->title = std::move(title);
  session->path = std::move(path);
  collect_initially_collapsed_layer_groups(session->document.layers(), session->collapsed_layer_groups);
  session->canvas = new CanvasWidget(document_tabs_);
  configure_canvas(session->canvas);
  session->canvas->set_document(&session->document);
  if (move_auto_select_check_ != nullptr) {
    session->canvas->set_auto_select_layer(move_auto_select_check_->isChecked());
  }
  session->canvas->set_tool(current_tool_);
  session->canvas->set_selection_mode(current_selection_mode_);
  session->canvas->set_marquee_style(current_marquee_style_);
  session->canvas->set_marquee_fixed_size(current_marquee_width_, current_marquee_height_);

  auto* canvas = session->canvas;
  const auto tab_title = session->title;
  sessions_.push_back(std::move(session));
  const auto tab_index = document_tabs_->addTab(canvas, tab_title);
  document_tabs_->setCurrentIndex(tab_index);
  canvas_ = canvas;
  canvas_->setFocus(Qt::OtherFocusReason);
  refresh_options_bar();
  refresh_layer_list();
  refresh_layer_controls();
  refresh_document_info();
  update_undo_redo_actions();
}

void MainWindow::activate_document_tab(int index) {
  auto* canvas = index >= 0 ? dynamic_cast<CanvasWidget*>(document_tabs_->widget(index)) : nullptr;
  if (canvas == nullptr || session_for_canvas(canvas) == nullptr) {
    canvas_ = nullptr;
    return;
  }
  canvas_ = canvas;
  canvas_->set_tool(current_tool_);
  canvas_->set_selection_mode(current_selection_mode_);
  canvas_->set_marquee_style(current_marquee_style_);
  canvas_->set_marquee_fixed_size(current_marquee_width_, current_marquee_height_);
  canvas_->setFocus(Qt::OtherFocusReason);
  refresh_options_bar();
  refresh_layer_list();
  refresh_layer_controls();
  refresh_document_info();
  update_undo_redo_actions();
}

void MainWindow::close_document_tab(int index) {
  if (sessions_.size() <= 1 || index < 0 || index >= document_tabs_->count()) {
    return;
  }
  auto* widget = dynamic_cast<CanvasWidget*>(document_tabs_->widget(index));
  const auto found = std::find_if(sessions_.begin(), sessions_.end(), [widget](const auto& candidate) {
    return candidate->canvas == widget;
  });
  if (found == sessions_.end()) {
    return;
  }
  document_tabs_->removeTab(index);
  sessions_.erase(found);
  delete widget;
  activate_document_tab(document_tabs_->currentIndex());
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

Document& MainWindow::document() {
  auto* active_session = session_for_canvas(dynamic_cast<CanvasWidget*>(document_tabs_->currentWidget()));
  if (active_session == nullptr) {
    throw std::logic_error("No active document");
  }
  return active_session->document;
}

const Document& MainWindow::document() const {
  const auto* active_session = session_for_canvas(dynamic_cast<CanvasWidget*>(document_tabs_->currentWidget()));
  if (active_session == nullptr) {
    throw std::logic_error("No active document");
  }
  return active_session->document;
}

MainWindow::DocumentSession& MainWindow::session() {
  auto* active_session = session_for_canvas(dynamic_cast<CanvasWidget*>(document_tabs_->currentWidget()));
  if (active_session == nullptr) {
    throw std::logic_error("No active document session");
  }
  return *active_session;
}

const MainWindow::DocumentSession& MainWindow::session() const {
  const auto* active_session = session_for_canvas(dynamic_cast<CanvasWidget*>(document_tabs_->currentWidget()));
  if (active_session == nullptr) {
    throw std::logic_error("No active document session");
  }
  return *active_session;
}

void MainWindow::reset_document(std::int32_t width, std::int32_t height, QColor background, QString history_label) {
  Document new_document(width, height, PixelFormat::rgb8());
  const auto background_format = background.alpha() == 0 ? PixelFormat::rgba8() : PixelFormat::rgb8();
  new_document.add_pixel_layer("Background", make_solid_pixels(new_document.width(), new_document.height(), background,
                                                            background_format));
  new_document.add_pixel_layer("Paint Layer", make_solid_pixels(new_document.width(), new_document.height(), QColor(0, 0, 0, 0),
                                                             PixelFormat::rgba8()));
  add_document_session(std::move(new_document), tr("Untitled-%1").arg(sessions_.size() + 1));
  auto& active_session = session();
  active_session.undo_stack.clear();
  active_session.redo_stack.clear();
  if (history_list_ != nullptr) {
    history_list_->clear();
  }
  update_history(std::move(history_label));
  refresh_layer_list();
  refresh_layer_controls();
  update_undo_redo_actions();
  statusBar()->showMessage(tr("Created %1 x %2 document").arg(width).arg(height));
}

void MainWindow::create_new_document() {
  const auto settings = request_new_document_settings(this);
  if (!settings.has_value()) {
    return;
  }
  reset_document(settings->width, settings->height, settings->background, tr("New document"));
}

void MainWindow::resize_canvas_dialog() {
  auto& doc = document();
  const auto settings = request_canvas_size_settings(this, doc.width(), doc.height());
  if (!settings.has_value()) {
    return;
  }
  if (settings->width == doc.width() && settings->height == doc.height()) {
    return;
  }

  push_undo_snapshot(tr("Canvas size"));
  resize_canvas_and_layers(doc, settings->width, settings->height);
  canvas_->clear_selection();
  canvas_->set_document(&doc);
  refresh_layer_list();
  refresh_layer_controls();
  refresh_document_info();
  statusBar()->showMessage(tr("Canvas %1 x %2").arg(settings->width).arg(settings->height));
}

void MainWindow::open_document() {
  const auto path = QFileDialog::getOpenFileName(this, tr("Open"), QString(), open_file_filter());
  if (path.isEmpty()) {
    return;
  }
  open_document_path(path);
}

void MainWindow::open_document_path(QString path) {
  try {
    const auto info = QFileInfo(path);
    const auto extension = info.suffix().toLower();
    Document opened;
    if (is_photoshop_document_extension(extension)) {
      opened = psd::DocumentIo::read_file(path.toStdString());
    } else {
      QImageReader reader(path);
      reader.setAutoTransform(true);
      const auto image = reader.read();
      if (image.isNull()) {
        throw std::runtime_error(reader.errorString().toStdString());
      }
      opened = document_from_qimage(image, info.completeBaseName().toStdString());
    }
    add_document_session(std::move(opened), info.fileName(), path);
    canvas_->fit_to_view();
    session().undo_stack.clear();
    session().redo_stack.clear();
    if (history_list_ != nullptr) {
      history_list_->clear();
    }
    update_history(tr("Open"));
    refresh_layer_list();
    refresh_layer_controls();
    update_undo_redo_actions();
    add_recent_file(path);
    statusBar()->showMessage(tr("Opened %1").arg(path));
  } catch (const std::exception& error) {
    QMessageBox::critical(this, tr("Open failed"), QString::fromUtf8(error.what()));
  }
}

void MainWindow::save_document() {
  if (session().path.isEmpty()) {
    save_document_as();
    return;
  }
  save_document_to_path(session().path);
}

void MainWindow::save_document_as() {
  QString selected_filter;
  auto path = QFileDialog::getSaveFileName(this, tr("Save As"), session().path, save_file_filter(), &selected_filter);
  if (path.isEmpty()) {
    return;
  }
  path = path_with_default_extension(path, selected_filter);
  save_document_to_path(path);
}

void MainWindow::save_document_to_path(QString path) {
  try {
    const auto extension = extension_for_path(path);
    if (is_photoshop_document_extension(extension)) {
      psd::DocumentIo::write_layered_rgb8_file(document(), path.toStdString());
    } else {
      QImageWriter writer(path);
      if (extension == QStringLiteral("jpg") || extension == QStringLiteral("jpeg")) {
        writer.setQuality(95);
      }
      const auto image = qimage_from_document(document(), image_format_preserves_alpha(extension.toStdString()));
      if (!writer.write(image)) {
        throw std::runtime_error(writer.errorString().toStdString());
      }
    }
    auto& active_session = session();
    active_session.path = path;
    active_session.title = QFileInfo(path).fileName();
    document_tabs_->setTabText(document_tabs_->currentIndex(), active_session.title);
    update_history(tr("Save"));
    add_recent_file(path);
    statusBar()->showMessage(tr("Saved %1").arg(path));
  } catch (const std::exception& error) {
    QMessageBox::critical(this, tr("Save failed"), QString::fromUtf8(error.what()));
  }
}

void MainWindow::export_flat_image() {
  QString selected_filter;
  auto path = QFileDialog::getSaveFileName(this, tr("Export Flat Image"), QString(), export_image_filter(), &selected_filter);
  if (path.isEmpty()) {
    return;
  }
  path = path_with_default_extension(path, selected_filter);

  try {
    const auto extension = extension_for_path(path);
    if (is_photoshop_document_extension(extension)) {
      psd::DocumentIo::write_flat_rgb8_file(document(), path.toStdString());
    } else {
      QImageWriter writer(path);
      if (extension == QStringLiteral("jpg") || extension == QStringLiteral("jpeg")) {
        writer.setQuality(95);
      }
      const auto image = qimage_from_document(document(), image_format_preserves_alpha(extension.toStdString()));
      if (!writer.write(image)) {
        throw std::runtime_error(writer.errorString().toStdString());
      }
    }
    update_history(tr("Export flat image"));
    statusBar()->showMessage(tr("Exported %1").arg(path));
  } catch (const std::exception& error) {
    QMessageBox::critical(this, tr("Export failed"), QString::fromUtf8(error.what()));
  }
}

void MainWindow::scan_legacy_plugins() {
  const auto paths = QFileDialog::getOpenFileNames(this, tr("Scan Legacy Photoshop Plug-ins"), QString(),
                                                  tr("Photoshop Plug-ins (*.8bf *.8bi *.8li);;All Files (*.*)"));
  if (paths.isEmpty()) {
    return;
  }

  QStringList report;
  int available = 0;
  for (const auto& path : paths) {
    if (register_legacy_plugin_path(path, &report)) {
      ++available;
    }
  }

  QMessageBox::information(this, tr("Legacy Photoshop Plug-ins"),
                           tr("%1 plug-in action(s) available under Plug-ins > Legacy Photoshop Plug-ins.\n\n%2")
                               .arg(available)
                               .arg(report.join('\n')));
}

void MainWindow::load_bundled_legacy_plugins() {
  const QDir fixture_dir(QCoreApplication::applicationDirPath() + QStringLiteral("/test-fixtures/photoshop-plugins"));
  if (!fixture_dir.exists()) {
    return;
  }

  const auto files =
      fixture_dir.entryInfoList({QStringLiteral("*.8bf"), QStringLiteral("*.8bi"), QStringLiteral("*.8li")},
                                QDir::Files | QDir::Readable, QDir::Name);
  for (const auto& file : files) {
    register_legacy_plugin_path(file.absoluteFilePath());
  }
}

bool MainWindow::register_legacy_plugin_path(const QString& path, QStringList* report) {
  LegacyPhotoshopAdapter adapter;
  const auto probe = adapter.probe(path.toStdString());
  const auto file_name = QFileInfo(path).fileName();
  if (report != nullptr) {
    *report << tr("%1: %2 (%3, %4)")
                   .arg(file_name, QString::fromStdString(probe.reason),
                        QString::fromStdString(legacy_plugin_kind_name(probe.kind)),
                        QString::fromStdString(probe.architecture));
  }
  if (!probe.supported) {
    return false;
  }

  const auto identifier = "legacy.photoshop." + QFileInfo(path).completeBaseName().toStdString();
  PluginDescriptor descriptor;
  descriptor.kind = probe.kind == LegacyPhotoshopPluginKind::Format8bi ? PHOTOSLOP_PLUGIN_FILE_FORMAT
                                                                        : PHOTOSLOP_PLUGIN_FILTER;
  descriptor.identifier = identifier;
  descriptor.display_name = QFileInfo(path).completeBaseName().toStdString();
  descriptor.path = path.toStdString();
  try {
    if (plugin_host_.find(identifier) == nullptr) {
      plugin_host_.register_plugin(std::move(descriptor));
    }
    if (const auto* registered = plugin_host_.find(identifier); registered != nullptr) {
      add_legacy_plugin_action(*registered);
      return true;
    }
  } catch (const std::exception&) {
  }
  return false;
}

void MainWindow::add_legacy_plugin_action(const PluginDescriptor& descriptor) {
  if (legacy_plugins_menu_ == nullptr) {
    return;
  }
  const auto identifier = QString::fromStdString(descriptor.identifier);
  for (auto* action : legacy_plugins_menu_->actions()) {
    if (action->data().toString() == identifier) {
      return;
    }
  }

  auto* action = legacy_plugins_menu_->addAction(QString::fromStdString(descriptor.display_name));
  action->setData(identifier);
  action->setObjectName(QStringLiteral("legacyPluginAction"));
  action->setIcon(simple_icon(QStringLiteral("8BF"), QColor(105, 185, 255)));
  connect(action, &QAction::triggered, this, [this, identifier] { run_legacy_plugin(identifier); });
}

void MainWindow::run_legacy_plugin(QString identifier) {
  const auto* descriptor = plugin_host_.find(identifier.toStdString());
  if (descriptor == nullptr) {
    return;
  }
  const auto active = document().active_layer_id();
  if (!active.has_value()) {
    statusBar()->showMessage(tr("Select a pixel layer before running the plug-in"));
    return;
  }
  auto* layer = document().find_layer(*active);
  if (layer == nullptr || layer->kind() != LayerKind::Pixel || layer->pixels().format().bit_depth != BitDepth::UInt8 ||
      layer->pixels().format().channels < 3) {
    statusBar()->showMessage(tr("Select an editable 8-bit pixel layer before running the plug-in"));
    return;
  }

  const auto name = QString::fromStdString(descriptor->display_name).toLower();
  if (!name.contains(QStringLiteral("greyscale")) && !name.contains(QStringLiteral("white to transparent"))) {
    QMessageBox::information(this, tr("Legacy Photoshop Plug-in"),
                             tr("%1 was scanned and is available, but this build only has compatibility shims for the "
                                "bundled Greyscale and White to Transparent test filters. A full 8BF host still needs "
                                "the out-of-process Photoshop SDK adapter.")
                                 .arg(QString::fromStdString(descriptor->display_name)));
    return;
  }

  push_undo_snapshot(tr("Legacy plug-in"));
  auto& pixels = layer->pixels();
  const auto channels = pixels.format().channels;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* px = pixels.pixel(x, y);
      if (name.contains(QStringLiteral("greyscale"))) {
        const auto gray = static_cast<std::uint8_t>(
            std::clamp(std::lround(0.299 * px[0] + 0.587 * px[1] + 0.114 * px[2]), 0L, 255L));
        px[0] = gray;
        px[1] = gray;
        px[2] = gray;
      } else {
        if (channels < 4) {
          continue;
        }
        const auto whiteness = std::min({px[0], px[1], px[2]});
        px[3] = static_cast<std::uint8_t>(255 - whiteness);
      }
    }
  }
  canvas_->document_changed(to_qrect(layer->bounds()));
  statusBar()->showMessage(tr("Applied %1").arg(QString::fromStdString(descriptor->display_name)));
}

void MainWindow::cut_selection() {
  auto ids = selected_layer_ids();
  if (ids.empty()) {
    const auto active = document().active_layer_id();
    if (active.has_value()) {
      ids.push_back(*active);
    }
  }
  if (ids.empty()) {
    statusBar()->showMessage(tr("Select a layer to cut"));
    return;
  }

  const std::set<LayerId> selected(ids.begin(), ids.end());
  std::vector<LayerId> layers_to_cut;
  for (const auto& layer : document().layers()) {
    if (!selected.contains(layer.id()) || layer.kind() != LayerKind::Pixel || !layer.visible()) {
      continue;
    }
    layers_to_cut.push_back(layer.id());
  }
  if (layers_to_cut.empty()) {
    clipboard_.reset();
    QApplication::clipboard()->clear();
    statusBar()->showMessage(tr("Selected layers are hidden or not editable; nothing cut"));
    return;
  }

  copy_selection();
  if (!clipboard_.has_value() || clipboard_->pixels.empty()) {
    return;
  }

  auto& doc = document();
  push_undo_snapshot(tr("Cut"));
  Rect affected;
  auto options = edit_options(*canvas_);
  for (const auto id : layers_to_cut) {
    auto* layer = doc.find_layer(id);
    if (layer == nullptr || layer->kind() != LayerKind::Pixel || !layer->visible()) {
      continue;
    }
    options.lock_transparent_pixels = layer_locks_transparent_pixels(*layer);
    affected = unite_rect(affected, photoslop::clear_rect(doc, id, layer->bounds(), options));
  }
  if (!affected.empty()) {
    canvas_->document_changed(to_qrect(affected));
  }
  refresh_layer_list();
  refresh_layer_controls();
  statusBar()->showMessage(tr("Cut %1 layer(s)").arg(static_cast<qulonglong>(layers_to_cut.size())));
}

void MainWindow::copy_selection() {
  auto ids = selected_layer_ids();
  if (ids.empty()) {
    const auto active = document().active_layer_id();
    if (active.has_value()) {
      ids.push_back(*active);
    }
  }
  if (ids.empty()) {
    statusBar()->showMessage(tr("Select a layer to copy"));
    return;
  }

  const std::set<LayerId> selected(ids.begin(), ids.end());
  std::vector<const Layer*> layers_to_copy;
  for (const auto& layer : document().layers()) {
    if (!selected.contains(layer.id()) || layer.kind() != LayerKind::Pixel || !layer.visible()) {
      continue;
    }
    layers_to_copy.push_back(&layer);
  }
  if (layers_to_copy.empty()) {
    clipboard_.reset();
    QApplication::clipboard()->clear();
    statusBar()->showMessage(tr("Selected layers are hidden or not editable; nothing copied"));
    return;
  }

  Rect copy_rect;
  if (canvas_->selected_document_rect().has_value()) {
    copy_rect = to_core_rect(*canvas_->selected_document_rect());
  } else {
    for (const auto* layer : layers_to_copy) {
      copy_rect = unite_rect(copy_rect, layer->bounds());
    }
  }
  copy_rect = intersect_copy_rect(copy_rect, Rect::from_size(document().width(), document().height()));
  if (copy_rect.empty()) {
    clipboard_.reset();
    QApplication::clipboard()->clear();
    statusBar()->showMessage(tr("Nothing to copy"));
    return;
  }

  PixelBuffer copied;
  if (layers_to_copy.size() == 1U) {
    copied = copy_pixels_from_layer(*layers_to_copy.front(), copy_rect, canvas_->selected_document_region());
  } else {
    Document selected_document(document().width(), document().height(), document().format());
    for (const auto* layer : layers_to_copy) {
      selected_document.add_layer(*layer);
    }
    const auto image =
        qimage_from_document(selected_document, true).copy(QRect(copy_rect.x, copy_rect.y, copy_rect.width, copy_rect.height));
    copied = pixels_from_image_rgba(image);
    apply_selection_mask(copied, copy_rect, canvas_->selected_document_region());
  }

  clipboard_ = ClipboardPayload{std::move(copied), QPoint(copy_rect.x, copy_rect.y)};
  QApplication::clipboard()->setImage(image_from_pixels(clipboard_->pixels));
  update_history(tr("Copy"));
  statusBar()->showMessage(
      tr("Copied %1 layer(s), %2 x %3 px")
          .arg(static_cast<qulonglong>(layers_to_copy.size()))
          .arg(copy_rect.width)
          .arg(copy_rect.height));
}

void MainWindow::copy_merged() {
  auto copy_rect = Rect::from_size(document().width(), document().height());
  if (canvas_->selected_document_rect().has_value()) {
    copy_rect = intersect_copy_rect(copy_rect, to_core_rect(*canvas_->selected_document_rect()));
  }
  if (copy_rect.empty()) {
    statusBar()->showMessage(tr("Nothing to copy"));
    return;
  }

  const auto image = qimage_from_document(document(), true).copy(QRect(copy_rect.x, copy_rect.y, copy_rect.width, copy_rect.height));
  clipboard_ = ClipboardPayload{pixels_from_image_rgba(image), QPoint(copy_rect.x, copy_rect.y)};
  QApplication::clipboard()->setImage(image);
  update_history(tr("Copy merged"));
  statusBar()->showMessage(tr("Copied merged %1 x %2 px").arg(copy_rect.width).arg(copy_rect.height));
}

void MainWindow::paste_clipboard() {
  PixelBuffer pixels;
  QPoint origin;
  if (clipboard_.has_value() && !clipboard_->pixels.empty()) {
    pixels = clipboard_->pixels;
    origin = clipboard_->origin + QPoint(16, 16);
  } else {
    const auto image = QApplication::clipboard()->image();
    if (image.isNull()) {
      statusBar()->showMessage(tr("Clipboard does not contain an image"));
      return;
    }
    pixels = pixels_from_image_rgba(image);
    origin = QPoint(std::max(0, (document().width() - pixels.width()) / 2),
                    std::max(0, (document().height() - pixels.height()) / 2));
  }

  push_undo_snapshot(tr("Paste"));
  Layer pasted(document().allocate_layer_id(), "Pasted Layer", std::move(pixels));
  pasted.set_bounds(Rect{origin.x(), origin.y(), pasted.pixels().width(), pasted.pixels().height()});
  document().add_layer(std::move(pasted));
  if (move_tool_action_ != nullptr) {
    move_tool_action_->trigger();
  } else {
    current_tool_ = CanvasTool::Move;
    canvas_->set_tool(current_tool_);
  }
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
  statusBar()->showMessage(tr("Pasted as new layer"));
}

void MainWindow::transform_active_layer_dialog() {
  if (canvas_ == nullptr || !canvas_->begin_free_transform()) {
    statusBar()->showMessage(tr("Select a pixel layer to transform"));
  }
}

void MainWindow::add_text_at(QPoint document_point) {
  if (canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) != nullptr) {
    finish_active_text_editor();
  }

  std::optional<LayerId> editing_layer;
  std::optional<bool> editing_layer_was_visible;
  QString initial_text;
  QString family = text_font_combo_ != nullptr ? text_font_combo_->currentFont().family() : font().family();
  int document_text_size = text_size_spin_ != nullptr ? text_size_spin_->value() : 48;
  bool text_bold = text_bold_button_ != nullptr && text_bold_button_->isChecked();
  bool text_italic = text_italic_button_ != nullptr && text_italic_button_->isChecked();
  QColor text_color = canvas_->primary_color();
  int document_editor_width = std::max(160, std::min(520, document().width() - document_point.x() - 8));
  int document_editor_height = 96;
  if (const auto active = document().active_layer_id(); active.has_value()) {
    if (auto* layer = document().find_layer(*active); layer != nullptr && layer->metadata().contains("photoslop.text") &&
        layer->bounds().contains(document_point.x(), document_point.y())) {
      editing_layer = *active;
      editing_layer_was_visible = layer->visible();
      initial_text = QString::fromStdString(layer->metadata().at("photoslop.text"));
      if (const auto found = layer->metadata().find("photoslop.text.font"); found != layer->metadata().end()) {
        const auto stored_family = QString::fromStdString(found->second);
        if (stored_family.compare(QStringLiteral("PSD Text"), Qt::CaseInsensitive) != 0) {
          family = stored_family;
        }
      }
      if (const auto found = layer->metadata().find("photoslop.text.size"); found != layer->metadata().end()) {
        document_text_size = std::max(1, std::atoi(found->second.c_str()));
      }
      if (const auto found = layer->metadata().find("photoslop.text.color"); found != layer->metadata().end()) {
        const QColor stored(QString::fromStdString(found->second));
        if (stored.isValid()) {
          text_color = stored;
        }
      }
      if (const auto found = layer->metadata().find("photoslop.text.bold"); found != layer->metadata().end()) {
        text_bold = found->second == "true";
      }
      if (const auto found = layer->metadata().find("photoslop.text.italic"); found != layer->metadata().end()) {
        text_italic = found->second == "true";
      }
      if (text_font_combo_ != nullptr) {
        text_font_combo_->setCurrentFont(QFont(family));
      }
      if (text_size_spin_ != nullptr) {
        text_size_spin_->setValue(document_text_size);
      }
      if (text_bold_button_ != nullptr) {
        text_bold_button_->setChecked(text_bold);
      }
      if (text_italic_button_ != nullptr) {
        text_italic_button_->setChecked(text_italic);
      }
      document_point = QPoint(layer->bounds().x, layer->bounds().y);
      document_editor_width = std::max(160, layer->bounds().width);
      document_editor_height = std::max(64, layer->bounds().height + document_text_size);
      layer->set_visible(false);
      canvas_->document_changed(to_qrect(layer->bounds()));
    }
  }

  auto* editor = new QTextEdit(canvas_);
  editor->setObjectName(QStringLiteral("inlineTextEditor"));
  editor->setAcceptRichText(false);
  editor->setPlainText(initial_text.isEmpty() ? tr("Type") : initial_text);
  editor->selectAll();
  QFont editor_font(family);
  editor_font.setPixelSize(std::max(8, static_cast<int>(std::round(document_text_size * canvas_->zoom()))));
  editor_font.setBold(text_bold);
  editor_font.setItalic(text_italic);
  editor->setFont(editor_font);
  editor->document()->setDocumentMargin(0);
  editor->document()->setDefaultFont(editor_font);
  editor->setFrameShape(QFrame::NoFrame);
  editor->setAttribute(Qt::WA_TranslucentBackground, true);
  editor->viewport()->setAttribute(Qt::WA_TranslucentBackground, true);
  editor->viewport()->setAutoFillBackground(false);
  editor->setLineWrapMode(QTextEdit::NoWrap);
  editor->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  editor->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  editor->setProperty("photoslop.documentTextSize", document_text_size);
  editor->setProperty("photoslop.documentTextWidth", document_editor_width);
  editor->setProperty("photoslop.documentTextColor", text_color);
  editor->setProperty("photoslop.documentTextX", document_point.x());
  editor->setProperty("photoslop.documentTextY", document_point.y());
  if (editing_layer.has_value()) {
    editor->setProperty("photoslop.editingLayerId", QVariant::fromValue<qulonglong>(*editing_layer));
  }
  if (editing_layer_was_visible.has_value()) {
    editor->setProperty("photoslop.editingLayerWasVisible", *editing_layer_was_visible);
  }
  editor->setStyleSheet(inline_text_editor_style(text_color, editor_font.pixelSize()));
  const auto widget_point = canvas_->widget_position_for_document_point(document_point);
  editor->setGeometry(widget_point.x(), widget_point.y(),
                      std::max(80, static_cast<int>(std::round(document_editor_width * canvas_->zoom()))),
                      std::max(32, static_cast<int>(std::round(document_editor_height * canvas_->zoom()))));
  const auto resize_editor = [editor = QPointer<QTextEdit>(editor), this] {
    if (editor == nullptr) {
      return;
    }
    const auto document_editor_width = std::max(64, editor->property("photoslop.documentTextWidth").toInt());
    const auto document_text_size = std::max(1, editor->property("photoslop.documentTextSize").toInt());
    const auto minimum_width = std::max(80, static_cast<int>(std::round(document_editor_width * canvas_->zoom())));
    editor->document()->setTextWidth(-1);
    const auto content_width = static_cast<int>(std::ceil(editor->document()->idealWidth())) + 6;
    const auto width = std::max(minimum_width, content_width);
    editor->setProperty("photoslop.documentTextWidth",
                        std::max(document_editor_width,
                                 static_cast<int>(std::ceil(static_cast<double>(width) / canvas_->zoom()))));
    editor->document()->setTextWidth(width);
    const auto text_height =
        std::max(32, static_cast<int>(std::ceil(editor->document()->size().height())) + 2);
    const auto minimum_height =
        std::max(32, static_cast<int>(std::ceil(static_cast<double>(document_text_size) * canvas_->zoom() * 1.45)));
    editor->resize(width, std::max(text_height, minimum_height));
  };
  connect(editor, &QTextEdit::textChanged, editor, resize_editor);
  editor->show();
  resize_editor();
  editor->setFocus(Qt::OtherFocusReason);

  auto committed = std::make_shared<bool>(false);
  const auto commit = [this, editor = QPointer<QTextEdit>(editor), document_point, editing_layer, committed] {
    if (*committed || editor == nullptr) {
      return;
    }
    *committed = true;
    commit_text_editor(editor, document_point, editing_layer);
  };
  auto* shortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), editor);
  connect(shortcut, &QShortcut::activated, editor, commit);
  auto* cancel_shortcut = new QShortcut(QKeySequence(Qt::Key_Escape), editor);
  cancel_shortcut->setContext(Qt::WidgetWithChildrenShortcut);
  connect(cancel_shortcut, &QShortcut::activated, editor,
          [this, editor = QPointer<QTextEdit>(editor), editing_layer, committed] {
            if (*committed || editor == nullptr) {
              return;
            }
            *committed = true;
            cancel_text_editor(editor, editing_layer);
          });
  connect(qApp, &QApplication::focusChanged, editor, [this, editor = QPointer<QTextEdit>(editor), commit](QWidget* old,
                                                                                                          QWidget* now) {
    if (editor == nullptr) {
      return;
    }
    const auto left_editor = old == editor || editor->isAncestorOf(old);
    const auto entered_editor = now == editor || editor->isAncestorOf(now);
    const auto entered_canvas = canvas_ != nullptr && (now == canvas_ || canvas_->isAncestorOf(now));
    if (((left_editor && !entered_editor) || entered_canvas) && !entered_editor && !is_text_option_widget(now)) {
      commit();
    }
  });
}

void MainWindow::cancel_text_editor(QTextEdit* editor, std::optional<LayerId> layer_id) {
  if (editor == nullptr) {
    return;
  }
  const auto restore_existing_visibility =
      editor->property("photoslop.editingLayerWasVisible").isValid()
          ? editor->property("photoslop.editingLayerWasVisible").toBool()
          : true;
  editor->hide();
  editor->setParent(nullptr);
  editor->deleteLater();

  if (layer_id.has_value()) {
    if (auto* layer = document().find_layer(*layer_id); layer != nullptr) {
      layer->set_visible(restore_existing_visibility);
      canvas_->document_changed(to_qrect(layer->bounds()));
      refresh_layer_list();
      refresh_layer_controls();
    }
  }
  statusBar()->showMessage(tr("Canceled text edit"));
}

void MainWindow::commit_text_editor(QTextEdit* editor, QPoint document_point, std::optional<LayerId> layer_id) {
  if (editor == nullptr) {
    return;
  }
  const auto text = editor->toPlainText().trimmed();
  const auto restore_existing_visibility =
      editor->property("photoslop.editingLayerWasVisible").isValid()
          ? editor->property("photoslop.editingLayerWasVisible").toBool()
          : true;
  const auto restore_hidden_text_layer = [this, layer_id, restore_existing_visibility] {
    if (!layer_id.has_value()) {
      return;
    }
    if (auto* layer = document().find_layer(*layer_id); layer != nullptr) {
      layer->set_visible(restore_existing_visibility);
      canvas_->document_changed(to_qrect(layer->bounds()));
      refresh_layer_list();
      refresh_layer_controls();
    }
  };
  editor->hide();
  editor->setParent(nullptr);
  editor->deleteLater();
  if (text.isEmpty()) {
    restore_hidden_text_layer();
    return;
  }

  const auto text_size = std::max(1, editor->property("photoslop.documentTextSize").toInt());
  const auto text_width =
      std::max(64, editor->property("photoslop.documentTextWidth").toInt());
  const auto text_color = editor->property("photoslop.documentTextColor").value<QColor>().isValid()
                              ? editor->property("photoslop.documentTextColor").value<QColor>()
                              : canvas_->primary_color();
  TextToolSettings settings{text, editor->font().family(), text_size, editor->font().bold(), editor->font().italic()};
  auto pixels = render_text_pixels(settings, text_color, text_width);
  if (pixels.empty()) {
    restore_hidden_text_layer();
    return;
  }

  if (layer_id.has_value()) {
    if (auto* layer = document().find_layer(*layer_id); layer != nullptr) {
      layer->set_visible(restore_existing_visibility);
    }
  }
  push_undo_snapshot(tr("Type"));
  auto name = settings.text.simplified();
  if (name.size() > 24) {
    name = name.left(24) + QStringLiteral("...");
  }
  if (layer_id.has_value()) {
    if (auto* layer = document().find_layer(*layer_id); layer != nullptr) {
      layer->set_name(tr("Text: %1").arg(name).toStdString());
      layer->set_pixels(std::move(pixels));
      layer->set_bounds(Rect{document_point.x(), document_point.y(), layer->pixels().width(), layer->pixels().height()});
      layer->set_visible(restore_existing_visibility);
      layer->metadata()["photoslop.text"] = settings.text.toStdString();
      layer->metadata()["photoslop.text.font"] = settings.family.toStdString();
      layer->metadata()["photoslop.text.size"] = std::to_string(settings.size);
      layer->metadata()["photoslop.text.color"] = text_color.name(QColor::HexRgb).toStdString();
      layer->metadata()["photoslop.text.bold"] = settings.bold ? "true" : "false";
      layer->metadata()["photoslop.text.italic"] = settings.italic ? "true" : "false";
    }
  } else {
    Layer text_layer(document().allocate_layer_id(), tr("Text: %1").arg(name).toStdString(), std::move(pixels));
    text_layer.set_bounds(
        Rect{document_point.x(), document_point.y(), text_layer.pixels().width(), text_layer.pixels().height()});
    text_layer.metadata()["photoslop.text"] = settings.text.toStdString();
    text_layer.metadata()["photoslop.text.font"] = settings.family.toStdString();
    text_layer.metadata()["photoslop.text.size"] = std::to_string(settings.size);
    text_layer.metadata()["photoslop.text.color"] = text_color.name(QColor::HexRgb).toStdString();
    text_layer.metadata()["photoslop.text.bold"] = settings.bold ? "true" : "false";
    text_layer.metadata()["photoslop.text.italic"] = settings.italic ? "true" : "false";
    document().add_layer(std::move(text_layer));
  }
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
  statusBar()->showMessage(tr("Created text layer"));
}

void MainWindow::finish_active_text_editor() {
  if (canvas_ == nullptr) {
    return;
  }
  auto* editor = canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  if (editor == nullptr) {
    return;
  }
  const QPoint document_point(editor->property("photoslop.documentTextX").toInt(),
                              editor->property("photoslop.documentTextY").toInt());
  std::optional<LayerId> layer_id;
  if (editor->property("photoslop.editingLayerId").isValid()) {
    layer_id = static_cast<LayerId>(editor->property("photoslop.editingLayerId").toULongLong());
  }
  commit_text_editor(editor, document_point, layer_id);
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void MainWindow::apply_filter(const QString& identifier) {
  auto& doc = document();
  auto active = doc.active_layer_id();
  if (!active.has_value()) {
    return;
  }
  auto* layer = doc.find_layer(*active);
  if (layer == nullptr) {
    return;
  }

  try {
    push_undo_snapshot(tr("Filter"));
    const auto selection = canvas_->selected_document_region();
    const auto original_pixels = selection.isEmpty() ? PixelBuffer{} : layer->pixels();
    filters_.apply(identifier.toStdString(), layer->pixels());
    if (!selection.isEmpty() && !original_pixels.empty()) {
      auto& filtered = layer->pixels();
      const auto bounds = layer->bounds();
      const auto channels = filtered.format().channels;
      for (std::int32_t y = 0; y < filtered.height(); ++y) {
        for (std::int32_t x = 0; x < filtered.width(); ++x) {
          if (selection.contains(QPoint(bounds.x + x, bounds.y + y))) {
            continue;
          }
          auto* dst = filtered.pixel(x, y);
          const auto* src = original_pixels.pixel(x, y);
          std::copy(src, src + channels, dst);
        }
      }
    }
    canvas_->document_changed(to_qrect(layer->bounds()));
    statusBar()->showMessage(tr("Applied filter"));
  } catch (const std::exception& error) {
    QMessageBox::critical(this, tr("Filter failed"), QString::fromUtf8(error.what()));
  }
}

void MainWindow::levels_dialog() {
  const auto settings = request_levels_settings(this);
  if (!settings.has_value()) {
    return;
  }
  apply_levels_adjustment(settings->black_input, settings->white_input, settings->gamma_percent);
}

void MainWindow::apply_levels_adjustment(int black_input, int white_input, int gamma_percent) {
  auto& doc = document();
  const auto active = doc.active_layer_id();
  if (!active.has_value()) {
    return;
  }
  auto* layer = doc.find_layer(*active);
  if (layer == nullptr || layer->kind() != LayerKind::Pixel || layer->pixels().format().bit_depth != BitDepth::UInt8 ||
      layer->pixels().format().channels < 3) {
    statusBar()->showMessage(tr("Select an editable RGB pixel layer"));
    return;
  }

  black_input = std::clamp(black_input, 0, 254);
  white_input = std::clamp(white_input, black_input + 1, 255);
  gamma_percent = std::clamp(gamma_percent, 10, 999);
  if (black_input == 0 && white_input == 255 && gamma_percent == 100) {
    return;
  }

  push_undo_snapshot(tr("Levels"));
  auto& pixels = layer->pixels();
  const auto bounds = layer->bounds();
  const auto selection = canvas_->selected_document_region();
  const auto input_range = static_cast<double>(white_input - black_input);
  const auto gamma = static_cast<double>(gamma_percent) / 100.0;
  const auto inverse_gamma = gamma <= 0.0 ? 1.0 : 1.0 / gamma;

  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      if (!selection.isEmpty() && !selection.contains(QPoint(bounds.x + x, bounds.y + y))) {
        continue;
      }
      auto* px = pixels.pixel(x, y);
      for (std::uint16_t channel = 0; channel < 3; ++channel) {
        const auto normalized =
            std::clamp((static_cast<double>(px[channel]) - static_cast<double>(black_input)) / input_range, 0.0, 1.0);
        const auto corrected = std::pow(normalized, inverse_gamma);
        px[channel] = static_cast<std::uint8_t>(std::clamp(std::lround(corrected * 255.0), 0L, 255L));
      }
    }
  }

  canvas_->document_changed(to_qrect(bounds));
  statusBar()->showMessage(tr("Applied Levels"));
}

void MainWindow::curves_dialog() {
  const auto settings = request_curves_settings(this);
  if (!settings.has_value()) {
    return;
  }
  apply_curves_adjustment(settings->shadow_output, settings->midtone_output, settings->highlight_output);
}

void MainWindow::apply_curves_adjustment(int shadow_output, int midtone_output, int highlight_output) {
  auto& doc = document();
  const auto active = doc.active_layer_id();
  if (!active.has_value()) {
    return;
  }
  auto* layer = doc.find_layer(*active);
  if (layer == nullptr || layer->kind() != LayerKind::Pixel || layer->pixels().format().bit_depth != BitDepth::UInt8 ||
      layer->pixels().format().channels < 3) {
    statusBar()->showMessage(tr("Select an editable RGB pixel layer"));
    return;
  }

  shadow_output = std::clamp(shadow_output, 0, 255);
  midtone_output = std::clamp(midtone_output, 0, 255);
  highlight_output = std::clamp(highlight_output, 0, 255);
  if (shadow_output == 0 && midtone_output == 128 && highlight_output == 255) {
    return;
  }

  const auto map_value = [shadow_output, midtone_output, highlight_output](std::uint8_t value) {
    const auto input = static_cast<double>(value);
    double output = 0.0;
    if (input <= 128.0) {
      const auto t = input / 128.0;
      output = static_cast<double>(shadow_output) +
               (static_cast<double>(midtone_output) - static_cast<double>(shadow_output)) * t;
    } else {
      const auto t = (input - 128.0) / 127.0;
      output = static_cast<double>(midtone_output) +
               (static_cast<double>(highlight_output) - static_cast<double>(midtone_output)) * t;
    }
    return static_cast<std::uint8_t>(std::clamp(std::lround(output), 0L, 255L));
  };

  push_undo_snapshot(tr("Curves"));
  auto& pixels = layer->pixels();
  const auto bounds = layer->bounds();
  const auto selection = canvas_->selected_document_region();
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      if (!selection.isEmpty() && !selection.contains(QPoint(bounds.x + x, bounds.y + y))) {
        continue;
      }
      auto* px = pixels.pixel(x, y);
      px[0] = map_value(px[0]);
      px[1] = map_value(px[1]);
      px[2] = map_value(px[2]);
    }
  }

  canvas_->document_changed(to_qrect(bounds));
  statusBar()->showMessage(tr("Applied Curves"));
}

void MainWindow::hue_saturation_dialog() {
  const auto settings = request_hue_saturation_settings(this);
  if (!settings.has_value()) {
    return;
  }
  apply_hue_saturation_adjustment(settings->hue_shift, settings->saturation_delta, settings->lightness_delta);
}

void MainWindow::apply_hue_saturation_adjustment(int hue_shift, int saturation_delta, int lightness_delta) {
  auto& doc = document();
  const auto active = doc.active_layer_id();
  if (!active.has_value()) {
    return;
  }
  auto* layer = doc.find_layer(*active);
  if (layer == nullptr || layer->kind() != LayerKind::Pixel || layer->pixels().format().bit_depth != BitDepth::UInt8 ||
      layer->pixels().format().channels < 3) {
    statusBar()->showMessage(tr("Select an editable RGB pixel layer"));
    return;
  }

  hue_shift = std::clamp(hue_shift, -180, 180);
  saturation_delta = std::clamp(saturation_delta, -100, 100);
  lightness_delta = std::clamp(lightness_delta, -100, 100);
  if (hue_shift == 0 && saturation_delta == 0 && lightness_delta == 0) {
    return;
  }

  push_undo_snapshot(tr("Hue/Saturation"));
  auto& pixels = layer->pixels();
  const auto channels = pixels.format().channels;
  const auto bounds = layer->bounds();
  const auto selection = canvas_->selected_document_region();
  const auto saturation_offset = static_cast<int>(std::round(static_cast<double>(saturation_delta) * 255.0 / 100.0));
  const auto lightness_offset = static_cast<int>(std::round(static_cast<double>(lightness_delta) * 255.0 / 100.0));

  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      if (!selection.isEmpty() && !selection.contains(QPoint(bounds.x + x, bounds.y + y))) {
        continue;
      }
      auto* px = pixels.pixel(x, y);
      QColor color(px[0], px[1], px[2]);
      const auto original_alpha = channels >= 4 ? px[3] : 255;
      auto hue = color.hslHue();
      if (hue < 0) {
        hue = 0;
      }
      hue = (hue + hue_shift) % 360;
      if (hue < 0) {
        hue += 360;
      }
      const auto saturation = std::clamp(color.hslSaturation() + saturation_offset, 0, 255);
      const auto lightness = std::clamp(color.lightness() + lightness_offset, 0, 255);
      const auto adjusted = QColor::fromHsl(hue, saturation, lightness);
      px[0] = static_cast<std::uint8_t>(adjusted.red());
      px[1] = static_cast<std::uint8_t>(adjusted.green());
      px[2] = static_cast<std::uint8_t>(adjusted.blue());
      if (channels >= 4) {
        px[3] = original_alpha;
      }
    }
  }

  canvas_->document_changed(to_qrect(bounds));
  statusBar()->showMessage(tr("Applied Hue/Saturation"));
}

void MainWindow::color_balance_dialog() {
  const auto settings = request_color_balance_settings(this);
  if (!settings.has_value()) {
    return;
  }
  apply_color_balance_adjustment(settings->cyan_red, settings->magenta_green, settings->yellow_blue);
}

void MainWindow::apply_color_balance_adjustment(int cyan_red, int magenta_green, int yellow_blue) {
  auto& doc = document();
  const auto active = doc.active_layer_id();
  if (!active.has_value()) {
    return;
  }
  auto* layer = doc.find_layer(*active);
  if (layer == nullptr || layer->kind() != LayerKind::Pixel || layer->pixels().format().bit_depth != BitDepth::UInt8 ||
      layer->pixels().format().channels < 3) {
    statusBar()->showMessage(tr("Select an editable RGB pixel layer"));
    return;
  }

  cyan_red = std::clamp(cyan_red, -100, 100);
  magenta_green = std::clamp(magenta_green, -100, 100);
  yellow_blue = std::clamp(yellow_blue, -100, 100);
  if (cyan_red == 0 && magenta_green == 0 && yellow_blue == 0) {
    return;
  }

  push_undo_snapshot(tr("Color Balance"));
  auto& pixels = layer->pixels();
  const auto bounds = layer->bounds();
  const auto selection = canvas_->selected_document_region();
  const auto red_delta = static_cast<int>(std::round(static_cast<double>(cyan_red) * 255.0 / 100.0));
  const auto green_delta = static_cast<int>(std::round(static_cast<double>(magenta_green) * 255.0 / 100.0));
  const auto blue_delta = static_cast<int>(std::round(static_cast<double>(yellow_blue) * 255.0 / 100.0));
  const auto adjust = [](std::uint8_t value, int delta) {
    return static_cast<std::uint8_t>(std::clamp(static_cast<int>(value) + delta, 0, 255));
  };

  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      if (!selection.isEmpty() && !selection.contains(QPoint(bounds.x + x, bounds.y + y))) {
        continue;
      }
      auto* px = pixels.pixel(x, y);
      px[0] = adjust(px[0], red_delta);
      px[1] = adjust(px[1], green_delta);
      px[2] = adjust(px[2], blue_delta);
    }
  }

  canvas_->document_changed(to_qrect(bounds));
  statusBar()->showMessage(tr("Applied Color Balance"));
}

void MainWindow::add_layer() {
  auto& doc = document();
  const auto settings = request_new_layer_settings(this, static_cast<int>(doc.layers().size()) + 1);
  if (!settings.has_value()) {
    return;
  }

  push_undo_snapshot(tr("New layer"));
  auto layer_pixels =
      make_solid_pixels(doc.width(), doc.height(), QColor(0, 0, 0, 0), PixelFormat::rgba8());
  auto& layer = doc.add_pixel_layer(settings->name.toStdString(), std::move(layer_pixels));
  layer.set_opacity(static_cast<float>(settings->opacity) / 100.0F);
  layer.set_blend_mode(settings->blend_mode);
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
}

void MainWindow::create_layer_folder() {
  auto& doc = document();
  std::set<std::string> existing_names;
  std::function<void(const std::vector<Layer>&)> collect_names = [&](const std::vector<Layer>& layers) {
    for (const auto& layer : layers) {
      existing_names.insert(layer.name());
      collect_names(layer.children());
    }
  };
  collect_names(doc.layers());

  int suffix = 1;
  std::string name;
  do {
    name = tr("Folder %1").arg(suffix++).toStdString();
  } while (existing_names.contains(name));

  push_undo_snapshot(tr("New folder"));
  Layer folder(doc.allocate_layer_id(), name, LayerKind::Group);
  folder.set_blend_mode(BlendMode::PassThrough);
  doc.add_layer(std::move(folder));
  refresh_layer_list();
  refresh_layer_controls();
  refresh_document_info();
  canvas_->document_changed();
  statusBar()->showMessage(tr("Created folder"));
}

void MainWindow::layer_via_copy() {
  const auto ids = selected_or_active_layer_ids();
  const auto payload = collect_layer_copy_pixels(document(), ids, *canvas_);
  if (!payload.has_value()) {
    statusBar()->showMessage(tr("Nothing visible to copy to a new layer"));
    return;
  }

  auto& doc = document();
  push_undo_snapshot(tr("Layer via copy"));
  Layer copied(doc.allocate_layer_id(), tr("Layer Via Copy").toStdString(), payload->pixels);
  copied.set_bounds(Rect{payload->origin.x(), payload->origin.y(), copied.pixels().width(), copied.pixels().height()});
  doc.add_layer(std::move(copied));
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed(to_qrect(payload->document_rect));
  statusBar()->showMessage(tr("Copied selection to a new layer"));
}

void MainWindow::layer_via_cut() {
  const auto ids = selected_or_active_layer_ids();
  auto payload = collect_layer_copy_pixels(document(), ids, *canvas_);
  if (!payload.has_value()) {
    statusBar()->showMessage(tr("Nothing visible to cut to a new layer"));
    return;
  }

  auto& doc = document();
  push_undo_snapshot(tr("Layer via cut"));
  auto options = edit_options(*canvas_);
  Rect affected = payload->document_rect;
  const auto selected_rect = canvas_->selected_document_rect();
  for (const auto id : payload->source_layer_ids) {
    auto* layer = doc.find_layer(id);
    if (layer == nullptr || layer->kind() != LayerKind::Pixel || !layer->visible()) {
      continue;
    }
    options.lock_transparent_pixels = layer_locks_transparent_pixels(*layer);
    const auto clear_area = selected_rect.has_value() ? to_core_rect(*selected_rect) : layer->bounds();
    affected = unite_rect(affected, photoslop::clear_rect(doc, id, clear_area, options));
  }

  Layer cut_layer(doc.allocate_layer_id(), tr("Layer Via Cut").toStdString(), std::move(payload->pixels));
  cut_layer.set_bounds(Rect{payload->origin.x(), payload->origin.y(), cut_layer.pixels().width(), cut_layer.pixels().height()});
  doc.add_layer(std::move(cut_layer));
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed(to_qrect(affected));
  statusBar()->showMessage(tr("Cut selection to a new layer"));
}

void MainWindow::duplicate_active_layer() {
  const auto ids = selected_or_active_layer_ids();
  if (ids.empty()) {
    return;
  }

  auto& doc = document();
  push_undo_snapshot(tr("Duplicate layer"));
  for (const auto id : ids) {
    const auto* source = doc.find_layer(id);
    if (source == nullptr || source->kind() != LayerKind::Pixel) {
      continue;
    }

    Layer duplicate(doc.allocate_layer_id(), source->name() + " Copy", source->pixels());
    duplicate.set_opacity(source->opacity());
    duplicate.set_blend_mode(source->blend_mode());
    duplicate.set_visible(source->visible());
    duplicate.set_bounds(source->bounds());
    duplicate.metadata() = source->metadata();
    duplicate.unknown_psd_blocks() = source->unknown_psd_blocks();
    duplicate.layer_style() = source->layer_style();
    doc.add_layer(std::move(duplicate));
  }
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
}

void MainWindow::rename_active_layer() {
  auto& doc = document();
  if (!doc.active_layer_id().has_value()) {
    return;
  }
  auto* layer = doc.find_layer(*doc.active_layer_id());
  if (layer == nullptr) {
    return;
  }

  bool accepted = false;
  const auto new_name =
      QInputDialog::getText(this, tr("Rename Layer"), tr("Name"), QLineEdit::Normal,
                            QString::fromStdString(layer->name()), &accepted);
  if (!accepted || new_name.trimmed().isEmpty()) {
    return;
  }

  push_undo_snapshot(tr("Rename layer"));
  layer->set_name(new_name.trimmed().toStdString());
  refresh_layer_list();
  refresh_layer_controls();
}

void MainWindow::edit_active_layer_style() {
  auto& doc = document();
  if (!doc.active_layer_id().has_value()) {
    return;
  }
  const auto layer_id = *doc.active_layer_id();
  auto* layer = doc.find_layer(layer_id);
  if (layer == nullptr) {
    return;
  }

  const auto original_opacity = layer->opacity();
  const auto original_blend_mode = layer->blend_mode();
  const auto original_style = layer->layer_style();
  auto apply_settings = [this, &doc, layer_id](const LayerStyleSettings& settings) {
    auto* target = doc.find_layer(layer_id);
    if (target == nullptr) {
      return;
    }
    const auto before = layer_render_bounds(*target);
    target->set_opacity(static_cast<float>(settings.opacity) / 100.0F);
    target->set_blend_mode(settings.blend_mode);
    target->layer_style() = settings.style;
    const auto after = layer_render_bounds(*target);
    if (canvas_ != nullptr) {
      canvas_->document_changed(to_qrect(unite_rect(before, after)));
    }
  };
  auto restore_original = [this, &doc, layer_id, original_opacity, original_blend_mode, original_style] {
    auto* target = doc.find_layer(layer_id);
    if (target == nullptr) {
      return;
    }
    const auto before = layer_render_bounds(*target);
    target->set_opacity(original_opacity);
    target->set_blend_mode(original_blend_mode);
    target->layer_style() = original_style;
    const auto after = layer_render_bounds(*target);
    if (canvas_ != nullptr) {
      canvas_->document_changed(to_qrect(unite_rect(before, after)));
    }
  };

  const auto settings = request_layer_style_settings(this, *layer, apply_settings);
  if (!settings.has_value()) {
    restore_original();
    refresh_layer_list();
    refresh_layer_controls();
    return;
  }

  restore_original();
  push_undo_snapshot(tr("Layer style"));
  apply_settings(*settings);
  refresh_layer_list();
  refresh_layer_controls();
  statusBar()->showMessage(tr("Updated layer style"));
}

void MainWindow::delete_active_layer() {
  const auto ids = selected_or_active_layer_ids();
  if (ids.empty()) {
    return;
  }
  auto& doc = document();
  push_undo_snapshot(tr("Delete layer"));
  for (const auto id : ids) {
    doc.remove_layer(id);
  }
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
}

void MainWindow::move_active_layer(int direction) {
  const auto ids = selected_or_active_layer_ids();
  if (ids.empty() || direction == 0) {
    return;
  }

  auto& layers = document().layers();
  const std::set<LayerId> selected(ids.begin(), ids.end());
  push_undo_snapshot(tr("Move layer"));
  if (direction > 0) {
    for (int index = static_cast<int>(layers.size()) - 2; index >= 0; --index) {
      if (selected.contains(layers[static_cast<std::size_t>(index)].id()) &&
          !selected.contains(layers[static_cast<std::size_t>(index + 1)].id())) {
        std::iter_swap(layers.begin() + index, layers.begin() + index + 1);
      }
    }
  } else {
    for (int index = 1; index < static_cast<int>(layers.size()); ++index) {
      if (selected.contains(layers[static_cast<std::size_t>(index)].id()) &&
          !selected.contains(layers[static_cast<std::size_t>(index - 1)].id())) {
        std::iter_swap(layers.begin() + index, layers.begin() + index - 1);
      }
    }
  }
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
}

void MainWindow::handle_layer_drop() {
  auto* list = dynamic_cast<LayerListWidget*>(layer_list_);
  if (list == nullptr) {
    reorder_layers_from_list();
    return;
  }

  auto request = list->take_drop_request();
  if (!request.has_value()) {
    reorder_layers_from_list();
    return;
  }

  auto& doc = document();
  auto trial_layers = doc.layers();
  const auto before_signature = layer_tree_signature(doc.layers());
  if (!move_layers_for_drop(trial_layers, *request) || layer_tree_signature(trial_layers) == before_signature) {
    refresh_layer_list();
    return;
  }

  push_undo_snapshot(tr("Reorder layers"));
  doc.layers() = std::move(trial_layers);
  if (request->position == LayerDropPosition::OnItem && request->target_layer_id.has_value()) {
    if (const auto* target = doc.find_layer(*request->target_layer_id);
        target != nullptr && target->kind() == LayerKind::Group) {
      session().collapsed_layer_groups.erase(*request->target_layer_id);
    }
  }
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
  statusBar()->showMessage(tr("Reordered layers"));
}

void MainWindow::reorder_layers_from_list() {
  if (updating_layer_list_ || layer_list_ == nullptr) {
    return;
  }

  auto& layers = document().layers();
  if (layer_list_->count() != static_cast<int>(layers.size())) {
    refresh_layer_list();
    return;
  }

  std::vector<LayerId> top_to_bottom;
  top_to_bottom.reserve(static_cast<std::size_t>(layer_list_->count()));
  std::set<LayerId> seen_ids;
  for (int row = 0; row < layer_list_->count(); ++row) {
    const auto id = static_cast<LayerId>(layer_list_->item(row)->data(kLayerIdRole).toULongLong());
    if (id == 0 || seen_ids.contains(id)) {
      refresh_layer_list();
      return;
    }
    seen_ids.insert(id);
    top_to_bottom.push_back(id);
  }

  std::vector<LayerId> current_top_to_bottom;
  current_top_to_bottom.reserve(layers.size());
  for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
    current_top_to_bottom.push_back(it->id());
  }
  if (top_to_bottom == current_top_to_bottom) {
    return;
  }
  for (const auto id : top_to_bottom) {
    if (std::find_if(layers.begin(), layers.end(), [id](const Layer& layer) { return layer.id() == id; }) ==
        layers.end()) {
      refresh_layer_list();
      return;
    }
  }

  push_undo_snapshot(tr("Reorder layers"));
  auto old_layers = std::move(layers);
  std::vector<Layer> reordered;
  reordered.reserve(old_layers.size());
  for (auto id_it = top_to_bottom.rbegin(); id_it != top_to_bottom.rend(); ++id_it) {
    const auto found = std::find_if(old_layers.begin(), old_layers.end(), [id = *id_it](const Layer& layer) {
      return layer.id() == id;
    });
    if (found == old_layers.end()) {
      layers = std::move(old_layers);
      refresh_layer_list();
      return;
    }
    reordered.push_back(std::move(*found));
    old_layers.erase(found);
  }
  if (!old_layers.empty()) {
    layers = std::move(old_layers);
    refresh_layer_list();
    return;
  }
  layers = std::move(reordered);
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
  statusBar()->showMessage(tr("Reordered layers"));
}

void MainWindow::toggle_layer_folder_expanded(LayerId id) {
  const auto* layer = document().find_layer(id);
  if (layer == nullptr || layer->kind() != LayerKind::Group || layer->children().empty()) {
    return;
  }

  auto& collapsed_groups = session().collapsed_layer_groups;
  const auto was_collapsed = collapsed_groups.contains(id);
  if (was_collapsed) {
    collapsed_groups.erase(id);
  } else {
    collapsed_groups.insert(id);
  }

  refresh_layer_list();
  statusBar()->showMessage(was_collapsed ? tr("Folder expanded") : tr("Folder collapsed"));
}

void MainWindow::reveal_layer_in_layer_list(LayerId id) {
  if (layer_list_ == nullptr) {
    return;
  }

  std::vector<LayerId> ancestors;
  collect_layer_ancestor_groups(document().layers(), id, ancestors);
  for (const auto ancestor_id : ancestors) {
    session().collapsed_layer_groups.erase(ancestor_id);
  }

  refresh_layer_list();
  for (int row = 0; row < layer_list_->count(); ++row) {
    auto* item = layer_list_->item(row);
    if (item == nullptr || static_cast<LayerId>(item->data(kLayerIdRole).toULongLong()) != id) {
      continue;
    }
    layer_list_->setCurrentItem(item, QItemSelectionModel::ClearAndSelect);
    layer_list_->scrollToItem(item, QAbstractItemView::PositionAtCenter);
    restyle_layer_rows(layer_list_);
    break;
  }
}

void MainWindow::set_layer_visibility_from_item(QListWidgetItem* item) {
  if (updating_layer_list_ || item == nullptr) {
    return;
  }
  const auto id = static_cast<LayerId>(item->data(kLayerIdRole).toULongLong());
  auto* layer = document().find_layer(id);
  if (layer == nullptr) {
    return;
  }

  const bool visible = item->checkState() == Qt::Checked;
  if (layer->visible() == visible) {
    return;
  }

  layer->set_visible(visible);
  const auto is_group = layer->kind() == LayerKind::Group;
  item->setForeground(visible ? QBrush(QColor(226, 230, 237)) : QBrush(QColor(126, 132, 142)));
  if (auto* row_widget = layer_list_ != nullptr ? layer_list_->itemWidget(item) : nullptr; row_widget != nullptr) {
    if (auto* visibility = row_widget->findChild<QToolButton*>(QStringLiteral("layerVisibilityCheck"));
        visibility != nullptr) {
      QSignalBlocker visibility_blocker(visibility);
      visibility->setChecked(visible);
      visibility->setText(layer_visibility_indicator_text(visible));
      visibility->setToolTip(layer_visibility_tooltip(visible));
    }
    if (auto* name = row_widget->findChild<QLabel*>(QStringLiteral("layerRowName")); name != nullptr) {
      name->setEnabled(visible);
    }
    if (auto* details = row_widget->findChild<QLabel*>(QStringLiteral("layerRowDetails")); details != nullptr) {
      details->setEnabled(visible);
    }
  }
  canvas_->document_changed(to_qrect(layer_render_bounds(*layer)));
  refresh_layer_controls();
  if (is_group) {
    refresh_layer_list();
  } else {
    restyle_layer_rows(layer_list_);
  }
  statusBar()->showMessage(visible ? tr("Layer shown") : tr("Layer hidden"));
}

void MainWindow::show_layer_context_menu(QPoint position) {
  if (layer_list_ == nullptr) {
    return;
  }

  auto* item = layer_list_->itemAt(position);
  if (item != nullptr && !item->isSelected()) {
    layer_list_->clearSelection();
    layer_list_->setCurrentItem(item);
    item->setSelected(true);
  }

  const auto ids = selected_or_active_layer_ids();
  const auto has_layer = !ids.empty();
  const auto active_id = document().active_layer_id();
  auto* active_layer = active_id.has_value() ? document().find_layer(*active_id) : nullptr;

  QMenu menu(this);
  menu.setObjectName(QStringLiteral("layerContextMenu"));
  if (layer_blending_options_action_ != nullptr) {
    layer_blending_options_action_->setEnabled(active_layer != nullptr);
    menu.addAction(layer_blending_options_action_);
    menu.addSeparator();
  }
  auto* new_action = menu.addAction(simple_icon(QStringLiteral("new")), tr("New Layer"));
  auto* new_folder_action = menu.addAction(simple_icon(QStringLiteral("dir"), QColor(245, 205, 105)), tr("New Folder"));
  auto* duplicate_action = menu.addAction(simple_icon(QStringLiteral("dup")), tr("Duplicate Layer"));
  auto* rename_action = menu.addAction(simple_icon(QStringLiteral("RN")), tr("Rename Layer..."));
  auto* delete_action = menu.addAction(simple_icon(QStringLiteral("trash")), tr("Delete Layer"));
  menu.addSeparator();
  auto* merge_selected_action =
      menu.addAction(simple_icon(QStringLiteral("merge"), QColor(160, 220, 255)), tr("Merge Selected to New Layer"));
  auto* merge_visible_action = menu.addAction(simple_icon(QStringLiteral("merge")), tr("Merge Visible to New Layer"));
  menu.addSeparator();
  auto* visibility_action = menu.addAction(tr("Visible"));
  visibility_action->setCheckable(true);
  visibility_action->setChecked(active_layer == nullptr || active_layer->visible());
  auto* lock_action = menu.addAction(tr("Lock Transparent Pixels"));
  lock_action->setCheckable(true);
  lock_action->setChecked(active_layer != nullptr && layer_locks_transparent_pixels(*active_layer));
  auto* select_opaque_action = menu.addAction(tr("Load Layer Transparency"));

  duplicate_action->setEnabled(has_layer);
  rename_action->setEnabled(active_layer != nullptr);
  delete_action->setEnabled(has_layer);
  merge_selected_action->setEnabled(has_layer);
  visibility_action->setEnabled(has_layer);
  lock_action->setEnabled(has_layer);
  select_opaque_action->setEnabled(active_layer != nullptr && canvas_ != nullptr);

  auto* chosen = menu.exec(layer_list_->viewport()->mapToGlobal(position));
  if (chosen == nullptr) {
    return;
  }
  if (chosen == layer_blending_options_action_) {
    return;
  }
  if (chosen == new_action) {
    add_layer();
  } else if (chosen == new_folder_action) {
    create_layer_folder();
  } else if (chosen == duplicate_action) {
    duplicate_active_layer();
  } else if (chosen == rename_action) {
    rename_active_layer();
  } else if (chosen == delete_action) {
    delete_active_layer();
  } else if (chosen == merge_selected_action) {
    merge_selected_to_new_layer();
  } else if (chosen == merge_visible_action) {
    merge_visible_to_new_layer();
  } else if (chosen == visibility_action) {
    set_active_layer_visible(visibility_action->isChecked());
  } else if (chosen == lock_action) {
    set_active_layer_lock_transparency(lock_action->isChecked());
  } else if (chosen == select_opaque_action && active_layer != nullptr && canvas_ != nullptr) {
    canvas_->select_layer_opaque_pixels(active_layer->id());
  }
}

void MainWindow::merge_visible_to_new_layer() {
  auto& doc = document();
  push_undo_snapshot(tr("Merge visible"));
  auto merged = Compositor{}.flatten_rgb8(doc);
  doc.add_pixel_layer(tr("Merged Visible").toStdString(), std::move(merged));
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
  statusBar()->showMessage(tr("Merged visible layers to a new layer"));
}

void MainWindow::merge_selected_to_new_layer() {
  const auto ids = selected_or_active_layer_ids();
  if (ids.empty()) {
    return;
  }

  auto& doc = document();
  Document selected_document(doc.width(), doc.height(), doc.format());
  for (const auto& layer : doc.layers()) {
    if (std::find(ids.begin(), ids.end(), layer.id()) != ids.end()) {
      selected_document.add_layer(layer);
    }
  }
  if (selected_document.layers().empty()) {
    return;
  }

  push_undo_snapshot(tr("Merge selected"));
  auto merged = Compositor{}.flatten_rgb8(selected_document);
  doc.add_pixel_layer(tr("Merged Selected").toStdString(), std::move(merged));
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
  statusBar()->showMessage(tr("Merged selected layers to a new layer"));
}

void MainWindow::fill_active_layer() {
  fill_active_layer_with_color(canvas_->primary_color(), tr("Fill"));
}

void MainWindow::fill_active_layer_with_color(QColor color, QString label) {
  const auto ids = selected_or_active_layer_ids();
  if (ids.empty()) {
    return;
  }

  auto& doc = document();
  push_undo_snapshot(label);
  auto options = edit_options(*canvas_);
  options.primary = edit_color(color);
  Rect affected;
  for (const auto id : ids) {
    auto* layer = doc.find_layer(id);
    if (layer == nullptr || layer->kind() != LayerKind::Pixel) {
      continue;
    }
    options.lock_transparent_pixels = layer_locks_transparent_pixels(*layer);
    const auto target = canvas_->has_selection() && canvas_->selected_document_rect().has_value()
                            ? to_core_rect(*canvas_->selected_document_rect())
                            : layer->bounds();
    affected = unite_rect(affected, photoslop::fill_rect(doc, id, target, options));
  }
  if (!affected.empty()) {
    canvas_->document_changed(to_qrect(affected));
  }
}

void MainWindow::clear_active_layer() {
  const auto ids = selected_or_active_layer_ids();
  if (ids.empty()) {
    return;
  }

  auto& doc = document();
  push_undo_snapshot(tr("Clear"));
  Rect affected;
  auto options = edit_options(*canvas_);
  for (const auto id : ids) {
    auto* layer = doc.find_layer(id);
    if (layer == nullptr || layer->kind() != LayerKind::Pixel) {
      continue;
    }
    options.lock_transparent_pixels = layer_locks_transparent_pixels(*layer);
    affected = unite_rect(affected, photoslop::clear_rect(doc, id, layer->bounds(), options));
  }
  if (!affected.empty()) {
    canvas_->document_changed(to_qrect(affected));
  }
}

void MainWindow::stroke_selection() {
  auto& doc = document();
  const auto active = doc.active_layer_id();
  if (!active.has_value()) {
    return;
  }
  const auto selection = canvas_->selected_document_region();
  if (selection.isEmpty()) {
    statusBar()->showMessage(tr("Make a selection before stroking"));
    return;
  }
  auto* layer = doc.find_layer(*active);
  if (layer == nullptr || layer->kind() != LayerKind::Pixel) {
    statusBar()->showMessage(tr("Select an editable pixel layer first"));
    return;
  }

  push_undo_snapshot(tr("Stroke selection"));
  auto options = edit_options(*canvas_);
  options.lock_transparent_pixels = layer_locks_transparent_pixels(*layer);
  const QRect canvas_rect(0, 0, doc.width(), doc.height());
  const auto stroke_region = selection_outline_region(selection, canvas_->brush_size(), canvas_rect);
  if (stroke_region.isEmpty()) {
    return;
  }
  options.selection = to_core_rect(stroke_region.boundingRect());
  options.selection_mask = [stroke_region](std::int32_t x, std::int32_t y) { return stroke_region.contains(QPoint(x, y)); };
  const auto affected = photoslop::fill_rect(doc, *active, to_core_rect(stroke_region.boundingRect()), options);
  if (!affected.empty()) {
    canvas_->document_changed(to_qrect(affected));
  }
  statusBar()->showMessage(tr("Stroked selection"));
}

void MainWindow::expand_selection_dialog() {
  if (!canvas_->has_selection()) {
    statusBar()->showMessage(tr("Make a selection before expanding"));
    return;
  }
  bool accepted = false;
  const auto pixels = QInputDialog::getInt(this, tr("Expand Selection"), tr("Expand by"), 4, 1, 250, 1, &accepted);
  if (accepted) {
    canvas_->expand_selection(pixels);
  }
}

void MainWindow::contract_selection_dialog() {
  if (!canvas_->has_selection()) {
    statusBar()->showMessage(tr("Make a selection before contracting"));
    return;
  }
  bool accepted = false;
  const auto pixels = QInputDialog::getInt(this, tr("Contract Selection"), tr("Contract by"), 4, 1, 250, 1, &accepted);
  if (accepted) {
    canvas_->contract_selection(pixels);
  }
}

void MainWindow::border_selection_dialog() {
  if (!canvas_->has_selection()) {
    statusBar()->showMessage(tr("Make a selection before selecting a border"));
    return;
  }
  bool accepted = false;
  const auto pixels = QInputDialog::getInt(this, tr("Border Selection"), tr("Width"), 4, 1, 250, 1, &accepted);
  if (accepted) {
    canvas_->border_selection(pixels);
  }
}

void MainWindow::flip_active_layer_horizontal() {
  const auto ids = selected_or_active_layer_ids();
  if (ids.empty()) {
    return;
  }

  auto& doc = document();
  push_undo_snapshot(tr("Flip horizontal"));
  Rect affected;
  for (const auto id : ids) {
    affected = unite_rect(affected, photoslop::flip_layer_horizontal(doc, id));
  }
  canvas_->document_changed(to_qrect(affected));
  refresh_layer_list();
  refresh_layer_controls();
}

void MainWindow::flip_active_layer_vertical() {
  const auto ids = selected_or_active_layer_ids();
  if (ids.empty()) {
    return;
  }

  auto& doc = document();
  push_undo_snapshot(tr("Flip vertical"));
  Rect affected;
  for (const auto id : ids) {
    affected = unite_rect(affected, photoslop::flip_layer_vertical(doc, id));
  }
  canvas_->document_changed(to_qrect(affected));
}

void MainWindow::crop_to_selection() {
  const auto selection = canvas_->selected_document_rect();
  if (!selection.has_value() || selection->isEmpty()) {
    statusBar()->showMessage(tr("Make a rectangular selection before cropping"));
    return;
  }

  push_undo_snapshot(tr("Crop"));
  auto& doc = document();
  if (!photoslop::crop_document(doc, to_core_rect(*selection))) {
    return;
  }
  canvas_->clear_selection();
  canvas_->set_document(&doc);
  refresh_layer_list();
  refresh_layer_controls();
  refresh_document_info();
  statusBar()->showMessage(tr("Cropped to selection"));
}

void MainWindow::rotate_canvas_clockwise() {
  auto& doc = document();
  push_undo_snapshot(tr("Rotate canvas"));
  photoslop::rotate_document_clockwise(doc);
  canvas_->clear_selection();
  canvas_->set_document(&doc);
  refresh_layer_list();
  refresh_layer_controls();
  refresh_document_info();
  statusBar()->showMessage(tr("Rotated canvas clockwise"));
}

void MainWindow::rotate_canvas_counterclockwise() {
  auto& doc = document();
  push_undo_snapshot(tr("Rotate canvas"));
  photoslop::rotate_document_counterclockwise(doc);
  canvas_->clear_selection();
  canvas_->set_document(&doc);
  refresh_layer_list();
  refresh_layer_controls();
  refresh_document_info();
  statusBar()->showMessage(tr("Rotated canvas counterclockwise"));
}

std::vector<LayerId> MainWindow::selected_layer_ids() const {
  std::vector<LayerId> ids;
  if (layer_list_ == nullptr) {
    return ids;
  }
  const auto selected = layer_list_->selectedItems();
  ids.reserve(static_cast<std::size_t>(selected.size()));
  for (const auto* item : selected) {
    ids.push_back(static_cast<LayerId>(item->data(kLayerIdRole).toULongLong()));
  }
  return ids;
}

std::vector<LayerId> MainWindow::selected_or_active_layer_ids() const {
  auto ids = selected_layer_ids();
  const auto active = document().active_layer_id();
  if (ids.empty() && active.has_value()) {
    ids.push_back(*active);
  }
  return ids;
}

void MainWindow::set_active_layer_from_selection() {
  if (updating_layer_controls_) {
    return;
  }
  if (canvas_ != nullptr) {
    canvas_->set_selected_layer_ids(selected_layer_ids());
  }
  if (layer_list_->currentItem() == nullptr) {
    return;
  }

  const auto id = static_cast<LayerId>(layer_list_->currentItem()->data(kLayerIdRole).toULongLong());
  auto& doc = document();
  if (doc.find_layer(id) != nullptr) {
    doc.set_active_layer(id);
    refresh_layer_controls();
    restyle_layer_rows(layer_list_);
  }
}

void MainWindow::set_active_layer_opacity(int value) {
  if (updating_layer_controls_) {
    return;
  }
  const auto ids = selected_or_active_layer_ids();
  if (ids.empty()) {
    return;
  }
  auto& doc = document();
  push_undo_snapshot(tr("Opacity"));
  Rect affected;
  for (const auto id : ids) {
    auto* layer = doc.find_layer(id);
    if (layer == nullptr) {
      continue;
    }
    layer->set_opacity(static_cast<float>(value) / 100.0F);
    affected = unite_rect(affected, layer_render_bounds(*layer));
  }
  canvas_->document_changed(to_qrect(affected));
}

void MainWindow::set_active_layer_blend(int index) {
  if (updating_layer_controls_ || index < 0) {
    return;
  }
  const auto ids = selected_or_active_layer_ids();
  if (ids.empty()) {
    return;
  }
  auto& doc = document();
  push_undo_snapshot(tr("Blend mode"));
  Rect affected;
  for (const auto id : ids) {
    auto* layer = doc.find_layer(id);
    if (layer == nullptr) {
      continue;
    }
    layer->set_blend_mode(static_cast<BlendMode>(blend_combo_->itemData(index).toInt()));
    affected = unite_rect(affected, layer_render_bounds(*layer));
  }
  canvas_->document_changed(to_qrect(affected));
}

void MainWindow::set_active_layer_visible(bool visible) {
  if (updating_layer_controls_) {
    return;
  }
  const auto ids = selected_or_active_layer_ids();
  if (ids.empty()) {
    return;
  }
  auto& doc = document();
  push_undo_snapshot(tr("Visibility"));
  Rect affected;
  for (const auto id : ids) {
    auto* layer = doc.find_layer(id);
    if (layer == nullptr) {
      continue;
    }
    layer->set_visible(visible);
    affected = unite_rect(affected, layer_render_bounds(*layer));
  }
  canvas_->document_changed(to_qrect(affected));
  refresh_layer_list();
  refresh_layer_controls();
}

void MainWindow::set_active_layer_lock_transparency(bool locked) {
  if (updating_layer_controls_) {
    return;
  }
  const auto ids = selected_or_active_layer_ids();
  if (ids.empty()) {
    return;
  }
  auto& doc = document();
  push_undo_snapshot(tr("Lock transparency"));
  for (const auto id : ids) {
    auto* layer = doc.find_layer(id);
    if (layer == nullptr) {
      continue;
    }
    set_layer_locks_transparent_pixels(*layer, locked);
  }
  refresh_layer_list();
  refresh_layer_controls();
  statusBar()->showMessage(locked ? tr("Transparent pixels locked") : tr("Transparent pixels unlocked"));
}

void MainWindow::undo() {
  auto& active_session = session();
  if (active_session.undo_stack.empty()) {
    return;
  }
  active_session.redo_stack.push_back(active_session.document);
  active_session.document = active_session.undo_stack.back();
  active_session.undo_stack.pop_back();
  canvas_->set_document(&active_session.document);
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
  statusBar()->showMessage(tr("Undo"));
  update_history(tr("Undo"));
  update_undo_redo_actions();
}

void MainWindow::redo() {
  auto& active_session = session();
  if (active_session.redo_stack.empty()) {
    return;
  }
  active_session.undo_stack.push_back(active_session.document);
  active_session.document = active_session.redo_stack.back();
  active_session.redo_stack.pop_back();
  canvas_->set_document(&active_session.document);
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
  statusBar()->showMessage(tr("Redo"));
  update_history(tr("Redo"));
  update_undo_redo_actions();
}

void MainWindow::push_undo_snapshot(QString label) {
  constexpr std::size_t kMaxUndo = 40;
  auto& active_session = session();
  active_session.undo_stack.push_back(active_session.document);
  if (active_session.undo_stack.size() > kMaxUndo) {
    active_session.undo_stack.erase(active_session.undo_stack.begin());
  }
  active_session.redo_stack.clear();
  update_history(label);
  update_undo_redo_actions();
  statusBar()->showMessage(label);
}

void MainWindow::refresh_layer_list() {
  if (layer_list_ == nullptr) {
    return;
  }
  const auto scroll_value = layer_list_->verticalScrollBar() != nullptr ? layer_list_->verticalScrollBar()->value() : 0;
  updating_layer_list_ = true;
  QSignalBlocker blocker(layer_list_);
  layer_list_->clear();

  const auto& doc = document();
  auto& collapsed_groups = session().collapsed_layer_groups;
  std::set<LayerId> current_group_ids;
  collect_layer_group_ids(doc.layers(), current_group_ids);
  for (auto collapsed = collapsed_groups.begin(); collapsed != collapsed_groups.end();) {
    if (!current_group_ids.contains(*collapsed)) {
      collapsed = collapsed_groups.erase(collapsed);
    } else {
      ++collapsed;
    }
  }

  const auto active = doc.active_layer_id();
  int row_to_select = -1;
  std::function<void(const std::vector<Layer>&, int, bool)> append_layers =
      [&](const std::vector<Layer>& layers, int depth, bool ancestors_visible) {
    for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
      const auto effective_visible = ancestors_visible && it->visible();
      const auto is_group = it->kind() == LayerKind::Group;
      const auto group_expanded = !is_group || !collapsed_groups.contains(it->id());
      auto* item = new QListWidgetItem(QString::fromStdString(it->name()), layer_list_);
      item->setData(kLayerIdRole, QVariant::fromValue<qulonglong>(static_cast<qulonglong>(it->id())));
      item->setData(kLayerDepthRole, depth);
      item->setData(kLayerIsGroupRole, is_group);
      item->setData(kLayerGroupExpandedRole, group_expanded);
      item->setFlags((item->flags() & ~Qt::ItemIsUserCheckable) | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
      item->setCheckState(it->visible() ? Qt::Checked : Qt::Unchecked);
      const auto folder_detail =
          is_group ? tr("\nFolder with %1 layers%2")
                         .arg(layer_descendant_count(*it))
                         .arg(group_expanded ? QString() : tr("\nCollapsed"))
                   : QString();
      item->setToolTip(tr("%1\n%2% opacity%3%4")
                           .arg(QString::fromStdString(it->name()))
                           .arg(std::round(it->opacity() * 100.0F))
                           .arg(!ancestors_visible
                                    ? tr("\nHidden by parent folder")
                                    : layer_locks_transparent_pixels(*it) ? tr("\nTransparent pixels locked") : QString())
                           .arg(folder_detail));
      item->setSizeHint(QSize(0, 50));
      item->setForeground(effective_visible ? QBrush(QColor(226, 230, 237)) : QBrush(QColor(126, 132, 142)));
      if (active.has_value() && *active == it->id()) {
        auto font = item->font();
        font.setBold(true);
        item->setFont(font);
        row_to_select = layer_list_->row(item);
      }
      layer_list_->setItemWidget(
          item, make_layer_row_widget(*it, item, layer_list_, depth, ancestors_visible, group_expanded,
                                      [this](LayerId layer_id) { toggle_layer_folder_expanded(layer_id); }));
      if (is_group && group_expanded) {
        append_layers(it->children(), depth + 1, effective_visible);
      }
    }
  };
  append_layers(doc.layers(), 0, true);

  if (row_to_select >= 0) {
    layer_list_->setCurrentRow(row_to_select);
  }
  updating_layer_list_ = false;
  if (canvas_ != nullptr) {
    canvas_->set_selected_layer_ids(selected_layer_ids());
  }
  restyle_layer_rows(layer_list_);
  if (auto* scroll_bar = layer_list_->verticalScrollBar(); scroll_bar != nullptr) {
    scroll_bar->setValue(std::clamp(scroll_value, scroll_bar->minimum(), scroll_bar->maximum()));
  }
  layer_list_->viewport()->update();
  layer_list_->viewport()->repaint();
}

void MainWindow::refresh_layer_controls() {
  updating_layer_controls_ = true;
  const auto reset = [this] {
    if (visible_check_ != nullptr) {
      visible_check_->setChecked(true);
    }
    if (opacity_slider_ != nullptr) {
      opacity_slider_->setValue(100);
    }
    if (opacity_spin_ != nullptr) {
      opacity_spin_->setValue(100);
    }
    if (blend_combo_ != nullptr) {
      blend_combo_->setCurrentIndex(0);
    }
    if (lock_transparency_check_ != nullptr) {
      lock_transparency_check_->setChecked(false);
    }
    if (layer_blending_options_action_ != nullptr) {
      layer_blending_options_action_->setEnabled(false);
    }
  };

  const auto& doc = document();
  const auto active = doc.active_layer_id();
  if (!active.has_value()) {
    reset();
    updating_layer_controls_ = false;
    return;
  }

  const auto* layer = doc.find_layer(*active);
  if (layer == nullptr) {
    reset();
    updating_layer_controls_ = false;
    return;
  }

  if (opacity_slider_ != nullptr) {
    opacity_slider_->setValue(static_cast<int>(std::round(layer->opacity() * 100.0F)));
  }
  if (opacity_spin_ != nullptr) {
    opacity_spin_->setValue(static_cast<int>(std::round(layer->opacity() * 100.0F)));
  }
  if (visible_check_ != nullptr) {
    visible_check_->setChecked(layer->visible());
  }
  if (blend_combo_ != nullptr) {
    const auto blend_value = static_cast<int>(layer->blend_mode());
    const auto index = blend_combo_->findData(blend_value);
    blend_combo_->setCurrentIndex(index >= 0 ? index : 0);
  }
  if (lock_transparency_check_ != nullptr) {
    lock_transparency_check_->setChecked(layer_locks_transparent_pixels(*layer));
  }
  if (layer_blending_options_action_ != nullptr) {
    layer_blending_options_action_->setEnabled(true);
  }
  updating_layer_controls_ = false;
}

void MainWindow::refresh_document_info() {
  if (document_info_label_ == nullptr) {
    return;
  }

  const auto& doc = document();
  document_info_label_->setText(tr("%1 x %2 px\n%3 layers\nZoom %4%")
                                    .arg(doc.width())
                                    .arg(doc.height())
                                    .arg(layer_tree_count(doc.layers()))
                                    .arg(static_cast<int>(std::round(canvas_->zoom() * 100.0))));
}

void MainWindow::update_canvas_info(CanvasInfoState info) {
  if (canvas_info_label_ == nullptr) {
    return;
  }

  if (!info.inside_document) {
    canvas_info_label_->setText(tr("X: -\nY: -\nRGB: -\nRect: -"));
    return;
  }

  const auto color = info.color;
  const auto color_line = tr("RGB: %1, %2, %3  #%4%5%6")
                              .arg(color.red())
                              .arg(color.green())
                              .arg(color.blue())
                              .arg(color.red(), 2, 16, QLatin1Char('0'))
                              .arg(color.green(), 2, 16, QLatin1Char('0'))
                              .arg(color.blue(), 2, 16, QLatin1Char('0'))
                              .toUpper();
  QString rect_line = tr("Rect: -");
  if (info.active_rect.has_value() && !info.active_rect->isEmpty()) {
    const auto rect = info.active_rect->normalized();
    rect_line = tr("%1: %2 x %3  at %4, %5")
                    .arg(info.active_rect_label.isEmpty() ? tr("Rect") : info.active_rect_label)
                    .arg(rect.width())
                    .arg(rect.height())
                    .arg(rect.x())
                    .arg(rect.y());
  }

  canvas_info_label_->setText(tr("X: %1\nY: %2\n%3\n%4")
                                  .arg(info.document_point.x())
                                  .arg(info.document_point.y())
                                  .arg(color_line)
                                  .arg(rect_line));
}

void MainWindow::choose_primary_color() {
  show_color_panel(true);
}

void MainWindow::choose_secondary_color() {
  show_color_panel(false);
}

void MainWindow::show_color_panel(bool foreground) {
  if (canvas_ == nullptr) {
    return;
  }
  if (color_dialog_ != nullptr) {
    if (color_dialog_->property("photoslop.colorTarget").toBool() == foreground) {
      color_dialog_->show();
      color_dialog_->raise();
      color_dialog_->activateWindow();
      return;
    }
    color_dialog_->close();
  }

  auto* dialog = create_photoslop_color_panel(
      this, foreground ? canvas_->primary_color() : canvas_->secondary_color(),
      foreground ? tr("Foreground Color") : tr("Background Color"),
      [this, foreground](QColor color) {
        color.setAlpha(255);
        if (foreground) {
          canvas_->set_primary_color(color);
          statusBar()->showMessage(tr("Foreground color changed"));
        } else {
          canvas_->set_secondary_color(color);
          statusBar()->showMessage(tr("Background color changed"));
        }
        refresh_color_buttons();
      });
  dialog->setProperty("photoslop.colorTarget", foreground);
  color_dialog_ = dialog;
  connect(dialog, &QObject::destroyed, this, [this, dialog] {
    if (color_dialog_ == dialog) {
      color_dialog_ = nullptr;
    }
  });
  dialog->show();
  dialog->raise();
  dialog->activateWindow();
}

void MainWindow::swap_colors() {
  const auto primary = canvas_->primary_color();
  canvas_->set_primary_color(canvas_->secondary_color());
  canvas_->set_secondary_color(primary);
  refresh_color_buttons();
  statusBar()->showMessage(tr("Swapped foreground/background"));
}

void MainWindow::default_colors() {
  canvas_->set_primary_color(Qt::black);
  canvas_->set_secondary_color(Qt::white);
  refresh_color_buttons();
  statusBar()->showMessage(tr("Default colors"));
}

void MainWindow::refresh_color_buttons() {
  if (primary_color_button_ != nullptr) {
    primary_color_button_->setText(tr("FG"));
    primary_color_button_->setToolTip(tr("Foreground color %1").arg(canvas_->primary_color().name(QColor::HexRgb).toUpper()));
    primary_color_button_->setStyleSheet(color_button_style(canvas_->primary_color()));
  }
  if (secondary_color_button_ != nullptr) {
    secondary_color_button_->setText(tr("BG"));
    secondary_color_button_->setToolTip(tr("Background color %1").arg(canvas_->secondary_color().name(QColor::HexRgb).toUpper()));
    secondary_color_button_->setStyleSheet(color_button_style(canvas_->secondary_color()));
  }
}

void MainWindow::apply_text_options_to_active_editor() {
  if (canvas_ == nullptr) {
    return;
  }
  auto* editor = canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  if (editor == nullptr) {
    return;
  }

  const auto text_color = editor->property("photoslop.documentTextColor").value<QColor>().isValid()
                              ? editor->property("photoslop.documentTextColor").value<QColor>()
                              : canvas_->primary_color();
  const auto document_text_size =
      text_size_spin_ != nullptr ? text_size_spin_->value()
                                 : std::max(1, editor->property("photoslop.documentTextSize").toInt());
  const auto family = text_font_combo_ != nullptr ? text_font_combo_->currentFont().family() : editor->font().family();

  QFont editor_font(family);
  editor_font.setPixelSize(std::max(8, static_cast<int>(std::round(document_text_size * canvas_->zoom()))));
  editor_font.setBold(text_bold_button_ != nullptr && text_bold_button_->isChecked());
  editor_font.setItalic(text_italic_button_ != nullptr && text_italic_button_->isChecked());

  editor->setProperty("photoslop.documentTextSize", document_text_size);
  editor->setFont(editor_font);
  editor->document()->setDefaultFont(editor_font);
  editor->setStyleSheet(inline_text_editor_style(text_color, editor_font.pixelSize()));

  const auto saved_cursor = editor->textCursor();
  QTextCursor document_cursor(editor->document());
  document_cursor.select(QTextCursor::Document);
  QTextCharFormat format;
  format.setFont(editor_font);
  format.setForeground(QBrush(text_color));
  document_cursor.mergeCharFormat(format);
  editor->setTextCursor(saved_cursor);

  const auto document_editor_width = std::max(64, editor->property("photoslop.documentTextWidth").toInt());
  const auto minimum_width = std::max(80, static_cast<int>(std::round(document_editor_width * canvas_->zoom())));
  editor->document()->setTextWidth(-1);
  const auto content_width = static_cast<int>(std::ceil(editor->document()->idealWidth())) + 6;
  const auto width = std::max(minimum_width, content_width);
  editor->setProperty("photoslop.documentTextWidth",
                      std::max(document_editor_width,
                               static_cast<int>(std::ceil(static_cast<double>(width) / canvas_->zoom()))));
  editor->document()->setTextWidth(width);
  const auto text_height = std::max(32, static_cast<int>(std::ceil(editor->document()->size().height())) + 2);
  const auto minimum_height =
      std::max(32, static_cast<int>(std::ceil(static_cast<double>(document_text_size) * canvas_->zoom() * 1.45)));
  editor->resize(width, std::max(text_height, minimum_height));
  editor->updateGeometry();
  editor->update();
}

bool MainWindow::is_text_option_widget(QWidget* widget) const {
  if (widget == nullptr) {
    return false;
  }
  const auto owns = [widget](const QWidget* candidate) {
    return candidate != nullptr && (widget == candidate || candidate->isAncestorOf(widget));
  };
  return owns(text_font_combo_) || owns(text_size_spin_) || owns(text_bold_button_) || owns(text_italic_button_);
}

void MainWindow::register_option_action(QAction* action, std::initializer_list<CanvasTool> tools) {
  if (action == nullptr) {
    return;
  }
  option_actions_.emplace_back(action, std::vector<CanvasTool>(tools.begin(), tools.end()));
}

void MainWindow::refresh_options_bar() {
  for (const auto& [action, tools] : option_actions_) {
    if (action == nullptr) {
      continue;
    }
    const auto visible = tools.empty() || std::find(tools.begin(), tools.end(), current_tool_) != tools.end();
    action->setVisible(visible);
  }

  if (move_auto_select_check_ != nullptr && canvas_ != nullptr) {
    QSignalBlocker blocker(move_auto_select_check_);
    move_auto_select_check_->setChecked(canvas_->auto_select_layer());
  }
  if (canvas_ != nullptr) {
    current_selection_mode_ = canvas_->selection_mode();
  }
  const auto set_checked = [](QAction* action, bool checked) {
    if (action == nullptr) {
      return;
    }
    QSignalBlocker blocker(action);
    action->setChecked(checked);
  };
  set_checked(selection_new_mode_action_, current_selection_mode_ == CanvasWidget::SelectionMode::Replace);
  set_checked(selection_add_mode_action_, current_selection_mode_ == CanvasWidget::SelectionMode::Add);
  set_checked(selection_subtract_mode_action_, current_selection_mode_ == CanvasWidget::SelectionMode::Subtract);
  set_checked(selection_intersect_mode_action_, current_selection_mode_ == CanvasWidget::SelectionMode::Intersect);
}

void MainWindow::load_recent_files() {
  QSettings settings(QStringLiteral("Photoslop"), QStringLiteral("Photoslop"));
  recent_files_ = settings.value(QStringLiteral("recentFiles")).toStringList();
  recent_files_.erase(std::remove_if(recent_files_.begin(), recent_files_.end(), [](const QString& path) {
                        return path.trimmed().isEmpty() || !QFileInfo::exists(path);
                      }),
                      recent_files_.end());
  while (recent_files_.size() > 10) {
    recent_files_.removeLast();
  }
}

void MainWindow::save_recent_files() const {
  QSettings settings(QStringLiteral("Photoslop"), QStringLiteral("Photoslop"));
  settings.setValue(QStringLiteral("recentFiles"), recent_files_);
}

void MainWindow::add_recent_file(QString path) {
  path = QFileInfo(path).absoluteFilePath();
  if (path.isEmpty()) {
    return;
  }
  recent_files_.removeAll(path);
  recent_files_.prepend(path);
  while (recent_files_.size() > 10) {
    recent_files_.removeLast();
  }
  save_recent_files();
  rebuild_recent_files_menu();
}

void MainWindow::rebuild_recent_files_menu() {
  if (recent_files_menu_ == nullptr) {
    return;
  }
  recent_files_menu_->clear();
  recent_files_menu_->setEnabled(!recent_files_.isEmpty());
  int index = 1;
  for (const auto& path : recent_files_) {
    const auto label = tr("&%1 %2").arg(index++).arg(QFileInfo(path).fileName());
    auto* action = recent_files_menu_->addAction(label);
    action->setToolTip(path);
    action->setData(path);
    connect(action, &QAction::triggered, this, [this, path] { open_recent_document(path); });
  }
  if (!recent_files_.isEmpty()) {
    recent_files_menu_->addSeparator();
    auto* clear_action = recent_files_menu_->addAction(tr("Clear Recent Files"));
    clear_action->setObjectName(QStringLiteral("fileClearRecentAction"));
    connect(clear_action, &QAction::triggered, this, [this] {
      recent_files_.clear();
      save_recent_files();
      rebuild_recent_files_menu();
    });
  }
}

void MainWindow::open_recent_document(QString path) {
  if (!QFileInfo::exists(path)) {
    recent_files_.removeAll(path);
    save_recent_files();
    rebuild_recent_files_menu();
    statusBar()->showMessage(tr("Recent file is missing"));
    return;
  }
  open_document_path(path);
}

QAction* MainWindow::add_tool_action(QToolBar* palette, QActionGroup* group, QString label, CanvasTool tool,
                                     QKeySequence shortcut) {
  auto* action = palette->addAction(label);
  action->setIcon(tool_icon(tool));
  action->setCheckable(true);
  action->setData(static_cast<int>(tool));
  apply_action_shortcut(action, shortcut);
  group->addAction(action);
  return action;
}

void MainWindow::update_history(QString label) {
  if (history_list_ != nullptr) {
    history_list_->insertItem(0, label);
    history_list_->setCurrentRow(0);
  }
  refresh_document_info();
}

void MainWindow::update_undo_redo_actions() {
  const auto index = document_tabs_ == nullptr ? -1 : document_tabs_->currentIndex();
  if (index < 0 || index >= static_cast<int>(sessions_.size())) {
    if (undo_action_ != nullptr) {
      undo_action_->setEnabled(false);
    }
    if (redo_action_ != nullptr) {
      redo_action_->setEnabled(false);
    }
    return;
  }
  const auto& active_session = *sessions_[static_cast<std::size_t>(index)];
  if (undo_action_ != nullptr) {
    undo_action_->setEnabled(!active_session.undo_stack.empty());
  }
  if (redo_action_ != nullptr) {
    redo_action_->setEnabled(!active_session.redo_stack.empty());
  }
}

void MainWindow::show_about() {
  QMessageBox::about(this, tr("About Photoslop"),
                     tr("Photoslop %1\nNative PSD-oriented pixel editor.").arg(PHOTOSLOP_VERSION));
}

}  // namespace photoslop::ui
