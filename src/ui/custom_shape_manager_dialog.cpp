#include "ui/custom_shape_manager_dialog.hpp"

#include "ui/custom_shape_library.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/preset_manager_scaffold.hpp"
#include "ui/preset_tree_widget.hpp"

#include <QDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>
#include <vector>

namespace patchy::ui {

QString request_custom_shape_manager(QWidget* parent, CustomShapeLibrary& library,
                                     const QString& initial_shape_id) {
  QDialog dialog(parent);
  PresetManagerScaffold scaffold(dialog, QStringLiteral("customShapeManagerDialog"),
                                 QObject::tr("Custom Shapes"));

  auto* tree = new PresetTreeWidget(&dialog);
  tree->setObjectName(QStringLiteral("customShapeManagerTree"));
  tree->setIconSize(QSize(40, 40));
  tree->set_entry_row_height(46);
  tree->set_reload_fallback(PresetTreeWidget::ReloadFallback::first_entry_expanding);
  tree->set_folder_label_callback([](const QString& folder, int count) {
    return QObject::tr("%1 (%2)").arg(custom_shape_folder_display_name(folder)).arg(count);
  });
  tree->set_entries_callback([&library] {
    std::vector<PresetTreeEntry> rows;
    for (const auto& entry : library.entries()) {
      const auto name = custom_shape_display_name(entry);
      rows.push_back({entry.storage_id, name, QIcon(entry.thumbnail), name, entry.folder});
    }
    return rows;
  });
  scaffold.add_tree(tree, 280);

  auto* preview = new QLabel(&dialog);
  preview->setObjectName(QStringLiteral("customShapeManagerPreview"));
  preview->setMinimumSize(220, 200);
  preview->setAlignment(Qt::AlignCenter);
  scaffold.right()->addWidget(preview, 1);

  auto* form = new QFormLayout();
  auto* name_edit = new QLineEdit(&dialog);
  name_edit->setObjectName(QStringLiteral("customShapeManagerNameEdit"));
  form->addRow(QObject::tr("Name:"), name_edit);
  auto* folder_edit = new QLineEdit(&dialog);
  folder_edit->setObjectName(QStringLiteral("customShapeManagerFolderEdit"));
  folder_edit->setPlaceholderText(QObject::tr("No folder"));
  folder_edit->setToolTip(
      QObject::tr("Folder for the selected shape(s); leave empty to remove them from folders"));
  form->addRow(QObject::tr("Folder:"), folder_edit);
  scaffold.right()->addLayout(form);

  auto* import_button = new QPushButton(QObject::tr("Import…"), &dialog);
  import_button->setObjectName(QStringLiteral("customShapeManagerImportButton"));
  import_button->setToolTip(QObject::tr("Import Photoshop .csh shape files or SVG files"));
  auto* duplicate_button = new QPushButton(QObject::tr("Duplicate"), &dialog);
  duplicate_button->setObjectName(QStringLiteral("customShapeManagerDuplicateButton"));
  auto* delete_button = new QPushButton(QObject::tr("Delete"), &dialog);
  delete_button->setObjectName(QStringLiteral("customShapeManagerDeleteButton"));
  delete_button->setToolTip(QObject::tr("Delete the selected shapes or folders (Del)"));
  scaffold.add_action_row({import_button}, {duplicate_button, delete_button});

  auto* restore_button = new QPushButton(QObject::tr("Restore Default Shapes"), &dialog);
  restore_button->setObjectName(QStringLiteral("customShapeManagerRestoreButton"));
  restore_button->setToolTip(QObject::tr("Bring back deleted built-in shapes and reset changed defaults"));
  scaffold.add_restore_button_left(restore_button);

  QString selected_storage_id;
  const auto use_selected = scaffold.single_selection_accept(
      [tree] { return tree->selected_ids(); },
      [&library](const QString& id) { return library.find_entry(id) != nullptr; },
      selected_storage_id);
  auto* use_button = scaffold.add_dialog_buttons(
      QObject::tr("Use Shape"), QStringLiteral("customShapeManagerUseButton"), use_selected);

  const auto show_update_failure = [&] {
    QMessageBox::warning(
        &dialog, QObject::tr("Custom Shapes"),
        QObject::tr("Could not update the selected shape. Check that the shape library folder is writable."));
  };

  const auto refresh_details = [&] {
    const auto ids = tree->selected_ids();
    const auto* entry = ids.size() == 1 ? library.find_entry(ids.front()) : nullptr;
    const auto single = entry != nullptr;
    const auto any = !ids.isEmpty();
    name_edit->setEnabled(single);
    folder_edit->setEnabled(any);
    duplicate_button->setEnabled(single);
    delete_button->setEnabled(any);
    use_button->setEnabled(single);
    if (!any) {
      name_edit->clear();
      folder_edit->clear();
      preview->setPixmap(QPixmap());
      return;
    }
    const auto* first = library.find_entry(ids.front());
    {
      const QSignalBlocker blocker(name_edit);
      if (single) {
        name_edit->setPlaceholderText({});
        name_edit->setText(entry->name);
      } else {
        name_edit->clear();
        name_edit->setPlaceholderText(
            QObject::tr("%n shape(s) selected", nullptr, static_cast<int>(ids.size())));
      }
    }
    {
      const QSignalBlocker blocker(folder_edit);
      folder_edit->setText(first != nullptr ? first->folder : QString());
    }
    if (first != nullptr) {
      const auto extent = std::max(160, std::min(preview->width(), preview->height()) - 12);
      preview->setPixmap(custom_shape_thumbnail(first->path, extent));
    }
  };

  const auto delete_selected = [&] {
    const auto ids = tree->selected_ids();
    if (ids.isEmpty()) {
      return;
    }
    const auto* only = ids.size() == 1 ? library.find_entry(ids.front()) : nullptr;
    const auto question =
        only != nullptr
            ? QObject::tr("Delete shape \"%1\"?").arg(custom_shape_display_name(*only))
            : QObject::tr("Delete %n shape(s)?", nullptr, static_cast<int>(ids.size()));
    if (QMessageBox::question(&dialog, QObject::tr("Delete Shapes"), question) != QMessageBox::Yes) {
      return;
    }
    tree->remember_collapsed_folders();
    const auto removed = library.remove_shapes(ids);
    tree->reload({});
    refresh_details();
    const auto failed = static_cast<int>(ids.size()) - removed;
    if (failed > 0) {
      QMessageBox::warning(
          &dialog, QObject::tr("Delete Shapes"),
          QObject::tr("Could not delete %n shape(s). Check that the shape library folder is writable.",
                      nullptr, failed));
    }
  };

  scaffold.connect_selection_changed(refresh_details);
  tree->set_entry_double_clicked_callback([use_selected](const QString&) { use_selected(); });
  scaffold.add_delete_plumbing(delete_button, delete_selected);

  QObject::connect(name_edit, &QLineEdit::editingFinished, &dialog, [&] {
    const auto ids = tree->selected_ids();
    if (ids.size() != 1 || name_edit->text().trimmed().isEmpty()) {
      return;
    }
    if (library.rename_shape(ids.front(), name_edit->text())) {
      tree->remember_collapsed_folders();
      tree->reload(ids.front());
      refresh_details();
    } else {
      show_update_failure();
    }
  });
  QObject::connect(folder_edit, &QLineEdit::editingFinished, &dialog, [&] {
    const auto ids = tree->selected_ids();
    if (ids.isEmpty()) {
      return;
    }
    auto failed = false;
    for (const auto& id : ids) {
      failed = !library.set_shape_folder(id, folder_edit->text()) || failed;
    }
    tree->remember_collapsed_folders();
    tree->reload(ids.front());
    refresh_details();
    if (failed) {
      show_update_failure();
    }
  });
  QObject::connect(import_button, &QPushButton::clicked, &dialog, [&] {
    const auto filter =
        QStringLiteral("%1;;%2;;%3;;%4")
            .arg(QObject::tr("Custom Shapes and SVG Files (*.csh *.svg *.svgz)"),
                 QObject::tr("Photoshop Custom Shapes (*.csh)"), QObject::tr("SVG Files (*.svg *.svgz)"),
                 QObject::tr("All Files (*.*)"));
    const auto paths = get_open_file_names(&dialog, QObject::tr("Import Shapes"), {}, filter, nullptr,
                                           QStringLiteral("customShapeManagerImportFileDialog"));
    if (paths.isEmpty()) {
      return;
    }
    QStringList problems;
    QString first_storage_id;
    int imported = 0;
    for (const auto& path : paths) {
      QString error;
      if (QFileInfo(path).suffix().compare(QStringLiteral("csh"), Qt::CaseInsensitive) == 0) {
        QStringList warnings;
        const auto added = library.import_csh(path, error, warnings);
        for (const auto& warning : warnings) {
          problems.append(QStringLiteral("%1: %2").arg(QFileInfo(path).fileName(), warning));
        }
        if (!error.isEmpty()) {
          problems.append(QStringLiteral("%1: %2").arg(QFileInfo(path).fileName(), error));
        }
        imported += static_cast<int>(added.size());
        if (first_storage_id.isEmpty() && !added.isEmpty()) {
          first_storage_id = added.front();
        }
        continue;
      }
      const auto shape = custom_shape_path_from_svg_file(path, error);
      if (!shape.has_value()) {
        problems.append(QStringLiteral("%1: %2").arg(QFileInfo(path).fileName(), error));
        continue;
      }
      const auto name = QFileInfo(path).completeBaseName();
      const auto storage_id = library.add_shape(name.isEmpty() ? QObject::tr("SVG Shape") : name, *shape);
      if (storage_id.isEmpty()) {
        problems.append(QStringLiteral("%1: %2").arg(
            QFileInfo(path).fileName(),
            QObject::tr("Could not update the selected shape. Check that the shape library folder is writable.")));
        continue;
      }
      ++imported;
      if (first_storage_id.isEmpty()) {
        first_storage_id = storage_id;
      }
    }
    if (first_storage_id.isEmpty()) {
      QMessageBox::warning(&dialog, QObject::tr("Import Shapes"),
                           problems.isEmpty() ? QObject::tr("No shapes could be imported.")
                                              : problems.join(QLatin1Char('\n')));
      return;
    }
    tree->remember_collapsed_folders();
    tree->reload(first_storage_id);
    refresh_details();
    if (!problems.isEmpty()) {
      QMessageBox message(QMessageBox::Information, QObject::tr("Import Shapes"),
                          QObject::tr("Imported %n shape(s).", nullptr, imported), QMessageBox::Ok, &dialog);
      message.setDetailedText(problems.join(QLatin1Char('\n')));
      message.exec();
    }
  });
  QObject::connect(duplicate_button, &QPushButton::clicked, &dialog, [&] {
    const auto ids = tree->selected_ids();
    if (ids.size() != 1) {
      return;
    }
    const auto duplicate = library.duplicate_shape(ids.front());
    if (!duplicate.isEmpty()) {
      tree->remember_collapsed_folders();
      tree->reload(duplicate);
      refresh_details();
    } else {
      show_update_failure();
    }
  });
  QObject::connect(restore_button, &QPushButton::clicked, &dialog, [&] {
    const auto restored = library.restore_default_shapes();
    const auto reset = library.reset_default_shapes_to_factory();
    const auto complete = library.default_shapes_match_factory();
    if (restored == 0 && reset == 0) {
      if (complete) {
        QMessageBox::information(&dialog, QObject::tr("Restore Default Shapes"),
                                 QObject::tr("All default shapes are already present with factory settings."));
      } else {
        QMessageBox::warning(
            &dialog, QObject::tr("Restore Default Shapes"),
            QObject::tr("Some default shapes could not be restored. Check that the shape library folder is writable."));
      }
      return;
    }
    tree->remember_collapsed_folders();
    tree->reload({});
    refresh_details();
    QStringList parts;
    if (restored > 0) {
      parts << QObject::tr("Restored %n default shape(s).", nullptr, restored);
    }
    if (reset > 0) {
      parts << QObject::tr("Reset %n default shape(s) to factory settings.", nullptr, reset);
    }
    if (!complete) {
      parts << QObject::tr("Some default shapes could not be restored. Check that the shape library folder is writable.");
      QMessageBox::warning(&dialog, QObject::tr("Restore Default Shapes"), parts.join(QLatin1Char('\n')));
    } else {
      QMessageBox::information(&dialog, QObject::tr("Restore Default Shapes"), parts.join(QLatin1Char('\n')));
    }
  });

  QString initial_storage_id;
  if (const auto* entry = library.find_entry_by_shape_id(initial_shape_id); entry != nullptr) {
    initial_storage_id = entry->storage_id;
  }
  tree->reload(initial_storage_id);
  refresh_details();
  dialog.resize(760, 520);
  exec_dialog(dialog);
  return selected_storage_id;
}

}  // namespace patchy::ui
