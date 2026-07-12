#include "ui/style_browser.hpp"

#include "ui/style_library.hpp"

#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QMessageBox>
#include <QSignalBlocker>

#include <map>

namespace patchy::ui {

namespace {

constexpr int kFolderMarkerRole = Qt::UserRole + 1;
constexpr int kNoStyleMarkerRole = Qt::UserRole + 2;

[[nodiscard]] QString tree_item_storage_id(const QTreeWidgetItem* item) {
  return item != nullptr && !item->data(0, kFolderMarkerRole).toBool() &&
                 !item->data(0, kNoStyleMarkerRole).toBool()
             ? item->data(0, Qt::UserRole).toString()
             : QString();
}

}  // namespace

StyleBrowserWidget::StyleBrowserWidget(StyleLibrary* library, QWidget* parent)
    : QTreeWidget(parent), library_(library) {
  setObjectName(QStringLiteral("styleBrowserTree"));
  setHeaderHidden(true);
  setSelectionMode(QAbstractItemView::ExtendedSelection);
  setIconSize(QSize(icon_extent_, icon_extent_));
  setIndentation(14);
  setContextMenuPolicy(Qt::CustomContextMenu);
  connect(this, &QWidget::customContextMenuRequested, this,
          &StyleBrowserWidget::show_context_menu);
  connect(this, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item, int) {
    if (item != nullptr && item->data(0, kNoStyleMarkerRole).toBool()) {
      emit no_style_clicked();
      return;
    }
    const auto storage_id = tree_item_storage_id(item);
    if (!storage_id.isEmpty()) {
      emit style_clicked(storage_id);
    }
  });
  connect(this, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
    const auto storage_id = tree_item_storage_id(item);
    if (!storage_id.isEmpty()) {
      emit style_double_clicked(storage_id);
    }
  });
  if (library_ != nullptr) {
    connect(library_, &StyleLibrary::changed, this, [this] { reload(current_storage_id()); });
  }
}

void StyleBrowserWidget::set_icon_extent(int extent) {
  icon_extent_ = std::max(16, extent);
  setIconSize(QSize(icon_extent_, icon_extent_));
}

void StyleBrowserWidget::set_show_no_style_entry(bool show) {
  show_no_style_entry_ = show;
}

void StyleBrowserWidget::reload(const QString& select_storage_id) {
  // Remember which folders the user collapsed so library edits keep the view.
  collapsed_folders_.clear();
  for (int index = 0; index < topLevelItemCount(); ++index) {
    const auto* item = topLevelItem(index);
    if (item->data(0, kFolderMarkerRole).toBool() && !item->isExpanded()) {
      collapsed_folders_.append(item->data(0, Qt::UserRole).toString());
    }
  }

  const QSignalBlocker blocker(this);
  clear();
  if (library_ == nullptr) {
    return;
  }
  QTreeWidgetItem* select_item = nullptr;
  if (show_no_style_entry_) {
    auto* none_item = new QTreeWidgetItem(this);
    none_item->setText(0, tr("No Style (remove all effects)"));
    none_item->setData(0, kNoStyleMarkerRole, true);
    none_item->setToolTip(0, tr("Remove every effect from the layer"));
    none_item->setSizeHint(0, QSize(0, std::max(26, icon_extent_ / 2 + 12)));
  }
  std::map<QString, QTreeWidgetItem*> folder_items;
  for (const auto& entry : library_->entries()) {
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
    item->setText(0, style_library_entry_display_name(entry));
    item->setIcon(0, QIcon(entry.thumbnail));
    item->setSizeHint(0, QSize(0, icon_extent_ + 6));
    item->setData(0, Qt::UserRole, entry.storage_id);
    item->setToolTip(0, style_library_entry_display_name(entry));
    if (entry.storage_id == select_storage_id) {
      select_item = item;
    }
  }
  for (const auto& [folder, item] : folder_items) {
    item->setText(0, tr("%1 (%2)").arg(folder).arg(item->childCount()));
    item->setExpanded(!collapsed_folders_.contains(folder));
  }
  if (select_item != nullptr) {
    if (select_item->parent() != nullptr) {
      select_item->parent()->setExpanded(true);
    }
    setCurrentItem(select_item);
    scrollToItem(select_item);
  }
}

QString StyleBrowserWidget::current_storage_id() const {
  return tree_item_storage_id(currentItem());
}

bool StyleBrowserWidget::current_is_no_style() const {
  const auto* item = currentItem();
  return item != nullptr && item->data(0, kNoStyleMarkerRole).toBool();
}

QStringList StyleBrowserWidget::selected_storage_ids() const {
  QStringList ids;
  const auto add_unique = [&ids](const QString& id) {
    if (!id.isEmpty() && !ids.contains(id)) {
      ids.append(id);
    }
  };
  for (const auto* item : selectedItems()) {
    if (item->data(0, kFolderMarkerRole).toBool()) {
      for (int child = 0; child < item->childCount(); ++child) {
        add_unique(tree_item_storage_id(item->child(child)));
      }
    } else {
      add_unique(tree_item_storage_id(item));
    }
  }
  return ids;
}

QString StyleBrowserWidget::export_suggested_name() const {
  for (const auto* item : selectedItems()) {
    if (item->data(0, kFolderMarkerRole).toBool()) {
      return item->data(0, Qt::UserRole).toString();
    }
  }
  const auto ids = selected_storage_ids();
  if (ids.size() == 1 && library_ != nullptr) {
    if (const auto* entry = library_->find_entry(ids.front()); entry != nullptr) {
      if (!entry->folder.isEmpty()) {
        return entry->folder;
      }
      return style_library_entry_display_name(*entry);
    }
  }
  return tr("Styles");
}

bool StyleBrowserWidget::export_selection_to(const QString& path) {
  if (library_ == nullptr) {
    return false;
  }
  const auto ids = selected_storage_ids();
  if (ids.isEmpty()) {
    return false;
  }
  QString error;
  if (!library_->export_asl(ids, path, error)) {
    QMessageBox::warning(this, tr("Export Styles"), error);
    return false;
  }
  return true;
}

void StyleBrowserWidget::export_selection() {
  if (library_ == nullptr || selected_storage_ids().isEmpty()) {
    return;
  }
  const auto suggested = export_suggested_name() + QStringLiteral(".asl");
  const auto path = QFileDialog::getSaveFileName(this, tr("Export Styles"), suggested,
                                                 tr("Photoshop Styles (*.asl)"));
  if (path.isEmpty()) {
    return;
  }
  const auto count = static_cast<int>(selected_storage_ids().size());
  if (export_selection_to(path)) {
    QMessageBox::information(this, tr("Export Styles"),
                             tr("Exported %n style(s) to \"%1\".", nullptr, count)
                                 .arg(QFileInfo(path).fileName()));
  }
}

void StyleBrowserWidget::show_context_menu(const QPoint& position) {
  auto* item = itemAt(position);
  if (item == nullptr || item->data(0, kNoStyleMarkerRole).toBool()) {
    return;
  }
  if (!item->isSelected()) {
    setCurrentItem(item);
  }
  const auto ids = selected_storage_ids();
  if (ids.isEmpty()) {
    return;
  }
  QMenu menu(this);
  auto* export_action = menu.addAction(
      item->data(0, kFolderMarkerRole).toBool() ? tr("Export Folder to .asl…")
                                                : tr("Export to .asl…"));
  export_action->setObjectName(QStringLiteral("styleBrowserExportAction"));
  connect(export_action, &QAction::triggered, this, &StyleBrowserWidget::export_selection);
  menu.exec(viewport()->mapToGlobal(position));
}

}  // namespace patchy::ui
