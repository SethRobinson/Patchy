#include "ui/assets_panel.hpp"

#include "ui/app_settings.hpp"
#include "ui/asset_library.hpp"
#include "ui/asset_thumbnails.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/layer_list_widget.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/theme_palette.hpp"
#include "ui/ui_text_size.hpp"

#include <QAction>
#include <QComboBox>
#include <QCoreApplication>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QSet>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace patchy::ui {

namespace {

constexpr int kThumbnailSide = kAssetThumbnailDisplaySide;
constexpr int kThumbnailTop = 4;
constexpr int kNameGap = 2;
constexpr int kGridCellWidth = 88;
constexpr int kGridCellHeight = 92;
constexpr int kAssetIdRole = Qt::UserRole;
// The category the panel last showed. Permanent settings key.
constexpr auto kCurrentCategoryKey = "assets/currentCategory";

// Paints one grid cell: the asset's picture over its name. The picture comes from the cache
// (a hash lookup); the first paint of an asset queues its load on the cache's worker and the
// cell is repainted when the picture arrives, so nothing is read or decoded here.
class AssetGridDelegate final : public QStyledItemDelegate {
public:
  AssetGridDelegate(AssetThumbnailCache* thumbnails, QObject* parent)
      : QStyledItemDelegate(parent), thumbnails_(thumbnails) {}

  void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
    painter->save();
    const bool selected = (option.state & QStyle::State_Selected) != 0;
    const bool hovered = (option.state & QStyle::State_MouseOver) != 0;
    if (selected || hovered) {
      painter->setRenderHint(QPainter::Antialiasing, true);
      painter->setPen(Qt::NoPen);
      painter->setBrush(selected ? theme().selection_soft_bg : theme().list_row_hover_bg);
      painter->drawRoundedRect(option.rect.adjusted(1, 1, -1, -1), 4.0, 4.0);
      painter->setRenderHint(QPainter::Antialiasing, false);
    }
    const QRect picture(option.rect.left() + (option.rect.width() - kThumbnailSide) / 2,
                        option.rect.top() + kThumbnailTop, kThumbnailSide, kThumbnailSide);
    const auto asset_id = index.data(kAssetIdRole).toString();
    thumbnails_->request(asset_id);
    if (const auto* image = thumbnails_->image(asset_id); image != nullptr) {
      const qreal ratio = option.widget != nullptr ? option.widget->devicePixelRatioF() : 1.0;
      QSizeF size = QSizeF(image->size()) / ratio;
      if (size.width() > picture.width() || size.height() > picture.height()) {
        size.scale(QSizeF(picture.size()), Qt::KeepAspectRatio);
      }
      const QRectF target(picture.center().x() + 0.5 - size.width() / 2.0,
                          picture.center().y() + 0.5 - size.height() / 2.0, size.width(), size.height());
      painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
      painter->drawImage(target, *image);
    } else {
      // No picture yet (or none to be had): a quiet frame in the list's own border role.
      painter->setPen(theme().list_surface_border);
      painter->setBrush(Qt::NoBrush);
      painter->drawRect(picture.adjusted(10, 10, -11, -11));
    }
    const QRect name_rect(option.rect.left() + 3, picture.bottom() + 1 + kNameGap, option.rect.width() - 6,
                          option.rect.bottom() - picture.bottom() - kNameGap);
    painter->setFont(option.font);
    painter->setPen(theme().text_primary);
    painter->drawText(name_rect, Qt::AlignHCenter | Qt::AlignTop,
                      QFontMetrics(option.font).elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight,
                                                           name_rect.width()));
    painter->restore();
  }

  QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override {
    return {kGridCellWidth - 4, kGridCellHeight - 4};
  }

private:
  AssetThumbnailCache* thumbnails_;
};

// The thumbnail grid. Dragging items out starts an asset drag, which the window's event
// filter takes over a canvas or a document tab (MainWindow::handle_asset_drag_event).
class AssetGridWidget final : public QListWidget {
public:
  explicit AssetGridWidget(QWidget* parent) : QListWidget(parent) {
    setObjectName(QStringLiteral("assetsGrid"));
    setViewMode(QListView::IconMode);
    setMovement(QListView::Static);
    setResizeMode(QListView::Adjust);
    setWrapping(true);
    setUniformItemSizes(true);
    setIconSize(QSize(kThumbnailSide, kThumbnailSide));
    setGridSize(QSize(kGridCellWidth, kGridCellHeight));
    setTextElideMode(Qt::ElideRight);
    setWordWrap(false);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragOnly);
    setDefaultDropAction(Qt::CopyAction);
    setContextMenuPolicy(Qt::CustomContextMenu);
  }

  std::function<void()> delete_pressed;
  std::function<void()> activate_pressed;

protected:
  QStringList mimeTypes() const override { return {QLatin1String(kAssetDragMimeType)}; }

  QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override {
    QStringList ids;
    for (const auto* item : items) {
      if (item != nullptr) {
        ids.push_back(item->data(kAssetIdRole).toString());
      }
    }
    if (ids.isEmpty()) {
      return nullptr;
    }
    auto* mime = new QMimeData();
    mime->setData(QLatin1String(kAssetDragMimeType), ids.join(QLatin1Char('\n')).toUtf8());
    mime->setData(QLatin1String(kAssetDragSourceMimeType), QByteArray::number(QCoreApplication::applicationPid()));
    return mime;
  }

  Qt::DropActions supportedDropActions() const override { return Qt::CopyAction; }

  void keyPressEvent(QKeyEvent* event) override {
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && delete_pressed) {
      delete_pressed();
      event->accept();
      return;
    }
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && activate_pressed) {
      activate_pressed();
      event->accept();
      return;
    }
    QListWidget::keyPressEvent(event);
  }
};

}  // namespace

QString AssetsPanel::category_display_name(const AssetCategory& category) {
  return category.name.isEmpty() ? tr("General") : category.name;
}

AssetsPanel::AssetsPanel(AssetLibrary* library, QWidget* parent) : QWidget(parent), library_(library) {
  setObjectName(QStringLiteral("assetsPanel"));
  setAcceptDrops(true);
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(6, 6, 6, 6);
  layout->setSpacing(6);

  const auto add_tool_button = [this](const char* object_name, const char* source, const char* tooltip) {
    auto* button = new QToolButton(this);
    button->setObjectName(QString::fromLatin1(object_name));
    bind_translated_text(button, source, "patchy::ui::AssetsPanel");
    bind_translated_tooltip(button, tooltip, "patchy::ui::AssetsPanel");
    apply_bound_translation(button);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
  };

  auto* top_row = new QHBoxLayout();
  top_row->setSpacing(4);
  category_combo_ = new QComboBox(this);
  category_combo_->setObjectName(QStringLiteral("assetsCategoryCombo"));
  category_combo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  category_combo_->setMinimumContentsLength(8);
  bind_translated_tooltip(category_combo_, QT_TR_NOOP("Category"), "patchy::ui::AssetsPanel");
  apply_bound_translation(category_combo_);
  connect(category_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (rebuilding_ || index < 0) {
      return;
    }
    current_category_id_ = category_combo_->itemData(index).toString();
    app_settings().setValue(QString::fromLatin1(kCurrentCategoryKey), current_category_id_);
    rebuild_grid();
  });
  top_row->addWidget(category_combo_, 1);
  manage_button_ = add_tool_button("assetsManageButton", QT_TR_NOOP("Manage"),
                                   QT_TR_NOOP("Add assets and manage categories"));
  manage_button_->setPopupMode(QToolButton::InstantPopup);
  auto* manage_menu = new QMenu(manage_button_);
  manage_menu->setObjectName(QStringLiteral("assetsManageMenu"));
  connect(manage_menu, &QMenu::aboutToShow, this, [this, manage_menu] {
    manage_menu->clear();
    fill_manage_menu(*manage_menu);
  });
  manage_button_->setMenu(manage_menu);
  top_row->addWidget(manage_button_);
  layout->addLayout(top_row);

  search_edit_ = new QLineEdit(this);
  search_edit_->setObjectName(QStringLiteral("assetsSearchEdit"));
  search_edit_->setClearButtonEnabled(true);
  search_edit_->setPlaceholderText(tr("Search assets"));
  connect(search_edit_, &QLineEdit::textChanged, this, [this] { rebuild_grid(); });
  layout->addWidget(search_edit_);

  thumbnails_ = new AssetThumbnailCache(library_ != nullptr ? library_->directory() : QString(), this);
  auto* grid = new AssetGridWidget(this);
  grid_ = grid;
  grid_->setItemDelegate(new AssetGridDelegate(thumbnails_, grid_));
  grid_->setMouseTracking(true);  // the hover highlight
  connect(thumbnails_, &AssetThumbnailCache::thumbnail_changed, this, [this](const QString& asset_id) {
    if (auto* item = items_.value(asset_id); item != nullptr) {
      grid_->viewport()->update(grid_->visualItemRect(item));
    }
  });
  grid_->setMinimumHeight(kGridCellHeight);
  grid->delete_pressed = [this] { delete_assets_interactive(selected_asset_ids()); };
  grid->activate_pressed = [this] {
    if (const auto ids = selected_asset_ids(); !ids.isEmpty()) {
      emit place_requested(ids);
    }
  };
  connect(grid_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
    if (item != nullptr) {
      emit place_requested({item->data(kAssetIdRole).toString()});
    }
  });
  connect(grid_, &QWidget::customContextMenuRequested, this,
          [this](const QPoint& position) { show_grid_context_menu(position); });
  layout->addWidget(grid_, 1);

  empty_hint_ = new QLabel(this);
  empty_hint_->setObjectName(QStringLiteral("assetsEmptyHint"));
  empty_hint_->setWordWrap(true);
  empty_hint_->setTextFormat(Qt::PlainText);
  keep_wrapped_label_height(*empty_hint_);
  layout->addWidget(empty_hint_, 1, Qt::AlignTop);

  add_button_ = add_tool_button("assetsAddButton", QT_TR_NOOP("Add Selected Layers"),
                                QT_TR_NOOP("Save the selected layers as assets"));
  add_button_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  connect(add_button_, &QToolButton::clicked, this, [this] { emit add_selected_requested(); });
  layout->addWidget(add_button_);

  current_category_id_ = app_settings().value(QString::fromLatin1(kCurrentCategoryKey)).toString();
  if (library_ != nullptr) {
    connect(library_, &AssetLibrary::changed, this, [this] { rebuild(); });
    // An asset just written brings its picture along: nothing is read back for it.
    connect(library_, &AssetLibrary::asset_added, thumbnails_, &AssetThumbnailCache::insert);
    connect(library_, &AssetLibrary::assets_removed, thumbnails_, &AssetThumbnailCache::forget);
  }
  rebuild();
}

bool AssetsPanel::busy() const { return (library_ != nullptr && library_->busy()) || thumbnails_->busy(); }

QString AssetsPanel::current_category_id() const {
  if (library_ == nullptr) {
    return {};
  }
  if (library_->find_category(current_category_id_) != nullptr) {
    return current_category_id_;
  }
  return library_->categories().empty() ? QString() : library_->categories().front().id;
}

void AssetsPanel::set_current_category(const QString& category_id) {
  if (library_ == nullptr || library_->find_category(category_id) == nullptr) {
    return;
  }
  current_category_id_ = category_id;
  app_settings().setValue(QString::fromLatin1(kCurrentCategoryKey), current_category_id_);
  rebuild();
}

QStringList AssetsPanel::selected_asset_ids() const {
  QStringList ids;
  // Grid order, whatever order the items were picked in.
  for (int row = 0; row < grid_->count(); ++row) {
    if (const auto* item = grid_->item(row); item != nullptr && item->isSelected()) {
      ids.push_back(item->data(kAssetIdRole).toString());
    }
  }
  return ids;
}

QStringList AssetsPanel::shown_asset_ids() const {
  QStringList ids;
  for (int row = 0; row < grid_->count(); ++row) {
    ids.push_back(grid_->item(row)->data(kAssetIdRole).toString());
  }
  return ids;
}

void AssetsPanel::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  thumbnails_->set_display_side(qRound(kThumbnailSide * devicePixelRatioF()));
  // The library is read when the panel is first shown, never before, and on its worker:
  // the panel shows at once and the grid fills when the index has landed.
  if (library_ != nullptr) {
    library_->load_in_background();
  }
}

void AssetsPanel::changeEvent(QEvent* event) {
  QWidget::changeEvent(event);
  if (event->type() != QEvent::LanguageChange) {
    return;
  }
  for (auto* child : findChildren<QObject*>()) {
    apply_bound_translation(child);
  }
  search_edit_->setPlaceholderText(tr("Search assets"));
  rebuild();
}

void AssetsPanel::rebuild() {
  rebuild_categories();
  rebuild_grid();
}

void AssetsPanel::rebuild_categories() {
  if (library_ == nullptr) {
    return;
  }
  rebuilding_ = true;
  const QSignalBlocker blocker(category_combo_);
  category_combo_->clear();
  const auto current = current_category_id();
  for (const auto& category : library_->categories()) {
    category_combo_->addItem(category_display_name(category), category.id);
  }
  const auto index = category_combo_->findData(current);
  category_combo_->setCurrentIndex(index >= 0 ? index : 0);
  // current_category_id_ keeps the remembered choice even while the library has not been
  // read yet (current_category_id() resolves it against what exists).
  rebuilding_ = false;
}

void AssetsPanel::rebuild_grid() {
  if (library_ == nullptr) {
    return;
  }
  struct Row {
    QString id;
    QString name;
    QString tooltip;
  };
  const auto search = search_edit_->text().trimmed();
  const auto unnamed = tr("Untitled");
  const auto current = current_category_id();
  std::vector<Row> rows;
  // The search looks through every category; without one the grid is the picked category.
  for (const auto& category : library_->categories()) {
    if (search.isEmpty() && category.id != current) {
      continue;
    }
    for (const auto& asset : category.assets) {
      const auto name = asset.name.isEmpty() ? unnamed : asset.name;
      if (!search.isEmpty() && !name.contains(search, Qt::CaseInsensitive)) {
        continue;
      }
      rows.push_back(
          Row{asset.id, name, search.isEmpty() ? name : tr("%1 (in %2)").arg(name, category_display_name(category))});
    }
  }
  const auto set_row = [](QListWidgetItem& item, const Row& row) {
    if (item.text() != row.name) {
      item.setText(row.name);
    }
    if (item.toolTip() != row.tooltip) {
      item.setToolTip(row.tooltip);
    }
  };
  // An item holds the name and the asset's id; the delegate paints the picture from the
  // thumbnail cache, so making an item reads nothing.
  const auto new_item = [this](const Row& row) {
    auto* item = new QListWidgetItem(row.name);
    item->setData(kAssetIdRole, row.id);
    item->setToolTip(row.tooltip);
    items_.insert(row.id, item);
    return item;
  };
  const auto id_at = [this](int row) { return grid_->item(row)->data(kAssetIdRole).toString(); };

  // What a library change usually is, done in place so that a large grid is not rebuilt for
  // one asset: the same assets (a rename), more at the end (an asset added), or a few gone
  // (deleted, moved to another category). Selection and scroll position stay as they are.
  const int shown = grid_->count();
  bool in_place = false;
  if (static_cast<int>(rows.size()) >= shown) {
    bool prefix = true;
    for (int row = 0; row < shown && prefix; ++row) {
      prefix = id_at(row) == rows[static_cast<std::size_t>(row)].id;
    }
    if (prefix) {
      for (int row = 0; row < shown; ++row) {
        set_row(*grid_->item(row), rows[static_cast<std::size_t>(row)]);
      }
      for (auto row = static_cast<std::size_t>(shown); row < rows.size(); ++row) {
        grid_->addItem(new_item(rows[row]));
      }
      in_place = true;
    }
  } else {
    constexpr std::size_t kMostRemovedInPlace = 64;
    std::size_t wanted = 0;
    std::vector<int> gone;
    for (int row = 0; row < shown && gone.size() <= kMostRemovedInPlace; ++row) {
      if (wanted < rows.size() && id_at(row) == rows[wanted].id) {
        ++wanted;
      } else {
        gone.push_back(row);
      }
    }
    if (wanted == rows.size() && gone.size() <= kMostRemovedInPlace) {
      for (auto row = gone.rbegin(); row != gone.rend(); ++row) {
        auto* item = grid_->takeItem(*row);
        items_.remove(item->data(kAssetIdRole).toString());
        delete item;
      }
      for (int row = 0; row < grid_->count(); ++row) {
        set_row(*grid_->item(row), rows[static_cast<std::size_t>(row)]);
      }
      in_place = true;
    }
  }
  if (!in_place) {
    const auto selected_list = selected_asset_ids();
    const QSet<QString> selected(selected_list.begin(), selected_list.end());
    grid_->setUpdatesEnabled(false);
    grid_->clear();
    items_.clear();
    for (const auto& row : rows) {
      auto* item = new_item(row);
      grid_->addItem(item);
      if (selected.contains(row.id)) {
        item->setSelected(true);
      }
    }
    grid_->setUpdatesEnabled(true);
  }

  const bool empty = grid_->count() == 0;
  // Until the library has been read there is nothing to say about it: no hint yet.
  const bool loading = !library_->loaded();
  if (empty && !loading) {
    empty_hint_->setText(search.isEmpty()
                             ? tr("No assets here yet. Select layers, then click Add Selected Layers.")
                             : tr("No asset matches the search."));
  }
  grid_->setVisible(!empty || loading);
  empty_hint_->setVisible(empty && !loading);
  category_combo_->setEnabled(search.isEmpty());
}

void AssetsPanel::report(bool ok, const QString& error) {
  if (!ok && !error.isEmpty()) {
    emit error_message(error);
  }
}

void AssetsPanel::fill_manage_menu(QMenu& menu) {
  auto* add = menu.addAction(tr("Add Selected Layers"));
  add->setObjectName(QStringLiteral("assetsAddSelectedMenuAction"));
  add->setEnabled(add_button_->isEnabled());
  connect(add, &QAction::triggered, this, [this] { emit add_selected_requested(); });
  menu.addSeparator();
  auto* new_category = menu.addAction(tr("New Category..."));
  new_category->setObjectName(QStringLiteral("assetsNewCategoryAction"));
  connect(new_category, &QAction::triggered, this, [this] { new_category_interactive(); });
  auto* rename_category = menu.addAction(tr("Rename Category..."));
  rename_category->setObjectName(QStringLiteral("assetsRenameCategoryAction"));
  connect(rename_category, &QAction::triggered, this, [this] { rename_category_interactive(); });
  auto* delete_category = menu.addAction(tr("Delete Category..."));
  delete_category->setObjectName(QStringLiteral("assetsDeleteCategoryAction"));
  delete_category->setEnabled(library_ != nullptr && library_->categories().size() > 1U);
  connect(delete_category, &QAction::triggered, this, [this] { delete_category_interactive(); });
  menu.addSeparator();
  auto* sort = menu.addAction(tr("Sort by Name"));
  sort->setObjectName(QStringLiteral("assetsSortByNameAction"));
  const auto* category = library_ != nullptr ? library_->find_category(current_category_id()) : nullptr;
  sort->setEnabled(category != nullptr && category->assets.size() > 1U);
  connect(sort, &QAction::triggered, this, [this] { sort_current_category(); });
}

void AssetsPanel::show_grid_context_menu(const QPoint& position) {
  if (library_ == nullptr) {
    return;
  }
  auto* item = grid_->itemAt(position);
  QMenu menu(this);
  menu.setObjectName(QStringLiteral("assetsContextMenu"));
  if (item == nullptr) {
    fill_manage_menu(menu);
    menu.exec(grid_->viewport()->mapToGlobal(position));
    return;
  }
  if (!item->isSelected()) {
    grid_->clearSelection();
    item->setSelected(true);
  }
  const auto ids = selected_asset_ids();
  auto* place = menu.addAction(tr("Place"));
  place->setObjectName(QStringLiteral("assetsPlaceAction"));
  connect(place, &QAction::triggered, this, [this, ids] { emit place_requested(ids); });
  auto* rename = menu.addAction(tr("Rename..."));
  rename->setObjectName(QStringLiteral("assetsRenameAction"));
  rename->setEnabled(ids.size() == 1);
  connect(rename, &QAction::triggered, this, [this, ids] { rename_asset_interactive(ids.front()); });
  auto* move_menu = menu.addMenu(tr("Move to Category"));
  move_menu->setObjectName(QStringLiteral("assetsMoveMenu"));
  for (const auto& category : library_->categories()) {
    auto* target = move_menu->addAction(category_display_name(category));
    const auto category_id = category.id;
    // An asset already there has nowhere to go; with several picked the entry stays.
    target->setEnabled(ids.size() > 1 || library_->category_of(ids.front()) != category_id);
    connect(target, &QAction::triggered, this, [this, ids, category_id] {
      for (const auto& id : ids) {
        QString error;
        report(library_->move_asset(id, category_id, &error), error);
      }
    });
  }
  menu.addSeparator();
  auto* remove = menu.addAction(tr("Delete"));
  remove->setObjectName(QStringLiteral("assetsDeleteAction"));
  connect(remove, &QAction::triggered, this, [this, ids] { delete_assets_interactive(ids); });
  menu.exec(grid_->viewport()->mapToGlobal(position));
}

void AssetsPanel::rename_asset_interactive(const QString& asset_id) {
  const auto* asset = library_ != nullptr ? library_->find_asset(asset_id) : nullptr;
  if (asset == nullptr) {
    return;
  }
  const auto name = request_text_input(this, QStringLiteral("assetNameDialog"), tr("Rename Asset"), tr("Name:"),
                                       asset->name);
  if (!name.has_value() || AssetLibrary::cleaned_name(*name).isEmpty()) {
    return;
  }
  QString error;
  report(library_->rename_asset(asset_id, *name, &error), error);
}

void AssetsPanel::delete_assets_interactive(const QStringList& asset_ids) {
  if (library_ == nullptr || asset_ids.isEmpty()) {
    return;
  }
  QString question;
  if (asset_ids.size() == 1) {
    const auto* asset = library_->find_asset(asset_ids.front());
    if (asset == nullptr) {
      return;
    }
    question = tr("Delete the asset \"%1\"? This cannot be undone.")
                   .arg(asset->name.isEmpty() ? tr("Untitled") : asset->name);
  } else {
    question = tr("Delete %n asset(s)? This cannot be undone.", nullptr, static_cast<int>(asset_ids.size()));
  }
  if (show_warning_message(this, tr("Delete Asset"), question, QMessageBox::Yes | QMessageBox::No, QMessageBox::No,
                           QStringLiteral("assetDeleteMessageBox")) != QMessageBox::Yes) {
    return;
  }
  for (const auto& id : asset_ids) {
    QString error;
    report(library_->remove_asset(id, &error), error);
  }
}

void AssetsPanel::new_category_interactive() {
  if (library_ == nullptr) {
    return;
  }
  const auto name = request_text_input(this, QStringLiteral("assetCategoryNameDialog"), tr("New Category"),
                                       tr("Name:"), QString());
  if (!name.has_value() || AssetLibrary::cleaned_name(*name).isEmpty()) {
    return;
  }
  QString error;
  const auto id = library_->add_category(*name, &error);
  report(id.has_value(), error);
  if (id.has_value()) {
    set_current_category(*id);
  }
}

void AssetsPanel::rename_category_interactive() {
  const auto* category = library_ != nullptr ? library_->find_category(current_category_id()) : nullptr;
  if (category == nullptr) {
    return;
  }
  const auto name = request_text_input(this, QStringLiteral("assetCategoryNameDialog"), tr("Rename Category"),
                                       tr("Name:"), category_display_name(*category));
  if (!name.has_value() || AssetLibrary::cleaned_name(*name).isEmpty()) {
    return;
  }
  QString error;
  report(library_->rename_category(category->id, *name, &error), error);
}

void AssetsPanel::delete_category_interactive() {
  const auto* category = library_ != nullptr ? library_->find_category(current_category_id()) : nullptr;
  if (category == nullptr || library_->categories().size() <= 1U) {
    return;
  }
  const auto category_id = category->id;
  // An empty category goes without a question; one that holds assets takes them with it.
  if (!category->assets.empty() &&
      show_warning_message(this, tr("Delete Category"),
                           tr("Delete the category \"%1\" and the %n asset(s) in it? This cannot be undone.", nullptr,
                              static_cast<int>(category->assets.size()))
                               .arg(category_display_name(*category)),
                           QMessageBox::Yes | QMessageBox::No, QMessageBox::No,
                           QStringLiteral("assetCategoryDeleteMessageBox")) != QMessageBox::Yes) {
    return;
  }
  QString error;
  report(library_->remove_category(category_id, &error), error);
}

void AssetsPanel::sort_current_category() {
  if (library_ == nullptr) {
    return;
  }
  QString error;
  report(library_->sort_category_by_name(current_category_id(), &error), error);
}

bool AssetsPanel::accepts_layer_drag(const QMimeData* mime_data) const {
  return !layer_ids_from_mime_data(mime_data).empty() &&
         layer_drag_source_session_from_mime_data(mime_data).has_value();
}

void AssetsPanel::dragEnterEvent(QDragEnterEvent* event) {
  if (accepts_layer_drag(event->mimeData())) {
    event->setDropAction(Qt::CopyAction);
    event->accept();
    return;
  }
  event->ignore();
}

void AssetsPanel::dragMoveEvent(QDragMoveEvent* event) {
  if (accepts_layer_drag(event->mimeData())) {
    event->setDropAction(Qt::CopyAction);
    event->accept();
    return;
  }
  event->ignore();
}

void AssetsPanel::dropEvent(QDropEvent* event) {
  if (!accepts_layer_drag(event->mimeData())) {
    event->ignore();
    return;
  }
  // Own the values: the Layers panel's QDrag::exec is still on the stack, so the window
  // adds the layers after this returns.
  const auto ids = layer_ids_from_mime_data(event->mimeData());
  const auto session = layer_drag_source_session_from_mime_data(event->mimeData());
  event->setDropAction(Qt::CopyAction);
  event->accept();
  emit layers_dropped(ids, static_cast<qint64>(*session));
}

}  // namespace patchy::ui
