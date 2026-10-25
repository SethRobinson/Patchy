#include "ui/options_flow_layout.hpp"

#include <QEvent>
#include <QResizeEvent>

#include <algorithm>

namespace patchy::ui {

namespace {

// QWidgetItem reports 0x0 for a hidden widget, but the rows of other tools are
// made of hidden widgets, so measure those the way QWidgetItem::sizeHint would
// if they were shown.
QSize flow_item_size(QLayoutItem* item) {
  QWidget* widget = item->widget();
  if (widget == nullptr || !widget->isHidden()) {
    return item->sizeHint();
  }
  widget->ensurePolished();
  QSize size = widget->sizeHint().expandedTo(widget->minimumSizeHint());
  size = size.boundedTo(widget->maximumSize()).expandedTo(widget->minimumSize());
  if (widget->sizePolicy().horizontalPolicy() == QSizePolicy::Ignored) {
    size.setWidth(0);
  }
  if (widget->sizePolicy().verticalPolicy() == QSizePolicy::Ignored) {
    size.setHeight(0);
  }
  return size;
}

bool is_collapsible(const QWidget* widget) {
  return widget != nullptr && widget->property(kOptionsBarCollapsibleProperty).toBool();
}

}  // namespace

FlowLayout::FlowLayout(QWidget* parent, int horizontal_spacing, int vertical_spacing)
    : QLayout(parent), horizontal_spacing_(horizontal_spacing), vertical_spacing_(vertical_spacing) {
  setContentsMargins(0, 0, 0, 0);
}

FlowLayout::~FlowLayout() {
  while (QLayoutItem* item = takeAt(0)) {
    delete item;
  }
}

void FlowLayout::addItem(QLayoutItem* item) {
  items_.append(item);
}

int FlowLayout::count() const {
  return static_cast<int>(items_.size());
}

QLayoutItem* FlowLayout::itemAt(int index) const {
  return items_.value(index);
}

QLayoutItem* FlowLayout::takeAt(int index) {
  return (index >= 0 && index < items_.size()) ? items_.takeAt(index) : nullptr;
}

Qt::Orientations FlowLayout::expandingDirections() const {
  return {};
}

bool FlowLayout::hasHeightForWidth() const {
  return true;
}

int FlowLayout::heightForWidth(int width) const {
  return do_layout(QRect(0, 0, width, 0), true, nullptr).height;
}

void FlowLayout::setGeometry(const QRect& rect) {
  QLayout::setGeometry(rect);
  if (!center_rows_) {
    do_layout(rect, false, nullptr);
    return;
  }
  // The bar can be taller than the live row (its minimum height, or another tool's
  // taller row reserved): split the spare height evenly above and below.
  const int needed = do_layout(QRect(rect.x(), rect.y(), rect.width(), 0), true, nullptr).height;
  const int offset = std::max(0, (rect.height() - needed) / 2);
  do_layout(rect.translated(0, offset), false, nullptr);
}

QSize FlowLayout::sizeHint() const {
  return minimumSize();
}

QSize FlowLayout::minimumSize() const {
  QSize size;
  for (auto* item : items_) {
    const QWidget* widget = item->widget();
    if (widget != nullptr && widget->isHidden()) {
      continue;
    }
    size = size.expandedTo(item->minimumSize());
  }
  const auto margins = contentsMargins();
  size += QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
  return size;
}

void FlowLayout::invalidate() {
  cached_width_ = -1;
  QLayout::invalidate();
}

void FlowLayout::set_rows(std::vector<QSet<QWidget*>> rows) {
  if (rows == rows_) {
    return;
  }
  rows_ = std::move(rows);
  cached_width_ = -1;
}

FlowLayout::RowsMeasure FlowLayout::measure_rows(int width) const {
  if (width == cached_width_) {
    return cached_measure_;
  }
  RowsMeasure measure;
  const QRect rect(0, 0, width, 0);
  // Collapse when any row that carries a duplicate slider would wrap with it.
  for (const auto& row : rows_) {
    if (std::none_of(row.begin(), row.end(), is_collapsible)) {
      continue;
    }
    const std::function<bool(QWidget*)> include = [&row](QWidget* widget) { return row.contains(widget); };
    if (do_layout(rect, true, &include).lines > 1) {
      measure.collapse = true;
      break;
    }
  }
  for (const auto& row : rows_) {
    const std::function<bool(QWidget*)> include = [&row, collapse = measure.collapse](QWidget* widget) {
      return row.contains(widget) && !(collapse && is_collapsible(widget));
    };
    measure.height = std::max(measure.height, do_layout(rect, true, &include).height);
  }
  cached_width_ = width;
  cached_measure_ = measure;
  return measure;
}

FlowLayout::Flow FlowLayout::do_layout(const QRect& rect, bool test_only,
                                       const std::function<bool(QWidget*)>* include) const {
  const auto margins = contentsMargins();
  const QRect effective = rect.adjusted(margins.left(), margins.top(), -margins.right(), -margins.bottom());
  int x = effective.x();
  int y = effective.y();
  int line_height = 0;
  int lines = 0;
  // The current line's items and their places, set once the line's height is known.
  struct Placed {
    QLayoutItem* item;
    QPoint origin;
    QSize size;
  };
  std::vector<Placed> line;
  const auto place_line = [&] {
    for (const auto& placed : line) {
      const int dy = center_rows_ ? (line_height - placed.size.height()) / 2 : 0;
      placed.item->setGeometry(QRect(placed.origin + QPoint(0, dy), placed.size));
    }
    line.clear();
  };
  for (auto* item : items_) {
    QWidget* widget = item->widget();
    if (include != nullptr) {
      if (widget != nullptr && !(*include)(widget)) {
        continue;
      }
    } else if (widget != nullptr && widget->isHidden()) {
      continue;
    }
    const QSize hint = flow_item_size(item);
    int next_x = x + hint.width() + horizontal_spacing_;
    if (next_x - horizontal_spacing_ > effective.right() + 1 && line_height > 0) {
      place_line();
      x = effective.x();
      y = y + line_height + vertical_spacing_;
      next_x = x + hint.width() + horizontal_spacing_;
      line_height = 0;
      ++lines;
    }
    if (!test_only) {
      line.push_back({item, QPoint(x, y), hint});
    }
    x = next_x;
    line_height = std::max(line_height, hint.height());
  }
  place_line();
  if (line_height > 0) {
    ++lines;
  }
  return Flow{y + line_height - rect.y() + margins.bottom(), lines};
}

QSize OptionsFlowContainer::sizeHint() const {
  const int available = width() > 0 ? width() : 1200;
  return QSize(available, height_for(available));
}

QSize OptionsFlowContainer::minimumSizeHint() const {
  const int available = width() > 0 ? width() : 0;
  return QSize(layout() != nullptr ? layout()->minimumSize().width() : 0, height_for(std::max(available, 1)));
}

bool OptionsFlowContainer::wants_collapse() const {
  const auto* flow = dynamic_cast<const FlowLayout*>(layout());
  if (flow == nullptr || !flow->has_rows() || width() <= 0) {
    return collapse_applied_;
  }
  return flow->measure_rows(width()).collapse;
}

void OptionsFlowContainer::set_collapse_applied(bool collapsed) {
  if (collapsed != collapse_applied_) {
    reserved_width_ = -1;
  }
  collapse_applied_ = collapsed;
}

bool OptionsFlowContainer::event(QEvent* event) {
  const bool handled = QWidget::event(event);
  // A control changed size (a longer label after a language switch, a wider field) without
  // the width changing: the slider decision must follow, or it stays whatever the controls
  // measured when the bar was last refreshed (GitHub issue 364: the tip picker's name).
  if (event->type() == QEvent::LayoutRequest && !updating_ && collapse_changed_ &&
      wants_collapse() != collapse_applied_) {
    collapse_changed_();
  }
  return handled;
}

void OptionsFlowContainer::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  // A new width can cross the slider-collapse threshold; MainWindow re-runs its
  // visibility pass, which hides or restores the duplicate sliders.
  if (collapse_changed_ && wants_collapse() != collapse_applied_) {
    collapse_changed_();
  }
  // Width changed: the wrapped height may differ, so ask the toolbar to relayout.
  updateGeometry();
}

int OptionsFlowContainer::height_for(int width) const {
  const auto* flow = dynamic_cast<const FlowLayout*>(layout());
  if (flow == nullptr) {
    return layout() != nullptr ? layout()->heightForWidth(width) : 0;
  }
  // The visible controls' wrapped height: the active tool's row, plus whatever a
  // transform or text session adds.
  const int live = flow->heightForWidth(width);
  if (updating_) {
    return std::max(live, reserved_width_ == width ? reserved_height_ : 0);
  }
  if (width != reserved_width_) {
    // A new width: forget what the old width needed.
    reserved_width_ = width;
    reserved_height_ = live;
  } else {
    reserved_height_ = std::max(reserved_height_, live);
  }
  return reserved_height_;
}

}  // namespace patchy::ui
