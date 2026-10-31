#include "ui/command_search.hpp"

#include "ui/hotkey_registry.hpp"
#include "ui/theme_palette.hpp"
#include "ui/theme_qss.hpp"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QFrame>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QSet>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <algorithm>
#include <memory>
#include <utility>

namespace patchy::ui {

namespace {

constexpr int kPopupWidth = 560;
constexpr int kVisibleRows = 12;
constexpr int kRowPadding = 6;
constexpr int kCheckColumn = 22;
constexpr int kButtonWidth = 30;
constexpr int kButtonGap = 2;

constexpr int kPathRole = Qt::UserRole + 1;
constexpr int kShortcutRole = Qt::UserRole + 2;
constexpr int kCheckRole = Qt::UserRole + 3;  // -1 not checkable, 0 off, 1 on
constexpr int kEntryRole = Qt::UserRole + 4;

void walk_menu(QMenu* menu, bool visible_only, const QString& top, QStringList& submenus,
               const std::function<void(QAction*, const QString&, const QStringList&)>& visit) {
  for (auto* action : menu->actions()) {
    if (action == nullptr || action->isSeparator() || qobject_cast<QWidgetAction*>(action) != nullptr) {
      continue;
    }
    if (visible_only && !action->isVisible()) {
      continue;
    }
    if (auto* submenu = action->menu(); submenu != nullptr) {
      if (visible_only && !action->isEnabled()) {
        continue;
      }
      submenus.push_back(clean_action_text(action));
      walk_menu(submenu, visible_only, top, submenus, visit);
      submenus.pop_back();
      continue;
    }
    visit(action, top, submenus);
  }
}

// Name, then the menu path in the muted color, the shortcut at the right edge; a check
// mark in the left column for a checked command.
class CommandRowDelegate final : public QStyledItemDelegate {
public:
  using QStyledItemDelegate::QStyledItemDelegate;

  QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const override {
    return {kPopupWidth, option.fontMetrics.height() + 2 * kRowPadding};
  }

  void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
    painter->save();
    const auto rect = option.rect;
    const bool selected = (option.state & QStyle::State_Selected) != 0;
    if (selected) {
      painter->fillRect(rect, theme().menu_item_selected_bg);
    } else if ((option.state & QStyle::State_MouseOver) != 0) {
      painter->fillRect(rect, theme().list_row_hover_bg);
    }
    const QColor primary = selected ? theme().text_on_accent : theme().text_primary;
    const QColor muted = selected ? theme().text_on_accent : theme().text_secondary;
    const auto& metrics = option.fontMetrics;
    const int baseline = rect.top() + (rect.height() - metrics.height()) / 2 + metrics.ascent();

    if (index.data(kCheckRole).toInt() == 1) {
      painter->setRenderHint(QPainter::Antialiasing);
      painter->setPen(QPen(primary, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      const QPointF center(rect.left() + kCheckColumn / 2.0 + 1, rect.center().y() + 0.5);
      QPainterPath check;
      check.moveTo(center + QPointF(-4.0, 0.0));
      check.lineTo(center + QPointF(-1.2, 3.0));
      check.lineTo(center + QPointF(4.0, -3.5));
      painter->drawPath(check);
    }

    int right = rect.right() - 2 * kRowPadding;
    const auto shortcut = index.data(kShortcutRole).toString();
    if (!shortcut.isEmpty()) {
      const int width = metrics.horizontalAdvance(shortcut);
      painter->setPen(muted);
      painter->drawText(right - width, baseline, shortcut);
      right -= width + 3 * kRowPadding;
    }
    int x = rect.left() + kCheckColumn;
    const auto name = metrics.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, right - x);
    painter->setPen(primary);
    painter->drawText(x, baseline, name);
    x += metrics.horizontalAdvance(name) + 3 * kRowPadding;
    const auto path = index.data(kPathRole).toString();
    if (!path.isEmpty() && x < right) {
      painter->setPen(muted);
      painter->drawText(x, baseline, metrics.elidedText(path, Qt::ElideLeft, right - x));
    }
    painter->restore();
  }
};

// Keeps typing in the field: Up/Down/PageUp/PageDown move the list, Enter runs the
// chosen command, Esc closes.
class SearchKeyFilter final : public QObject {
public:
  SearchKeyFilter(QListWidget* list, std::function<void()> run, QObject* parent)
      : QObject(parent), list_(list), run_(std::move(run)) {}

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() == QEvent::ShortcutOverride) {
      // Plain typing belongs to the field, never an application shortcut.
      const auto* key = static_cast<const QKeyEvent*>(event);
      if (key->key() == Qt::Key_Escape || key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
        event->accept();
        return true;
      }
    }
    if (event->type() == QEvent::KeyPress) {
      const auto* key = static_cast<const QKeyEvent*>(event);
      switch (key->key()) {
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_PageUp:
        case Qt::Key_PageDown:
          QApplication::sendEvent(list_, event);
          return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
          run_();
          return true;
        case Qt::Key_Escape:
          if (auto* popup = list_->window(); popup != nullptr) {
            popup->close();
          }
          return true;
        default:
          break;
      }
    }
    return QObject::eventFilter(watched, event);
  }

private:
  QListWidget* list_;
  std::function<void()> run_;
};

// Places the menu bar's search button right after the last visible menu title.
class SearchButtonPlacer final : public QObject {
public:
  SearchButtonPlacer(QMenuBar* bar, QToolButton* button) : QObject(button), bar_(bar), button_(button) {
    bar_->installEventFilter(this);
  }

  void place() {
    if (bar_ == nullptr || button_ == nullptr) {
      return;
    }
    QAction* last = nullptr;
    for (auto* action : bar_->actions()) {
      if (action->isVisible() && action->menu() != nullptr) {
        last = action;
      }
    }
    const auto title = last != nullptr ? bar_->actionGeometry(last) : QRect();
    // A title the bar moved into its overflow (">>") menu is not at its own rectangle.
    const bool title_shown = title.isValid() && bar_->actionAt(title.center()) == last;
    int right_limit = bar_->width();
    if (const auto* controls =
            bar_->findChild<QWidget*>(QStringLiteral("windowChromeControls"), Qt::FindDirectChildrenOnly);
        controls != nullptr && controls->isVisible()) {
      right_limit = controls->x();
    }
    const int x = title.right() + 1 + kButtonGap;
    if (bar_->isNativeMenuBar() || !title_shown || x + kButtonWidth > right_limit) {
      button_->hide();
      return;
    }
    button_->setGeometry(x, 0, kButtonWidth, bar_->height());
    button_->show();
    button_->raise();
  }

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (watched == bar_) {
      switch (event->type()) {
        case QEvent::Resize:
        case QEvent::Show:
        case QEvent::LayoutRequest:
        case QEvent::LanguageChange:
        case QEvent::FontChange:
        case QEvent::StyleChange:
        case QEvent::ActionAdded:
        case QEvent::ActionChanged:
        case QEvent::ActionRemoved:
          // The bar lays its titles out when it next handles the change; place after it.
          if (!pending_) {
            pending_ = true;
            QTimer::singleShot(0, this, [this] {
              pending_ = false;
              place();
            });
          }
          break;
        default:
          break;
      }
    }
    return QObject::eventFilter(watched, event);
  }

private:
  QPointer<QMenuBar> bar_;
  QPointer<QToolButton> button_;
  bool pending_{false};
};

}  // namespace

QString strip_menu_ellipsis(QString label) {
  while (label.endsWith(QStringLiteral("..."))) {
    label.chop(3);
  }
  while (label.endsWith(QChar(0x2026))) {
    label.chop(1);
  }
  return label.trimmed();
}

void for_each_menu_bar_command(
    const QMenuBar* menu_bar, bool visible_only,
    const std::function<void(QAction* action, const QString& top, const QStringList& submenus)>& visit) {
  if (menu_bar == nullptr) {
    return;
  }
  for (auto* top_action : menu_bar->actions()) {
    auto* menu = top_action->menu();
    if (menu == nullptr || (visible_only && !top_action->isVisible())) {
      continue;
    }
    QStringList submenus;
    walk_menu(menu, visible_only, clean_action_text(top_action), submenus, visit);
  }
}

std::vector<MenuCommandEntry> collect_menu_commands(const QMenuBar* menu_bar) {
  std::vector<MenuCommandEntry> entries;
  QSet<const QAction*> seen;
  const QString separator = QStringLiteral(" › ");
  for_each_menu_bar_command(menu_bar, true, [&](QAction* action, const QString& top, const QStringList& submenus) {
    if (!action->isEnabled() || seen.contains(action)) {
      return;
    }
    const auto name = strip_menu_ellipsis(clean_action_text(action));
    if (name.isEmpty()) {
      return;
    }
    seen.insert(action);
    auto path = top;
    for (const auto& submenu : submenus) {
      path += separator + strip_menu_ellipsis(submenu);
    }
    entries.push_back({action, name, path});
  });
  return entries;
}

std::vector<int> match_menu_commands(const std::vector<MenuCommandEntry>& entries, const QString& query) {
  const auto words = query.toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
  const auto phrase = words.join(QLatin1Char(' '));
  std::vector<std::pair<int, int>> ranked;  // (tier, index)
  for (int index = 0; index < static_cast<int>(entries.size()); ++index) {
    const auto& entry = entries[static_cast<std::size_t>(index)];
    const auto name = entry.name.toLower();
    const auto path = entry.path.toLower();
    bool all = true;
    bool all_in_name = true;
    for (const auto& word : words) {
      const bool in_name = name.contains(word);
      all_in_name = all_in_name && in_name;
      if (!in_name && !path.contains(word)) {
        all = false;
        break;
      }
    }
    if (!all) {
      continue;
    }
    const int tier = phrase.isEmpty() ? 0 : name.startsWith(phrase) ? 0 : all_in_name ? 1 : 2;
    ranked.emplace_back(tier, index);
  }
  std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  std::vector<int> result;
  result.reserve(ranked.size());
  for (const auto& [tier, index] : ranked) {
    result.push_back(index);
  }
  return result;
}

QFrame* show_command_search_popup(QWidget* parent, std::vector<MenuCommandEntry> entries, QPoint top_left) {
  auto* popup = new QFrame(parent, Qt::Popup);
  popup->setAttribute(Qt::WA_DeleteOnClose);
  popup->setObjectName(QStringLiteral("commandSearchPopup"));
  popup->setFrameShape(QFrame::StyledPanel);
  set_themed_style(*popup, QStringLiteral(R"(
    QFrame#commandSearchPopup {
      background: @menu_bg;
      border: 1px solid @menu_border;
    }
    QFrame#commandSearchPopup QListWidget {
      background: @menu_bg;
      color: @text_primary;
      border: 0;
      outline: none;
    }
  )"));

  auto* layout = new QVBoxLayout(popup);
  layout->setContentsMargins(6, 6, 6, 6);
  layout->setSpacing(6);

  auto* search = new QLineEdit(popup);
  search->setObjectName(QStringLiteral("commandSearchEdit"));
  search->setPlaceholderText(QCoreApplication::translate("patchy::ui::CommandSearch", "Search menu commands"));
  search->setClearButtonEnabled(true);
  layout->addWidget(search);

  auto* list = new QListWidget(popup);
  list->setObjectName(QStringLiteral("commandSearchList"));
  list->setItemDelegate(new CommandRowDelegate(list));
  list->setUniformItemSizes(true);
  list->setSelectionMode(QAbstractItemView::SingleSelection);
  list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  list->setFocusPolicy(Qt::NoFocus);
  list->setMouseTracking(true);
  list->viewport()->setAttribute(Qt::WA_Hover, true);
  // Polished first: the application sheet's QListWidget min-height would otherwise replace
  // this minimum when the popup shows, and its font sets the row height.
  list->ensurePolished();
  const int row_height = list->fontMetrics().height() + 2 * kRowPadding;
  list->setFixedHeight(row_height * kVisibleRows + 2 * list->frameWidth());
  layout->addWidget(list);

  auto shared_entries = std::make_shared<std::vector<MenuCommandEntry>>(std::move(entries));
  const auto refill = [list, shared_entries](const QString& query) {
    list->clear();
    for (const int index : match_menu_commands(*shared_entries, query)) {
      const auto& entry = (*shared_entries)[static_cast<std::size_t>(index)];
      if (entry.action == nullptr) {
        continue;
      }
      auto* item = new QListWidgetItem(entry.name, list);
      item->setData(kPathRole, entry.path);
      item->setData(kShortcutRole, action_shortcut_text(entry.action.data()));
      item->setData(kCheckRole, entry.action->isCheckable() ? (entry.action->isChecked() ? 1 : 0) : -1);
      item->setData(kEntryRole, index);
    }
    if (list->count() > 0) {
      list->setCurrentRow(0);
    }
  };
  const auto run = [popup, list, shared_entries] {
    auto* item = list->currentItem();
    if (item == nullptr) {
      return;
    }
    const QPointer<QAction> action = (*shared_entries)[static_cast<std::size_t>(item->data(kEntryRole).toInt())].action;
    popup->close();
    if (action == nullptr) {
      return;
    }
    // After the popup is gone, so a command that opens a dialog or a menu never fights
    // the popup's grab.
    QTimer::singleShot(0, action.data(), [action] {
      if (action != nullptr && action->isEnabled()) {
        action->trigger();
      }
    });
  };
  QObject::connect(search, &QLineEdit::textChanged, popup, refill);
  QObject::connect(list, &QListWidget::itemClicked, popup, [run] { run(); });
  search->installEventFilter(new SearchKeyFilter(list, run, search));
  refill(QString());

  popup->resize(kPopupWidth, popup->sizeHint().height());
  auto position = top_left;
  const auto* screen = parent != nullptr ? parent->screen() : QGuiApplication::screenAt(top_left);
  if (screen == nullptr) {
    screen = QGuiApplication::primaryScreen();
  }
  if (screen != nullptr) {
    const auto available = screen->availableGeometry();
    position.setX(std::clamp(position.x(), available.left(),
                             std::max(available.left(), available.right() - popup->width() + 1)));
    position.setY(std::clamp(position.y(), available.top(),
                             std::max(available.top(), available.bottom() - popup->height() + 1)));
  }
  popup->move(position);
  popup->show();
  search->setFocus(Qt::PopupFocusReason);
  return popup;
}

QToolButton* add_menu_bar_search_button(QMenuBar* menu_bar, QAction* action) {
  auto* button = new QToolButton(menu_bar);
  button->setObjectName(QStringLiteral("menuBarCommandSearchButton"));
  button->setDefaultAction(action);
  button->setFocusPolicy(Qt::NoFocus);
  button->setAutoRaise(true);
  button->setIconSize(QSize(16, 16));
  set_themed_style(*button, QStringLiteral(R"(
    QToolButton#menuBarCommandSearchButton {
      background: transparent;
      border: 0;
      padding: 0;
      min-width: 0;
      min-height: 0;
    }
    QToolButton#menuBarCommandSearchButton:hover,
    QToolButton#menuBarCommandSearchButton:pressed {
      background: @menu_bar_item_hover_bg;
    }
  )"));
  auto* placer = new SearchButtonPlacer(menu_bar, button);
  placer->place();
  return button;
}

}  // namespace patchy::ui
