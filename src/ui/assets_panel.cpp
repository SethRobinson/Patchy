#include "ui/assets_panel.hpp"

#include "ui/app_settings.hpp"
#include "ui/asset_library.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/layer_list_widget.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/ui_text_size.hpp"

#include <QAction>
#include <QComboBox>
#include <QCoreApplication>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPixmap>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace patchy::ui {

namespace {

constexpr int kThumbnailSide = 64;
constexpr int kGridCellWidth = 88;
constexpr int kGridCellHeight = 92;
constexpr int kAssetIdRole = Qt::UserRole;
// The category the panel last showed. Permanent settings key.
constexpr auto kCurrentCategoryKey = "assets/currentCategory";

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

  auto* grid = new AssetGridWidget(this);
  grid_ = grid;
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
  }
  rebuild();
}

void AssetsPanel::set_thumbnail_provider(std::function<QImage(const QString& asset_id)> provider) {
  thumbnail_provider_ = std::move(provider);
}

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
  // The library is read when the panel is first shown, never at startup.
  if (library_ != nullptr && !library_->loaded()) {
    library_->load();
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

QIcon AssetsPanel::icon_for(const QString& asset_id) {
  if (const auto it = icons_.constFind(asset_id); it != icons_.constEnd()) {
    return *it;
  }
  // Read or rendered once per asset and kept: nothing here runs per repaint.
  QImage image;
  if (thumbnail_provider_) {
    image = thumbnail_provider_(asset_id);
  } else if (library_ != nullptr) {
    image = library_->thumbnail(asset_id);
  }
  const auto ratio = devicePixelRatioF();
  const auto side = static_cast<int>(kThumbnailSide * ratio);
  QPixmap pixmap(side, side);
  pixmap.fill(Qt::transparent);
  if (!image.isNull()) {
    const auto scaled = image.scaled(QSize(side, side), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPainter painter(&pixmap);
    painter.drawImage(QPoint((side - scaled.width()) / 2, (side - scaled.height()) / 2), scaled);
  }
  pixmap.setDevicePixelRatio(ratio);
  QIcon icon;
  // The same picture selected or not: the list's own highlight marks the selection.
  icon.addPixmap(pixmap, QIcon::Normal);
  icon.addPixmap(pixmap, QIcon::Selected);
  icons_.insert(asset_id, icon);
  return icon;
}

void AssetsPanel::rebuild_grid() {
  if (library_ == nullptr) {
    return;
  }
  const auto selected = selected_asset_ids();
  const auto search = search_edit_->text().trimmed();
  grid_->clear();
  const auto unnamed = tr("Untitled");
  // The search looks through every category; without one the grid is the picked category.
  for (const auto& category : library_->categories()) {
    if (search.isEmpty() && category.id != current_category_id()) {
      continue;
    }
    for (const auto& asset : category.assets) {
      const auto name = asset.name.isEmpty() ? unnamed : asset.name;
      if (!search.isEmpty() && !name.contains(search, Qt::CaseInsensitive)) {
        continue;
      }
      auto* item = new QListWidgetItem(icon_for(asset.id), name);
      item->setData(kAssetIdRole, asset.id);
      item->setToolTip(search.isEmpty() ? name
                                        : tr("%1 (in %2)").arg(name, category_display_name(category)));
      item->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
      item->setSizeHint(QSize(kGridCellWidth - 4, kGridCellHeight - 4));
      grid_->addItem(item);
      if (selected.contains(asset.id)) {
        item->setSelected(true);
      }
    }
  }
  const bool empty = grid_->count() == 0;
  if (empty) {
    empty_hint_->setText(search.isEmpty()
                             ? tr("No assets here yet. Select layers, then click Add Selected Layers.")
                             : tr("No asset matches the search."));
  }
  grid_->setVisible(!empty);
  empty_hint_->setVisible(empty);
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
    icons_.remove(id);
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
