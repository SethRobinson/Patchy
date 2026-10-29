#include "ui/brush_tip_manager_dialog.hpp"

#include "core/brush_tip.hpp"
#include "core/document.hpp"
#include "core/pixel_tools.hpp"
#include "ui/brush_dynamics_popup.hpp"
#include "ui/brush_stroke_preview.hpp"
#include "ui/brush_tip_library.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/preset_manager_scaffold.hpp"
#include "ui/preset_tree_widget.hpp"
#include "ui/theme_qss.hpp"
#include "ui/measurement_units.hpp"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <memory>

namespace patchy::ui {

void request_brush_tip_manager(QWidget* parent, BrushTipLibrary& library, const QString& initial_tip_id,
                               const std::function<QImage()>& capture_define_source,
                               const std::function<void(const QString&)>& activate_tip) {
  QDialog dialog(parent);
  PresetManagerScaffold scaffold(dialog, QStringLiteral("brushTipManagerDialog"),
                                 QObject::tr("Brush Tips"));

  auto* tree = new PresetTreeWidget(&dialog);
  tree->setObjectName(QStringLiteral("brushTipManagerTree"));
  tree->setIconSize(QSize(40, 40));
  // No uniform row heights: Qt would size every row from the first one (a short folder row),
  // squashing the 40px thumbnails into overlapping slivers.
  tree->set_entry_row_height(46);  // room for the 40px thumbnail
  tree->set_reload_fallback(PresetTreeWidget::ReloadFallback::first_entry_when_expanded);
  tree->set_folder_label_callback([](const QString& folder, int count) {
    return QObject::tr("%1 (%2)").arg(folder).arg(count);
  });
  tree->set_entries_callback([&library] {
    std::vector<PresetTreeEntry> rows;
    for (const auto& entry : library.entries()) {
      auto tooltip =
          QObject::tr("%1 (%2×%3)").arg(entry.name).arg(entry.size.width()).arg(entry.size.height());
      if (brush_tip_entry_has_dynamics(entry)) {
        tooltip += QObject::tr(" • dynamics");
      }
      rows.push_back(
          {entry.id, entry.name, QIcon(brush_tip_thumbnail_with_badge(entry)), tooltip, entry.folder});
    }
    return rows;
  });
  // Imported sets run to hundreds of tips: a filter over the names (folders open while it
  // narrows the list), like the shape and preset pickers' search fields.
  auto* filter = new QLineEdit(&dialog);
  filter->setObjectName(QStringLiteral("brushTipManagerFilterEdit"));
  filter->setPlaceholderText(QObject::tr("Filter"));
  filter->setClearButtonEnabled(true);
  QObject::connect(filter, &QLineEdit::textChanged, tree, [tree](const QString& text) { tree->set_filter_text(text); });
  scaffold.add_tree(tree, 320, filter);

  auto* preview = new BrushStrokePreview(&dialog);
  scaffold.right()->addWidget(preview);

  auto* form = new QFormLayout();
  auto* name_edit = new QLineEdit(&dialog);
  name_edit->setObjectName(QStringLiteral("brushTipNameEdit"));
  form->addRow(QObject::tr("Name:"), name_edit);
  auto* folder_edit = new QLineEdit(&dialog);
  folder_edit->setObjectName(QStringLiteral("brushTipFolderEdit"));
  folder_edit->setPlaceholderText(QObject::tr("No folder"));
  folder_edit->setToolTip(
      QObject::tr("Folder for the selected brush tip(s); leave empty to remove them from folders"));
  form->addRow(QObject::tr("Folder:"), folder_edit);
  auto* spacing_spin = new QSpinBox(&dialog);
  spacing_spin->setObjectName(QStringLiteral("brushTipSpacingSpin"));
  spacing_spin->setRange(1, 1000);
  spacing_spin->setSuffix(percent_suffix());
  spacing_spin->setValue(25);
  spacing_spin->setToolTip(QObject::tr("Distance between stamps as a percentage of the brush size"));
  spacing_spin->setMinimumWidth(120);
  form->addRow(QObject::tr("Spacing:"), spacing_spin);
  auto* dynamics_button = new QPushButton(QObject::tr("Edit Dynamics…"), &dialog);
  dynamics_button->setObjectName(QStringLiteral("brushTipManagerDynamicsButton"));
  dynamics_button->setToolTip(
      QObject::tr("Tip shape, dynamics, texture, dual brush, color, and effects for the selected brush tip"));
  form->addRow(QObject::tr("Dynamics:"), dynamics_button);
  auto* size_label = new QLabel(&dialog);
  size_label->setObjectName(QStringLiteral("brushTipSizeLabel"));
  form->addRow(QObject::tr("Size:"), size_label);
  scaffold.right()->addLayout(form);

  auto* import_button = new QPushButton(QObject::tr("Import .abr…"), &dialog);
  import_button->setObjectName(QStringLiteral("brushTipManagerImportButton"));
  auto* define_button = new QPushButton(QObject::tr("Define from Selection"), &dialog);
  define_button->setObjectName(QStringLiteral("brushTipManagerDefineButton"));
  define_button->setToolTip(
      QObject::tr("Create a brush tip from the current selection (or the whole image): dark pixels paint, "
                  "light pixels stay clear"));
  auto* duplicate_button = new QPushButton(QObject::tr("Duplicate"), &dialog);
  duplicate_button->setObjectName(QStringLiteral("brushTipManagerDuplicateButton"));
  auto* delete_button = new QPushButton(QObject::tr("Delete"), &dialog);
  delete_button->setObjectName(QStringLiteral("brushTipManagerDeleteButton"));
  delete_button->setToolTip(QObject::tr("Delete the selected brush tips or folders (Del)"));
  scaffold.add_action_row({import_button, define_button}, {duplicate_button, delete_button});
  auto* restore_row = new QHBoxLayout();
  auto* restore_button = new QPushButton(QObject::tr("Restore Default Brushes"), &dialog);
  restore_button->setObjectName(QStringLiteral("brushTipManagerRestoreButton"));
  restore_button->setToolTip(QObject::tr("Bring back any deleted built-in brush tips"));
  restore_row->addWidget(restore_button);
  restore_row->addStretch(1);
  scaffold.right()->addLayout(restore_row);
  scaffold.right()->addStretch(1);

  auto* use_button = scaffold.add_dialog_buttons(
      QObject::tr("Use Brush"), QStringLiteral("brushTipManagerUseButton"), [&] {
        const auto ids = tree->selected_ids();
        if (ids.size() == 1 && activate_tip) {
          activate_tip(ids.front());
        }
        dialog.accept();
      });

  const auto refresh_details = [&] {
    const auto ids = tree->selected_ids();
    const auto* entry = ids.size() == 1 ? library.find_entry(ids.front()) : nullptr;
    const auto single = entry != nullptr;
    const auto any = !ids.isEmpty();
    name_edit->setEnabled(single);
    folder_edit->setEnabled(any);
    spacing_spin->setEnabled(any);
    dynamics_button->setEnabled(single);
    duplicate_button->setEnabled(single);
    delete_button->setEnabled(any);
    use_button->setEnabled(single);
    if (!any) {
      name_edit->clear();
      folder_edit->clear();
      size_label->clear();
      preview->set_dynamics({}, 0.0, 100.0);
      preview->set_tip(nullptr);
      return;
    }
    if (single) {
      {
        const QSignalBlocker name_blocker(name_edit);
        name_edit->setText(entry->name);
      }
      {
        const QSignalBlocker folder_blocker(folder_edit);
        folder_edit->setText(entry->folder);
      }
      {
        const QSignalBlocker spacing_blocker(spacing_spin);
        spacing_spin->setValue(std::clamp(static_cast<int>(std::lround(entry->spacing * 100.0)), 1, 1000));
      }
      size_label->setText(QObject::tr("%1 × %2 px").arg(entry->size.width()).arg(entry->size.height()));
      preview->set_spacing(entry->spacing);
      preview->set_dynamics(entry->dynamics, entry->base_angle_degrees, entry->base_roundness);
      preview->set_tip(library.tip(entry->id));
      return;
    }
    // Multi-selection: folder and spacing edits apply to all selected tips.
    const auto* first_entry = library.find_entry(ids.front());
    {
      const QSignalBlocker name_blocker(name_edit);
      name_edit->clear();
    }
    {
      const QSignalBlocker folder_blocker(folder_edit);
      folder_edit->setText(first_entry != nullptr ? first_entry->folder : QString());
    }
    size_label->setText(QObject::tr("%n brush tip(s) selected", nullptr, static_cast<int>(ids.size())));
    if (first_entry != nullptr) {
      preview->set_spacing(first_entry->spacing);
      preview->set_dynamics(first_entry->dynamics, first_entry->base_angle_degrees,
                            first_entry->base_roundness);
      preview->set_tip(library.tip(first_entry->id));
    }
  };

  const auto delete_selected = [&] {
    const auto ids = tree->selected_ids();
    if (ids.isEmpty()) {
      return;
    }
    QString question;
    if (ids.size() == 1) {
      const auto* entry = library.find_entry(ids.front());
      question = QObject::tr("Delete brush tip \"%1\"?").arg(entry != nullptr ? entry->name : ids.front());
    } else {
      question = QObject::tr("Delete %n brush tip(s)?", nullptr, static_cast<int>(ids.size()));
    }
    const auto answer = QMessageBox::question(&dialog, QObject::tr("Delete Brush Tips"), question);
    if (answer != QMessageBox::Yes) {
      return;
    }
    tree->remember_collapsed_folders();
    library.remove_tips(ids);
    tree->reload(QString());
    refresh_details();
  };

  scaffold.connect_selection_changed([&] { refresh_details(); });
  tree->set_entry_double_clicked_callback([&](const QString& id) {
    if (activate_tip) {
      activate_tip(id);
      dialog.accept();
    }
  });
  scaffold.add_delete_plumbing(delete_button, [&] { delete_selected(); });

  QObject::connect(name_edit, &QLineEdit::editingFinished, &dialog, [&] {
    const auto ids = tree->selected_ids();
    if (ids.size() != 1 || name_edit->text().trimmed().isEmpty()) {
      return;
    }
    if (library.find_entry(ids.front()) != nullptr && library.rename_tip(ids.front(), name_edit->text())) {
      tree->remember_collapsed_folders();
      tree->reload(ids.front());
      refresh_details();
    }
  });
  QObject::connect(folder_edit, &QLineEdit::editingFinished, &dialog, [&] {
    const auto ids = tree->selected_ids();
    if (ids.isEmpty()) {
      return;
    }
    const auto folder = folder_edit->text();
    auto moved = false;
    for (const auto& id : ids) {
      moved = library.set_tip_folder(id, folder) || moved;
    }
    if (moved) {
      tree->remember_collapsed_folders();
      tree->reload(ids.front());
      refresh_details();
    }
  });
  QObject::connect(spacing_spin, &QSpinBox::valueChanged, &dialog, [&](int value) {
    const auto ids = tree->selected_ids();
    for (const auto& id : ids) {
      library.set_tip_spacing(id, static_cast<double>(value) / 100.0);
    }
    if (!ids.isEmpty()) {
      preview->set_spacing(static_cast<double>(value) / 100.0);
    }
  });
  QObject::connect(dynamics_button, &QPushButton::clicked, &dialog, [&] {
    const auto ids = tree->selected_ids();
    if (ids.size() != 1) {
      return;
    }
    const auto tip_id = ids.front();
    const auto* entry = library.find_entry(tip_id);
    if (entry == nullptr) {
      return;
    }

    QDialog editor(&dialog);
    editor.setObjectName(QStringLiteral("brushTipManagerDynamicsDialog"));
    editor.setWindowTitle(QObject::tr("Brush Dynamics: %1").arg(entry->name));
    auto* editor_layout = new QVBoxLayout(&editor);
    auto* panel = new BrushDynamicsPanel(&editor);
    panel->set_values(entry->dynamics, entry->base_angle_degrees, entry->base_roundness);
    panel->set_preview_tip(library.tip(tip_id), entry->spacing);
    editor_layout->addWidget(panel);
    auto* editor_buttons = new QDialogButtonBox(QDialogButtonBox::Close, &editor);
    editor_layout->addWidget(editor_buttons);
    QObject::connect(editor_buttons, &QDialogButtonBox::rejected, &editor, &QDialog::reject);

    // Edits apply live (debounced): the sidecar persists, the stroke preview re-renders, and a
    // canvas using this tip picks the change up through the library's changed() signal.
    const auto apply = [&library, &tip_id, panel, preview] {
      library.set_tip_dynamics(tip_id, panel->dynamics(), panel->base_angle_degrees(),
                               panel->base_roundness());
      preview->set_dynamics(panel->dynamics(), panel->base_angle_degrees(), panel->base_roundness());
    };
    auto* apply_timer = new QTimer(&editor);
    apply_timer->setSingleShot(true);
    apply_timer->setInterval(200);
    QObject::connect(apply_timer, &QTimer::timeout, &editor, apply);
    QObject::connect(panel, &BrushDynamicsPanel::edited, apply_timer, qOverload<>(&QTimer::start));
    exec_dialog(editor);
    if (apply_timer->isActive()) {
      apply_timer->stop();
      apply();  // flush an edit still inside the debounce window
    }
    tree->remember_collapsed_folders();
    tree->reload(tip_id);  // refresh the dynamics badge
    refresh_details();
  });
  QObject::connect(import_button, &QPushButton::clicked, &dialog, [&] {
    const auto path = get_open_file_name(&dialog, QObject::tr("Import Photoshop Brushes"), QString(),
                                         QObject::tr("Photoshop Brushes (*.abr)"), nullptr,
                                         QStringLiteral("brushTipManagerImportFileDialog"));
    if (path.isEmpty()) {
      return;
    }
    const auto before = library.entries().size();
    QString error;
    QStringList warnings;
    const auto first_id = library.import_abr(path, error, warnings);
    if (first_id.isEmpty()) {
      QMessageBox::warning(&dialog, QObject::tr("Import Brushes"), error);
      return;
    }
    tree->remember_collapsed_folders();
    tree->reload(first_id);
    refresh_details();
    if (!warnings.isEmpty()) {
      const auto imported = static_cast<int>(library.entries().size() - before);
      QMessageBox message(QMessageBox::Information, QObject::tr("Import Brushes"),
                          abr_import_summary(imported, warnings), QMessageBox::Ok, &dialog);
      message.setDetailedText(warnings.join(QStringLiteral("\n")));
      message.exec();
    }
  });
  QObject::connect(define_button, &QPushButton::clicked, &dialog, [&] {
    if (!capture_define_source) {
      return;
    }
    const auto mask = capture_define_source();
    if (mask.isNull()) {
      QMessageBox::information(&dialog, QObject::tr("Define Brush Tip"),
                               QObject::tr("There is no image content to define a brush from."));
      return;
    }
    bool accepted = false;
    const auto name = QInputDialog::getText(&dialog, QObject::tr("Define Brush Tip"), QObject::tr("Name:"),
                                            QLineEdit::Normal,
                                            QObject::tr("Brush %1").arg(library.entries().size() + 1), &accepted);
    if (!accepted || name.trimmed().isEmpty()) {
      return;
    }
    const auto id = library.add_tip(name.trimmed(), mask, 0.25);
    if (id.isEmpty()) {
      QMessageBox::warning(&dialog, QObject::tr("Define Brush Tip"),
                           QObject::tr("The selection is empty or too large to use as a brush tip."));
      return;
    }
    tree->remember_collapsed_folders();
    tree->reload(id);
    refresh_details();
  });
  QObject::connect(restore_button, &QPushButton::clicked, &dialog, [&] {
    const auto restored = library.restore_default_tips();
    // Also un-mess customized defaults: spacing, tip shape, and dynamics go back to factory.
    const auto reset = library.reset_default_tips_to_factory();
    if (restored > 0 || reset > 0) {
      tree->remember_collapsed_folders();
      const auto selected = tree->selected_ids();
      tree->reload(selected.isEmpty() ? QString() : selected.front());
      refresh_details();
      QStringList parts;
      if (restored > 0) {
        parts << QObject::tr("Restored %n default brush tip(s).", nullptr, restored);
      }
      if (reset > 0) {
        parts << QObject::tr("Reset %n default brush tip(s) to factory settings.", nullptr, reset);
      }
      QMessageBox::information(&dialog, QObject::tr("Restore Default Brushes"),
                               parts.join(QLatin1Char('\n')));
    } else {
      QMessageBox::information(&dialog, QObject::tr("Restore Default Brushes"),
                               QObject::tr("All default brush tips are already present with factory settings."));
    }
  });
  QObject::connect(duplicate_button, &QPushButton::clicked, &dialog, [&] {
    const auto ids = tree->selected_ids();
    const auto* entry = ids.size() == 1 ? library.find_entry(ids.front()) : nullptr;
    if (entry == nullptr) {
      return;
    }
    const auto tip = library.tip(entry->id);
    if (tip == nullptr) {
      return;
    }
    const auto new_id = library.add_tip(QObject::tr("%1 Copy").arg(entry->name),
                                        coverage_image_from_brush_tip(*tip), entry->spacing, entry->folder);
    if (!new_id.isEmpty()) {
      tree->remember_collapsed_folders();
      tree->reload(new_id);
      refresh_details();
    }
  });

  define_button->setEnabled(static_cast<bool>(capture_define_source));
  tree->reload(initial_tip_id);
  refresh_details();
  // Applied after every child exists; unprefixed sub-control selectors (see dialog_utils note).
  append_themed_style(dialog, dialog_spinbox_button_style());
  dialog.resize(820, 520);
  exec_dialog(dialog);
}

}  // namespace patchy::ui
