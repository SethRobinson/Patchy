#include "ui/channel_panel.hpp"
#include "ui/action_icons.hpp"
#include "ui/modifier_names.hpp"
#include "ui/theme_palette.hpp"

#include <QAbstractItemModel>
#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QListWidget>
#include <QMenu>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QSignalBlocker>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace patchy::ui {

namespace {

// Draws a row's channel shortcut right-aligned in the text area and elides the name
// before it, so a narrow panel shortens the name, never the key. A key that does not
// fit beside the icon is left out.
class ChannelRowDelegate final : public QStyledItemDelegate {
public:
  ChannelRowDelegate(int shortcut_role, QObject* parent) : QStyledItemDelegate(parent), shortcut_role_(shortcut_role) {}

  void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
    const auto shortcut = index.data(shortcut_role_).toString();
    if (shortcut.isEmpty()) {
      QStyledItemDelegate::paint(painter, option, index);
      return;
    }
    QStyleOptionViewItem item_option(option);
    initStyleOption(&item_option, index);
    const auto* widget = option.widget;
    auto* style = widget != nullptr ? widget->style() : QApplication::style();
    const auto text_rect = style->subElementRect(QStyle::SE_ItemViewItemText, &item_option, widget);
    const QFontMetrics metrics(item_option.font);
    constexpr int kRightPadding = 8;
    constexpr int kGap = 10;
    const int shortcut_width = metrics.horizontalAdvance(shortcut);
    const QRect shortcut_rect(text_rect.right() + 1 - kRightPadding - shortcut_width, text_rect.top(), shortcut_width,
                              text_rect.height());
    if (shortcut_rect.left() < text_rect.left()) {
      QStyledItemDelegate::paint(painter, option, index);
      return;
    }
    item_option.text = metrics.elidedText(item_option.text, Qt::ElideRight,
                                          std::max(0, shortcut_rect.left() - kGap - text_rect.left()));
    style->drawControl(QStyle::CE_ItemViewItem, &item_option, painter, widget);
    painter->save();
    painter->setFont(item_option.font);
    const bool selected = (option.state & QStyle::State_Selected) != 0;
    painter->setPen(!(option.state & QStyle::State_Enabled) ? theme().text_disabled
                    : selected                              ? theme().list_selection_text
                                                            : theme().text_secondary);
    painter->drawText(shortcut_rect, Qt::AlignRight | Qt::AlignVCenter, shortcut);
    painter->restore();
  }

private:
  int shortcut_role_;
};

}  // namespace

ChannelPanel::ChannelPanel(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("channelPanel"));
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(6, 6, 6, 6);
  layout->setSpacing(5);

  list_ = new QListWidget(this);
  list_->setObjectName(QStringLiteral("channelList"));
  list_->setSelectionMode(QAbstractItemView::SingleSelection);
  list_->setDragEnabled(true);
  list_->setAcceptDrops(true);
  list_->setDropIndicatorShown(true);
  list_->setDragDropOverwriteMode(false);
  list_->setDefaultDropAction(Qt::MoveAction);
  list_->setDragDropMode(QAbstractItemView::InternalMove);
  list_->setIconSize(QSize(kEyeColumnWidth + kThumbnailSize.width(), kThumbnailSize.height()));
  list_->setSpacing(1);
  list_->viewport()->installEventFilter(this);
  list_->setContextMenuPolicy(Qt::CustomContextMenu);
  list_->setItemDelegate(new ChannelRowDelegate(kShortcutRole, list_));
  layout->addWidget(list_, 1);

  auto* action_bar = new QWidget(this);
  action_bar->setObjectName(QStringLiteral("channelActionBar"));
  auto* buttons = new QHBoxLayout(action_bar);
  buttons->setContentsMargins(0, 0, 0, 0);
  buttons->setSpacing(4);
  const auto add_button = [action_bar, buttons](const QString& object_name) {
    auto* button = new QToolButton(action_bar);
    button->setObjectName(object_name);
    button->setProperty("channelActionButton", true);
    button->setAutoRaise(false);
    button->setFocusPolicy(Qt::NoFocus);
    button->setFixedSize(34, 30);
    button->setIconSize(QSize(20, 20));
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    buttons->addWidget(button);
    return button;
  };
  create_button_ = add_button(QStringLiteral("channelNewButton"));
  save_selection_button_ = add_button(QStringLiteral("channelSaveSelectionButton"));
  load_selection_button_ = add_button(QStringLiteral("channelLoadSelectionButton"));
  buttons->addSpacing(4);
  rename_button_ = add_button(QStringLiteral("channelRenameButton"));
  invert_button_ = add_button(QStringLiteral("channelInvertButton"));
  remove_button_ = add_button(QStringLiteral("channelDeleteButton"));
  buttons->addStretch(1);
  layout->addWidget(action_bar);

  connect(list_, &QListWidget::currentItemChanged, this,
          [this](QListWidgetItem* current, QListWidgetItem*) { handle_current_item_changed(current); });
  connect(list_->model(), &QAbstractItemModel::rowsMoved, this,
          [this] { QTimer::singleShot(0, this, [this] { handle_rows_moved(); }); });
  // Clicking the row that is already current: Composite brings every channel back, a
  // color row shows that channel alone again (selection changes alone do not fire).
  connect(list_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
    if (item == nullptr || item != reselect_item_ || item != list_->currentItem() || !target_callback_) {
      return;
    }
    reselect_item_ = nullptr;
    const auto row = row_from_item(item);
    if (row.kind != RowKind::Alpha && row.kind != RowKind::Spot && row.kind != RowKind::QuickMask) {
      target_callback_(row.kind, row.id, false);
    }
  });
  connect(list_, &QWidget::customContextMenuRequested, this,
          [this](const QPoint& position) { show_context_menu(position); });

  retranslate_ui();
  refresh_action_states();
}

void ChannelPanel::set_rows(std::vector<Row> rows, std::optional<Row> selected) {
  updating_ = true;
  reselect_item_ = nullptr;
  committed_rows_ = rows;
  fixed_spot_positions_.clear();
  std::size_t saved_index = 0;
  for (const auto& row : rows) {
    if (row.kind == RowKind::Alpha || row.kind == RowKind::Spot) {
      if (row.kind == RowKind::Spot) {
        fixed_spot_positions_.emplace_back(saved_index, row.id);
      }
      ++saved_index;
    }
  }
  const QSignalBlocker blocker(list_);
  bool same_rows = list_->count() == static_cast<int>(rows.size());
  for (int index = 0; same_rows && index < list_->count(); ++index) {
    const auto* item = list_->item(index);
    same_rows = static_cast<RowKind>(item->data(kKindRole).toInt()) == rows[static_cast<std::size_t>(index)].kind &&
                static_cast<ChannelId>(item->data(kChannelIdRole).toULongLong()) ==
                    rows[static_cast<std::size_t>(index)].id;
  }
  if (same_rows) {
    QListWidgetItem* current = nullptr;
    for (int index = 0; index < list_->count(); ++index) {
      auto* item = list_->item(index);
      const auto& row = rows[static_cast<std::size_t>(index)];
      const bool saved = row.kind == RowKind::Alpha || row.kind == RowKind::Spot;
      const int eye = saved ? (row.overlay ? 1 : 0) : row.eye.has_value() ? (*row.eye ? 1 : 0) : -1;
      if (item->text() != row.name) {
        item->setText(row.name);
      }
      if (const auto shortcut = row_shortcuts_.contains(row.kind) ? row_shortcuts_.at(row.kind) : QString();
          item->data(kShortcutRole).toString() != shortcut) {
        item->setData(kShortcutRole, shortcut);
      }
      const auto old_thumbnail = item->data(kThumbnailRole).value<QPixmap>();
      if (item->data(kEyeRole).toInt() != eye || old_thumbnail.cacheKey() != row.thumbnail.cacheKey()) {
        item->setData(kThumbnailRole, row.thumbnail);
        item->setData(kEyeRole, eye);
        item->setIcon(row_icon(row.thumbnail, eye));
      }
      if (selected.has_value() && selected->kind == row.kind && selected->id == row.id) {
        current = item;
      }
    }
    if (current == nullptr && list_->count() > 0) {
      current = list_->item(0);
    }
    if (list_->currentItem() != current) {
      list_->setCurrentItem(current);
    }
    updating_ = false;
    refresh_action_states();
    return;
  }
  list_->clear();
  QListWidgetItem* selected_item = nullptr;
  for (auto& row : rows) {
    const bool saved = row.kind == RowKind::Alpha || row.kind == RowKind::Spot;
    const int eye = saved ? (row.overlay ? 1 : 0) : row.eye.has_value() ? (*row.eye ? 1 : 0) : -1;
    auto* item = new QListWidgetItem(row_icon(row.thumbnail, eye), row.name, list_);
    item->setData(kKindRole, static_cast<int>(row.kind));
    item->setData(kChannelIdRole, QVariant::fromValue<qulonglong>(row.id));
    item->setData(kThumbnailRole, row.thumbnail);
    item->setData(kEyeRole, eye);
    if (row_shortcuts_.contains(row.kind)) {
      item->setData(kShortcutRole, row_shortcuts_.at(row.kind));
    }
    auto flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (row.kind == RowKind::Alpha) {
      flags |= Qt::ItemIsDragEnabled;
      item->setToolTip(tr("Select for a grayscale view. Click the eye to show a colored overlay.") +
                       QLatin1Char('\n') +
                       resolve_modifier_names(tr("%CTRL%-click to load this channel as a selection.")));
    } else if (row.kind == RowKind::Spot) {
      item->setToolTip(tr("Spot channels can be previewed but not edited.") + QLatin1Char('\n') +
                       resolve_modifier_names(tr("%CTRL%-click to load this channel as a selection.")));
    } else if (row.kind == RowKind::QuickMask) {
      item->setToolTip(tr("Temporary selection mask. White selects, black masks, and gray creates partial selection."));
    } else if (row.kind == RowKind::Cyan || row.kind == RowKind::Magenta || row.kind == RowKind::Yellow ||
               row.kind == RowKind::Black) {
      item->setToolTip(tr("Preview this ink plate of the CMYK proof: darker means more ink. Plates come from "
                          "the working CMYK profile and are read-only.") +
                       QLatin1Char('\n') +
                       resolve_modifier_names(tr("%CTRL%-click to load where this plate has no ink as a selection.")));
    } else {
      const auto preview_tip = row.kind == RowKind::Composite
                                   ? tr("Show the normal composite image.")
                                   : tr("Preview this component as grayscale. Component channels are read-only.");
      item->setToolTip(preview_tip + QLatin1Char('\n') +
                       resolve_modifier_names(tr("%CTRL%-click to load this channel as a selection.")));
    }
    item->setFlags(flags);
    if (selected.has_value() && selected->kind == row.kind && selected->id == row.id) {
      selected_item = item;
    }
  }
  if (selected_item == nullptr && list_->count() > 0) {
    selected_item = list_->item(0);
  }
  list_->setCurrentItem(selected_item);
  updating_ = false;
  refresh_action_states();
}

void ChannelPanel::set_document_available(bool available) {
  document_available_ = available;
  if (!available) {
    channel_creation_available_ = false;
  }
  list_->setEnabled(available);
  refresh_action_states();
}

void ChannelPanel::set_channel_creation_available(bool available) {
  channel_creation_available_ = available;
  refresh_action_states();
}

void ChannelPanel::set_actions(QAction* create, QAction* save_selection, QAction* load_selection,
                               QAction* rename, QAction* invert, QAction* remove) {
  const auto bind_action = [](QToolButton* button, QAction* action) {
    button->setDefaultAction(action);
    button->setAccessibleName(action->text());
    QObject::connect(action, &QAction::changed, button,
                     [button, action] { button->setAccessibleName(action->text()); });
  };
  bind_action(create_button_, create);
  bind_action(save_selection_button_, save_selection);
  bind_action(load_selection_button_, load_selection);
  bind_action(rename_button_, rename);
  bind_action(invert_button_, invert);
  bind_action(remove_button_, remove);
  retranslate_ui();
  refresh_action_states();
}

void ChannelPanel::set_layer_actions(QAction* gray_layer, QAction* color_layer) {
  gray_layer_action_ = gray_layer;
  color_layer_action_ = color_layer;
  refresh_action_states();
}

void ChannelPanel::set_target_callback(TargetCallback callback) {
  target_callback_ = std::move(callback);
}

void ChannelPanel::set_load_selection_callback(LoadSelectionCallback callback) {
  load_selection_callback_ = std::move(callback);
}

void ChannelPanel::set_reorder_callback(ReorderCallback callback) {
  reorder_callback_ = std::move(callback);
}

void ChannelPanel::set_eye_callback(EyeCallback callback) {
  eye_callback_ = std::move(callback);
}

void ChannelPanel::set_row_shortcuts(std::map<RowKind, QString> shortcuts) {
  row_shortcuts_ = std::move(shortcuts);
  const QSignalBlocker blocker(list_);
  for (int row = 0; row < list_->count(); ++row) {
    auto* item = list_->item(row);
    const auto kind = static_cast<RowKind>(item->data(kKindRole).toInt());
    const auto shortcut = row_shortcuts_.contains(kind) ? row_shortcuts_.at(kind) : QString();
    if (item->data(kShortcutRole).toString() != shortcut) {
      item->setData(kShortcutRole, shortcut);
    }
  }
}

void ChannelPanel::set_row_thumbnail(RowKind kind, const QPixmap& thumbnail) {
  for (auto& row : committed_rows_) {
    if (row.kind == kind) {
      row.thumbnail = thumbnail;
    }
  }
  const QSignalBlocker blocker(list_);
  for (int row = 0; row < list_->count(); ++row) {
    auto* item = list_->item(row);
    if (static_cast<RowKind>(item->data(kKindRole).toInt()) == kind) {
      item->setData(kThumbnailRole, thumbnail);
      item->setIcon(row_icon(thumbnail, item->data(kEyeRole).toInt()));
    }
  }
}

QRect ChannelPanel::eye_rect(int row) const {
  const auto* item = list_->item(row);
  if (item == nullptr || item->data(kEyeRole).toInt() < 0) {
    return {};
  }
  const auto rect = list_->visualItemRect(item);
  // The eye is the left part of the row icon, which starts a few pixels in.
  return QRect(rect.left(), rect.top(), kEyeColumnWidth + 6, rect.height());
}

std::optional<bool> ChannelPanel::row_eye(int row) const {
  const auto* item = list_->item(row);
  if (item == nullptr || item->data(kEyeRole).toInt() < 0) {
    return std::nullopt;
  }
  return item->data(kEyeRole).toInt() == 1;
}

QPixmap ChannelPanel::row_thumbnail(int row) const {
  const auto* item = list_->item(row);
  return item != nullptr ? item->data(kThumbnailRole).value<QPixmap>() : QPixmap();
}

QString ChannelPanel::row_shortcut(int row) const {
  const auto* item = list_->item(row);
  return item != nullptr ? item->data(kShortcutRole).toString() : QString();
}

QIcon ChannelPanel::row_icon(const QPixmap& thumbnail, int eye) const {
  const auto ratio = std::max<qreal>(1.0, devicePixelRatioF());
  const QSize size(kEyeColumnWidth + kThumbnailSize.width(), kThumbnailSize.height());
  QPixmap result(size * ratio);
  result.setDevicePixelRatio(ratio);
  result.fill(Qt::transparent);
  QPainter painter(&result);
  if (eye >= 0) {
    const auto icon = simple_icon(eye != 0 ? QStringLiteral("eye") : QStringLiteral("eyeOff"),
                                  eye != 0 ? theme().layer_eye_on : theme().layer_eye_off);
    icon.paint(&painter, QRect(2, (size.height() - 16) / 2, 16, 16));
  }
  if (!thumbnail.isNull()) {
    painter.drawPixmap(QRect(QPoint(kEyeColumnWidth, 0), kThumbnailSize), thumbnail);
  }
  painter.end();
  return QIcon(result);
}

void ChannelPanel::refresh_row_icons() {
  const QSignalBlocker blocker(list_);
  for (int row = 0; row < list_->count(); ++row) {
    auto* item = list_->item(row);
    item->setIcon(row_icon(item->data(kThumbnailRole).value<QPixmap>(), item->data(kEyeRole).toInt()));
  }
}

bool ChannelPanel::handle_eye_press(QPoint viewport_position) {
  auto* item = list_->itemAt(viewport_position);
  if (item == nullptr || !document_available_) {
    return false;
  }
  if (!eye_rect(list_->row(item)).contains(viewport_position)) {
    return false;
  }
  const auto kind = static_cast<RowKind>(item->data(kKindRole).toInt());
  if (kind == RowKind::Alpha || kind == RowKind::Spot) {
    toggle_saved_channel_overlay(item);
  } else if (eye_callback_) {
    eye_callback_(kind);
  }
  return true;
}

void ChannelPanel::toggle_saved_channel_overlay(QListWidgetItem* item) {
  const bool on = item->data(kEyeRole).toInt() != 1;
  {
    const QSignalBlocker blocker(list_);
    if (on) {
      for (int row = 0; row < list_->count(); ++row) {
        auto* other = list_->item(row);
        const auto kind = static_cast<RowKind>(other->data(kKindRole).toInt());
        if (other != item && (kind == RowKind::Alpha || kind == RowKind::Spot)) {
          other->setData(kEyeRole, 0);
        }
      }
    }
    item->setData(kEyeRole, on ? 1 : 0);
    if (on) {
      list_->setCurrentItem(item);
    }
  }
  refresh_row_icons();
  refresh_action_states();
  if (item == list_->currentItem() && target_callback_) {
    const auto selected = row_from_item(item);
    target_callback_(selected.kind, selected.id, selected.overlay);
  }
}

std::optional<ChannelPanel::Row> ChannelPanel::selected_row() const {
  const auto* item = list_->currentItem();
  if (item == nullptr) {
    return std::nullopt;
  }
  return row_from_item(item);
}

std::vector<ChannelId> ChannelPanel::saved_channel_order() const {
  std::vector<ChannelId> result;
  for (int row = 0; row < list_->count(); ++row) {
    const auto* item = list_->item(row);
    const auto kind = static_cast<RowKind>(item->data(kKindRole).toInt());
    if (kind == RowKind::Alpha || kind == RowKind::Spot) {
      result.push_back(static_cast<ChannelId>(item->data(kChannelIdRole).toULongLong()));
    }
  }
  return result;
}

void ChannelPanel::changeEvent(QEvent* event) {
  if (event != nullptr && event->type() == QEvent::LanguageChange) {
    retranslate_ui();
  }
  if (event != nullptr && (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange)) {
    refresh_row_icons();  // the eyes follow the color scheme
  }
  QWidget::changeEvent(event);
}

bool ChannelPanel::eventFilter(QObject* watched, QEvent* event) {
  if (watched == list_->viewport() && event != nullptr) {
    if (event->type() == QEvent::ToolTip) {
      const auto* help = static_cast<QHelpEvent*>(event);
      if (auto* item = list_->itemAt(help->pos());
          item != nullptr && eye_rect(list_->row(item)).contains(help->pos())) {
        const auto kind = static_cast<RowKind>(item->data(kKindRole).toInt());
        QToolTip::showText(help->globalPos(),
                           kind == RowKind::Alpha || kind == RowKind::Spot
                               ? tr("Show or hide this channel as a colored overlay")
                               : kind == RowKind::Composite
                                     ? tr("Show every color channel")
                                     : tr("Show or hide this channel. At least one channel stays visible."),
                           list_->viewport());
        return true;
      }
    }
    if (event->type() == QEvent::MouseMove && (eye_mouse_press_ || ctrl_load_mouse_press_)) {
      // The list's drag selection would select the row under the pointer, the row whose
      // eye was just clicked, on the smallest move while the button is held.
      event->accept();
      return true;
    }
    if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick) {
      eye_mouse_press_ = false;
      const auto* mouse_event = static_cast<QMouseEvent*>(event);
      if (mouse_event->button() == Qt::LeftButton &&
          (mouse_event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier)) == 0 &&
          handle_eye_press(mouse_event->position().toPoint())) {
        // An eye click never selects the row or starts a drag (rapid clicks arrive as
        // press/double-click pairs; each one toggles).
        eye_mouse_press_ = true;
        event->accept();
        return true;
      }
    }
    if (event->type() == QEvent::MouseButtonRelease && eye_mouse_press_) {
      eye_mouse_press_ = false;
      event->accept();
      return true;
    }
    if (event->type() == QEvent::MouseButtonPress) {
      ctrl_load_mouse_press_ = false;
      if (auto* pressed = list_->itemAt(static_cast<QMouseEvent*>(event)->position().toPoint());
          pressed != nullptr && pressed == list_->currentItem()) {
        reselect_item_ = pressed;
      } else {
        reselect_item_ = nullptr;
      }
      auto* mouse_event = static_cast<QMouseEvent*>(event);
      const auto load_modifier =
          (mouse_event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier)) != 0;
      if (mouse_event->button() == Qt::LeftButton && load_modifier && document_available_ &&
          load_selection_callback_) {
        if (auto* item = list_->itemAt(mouse_event->position().toPoint()); item != nullptr) {
          const auto row = row_from_item(item);
          ctrl_load_mouse_press_ = true;
          load_selection_callback_(row.kind, row.id);
          event->accept();
          return true;
        }
      }
    } else if (event->type() == QEvent::MouseButtonRelease && ctrl_load_mouse_press_) {
      auto* mouse_event = static_cast<QMouseEvent*>(event);
      if (mouse_event->button() == Qt::LeftButton) {
        ctrl_load_mouse_press_ = false;
        event->accept();
        return true;
      }
    }
  }
  return QWidget::eventFilter(watched, event);
}

ChannelPanel::Row ChannelPanel::row_from_item(const QListWidgetItem* item) const {
  Row row;
  if (item == nullptr) {
    return row;
  }
  row.kind = static_cast<RowKind>(item->data(kKindRole).toInt());
  row.id = static_cast<ChannelId>(item->data(kChannelIdRole).toULongLong());
  row.name = item->text();
  row.thumbnail = item->data(kThumbnailRole).value<QPixmap>();
  const auto eye = item->data(kEyeRole).toInt();
  row.overlay = (row.kind == RowKind::Alpha || row.kind == RowKind::Spot) && eye == 1;
  if (eye >= 0) {
    row.eye = eye == 1;
  }
  return row;
}

void ChannelPanel::handle_current_item_changed(QListWidgetItem* current) {
  refresh_action_states();
  if (updating_ || current == nullptr || !target_callback_) {
    return;
  }
  const auto selected = row_from_item(current);
  if (selected.kind != RowKind::Alpha && selected.kind != RowKind::Spot) {
    bool changed = false;
    {
      const QSignalBlocker blocker(list_);
      for (int row = 0; row < list_->count(); ++row) {
        auto* item = list_->item(row);
        const auto kind = static_cast<RowKind>(item->data(kKindRole).toInt());
        if ((kind == RowKind::Alpha || kind == RowKind::Spot) && item->data(kEyeRole).toInt() == 1) {
          item->setData(kEyeRole, 0);
          changed = true;
        }
      }
    }
    if (changed) {
      refresh_row_icons();
    }
  }
  target_callback_(selected.kind, selected.id, selected.overlay);
}

void ChannelPanel::handle_rows_moved() {
  if (updating_) {
    return;
  }
  // The derived rows (Composite, the components, any ink plates and Quick Mask) lead the
  // list in a fixed order; only saved channels move.
  std::size_t fixed_rows = 0;
  while (fixed_rows < committed_rows_.size() && committed_rows_[fixed_rows].kind != RowKind::Alpha &&
         committed_rows_[fixed_rows].kind != RowKind::Spot) {
    ++fixed_rows;
  }
  bool derived_rows_moved = list_->count() < static_cast<int>(fixed_rows);
  for (std::size_t row = 0; !derived_rows_moved && row < fixed_rows; ++row) {
    derived_rows_moved = static_cast<RowKind>(list_->item(static_cast<int>(row))->data(kKindRole).toInt()) !=
                         committed_rows_[row].kind;
  }
  const auto order = saved_channel_order();
  const auto spot_moved = std::any_of(fixed_spot_positions_.begin(), fixed_spot_positions_.end(),
                                      [&order](const auto& fixed) {
                                        return fixed.first >= order.size() || order[fixed.first] != fixed.second;
                                      });
  if (derived_rows_moved || spot_moved) {
    const auto selected = selected_row();
    set_rows(committed_rows_, selected);
    return;
  }
  if (reorder_callback_) {
    reorder_callback_(order);
  }
}

void ChannelPanel::show_context_menu(const QPoint& position) {
  if (auto* item = list_->itemAt(position); item != nullptr) {
    list_->setCurrentItem(item);
  }
  refresh_action_states();

  QMenu menu(this);
  menu.setObjectName(QStringLiteral("channelContextMenu"));
  menu.addAction(create_button_->defaultAction());
  menu.addAction(save_selection_button_->defaultAction());
  menu.addAction(load_selection_button_->defaultAction());
  menu.addSeparator();
  menu.addAction(rename_button_->defaultAction());
  menu.addAction(invert_button_->defaultAction());
  menu.addAction(remove_button_->defaultAction());
  if (gray_layer_action_ != nullptr && color_layer_action_ != nullptr) {
    menu.addSeparator();
    menu.addAction(gray_layer_action_);
    menu.addAction(color_layer_action_);
  }
  menu.exec(list_->viewport()->mapToGlobal(position));
}

void ChannelPanel::refresh_action_states() {
  const auto selected = selected_row();
  const bool loadable = selected.has_value() &&
                        selected->kind != RowKind::QuickMask;
  const bool editable_alpha = selected.has_value() && selected->kind == RowKind::Alpha;
  if (create_button_->defaultAction() != nullptr) {
    create_button_->defaultAction()->setEnabled(document_available_ && channel_creation_available_);
  }
  if (save_selection_button_->defaultAction() != nullptr) {
    save_selection_button_->defaultAction()->setEnabled(document_available_ && channel_creation_available_);
  }
  if (load_selection_button_->defaultAction() != nullptr) {
    load_selection_button_->defaultAction()->setEnabled(document_available_ && loadable);
  }
  if (rename_button_->defaultAction() != nullptr) {
    rename_button_->defaultAction()->setEnabled(document_available_ && editable_alpha);
  }
  if (invert_button_->defaultAction() != nullptr) {
    invert_button_->defaultAction()->setEnabled(document_available_ && editable_alpha);
  }
  if (remove_button_->defaultAction() != nullptr) {
    remove_button_->defaultAction()->setEnabled(document_available_ && editable_alpha);
  }
  if (gray_layer_action_ != nullptr) {
    gray_layer_action_->setEnabled(document_available_ && loadable);
  }
  if (color_layer_action_ != nullptr) {
    const bool colored = selected.has_value() &&
                         (selected->kind == RowKind::Red || selected->kind == RowKind::Green ||
                          selected->kind == RowKind::Blue || selected->kind == RowKind::Cyan ||
                          selected->kind == RowKind::Magenta || selected->kind == RowKind::Yellow ||
                          selected->kind == RowKind::Black);
    color_layer_action_->setEnabled(document_available_ && colored);
  }
}

void ChannelPanel::retranslate_ui() {
  const auto update_button = [](QToolButton* button, const QString& fallback_tooltip) {
    if (button->defaultAction() == nullptr) {
      button->setToolTip(fallback_tooltip);
      button->setAccessibleName(fallback_tooltip);
      return;
    }
    button->setToolTip(button->defaultAction()->toolTip());
    button->setAccessibleName(button->defaultAction()->text());
  };
  update_button(create_button_, tr("New alpha channel"));
  update_button(save_selection_button_, tr("Save selection as channel"));
  update_button(load_selection_button_, tr("Load channel as selection"));
  update_button(rename_button_, tr("Rename channel"));
  update_button(invert_button_, tr("Invert channel"));
  update_button(remove_button_, tr("Delete channel"));
}

}  // namespace patchy::ui
