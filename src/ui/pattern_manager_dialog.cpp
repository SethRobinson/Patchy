#include "ui/pattern_manager_dialog.hpp"

#include "ui/pattern_library.hpp"
#include "ui/preset_manager_scaffold.hpp"
#include "ui/preset_tree_widget.hpp"

#include <QDialog>
#include <QFileDialog>
#include <QFormLayout>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <utility>

namespace patchy::ui {

namespace {

// Tiles the pattern centered on the widget, outlining the tile under the viewport
// center exactly like the Seamless Tile Preview so wrap boundaries stay findable.
// Any mouse button drags to pan (the tiling wraps, so no pan can scroll off), the
// wheel zooms about the cursor (nearest-neighbor when magnifying, smooth when
// shrinking photo textures), and double-click resets the view. Zoom and pan persist
// across selection changes within one run.
class PatternPreview final : public QWidget {
public:
  explicit PatternPreview(QWidget* parent = nullptr) : QWidget(parent) {
    setObjectName(QStringLiteral("patternManagerPreview"));
    setMinimumSize(300, 180);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setCursor(Qt::OpenHandCursor);
    setToolTip(QObject::tr("Drag to pan. Mouse wheel zooms. Double-click resets the view."));
    publish_zoom();
    set_pan(QPoint());
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
      // One tile's origin sits at the centered position offset by the pan; the wrap
      // start (first origin at or left of/above the viewport) aligns the grid to it.
      const QPoint origin((width() - draw_w) / 2 + pan_.x(), (height() - draw_h) / 2 + pan_.y());
      const auto wrap_start = [](int anchor, int step) {
        const int rem = anchor % step;
        return rem > 0 ? rem - step : rem;  // in (-step, 0]
      };
      const int start_x = wrap_start(origin.x(), draw_w);
      const int start_y = wrap_start(origin.y(), draw_h);
      painter.drawTiledPixmap(rect(), scaled, QPoint(-start_x, -start_y));
      // Same faint outline the Seamless Tile Preview draws: the tile under the viewport
      // center, so the wrap boundaries stay findable wherever the view is panned.
      const int outline_x = start_x + ((width() / 2 - start_x) / draw_w) * draw_w;
      const int outline_y = start_y + ((height() / 2 - start_y) / draw_h) * draw_h;
      painter.setPen(QColor(255, 255, 255, 60));
      painter.drawRect(QRect(outline_x, outline_y, draw_w - 1, draw_h - 1));
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
    if (next != zoom_) {
      zoom_about(event->position(), next);
    }
    event->accept();
  }

  void mousePressEvent(QMouseEvent* event) override {
    // Any button pans; the widget has no context menu, so right-drag is free.
    const auto button = event->button();
    if (!dragging_ && button != Qt::NoButton) {
      dragging_ = true;
      drag_button_ = button;
      drag_position_ = event->pos();
      setCursor(Qt::ClosedHandCursor);
    }
  }

  void mouseMoveEvent(QMouseEvent* event) override {
    if (dragging_) {
      set_pan(pan_ + (event->pos() - drag_position_));
      drag_position_ = event->pos();
    }
  }

  void mouseReleaseEvent(QMouseEvent* event) override {
    if (dragging_ && event->button() == drag_button_) {
      dragging_ = false;
      setCursor(Qt::OpenHandCursor);
    }
  }

  void mouseDoubleClickEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton) {
      set_pan(QPoint());
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

  void set_pan(QPoint pan) {
    pan_ = pan;
    setProperty("previewPanOffset", pan_);  // test-visible state
    update();
  }

  void publish_zoom() {
    setProperty("previewZoomPercent", zoom_percent());  // test-visible state
  }

  [[nodiscard]] static int draw_extent(int extent, double zoom) {
    return std::max(1, static_cast<int>(std::lround(extent * zoom)));
  }

  // Re-derives the pan so the tile-space point under pos stays stationary through the
  // zoom (same tile-fraction anchoring as the Seamless Tile Preview).
  void zoom_about(QPointF pos, double next_zoom) {
    const auto old_draw_w = draw_extent(tile_.width(), zoom_);
    const auto old_draw_h = draw_extent(tile_.height(), zoom_);
    const auto new_draw_w = draw_extent(tile_.width(), next_zoom);
    const auto new_draw_h = draw_extent(tile_.height(), next_zoom);
    const auto tile_x = (pos.x() - ((width() - old_draw_w) / 2 + pan_.x())) / old_draw_w;
    const auto tile_y = (pos.y() - ((height() - old_draw_h) / 2 + pan_.y())) / old_draw_h;
    set_pan(QPoint(
        static_cast<int>(std::lround(pos.x() - tile_x * new_draw_w - (width() - new_draw_w) / 2)),
        static_cast<int>(std::lround(pos.y() - tile_y * new_draw_h - (height() - new_draw_h) / 2))));
    set_zoom(next_zoom);
  }

  // Scaling once per zoom/tile change keeps paint at one drawTiledPixmap call even when
  // zoomed far out (a per-tile loop would explode for tiny tiles).
  [[nodiscard]] const QPixmap& scaled_tile() {
    const auto draw_w = draw_extent(tile_.width(), zoom_);
    const auto draw_h = draw_extent(tile_.height(), zoom_);
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
  QPoint pan_;
  QPoint drag_position_;
  bool dragging_{false};
  Qt::MouseButton drag_button_{Qt::NoButton};
};

}  // namespace

QString request_pattern_manager(QWidget* parent, PatternLibrary& library,
                                const QString& initial_pattern_id,
                                std::function<void(const QString& name, const PixelBuffer& tile)> open_as_image) {
  QDialog dialog(parent);
  PresetManagerScaffold scaffold(dialog, QStringLiteral("patternManagerDialog"),
                                 QObject::tr("Patterns"));

  auto* tree = new PresetTreeWidget(&dialog);
  tree->setObjectName(QStringLiteral("patternManagerTree"));
  tree->setIconSize(QSize(40, 40));
  tree->set_entry_row_height(46);
  tree->set_reload_fallback(PresetTreeWidget::ReloadFallback::first_entry_expanding);
  tree->set_folder_label_callback([](const QString& folder, int count) {
    return QObject::tr("%1 (%2)").arg(folder).arg(count);
  });
  tree->set_entries_callback([&library] {
    std::vector<PresetTreeEntry> rows;
    for (const auto& entry : library.entries()) {
      rows.push_back({entry.storage_id, pattern_library_entry_display_name(entry),
                      QIcon(entry.thumbnail),
                      QObject::tr("%1 (%2×%3)")
                          .arg(pattern_library_entry_display_name(entry))
                          .arg(entry.size.width())
                          .arg(entry.size.height()),
                      entry.folder});
    }
    return rows;
  });
  scaffold.add_tree(tree, 300);

  auto* preview = new PatternPreview(&dialog);
  scaffold.right()->addWidget(preview, 1);

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
  scaffold.right()->addLayout(form);

  auto* import_button = new QPushButton(QObject::tr("Import…"), &dialog);
  import_button->setObjectName(QStringLiteral("patternManagerImportButton"));
  import_button->setToolTip(QObject::tr("Import Photoshop .pat pattern files or images"));
  auto* open_image_button = new QPushButton(QObject::tr("Open as Image"), &dialog);
  open_image_button->setObjectName(QStringLiteral("patternManagerOpenImageButton"));
  open_image_button->setToolTip(QObject::tr("Open the selected pattern's texture as a new image"));
  open_image_button->setVisible(open_as_image != nullptr);
  auto* duplicate_button = new QPushButton(QObject::tr("Duplicate"), &dialog);
  duplicate_button->setObjectName(QStringLiteral("patternManagerDuplicateButton"));
  auto* delete_button = new QPushButton(QObject::tr("Delete"), &dialog);
  delete_button->setObjectName(QStringLiteral("patternManagerDeleteButton"));
  delete_button->setToolTip(QObject::tr("Delete the selected patterns or folders (Del)"));
  scaffold.add_action_row({import_button, open_image_button}, {duplicate_button, delete_button});

  auto* restore_button = new QPushButton(QObject::tr("Restore Default Patterns"), &dialog);
  restore_button->setObjectName(QStringLiteral("patternManagerRestoreButton"));
  restore_button->setToolTip(QObject::tr("Bring back deleted built-in patterns and reset changed defaults"));
  scaffold.add_restore_button_left(restore_button);

  QSet<QString> requested_open_ids;  // one queued document per pattern per dialog run
  QString selected_storage_id;
  const auto use_selected = scaffold.single_selection_accept(
      [tree] { return tree->selected_ids(); },
      [&library](const QString& id) { return library.find_entry(id) != nullptr; },
      selected_storage_id);
  auto* use_button = scaffold.add_dialog_buttons(
      QObject::tr("Use Pattern"), QStringLiteral("patternManagerUseButton"), use_selected);

  const auto show_update_failure = [&] {
    QMessageBox::warning(
        &dialog, QObject::tr("Patterns"),
        QObject::tr("Could not update the selected pattern. Check that the pattern library folder is writable."));
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

  const auto delete_selected = [&] {
    const auto ids = tree->selected_ids();
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
    tree->remember_collapsed_folders();
    const auto removed = library.remove_patterns(ids);
    tree->reload({});
    refresh_details();
    const auto failed = static_cast<int>(ids.size()) - removed;
    if (failed > 0) {
      QMessageBox::warning(
          &dialog, QObject::tr("Delete Patterns"),
          QObject::tr("Could not delete %n pattern(s). Check that the pattern library folder is writable.",
                      nullptr, failed));
    }
  };

  scaffold.connect_selection_changed(refresh_details);
  tree->set_entry_double_clicked_callback([use_selected](const QString&) { use_selected(); });
  scaffold.add_delete_plumbing(delete_button, delete_selected);

  QObject::connect(open_image_button, &QPushButton::clicked, &dialog, [&] {
    const auto ids = tree->selected_ids();
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
    const auto ids = tree->selected_ids();
    if (ids.size() != 1 || name_edit->text().trimmed().isEmpty()) {
      return;
    }
    if (library.rename_pattern(ids.front(), name_edit->text())) {
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
    auto moved = false;
    auto failed = false;
    for (const auto& id : ids) {
      const auto updated = library.set_pattern_folder(id, folder_edit->text());
      moved = updated || moved;
      failed = !updated || failed;
    }
    if (moved) {
      tree->remember_collapsed_folders();
      tree->reload(ids.front());
      refresh_details();
    }
    if (failed) {
      show_update_failure();
    }
  });
  QObject::connect(import_button, &QPushButton::clicked, &dialog, [&] {
    QStringList image_globs;
    for (const auto& format : QImageReader::supportedImageFormats()) {
      const auto extension = QString::fromLatin1(format);
      image_globs.append(QStringLiteral("*.") + extension.toLower());
      image_globs.append(QStringLiteral("*.") + extension.toUpper());
    }
    image_globs.removeDuplicates();
    const auto filter =
        QStringLiteral("%1 (*.pat %2);;%3 (*.pat);;%4 (%2);;%5")
            .arg(QObject::tr("Patterns and Images"), image_globs.join(QLatin1Char(' ')),
                 QObject::tr("Photoshop Patterns"), QObject::tr("Images"),
                 QObject::tr("All Files (*.*)"));
    const auto paths =
        QFileDialog::getOpenFileNames(&dialog, QObject::tr("Import Patterns"), {}, filter);
    if (paths.isEmpty()) {
      return;
    }
    const auto before = library.entries().size();
    QStringList problems;
    QString first_storage_id;
    for (const auto& path : paths) {
      QString error;
      QStringList warnings;
      const auto is_pat =
          QFileInfo(path).suffix().compare(QStringLiteral("pat"), Qt::CaseInsensitive) == 0;
      const auto imported = is_pat ? library.import_pat(path, error, warnings)
                                   : library.import_image(path, error, warnings);
      if (imported.isEmpty()) {
        problems.append(error);
      } else if (first_storage_id.isEmpty()) {
        first_storage_id = imported;
      }
      problems.append(warnings);
    }
    if (first_storage_id.isEmpty()) {
      QMessageBox::warning(&dialog, QObject::tr("Import Patterns"),
                           problems.isEmpty() ? QObject::tr("No patterns could be imported.")
                                              : problems.join(QLatin1Char('\n')));
      return;
    }
    tree->remember_collapsed_folders();
    tree->reload(first_storage_id);
    refresh_details();
    if (!problems.isEmpty()) {
      const auto imported = static_cast<int>(library.entries().size() - before);
      QMessageBox message(QMessageBox::Information, QObject::tr("Import Patterns"),
                          QObject::tr("Imported %n pattern(s).", nullptr, imported), QMessageBox::Ok,
                          &dialog);
      message.setDetailedText(problems.join(QLatin1Char('\n')));
      message.exec();
    }
  });
  QObject::connect(duplicate_button, &QPushButton::clicked, &dialog, [&] {
    const auto ids = tree->selected_ids();
    if (ids.size() != 1) {
      return;
    }
    const auto duplicate = library.duplicate_pattern(ids.front());
    if (!duplicate.isEmpty()) {
      tree->remember_collapsed_folders();
      tree->reload(duplicate);
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
    tree->remember_collapsed_folders();
    tree->reload({});
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
  QString initial_storage_id;
  if (const auto* entry = library.find_entry_by_pattern_id(initial_pattern_id); entry != nullptr) {
    initial_storage_id = entry->storage_id;
  }
  tree->reload(initial_storage_id);
  refresh_details();
  dialog.resize(800, 520);
  dialog.exec();
  return selected_storage_id;
}

}  // namespace patchy::ui
