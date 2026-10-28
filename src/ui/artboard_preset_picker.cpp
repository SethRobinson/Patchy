#include "ui/artboard_preset_picker.hpp"

#include "ui/icon_theme.hpp"
#include "ui/theme_palette.hpp"
#include "ui/theme_qss.hpp"
#include "ui/ui_text_size.hpp"

#include <QAction>
#include <QCoreApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QPaintEvent>
#include <QStyleOptionToolButton>
#include <QStylePainter>
#include <QWidgetAction>

#include <algorithm>

namespace patchy::ui {

namespace {

constexpr int kChevronSize = 12;
constexpr int kTextInset = 6;
constexpr int kChevronRoom = kChevronSize + 10;

// Menu text treats '&' as a mnemonic marker ("Icons & Games" showed as "Icons _Games").
QString menu_text(QString text) {
  return text.replace(QLatin1Char('&'), QStringLiteral("&&"));
}

QString preset_label(const ArtboardSizePreset& preset) {
  // The string the old combo showed, so every catalog already has it.
  return QObject::tr("%1 (%2 x %3)").arg(preset.name).arg(preset.size.width()).arg(preset.size.height());
}

}  // namespace

ArtboardPresetPicker::ArtboardPresetPicker(QWidget* parent) : QToolButton(parent) {
  setObjectName(QStringLiteral("artboardPresetButton"));
  setPopupMode(QToolButton::InstantPopup);
  setToolButtonStyle(Qt::ToolButtonTextOnly);
  setFocusPolicy(Qt::NoFocus);
  set_themed_style(*this, QStringLiteral(R"(
    QToolButton#artboardPresetButton {
      background: @field_bg;
      border: 1px solid @field_inset_border;
      border-top-color: @field_bevel_top;
      border-radius: 0;
      padding: 0;
      min-height: 24px;
      max-height: 24px;
    }
    QToolButton#artboardPresetButton:hover {
      border-color: @button_hover_border;
    }
    QToolButton#artboardPresetButton:disabled {
      background: @field_bg_disabled;
      border-color: @field_border_disabled;
    }
    QToolButton#artboardPresetButton::menu-indicator {
      width: 0;
    }
  )"));
  menu_ = new QMenu(this);
  menu_->setObjectName(QStringLiteral("artboardPresetMenu"));
  setMenu(menu_);
  // A stale search from a dismissed menu resets before showing again; the focused field is
  // what routes typed keys into the search.
  connect(menu_, &QMenu::aboutToShow, this, [this] {
    if (search_ != nullptr) {
      search_->clear();
      apply_search(QString());
      search_->setFocus();
    }
  });
  rebuild();
}

void ArtboardPresetPicker::rebuild() {
  for (auto* action : group_actions_) {
    action->menu()->deleteLater();
  }
  menu_->clear();
  group_actions_.clear();
  result_actions_.clear();
  presets_ = artboard_size_presets();

  auto* search_row = new QWidget(menu_);
  search_row->setObjectName(QStringLiteral("artboardPresetSearchRow"));
  auto* search_layout = new QHBoxLayout(search_row);
  search_layout->setContentsMargins(8, 6, 8, 6);
  search_ = new QLineEdit(search_row);
  search_->setObjectName(QStringLiteral("artboardPresetSearchEdit"));
  search_->setClearButtonEnabled(true);
  search_->setFixedHeight(24);
  // The field's own Cut/Copy/Paste popup must not nest inside the menu popup.
  search_->setContextMenuPolicy(Qt::NoContextMenu);
  search_->setPlaceholderText(tr("Search presets..."));
  search_->installEventFilter(this);
  search_layout->addWidget(search_);
  // The global QWidget rule would otherwise paint @window_bg over the menu background.
  set_themed_style(*search_row, QStringLiteral("QWidget#artboardPresetSearchRow { background: transparent; }"));
  auto* search_action = new QWidgetAction(menu_);
  search_action->setObjectName(QStringLiteral("artboardPresetSearchAction"));
  search_action->setDefaultWidget(search_row);
  menu_->addAction(search_action);
  connect(search_, &QLineEdit::textChanged, this, [this](const QString& text) { apply_search(text); });

  custom_ = menu_->addAction(QObject::tr("Custom"));
  custom_->setObjectName(QStringLiteral("artboardPresetCustomAction"));
  custom_->setCheckable(true);
  custom_->setData(-1);
  connect(custom_, &QAction::triggered, this, [this] { choose(custom_); });
  menu_->addSeparator();

  QMenu* group_menu = nullptr;
  QString group;
  for (int index = 0; index < static_cast<int>(presets_.size()); ++index) {
    const auto& preset = presets_[static_cast<std::size_t>(index)];
    if (group_menu == nullptr || preset.group != group) {
      group = preset.group;
      group_menu = menu_->addMenu(menu_text(group));
      group_menu->setObjectName(QStringLiteral("artboardPresetGroupMenu"));
      group_actions_ << group_menu->menuAction();
    }
    auto* action = group_menu->addAction(menu_text(preset_label(preset)));
    action->setCheckable(true);
    action->setData(index);
    connect(action, &QAction::triggered, this, [this, action] { choose(action); });
  }
  // The search results: every preset once, flat, its group named at the right.
  no_match_ = menu_->addAction(tr("No matching presets"));
  no_match_->setObjectName(QStringLiteral("artboardPresetNoMatchAction"));
  no_match_->setEnabled(false);
  no_match_->setVisible(false);
  for (int index = 0; index < static_cast<int>(presets_.size()); ++index) {
    const auto& preset = presets_[static_cast<std::size_t>(index)];
    auto* action = menu_->addAction(menu_text(preset_label(preset) + QLatin1Char('\t') + preset.group));
    action->setObjectName(QStringLiteral("artboardPresetResultAction"));
    action->setCheckable(true);
    action->setData(index);
    action->setVisible(false);
    connect(action, &QAction::triggered, this, [this, action] { choose(action); });
    result_actions_ << action;
  }

  // One width for every choice, so picking a preset never shifts the bar.
  const QFontMetrics metrics(font());
  int widest = metrics.horizontalAdvance(QObject::tr("Custom"));
  for (const auto& preset : presets_) {
    widest = std::max(widest, metrics.horizontalAdvance(preset.name));
  }
  preferred_width_ = std::clamp(widest + kTextInset + kChevronRoom + 2, 120, ui_text_length(200));
  updateGeometry();
  show_current();
}

// The width comes from the size hints, not setFixedWidth: polishing applies the
// stylesheet's QToolButton minimum over a fixed width, and the button then followed its text.
QSize ArtboardPresetPicker::sizeHint() const {
  return {preferred_width_, QToolButton::sizeHint().height()};
}

QSize ArtboardPresetPicker::minimumSizeHint() const {
  return sizeHint();
}

// The button reads like the bar's other fields: the field surface, the chosen name at the
// left and a chevron pinned at the right, whatever the base style does with a tool
// button's label.
void ArtboardPresetPicker::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QStylePainter painter(this);
  QStyleOptionToolButton option;
  option.initFrom(this);
  option.subControls = QStyle::SC_ToolButton;
  option.toolButtonStyle = Qt::ToolButtonTextOnly;
  painter.drawComplexControl(QStyle::CC_ToolButton, option);
  const auto& colors = theme();
  const QRect text_rect = rect().adjusted(kTextInset, 0, -kChevronRoom, 0);
  painter.setPen(isEnabled() ? colors.text_primary : colors.field_text_disabled);
  painter.drawText(text_rect, Qt::AlignVCenter | Qt::AlignLeft,
                   fontMetrics().elidedText(text(), Qt::ElideRight, text_rect.width()));
  const QRect chevron(width() - kChevronSize - 6, (height() - kChevronSize) / 2, kChevronSize, kChevronSize);
  themed_svg_icon(QStringLiteral("chevron-down"))
      .paint(&painter, chevron, Qt::AlignCenter, isEnabled() ? QIcon::Normal : QIcon::Disabled);
}

void ArtboardPresetPicker::set_current_size(QSize size) {
  const auto current = std::find_if(presets_.begin(), presets_.end(),
                                    [this](const ArtboardSizePreset& preset) { return preset.id == current_id_; });
  if (current == presets_.end() || current->size != size) {
    const auto match = std::find_if(presets_.begin(), presets_.end(),
                                    [size](const ArtboardSizePreset& preset) { return preset.size == size; });
    current_id_ = match != presets_.end() ? match->id : QString();
  }
  show_current();
}

void ArtboardPresetPicker::show_current() {
  int current_index = -1;
  for (int index = 0; index < static_cast<int>(presets_.size()); ++index) {
    if (presets_[static_cast<std::size_t>(index)].id == current_id_) {
      current_index = index;
      break;
    }
  }
  if (current_index < 0) {
    current_id_.clear();
  }
  setText(current_index >= 0 ? presets_[static_cast<std::size_t>(current_index)].name : QObject::tr("Custom"));
  if (custom_ != nullptr) {
    custom_->setChecked(current_index < 0);
  }
  for (auto* group_action : group_actions_) {
    for (auto* action : group_action->menu()->actions()) {
      action->setChecked(action->data().toInt() == current_index);
    }
  }
  for (auto* action : result_actions_) {
    action->setChecked(action->data().toInt() == current_index);
  }
  update();
}

void ArtboardPresetPicker::choose(QAction* action) {
  const int index = action != nullptr ? action->data().toInt() : -1;
  if (index < 0 || index >= static_cast<int>(presets_.size())) {
    current_id_.clear();
    show_current();
    return;
  }
  const auto& preset = presets_[static_cast<std::size_t>(index)];
  current_id_ = preset.id;
  show_current();
  emit preset_chosen(preset.size);
}

void ArtboardPresetPicker::apply_search(const QString& text) {
  const bool searching = !text.trimmed().isEmpty();
  for (auto* action : group_actions_) {
    action->setVisible(!searching);
  }
  QAction* first = nullptr;
  for (auto* action : result_actions_) {
    const auto& preset = presets_[static_cast<std::size_t>(action->data().toInt())];
    const bool match = searching && size_preset_matches_search(preset.name, preset.group, preset.size, text);
    action->setVisible(match);
    if (match && first == nullptr) {
      first = action;
    }
  }
  no_match_->setVisible(searching && first == nullptr);
  // The best match is highlighted, so Enter takes it straight from the field.
  menu_->setActiveAction(first);
}

QList<QAction*> ArtboardPresetPicker::navigable_actions() const {
  QList<QAction*> actions;
  for (auto* action : menu_->actions()) {
    if (action->isVisible() && action->isEnabled() && !action->isSeparator() &&
        qobject_cast<QWidgetAction*>(action) == nullptr) {
      actions << action;
    }
  }
  return actions;
}

bool ArtboardPresetPicker::eventFilter(QObject* watched, QEvent* event) {
  if (watched != search_ || event->type() != QEvent::KeyPress) {
    return QToolButton::eventFilter(watched, event);
  }
  auto* key_event = static_cast<QKeyEvent*>(event);
  const auto actions = navigable_actions();
  auto* active = menu_->activeAction();
  switch (key_event->key()) {
    case Qt::Key_Down:
    case Qt::Key_Up: {
      if (actions.isEmpty()) {
        return true;
      }
      const auto at = actions.indexOf(active);
      const auto step = key_event->key() == Qt::Key_Down ? 1 : -1;
      const auto next = at < 0 ? (step > 0 ? 0 : actions.size() - 1) : (at + step + actions.size()) % actions.size();
      menu_->setActiveAction(actions[next]);
      return true;
    }
    case Qt::Key_Right:
      // Opens the highlighted group; otherwise the key moves the field's cursor.
      if (active != nullptr && active->menu() != nullptr && actions.contains(active)) {
        QCoreApplication::sendEvent(menu_, event);
        return true;
      }
      return false;
    case Qt::Key_Return:
    case Qt::Key_Enter:
      if (active == nullptr || !actions.contains(active)) {
        return true;
      }
      if (active->menu() != nullptr) {
        QCoreApplication::sendEvent(menu_, event);
        return true;
      }
      menu_->hide();
      active->trigger();
      return true;
    case Qt::Key_Escape:
      menu_->hide();
      return true;
    default:
      return false;
  }
}

}  // namespace patchy::ui
