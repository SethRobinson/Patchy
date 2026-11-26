#pragma once

// The Assets panel (docs/assets-panel.md, GitHub issue 751): a search box, a category
// picker and a grid of named thumbnails over the user's AssetLibrary. The panel manages
// the library (categories, names, order); the window adds layers to it and places assets.

#include "core/layer.hpp"

#include <QHash>
#include <QIcon>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <functional>
#include <vector>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QMenu;
class QMimeData;
class QToolButton;

namespace patchy::ui {

class AssetLibrary;
struct AssetCategory;

class AssetsPanel final : public QWidget {
  Q_OBJECT

public:
  explicit AssetsPanel(AssetLibrary* library, QWidget* parent = nullptr);

  // Returns the asset's thumbnail, rendering and caching it when the stored one is gone.
  void set_thumbnail_provider(std::function<QImage(const QString& asset_id)> provider);

  [[nodiscard]] QString current_category_id() const;
  void set_current_category(const QString& category_id);
  [[nodiscard]] QStringList selected_asset_ids() const;
  // The ids the grid shows now, in grid order.
  [[nodiscard]] QStringList shown_asset_ids() const;
  // The footer button; the window disables it while no document is open.
  [[nodiscard]] QToolButton* add_button() const noexcept { return add_button_; }

  void rename_asset_interactive(const QString& asset_id);
  void delete_assets_interactive(const QStringList& asset_ids);
  void new_category_interactive();
  void rename_category_interactive();
  void delete_category_interactive();
  void sort_current_category();

  [[nodiscard]] static QString category_display_name(const AssetCategory& category);

signals:
  void add_selected_requested();
  void place_requested(const QStringList& asset_ids);
  // Rows dragged here from a Layers panel: the layer ids and the session they belong to.
  void layers_dropped(const std::vector<patchy::LayerId>& layer_ids, qint64 source_session_id);
  void status_message(const QString& text);
  void error_message(const QString& text);

protected:
  void showEvent(QShowEvent* event) override;
  void changeEvent(QEvent* event) override;
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dragMoveEvent(QDragMoveEvent* event) override;
  void dropEvent(QDropEvent* event) override;

private:
  void rebuild();
  void rebuild_categories();
  void rebuild_grid();
  void fill_manage_menu(QMenu& menu);
  void show_grid_context_menu(const QPoint& position);
  [[nodiscard]] QIcon icon_for(const QString& asset_id);
  [[nodiscard]] bool accepts_layer_drag(const QMimeData* mime_data) const;
  void report(bool ok, const QString& error);

  AssetLibrary* library_{nullptr};
  std::function<QImage(const QString&)> thumbnail_provider_;
  QComboBox* category_combo_{nullptr};
  QToolButton* manage_button_{nullptr};
  QLineEdit* search_edit_{nullptr};
  QListWidget* grid_{nullptr};
  QLabel* empty_hint_{nullptr};
  QToolButton* add_button_{nullptr};
  QString current_category_id_;
  QHash<QString, QIcon> icons_;
  bool rebuilding_{false};
};

}  // namespace patchy::ui
