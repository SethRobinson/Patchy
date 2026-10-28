#include "ui/preset_grid_widget.hpp"

#include "ui/theme_palette.hpp"

#include <QButtonGroup>
#include <QDrag>
#include <QDropEvent>
#include <QIcon>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidgetItemIterator>

#include <algorithm>
#include <iterator>
#include <map>
#include <utility>

namespace patchy::ui {

namespace {

// Clicks this far into a header row open or close it (the arrow).
constexpr int kHeaderArrowZone = 20;

// Paints the cells (thumbnail only, selection and hover through theme roles) and the folder
// headers (arrow and bold label); the app sheet's list-row rules are for text rows.
class PresetGridDelegate final : public QStyledItemDelegate {
public:
  explicit PresetGridDelegate(PresetGridWidget* grid) : QStyledItemDelegate(grid), grid_(grid) {}

  void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);
    const auto& colors = theme();
    const bool selected = (opt.state & QStyle::State_Selected) != 0;
    const bool hovered = (opt.state & QStyle::State_MouseOver) != 0;
    const bool drop_target = grid_->drop_highlight_row() == index.row();
    const QRect cell = opt.rect.adjusted(0, 0, -1, -1);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, false);
    if (selected) {
      painter->fillRect(cell, colors.list_selection_bg);
      painter->setPen(colors.list_selection_border);
      painter->drawRect(cell);
    } else if (hovered) {
      painter->fillRect(cell, colors.list_row_hover_bg);
    }
    if (drop_target) {
      painter->setPen(QPen(colors.accent, 2));
      painter->drawRect(cell.adjusted(1, 1, 0, 0));
    }
    if (index.data(PresetGridWidget::kFolderMarkerRole).toBool()) {
      const auto text_color = selected ? colors.list_selection_text : colors.text_primary;
      const bool expanded = index.data(PresetGridWidget::kExpandedRole).toBool();
      const QPointF center(opt.rect.left() + 9.5, opt.rect.center().y() + 0.5);
      QPainterPath arrow;
      if (expanded) {
        arrow.moveTo(center + QPointF(-4, -2));
        arrow.lineTo(center + QPointF(4, -2));
        arrow.lineTo(center + QPointF(0, 3));
      } else {
        arrow.moveTo(center + QPointF(-2, -4));
        arrow.lineTo(center + QPointF(3, 0));
        arrow.lineTo(center + QPointF(-2, 4));
      }
      arrow.closeSubpath();
      painter->setRenderHint(QPainter::Antialiasing, true);
      painter->fillPath(arrow, text_color);
      painter->setFont(opt.font);
      painter->setPen(text_color);
      const auto text_rect = opt.rect.adjusted(kHeaderArrowZone, 0, -4, 0);
      painter->drawText(text_rect, Qt::AlignLeft | Qt::AlignVCenter,
                        opt.fontMetrics.elidedText(opt.text, Qt::ElideRight, text_rect.width()));
    } else {
      QRect icon_rect(QPoint(0, 0), opt.decorationSize);
      icon_rect.moveCenter(opt.rect.center());
      opt.icon.paint(painter, icon_rect, Qt::AlignCenter, QIcon::Normal, QIcon::Off);
    }
    painter->restore();
  }

private:
  PresetGridWidget* grid_;
};

QString item_folder(const QListWidgetItem* item) {
  if (item == nullptr) {
    return {};
  }
  return PresetGridWidget::is_folder_item(item) ? item->data(Qt::UserRole).toString()
                                                : item->data(PresetGridWidget::kFolderRole).toString();
}

}  // namespace

PresetGridWidget::PresetGridWidget(QWidget* parent) : QListWidget(parent) {
  setViewMode(QListView::IconMode);
  setFlow(QListView::LeftToRight);
  setWrapping(true);
  setResizeMode(QListView::Adjust);
  setMovement(QListView::Static);
  setUniformItemSizes(false);
  setSpacing(2);
  setSelectionMode(QAbstractItemView::ExtendedSelection);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setDragDropMode(QAbstractItemView::NoDragDrop);
  setDragEnabled(false);
  setAcceptDrops(false);
  setMouseTracking(true);
  viewport()->setAttribute(Qt::WA_Hover);
  setItemDelegate(new PresetGridDelegate(this));
  connect(this, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
    if (const auto id = item_entry_id(item); !id.isEmpty() && entry_clicked_) {
      entry_clicked_(id);
    }
  });
  connect(this, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
    if (const auto id = item_entry_id(item); !id.isEmpty() && entry_double_clicked_) {
      entry_double_clicked_(id);
    }
  });
}

void PresetGridWidget::set_entries_callback(std::function<std::vector<PresetTreeEntry>()> callback) {
  entries_ = std::move(callback);
}

void PresetGridWidget::set_folder_label_callback(std::function<QString(const QString&, int)> callback) {
  folder_label_ = std::move(callback);
}

void PresetGridWidget::set_cell_extent(int extent) {
  cell_extent_ = std::max(8, extent);
}

void PresetGridWidget::set_reload_fallback(PresetTreeWidget::ReloadFallback fallback) {
  reload_fallback_ = fallback;
}

void PresetGridWidget::set_entry_clicked_callback(std::function<void(const QString&)> callback) {
  entry_clicked_ = std::move(callback);
}

void PresetGridWidget::set_entry_double_clicked_callback(std::function<void(const QString&)> callback) {
  entry_double_clicked_ = std::move(callback);
}

void PresetGridWidget::set_folder_click_toggles(bool toggles) {
  folder_click_toggles_ = toggles;
}

void PresetGridWidget::set_folder_toggled_callback(std::function<void()> callback) {
  folder_toggled_ = std::move(callback);
}

bool PresetGridWidget::is_folder_item(const QListWidgetItem* item) {
  return item != nullptr && item->data(kFolderMarkerRole).toBool();
}

bool PresetGridWidget::is_folder_expanded(const QListWidgetItem* item) {
  return is_folder_item(item) && item->data(kExpandedRole).toBool();
}

QString PresetGridWidget::item_entry_id(const QListWidgetItem* item) {
  return item != nullptr && !is_folder_item(item) ? item->data(Qt::UserRole).toString() : QString();
}

QListWidgetItem* PresetGridWidget::folder_item(const QString& folder) const {
  for (int row = 0; row < count(); ++row) {
    auto* candidate = item(row);
    if (is_folder_item(candidate) && candidate->data(Qt::UserRole).toString() == folder) {
      return candidate;
    }
  }
  return nullptr;
}

QListWidgetItem* PresetGridWidget::entry_item(const QString& id) const {
  for (int row = 0; row < count(); ++row) {
    auto* candidate = item(row);
    if (!id.isEmpty() && item_entry_id(candidate) == id) {
      return candidate;
    }
  }
  return nullptr;
}

void PresetGridWidget::remember_collapsed_folders() {
  collapsed_folders_.clear();
  for (const auto& folder : collapsed_folders()) {
    collapsed_folders_.insert(folder);
  }
}

QStringList PresetGridWidget::collapsed_folders() const {
  QStringList folders;
  for (int row = 0; row < count(); ++row) {
    const auto* candidate = item(row);
    if (is_folder_item(candidate) && !is_folder_expanded(candidate)) {
      folders.append(candidate->data(Qt::UserRole).toString());
    }
  }
  return folders;
}

QStringList PresetGridWidget::remembered_collapsed_folders() const {
  return QStringList(collapsed_folders_.begin(), collapsed_folders_.end());
}

void PresetGridWidget::set_collapsed_folders(const QStringList& folders) {
  collapsed_folders_ = QSet<QString>(folders.begin(), folders.end());
}

void PresetGridWidget::apply_visibility() {
  std::map<QString, int> matches_per_folder;
  for (int row = 0; row < count(); ++row) {
    auto* entry = item(row);
    if (is_folder_item(entry)) {
      continue;
    }
    const auto folder = entry->data(kFolderRole).toString();
    const bool match =
        filter_text_.isEmpty() || entry->data(kNameRole).toString().contains(filter_text_, Qt::CaseInsensitive);
    const auto* header = folder.isEmpty() ? nullptr : folder_item(folder);
    const bool open = header == nullptr || is_folder_expanded(header);
    entry->setHidden(!match || !open);
    if (entry->isHidden() && entry->isSelected()) {
      entry->setSelected(false);
    }
    matches_per_folder[folder] += match ? 1 : 0;
  }
  for (int row = 0; row < count(); ++row) {
    auto* header = item(row);
    if (is_folder_item(header)) {
      header->setHidden(!filter_text_.isEmpty() && matches_per_folder[header->data(Qt::UserRole).toString()] == 0);
    }
  }
  if (auto* current = currentItem(); current != nullptr && current->isHidden()) {
    if (auto* header = folder_item(current->data(kFolderRole).toString()); header != nullptr && !header->isHidden()) {
      selectionModel()->setCurrentIndex(indexFromItem(header), QItemSelectionModel::NoUpdate);
    }
  }
}

void PresetGridWidget::update_header_widths() {
  auto width = viewport()->width() - 2 * spacing() - 1;
  if (verticalScrollBarPolicy() == Qt::ScrollBarAsNeeded && !verticalScrollBar()->isVisible()) {
    // The scroll bar a long grid brings must not push the headers past the row.
    width -= style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, this);
  }
  const QSize header_size(std::max(cell_extent_, width), std::max(24, fontMetrics().height() + 10));
  bool changed = false;
  for (int row = 0; row < count(); ++row) {
    auto* header = item(row);
    if (is_folder_item(header) && header->sizeHint() != header_size) {
      header->setSizeHint(header_size);
      changed = true;
    }
  }
  if (changed) {
    scheduleDelayedItemsLayout();
  }
}

void PresetGridWidget::set_folder_expanded(QListWidgetItem* header, bool expanded) {
  if (!is_folder_item(header) || is_folder_expanded(header) == expanded) {
    return;
  }
  header->setData(kExpandedRole, expanded);
  apply_visibility();
  if (folder_toggled_) {
    folder_toggled_();
  }
}

void PresetGridWidget::set_filter_text(const QString& text) {
  const auto needle = text.trimmed();
  const QSignalBlocker blocker(this);
  if (filter_text_.isEmpty() && !needle.isEmpty()) {
    remember_collapsed_folders();
  }
  filter_text_ = needle;
  for (int row = 0; row < count(); ++row) {
    auto* header = item(row);
    if (is_folder_item(header)) {
      header->setData(kExpandedRole,
                      !needle.isEmpty() || !collapsed_folders_.contains(header->data(Qt::UserRole).toString()));
    }
  }
  apply_visibility();
}

QString PresetGridWidget::first_visible_entry_id() const {
  for (int row = 0; row < count(); ++row) {
    const auto* candidate = item(row);
    if (!candidate->isHidden() && !is_folder_item(candidate)) {
      return item_entry_id(candidate);
    }
  }
  return {};
}

void PresetGridWidget::reload(const QString& select_id) {
  const QSignalBlocker blocker(this);
  clear();
  if (!entries_) {
    return;
  }
  // Entries without a folder first, then each folder's header and cells, so a folder's cells
  // always wrap under its own header whatever order the callback lists them in.
  std::vector<PresetTreeEntry> ungrouped;
  std::vector<std::pair<QString, std::vector<PresetTreeEntry>>> folders;
  for (auto& entry : entries_()) {
    if (entry.folder.isEmpty()) {
      ungrouped.push_back(std::move(entry));
      continue;
    }
    auto found = std::find_if(folders.begin(), folders.end(),
                              [&entry](const auto& folder) { return folder.first == entry.folder; });
    if (found == folders.end()) {
      folders.emplace_back(entry.folder, std::vector<PresetTreeEntry>{});
      found = std::prev(folders.end());
    }
    found->second.push_back(std::move(entry));
  }
  QListWidgetItem* select_item = nullptr;
  const QSize cell_size(cell_extent_, cell_extent_);
  const auto add_entry = [&](const PresetTreeEntry& entry) {
    auto* cell = new QListWidgetItem(this);
    cell->setIcon(entry.icon);
    cell->setData(Qt::UserRole, entry.id);
    cell->setData(kNameRole, entry.text);
    cell->setData(kFolderRole, entry.folder);
    cell->setData(Qt::AccessibleTextRole, entry.text);
    cell->setToolTip(entry.tool_tip.isEmpty() ? entry.text : entry.tool_tip);
    cell->setSizeHint(cell_size);
    if (entry.id == select_id) {
      select_item = cell;
    }
  };
  for (const auto& entry : ungrouped) {
    add_entry(entry);
  }
  for (const auto& [folder, entries] : folders) {
    auto* header = new QListWidgetItem(this);
    const auto label = folder_label_ ? folder_label_(folder, static_cast<int>(entries.size())) : folder;
    header->setText(label);
    header->setData(kFolderMarkerRole, true);
    header->setData(Qt::UserRole, folder);
    header->setData(kNameRole, label);
    header->setData(kExpandedRole, !filter_text_.isEmpty() || !collapsed_folders_.contains(folder));
    auto font = header->font();
    font.setBold(true);
    header->setFont(font);
    for (const auto& entry : entries) {
      add_entry(entry);
    }
  }
  update_header_widths();
  apply_visibility();
  if (select_item != nullptr) {
    if (auto* header = folder_item(select_item->data(kFolderRole).toString()); header != nullptr) {
      header->setData(kExpandedRole, true);
      apply_visibility();
    }
    setCurrentItem(select_item);
    scrollToItem(select_item);
    return;
  }
  if (reload_fallback_ == PresetTreeWidget::ReloadFallback::none || count() == 0) {
    return;
  }
  auto* first = item(0);
  const bool folder_with_children = is_folder_item(first) && count() > 1 && !is_folder_item(item(1));
  if (reload_fallback_ == PresetTreeWidget::ReloadFallback::first_entry_expanding && folder_with_children) {
    first->setData(kExpandedRole, true);
    apply_visibility();
    setCurrentItem(item(1));
  } else if (reload_fallback_ == PresetTreeWidget::ReloadFallback::first_entry_when_expanded &&
             folder_with_children && is_folder_expanded(first)) {
    setCurrentItem(item(1));
  } else {
    setCurrentItem(first);
  }
}

QString PresetGridWidget::current_id() const {
  return item_entry_id(currentItem());
}

QString PresetGridWidget::current_folder() const {
  const auto* current = currentItem();
  return is_folder_item(current) ? current->data(Qt::UserRole).toString() : QString();
}

QStringList PresetGridWidget::selected_ids() const {
  QStringList ids;
  QSet<QString> selected_folders;
  for (int row = 0; row < count(); ++row) {
    const auto* candidate = item(row);
    if (is_folder_item(candidate)) {
      if (candidate->isSelected()) {
        selected_folders.insert(candidate->data(Qt::UserRole).toString());
      }
      continue;
    }
    const auto id = item_entry_id(candidate);
    if (!id.isEmpty() && !ids.contains(id) &&
        (candidate->isSelected() || selected_folders.contains(candidate->data(kFolderRole).toString()))) {
      ids.append(id);
    }
  }
  return ids;
}

void PresetGridWidget::resizeEvent(QResizeEvent* event) {
  QListWidget::resizeEvent(event);
  update_header_widths();
}

void PresetGridWidget::mousePressEvent(QMouseEvent* event) {
  auto* target = itemAt(event->position().toPoint());
  if (event->button() == Qt::LeftButton && is_folder_item(target)) {
    const auto rect = visualItemRect(target);
    if (folder_click_toggles_ || event->position().x() < rect.left() + kHeaderArrowZone) {
      set_folder_expanded(target, !is_folder_expanded(target));
      event->accept();
      return;
    }
  }
  QListWidget::mousePressEvent(event);
}

void PresetGridWidget::mouseDoubleClickEvent(QMouseEvent* event) {
  auto* target = itemAt(event->position().toPoint());
  if (event->button() == Qt::LeftButton && is_folder_item(target)) {
    // The first click of a toggling header already opened or closed it.
    if (!folder_click_toggles_ && event->position().x() >= visualItemRect(target).left() + kHeaderArrowZone) {
      set_folder_expanded(target, !is_folder_expanded(target));
    }
    event->accept();
    return;
  }
  QListWidget::mouseDoubleClickEvent(event);
}

void PresetGridWidget::keyPressEvent(QKeyEvent* event) {
  auto* current = currentItem();
  if (is_folder_item(current)) {
    switch (event->key()) {
      case Qt::Key_Return:
      case Qt::Key_Enter:
        set_folder_expanded(current, !is_folder_expanded(current));
        event->accept();
        return;
      case Qt::Key_Left:
        set_folder_expanded(current, false);
        event->accept();
        return;
      case Qt::Key_Right:
        set_folder_expanded(current, true);
        event->accept();
        return;
      default:
        break;
    }
  }
  QListWidget::keyPressEvent(event);
}

void PresetGridWidget::set_drop_callback(std::function<void(const QStringList&, const QString&)> moved) {
  drop_moved_ = std::move(moved);
  const bool enabled = static_cast<bool>(drop_moved_);
  setDragEnabled(enabled);
  setAcceptDrops(enabled);
  viewport()->setAcceptDrops(enabled);
  setDragDropMode(enabled ? QAbstractItemView::InternalMove : QAbstractItemView::NoDragDrop);
}

void PresetGridWidget::startDrag(Qt::DropActions /*supported_actions*/) {
  const auto indexes = selectedIndexes();
  if (indexes.isEmpty() || !drop_moved_) {
    return;
  }
  auto* mime = model()->mimeData(indexes);
  if (mime == nullptr) {
    return;
  }
  // QListView's icon-mode drag would paint the cells following the cursor and move them; the
  // library owns the order, so this is a plain drag with the first thumbnail as its image.
  auto* drag = new QDrag(this);
  drag->setMimeData(mime);
  const auto* first = itemFromIndex(indexes.front());
  if (first != nullptr && !first->icon().isNull()) {
    drag->setPixmap(first->icon().pixmap(iconSize()));
  }
  drag->exec(Qt::MoveAction, Qt::MoveAction);
  set_drop_highlight(-1);
}

QListWidgetItem* PresetGridWidget::drop_folder_row(const QListWidgetItem* target) const {
  const auto folder = item_folder(target);
  return folder.isEmpty() ? nullptr : folder_item(folder);
}

void PresetGridWidget::set_drop_highlight(int row) {
  if (drop_highlight_row_ != row) {
    drop_highlight_row_ = row;
    viewport()->update();
  }
}

void PresetGridWidget::dragEnterEvent(QDragEnterEvent* event) {
  if (drop_moved_ && event->source() == this) {
    event->acceptProposedAction();
    return;
  }
  event->ignore();
}

void PresetGridWidget::dragMoveEvent(QDragMoveEvent* event) {
  if (drop_moved_ && event->source() == this) {
    const auto* header = drop_folder_row(itemAt(event->position().toPoint()));
    set_drop_highlight(header != nullptr ? row(header) : -1);
    event->acceptProposedAction();
    return;
  }
  event->ignore();
}

void PresetGridWidget::dragLeaveEvent(QDragLeaveEvent* event) {
  set_drop_highlight(-1);
  QListWidget::dragLeaveEvent(event);
}

void PresetGridWidget::dropEvent(QDropEvent* event) {
  set_drop_highlight(-1);
  if (!drop_moved_ || event->source() != this) {
    event->ignore();
    return;
  }
  event->setDropAction(Qt::IgnoreAction);
  event->accept();
  drop_selected_on(itemAt(event->position().toPoint()));
}

void PresetGridWidget::drop_selected_on(const QListWidgetItem* target) {
  const auto folder = item_folder(target);
  const auto ids = selected_ids();
  if (ids.isEmpty() || !drop_moved_) {
    return;
  }
  QTimer::singleShot(0, this, [this, ids, folder] {
    if (drop_moved_) {
      drop_moved_(ids, folder);
    }
  });
}

PresetBrowserView::PresetBrowserView(QWidget* parent)
    : QStackedWidget(parent), tree_(new PresetTreeWidget(this)), grid_(new PresetGridWidget(this)) {
  addWidget(tree_);
  addWidget(grid_);
  setCurrentWidget(tree_);
  connect(tree_, &QTreeWidget::itemSelectionChanged, this, [this] {
    if (currentWidget() == tree_ && selection_changed_) {
      selection_changed_();
    }
  });
  connect(grid_, &QListWidget::itemSelectionChanged, this, [this] {
    if (currentWidget() == grid_ && selection_changed_) {
      selection_changed_();
    }
  });
}

QAbstractItemView* PresetBrowserView::active_view() const {
  return grid_view() ? static_cast<QAbstractItemView*>(grid_) : static_cast<QAbstractItemView*>(tree_);
}

bool PresetBrowserView::grid_view() const {
  return currentWidget() == grid_;
}

void PresetBrowserView::set_grid_view(bool grid) {
  if (grid == grid_view()) {
    return;
  }
  // Before the first reload (or while filtering) the shown view's rows do not say which folders
  // are closed; the set it was given (or recorded when the filter started) does.
  const bool unfilled = grid_view() ? grid_->count() == 0 : tree_->topLevelItemCount() == 0;
  const auto collapsed = filter_text_.isEmpty() && !unfilled
                             ? collapsed_folders()
                             : (grid_view() ? grid_->remembered_collapsed_folders()
                                            : tree_->remembered_collapsed_folders());
  const auto current = current_id();
  const auto folder = current_folder();
  const bool had_focus = active_view()->hasFocus();
  if (grid) {
    grid_->set_collapsed_folders(collapsed);
    grid_->reload(current);
    if (current.isEmpty() && !folder.isEmpty()) {
      grid_->setCurrentItem(grid_->folder_item(folder));
    }
    grid_->set_filter_text(filter_text_);
  } else {
    tree_->set_collapsed_folders(collapsed);
    tree_->reload(current);
    if (current.isEmpty() && !folder.isEmpty()) {
      for (int index = 0; index < tree_->topLevelItemCount(); ++index) {
        auto* row = tree_->topLevelItem(index);
        if (row->data(0, PresetTreeWidget::kFolderMarkerRole).toBool() &&
            row->data(0, Qt::UserRole).toString() == folder) {
          tree_->setCurrentItem(row);
        }
      }
    }
    if (!filter_text_.isEmpty()) {
      tree_->set_filter_text(filter_text_);
    }
  }
  setCurrentWidget(grid ? static_cast<QWidget*>(grid_) : static_cast<QWidget*>(tree_));
  if (had_focus) {
    active_view()->setFocus(Qt::OtherFocusReason);
  }
  if (selection_changed_) {
    selection_changed_();
  }
}

void PresetBrowserView::connect_view_buttons(QToolButton* list_button, QToolButton* grid_button,
                                             std::function<void(bool)> changed) {
  auto* group = new QButtonGroup(this);
  group->setExclusive(true);
  for (auto* button : {list_button, grid_button}) {
    button->setCheckable(true);
    button->setAutoRaise(true);
    group->addButton(button);
  }
  list_button->setChecked(!grid_view());
  grid_button->setChecked(grid_view());
  const auto pick = [this, changed = std::move(changed)](bool grid) {
    if (grid == grid_view()) {
      return;
    }
    set_grid_view(grid);
    if (changed) {
      changed(grid);
    }
  };
  connect(list_button, &QToolButton::clicked, this, [pick] { pick(false); });
  connect(grid_button, &QToolButton::clicked, this, [pick] { pick(true); });
}

void PresetBrowserView::set_entries_callback(const std::function<std::vector<PresetTreeEntry>()>& callback) {
  tree_->set_entries_callback(callback);
  grid_->set_entries_callback(callback);
}

void PresetBrowserView::set_folder_label_callback(const std::function<QString(const QString&, int)>& callback) {
  tree_->set_folder_label_callback(callback);
  grid_->set_folder_label_callback(callback);
}

void PresetBrowserView::set_reload_fallback(PresetTreeWidget::ReloadFallback fallback) {
  tree_->set_reload_fallback(fallback);
  grid_->set_reload_fallback(fallback);
}

void PresetBrowserView::set_entry_clicked_callback(const std::function<void(const QString&)>& callback) {
  tree_->set_entry_clicked_callback(callback);
  grid_->set_entry_clicked_callback(callback);
}

void PresetBrowserView::set_entry_double_clicked_callback(const std::function<void(const QString&)>& callback) {
  tree_->set_entry_double_clicked_callback(callback);
  grid_->set_entry_double_clicked_callback(callback);
}

void PresetBrowserView::set_drop_callback(const std::function<void(const QStringList&, const QString&)>& moved) {
  tree_->set_drop_callback(moved);
  grid_->set_drop_callback(moved);
}

void PresetBrowserView::set_selection_changed_callback(const std::function<void()>& callback) {
  selection_changed_ = callback;
}

void PresetBrowserView::set_folder_toggled_callback(const std::function<void()>& callback) {
  if (!callback) {
    return;
  }
  connect(tree_, &QTreeWidget::itemExpanded, this, [callback](QTreeWidgetItem*) { callback(); });
  connect(tree_, &QTreeWidget::itemCollapsed, this, [callback](QTreeWidgetItem*) { callback(); });
  grid_->set_folder_toggled_callback(callback);
}

void PresetBrowserView::remember_collapsed_folders() {
  if (grid_view()) {
    grid_->remember_collapsed_folders();
  } else {
    tree_->remember_collapsed_folders();
  }
}

QStringList PresetBrowserView::collapsed_folders() const {
  return grid_view() ? grid_->collapsed_folders() : tree_->collapsed_folders();
}

void PresetBrowserView::set_collapsed_folders(const QStringList& folders) {
  tree_->set_collapsed_folders(folders);
  grid_->set_collapsed_folders(folders);
}

void PresetBrowserView::reload(const QString& select_id) {
  if (grid_view()) {
    grid_->reload(select_id);
  } else {
    tree_->reload(select_id);
  }
}

void PresetBrowserView::set_filter_text(const QString& text) {
  filter_text_ = text.trimmed();
  if (grid_view()) {
    grid_->set_filter_text(text);
  } else {
    tree_->set_filter_text(text);
  }
}

QString PresetBrowserView::first_visible_entry_id() const {
  return grid_view() ? grid_->first_visible_entry_id() : tree_->first_visible_entry_id();
}

QString PresetBrowserView::current_or_first_visible_entry_id() const {
  if (grid_view()) {
    const auto* current = grid_->currentItem();
    const auto id = PresetGridWidget::item_entry_id(current);
    return !id.isEmpty() && !current->isHidden() ? id : grid_->first_visible_entry_id();
  }
  const auto* current = tree_->currentItem();
  const auto id = PresetTreeWidget::item_entry_id(current);
  const bool visible =
      !id.isEmpty() && !current->isHidden() && (current->parent() == nullptr || !current->parent()->isHidden());
  return visible ? id : tree_->first_visible_entry_id();
}

void PresetBrowserView::focus_first_visible() {
  active_view()->setFocus(Qt::TabFocusReason);
  if (grid_view()) {
    if (grid_->currentItem() == nullptr || grid_->currentItem()->isHidden()) {
      for (int row = 0; row < grid_->count(); ++row) {
        if (!grid_->item(row)->isHidden()) {
          grid_->setCurrentItem(grid_->item(row));
          break;
        }
      }
    }
    return;
  }
  if (tree_->currentItem() == nullptr || tree_->currentItem()->isHidden()) {
    for (QTreeWidgetItemIterator it(tree_, QTreeWidgetItemIterator::NotHidden); *it != nullptr; ++it) {
      tree_->setCurrentItem(*it);
      break;
    }
  }
}

QString PresetBrowserView::current_id() const {
  return grid_view() ? grid_->current_id() : tree_->current_id();
}

QString PresetBrowserView::current_folder() const {
  return grid_view() ? grid_->current_folder() : tree_->current_folder();
}

QStringList PresetBrowserView::selected_ids() const {
  return grid_view() ? grid_->selected_ids() : tree_->selected_ids();
}

}  // namespace patchy::ui
