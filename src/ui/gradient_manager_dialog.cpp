#include "ui/gradient_manager_dialog.hpp"

#include "ui/dialog_utils.hpp"
#include "ui/gradient_library.hpp"
#include "ui/preset_manager_scaffold.hpp"

#include <QDialog>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <functional>
#include <map>

namespace patchy::ui {
namespace {
constexpr int kStorageRole = Qt::UserRole;
constexpr int kFolderRole = Qt::UserRole + 1;
} // namespace

QString request_gradient_manager(QWidget *parent, GradientLibrary &library,
                                 const QString &initial_storage_id,
                                 std::optional<GradientDefinition> current) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("gradientManagerDialog"));
  dialog.resize(820, 520);
  auto *root = install_dark_dialog_chrome(dialog, new QVBoxLayout(&dialog),
                                          QObject::tr("Gradient Manager"));
  auto *body = new QHBoxLayout();
  root->addLayout(body, 1);
  auto *tree = new QTreeWidget(&dialog);
  tree->setObjectName(QStringLiteral("gradientManagerTree"));
  tree->setHeaderHidden(true);
  tree->setRootIsDecorated(true);
  tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
  tree->setIconSize(QSize(112, 28));
  body->addWidget(tree, 1);
  auto *details = new QWidget(&dialog);
  auto *form = new QFormLayout(details);
  auto *preview = new QLabel(details);
  preview->setObjectName(QStringLiteral("gradientManagerPreview"));
  preview->setMinimumSize(320, 90);
  preview->setAlignment(Qt::AlignCenter);
  form->addRow(preview);
  auto *name = new QLineEdit(details);
  name->setObjectName(QStringLiteral("gradientManagerNameEdit"));
  form->addRow(QObject::tr("Name"), name);
  auto *folder = new QLineEdit(details);
  folder->setObjectName(QStringLiteral("gradientManagerFolderEdit"));
  form->addRow(QObject::tr("Folder"), folder);
  body->addWidget(details);

  auto *new_current = new QPushButton(QObject::tr("New from Current"), &dialog);
  new_current->setObjectName(QStringLiteral("gradientManagerNewButton"));
  new_current->setEnabled(current.has_value());
  auto *import_button = new QPushButton(QObject::tr("Import .grd..."), &dialog);
  import_button->setObjectName(QStringLiteral("gradientManagerImportButton"));
  auto *export_button = new QPushButton(QObject::tr("Export..."), &dialog);
  export_button->setObjectName(QStringLiteral("gradientManagerExportButton"));
  auto *duplicate = new QPushButton(QObject::tr("Duplicate"), &dialog);
  duplicate->setObjectName(QStringLiteral("gradientManagerDuplicateButton"));
  auto *remove = new QPushButton(QObject::tr("Delete"), &dialog);
  remove->setObjectName(QStringLiteral("gradientManagerDeleteButton"));
  auto *restore =
      new QPushButton(QObject::tr("Restore Default Gradients"), &dialog);
  restore->setObjectName(QStringLiteral("gradientManagerRestoreButton"));
  auto *use = new QPushButton(QObject::tr("Use Gradient"), &dialog);
  use->setObjectName(QStringLiteral("gradientManagerUseButton"));
  root->addLayout(PresetManagerScaffold::button_row(
      {new_current, import_button, export_button, duplicate, remove, restore},
      {use}));

  QSet<QString> expanded;
  auto selected_id = [&]() {
    auto *item = tree->currentItem();
    return item ? item->data(0, kStorageRole).toString() : QString{};
  };
  auto selected_ids = [&]() {
    QStringList ids;
    std::function<void(QTreeWidgetItem *)> append = [&](QTreeWidgetItem *item) {
      const auto id = item->data(0, kStorageRole).toString();
      if (!id.isEmpty() && !ids.contains(id))
        ids.push_back(id);
      for (int child = 0; child < item->childCount(); ++child)
        append(item->child(child));
    };
    for (auto *item : tree->selectedItems())
      append(item);
    return ids;
  };
  std::function<void(const QString &)> reload = [&](const QString &select) {
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
      if (tree->topLevelItem(i)->isExpanded())
        expanded.insert(tree->topLevelItem(i)->data(0, kFolderRole).toString());
    tree->clear();
    std::map<QString, QTreeWidgetItem *> folders;
    QTreeWidgetItem *select_item = nullptr;
    for (const auto &entry : library.entries()) {
      QTreeWidgetItem *parent_item = nullptr;
      QString path;
      for (const auto &part : entry.folder.split('/', Qt::SkipEmptyParts)) {
        path = path.isEmpty() ? part : path + '/' + part;
        auto found = folders.find(path);
        if (found == folders.end()) {
          auto *created =
              parent_item
                  ? new QTreeWidgetItem(
                        parent_item,
                        QStringList{gradient_folder_display_name(part)})
                  : new QTreeWidgetItem(
                        tree, QStringList{gradient_folder_display_name(part)});
          created->setData(0, kFolderRole, path);
          created->setExpanded(expanded.contains(path));
          folders.emplace(path, created);
          parent_item = created;
        } else
          parent_item = found->second;
      }
      auto *item =
          parent_item
              ? new QTreeWidgetItem(
                    parent_item,
                    QStringList{gradient_library_entry_display_name(entry)})
              : new QTreeWidgetItem(
                    tree,
                    QStringList{gradient_library_entry_display_name(entry)});
      item->setData(0, kStorageRole, entry.storage_id);
      item->setIcon(0, QIcon(entry.thumbnail));
      if (entry.storage_id == select)
        select_item = item;
    }
    if (select_item) {
      if (select_item->parent())
        select_item->parent()->setExpanded(true);
      tree->setCurrentItem(select_item);
    } else if (tree->topLevelItemCount())
      tree->setCurrentItem(tree->topLevelItem(0));
  };
  auto refresh = [&]() {
    const auto *entry = library.find_entry(selected_id());
    name->setEnabled(entry);
    folder->setEnabled(entry);
    duplicate->setEnabled(entry);
    use->setEnabled(entry);
    export_button->setEnabled(!selected_ids().isEmpty());
    remove->setEnabled(!selected_ids().isEmpty());
    if (!entry) {
      name->clear();
      folder->clear();
      preview->clear();
      return;
    }
    name->setText(entry->name);
    folder->setText(entry->folder);
    preview->setPixmap(gradient_thumbnail(entry->definition, 320, 72));
  };
  QObject::connect(tree, &QTreeWidget::itemSelectionChanged, &dialog, refresh);
  QObject::connect(tree, &QTreeWidget::itemDoubleClicked, &dialog,
                   [&](QTreeWidgetItem *item, int) {
                     if (!item->data(0, kStorageRole).toString().isEmpty())
                       dialog.accept();
                   });
  QObject::connect(name, &QLineEdit::editingFinished, &dialog, [&] {
    const auto id = selected_id();
    if (!id.isEmpty()) {
      library.rename_gradient(id, name->text());
      reload(id);
      refresh();
    }
  });
  QObject::connect(folder, &QLineEdit::editingFinished, &dialog, [&] {
    const auto id = selected_id();
    if (!id.isEmpty()) {
      library.set_gradient_folder(id, folder->text());
      reload(id);
      refresh();
    }
  });
  QObject::connect(new_current, &QPushButton::clicked, &dialog, [&] {
    bool ok = false;
    const auto chosen = QInputDialog::getText(
        &dialog, QObject::tr("New Gradient"), QObject::tr("Name"),
        QLineEdit::Normal, QObject::tr("New Gradient"), &ok);
    if (ok && current) {
      const auto id = library.add_gradient(chosen, *current, folder->text());
      reload(id);
      refresh();
    }
  });
  QObject::connect(duplicate, &QPushButton::clicked, &dialog, [&] {
    const auto id = library.duplicate_gradient(selected_id());
    reload(id);
    refresh();
  });
  QObject::connect(remove, &QPushButton::clicked, &dialog, [&] {
    library.remove_gradients(selected_ids());
    reload({});
    refresh();
  });
  QObject::connect(restore, &QPushButton::clicked, &dialog, [&] {
    library.restore_default_gradients();
    library.reset_default_gradients_to_factory();
    reload({});
    refresh();
  });
  QObject::connect(import_button, &QPushButton::clicked, &dialog, [&] {
    const auto path = get_open_file_name(
        &dialog, QObject::tr("Import Photoshop Gradients"), {},
        QObject::tr("Photoshop Gradients (*.grd)"), nullptr,
        QStringLiteral("gradientManagerImportFileDialog"));
    if (path.isEmpty())
      return;
    QString error;
    QStringList warnings;
    const auto id = library.import_grd(path, error, warnings);
    if (id.isEmpty())
      (void)show_warning_message(&dialog, QObject::tr("Import Gradients"),
                                 error, QMessageBox::Ok);
    else {
      reload(id);
      refresh();
      if (!warnings.isEmpty()) {
        QMessageBox message(QMessageBox::Information,
                            QObject::tr("Import Gradients"),
                            QObject::tr("Imported gradients with warnings."),
                            QMessageBox::Ok, &dialog);
        message.setDetailedText(warnings.join('\n'));
        message.exec();
      }
    }
  });
  QObject::connect(export_button, &QPushButton::clicked, &dialog, [&] {
    auto path = get_save_file_name(
        &dialog, QObject::tr("Export Photoshop Gradients"), {},
        QObject::tr("Photoshop Gradients (*.grd)"), nullptr,
        QStringLiteral("gradientManagerExportFileDialog"));
    if (path.isEmpty())
      return;
    if (!path.endsWith(QStringLiteral(".grd"), Qt::CaseInsensitive))
      path += QStringLiteral(".grd");
    QString error;
    if (!library.export_grd(selected_ids(), path, error))
      (void)show_warning_message(&dialog, QObject::tr("Export Gradients"),
                                 error, QMessageBox::Ok);
  });
  QObject::connect(use, &QPushButton::clicked, &dialog, &QDialog::accept);
  reload(initial_storage_id);
  refresh();
  return run_non_modal_dialog(dialog) == QDialog::Accepted ? selected_id()
                                                           : QString{};
}

} // namespace patchy::ui
