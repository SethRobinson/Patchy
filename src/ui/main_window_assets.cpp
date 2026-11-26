// MainWindow's side of the Assets panel (GitHub issue 751, docs/assets-panel.md): saving
// layers as assets, placing assets into documents, and the asset drag over canvases and
// document tabs.
//
// Design limits (docs/legal-constraints-interface.md, "Assets panel"): an asset is one whole
// PSD written once; it stores no reference to the document or layer it was made from, and
// a placed layer stores none to its asset, so nothing here updates, tracks or notifies.

#include "ui/main_window.hpp"

#include "core/layer.hpp"
#include "core/layer_metadata.hpp"
#include "core/layer_render_utils.hpp"
#include "core/layer_tree.hpp"
#include "core/rect_utils.hpp"
#include "psd/psd_document_io.hpp"
#include "ui/asset_library.hpp"
#include "ui/assets_panel.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/canvas_widget_shared.hpp"
#include "ui/image_document_io.hpp"
#include "ui/localization.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/panel_column.hpp"

#include <QAction>
#include <QDir>
#include <QDropEvent>
#include <QFile>
#include <QImage>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

constexpr int kAssetsDockPreferredHeight = 260;
// The stored thumbnail's longest side: the grid shows it at 64 px, doubled on dense screens.
constexpr int kAssetThumbnailSide = 192;
// The classic PSD canvas limit: an asset is a PSD, never a PSB.
constexpr std::int32_t kAssetMaximumSide = 30000;

void unite_tree_render_bounds(const Layer& layer, Rect& bounds) {
  bounds = unite_rect(bounds, layer_render_bounds(layer));
  for (const auto& child : layer.children()) {
    unite_tree_render_bounds(child, bounds);
  }
}

std::optional<Document> read_asset_document(const QByteArray& bytes) {
  if (bytes.isEmpty()) {
    return std::nullopt;
  }
  try {
    return psd::DocumentIo::read(
        std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(bytes.constData()),
                                      static_cast<std::size_t>(bytes.size())));
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

// The asset's layers alone on a transparent ground, the root shown whatever its eye says.
QImage render_asset_thumbnail(const Document& asset) {
  if (asset.width() <= 0 || asset.height() <= 0 || asset.layers().empty()) {
    return {};
  }
  Document shown = asset;
  for (auto& root : shown.layers()) {
    root.set_visible(true);
    root.set_clipped(false);
  }
  const auto image = qimage_from_document(std::as_const(shown), true);
  if (image.isNull()) {
    return {};
  }
  if (image.width() <= kAssetThumbnailSide && image.height() <= kAssetThumbnailSide) {
    return image;
  }
  return image.scaled(QSize(kAssetThumbnailSide, kAssetThumbnailSide), Qt::KeepAspectRatio,
                      Qt::SmoothTransformation);
}

}  // namespace

void MainWindow::create_assets_dock(std::vector<PanelSpec>& specs) {
  asset_library_ = new AssetLibrary(asset_library_directory(), this);
  // An index lost to a damaged file is rebuilt from the asset files, each named after the
  // layer it holds.
  asset_library_->set_recovered_name_provider([](const QString& asset_path) {
    QFile file(asset_path);
    if (!file.open(QIODevice::ReadOnly)) {
      return QString();
    }
    const auto document = read_asset_document(file.readAll());
    if (!document.has_value() || document->layers().empty()) {
      return QString();
    }
    return QString::fromStdString(document->layers().back().name());
  });
  assets_panel_ = new AssetsPanel(asset_library_, this);
  assets_panel_->set_thumbnail_provider([this](const QString& asset_id) { return asset_thumbnail(asset_id); });

  // The command behind the panel's Add Selected Layers button and its menu entry, on the
  // window so Search Commands and a shortcut reach it with the panel closed.
  assets_add_selected_action_ = new QAction(this);
  assets_add_selected_action_->setObjectName(QStringLiteral("layerAddToAssetsAction"));
  assets_add_selected_action_->setMenuRole(QAction::NoRole);
  bind_action_text(assets_add_selected_action_, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Add to Assets"));
  register_hotkey(assets_add_selected_action_, "layer.add_to_assets", QKeySequence());
  connect(assets_add_selected_action_, &QAction::triggered, this, [this] { add_selected_layers_to_assets(); });
  addAction(assets_add_selected_action_);
  register_document_action(assets_add_selected_action_);
  register_document_widget(assets_panel_->add_button());

  connect(assets_panel_, &AssetsPanel::add_selected_requested, this, [this] { add_selected_layers_to_assets(); });
  connect(assets_panel_, &AssetsPanel::place_requested, this,
          [this](const QStringList& asset_ids) { place_assets_in_active_document(asset_ids); });
  connect(assets_panel_, &AssetsPanel::layers_dropped, this,
          [this](const std::vector<LayerId>& layer_ids, qint64 source_session_id) {
            // The Layers panel's QDrag::exec is still on the stack: add after it returns.
            QTimer::singleShot(0, this, [this, layer_ids, source_session_id] {
              auto* source = shutting_down_ ? nullptr : session_with_id(source_session_id);
              if (source != nullptr) {
                add_layers_to_assets(*source, layer_ids);
              }
            });
          });
  connect(assets_panel_, &AssetsPanel::status_message, this,
          [this](const QString& text) { statusBar()->showMessage(text); });
  connect(assets_panel_, &AssetsPanel::error_message, this, [this](const QString& text) { show_status_error(text); });

  specs.push_back(PanelSpec{.id = QStringLiteral("assetsDock"),
                            .object_prefix = QStringLiteral("assets"),
                            .title = tr("Assets"),
                            .content = assets_panel_,
                            .preferred_height = kAssetsDockPreferredHeight});
}

QImage MainWindow::asset_thumbnail(const QString& asset_id) {
  if (asset_library_ == nullptr) {
    return {};
  }
  if (auto stored = asset_library_->thumbnail(asset_id); !stored.isNull()) {
    return stored;
  }
  // The cached picture is gone: render it from the asset's layers once and keep it again.
  const auto document = read_asset_document(asset_library_->read_asset_bytes(asset_id));
  if (!document.has_value()) {
    return {};
  }
  auto rendered = render_asset_thumbnail(*document);
  if (!rendered.isNull()) {
    asset_library_->store_thumbnail(asset_id, rendered);
  }
  return rendered;
}

void MainWindow::add_selected_layers_to_assets() {
  if (asset_library_ == nullptr || !has_active_document()) {
    return;
  }
  select_only_layer_if_none_active();
  auto ids = root_drop_layer_ids(std::as_const(document()).layers(), selected_or_active_layer_ids());
  if (ids.empty()) {
    show_status_error(tr("Select a layer to add to Assets"));
    return;
  }
  // Opened here so a command run from Search Commands shows what it added.
  show_panel(QStringLiteral("assetsDock"));
  add_layers_to_assets(session(), std::move(ids));
}

int MainWindow::add_layers_to_assets(DocumentSession& source, std::vector<LayerId> ids) {
  if (asset_library_ == nullptr || assets_panel_ == nullptr) {
    return 0;
  }
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return 0;
  }
  if (&source == active_session() && canvas_ != nullptr) {
    // A pending transform or inline text edit belongs in the layer before it is read, as
    // for a copy to another document. Nothing else touches the document.
    canvas_->finish_free_transform();
    finish_active_text_editor();
  }
  asset_library_->ensure_loaded();
  const auto& source_document = std::as_const(source.document);
  ids = root_drop_layer_ids(source_document.layers(), ids);
  if (ids.empty()) {
    show_status_error(tr("Select a layer to add to Assets"));
    return 0;
  }
  if (asset_library_->directory().isEmpty()) {
    show_status_error(tr("There is no folder to keep assets in"));
    return 0;
  }
  // Links are stored relative to the asset's folder, as a copy to a document saved there
  // would store them (copy_layers_between_documents).
  const auto asset_folder_file = QDir(asset_library_->directory()).filePath(QStringLiteral("asset.psd"));
  const auto category_id = assets_panel_->current_category_id();
  int added = 0;
  QString failure;
  // One asset per selected root, top to bottom; a group is one asset with its children.
  for (const auto root_id : ids) {
    const auto* root = source_document.find_layer(root_id);
    if (root == nullptr) {
      continue;
    }
    // The canvas is trimmed to what the layers draw. Layers that draw nothing, or whose
    // Smart Filters depend on where they sit, keep the source canvas and their place.
    Rect content;
    unite_tree_render_bounds(*root, content);
    const bool keep_canvas = content.empty() || move_layer_requires_smart_filter_rerender(*root);
    const auto width = keep_canvas ? source_document.width() : content.width;
    const auto height = keep_canvas ? source_document.height() : content.height;
    if (width <= 0 || height <= 0 || width > kAssetMaximumSide || height > kAssetMaximumSide) {
      failure = tr("\"%1\" is too large to keep as an asset").arg(QString::fromStdString(root->name()));
      continue;
    }
    // The source's resolution, mode, depth and profile, and nothing else of the document:
    // no guides, channels, paths or file information travel with an asset.
    Document asset(width, height, source_document.format());
    asset.color_state() = source_document.color_state();
    asset.color_state().view_exposure_stops = 0.0F;
    asset.print_settings() = source_document.print_settings();
    asset.set_global_light(source_document.metadata().global_light);
    CrossDocumentLayerPlacement placement;
    placement.exact_offset = keep_canvas ? QPoint(0, 0) : QPoint(-content.x, -content.y);
    placement.keep_names = true;
    QString error;
    const auto copied = copy_layers_between_documents(source_document, source.path, {root_id}, asset,
                                                      asset_folder_file, placement, [] { return true; }, &error);
    if (copied.empty()) {
      failure = error;
      continue;
    }
    // A Background is an ordinary layer once it is an asset.
    if (auto* copy = asset.find_layer(copied.front()); copy != nullptr) {
      clear_background_locks_if_copy_of_background(*copy, root_id, source_document.layers(),
                                                   source_document.width(), source_document.height());
    }
    std::vector<std::uint8_t> bytes;
    try {
      bytes = psd::DocumentIo::write_layered_rgb8(std::as_const(asset), psd::WriteOptions{});
    } catch (const std::exception& write_error) {
      failure = translate_data_text(write_error.what());
      continue;
    }
    const auto thumbnail = render_asset_thumbnail(asset);
    if (asset_library_->add_asset(category_id, QString::fromStdString(root->name()), bytes, thumbnail, &error)
            .has_value()) {
      ++added;
    } else {
      failure = error;
    }
  }
  if (added > 0) {
    statusBar()->showMessage(tr("Added %n asset(s)", nullptr, added));
  }
  if (!failure.isEmpty()) {
    show_status_error(failure);
  }
  return added;
}

void MainWindow::place_assets_in_active_document(const QStringList& asset_ids) {
  if (!has_active_document()) {
    show_status_error(tr("Open a document to place an asset in"));
    return;
  }
  // No drop point: the layers land in the middle of the canvas.
  place_assets(asset_ids, session().session_id, CrossDocumentLayerPlacement{});
}

bool MainWindow::place_assets(const QStringList& asset_ids, std::int64_t target_session_id,
                              const CrossDocumentLayerPlacement& placement) {
  if (asset_library_ == nullptr || asset_ids.isEmpty() || shutting_down_) {
    return false;
  }
  auto* target = session_with_id(target_session_id);
  if (target == nullptr) {
    return false;  // the document closed between the drop and this call
  }
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return false;
  }
  // The receiving document becomes the active one (a float's window comes forward), so the
  // Layers panel, the refresh and Undo all act on the document that changed.
  if (target != active_session()) {
    activate_document_session(*target);
  }
  canvas_->finish_free_transform();
  finish_active_text_editor();
  target = session_with_id(target_session_id);
  if (target == nullptr) {
    return false;
  }

  // Build the new stack in a staged copy, as Files as Layers does: a refusal part-way
  // leaves the document untouched.
  Document staged = std::as_const(target->document);
  std::vector<LayerId> added_top_to_bottom;
  QStringList names;
  for (const auto& asset_id : asset_ids) {
    const auto* entry = asset_library_->find_asset(asset_id);
    if (entry == nullptr) {
      continue;
    }
    const auto name = entry->name.isEmpty() ? tr("Untitled") : entry->name;
    auto loaded = read_asset_document(asset_library_->read_asset_bytes(asset_id));
    if (!loaded.has_value() || loaded->layers().empty()) {
      show_status_error(tr("The asset \"%1\" could not be read").arg(name));
      return false;
    }
    record_text_layout_metrics_for_reopened_text(*loaded);
    lay_out_raster_less_imported_text(*loaded);
    if (const auto* background =
            background_layer(std::as_const(*loaded).layers(), loaded->width(), loaded->height());
        background != nullptr) {
      auto* layer = loaded->find_layer(background->id());
      set_layer_lock_flags(*layer, layer_lock_flags(*layer) & ~(kLayerLockPosition | kLayerLockTransparentPixels |
                                                                kLayerLockArtboardAutonest));
    }
    // copy_layers_between_documents takes ids top to bottom; layers() is bottom to top.
    std::vector<LayerId> root_ids;
    const auto& roots = std::as_const(*loaded).layers();
    for (auto it = roots.rbegin(); it != roots.rend(); ++it) {
      root_ids.push_back(it->id());
    }
    QString error;
    // The same copy a layer dragged from another document gets: text stays text, shapes
    // stay shapes, and the copies have no tie to the asset they came from.
    const auto copied = copy_layers_between_documents(std::as_const(*loaded), asset_library_->asset_path(asset_id),
                                                      root_ids, staged, target->path, placement,
                                                      [] { return true; }, &error);
    if (copied.empty()) {
      show_status_error(error.isEmpty() ? tr("The asset \"%1\" could not be placed").arg(name) : error);
      return false;
    }
    added_top_to_bottom.insert(added_top_to_bottom.begin(), copied.begin(), copied.end());
    names.push_back(name);
  }
  if (added_top_to_bottom.empty()) {
    return false;
  }
  push_undo_snapshot(*target, tr("Place asset"));
  target->document = std::move(staged);
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
  // After the refreshes: a rebuild collapses the selection to the active row.
  select_layers_in_layer_list(added_top_to_bottom, added_top_to_bottom.front());
  update_undo_redo_actions();
  statusBar()->showMessage(names.size() == 1 ? tr("Placed the asset \"%1\"").arg(names.front())
                                             : tr("Placed %n asset(s)", nullptr, static_cast<int>(names.size())));
  return true;
}

bool MainWindow::handle_asset_drag_event(QObject* watched, QEvent* event) {
  if (event == nullptr || document_tabs_ == nullptr || shutting_down_ || asset_library_ == nullptr) {
    return false;
  }
  const auto type = event->type();
  if (type != QEvent::DragEnter && type != QEvent::DragMove && type != QEvent::Drop) {
    return false;
  }
  // The document under the pointer: a session canvas (tab page or float) or a document
  // tab. Anywhere else the drag has no target.
  auto* canvas = qobject_cast<CanvasWidget*>(watched);
  auto* tab_bar = document_tabs_->tabBar();
  const bool on_tab_bar = canvas == nullptr && tab_bar != nullptr && watched == tab_bar;
  if (canvas == nullptr && !on_tab_bar) {
    return false;
  }
  auto* drop_event = static_cast<QDropEvent*>(event);
  const auto asset_ids = asset_ids_from_mime_data(drop_event->mimeData());
  if (asset_ids.isEmpty()) {
    return false;  // files, layers and everything else keep their own handlers
  }

  const auto position = drop_event->position().toPoint();
  int tab_index = -1;
  DocumentSession* target = nullptr;
  if (canvas != nullptr) {
    target = session_for_canvas(canvas);
  } else {
    tab_index = tab_bar->tabAt(position);
    if (tab_index >= 0) {
      target = session_for_canvas(dynamic_cast<CanvasWidget*>(document_tabs_->widget(tab_index)));
    }
  }
  // A drag from another Patchy process, a gap between tabs or the preview-dialog edit lock
  // refuses the drop. Answering here keeps the file-drop handlers from seeing the drag.
  const bool own_drag = asset_drag_is_from_this_process(drop_event->mimeData());
  const bool locked = preview_dialog_edit_locked();
  if (target == nullptr || !own_drag || locked) {
    if (on_tab_bar) {
      hide_tab_strip_highlight();
    }
    if (type == QEvent::Drop && locked && target != nullptr && own_drag) {
      show_preview_dialog_edit_lock_message();
    }
    drop_event->ignore();
    return true;
  }

  drop_event->setDropAction(Qt::CopyAction);
  drop_event->accept();
  if (type != QEvent::Drop) {
    if (on_tab_bar) {
      const auto tab_rect = tab_bar->tabRect(tab_index);
      show_tab_strip_highlight(QRect(document_tabs_->mapFrom(tab_bar, tab_rect.topLeft()), tab_rect.size()));
    }
    return true;
  }
  if (on_tab_bar) {
    hide_tab_strip_highlight();
  }

  // Return from the drop before touching the document: the panel's QDrag::exec is still
  // on the stack. Own every value the deferred call needs; never the event or its mime data.
  CrossDocumentLayerPlacement placement;
  if (canvas != nullptr) {
    placement.drop_document_point = canvas->document_point_for_widget_position(position);
  }
  const auto target_id = target->session_id;
  QTimer::singleShot(0, this, [this, asset_ids, target_id, placement] {
    if (!shutting_down_) {
      place_assets(asset_ids, target_id, placement);
    }
  });
  return true;
}

}  // namespace patchy::ui
