#include "ui/recent_file_list.hpp"

#include "ui/dialog_utils.hpp"
#include "ui/recent_thumbnails.hpp"
#include "ui/theme_palette.hpp"
#include "ui/ui_text_size.hpp"

#include <QDir>
#include <QListWidget>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHelpEvent>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QStyle>
#include <QToolTip>
#include <QStyleOptionViewItem>

#include <algorithm>

namespace patchy::ui {

namespace {

// A row's first line: the file or folder name, or the whole path for a drive root
// ("C:/"), whose name part is empty.
QString recent_row_name(const QString& path) {
  const auto name = QFileInfo(path).fileName();
  return name.isEmpty() ? QDir::toNativeSeparators(path) : name;
}

}  // namespace

QStringList recent_filter_tokens(const QString& filter_text) {
  return filter_text.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

bool recent_path_matches(const QString& path, const QStringList& tokens) {
  // A plain token matches the FILE NAME (extension included), not the folder: the
  // whole path used to count, so "jpg" matched every file under a folder called
  // JPG and the filter looked broken (GitHub issue 158). ".jpg" or "*.jpg" is an
  // extension filter, and a token with a slash still matches the folder path.
  const auto native_path = QDir::toNativeSeparators(path);
  const auto name = QFileInfo(path).fileName();
  return std::all_of(tokens.begin(), tokens.end(), [&native_path, &name](const QString& token) {
    if (token.contains(QLatin1Char('/')) || token.contains(QLatin1Char('\\'))) {
      return native_path.contains(QDir::toNativeSeparators(token), Qt::CaseInsensitive);
    }
    auto extension = token;
    if (extension.startsWith(QLatin1Char('*'))) {
      extension.remove(0, 1);
    }
    if (extension.startsWith(QLatin1Char('.')) && extension.size() > 1) {
      return name.endsWith(extension, Qt::CaseInsensitive);
    }
    return name.contains(token, Qt::CaseInsensitive);
  });
}

RecentFileList::RecentFileList(QWidget* parent, int visible_rows)
    : QListWidget(parent), visible_rows_(std::max(1, visible_rows)) {
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
  setMouseTracking(true);
  // The delegate elides both text lines to the row width, so only the vertical bar
  // is ever wanted; per-pixel scrolling keeps the wheel smooth over a long list.
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  setUniformItemSizes(true);  // constant row height; skips a sizeHint per row
  setItemDelegate(new RecentFileDelegate(this));
  setContextMenuPolicy(Qt::CustomContextMenu);
  // On the viewport, not the list: the scroll bar keeps a normal arrow cursor.
  viewport()->setCursor(Qt::PointingHandCursor);
}

void RecentFileList::set_fixed_rows(int rows) {
  fixed_rows_ = std::max(0, rows);
  updateGeometry();
}

QSize RecentFileList::sizeHint() const {
  if (grid_view_) {
    return {QListWidget::sizeHint().width(), grid_rows_shown() * grid_cell_size().height() + kRecentListChrome};
  }
  const auto rows = fixed_rows_ > 0 ? fixed_rows_ : count();
  return {QListWidget::sizeHint().width(), std::clamp(rows, 1, visible_rows_) * recent_row_height() + kRecentListChrome};
}

QSize RecentFileList::minimumSizeHint() const {
  const int row = grid_view_ ? grid_cell_size().height() : recent_row_height();
  return {QListWidget::minimumSizeHint().width(), row + kRecentListChrome};
}

void RecentFileList::set_grid_view(bool grid, RecentThumbnailCache* thumbnails, int visible_rows) {
  grid_view_ = grid;
  thumbnails_ = grid ? thumbnails : nullptr;
  visible_grid_rows_ = std::max(1, visible_rows);
  rows_changed();
}

void RecentFileList::rows_changed() {
  // The no-match placeholder (an empty path) is one full-width row in either view.
  cells_ = grid_view_ && count() > 0 && !item(0)->data(kRecentPathRole).toString().isEmpty();
  apply_view_layout();
}

int RecentFileList::grid_layout_width() const {
  // QListView wraps a row of cells inside the viewport as it is without scroll bars, less the
  // room it always keeps for the vertical bar (so the bar's arrival moves no cell), and starts
  // a new row when a cell would END at that bound: stay one pixel inside it.
  int width = maximumViewportSize().width();
  if (verticalScrollBarPolicy() == Qt::ScrollBarAsNeeded) {
    width -= style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, verticalScrollBar());
    if (style()->styleHint(QStyle::SH_ScrollView_FrameOnlyAroundContents, nullptr, this) != 0) {
      width -= 2 * style()->pixelMetric(QStyle::PM_DefaultFrameWidth, nullptr, this);
    }
  }
  return std::max(1, width - 1);
}

int RecentFileList::grid_columns() const {
  // Rounded, not floored: 3.6 target widths become four cells a little under the target,
  // 3.4 become three a little over it.
  return std::max(1, (2 * grid_layout_width() + kRecentGridTargetCellWidth) / (2 * kRecentGridTargetCellWidth));
}

int RecentFileList::width_for_grid_columns(int columns) const {
  // What the widget's frame, its padding and the vertical bar's room take is the same at
  // every width.
  return std::max(1, columns) * kRecentGridTargetCellWidth + (width() - grid_layout_width());
}

int RecentFileList::grid_rows_shown() const {
  // The pinned count when there is one (the box keeps its height while a filter narrows it,
  // down to the no-match placeholder row); otherwise the cells themselves.
  const int entries = fixed_rows_ > 0 ? fixed_rows_ : (cells_ ? count() : 0);
  if (entries <= 0) {
    return 1;
  }
  const int columns = grid_columns();
  return std::clamp((entries + columns - 1) / columns, 1, visible_grid_rows_);
}

QSize RecentFileList::grid_cell_size() const {
  const int width = std::max(1, grid_layout_width() / grid_columns());
  return {width, grid_cell_height(width)};
}

QRect RecentFileList::grid_picture_rect(const QRect& cell_rect) {
  const int width = std::max(1, cell_rect.width() - 2 * kRecentGridCellInset);
  return {cell_rect.left() + kRecentGridCellInset, cell_rect.top() + kRecentGridCellInset, width,
          std::max(1, width * 3 / 4)};
}

void RecentFileList::apply_view_layout() {
  // The same list view either way: rows top to bottom, or cells that wrap at the viewport's
  // right edge. Qt lays the cells out again on every resize (Adjust).
  setFlow(cells_ ? QListView::LeftToRight : QListView::TopToBottom);
  setWrapping(cells_);
  setResizeMode(cells_ ? QListView::Adjust : QListView::Fixed);
  // Rows share one cached size hint (their width follows the viewport by itself). A cell's
  // size is the hint, which changes with the viewport's width, so cells are asked each time.
  setUniformItemSizes(!cells_);
  setGridSize(cells_ ? grid_cell_size() : QSize());
  if (cells_ && thumbnails_ != nullptr) {
    thumbnails_->set_display_box(grid_picture_rect(QRect(QPoint(0, 0), grid_cell_size())).size() *
                                 devicePixelRatioF());
  }
  grid_height_laid_out_ = grid_rows_shown() * grid_cell_size().height();
  updateGeometry();
  viewport()->update();
}

bool RecentFileList::update_path(const QString& path) {
  // One cell, not the viewport: pictures arrive one at a time, forty or more after the first
  // paint of a large window.
  const auto visible = viewport()->rect();
  for (int row = 0; row < count(); ++row) {
    if (item(row)->data(kRecentPathRole).toString() != path) {
      continue;
    }
    if (const auto rect = visualItemRect(item(row)); rect.intersects(visible)) {
      viewport()->update(rect);
    }
    return true;  // found; a row scrolled out of view needs no repaint
  }
  return false;
}

void RecentFileList::resizeEvent(QResizeEvent* event) {
  QListWidget::resizeEvent(event);
  if (!cells_) {
    return;
  }
  // The cells share the new width.
  if (const auto cell = grid_cell_size(); gridSize() != cell) {
    setGridSize(cell);
    if (thumbnails_ != nullptr) {
      thumbnails_->set_display_box(grid_picture_rect(QRect(QPoint(0, 0), cell)).size() * devicePixelRatioF());
    }
  }
  // The height the box asks for follows both: the rows the entries fill at this width and
  // the cells' own height, which grows with their width.
  if (const int height = grid_rows_shown() * grid_cell_size().height(); height != grid_height_laid_out_) {
    grid_height_laid_out_ = height;
    updateGeometry();
  }
}

void RecentFileList::set_remove_handler(std::function<void(const QString&)> handler, const QString& tooltip) {
  remove_handler_ = std::move(handler);
  remove_tooltip_ = tooltip;
  viewport()->update();
}

QRect RecentFileList::remove_button_rect(const QRect& row_rect) {
  constexpr int kSize = 22;
  return {row_rect.right() - 8 - kSize + 1, row_rect.center().y() - kSize / 2 + 1, kSize, kSize};
}

QRect RecentFileList::grid_remove_button_rect(const QRect& cell_rect) {
  constexpr int kSize = 22;
  const auto picture = grid_picture_rect(cell_rect);
  return {picture.right() - 3 - kSize + 1, picture.top() + 3, kSize, kSize};
}

QRect RecentFileList::remove_button_rect_in(const QRect& item_rect) const {
  return cells_ ? grid_remove_button_rect(item_rect) : remove_button_rect(item_rect);
}

int RecentFileList::remove_button_row_at(const QPoint& position) const {
  if (!remove_handler_) {
    return -1;
  }
  const auto index = indexAt(position);
  if (!index.isValid() || index.data(kRecentPathRole).toString().isEmpty()) {
    return -1;
  }
  return remove_button_rect_in(visualRect(index)).contains(position) ? index.row() : -1;
}

void RecentFileList::set_remove_hover_row(int row) {
  if (row == remove_hover_row_) {
    return;
  }
  remove_hover_row_ = row;
  viewport()->update();
}

void RecentFileList::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    remove_pressed_row_ = remove_button_row_at(event->position().toPoint());
    if (remove_pressed_row_ >= 0) {
      event->accept();
      return;  // the row under the button neither opens nor takes the press
    }
  }
  QListWidget::mousePressEvent(event);
}

void RecentFileList::mouseReleaseEvent(QMouseEvent* event) {
  if (remove_pressed_row_ >= 0 && event->button() == Qt::LeftButton) {
    const int pressed = remove_pressed_row_;
    remove_pressed_row_ = -1;
    event->accept();
    if (remove_button_row_at(event->position().toPoint()) != pressed) {
      return;  // released off the button: nothing happens, as with any button
    }
    const auto path = item(pressed) != nullptr ? item(pressed)->data(kRecentPathRole).toString() : QString();
    if (!path.isEmpty()) {
      // Queued: the handler rebuilds this list, which must not happen inside
      // its own mouse event.
      QMetaObject::invokeMethod(
          this, [handler = remove_handler_, path] { handler(path); }, Qt::QueuedConnection);
    }
    return;
  }
  QListWidget::mouseReleaseEvent(event);
}

void RecentFileList::mouseMoveEvent(QMouseEvent* event) {
  set_remove_hover_row(remove_button_row_at(event->position().toPoint()));
  QListWidget::mouseMoveEvent(event);
}

void RecentFileList::leaveEvent(QEvent* event) {
  set_remove_hover_row(-1);
  QListWidget::leaveEvent(event);
}

bool RecentFileList::viewportEvent(QEvent* event) {
  if (event->type() == QEvent::ToolTip) {
    const auto* help = static_cast<QHelpEvent*>(event);
    if (remove_button_row_at(help->pos()) >= 0) {
      QToolTip::showText(help->globalPos(), remove_tooltip_, viewport());
      return true;
    }
  }
  if (event->type() == QEvent::Leave) {
    set_remove_hover_row(-1);
  }
  return QListWidget::viewportEvent(event);
}

int recent_row_height() { return ui_text_length(kRecentRowHeight); }

namespace {

constexpr int kGridNameGap = 4;
constexpr int kGridNameLine = 18;
constexpr int kGridBottom = 6;

// The remove (x) button of a hovered row or cell. `backed` gives it a filled plate, for the
// grid, where it sits on the picture.
void paint_remove_button(QPainter* painter, const QRect& button, bool hovered, bool backed) {
  painter->setRenderHint(QPainter::Antialiasing, true);
  if (hovered || backed) {
    painter->setPen(Qt::NoPen);
    painter->setBrush(hovered ? theme().start_panel_row_remove_hover_bg : theme().list_surface_bg);
    painter->drawRoundedRect(button, 4.0, 4.0);
  }
  QPen cross(hovered ? theme().text_primary : theme().start_panel_muted_text);
  cross.setWidthF(1.6);
  cross.setCapStyle(Qt::RoundCap);
  painter->setPen(cross);
  const QRectF glyph = QRectF(button).adjusted(7.0, 7.0, -7.0, -7.0);
  painter->drawLine(glyph.topLeft(), glyph.bottomRight());
  painter->drawLine(glyph.topRight(), glyph.bottomLeft());
  painter->setRenderHint(QPainter::Antialiasing, false);
}

// One grid cell: the picture box (the thumbnail, or the file's extension when it has none)
// over the file name. Asking the cache is a hash lookup; the first ask for a path queues its
// load on the worker and the cell is repainted when the picture arrives.
void paint_grid_cell(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index,
                     const RecentFileList& list, const QString& path) {
  const bool selected = (option.state & QStyle::State_Selected) != 0;
  const bool hovered = (option.state & QStyle::State_MouseOver) != 0;
  if (selected || hovered) {
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);
    painter->setBrush(selected ? theme().selection_soft_bg : theme().start_panel_row_hover_bg);
    painter->drawRoundedRect(option.rect.adjusted(2, 2, -2, -2), 4.0, 4.0);
    painter->setRenderHint(QPainter::Antialiasing, false);
  }

  const QRect picture = RecentFileList::grid_picture_rect(option.rect);
  painter->setPen(theme().list_surface_border);
  painter->setBrush(theme().window_bg);
  painter->drawRect(picture.adjusted(0, 0, -1, -1));

  auto* thumbnails = list.thumbnails();
  const QImage* image = nullptr;
  auto state = RecentThumbnailState::unavailable;
  if (thumbnails != nullptr) {
    thumbnails->request(path);
    image = thumbnails->image(path);
    state = thumbnails->state(path);
  }
  const QRect inner = picture.adjusted(1, 1, -1, -1);
  if (image != nullptr) {
    const qreal ratio = option.widget != nullptr ? option.widget->devicePixelRatioF() : 1.0;
    QSizeF size = QSizeF(image->size()) / ratio;
    if (size.width() > inner.width() || size.height() > inner.height()) {
      size.scale(QSizeF(inner.size()), Qt::KeepAspectRatio);
    }
    const QRectF target(inner.center().x() + 0.5 - size.width() / 2.0, inner.center().y() + 0.5 - size.height() / 2.0,
                        size.width(), size.height());
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter->drawImage(target, *image);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, false);
  } else if (state == RecentThumbnailState::unavailable) {
    // No picture for this file (docs/ui-conventions-menus.md): its kind, in the dimmed color.
    const auto suffix = QFileInfo(path).suffix().toUpper();
    painter->setFont(offset_font(option.font, 1, true));
    painter->setPen(theme().start_panel_muted_text);
    painter->drawText(inner, Qt::AlignCenter,
                      painter->fontMetrics().elidedText(suffix.isEmpty() ? QStringLiteral("?") : suffix,
                                                        Qt::ElideRight, inner.width() - 8));
  }

  const bool remove_hovered = list.remove_button_hovered(index.row());
  if (list.has_remove_button() && (hovered || selected || remove_hovered)) {
    paint_remove_button(painter, RecentFileList::grid_remove_button_rect(option.rect), remove_hovered, true);
  }

  const QRect name_rect(option.rect.left() + 6, picture.bottom() + 1 + kGridNameGap, option.rect.width() - 12,
                        ui_text_length(kGridNameLine));
  painter->setFont(option.font);
  painter->setPen(theme().text_primary);
  QString name = QFileInfo(path).fileName();
  if (name.isEmpty()) {
    name = QDir::toNativeSeparators(path);
  }
  painter->drawText(name_rect, Qt::AlignHCenter | Qt::AlignVCenter,
                    QFontMetrics(option.font).elidedText(name, Qt::ElideMiddle, name_rect.width()));
}

}  // namespace

int RecentFileList::grid_cell_height(int cell_width) {
  return kRecentGridCellInset + grid_picture_rect(QRect(0, 0, cell_width, 0)).height() + kGridNameGap +
         ui_text_length(kGridNameLine) + kGridBottom;
}

QSize RecentFileDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const {
  if (const auto* list = dynamic_cast<const RecentFileList*>(option.widget); list != nullptr && list->shows_cells()) {
    // The size the list set for its grid: asked once per cell on every layout, so it must be a
    // stored value, not a fresh measurement of the viewport and the style.
    return list->gridSize();
  }
  // A row is as wide as the viewport whatever this says (QListView widens it), so say as
  // little as possible: the hint is cached for every row, and a width taken while the box was
  // wide would keep the rows that wide, their text and remove button out of view, once the
  // window narrows.
  return QSize(1, recent_row_height());
}

void RecentFileDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
  painter->save();
  const QRect row = option.rect.adjusted(2, 2, -2, -2);
  const auto path = index.data(kRecentPathRole).toString();
  if (const auto* cells = dynamic_cast<const RecentFileList*>(option.widget);
      cells != nullptr && cells->shows_cells() && !path.isEmpty()) {
    paint_grid_cell(painter, option, index, *cells, path);
    painter->restore();
    return;
  }
  if (path.isEmpty()) {
    // The no-match placeholder: one dimmed line, and no hover or selection
    // chrome, because there is nothing here to click.
    painter->setPen(theme().start_panel_muted_text);
    painter->drawText(row.adjusted(10, 0, -10, 0), Qt::AlignLeft | Qt::AlignVCenter,
                      index.data(Qt::DisplayRole).toString());
    painter->restore();
    return;
  }

  const bool selected = (option.state & QStyle::State_Selected) != 0;
  const bool hovered = (option.state & QStyle::State_MouseOver) != 0;
  if (selected || hovered) {
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);
    painter->setBrush(selected ? theme().selection_soft_bg : theme().start_panel_row_hover_bg);
    painter->drawRoundedRect(row, 4.0, 4.0);
    painter->setRenderHint(QPainter::Antialiasing, false);
  }

  // The remove button of a list that offers one, drawn on the hovered row only;
  // the text gives up the button's width while it shows.
  const auto* list = dynamic_cast<const RecentFileList*>(option.widget);
  // The Open Recent menus highlight the row under the mouse as their current row,
  // so a selected row counts as hovered there.
  const bool show_remove = list != nullptr && list->has_remove_button() &&
                           (hovered || selected || list->remove_button_hovered(index.row()));
  int text_right_inset = 12;  // from option.rect: the row's 2 px inset plus the usual 10
  if (show_remove) {
    const QRect button = RecentFileList::remove_button_rect(option.rect);
    text_right_inset = option.rect.right() - button.left() + 4;
    paint_remove_button(painter, button, list->remove_button_hovered(index.row()), false);
  }

  const QFileInfo info(path);
  const QRect text_area = row.adjusted(10, 3, -(text_right_inset - 2), -3);
  const auto name_font = offset_font(option.font, 0, true);
  painter->setFont(name_font);
  painter->setPen(theme().text_primary);
  const QRect name_rect(text_area.left(), text_area.top(), text_area.width(), text_area.height() / 2);
  painter->drawText(name_rect, Qt::AlignLeft | Qt::AlignVCenter,
                    QFontMetrics(name_font).elidedText(recent_row_name(path), Qt::ElideMiddle, name_rect.width()));

  const auto path_font = offset_font(option.font, -1, false);
  painter->setFont(path_font);
  painter->setPen(theme().start_panel_muted_text);
  const QRect path_rect(text_area.left(), text_area.top() + text_area.height() / 2, text_area.width(),
                        text_area.height() - text_area.height() / 2);
  painter->drawText(path_rect, Qt::AlignLeft | Qt::AlignVCenter,
                    QFontMetrics(path_font).elidedText(QDir::toNativeSeparators(info.absolutePath()),
                                                       Qt::ElideMiddle, path_rect.width()));
  painter->restore();
}

int fill_recent_file_list(QListWidget& list, const QStringList& paths, const QString& filter_text,
                          const QString& placeholder) {
  list.clear();
  const auto tokens = recent_filter_tokens(filter_text);
  int rows = 0;
  for (const auto& path : paths) {
    if (!recent_path_matches(path, tokens)) {
      continue;
    }
    auto* item = new QListWidgetItem(recent_row_name(path), &list);
    item->setData(kRecentPathRole, path);
    item->setToolTip(QDir::toNativeSeparators(path));
    ++rows;
  }
  if (!paths.isEmpty() && rows == 0) {
    // Keeps the box where it is while a filter hides every row, instead of
    // collapsing it under the edit the user is typing into.
    auto* item = new QListWidgetItem(placeholder, &list);
    item->setData(kRecentPathRole, QString());
    item->setFlags(Qt::NoItemFlags);
  }
  if (auto* recent = dynamic_cast<RecentFileList*>(&list); recent != nullptr) {
    recent->rows_changed();  // cells or rows, by what the list now holds
  }
  list.updateGeometry();
  return rows;
}

}  // namespace patchy::ui
