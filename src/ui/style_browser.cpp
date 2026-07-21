#include "ui/style_browser.hpp"

#include "ui/style_library.hpp"

#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QMessageBox>

#include <algorithm>

namespace patchy::ui {

namespace {

constexpr int kNoStyleMarkerRole = Qt::UserRole + 2;

}  // namespace

StyleBrowserWidget::StyleBrowserWidget(StyleLibrary* library, QWidget* parent)
    : PresetTreeWidget(parent), library_(library) {
  setObjectName(QStringLiteral("styleBrowserTree"));
  setIconSize(QSize(icon_extent_, icon_extent_));
  set_entry_row_height(icon_extent_ + 6);
  set_remember_collapse_on_reload(true);
  setContextMenuPolicy(Qt::CustomContextMenu);
  connect(this, &QWidget::customContextMenuRequested, this,
          &StyleBrowserWidget::show_context_menu);
  // Every user-facing string stays in this class so its translation context
  // remains patchy::ui::StyleBrowserWidget.
  set_folder_label_callback(
      [this](const QString& folder, int count) { return tr("%1 (%2)").arg(folder).arg(count); });
  set_prepend_rows_callback([this](QTreeWidget& tree) {
    if (!show_no_style_entry_) {
      return;
    }
    auto* none_item = new QTreeWidgetItem(&tree);
    none_item->setText(0, tr("No Style (remove all effects)"));
    none_item->setData(0, kNoStyleMarkerRole, true);
    none_item->setToolTip(0, tr("Remove every effect from the layer"));
    none_item->setSizeHint(0, QSize(0, std::max(26, icon_extent_ / 2 + 12)));
  });
  set_entry_clicked_callback([this](const QString& storage_id) { emit style_clicked(storage_id); });
  set_entry_double_clicked_callback(
      [this](const QString& storage_id) { emit style_double_clicked(storage_id); });
  set_special_row_clicked_callback([this](QTreeWidgetItem* item) {
    if (item->data(0, kNoStyleMarkerRole).toBool()) {
      emit no_style_clicked();
    }
  });
  if (library_ != nullptr) {
    set_entries_callback([this] {
      std::vector<PresetTreeEntry> rows;
      for (const auto& entry : library_->entries()) {
        const auto name = style_library_entry_display_name(entry);
        rows.push_back({entry.storage_id, name, QIcon(entry.thumbnail), name, entry.folder});
      }
      return rows;
    });
    connect(library_, &StyleLibrary::changed, this, [this] { reload(current_storage_id()); });
  }
}

void StyleBrowserWidget::set_icon_extent(int extent) {
  icon_extent_ = std::max(16, extent);
  setIconSize(QSize(icon_extent_, icon_extent_));
  set_entry_row_height(icon_extent_ + 6);
}

void StyleBrowserWidget::set_show_no_style_entry(bool show) {
  show_no_style_entry_ = show;
}

QString StyleBrowserWidget::current_storage_id() const {
  return current_id();
}

bool StyleBrowserWidget::current_is_no_style() const {
  const auto* item = currentItem();
  return item != nullptr && item->data(0, kNoStyleMarkerRole).toBool();
}

QStringList StyleBrowserWidget::selected_storage_ids() const {
  return selected_ids();
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
