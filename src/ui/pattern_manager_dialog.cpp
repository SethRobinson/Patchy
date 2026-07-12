#include "ui/pattern_manager_dialog.hpp"

#include "ui/pattern_library.hpp"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSet>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace patchy::ui {

namespace {

constexpr int kFolderMarkerRole = Qt::UserRole + 1;

// Tiles the pattern centered on the widget, with the same faint outline around the
// center tile the Seamless Tile Preview uses so wrap boundaries are visible. The mouse
// wheel zooms (nearest-neighbor when magnifying, smooth when shrinking photo textures);
// double-click resets to 100%. Zoom persists across selection changes within one run.
class PatternPreview final : public QWidget {
public:
  explicit PatternPreview(QWidget* parent = nullptr) : QWidget(parent) {
    setObjectName(QStringLiteral("patternManagerPreview"));
    setMinimumSize(300, 180);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setToolTip(QObject::tr("Mouse wheel zooms. Double-click resets zoom."));
    publish_zoom();
  }

  void set_pattern(const PatternResource* pattern) {
    tile_ = {};
    scaled_ = {};
    if (pattern != nullptr && !pattern->tile.empty() &&
        pattern->tile.format() == PixelFormat::rgba8()) {
      const auto& tile = pattern->tile;
      QImage image(tile.width(), tile.height(), QImage::Format_RGBA8888);
      for (std::int32_t y = 0; y < tile.height(); ++y) {
        std::copy_n(tile.pixel(0, y), static_cast<std::size_t>(tile.width()) * 4U,
                    image.scanLine(y));
      }
      tile_ = QPixmap::fromImage(std::move(image));
    }
    // A zoom carried over from the previous selection may exceed the new tile's cap.
    if (zoom_ != 1.0) {
      set_zoom(std::clamp(zoom_, kMinZoom, max_zoom_for_tile()));
    }
    update();
  }

protected:
  void paintEvent(QPaintEvent* /*event*/) override {
    QPainter painter(this);
    constexpr int kChecker = 10;
    for (int y = 0; y < height(); y += kChecker) {
      for (int x = 0; x < width(); x += kChecker) {
        const auto dark = ((x / kChecker) + (y / kChecker)) % 2 != 0;
        painter.fillRect(QRect(x, y, kChecker, kChecker),
                         dark ? QColor(160, 160, 160) : QColor(215, 215, 215));
      }
    }
    if (!tile_.isNull()) {
      const auto& scaled = scaled_tile();
      const auto draw_w = scaled.width();
      const auto draw_h = scaled.height();
      // Center one tile on the widget; the wrap phase aligns the rest of the grid to it.
      const QPoint center_origin((width() - draw_w) / 2, (height() - draw_h) / 2);
      const auto phase = [](int origin, int step) {
        const int rem = (-origin) % step;
        return rem < 0 ? rem + step : rem;
      };
      painter.drawTiledPixmap(rect(), scaled,
                              QPoint(phase(center_origin.x(), draw_w), phase(center_origin.y(), draw_h)));
      // Same faint outline the Seamless Tile Preview draws around its center tile.
      painter.setPen(QColor(255, 255, 255, 60));
      painter.drawRect(QRect(center_origin, QSize(draw_w - 1, draw_h - 1)));
      if (const auto percent = zoom_percent(); percent != 100) {
        const auto text = QStringLiteral("%1%").arg(percent);
        QRect badge(0, 0, painter.fontMetrics().horizontalAdvance(text) + 10,
                    painter.fontMetrics().height() + 4);
        badge.moveBottomRight(QPoint(width() - 6, height() - 6));
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(20, 22, 26, 190));
        painter.drawRoundedRect(badge, 3, 3);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QColor(230, 233, 238));
        painter.drawText(badge, Qt::AlignCenter, text);
      }
    }
    painter.setPen(QColor(0, 0, 0, 80));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
  }

  void wheelEvent(QWheelEvent* event) override {
    const auto delta = event->angleDelta().y();
    if (delta == 0 || tile_.isNull()) {
      event->ignore();
      return;
    }
    auto next = std::clamp(delta > 0 ? zoom_ * 1.25 : zoom_ / 1.25, kMinZoom, max_zoom_for_tile());
    // The per-tile cap can sit below the current zoom (true 100% is always allowed);
    // never let a zoom-in step move backwards through it.
    if (delta > 0 ? next < zoom_ : next > zoom_) {
      next = zoom_;
    }
    set_zoom(next);
    event->accept();
  }

  void mouseDoubleClickEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton) {
      set_zoom(1.0);
      event->accept();
      return;
    }
    QWidget::mouseDoubleClickEvent(event);
  }

private:
  static constexpr double kMinZoom = 1.0 / 16.0;
  static constexpr double kMaxZoom = 16.0;
  // Bounds the cached scaled pixmap: a 1024px photo texture at 16x would otherwise
  // allocate a gigabyte. Exactly 100% bypasses the cap (scaled_tile reuses tile_
  // without allocating), so huge tiles still preview at their real size.
  static constexpr int kMaxScaledDimension = 4096;

  [[nodiscard]] double max_zoom_for_tile() const {
    const auto largest = std::max(tile_.width(), tile_.height());
    return largest > 0
               ? std::max(kMinZoom, std::min(kMaxZoom, static_cast<double>(kMaxScaledDimension) / largest))
               : kMaxZoom;
  }

  [[nodiscard]] int zoom_percent() const {
    return static_cast<int>(std::lround(zoom_ * 100.0));
  }

  void set_zoom(double zoom) {
    zoom_ = zoom;
    scaled_ = {};
    publish_zoom();
    update();
  }

  void publish_zoom() {
    setProperty("previewZoomPercent", zoom_percent());  // test-visible state
  }

  // Scaling once per zoom/tile change keeps paint at one drawTiledPixmap call even when
  // zoomed far out (a per-tile loop would explode for tiny tiles).
  [[nodiscard]] const QPixmap& scaled_tile() {
    const auto draw_w = std::max(1, static_cast<int>(std::lround(tile_.width() * zoom_)));
    const auto draw_h = std::max(1, static_cast<int>(std::lround(tile_.height() * zoom_)));
    if (scaled_.isNull() || scaled_.width() != draw_w || scaled_.height() != draw_h) {
      scaled_ = draw_w == tile_.width() && draw_h == tile_.height()
                    ? tile_
                    : tile_.scaled(draw_w, draw_h, Qt::IgnoreAspectRatio,
                                   zoom_ < 1.0 ? Qt::SmoothTransformation : Qt::FastTransformation);
    }
    return scaled_;
  }

  QPixmap tile_;
  QPixmap scaled_;
  double zoom_{1.0};
};

[[nodiscard]] QString tree_item_storage_id(const QTreeWidgetItem* item) {
  return item != nullptr && !item->data(0, kFolderMarkerRole).toBool()
             ? item->data(0, Qt::UserRole).toString()
             : QString();
}

}  // namespace

QString request_pattern_manager(QWidget* parent, PatternLibrary& library,
                                const QString& initial_pattern_id,
                                std::function<void(const QString& name, const PixelBuffer& tile)> open_as_image) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("patternManagerDialog"));
  dialog.setWindowTitle(QObject::tr("Patterns"));
  dialog.setModal(true);

  auto* main_layout = new QHBoxLayout(&dialog);
  auto* tree = new QTreeWidget(&dialog);
  tree->setObjectName(QStringLiteral("patternManagerTree"));
  tree->setHeaderHidden(true);
  tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
  tree->setIconSize(QSize(40, 40));
  tree->setIndentation(14);
  tree->setMinimumWidth(300);
  main_layout->addWidget(tree, 1);

  auto* right = new QVBoxLayout();
  main_layout->addLayout(right, 2);
  auto* preview = new PatternPreview(&dialog);
  right->addWidget(preview, 1);

  auto* form = new QFormLayout();
  auto* name_edit = new QLineEdit(&dialog);
  name_edit->setObjectName(QStringLiteral("patternManagerNameEdit"));
  form->addRow(QObject::tr("Name:"), name_edit);
  auto* folder_edit = new QLineEdit(&dialog);
  folder_edit->setObjectName(QStringLiteral("patternManagerFolderEdit"));
  folder_edit->setPlaceholderText(QObject::tr("No folder"));
  folder_edit->setToolTip(
      QObject::tr("Folder for the selected pattern(s); leave empty to remove them from folders"));
  form->addRow(QObject::tr("Folder:"), folder_edit);
  auto* size_label = new QLabel(&dialog);
  size_label->setObjectName(QStringLiteral("patternManagerSizeLabel"));
  form->addRow(QObject::tr("Size:"), size_label);
  right->addLayout(form);

  auto* action_row = new QHBoxLayout();
  auto* import_button = new QPushButton(QObject::tr("Import .pat…"), &dialog);
  import_button->setObjectName(QStringLiteral("patternManagerImportButton"));
  auto* open_image_button = new QPushButton(QObject::tr("Open as Image"), &dialog);
  open_image_button->setObjectName(QStringLiteral("patternManagerOpenImageButton"));
  open_image_button->setToolTip(QObject::tr("Open the selected pattern's texture as a new image"));
  open_image_button->setVisible(open_as_image != nullptr);
  auto* duplicate_button = new QPushButton(QObject::tr("Duplicate"), &dialog);
  duplicate_button->setObjectName(QStringLiteral("patternManagerDuplicateButton"));
  auto* delete_button = new QPushButton(QObject::tr("Delete"), &dialog);
  delete_button->setObjectName(QStringLiteral("patternManagerDeleteButton"));
  delete_button->setToolTip(QObject::tr("Delete the selected patterns or folders (Del)"));
  action_row->addWidget(import_button);
  action_row->addWidget(open_image_button);
  action_row->addStretch(1);
  action_row->addWidget(duplicate_button);
  action_row->addWidget(delete_button);
  right->addLayout(action_row);

  auto* restore_button = new QPushButton(QObject::tr("Restore Default Patterns"), &dialog);
  restore_button->setObjectName(QStringLiteral("patternManagerRestoreButton"));
  restore_button->setToolTip(QObject::tr("Bring back deleted built-in patterns and reset changed defaults"));
  right->addWidget(restore_button, 0, Qt::AlignLeft);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
  auto* use_button = buttons->addButton(QObject::tr("Use Pattern"), QDialogButtonBox::AcceptRole);
  use_button->setObjectName(QStringLiteral("patternManagerUseButton"));
  right->addWidget(buttons);

  QSet<QString> collapsed_folders;
  QSet<QString> requested_open_ids;  // one queued document per pattern per dialog run
  QString selected_storage_id;

  const auto show_update_failure = [&] {
    QMessageBox::warning(
        &dialog, QObject::tr("Patterns"),
        QObject::tr("Could not update the selected pattern. Check that the pattern library folder is writable."));
  };

  const auto collect_selected_storage_ids = [&]() {
    QStringList ids;
    const auto add_unique = [&ids](const QString& id) {
      if (!id.isEmpty() && !ids.contains(id)) {
        ids.append(id);
      }
    };
    for (const auto* item : tree->selectedItems()) {
      if (item->data(0, kFolderMarkerRole).toBool()) {
        for (int child = 0; child < item->childCount(); ++child) {
          add_unique(tree_item_storage_id(item->child(child)));
        }
      } else {
        add_unique(tree_item_storage_id(item));
      }
    }
    return ids;
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

  const auto reload_tree = [&](const QString& select_storage_id) {
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
      auto* item = parent_item != nullptr ? new QTreeWidgetItem(parent_item)
                                         : new QTreeWidgetItem(tree);
      item->setText(0, pattern_library_entry_display_name(entry));
      item->setIcon(0, QIcon(entry.thumbnail));
      item->setSizeHint(0, QSize(0, 46));
      item->setData(0, Qt::UserRole, entry.storage_id);
      item->setToolTip(0, QObject::tr("%1 (%2×%3)")
                              .arg(pattern_library_entry_display_name(entry))
                              .arg(entry.size.width())
                              .arg(entry.size.height()));
      if (entry.storage_id == select_storage_id) {
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
      if (first->data(0, kFolderMarkerRole).toBool() && first->childCount() > 0) {
        first->setExpanded(true);
        tree->setCurrentItem(first->child(0));
      } else {
        tree->setCurrentItem(first);
      }
    }
  };

  const auto refresh_details = [&] {
    const auto ids = collect_selected_storage_ids();
    const auto* entry = ids.size() == 1 ? library.find_entry(ids.front()) : nullptr;
    const auto single = entry != nullptr;
    const auto any = !ids.isEmpty();
    name_edit->setEnabled(single);
    folder_edit->setEnabled(any);
    duplicate_button->setEnabled(single);
    delete_button->setEnabled(any);
    use_button->setEnabled(single);
    open_image_button->setEnabled(single && open_as_image != nullptr &&
                                  !requested_open_ids.contains(ids.front()));
    if (!any) {
      name_edit->clear();
      folder_edit->clear();
      size_label->clear();
      preview->set_pattern(nullptr);
      return;
    }
    if (single) {
      {
        const QSignalBlocker blocker(name_edit);
        name_edit->setText(entry->name);
      }
      {
        const QSignalBlocker blocker(folder_edit);
        folder_edit->setText(entry->folder);
      }
      size_label->setText(QObject::tr("%1 × %2 px").arg(entry->size.width()).arg(entry->size.height()));
      const auto resource = library.resource(entry->id);
      preview->set_pattern(resource.has_value() ? &*resource : nullptr);
      return;
    }
    const auto* first = library.find_entry(ids.front());
    name_edit->clear();
    folder_edit->setText(first != nullptr ? first->folder : QString());
    size_label->setText(
        QObject::tr("%n pattern(s) selected", nullptr, static_cast<int>(ids.size())));
    const auto resource = first != nullptr ? library.resource(first->id) : std::nullopt;
    preview->set_pattern(resource.has_value() ? &*resource : nullptr);
  };

  const auto use_selected = [&] {
    const auto ids = collect_selected_storage_ids();
    const auto* entry = ids.size() == 1 ? library.find_entry(ids.front()) : nullptr;
    if (entry == nullptr) {
      return;
    }
    selected_storage_id = entry->storage_id;
    dialog.accept();
  };

  const auto delete_selected = [&] {
    const auto ids = collect_selected_storage_ids();
    if (ids.isEmpty()) {
      return;
    }
    const auto question = ids.size() == 1
                              ? QObject::tr("Delete pattern \"%1\"?")
                                    .arg(library.find_entry(ids.front()) != nullptr
                                             ? library.find_entry(ids.front())->name
                                             : ids.front())
                              : QObject::tr("Delete %n pattern(s)?", nullptr,
                                            static_cast<int>(ids.size()));
    if (QMessageBox::question(&dialog, QObject::tr("Delete Patterns"), question) !=
        QMessageBox::Yes) {
      return;
    }
    remember_collapse_state();
    const auto removed = library.remove_patterns(ids);
    reload_tree({});
    refresh_details();
    const auto failed = static_cast<int>(ids.size()) - removed;
    if (failed > 0) {
      QMessageBox::warning(
          &dialog, QObject::tr("Delete Patterns"),
          QObject::tr("Could not delete %n pattern(s). Check that the pattern library folder is writable.",
                      nullptr, failed));
    }
  };

  QObject::connect(tree, &QTreeWidget::itemSelectionChanged, &dialog, refresh_details);
  QObject::connect(tree, &QTreeWidget::itemDoubleClicked, &dialog,
                   [&](QTreeWidgetItem* item, int) {
                     if (!tree_item_storage_id(item).isEmpty()) {
                       use_selected();
                     }
                   });
  auto* delete_shortcut = new QShortcut(QKeySequence::Delete, tree);
  delete_shortcut->setContext(Qt::WidgetWithChildrenShortcut);
  QObject::connect(delete_shortcut, &QShortcut::activated, &dialog, delete_selected);
  QObject::connect(delete_button, &QPushButton::clicked, &dialog, delete_selected);

  QObject::connect(open_image_button, &QPushButton::clicked, &dialog, [&] {
    const auto ids = collect_selected_storage_ids();
    const auto* entry = ids.size() == 1 ? library.find_entry(ids.front()) : nullptr;
    if (entry == nullptr || open_as_image == nullptr ||
        requested_open_ids.contains(entry->storage_id)) {
      return;
    }
    // The exact row's pixels, not the Photoshop-id lookup: a same-id/different-pixel
    // duplicate elsewhere in the library must not hijack this row.
    const auto resource = library.resource_for_entry(entry->storage_id);
    if (!resource.has_value()) {
      QMessageBox::warning(&dialog, QObject::tr("Patterns"),
                           QObject::tr("Could not load the selected pattern's texture."));
      return;
    }
    requested_open_ids.insert(entry->storage_id);
    open_as_image(pattern_library_entry_display_name(*entry), resource->tile);
    refresh_details();  // disables the button for the queued pattern
  });

  QObject::connect(name_edit, &QLineEdit::editingFinished, &dialog, [&] {
    const auto ids = collect_selected_storage_ids();
    if (ids.size() != 1 || name_edit->text().trimmed().isEmpty()) {
      return;
    }
    if (library.rename_pattern(ids.front(), name_edit->text())) {
      remember_collapse_state();
      reload_tree(ids.front());
      refresh_details();
    } else {
      show_update_failure();
    }
  });
  QObject::connect(folder_edit, &QLineEdit::editingFinished, &dialog, [&] {
    const auto ids = collect_selected_storage_ids();
    if (ids.isEmpty()) {
      return;
    }
    auto moved = false;
    auto failed = false;
    for (const auto& id : ids) {
      const auto updated = library.set_pattern_folder(id, folder_edit->text());
      moved = updated || moved;
      failed = !updated || failed;
    }
    if (moved) {
      remember_collapse_state();
      reload_tree(ids.front());
      refresh_details();
    }
    if (failed) {
      show_update_failure();
    }
  });
  QObject::connect(import_button, &QPushButton::clicked, &dialog, [&] {
    const auto path = QFileDialog::getOpenFileName(&dialog, QObject::tr("Import Photoshop Patterns"),
                                                   {}, QObject::tr("Photoshop Patterns (*.pat)"));
    if (path.isEmpty()) {
      return;
    }
    const auto before = library.entries().size();
    QString error;
    QStringList warnings;
    const auto first = library.import_pat(path, error, warnings);
    if (first.isEmpty()) {
      QMessageBox::warning(&dialog, QObject::tr("Import Patterns"), error);
      return;
    }
    remember_collapse_state();
    reload_tree(first);
    refresh_details();
    if (!warnings.isEmpty()) {
      const auto imported = static_cast<int>(library.entries().size() - before);
      QMessageBox message(QMessageBox::Information, QObject::tr("Import Patterns"),
                          QObject::tr("Imported %n pattern(s).", nullptr, imported), QMessageBox::Ok,
                          &dialog);
      message.setDetailedText(warnings.join(QLatin1Char('\n')));
      message.exec();
    }
  });
  QObject::connect(duplicate_button, &QPushButton::clicked, &dialog, [&] {
    const auto ids = collect_selected_storage_ids();
    if (ids.size() != 1) {
      return;
    }
    const auto duplicate = library.duplicate_pattern(ids.front());
    if (!duplicate.isEmpty()) {
      remember_collapse_state();
      reload_tree(duplicate);
      refresh_details();
    } else {
      show_update_failure();
    }
  });
  QObject::connect(restore_button, &QPushButton::clicked, &dialog, [&] {
    const auto restored = library.restore_default_patterns();
    const auto reset = library.reset_default_patterns_to_factory();
    const auto complete = library.default_patterns_match_factory();
    if (restored == 0 && reset == 0) {
      if (complete) {
        QMessageBox::information(
            &dialog, QObject::tr("Restore Default Patterns"),
            QObject::tr("All default patterns are already present with factory settings."));
      } else {
        QMessageBox::warning(
            &dialog, QObject::tr("Restore Default Patterns"),
            QObject::tr("Some default patterns could not be restored. Check that the pattern library folder is writable."));
      }
      return;
    }
    remember_collapse_state();
    reload_tree({});
    refresh_details();
    QStringList parts;
    if (restored > 0) {
      parts << QObject::tr("Restored %n default pattern(s).", nullptr, restored);
    }
    if (reset > 0) {
      parts << QObject::tr("Reset %n default pattern(s) to factory settings.", nullptr, reset);
    }
    if (!complete) {
      parts << QObject::tr("Some default patterns could not be restored. Check that the pattern library folder is writable.");
      QMessageBox::warning(&dialog, QObject::tr("Restore Default Patterns"),
                           parts.join(QLatin1Char('\n')));
    } else {
      QMessageBox::information(&dialog, QObject::tr("Restore Default Patterns"),
                               parts.join(QLatin1Char('\n')));
    }
  });
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, use_selected);

  QString initial_storage_id;
  if (const auto* entry = library.find_entry_by_pattern_id(initial_pattern_id); entry != nullptr) {
    initial_storage_id = entry->storage_id;
  }
  reload_tree(initial_storage_id);
  refresh_details();
  dialog.resize(800, 520);
  dialog.exec();
  return selected_storage_id;
}

}  // namespace patchy::ui
