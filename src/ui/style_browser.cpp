#include "ui/style_browser.hpp"

#include "ui/dialog_utils.hpp"
#include "ui/style_library.hpp"

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
      [](const QString& folder, int count) { return tr("%1 (%2)").arg(folder).arg(count); });
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
  return export_ids_to(selected_storage_ids(), path);
}

bool StyleBrowserWidget::export_ids_to(const QStringList& ids, const QString& path) {
  if (library_ == nullptr || ids.isEmpty()) {
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
  export_ids(selected_storage_ids(), export_suggested_name());
}

void StyleBrowserWidget::export_ids(const QStringList& ids, const QString& suggested_name) {
  if (library_ == nullptr || ids.isEmpty()) {
    return;
  }
  const auto suggested = suggested_name + QStringLiteral(".asl");
  auto path = get_save_file_name(this, tr("Export Styles"), suggested,
                                 tr("Photoshop Styles (*.asl)"), nullptr,
                                 QStringLiteral("styleBrowserExportFileDialog"));
  if (path.isEmpty()) {
    return;
  }
  if (!path.endsWith(QStringLiteral(".asl"), Qt::CaseInsensitive)) {
    path += QStringLiteral(".asl");
  }
  const auto count = static_cast<int>(ids.size());
  if (export_ids_to(ids, path)) {
    offer_browser_download_for_saved_file(path);
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
  const bool folder_row = item->data(0, kFolderMarkerRole).toBool();
  exec_export_menu(viewport()->mapToGlobal(position), ids, folder_row,
                   folder_row ? item->data(0, Qt::UserRole).toString() : QString());
}

void StyleBrowserWidget::exec_export_menu(const QPoint& global_position, const QStringList& ids, bool folder_row,
                                          const QString& folder) {
  if (ids.isEmpty()) {
    return;
  }
  QString suggested = folder;
  if (suggested.isEmpty() && ids.size() == 1 && library_ != nullptr) {
    if (const auto* entry = library_->find_entry(ids.front()); entry != nullptr) {
      suggested = entry->folder.isEmpty() ? style_library_entry_display_name(*entry) : entry->folder;
    }
  }
  if (suggested.isEmpty()) {
    suggested = tr("Styles");
  }
  QMenu menu(this);
  auto* export_action = menu.addAction(folder_row ? tr("Export Folder to .asl…") : tr("Export to .asl…"));
  export_action->setObjectName(QStringLiteral("styleBrowserExportAction"));
  connect(export_action, &QAction::triggered, this, [this, ids, suggested] { export_ids(ids, suggested); });
  menu.exec(global_position);
}

}  // namespace patchy::ui
