#include "ui/recent_file_list.hpp"

#include "ui/dialog_utils.hpp"
#include "ui/theme_palette.hpp"

#include <QDir>
#include <QListWidget>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHelpEvent>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
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
  const auto rows = fixed_rows_ > 0 ? fixed_rows_ : count();
  return {QListWidget::sizeHint().width(), std::clamp(rows, 1, visible_rows_) * kRecentRowHeight + kRecentListChrome};
}

QSize RecentFileList::minimumSizeHint() const {
  return {QListWidget::minimumSizeHint().width(), kRecentRowHeight + kRecentListChrome};
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

int RecentFileList::remove_button_row_at(const QPoint& position) const {
  if (!remove_handler_) {
    return -1;
  }
  const auto index = indexAt(position);
  if (!index.isValid() || index.data(kRecentPathRole).toString().isEmpty()) {
    return -1;
  }
  return remove_button_rect(visualRect(index)).contains(position) ? index.row() : -1;
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

QSize RecentFileDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const {
  return QSize(option.rect.width(), kRecentRowHeight);
}

void RecentFileDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
  painter->save();
  const QRect row = option.rect.adjusted(2, 2, -2, -2);
  const auto path = index.data(kRecentPathRole).toString();
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
    painter->setRenderHint(QPainter::Antialiasing, true);
    if (list->remove_button_hovered(index.row())) {
      painter->setPen(Qt::NoPen);
      painter->setBrush(theme().start_panel_row_remove_hover_bg);
      painter->drawRoundedRect(button, 4.0, 4.0);
    }
    QPen cross(list->remove_button_hovered(index.row()) ? theme().text_primary : theme().start_panel_muted_text);
    cross.setWidthF(1.6);
    cross.setCapStyle(Qt::RoundCap);
    painter->setPen(cross);
    const QRectF glyph = QRectF(button).adjusted(7.0, 7.0, -7.0, -7.0);
    painter->drawLine(glyph.topLeft(), glyph.bottomRight());
    painter->drawLine(glyph.topRight(), glyph.bottomLeft());
    painter->setRenderHint(QPainter::Antialiasing, false);
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
  list.updateGeometry();
  return rows;
}

}  // namespace patchy::ui
