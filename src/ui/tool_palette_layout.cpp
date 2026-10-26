#include "ui/tool_palette_layout.hpp"

#include "ui/theme_palette.hpp"

#include <QAction>
#include <QEvent>
#include <QHBoxLayout>
#include <QLayout>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QWheelEvent>
#include <QWidgetAction>

#include <algorithm>
#include <utility>

namespace patchy::ui {
namespace {

// A slot's toolbar item. The palette asks it for a widget whenever the item is added, and
// hands the widget back when it is removed: the slot's one button moves in and out, never
// deleted, so its filters, menu and shown tool survive every rebuild and layout switch.
class ToolPaletteSlotAction final : public QWidgetAction {
public:
  ToolPaletteSlotAction(QToolButton* button, QWidget* stash, QObject* parent)
      : QWidgetAction(parent), button_(button), stash_(stash) {}

protected:
  QWidget* createWidget(QWidget* parent) override {
    if (button_ == nullptr) {
      return nullptr;
    }
    button_->setParent(parent);
    return button_;
  }

  void deleteWidget(QWidget* widget) override {
    widget->hide();
    if (stash_ != nullptr) {
      widget->setParent(stash_);
    } else {
      widget->deleteLater();
    }
  }

private:
  QPointer<QToolButton> button_;
  QPointer<QWidget> stash_;
};

QString tool_id(const QAction* tool) {
  return tool != nullptr ? tool->property(kToolPaletteToolIdProperty).toString() : QString();
}

}  // namespace

ToolPaletteScrollArrow::ToolPaletteScrollArrow(Direction direction, QWidget* parent)
    : QWidget(parent), direction_(direction) {
  setObjectName(direction == Direction::Up ? QStringLiteral("toolPaletteScrollUpArrow")
                                           : QStringLiteral("toolPaletteScrollDownArrow"));
  // The toolbar would squeeze a plain widget's row; the arrow keeps its 12 px.
  setFixedHeight(sizeHint().height());
}

QSize ToolPaletteScrollArrow::sizeHint() const { return {24, 12}; }

void ToolPaletteScrollArrow::paintEvent(QPaintEvent* event) {
  QWidget::paintEvent(event);
  const auto& colors = theme();
  QPainter painter(this);
  painter.fillRect(rect(), colors.tool_palette_bg);
  painter.setRenderHint(QPainter::Antialiasing);
  // A small filled chevron, centered.
  const QPointF center(width() / 2.0, height() / 2.0);
  constexpr qreal kHalfWidth = 4.5;
  constexpr qreal kHalfHeight = 2.5;
  const qreal tip = direction_ == Direction::Up ? -kHalfHeight : kHalfHeight;
  QPainterPath path;
  path.moveTo(center.x() - kHalfWidth, center.y() - tip);
  path.lineTo(center.x() + kHalfWidth, center.y() - tip);
  path.lineTo(center.x(), center.y() + tip);
  path.closeSubpath();
  painter.fillPath(path, colors.icon_ink);
}

void ToolPaletteScrollArrow::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton && on_click) {
    on_click();
    event->accept();
    return;
  }
  QWidget::mousePressEvent(event);
}

ToolPaletteLayout::ToolPaletteLayout(QToolBar* palette, QWidget* customize_widget, QWidget* stash)
    : QObject(palette), palette_(palette), stash_(stash) {
  extension_ = palette->findChild<QToolButton*>(QStringLiteral("qt_toolbar_ext_button"));
  customize_item_ = palette->addWidget(customize_widget);
  auto* scroll_up = new ToolPaletteScrollArrow(ToolPaletteScrollArrow::Direction::Up, palette);
  scroll_up->on_click = [this] { scroll_by(-1); };
  scroll_up_item_ = palette->addWidget(scroll_up);
  scroll_up_item_->setVisible(false);
  // The down arrow covers the overflow extension in wheel-scroll mode: it sits where Qt
  // seats the extension, so it shows exactly when the column overflows.
  scroll_down_arrow_ = new ToolPaletteScrollArrow(ToolPaletteScrollArrow::Direction::Down, palette);
  scroll_down_arrow_->on_click = [this] { scroll_by(1); };
  scroll_down_arrow_->hide();
  palette->installEventFilter(this);
  if (extension_ != nullptr) {
    extension_->installEventFilter(this);
  }
}

void ToolPaletteLayout::add_slot(int cluster, QToolButton* button, QMenu* menu, QList<QAction*> members,
                                 QAction* cycle) {
  prepare_button(button);
  Slot slot;
  slot.cluster = cluster;
  slot.button = button;
  slot.menu = menu;
  slot.members = std::move(members);
  slot.cycle = cycle;
  slot.item = new ToolPaletteSlotAction(button, stash_, palette_);
  slots_.push_back(std::move(slot));
}

void ToolPaletteLayout::prepare_button(QToolButton* button) {
  // A button inside a two-column row has no toolbar parent, so it would lose the bar's
  // auto-raise and icon size: both are set on the button itself.
  button->setAutoRaise(true);
  button->setToolButtonStyle(Qt::ToolButtonIconOnly);
  button->setIconSize(palette_->iconSize());
  QObject::connect(palette_, &QToolBar::iconSizeChanged, button, &QToolButton::setIconSize);
  button->hide();
  button->setParent(stash_);
}

bool ToolPaletteLayout::slot_visible(const Slot& slot) const {
  return std::any_of(slot.members.begin(), slot.members.end(),
                     [this](const QAction* member) { return !tool_hidden(member); });
}

QList<QAction*> ToolPaletteLayout::visible_members(const Slot& slot) const {
  QList<QAction*> visible;
  for (auto* member : slot.members) {
    if (!tool_hidden(member)) {
      visible.push_back(member);
    }
  }
  return visible;
}

bool ToolPaletteLayout::tool_hidden(const QAction* tool) const {
  const auto id = tool_id(tool);
  return !id.isEmpty() && customization_.hidden_tools.contains(id);
}

QList<QAction*> ToolPaletteLayout::cycle_members(const QToolButton* button) const {
  for (const auto& slot : slots_) {
    if (slot.button == button) {
      const auto visible = visible_members(slot);
      return visible.isEmpty() ? slot.members : visible;
    }
  }
  return {};
}

void ToolPaletteLayout::refresh_flyout(const Slot& slot) {
  if (slot.menu == nullptr) {
    return;
  }
  auto shown = visible_members(slot);
  if (shown.isEmpty()) {
    shown = slot.members;
  }
  // A member left out leaves the menu; the cycle command stays last, after a separator.
  slot.menu->clear();
  for (auto* member : shown) {
    slot.menu->addAction(member);
  }
  if (slot.cycle != nullptr) {
    slot.menu->addSeparator();
    slot.menu->addAction(slot.cycle);
  }
  if (!shown.contains(slot.button->defaultAction())) {
    slot.button->setDefaultAction(shown.front());
    slot.button->setMenu(slot.menu);
    slot.button->setToolTip(shown.front()->toolTip());
  }
}

void ToolPaletteLayout::apply(const ToolPaletteCustomization& customization) {
  customization_ = normalized_tool_palette_customization(customization);
  if (palette_ == nullptr) {
    return;
  }
  // An open overflow expansion was measured for the old items: close it first.
  if (extension_ != nullptr && extension_->isChecked()) {
    extension_->click();
  }
  customize_item_->setVisible(customization_.show_customize_button);
  for (const auto& slot : slots_) {
    refresh_flyout(slot);
  }
  clear_items();
  build_items();
  scroll_offset_ = 0;
  wheel_remainder_ = 0;
  apply_scroll();
  schedule_scroll_update();
}

void ToolPaletteLayout::clear_items() {
  for (const auto& item : items_) {
    if (item.row != nullptr) {
      for (auto* button : item.row->findChildren<QToolButton*>(Qt::FindDirectChildrenOnly)) {
        button->hide();
        button->setParent(stash_);
      }
    }
    palette_->removeAction(item.action);
    if (item.separator || item.row != nullptr) {
      delete item.action;
    }
  }
  items_.clear();
}

void ToolPaletteLayout::build_items() {
  int clusters = 0;
  for (const auto& slot : slots_) {
    clusters = std::max(clusters, slot.cluster + 1);
  }
  const int spacing = palette_->layout() != nullptr ? palette_->layout()->spacing() : 1;
  bool first = true;
  for (int cluster = 0; cluster < clusters; ++cluster) {
    std::vector<const Slot*> shown;
    for (const auto& slot : slots_) {
      if (slot.cluster == cluster && slot_visible(slot)) {
        shown.push_back(&slot);
      }
    }
    if (shown.empty()) {
      continue;
    }
    if (!first) {
      items_.push_back({palette_->addSeparator(), true, nullptr});
    }
    first = false;
    if (!customization_.two_columns) {
      for (const auto* slot : shown) {
        slot->item->setVisible(true);
        palette_->addAction(slot->item);
        items_.push_back({slot->item, false, nullptr});
      }
      continue;
    }
    // Two columns: the cluster's tools left to right, two to a row (philipzae's layout in
    // GitHub issue 213). A row is one toolbar item, so an overflow folds whole rows.
    for (std::size_t index = 0; index < shown.size(); index += 2) {
      auto* row = new QWidget(palette_);
      row->setObjectName(QStringLiteral("toolPaletteRow"));
      auto* row_layout = new QHBoxLayout(row);
      row_layout->setContentsMargins(0, 0, 0, 0);
      row_layout->setSpacing(spacing);
      for (std::size_t column = index; column < std::min(shown.size(), index + 2); ++column) {
        auto* button = shown[column]->button;
        button->setParent(row);
        row_layout->addWidget(button);
        button->show();
      }
      row_layout->addStretch(1);
      items_.push_back({palette_->addWidget(row), false, row});
    }
  }
}

bool ToolPaletteLayout::scroll_mode() const { return customization_.wheel_scroll; }

int ToolPaletteLayout::scrollable_count() const {
  return static_cast<int>(std::count_if(items_.begin(), items_.end(), [](const Item& item) { return !item.separator; }));
}

void ToolPaletteLayout::apply_scroll() {
  if (!scroll_mode()) {
    scroll_offset_ = 0;
  }
  scroll_offset_ = std::clamp(scroll_offset_, 0, std::max(0, scrollable_count() - 1));
  // Items above the offset sit off the top; a separator goes with the cluster above it.
  int index = 0;
  for (const auto& item : items_) {
    if (item.separator) {
      item.action->setVisible(index > scroll_offset_);
      continue;
    }
    item.action->setVisible(index >= scroll_offset_);
    ++index;
  }
  scroll_up_item_->setVisible(scroll_mode() && scroll_offset_ > 0);
}

bool ToolPaletteLayout::can_scroll_up() const { return scroll_mode() && scroll_offset_ > 0; }

bool ToolPaletteLayout::can_scroll_down() const {
  return scroll_mode() && palette_ != nullptr && scroll_offset_ < scrollable_count() - 1 &&
         palette_->sizeHint().height() > palette_->height();
}

bool ToolPaletteLayout::scroll_by(int steps) {
  bool moved = false;
  for (; steps > 0 && can_scroll_down(); --steps) {
    ++scroll_offset_;
    apply_scroll();
    moved = true;
  }
  for (; steps < 0 && can_scroll_up(); ++steps) {
    --scroll_offset_;
    apply_scroll();
    moved = true;
  }
  if (moved) {
    schedule_scroll_update();
  }
  return moved;
}

void ToolPaletteLayout::schedule_scroll_update() {
  if (scroll_update_scheduled_) {
    return;
  }
  scroll_update_scheduled_ = true;
  // Deferred a turn so the toolbar layout and the footer placer have seated everything.
  QTimer::singleShot(0, this, [this] {
    scroll_update_scheduled_ = false;
    update_scroll_state();
  });
}

void ToolPaletteLayout::update_scroll_state() {
  if (palette_ == nullptr) {
    return;
  }
  // The window grew: bring items back from the top while they fit, so a scrolled column
  // never leaves empty space under its last tool.
  const int spacing = palette_->layout() != nullptr ? palette_->layout()->spacing() : 0;
  while (scroll_mode() && scroll_offset_ > 0) {
    int index = 0;
    int needed = 0;
    bool counting_separator = false;
    for (const auto& item : items_) {
      if (item.separator) {
        // The separator right after the item coming back reappears with it.
        if (counting_separator) {
          if (auto* widget = palette_->widgetForAction(item.action); widget != nullptr) {
            needed += widget->sizeHint().height() + spacing;
          }
        }
        continue;
      }
      if (index == scroll_offset_ - 1) {
        if (auto* widget = palette_->widgetForAction(item.action); widget != nullptr) {
          needed += widget->sizeHint().height() + spacing;
        }
        counting_separator = true;
      } else if (counting_separator) {
        break;
      }
      ++index;
    }
    int slack = palette_->height() - palette_->sizeHint().height();
    if (scroll_offset_ == 1) {
      if (auto* arrow = palette_->widgetForAction(scroll_up_item_); arrow != nullptr) {
        slack += arrow->sizeHint().height() + spacing;
      }
    }
    if (needed > slack) {
      break;
    }
    --scroll_offset_;
    apply_scroll();
  }
  place_down_arrow();
}

void ToolPaletteLayout::place_down_arrow() {
  if (scroll_down_arrow_ == nullptr || palette_ == nullptr) {
    return;
  }
  const bool show = scroll_mode() && extension_ != nullptr && !extension_->isHidden();
  if (!show) {
    scroll_down_arrow_->hide();
    return;
  }
  // Over the extension band, the palette's width less its 1 px right border.
  const auto band = extension_->geometry();
  scroll_down_arrow_->setGeometry(0, band.top(), std::max(1, palette_->width() - 1), band.height());
  scroll_down_arrow_->show();
  scroll_down_arrow_->raise();
}

bool ToolPaletteLayout::eventFilter(QObject* watched, QEvent* event) {
  if (watched == extension_) {
    switch (event->type()) {
      case QEvent::Show:
      case QEvent::Hide:
      case QEvent::Move:
      case QEvent::Resize:
        schedule_scroll_update();
        break;
      default:
        break;
    }
    return false;
  }
  switch (event->type()) {
    case QEvent::Wheel: {
      if (!scroll_mode()) {
        return false;
      }
      auto* wheel = static_cast<QWheelEvent*>(event);
      // A notched wheel sends 120 per step; a touchpad's small deltas add up to steps.
      wheel_remainder_ += wheel->angleDelta().y();
      int steps = 0;
      for (; wheel_remainder_ >= 120; wheel_remainder_ -= 120) {
        --steps;
      }
      for (; wheel_remainder_ <= -120; wheel_remainder_ += 120) {
        ++steps;
      }
      if (steps != 0) {
        scroll_by(steps);
      }
      wheel->accept();
      return true;
    }
    case QEvent::Resize:
    case QEvent::Show:
    case QEvent::LayoutRequest:
      schedule_scroll_update();
      break;
    default:
      break;
  }
  return false;
}

}  // namespace patchy::ui
