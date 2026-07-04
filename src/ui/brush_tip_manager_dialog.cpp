#include "ui/brush_tip_manager_dialog.hpp"

#include "core/brush_tip.hpp"
#include "core/document.hpp"
#include "core/pixel_tools.hpp"
#include "ui/brush_dynamics_popup.hpp"
#include "ui/brush_tip_library.hpp"
#include "ui/dialog_utils.hpp"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSet>
#include <QShortcut>
#include <QSpinBox>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>

namespace patchy::ui {

namespace {

constexpr int kFolderMarkerRole = Qt::UserRole + 1;

// Paints an S-curve stroke with the actual stamping engine into a scratch document, so the
// preview shows exactly what the canvas will produce (spacing, antialiasing, build-up cap off).
class BrushStrokePreview : public QWidget {
public:
  explicit BrushStrokePreview(QWidget* parent = nullptr) : QWidget(parent) {
    setObjectName(QStringLiteral("brushTipStrokePreview"));
    setMinimumSize(320, 130);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  }

  void set_tip(std::shared_ptr<const patchy::BrushTip> tip) {
    tip_ = std::move(tip);
    rebuild();
  }

  void set_spacing(double spacing) {
    spacing_ = std::clamp(spacing, 0.01, 10.0);
    rebuild();
  }

  void set_dynamics(const patchy::BrushDynamics& dynamics, double base_angle_degrees,
                    double base_roundness) {
    dynamics_ = dynamics;
    base_angle_degrees_ = base_angle_degrees;
    base_roundness_ = std::clamp(base_roundness, 1.0, 100.0);
    rebuild();
  }

protected:
  void paintEvent(QPaintEvent* /*event*/) override {
    QPainter painter(this);
    painter.fillRect(rect(), Qt::white);
    if (!stroke_.isNull()) {
      painter.drawImage(rect(), stroke_);
    }
    painter.setPen(QColor(0, 0, 0, 40));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
  }

  void resizeEvent(QResizeEvent* /*event*/) override { rebuild(); }

private:
  void rebuild() {
    stroke_ = {};
    if (tip_ == nullptr || tip_->empty() || width() <= 4 || height() <= 4) {
      update();
      return;
    }

    const auto document_width = std::max(64, width());
    const auto document_height = std::max(48, height());
    patchy::Document document(document_width, document_height, patchy::PixelFormat::rgba8());
    patchy::PixelBuffer pixels(document_width, document_height, patchy::PixelFormat::rgba8());
    pixels.clear(0);
    const auto layer_id = document.add_pixel_layer("Preview", std::move(pixels)).id();

    const auto mips = patchy::build_brush_tip_mips(*tip_);
    const auto brush_size =
        std::clamp(std::max(tip_->width, tip_->height), 8, std::max(8, document_height / 2));
    const auto scaled = patchy::make_scaled_brush_tip(mips, brush_size);
    if (scaled.empty()) {
      update();
      return;
    }

    patchy::EditOptions options;
    options.primary = patchy::EditColor{0, 0, 0, 255};
    options.brush_size = brush_size;
    options.brush_tip = &scaled;
    options.brush_tip_spacing = spacing_;
    options.brush_angle_degrees = base_angle_degrees_;
    options.brush_roundness = static_cast<int>(std::lround(base_roundness_));
    options.brush_dynamics = dynamics_;
    options.brush_dynamics.seed = 1234;  // fixed seed: a stable preview instead of reshuffling per repaint

    // Sample a gentle S-curve across the preview, chopped into short segments like real input.
    patchy::BrushTipStrokeState state;
    const auto margin = static_cast<double>(brush_size) / 2.0 + 4.0;
    const auto usable_width = static_cast<double>(document_width) - 2.0 * margin;
    const auto center_y = static_cast<double>(document_height) / 2.0;
    const auto wave_height = std::max(4.0, static_cast<double>(document_height) / 2.0 - margin);
    constexpr int kSegments = 48;
    double previous_x = margin;
    double previous_y = center_y;
    for (int step = 1; step <= kSegments; ++step) {
      const auto t = static_cast<double>(step) / kSegments;
      const auto x = margin + usable_width * t;
      const auto y = center_y - std::sin(t * 2.0 * 3.14159265358979323846) * wave_height;
      (void)patchy::paint_brush_segment(document, layer_id, previous_x, previous_y, x, y, options, false, state);
      previous_x = x;
      previous_y = y;
    }

    const auto* layer = document.find_layer(layer_id);
    if (layer == nullptr) {
      update();
      return;
    }
    const auto& painted = layer->pixels();
    QImage image(painted.width(), painted.height(), QImage::Format_RGBA8888);
    for (std::int32_t y = 0; y < painted.height(); ++y) {
      const auto row = painted.row(y);
      std::copy_n(row.data(), static_cast<std::size_t>(painted.width()) * 4U, image.scanLine(y));
    }
    stroke_ = std::move(image);
    update();
  }

  std::shared_ptr<const patchy::BrushTip> tip_;
  double spacing_{0.25};
  patchy::BrushDynamics dynamics_{};
  double base_angle_degrees_{0.0};
  double base_roundness_{100.0};
  QImage stroke_;
};

[[nodiscard]] QString tree_item_tip_id(const QTreeWidgetItem* item) {
  return item != nullptr && !item->data(0, kFolderMarkerRole).toBool()
             ? item->data(0, Qt::UserRole).toString()
             : QString();
}

}  // namespace

void request_brush_tip_manager(QWidget* parent, BrushTipLibrary& library, const QString& initial_tip_id,
                               const std::function<QImage()>& capture_define_source,
                               const std::function<void(const QString&)>& activate_tip) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("brushTipManagerDialog"));
  dialog.setWindowTitle(QObject::tr("Brush Tips"));
  dialog.setModal(true);

  auto* main_layout = new QHBoxLayout(&dialog);

  auto* tree = new QTreeWidget(&dialog);
  tree->setObjectName(QStringLiteral("brushTipManagerTree"));
  tree->setHeaderHidden(true);
  tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
  tree->setIconSize(QSize(40, 40));
  tree->setIndentation(14);
  // No uniform row heights: Qt would size every row from the first one (a short folder row),
  // squashing the 40px thumbnails into overlapping slivers.
  tree->setMinimumWidth(320);
  main_layout->addWidget(tree, 1);

  auto* right = new QVBoxLayout();
  main_layout->addLayout(right, 2);

  auto* preview = new BrushStrokePreview(&dialog);
  right->addWidget(preview);

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
  spacing_spin->setSuffix(QStringLiteral("%"));
  spacing_spin->setValue(25);
  spacing_spin->setToolTip(QObject::tr("Distance between stamps as a percentage of the brush size"));
  spacing_spin->setMinimumWidth(120);
  form->addRow(QObject::tr("Spacing:"), spacing_spin);
  auto* dynamics_button = new QPushButton(QObject::tr("Edit Dynamics…"), &dialog);
  dynamics_button->setObjectName(QStringLiteral("brushTipManagerDynamicsButton"));
  dynamics_button->setToolTip(
      QObject::tr("Tip shape, shape dynamics, scattering, and opacity jitter for the selected brush tip"));
  form->addRow(QObject::tr("Dynamics:"), dynamics_button);
  auto* size_label = new QLabel(&dialog);
  size_label->setObjectName(QStringLiteral("brushTipSizeLabel"));
  form->addRow(QObject::tr("Size:"), size_label);
  right->addLayout(form);

  auto* action_row = new QHBoxLayout();
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
  action_row->addWidget(import_button);
  action_row->addWidget(define_button);
  action_row->addStretch(1);
  action_row->addWidget(duplicate_button);
  action_row->addWidget(delete_button);
  right->addLayout(action_row);
  auto* restore_row = new QHBoxLayout();
  auto* restore_button = new QPushButton(QObject::tr("Restore Default Brushes"), &dialog);
  restore_button->setObjectName(QStringLiteral("brushTipManagerRestoreButton"));
  restore_button->setToolTip(QObject::tr("Bring back any deleted built-in brush tips"));
  restore_row->addWidget(restore_button);
  restore_row->addStretch(1);
  right->addLayout(restore_row);
  right->addStretch(1);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
  auto* use_button = buttons->addButton(QObject::tr("Use Brush"), QDialogButtonBox::AcceptRole);
  use_button->setObjectName(QStringLiteral("brushTipManagerUseButton"));
  right->addWidget(buttons);

  QSet<QString> collapsed_folders;

  // Every selected tip id; a selected folder row stands for all of its children.
  const auto collect_selected_tip_ids = [&]() -> QStringList {
    QStringList ids;
    const auto add_unique = [&ids](const QString& id) {
      if (!id.isEmpty() && !ids.contains(id)) {
        ids.append(id);
      }
    };
    for (const auto* item : tree->selectedItems()) {
      if (item->data(0, kFolderMarkerRole).toBool()) {
        for (int child = 0; child < item->childCount(); ++child) {
          add_unique(tree_item_tip_id(item->child(child)));
        }
      } else {
        add_unique(tree_item_tip_id(item));
      }
    }
    return ids;
  };

  const auto reload_tree = [&](const QString& select_id) {
    const QSignalBlocker blocker(tree);
    tree->clear();
    QTreeWidgetItem* select_item = nullptr;
    std::map<QString, QTreeWidgetItem*> folder_items;
    for (const auto& entry : library.entries()) {
      QTreeWidgetItem* parent_item = nullptr;
      if (!entry.folder.isEmpty()) {
        auto found = folder_items.find(entry.folder);
        if (found == folder_items.end()) {
          auto* folder_item = new QTreeWidgetItem(tree);
          folder_item->setData(0, kFolderMarkerRole, true);
          folder_item->setData(0, Qt::UserRole, entry.folder);
          auto font = folder_item->font(0);
          font.setBold(true);
          folder_item->setFont(0, font);
          found = folder_items.emplace(entry.folder, folder_item).first;
        }
        parent_item = found->second;
      }
      auto* item = parent_item != nullptr ? new QTreeWidgetItem(parent_item) : new QTreeWidgetItem(tree);
      item->setText(0, entry.name);
      item->setIcon(0, QIcon(brush_tip_thumbnail_with_badge(entry)));
      item->setSizeHint(0, QSize(0, 46));  // room for the 40px thumbnail
      item->setData(0, Qt::UserRole, entry.id);
      auto tooltip = QObject::tr("%1 (%2×%3)").arg(entry.name).arg(entry.size.width()).arg(entry.size.height());
      if (brush_tip_entry_has_dynamics(entry)) {
        tooltip += QObject::tr(" • dynamics");
      }
      item->setToolTip(0, tooltip);
      if (entry.id == select_id) {
        select_item = item;
      }
    }
    for (const auto& [folder, item] : folder_items) {
      item->setText(0, QObject::tr("%1 (%2)").arg(folder).arg(item->childCount()));
      item->setExpanded(!collapsed_folders.contains(folder));
    }
    if (select_item != nullptr) {
      if (select_item->parent() != nullptr) {
        select_item->parent()->setExpanded(true);
      }
      tree->setCurrentItem(select_item);
      tree->scrollToItem(select_item);
    } else if (tree->topLevelItemCount() > 0) {
      auto* first = tree->topLevelItem(0);
      if (first->data(0, kFolderMarkerRole).toBool() && first->childCount() > 0 && first->isExpanded()) {
        tree->setCurrentItem(first->child(0));
      } else {
        tree->setCurrentItem(first);
      }
    }
  };

  const auto refresh_details = [&] {
    const auto ids = collect_selected_tip_ids();
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

  const auto remember_collapse_state = [&] {
    collapsed_folders.clear();
    for (int index = 0; index < tree->topLevelItemCount(); ++index) {
      const auto* item = tree->topLevelItem(index);
      if (item->data(0, kFolderMarkerRole).toBool() && !item->isExpanded()) {
        collapsed_folders.insert(item->data(0, Qt::UserRole).toString());
      }
    }
  };

  const auto delete_selected = [&] {
    const auto ids = collect_selected_tip_ids();
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
    remember_collapse_state();
    library.remove_tips(ids);
    reload_tree(QString());
    refresh_details();
  };

  QObject::connect(tree, &QTreeWidget::itemSelectionChanged, &dialog, [&] { refresh_details(); });
  QObject::connect(tree, &QTreeWidget::itemDoubleClicked, &dialog, [&](QTreeWidgetItem* item, int) {
    const auto id = tree_item_tip_id(item);
    if (!id.isEmpty() && activate_tip) {
      activate_tip(id);
      dialog.accept();
    }
  });
  auto* delete_shortcut = new QShortcut(QKeySequence::Delete, tree);
  delete_shortcut->setContext(Qt::WidgetWithChildrenShortcut);
  QObject::connect(delete_shortcut, &QShortcut::activated, &dialog, [&] { delete_selected(); });
  QObject::connect(delete_button, &QPushButton::clicked, &dialog, [&] { delete_selected(); });

  QObject::connect(name_edit, &QLineEdit::editingFinished, &dialog, [&] {
    const auto ids = collect_selected_tip_ids();
    if (ids.size() != 1 || name_edit->text().trimmed().isEmpty()) {
      return;
    }
    if (library.find_entry(ids.front()) != nullptr && library.rename_tip(ids.front(), name_edit->text())) {
      remember_collapse_state();
      reload_tree(ids.front());
      refresh_details();
    }
  });
  QObject::connect(folder_edit, &QLineEdit::editingFinished, &dialog, [&] {
    const auto ids = collect_selected_tip_ids();
    if (ids.isEmpty()) {
      return;
    }
    const auto folder = folder_edit->text();
    auto moved = false;
    for (const auto& id : ids) {
      moved = library.set_tip_folder(id, folder) || moved;
    }
    if (moved) {
      remember_collapse_state();
      reload_tree(ids.front());
      refresh_details();
    }
  });
  QObject::connect(spacing_spin, &QSpinBox::valueChanged, &dialog, [&](int value) {
    const auto ids = collect_selected_tip_ids();
    for (const auto& id : ids) {
      library.set_tip_spacing(id, static_cast<double>(value) / 100.0);
    }
    if (!ids.isEmpty()) {
      preview->set_spacing(static_cast<double>(value) / 100.0);
    }
  });
  QObject::connect(dynamics_button, &QPushButton::clicked, &dialog, [&] {
    const auto ids = collect_selected_tip_ids();
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
    editor.exec();
    if (apply_timer->isActive()) {
      apply_timer->stop();
      apply();  // flush an edit still inside the debounce window
    }
    remember_collapse_state();
    reload_tree(tip_id);  // refresh the dynamics badge
    refresh_details();
  });
  QObject::connect(import_button, &QPushButton::clicked, &dialog, [&] {
    const auto path = QFileDialog::getOpenFileName(&dialog, QObject::tr("Import Photoshop Brushes"), QString(),
                                                   QObject::tr("Photoshop Brushes (*.abr)"));
    if (path.isEmpty()) {
      return;
    }
    QString error;
    QStringList warnings;
    const auto first_id = library.import_abr(path, error, warnings);
    if (first_id.isEmpty()) {
      QMessageBox::warning(&dialog, QObject::tr("Import Brushes"), error);
      return;
    }
    remember_collapse_state();
    reload_tree(first_id);
    refresh_details();
    if (!warnings.isEmpty()) {
      QMessageBox message(QMessageBox::Information, QObject::tr("Import Brushes"),
                          QObject::tr("Imported brush tips with %n warning(s).", nullptr,
                                      static_cast<int>(warnings.size())),
                          QMessageBox::Ok, &dialog);
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
    remember_collapse_state();
    reload_tree(id);
    refresh_details();
  });
  QObject::connect(restore_button, &QPushButton::clicked, &dialog, [&] {
    const auto restored = library.restore_default_tips();
    if (restored > 0) {
      remember_collapse_state();
      const auto selected = collect_selected_tip_ids();
      reload_tree(selected.isEmpty() ? QString() : selected.front());
      refresh_details();
      QMessageBox::information(&dialog, QObject::tr("Restore Default Brushes"),
                               QObject::tr("Restored %n default brush tip(s).", nullptr, restored));
    } else {
      QMessageBox::information(&dialog, QObject::tr("Restore Default Brushes"),
                               QObject::tr("All default brush tips are already present."));
    }
  });
  QObject::connect(duplicate_button, &QPushButton::clicked, &dialog, [&] {
    const auto ids = collect_selected_tip_ids();
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
      remember_collapse_state();
      reload_tree(new_id);
      refresh_details();
    }
  });
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
    const auto ids = collect_selected_tip_ids();
    if (ids.size() == 1 && activate_tip) {
      activate_tip(ids.front());
    }
    dialog.accept();
  });

  define_button->setEnabled(static_cast<bool>(capture_define_source));
  reload_tree(initial_tip_id);
  refresh_details();
  // Applied after every child exists; unprefixed sub-control selectors (see dialog_utils note).
  dialog.setStyleSheet(dialog.styleSheet() + dialog_spinbox_button_style());
  dialog.resize(820, 520);
  dialog.exec();
}

}  // namespace patchy::ui
