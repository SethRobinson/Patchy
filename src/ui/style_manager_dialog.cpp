#include "ui/style_manager_dialog.hpp"

#include "ui/style_browser.hpp"
#include "ui/style_library.hpp"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace patchy::ui {

QString request_style_manager(QWidget* parent, StyleLibrary& library,
                              const QString& initial_style_id) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("styleManagerDialog"));
  dialog.setWindowTitle(QObject::tr("Styles"));
  dialog.setModal(true);

  auto* main_layout = new QHBoxLayout(&dialog);
  auto* tree = new StyleBrowserWidget(&library, &dialog);
  tree->setObjectName(QStringLiteral("styleManagerTree"));
  tree->set_icon_extent(40);
  tree->setMinimumWidth(300);
  main_layout->addWidget(tree, 1);

  auto* right = new QVBoxLayout();
  main_layout->addLayout(right, 2);
  auto* preview = new QLabel(&dialog);
  preview->setObjectName(QStringLiteral("styleManagerPreview"));
  preview->setMinimumSize(220, 200);
  preview->setAlignment(Qt::AlignCenter);
  right->addWidget(preview, 1);

  auto* form = new QFormLayout();
  auto* name_edit = new QLineEdit(&dialog);
  name_edit->setObjectName(QStringLiteral("styleManagerNameEdit"));
  form->addRow(QObject::tr("Name:"), name_edit);
  auto* folder_edit = new QLineEdit(&dialog);
  folder_edit->setObjectName(QStringLiteral("styleManagerFolderEdit"));
  folder_edit->setPlaceholderText(QObject::tr("No folder"));
  folder_edit->setToolTip(
      QObject::tr("Folder for the selected style(s); leave empty to remove them from folders"));
  form->addRow(QObject::tr("Folder:"), folder_edit);
  auto* effects_label = new QLabel(&dialog);
  effects_label->setObjectName(QStringLiteral("styleManagerEffectsLabel"));
  effects_label->setWordWrap(true);
  form->addRow(QObject::tr("Contains:"), effects_label);
  right->addLayout(form);

  auto* action_row = new QHBoxLayout();
  auto* import_button = new QPushButton(QObject::tr("Import .asl…"), &dialog);
  import_button->setObjectName(QStringLiteral("styleManagerImportButton"));
  auto* export_button = new QPushButton(QObject::tr("Export…"), &dialog);
  export_button->setObjectName(QStringLiteral("styleManagerExportButton"));
  export_button->setToolTip(QObject::tr("Export the selected styles or folders to a .asl file"));
  auto* duplicate_button = new QPushButton(QObject::tr("Duplicate"), &dialog);
  duplicate_button->setObjectName(QStringLiteral("styleManagerDuplicateButton"));
  auto* delete_button = new QPushButton(QObject::tr("Delete"), &dialog);
  delete_button->setObjectName(QStringLiteral("styleManagerDeleteButton"));
  delete_button->setToolTip(QObject::tr("Delete the selected styles or folders (Del)"));
  action_row->addWidget(import_button);
  action_row->addWidget(export_button);
  action_row->addStretch(1);
  action_row->addWidget(duplicate_button);
  action_row->addWidget(delete_button);
  right->addLayout(action_row);

  auto* restore_button = new QPushButton(QObject::tr("Restore Default Styles"), &dialog);
  restore_button->setObjectName(QStringLiteral("styleManagerRestoreButton"));
  restore_button->setToolTip(
      QObject::tr("Bring back deleted built-in styles and reset changed defaults"));
  right->addWidget(restore_button, 0, Qt::AlignLeft);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
  auto* use_button = buttons->addButton(QObject::tr("Use Style"), QDialogButtonBox::AcceptRole);
  use_button->setObjectName(QStringLiteral("styleManagerUseButton"));
  right->addWidget(buttons);

  QString selected_storage_id;

  const auto show_update_failure = [&] {
    QMessageBox::warning(
        &dialog, QObject::tr("Styles"),
        QObject::tr("Could not update the selected style. Check that the style library folder is writable."));
  };

  // One-line modeled-effects summary for the selected entry.
  const auto effects_summary = [](const StyleLibraryEntry& entry) {
    QStringList parts;
    const auto add = [&parts](std::size_t count, const QString& name) {
      if (count == 1U) {
        parts << name;
      } else if (count > 1U) {
        parts << QStringLiteral("%1 ×%2").arg(name).arg(count);
      }
    };
    const auto& style = entry.style;
    add(style.bevels.size(), QObject::tr("Bevel & Emboss"));
    add(style.strokes.size(), QObject::tr("Stroke"));
    add(style.inner_shadows.size(), QObject::tr("Inner Shadow"));
    add(style.inner_glows.size(), QObject::tr("Inner Glow"));
    add(style.satins.size(), QObject::tr("Satin"));
    add(style.color_overlays.size(), QObject::tr("Color Overlay"));
    add(style.gradient_fills.size(), QObject::tr("Gradient Overlay"));
    add(style.pattern_overlays.size(), QObject::tr("Pattern Overlay"));
    add(style.outer_glows.size(), QObject::tr("Outer Glow"));
    add(style.drop_shadows.size(), QObject::tr("Drop Shadow"));
    if (entry.blend_settings.has_value()) {
      parts << QObject::tr("Blending Options");
    }
    return parts.isEmpty() ? QObject::tr("No effects") : parts.join(QStringLiteral(", "));
  };

  const auto refresh_details = [&] {
    const auto ids = tree->selected_storage_ids();
    const auto* entry = ids.size() == 1 ? library.find_entry(ids.front()) : nullptr;
    const auto single = entry != nullptr;
    const auto any = !ids.isEmpty();
    name_edit->setEnabled(single);
    folder_edit->setEnabled(any);
    duplicate_button->setEnabled(single);
    delete_button->setEnabled(any);
    export_button->setEnabled(any);
    use_button->setEnabled(single);
    if (!any) {
      name_edit->clear();
      folder_edit->clear();
      effects_label->clear();
      preview->setPixmap(QPixmap());
      return;
    }
    const auto* first = library.find_entry(ids.front());
    if (single) {
      {
        const QSignalBlocker blocker(name_edit);
        name_edit->setText(entry->name);
      }
      {
        const QSignalBlocker blocker(folder_edit);
        folder_edit->setText(entry->folder);
      }
      effects_label->setText(effects_summary(*entry));
    } else {
      name_edit->clear();
      {
        const QSignalBlocker blocker(folder_edit);
        folder_edit->setText(first != nullptr ? first->folder : QString());
      }
      effects_label->setText(
          QObject::tr("%n style(s) selected", nullptr, static_cast<int>(ids.size())));
    }
    if (first != nullptr) {
      const auto extent = std::max(160, std::min(preview->width(), preview->height()) - 12);
      preview->setPixmap(render_style_preview(first->style, first->blend_settings,
                                              library.patterns_for_entry(first->storage_id),
                                              extent));
    }
  };

  const auto use_selected = [&] {
    const auto ids = tree->selected_storage_ids();
    const auto* entry = ids.size() == 1 ? library.find_entry(ids.front()) : nullptr;
    if (entry == nullptr) {
      return;
    }
    selected_storage_id = entry->storage_id;
    dialog.accept();
  };

  const auto delete_selected = [&] {
    const auto ids = tree->selected_storage_ids();
    if (ids.isEmpty()) {
      return;
    }
    const auto question =
        ids.size() == 1
            ? QObject::tr("Delete style \"%1\"?")
                  .arg(library.find_entry(ids.front()) != nullptr
                           ? style_library_entry_display_name(*library.find_entry(ids.front()))
                           : ids.front())
            : QObject::tr("Delete %n style(s)?", nullptr, static_cast<int>(ids.size()));
    if (QMessageBox::question(&dialog, QObject::tr("Delete Styles"), question) !=
        QMessageBox::Yes) {
      return;
    }
    const auto removed = library.remove_styles(ids);
    refresh_details();
    const auto failed = static_cast<int>(ids.size()) - removed;
    if (failed > 0) {
      QMessageBox::warning(
          &dialog, QObject::tr("Delete Styles"),
          QObject::tr("Could not delete %n style(s). Check that the style library folder is writable.",
                      nullptr, failed));
    }
  };

  QObject::connect(tree, &QTreeWidget::itemSelectionChanged, &dialog, refresh_details);
  QObject::connect(tree, &StyleBrowserWidget::style_double_clicked, &dialog,
                   [&](const QString&) { use_selected(); });
  auto* delete_shortcut = new QShortcut(QKeySequence::Delete, tree);
  delete_shortcut->setContext(Qt::WidgetWithChildrenShortcut);
  QObject::connect(delete_shortcut, &QShortcut::activated, &dialog, delete_selected);
  QObject::connect(delete_button, &QPushButton::clicked, &dialog, delete_selected);
  QObject::connect(export_button, &QPushButton::clicked, &dialog,
                   [tree] { tree->export_selection(); });

  QObject::connect(name_edit, &QLineEdit::editingFinished, &dialog, [&] {
    const auto ids = tree->selected_storage_ids();
    if (ids.size() != 1 || name_edit->text().trimmed().isEmpty()) {
      return;
    }
    if (library.rename_style(ids.front(), name_edit->text())) {
      refresh_details();
    } else {
      show_update_failure();
    }
  });
  QObject::connect(folder_edit, &QLineEdit::editingFinished, &dialog, [&] {
    const auto ids = tree->selected_storage_ids();
    if (ids.isEmpty()) {
      return;
    }
    auto failed = false;
    for (const auto& id : ids) {
      failed = !library.set_style_folder(id, folder_edit->text()) || failed;
    }
    refresh_details();
    if (failed) {
      show_update_failure();
    }
  });
  QObject::connect(import_button, &QPushButton::clicked, &dialog, [&] {
    const auto path = QFileDialog::getOpenFileName(&dialog, QObject::tr("Import Photoshop Styles"),
                                                   {}, QObject::tr("Photoshop Styles (*.asl)"));
    if (path.isEmpty()) {
      return;
    }
    const auto before = library.entries().size();
    QString error;
    QStringList warnings;
    const auto first = library.import_asl(path, error, warnings);
    if (first.isEmpty()) {
      QMessageBox::warning(&dialog, QObject::tr("Import Styles"), error);
      return;
    }
    tree->reload(first);
    refresh_details();
    if (!warnings.isEmpty()) {
      const auto imported = static_cast<int>(library.entries().size() - before);
      QMessageBox message(QMessageBox::Information, QObject::tr("Import Styles"),
                          QObject::tr("Imported %n style(s).", nullptr, imported), QMessageBox::Ok,
                          &dialog);
      message.setDetailedText(warnings.join(QLatin1Char('\n')));
      message.exec();
    }
  });
  QObject::connect(duplicate_button, &QPushButton::clicked, &dialog, [&] {
    const auto ids = tree->selected_storage_ids();
    if (ids.size() != 1) {
      return;
    }
    const auto duplicate = library.duplicate_style(ids.front());
    if (!duplicate.isEmpty()) {
      tree->reload(duplicate);
      refresh_details();
    } else {
      show_update_failure();
    }
  });
  QObject::connect(restore_button, &QPushButton::clicked, &dialog, [&] {
    const auto restored = library.restore_default_styles();
    const auto reset = library.reset_default_styles_to_factory();
    const auto complete = library.default_styles_match_factory();
    if (restored == 0 && reset == 0) {
      if (complete) {
        QMessageBox::information(
            &dialog, QObject::tr("Restore Default Styles"),
            QObject::tr("All default styles are already present with factory settings."));
      } else {
        QMessageBox::warning(
            &dialog, QObject::tr("Restore Default Styles"),
            QObject::tr("Some default styles could not be restored. Check that the style library folder is writable."));
      }
      return;
    }
    refresh_details();
    QStringList parts;
    if (restored > 0) {
      parts << QObject::tr("Restored %n default style(s).", nullptr, restored);
    }
    if (reset > 0) {
      parts << QObject::tr("Reset %n default style(s) to factory settings.", nullptr, reset);
    }
    if (!complete) {
      parts << QObject::tr("Some default styles could not be restored. Check that the style library folder is writable.");
      QMessageBox::warning(&dialog, QObject::tr("Restore Default Styles"),
                           parts.join(QLatin1Char('\n')));
    } else {
      QMessageBox::information(&dialog, QObject::tr("Restore Default Styles"),
                               parts.join(QLatin1Char('\n')));
    }
  });
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, use_selected);

  QString initial_storage_id;
  if (const auto* entry = library.find_entry_by_style_id(initial_style_id); entry != nullptr) {
    initial_storage_id = entry->storage_id;
  }
  tree->reload(initial_storage_id);
  refresh_details();
  dialog.resize(860, 560);
  dialog.exec();
  return selected_storage_id;
}

}  // namespace patchy::ui
