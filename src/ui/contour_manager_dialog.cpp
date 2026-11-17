#include "ui/contour_manager_dialog.hpp"

#include "core/contour_editing.hpp"
#include "core/contour_presets.hpp"
#include "ui/contour_editor_dialog.hpp"
#include "ui/contour_library.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/localization.hpp"
#include "ui/preset_manager_scaffold.hpp"
#include "ui/theme_palette.hpp"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace patchy::ui {

namespace {

// A saved contour's row carries its storage id, a built-in's row its preset id.
constexpr int kStorageRole = Qt::UserRole;
constexpr int kBuiltinRole = Qt::UserRole + 1;

// "<name> copy", else "<name> copy 2" and up, free among the saved contours.
QString contour_copy_name(const QString& name, const ContourLibrary& library) {
  const auto taken = [&library](const QString& candidate) {
    const auto& entries = library.entries();
    return std::any_of(entries.begin(), entries.end(),
                       [&candidate](const ContourLibraryEntry& entry) { return entry.name == candidate; });
  };
  auto candidate = QObject::tr("%1 copy").arg(name);
  for (int number = 2; taken(candidate); ++number) {
    candidate = QObject::tr("%1 copy %2").arg(name).arg(number);
  }
  return candidate;
}

}  // namespace

void request_contour_manager(QWidget* parent, ContourLibrary& library, const QString& initial_storage_id) {
  QDialog dialog(parent);
  PresetManagerScaffold scaffold(dialog, QStringLiteral("contourManagerDialog"), QObject::tr("Contour Manager"));

  auto* tree = new QTreeWidget(&dialog);
  tree->setObjectName(QStringLiteral("contourManagerTree"));
  tree->setHeaderHidden(true);
  // Name with the curve chip, then Built-in.
  tree->setColumnCount(2);
  tree->header()->setStretchLastSection(false);
  tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  tree->setRootIsDecorated(false);
  tree->setSelectionMode(QAbstractItemView::SingleSelection);
  tree->setIconSize(QSize(32, 24));
  scaffold.add_tree(tree, 280);

  auto* builtin_note = new QLabel(
      QObject::tr("Built-in contour: it cannot be renamed, edited or deleted. Duplicate it to make a copy "
                  "you can edit."),
      &dialog);
  builtin_note->setObjectName(QStringLiteral("contourManagerBuiltinNote"));
  builtin_note->setWordWrap(true);
  // Its room stays when it hides, so the preview keeps its size from row to row.
  auto note_policy = builtin_note->sizePolicy();
  note_policy.setRetainSizeWhenHidden(true);
  builtin_note->setSizePolicy(note_policy);

  // The selected contour's curve, drawn by the editor's graph so it reads the same.
  auto* preview = new ContourGraphWidget(&dialog);
  preview->setObjectName(QStringLiteral("contourManagerPreview"));
  preview->set_read_only(true);
  scaffold.right()->addWidget(preview, 1);

  auto* form = new QFormLayout();
  auto* name_edit = new QLineEdit(&dialog);
  name_edit->setObjectName(QStringLiteral("contourManagerNameEdit"));
  form->addRow(QObject::tr("Name:"), name_edit);
  scaffold.right()->addLayout(form);
  scaffold.right()->addWidget(builtin_note);

  auto* edit_button = new QPushButton(QObject::tr("Edit Curve…"), &dialog);
  edit_button->setObjectName(QStringLiteral("contourManagerEditButton"));
  edit_button->setToolTip(QObject::tr("Edit this contour's curve"));
  auto* duplicate_button = new QPushButton(QObject::tr("Duplicate"), &dialog);
  duplicate_button->setObjectName(QStringLiteral("contourManagerDuplicateButton"));
  auto* delete_button = new QPushButton(QObject::tr("Delete"), &dialog);
  delete_button->setObjectName(QStringLiteral("contourManagerDeleteButton"));
  scaffold.add_action_row({edit_button, duplicate_button}, {delete_button});
  auto* load_button = new QPushButton(QObject::tr("Load Contours…"), &dialog);
  load_button->setObjectName(QStringLiteral("contourManagerLoadButton"));
  auto* save_button = new QPushButton(QObject::tr("Save Contours…"), &dialog);
  save_button->setObjectName(QStringLiteral("contourManagerSaveButton"));
  scaffold.add_action_row({load_button, save_button}, {});

  auto* close_buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
  close_buttons->setObjectName(QStringLiteral("contourManagerButtons"));
  scaffold.right()->addWidget(close_buttons);
  QObject::connect(close_buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  // No button is the default, so Enter in the Name field only commits the name.
  for (auto* button : dialog.findChildren<QPushButton*>()) {
    button->setAutoDefault(false);
    button->setDefault(false);
  }

  const auto current_item = [tree]() -> QTreeWidgetItem* {
    auto* item = tree->currentItem();
    return item != nullptr && item->isSelected() ? item : nullptr;
  };
  const auto selected_entry = [&]() -> const ContourLibraryEntry* {
    const auto* item = current_item();
    return item != nullptr ? library.find_entry(item->data(0, kStorageRole).toString()) : nullptr;
  };
  const auto selected_builtin = [&]() -> const ContourPreset* {
    const auto* item = current_item();
    const auto id = item != nullptr ? item->data(0, kBuiltinRole).toString() : QString();
    return id.isEmpty() ? nullptr : find_builtin_contour_preset(id.toStdString());
  };

  const auto refresh_details = [&] {
    const auto* entry = selected_entry();
    const auto* builtin = entry == nullptr ? selected_builtin() : nullptr;
    builtin_note->setVisible(builtin != nullptr);
    name_edit->setEnabled(entry != nullptr);
    edit_button->setEnabled(entry != nullptr);
    duplicate_button->setEnabled(entry != nullptr || builtin != nullptr);
    delete_button->setEnabled(entry != nullptr);
    save_button->setEnabled(!library.entries().empty());
    const QSignalBlocker blocker(name_edit);
    if (entry != nullptr) {
      name_edit->setText(entry->name);
      preview->set_contour(entry->contour);
    } else if (builtin != nullptr) {
      name_edit->setText(translate_data_text(builtin->english_name));
      preview->set_contour(builtin->contour);
    } else {
      name_edit->clear();
      preview->set_contour(StyleContour{});
    }
  };
  // Rebuilds the list and selects the saved contour `storage_id`, else the
  // built-in `builtin_id`, else the first row.
  const auto reload = [&](const QString& storage_id, const QString& builtin_id = {}) {
    {
      const QSignalBlocker blocker(tree);
      tree->clear();
      QTreeWidgetItem* select = nullptr;
      for (const auto& preset : builtin_contour_presets()) {
        auto* item = new QTreeWidgetItem(tree, QStringList{translate_data_text(preset.english_name)});
        item->setIcon(0, contour_preset_icon(preset.contour));
        item->setData(0, kBuiltinRole, QString::fromLatin1(preset.id));
        item->setText(1, QObject::tr("Built-in"));
        item->setForeground(1, theme().text_secondary);
        if (storage_id.isEmpty() && !builtin_id.isEmpty() && builtin_id == QLatin1String(preset.id)) {
          select = item;
        }
      }
      for (const auto& entry : library.entries()) {
        auto* item = new QTreeWidgetItem(tree, QStringList{entry.name});
        item->setIcon(0, contour_preset_icon(entry.contour));
        item->setData(0, kStorageRole, entry.storage_id);
        if (!storage_id.isEmpty() && entry.storage_id == storage_id) {
          select = item;
        }
      }
      if (select == nullptr) {
        select = tree->topLevelItem(0);
      }
      if (select != nullptr) {
        tree->setCurrentItem(select);
        tree->scrollToItem(select);
      }
    }
    refresh_details();
  };
  // Rebuilds the list around whatever is selected now.
  const auto reload_keeping_selection = [&] {
    const auto* item = current_item();
    reload(item != nullptr ? item->data(0, kStorageRole).toString() : QString(),
           item != nullptr ? item->data(0, kBuiltinRole).toString() : QString());
  };
  const auto show_error = [&](const QString& title, const QString& text) {
    show_critical_message(&dialog, title, text, QStringLiteral("layerStyleContourErrorDialog"));
  };

  const auto edit_selected = [&] {
    const auto* entry = selected_entry();
    if (entry == nullptr) {
      return;
    }
    const auto storage_id = entry->storage_id;
    const auto before = entry->contour;
    QString saved_as;
    const auto edited = request_contour_edit(&dialog, before, {}, &library, &saved_as);
    if (!edited.has_value()) {
      // Cancel keeps a contour the editor saved as new: it is on the list now.
      reload(storage_id);
      return;
    }
    if (!saved_as.isEmpty()) {
      // Saved as a new contour: the one the editor opened on stays as it was.
      reload(saved_as);
      return;
    }
    if (!style_contours_same_shape(*edited, before) && !library.set_contour_curve(storage_id, *edited)) {
      show_error(QObject::tr("Contour Manager"), QObject::tr("Could not save the contour."));
    }
    reload(storage_id);
  };
  // A copy of any row, built-in or saved, becomes a new saved contour, selected
  // with its name ready to change.
  const auto duplicate_selected = [&] {
    const auto* entry = selected_entry();
    const auto* builtin = entry == nullptr ? selected_builtin() : nullptr;
    if (entry == nullptr && builtin == nullptr) {
      return;
    }
    const auto name = entry != nullptr ? entry->name : translate_data_text(builtin->english_name);
    const auto contour = entry != nullptr ? entry->contour : builtin->contour;
    const auto id = library.add_contour(contour_copy_name(name, library), contour);
    if (id.isEmpty()) {
      show_error(QObject::tr("Contour Manager"), QObject::tr("Could not save the contour."));
      return;
    }
    reload(id);
    name_edit->setFocus();
    name_edit->selectAll();
  };
  const auto delete_selected = [&] {
    const auto* entry = selected_entry();
    if (entry == nullptr) {
      return;
    }
    if (show_warning_message(&dialog, QObject::tr("Delete Contour"),
                             QObject::tr("Delete contour \"%1\"?").arg(entry->name),
                             QMessageBox::Yes | QMessageBox::No, QMessageBox::No,
                             QStringLiteral("contourManagerDeleteMessageBox")) != QMessageBox::Yes) {
      return;
    }
    // The next saved contour takes the selection, else the one before it.
    const auto& entries = library.entries();
    const auto index = static_cast<std::size_t>(entry - entries.data());
    QString next;
    if (index + 1U < entries.size()) {
      next = entries[index + 1U].storage_id;
    } else if (index > 0U) {
      next = entries[index - 1U].storage_id;
    }
    const auto storage_id = entry->storage_id;
    (void)library.remove_contour(storage_id);
    reload(next);
  };

  scaffold.connect_selection_changed(refresh_details);
  scaffold.add_delete_plumbing(delete_button, delete_selected);
  QObject::connect(edit_button, &QPushButton::clicked, &dialog, edit_selected);
  QObject::connect(duplicate_button, &QPushButton::clicked, &dialog, duplicate_selected);
  QObject::connect(tree, &QTreeWidget::itemDoubleClicked, &dialog, [&](QTreeWidgetItem*, int) { edit_selected(); });
  QObject::connect(name_edit, &QLineEdit::editingFinished, &dialog, [&] {
    const auto* entry = selected_entry();
    const auto name = name_edit->text().trimmed();
    if (entry == nullptr || name.isEmpty() || name == entry->name) {
      refresh_details();
      return;
    }
    const auto storage_id = entry->storage_id;
    if (!library.rename_contour(storage_id, name)) {
      show_error(QObject::tr("Rename Contour"), QObject::tr("Could not rename the contour."));
    }
    reload(storage_id);
  });
  QObject::connect(load_button, &QPushButton::clicked, &dialog, [&] {
    const auto ids = load_contours_from_file(&dialog, library, QStringLiteral("contourManagerLoadFileDialog"));
    if (ids.isEmpty()) {
      reload_keeping_selection();
    } else {
      reload(ids.front());
    }
  });
  QObject::connect(save_button, &QPushButton::clicked, &dialog, [&] {
    save_contours_to_file(&dialog, library, QStringLiteral("contourManagerSaveFileDialog"));
  });

  reload(initial_storage_id);
  dialog.resize(760, 480);
  exec_dialog(dialog);
}

}  // namespace patchy::ui
