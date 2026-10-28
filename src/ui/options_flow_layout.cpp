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
  // Rows start at the top margin, so a one-row tool keeps its place on line one when the
  // bar holds the height another tool's second row reserved (GitHub issue 409). Only
  // height beyond what any tool's rows need at this width (a taller minimum strip) is
  // split evenly above and below; the reserved rows' spare height stays below.
  const int needed = do_layout(QRect(rect.x(), rect.y(), rect.width(), 0), true, nullptr).height;
  const int tallest = has_rows() ? std::max(needed, measure_rows(rect.width()).height) : needed;
  const int offset = std::max(0, (rect.height() - tallest) / 2);
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
  ++rows_generation_;
  cached_width_ = -1;
}

FlowLayout::RowsMeasure FlowLayout::measure_rows(int width) const {
  if (width == cached_width_) {
    return cached_measure_;
  }
  RowsMeasure measure;
  measure.collapse.reserve(rows_.size());
  const QRect rect(0, 0, width, 0);
  // Each row that carries a duplicate slider drops it when it would wrap with it; other
  // rows never cost it its slider (GitHub issue 458 follow-up: a wider tool's row hid
  // the Clone Stamp's Size slider with room to spare).
  for (const auto& row : rows_) {
    bool collapse = false;
    if (std::any_of(row.begin(), row.end(), is_collapsible)) {
      const std::function<bool(QWidget*)> include = [&row](QWidget* widget) { return row.contains(widget); };
      collapse = do_layout(rect, true, &include).lines > 1;
    }
    measure.collapse.push_back(collapse);
    const std::function<bool(QWidget*)> include = [&row, collapse](QWidget* widget) {
      return row.contains(widget) && !(collapse && is_collapsible(widget));
    };
    measure.height = std::max(measure.height, do_layout(rect, true, &include).height);
  }
  cached_width_ = width;
  cached_measure_ = measure;
  return measure;
}

bool FlowLayout::active_row_collapses(int width) const {
  if (active_row_ < 0 || active_row_ >= static_cast<int>(rows_.size())) {
    return false;
  }
  const auto measure = measure_rows(width);
  return measure.collapse[static_cast<std::size_t>(active_row_)];
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
    // Then each separator between two controls moves so its line sits halfway between
    // their ink, now that their boxes have their final sizes.
    for (std::size_t index = 1; index + 1 < line.size(); ++index) {
      auto* separator = dynamic_cast<OptionsBarSeparator*>(line[index].item->widget());
      const QWidget* before = line[index - 1].item->widget();
      const QWidget* after = line[index + 1].item->widget();
      if (separator == nullptr || before == nullptr || after == nullptr ||
          dynamic_cast<const OptionsBarSeparator*>(before) != nullptr ||
          dynamic_cast<const OptionsBarSeparator*>(after) != nullptr) {
        continue;
      }
      const QMargins before_ink = ink_insets_ ? ink_insets_(*before) : QMargins();
      const QMargins after_ink = ink_insets_ ? ink_insets_(*after) : QMargins();
      // The clear columns between the last ink before and the first ink after.
      const int first = before->geometry().right() - before_ink.right() + 1;
      const int last = after->geometry().left() + after_ink.left() - 1;
      if (last < first) {
        continue;
      }
      QRect box = separator->geometry();
      // An even count puts the extra column on the left: glyph edges fade, so the ink after
      // reads a little later than its outline says.
      box.moveLeft(first + (last - first + 1) / 2 - OptionsBarSeparator::line_offset(box.width()));
      separator->setGeometry(box);
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

OptionsBarSeparator::OptionsBarSeparator(QWidget* parent) : QFrame(parent) {
  setObjectName(QStringLiteral("optionSeparator"));
  setFrameShape(QFrame::VLine);
  setFrameShadow(QFrame::Plain);
  setFixedHeight(24);
}

// Until it is shown the container has no real width (an unshown child is 100 px wide),
// and the wrapped height at that width (every control on its own row, about 470 px)
// would hold the window that tall: the bar is Fixed vertically, so the window's layout
// takes its size hint as its minimum. Both hints read one row then.
bool OptionsFlowContainer::laid_out() const { return isVisible() && width() > 0; }

QSize OptionsFlowContainer::sizeHint() const {
  if (!laid_out()) {
    return QSize(1200, kOneRowHeight);
  }
  return QSize(width(), height_for(width()));
}

QSize OptionsFlowContainer::minimumSizeHint() const {
  const int minimum_width = layout() != nullptr ? layout()->minimumSize().width() : 0;
  return QSize(minimum_width, laid_out() ? height_for(width()) : kOneRowHeight);
}

bool OptionsFlowContainer::wants_collapse() const {
  const auto* flow = dynamic_cast<const FlowLayout*>(layout());
  if (flow == nullptr || !flow->has_rows() || width() <= 0) {
    return collapse_applied_;
  }
  return flow->active_row_collapses(width());
}

void OptionsFlowContainer::set_collapse_applied(bool collapsed) {
  const auto* flow = dynamic_cast<const FlowLayout*>(layout());
  const int row = flow != nullptr ? flow->active_row() : -1;
  const int generation = flow != nullptr ? flow->rows_generation() : -1;
  if (collapsed != collapse_applied_ && row == applied_row_ && generation == applied_rows_generation_) {
    reserved_width_ = -1;
  }
  collapse_applied_ = collapsed;
  applied_row_ = row;
  applied_rows_generation_ = generation;
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
  // transform or text session adds. Never below one row, so the strip keeps its
  // height for a tool with no controls. The floor lives here, not in a QSS
  // min-height on the bar: an explicit minimum hides the wrapped height from the
  // window's layout, whose minimum then let a short window squeeze the panel column
  // under its content (local issue 016).
  const int live = std::max(kOneRowHeight, flow->heightForWidth(width));
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
