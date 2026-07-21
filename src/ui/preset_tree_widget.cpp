#include "ui/preset_tree_widget.hpp"

#include <QSignalBlocker>

#include <map>
#include <utility>

namespace patchy::ui {

PresetTreeWidget::PresetTreeWidget(QWidget* parent) : QTreeWidget(parent) {
  setHeaderHidden(true);
  setSelectionMode(QAbstractItemView::ExtendedSelection);
  setIndentation(14);
  connect(this, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item, int) {
    const auto id = item_entry_id(item);
    if (!id.isEmpty()) {
      if (entry_clicked_) {
        entry_clicked_(id);
      }
      return;
    }
    if (item != nullptr && !item->data(0, kFolderMarkerRole).toBool() && special_row_clicked_) {
      special_row_clicked_(item);
    }
  });
  connect(this, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
    const auto id = item_entry_id(item);
    if (!id.isEmpty() && entry_double_clicked_) {
      entry_double_clicked_(id);
    }
  });
}

void PresetTreeWidget::set_entries_callback(std::function<std::vector<PresetTreeEntry>()> callback) {
  entries_ = std::move(callback);
}

void PresetTreeWidget::set_folder_label_callback(
    std::function<QString(const QString&, int)> callback) {
  folder_label_ = std::move(callback);
}

void PresetTreeWidget::set_prepend_rows_callback(std::function<void(QTreeWidget&)> callback) {
  prepend_rows_ = std::move(callback);
}

void PresetTreeWidget::set_entry_row_height(int height) {
  entry_row_height_ = height;
}

void PresetTreeWidget::set_reload_fallback(ReloadFallback fallback) {
  reload_fallback_ = fallback;
}

void PresetTreeWidget::set_remember_collapse_on_reload(bool remember) {
  remember_collapse_on_reload_ = remember;
}

void PresetTreeWidget::set_entry_clicked_callback(std::function<void(const QString&)> callback) {
  entry_clicked_ = std::move(callback);
}

void PresetTreeWidget::set_entry_double_clicked_callback(
    std::function<void(const QString&)> callback) {
  entry_double_clicked_ = std::move(callback);
}

void PresetTreeWidget::set_special_row_clicked_callback(
    std::function<void(QTreeWidgetItem*)> callback) {
  special_row_clicked_ = std::move(callback);
}

void PresetTreeWidget::remember_collapsed_folders() {
  collapsed_folders_.clear();
  for (int index = 0; index < topLevelItemCount(); ++index) {
    const auto* item = topLevelItem(index);
    if (item->data(0, kFolderMarkerRole).toBool() && !item->isExpanded()) {
      collapsed_folders_.insert(item->data(0, Qt::UserRole).toString());
    }
  }
}

void PresetTreeWidget::reload(const QString& select_id) {
  if (remember_collapse_on_reload_) {
    remember_collapsed_folders();
  }
  const QSignalBlocker blocker(this);
  clear();
  if (!entries_) {
    return;
  }
  QTreeWidgetItem* select_item = nullptr;
  if (prepend_rows_) {
    prepend_rows_(*this);
  }
  std::map<QString, QTreeWidgetItem*> folder_items;
  for (const auto& entry : entries_()) {
    QTreeWidgetItem* parent_item = nullptr;
    if (!entry.folder.isEmpty()) {
      auto found = folder_items.find(entry.folder);
      if (found == folder_items.end()) {
        auto* folder_item = new QTreeWidgetItem(this);
        folder_item->setData(0, kFolderMarkerRole, true);
        folder_item->setData(0, Qt::UserRole, entry.folder);
        auto font = folder_item->font(0);
        font.setBold(true);
        folder_item->setFont(0, font);
        found = folder_items.emplace(entry.folder, folder_item).first;
      }
      parent_item = found->second;
    }
    auto* item = parent_item != nullptr ? new QTreeWidgetItem(parent_item)
                                        : new QTreeWidgetItem(this);
    item->setText(0, entry.text);
    item->setIcon(0, entry.icon);
    item->setSizeHint(0, QSize(0, entry_row_height_));
    item->setData(0, Qt::UserRole, entry.id);
    item->setToolTip(0, entry.tool_tip);
    if (entry.id == select_id) {
      select_item = item;
    }
  }
  for (const auto& [folder, item] : folder_items) {
    item->setText(0, folder_label_ ? folder_label_(folder, item->childCount()) : folder);
    item->setExpanded(!collapsed_folders_.contains(folder));
  }
  if (select_item != nullptr) {
    if (select_item->parent() != nullptr) {
      select_item->parent()->setExpanded(true);
    }
    setCurrentItem(select_item);
    scrollToItem(select_item);
    return;
  }
  if (reload_fallback_ == ReloadFallback::none || topLevelItemCount() == 0) {
    return;
  }
  auto* first = topLevelItem(0);
  const auto folder_with_children =
      first->data(0, kFolderMarkerRole).toBool() && first->childCount() > 0;
  if (reload_fallback_ == ReloadFallback::first_entry_expanding && folder_with_children) {
    first->setExpanded(true);
    setCurrentItem(first->child(0));
  } else if (reload_fallback_ == ReloadFallback::first_entry_when_expanded &&
             folder_with_children && first->isExpanded()) {
    setCurrentItem(first->child(0));
  } else {
    setCurrentItem(first);
  }
}

QString PresetTreeWidget::current_id() const {
  return item_entry_id(currentItem());
}

QStringList PresetTreeWidget::selected_ids() const {
  QStringList ids;
  const auto add_unique = [&ids](const QString& id) {
    if (!id.isEmpty() && !ids.contains(id)) {
      ids.append(id);
    }
  };
  for (const auto* item : selectedItems()) {
    if (item->data(0, kFolderMarkerRole).toBool()) {
      for (int child = 0; child < item->childCount(); ++child) {
        add_unique(item_entry_id(item->child(child)));
      }
    } else {
      add_unique(item_entry_id(item));
    }
  }
  return ids;
}

QString PresetTreeWidget::item_entry_id(const QTreeWidgetItem* item) {
  return item != nullptr && !item->data(0, kFolderMarkerRole).toBool()
             ? item->data(0, Qt::UserRole).toString()
             : QString();
}

}  // namespace patchy::ui
