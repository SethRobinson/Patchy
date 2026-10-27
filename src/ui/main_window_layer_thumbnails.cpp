// Layers panel thumbnail gestures: Ctrl-click selection loads with their combine modifiers
// (#433), the short context menus on a row's layer and mask thumbnails (#431, #432) and on its
// fx and smart object badges (#473, #474), and the footer Delete button acting on a targeted
// mask (#441).
// See docs/layer-panel.md "Thumbnail menus and selection loads".
#include "core/layer.hpp"
#include "core/layer_metadata.hpp"
#include "core/pixel_buffer.hpp"
#include "core/vector_shape.hpp"
#include "ui/main_window.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/layer_list_widget.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/vector_operations.hpp"

#include <QAction>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMessageBox>
#include <QPoint>
#include <QPushButton>
#include <QStatusBar>
#include <QString>
#include <QStringLiteral>
#include <QTimer>
#include <QToolButton>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <utility>

namespace patchy::ui {

namespace {

// The vector mask's coverage cache over the whole canvas (zero outside cache_bounds), so the
// selection matches the rendered mask.
PixelBuffer vector_mask_canvas_coverage(const LayerVectorMask& mask, int width, int height) {
  PixelBuffer coverage(width, height, PixelFormat::gray8());
  for (int y = 0; y < coverage.height(); ++y) {
    for (int x = 0; x < coverage.width(); ++x) {
      const auto local_x = x - mask.cache_bounds.x;
      const auto local_y = y - mask.cache_bounds.y;
      std::uint8_t value = 0;
      if (!mask.cache.empty() && local_x >= 0 && local_y >= 0 && local_x < mask.cache.width() &&
          local_y < mask.cache.height()) {
        value = *mask.cache.pixel(local_x, local_y);
      }
      *coverage.pixel(x, y) = value;
    }
  }
  return coverage;
}

}  // namespace

int thumbnail_click_selection_operation(Qt::KeyboardModifiers modifiers) {
  const bool shift = (modifiers & Qt::ShiftModifier) != 0;
  const bool alt = (modifiers & Qt::AltModifier) != 0;
  if (shift && alt) {
    return 3;
  }
  if (alt) {
    return 2;
  }
  return shift ? 1 : 0;
}

void MainWindow::load_layer_part_selection(LayerId id, LayerCtrlClickTarget target, int operation) {
  if (canvas_ == nullptr || !has_active_document()) {
    return;
  }
  const auto& doc = std::as_const(document());
  const auto* layer = doc.find_layer(id);
  if (layer == nullptr) {
    return;
  }
  // `load` replaces the selection with the part; a part that cannot load runs the canvas's own
  // command alone, which reports why and leaves the selection as it was.
  QString label;
  QString loaded_message;
  std::function<void()> load;
  switch (target) {
    case LayerCtrlClickTarget::ContentThumbnail:
      if (layer->kind() != LayerKind::Pixel) {
        canvas_->select_layer_opaque_pixels(id);
        return;
      }
      label = tr("Load Layer Transparency");
      load = [this, id] { canvas_->select_layer_opaque_pixels(id); };
      break;
    case LayerCtrlClickTarget::MaskThumbnail:
      if (!layer->mask().has_value()) {
        canvas_->select_layer_mask_pixels(id);
        return;
      }
      label = tr("Load layer mask selection");
      load = [this, id] { canvas_->select_layer_mask_pixels(id); };
      break;
    case LayerCtrlClickTarget::VectorMaskThumbnail: {
      const auto* mask = layer->vector_mask();
      if (mask == nullptr) {
        return;
      }
      label = tr("Load vector mask selection");
      loaded_message = tr("Loaded the vector mask as a selection");
      load = [this, coverage = vector_mask_canvas_coverage(*mask, doc.width(), doc.height())] {
        canvas_->apply_grayscale_to_selection(coverage);
      };
      break;
    }
    case LayerCtrlClickTarget::SmartFilterMaskThumbnail: {
      const auto* stack = layer->smart_filter_stack();
      auto pixels = stack != nullptr ? materialize_smart_filter_mask(stack->mask, doc.width(), doc.height())
                                     : std::nullopt;
      if (!pixels.has_value()) {
        return;
      }
      label = tr("Load Smart Filter mask selection");
      loaded_message = tr("Loaded the Smart Filter mask as a selection");
      load = [this, coverage = std::move(*pixels)] { canvas_->apply_grayscale_to_selection(coverage); };
      break;
    }
  }
  // Add, subtract and intersect combine the part's coverage with the selection before the load,
  // through the Make Selection dialog's combine (0 replace, 1 add, 2 subtract, 3 intersect).
  canvas_->run_selection_command(label, [this, operation, &load] {
    if (operation == 0) {
      load();
      return;
    }
    const auto existing = canvas_->selection_as_grayscale();
    load();
    auto coverage = canvas_->selection_as_grayscale();
    combine_selection_coverage(coverage, operation, existing);
    canvas_->apply_grayscale_to_selection(coverage);
  });
  canvas_->update();
  if (operation == 1) {
    statusBar()->showMessage(tr("Added to the selection"));
  } else if (operation == 2) {
    statusBar()->showMessage(tr("Subtracted from the selection"));
  } else if (operation == 3) {
    statusBar()->showMessage(tr("Intersected with the selection"));
  } else if (!loaded_message.isEmpty()) {
    statusBar()->showMessage(loaded_message);
  }
}

bool MainWindow::show_layer_thumbnail_context_menu(QListWidgetItem* item, QPoint viewport_position) {
  auto* list = dynamic_cast<LayerListWidget*>(layer_list_);
  if (list == nullptr || item == nullptr || canvas_ == nullptr || !has_active_document()) {
    return false;
  }
  const auto target = list->ctrl_click_target(item, viewport_position);
  const auto id = static_cast<LayerId>(item->data(kLayerIdRole).toULongLong());
  const auto* layer = std::as_const(document()).find_layer(id);
  if (!target.has_value() || layer == nullptr) {
    return false;
  }
  const bool content = *target == LayerCtrlClickTarget::ContentThumbnail;
  const bool raster_mask = *target == LayerCtrlClickTarget::MaskThumbnail && layer->mask().has_value();
  const bool vector_mask = *target == LayerCtrlClickTarget::VectorMaskThumbnail && layer->vector_mask() != nullptr;
  // Folders and adjustments have no pixels to load: their thumbnails keep the full menu, as does
  // the Smart Filter mask.
  if (!(content && layer->kind() == LayerKind::Pixel) && !raster_mask && !vector_mask) {
    return false;
  }
  // The mask commands act on the active layer: the row under the pointer becomes it, as a click
  // on its thumbnail does.
  if (document().active_layer_id() != id) {
    document().set_active_layer(id);
    restyle_layer_rows(layer_list_);
    refresh_layer_controls();
  }

  QMenu menu(this);
  const auto add_selection_entries = [this, &menu, id, target](const std::array<const char*, 4>& texts,
                                                               const QString& object_prefix) {
    static constexpr std::array<const char*, 4> kKeys{"Select", "Add", "Subtract", "Intersect"};
    for (int operation = 0; operation < 4; ++operation) {
      auto* action = menu.addAction(tr(texts[static_cast<std::size_t>(operation)]));
      action->setObjectName(object_prefix + QLatin1String(kKeys[static_cast<std::size_t>(operation)]) +
                            QStringLiteral("Action"));
      connect(action, &QAction::triggered, this,
              [this, id, part = *target, operation] { load_layer_part_selection(id, part, operation); });
    }
  };
  // The row menu's export actions (#462), for the layer under the pointer, which the
  // selection includes (a right-click on an unselected row selects it first). A mask
  // thumbnail's entries export that mask itself as a grayscale image, so they are this
  // menu's own actions on the same labels, not the shared layer-export ones.
  const auto add_export_menu = [this, &menu, raster_mask, vector_mask] {
    menu.addSeparator();
    auto* export_menu = menu.addMenu(tr("Export"));
    export_menu->setObjectName(QStringLiteral("layerThumbnailExportMenu"));
    if (!raster_mask && !vector_mask) {
      add_layer_export_actions(export_menu);
      return;
    }
    const auto source = vector_mask ? LayerExportSource::VectorMask : LayerExportSource::LayerMask;
    auto* quick = export_menu->addAction(tr("Quick Export as PNG"));
    quick->setObjectName(QStringLiteral("layerMaskQuickExportPngAction"));
    connect(quick, &QAction::triggered, this, [this, source] { export_selected_layers(false, source); });
    auto* export_as = export_menu->addAction(tr("Export As..."));
    export_as->setObjectName(QStringLiteral("layerMaskExportAsAction"));
    connect(export_as, &QAction::triggered, this, [this, source] { export_selected_layers(true, source); });
  };
  if (content) {
    menu.setObjectName(QStringLiteral("layerThumbnailMenu"));
    add_selection_entries({QT_TR_NOOP("Select Pixels"), QT_TR_NOOP("Add Pixels to Selection"),
                           QT_TR_NOOP("Subtract Pixels from Selection"), QT_TR_NOOP("Intersect Pixels with Selection")},
                          QStringLiteral("layerThumbnailPixels"));
    add_export_menu();
    add_layer_thumbnail_size_menu(&menu);  // after its own separator
  } else {
    menu.setObjectName(raster_mask ? QStringLiteral("layerMaskThumbnailMenu")
                                   : QStringLiteral("layerVectorMaskThumbnailMenu"));
    const bool pixels_locked = layer_id_locks_image_pixels(id);
    const bool disabled = raster_mask ? layer->mask()->disabled : layer->vector_mask()->disabled;
    QAction* toggle = nullptr;
    if (raster_mask) {
      toggle = menu.addAction(disabled ? tr("Enable Layer Mask") : tr("Disable Layer Mask"));
      connect(toggle, &QAction::triggered, this, [this, disabled] { set_active_layer_mask_disabled(!disabled); });
    } else {
      toggle = menu.addAction(disabled ? tr("Enable Vector Mask") : tr("Disable Vector Mask"));
      connect(toggle, &QAction::triggered, this,
              [this, disabled] { set_active_layer_vector_mask_disabled(!disabled); });
    }
    toggle->setObjectName(QStringLiteral("layerMaskThumbnailToggleAction"));
    toggle->setEnabled(!pixels_locked);
    if (vector_mask) {
      const bool linked = !layer->vector_mask()->unlinked;
      auto* link = menu.addAction(linked ? tr("Unlink Vector Mask") : tr("Link Vector Mask"));
      link->setObjectName(QStringLiteral("layerVectorMaskThumbnailLinkAction"));
      link->setEnabled(!pixels_locked);
      connect(link, &QAction::triggered, this, [this, linked] { set_active_layer_vector_mask_linked(!linked); });
    }
    auto* properties = menu.addAction(tr("Mask Properties..."));
    properties->setObjectName(QStringLiteral("layerMaskThumbnailPropertiesAction"));
    properties->setEnabled(!pixels_locked);
    connect(properties, &QAction::triggered, this, [this, id, vector_mask] {
      // After the menu's own loop ends: the dialog runs one too.
      const auto session_id = session().session_id;
      QTimer::singleShot(0, this, [this, id, vector_mask, session_id] {
        if (has_active_document() && session().session_id == session_id) {
          edit_layer_mask_properties(id, vector_mask);
        }
      });
    });
    menu.addSeparator();
    // The Layer menu's own commands, so their shortcuts show and their state is the menu's.
    const auto add_shared = [&menu](QAction* action) {
      if (action != nullptr) {
        menu.addAction(action);
      }
    };
    if (raster_mask) {
      add_shared(delete_layer_mask_action_);
      add_shared(apply_layer_mask_action_);
    } else {
      add_shared(findChild<QAction*>(QStringLiteral("layerVectorMaskDeleteAction")));
      add_shared(findChild<QAction*>(QStringLiteral("layerVectorMaskRasterizeAction")));
    }
    menu.addSeparator();
    add_selection_entries({QT_TR_NOOP("Select Mask Pixels"), QT_TR_NOOP("Add Mask to Selection"),
                           QT_TR_NOOP("Subtract Mask from Selection"), QT_TR_NOOP("Intersect Mask with Selection")},
                          QStringLiteral("layerMaskThumbnail"));
    add_export_menu();
  }
  hide_menu_action_icons(&menu);
  menu.exec(layer_list_->viewport()->mapToGlobal(viewport_position));
  return true;
}

bool MainWindow::show_layer_badge_context_menu(QListWidgetItem* item, QPoint viewport_position) {
  auto* row_widget = item != nullptr && layer_list_ != nullptr ? layer_list_->itemWidget(item) : nullptr;
  if (row_widget == nullptr || !has_active_document()) {
    return false;
  }
  const auto global = layer_list_->viewport()->mapToGlobal(viewport_position);
  const auto badge_at = [row_widget, global](const char* name) {
    auto* badge = row_widget->findChild<QToolButton*>(QLatin1String(name));
    return badge != nullptr && badge->isVisible() && badge->rect().contains(badge->mapFromGlobal(global));
  };
  const bool fx = badge_at("layerFxBadgeButton");
  if (!fx && !badge_at("layerSmartObjectBadgeButton")) {
    return false;
  }
  const auto id = static_cast<LayerId>(item->data(kLayerIdRole).toULongLong());
  const auto* layer = std::as_const(document()).find_layer(id);
  if (layer == nullptr) {
    return false;
  }
  // The commands act on the row whose badge was clicked, picked the way a left click on that badge
  // picks it: the fx badge keeps a selection that holds the row, the smart object badge selects the
  // row alone.
  const auto selected = selected_layer_ids();
  if (fx && std::find(selected.begin(), selected.end(), id) != selected.end()) {
    select_layers_in_layer_list(selected, id, true);
  } else {
    reveal_layer_in_layer_list(id, true);
  }
  if (document().active_layer_id() != id) {
    document().set_active_layer(id);
    restyle_layer_rows(layer_list_);
    refresh_layer_controls();
  }
  layer = std::as_const(document()).find_layer(id);
  if (layer == nullptr) {
    return false;
  }

  QMenu menu(this);
  // The Layer menu's own commands, so their names, shortcuts and enabled states are the menu's.
  const auto add_shared = [&menu](QAction* action) {
    if (action != nullptr) {
      menu.addAction(action);
    }
  };
  if (fx) {
    menu.setObjectName(QStringLiteral("layerFxBadgeMenu"));
    refresh_layer_style_action_states();
    add_shared(layer_copy_style_action_);
    add_shared(layer_paste_style_action_);
    add_shared(layer_delete_style_action_);
    menu.addSeparator();
    // The Layer Styles row's master eye (LayerStyle::effects_visible).
    const bool visible = layer->layer_style().effects_visible;
    auto* toggle = menu.addAction(tr("Show Layer Styles"));
    toggle->setObjectName(QStringLiteral("layerFxBadgeShowStylesAction"));
    toggle->setCheckable(true);
    toggle->setChecked(visible);
    toggle->setEnabled(!layer_is_effectively_locked(document().layers(), id));
    connect(toggle, &QAction::triggered, this, [this, id, visible] {
      // Deferred like the row buttons: the change rebuilds the rows under the menu's caller.
      const auto session_id = session().session_id;
      QTimer::singleShot(0, this, [this, id, visible, session_id] {
        if (has_active_document() && session().session_id == session_id) {
          set_layer_effects_visible(id, !visible);
        }
      });
    });
  } else {
    // The Layer > Smart Objects commands minus Convert to Smart Object, Edit (the left click) and
    // New Smart Object via Copy.
    menu.setObjectName(QStringLiteral("layerSmartObjectBadgeMenu"));
    refresh_smart_object_action_states();
    add_shared(layer_smart_object_replace_action_);
    menu.addSeparator();
    add_shared(layer_smart_object_update_action_);
    add_shared(layer_smart_object_relink_action_);
    add_shared(layer_smart_object_embed_action_);
    menu.addSeparator();
    add_shared(layer_smart_object_to_layers_action_);
    add_shared(layer_smart_object_to_normal_action_);
    menu.addSeparator();
    add_shared(layer_smart_object_export_action_);
  }
  hide_menu_action_icons(&menu);
  menu.exec(global);
  return true;
}

void MainWindow::delete_from_layers_panel() {
  // With one layer selected and its mask the edit target, the footer's Delete removes that mask
  // rather than the layer (#441); a raster mask that could be applied asks first.
  if (canvas_ != nullptr && has_active_document()) {
    const auto ids = selected_or_active_layer_ids();
    const auto active = std::as_const(document()).active_layer_id();
    const auto* layer = active.has_value() ? std::as_const(document()).find_layer(*active) : nullptr;
    if (layer != nullptr && ids.size() == 1U && ids.front() == *active) {
      const auto edit_target = canvas_->layer_edit_target();
      if (edit_target == CanvasWidget::LayerEditTarget::VectorMask && layer->vector_mask() != nullptr) {
        delete_active_vector_mask();
        return;
      }
      if (edit_target == CanvasWidget::LayerEditTarget::Mask && layer->mask().has_value()) {
        const bool can_apply = layer->kind() == LayerKind::Pixel && !layer_pixels_are_procedural(*layer) &&
                               !layer_id_locks_image_pixels(*active) && layer->pixels().format().channels >= 3;
        if (!can_apply) {
          delete_active_layer_mask();
          return;
        }
        QMessageBox box(this);
        box.setObjectName(QStringLiteral("deleteLayerMaskPrompt"));
        box.setIcon(QMessageBox::Question);
        box.setWindowTitle(tr("Delete Layer Mask"));
        box.setText(tr("Apply the mask to the layer before deleting it?"));
        auto* apply_button = box.addButton(tr("Apply"), QMessageBox::AcceptRole);
        apply_button->setObjectName(QStringLiteral("deleteLayerMaskApplyButton"));
        auto* delete_button = box.addButton(tr("Delete"), QMessageBox::DestructiveRole);
        delete_button->setObjectName(QStringLiteral("deleteLayerMaskDeleteButton"));
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(apply_button);
        exec_dialog(box);
        if (box.clickedButton() == apply_button) {
          apply_active_layer_mask();
        } else if (box.clickedButton() == delete_button) {
          delete_active_layer_mask();
        }
        return;
      }
    }
  }
  delete_active_layer();
}

}  // namespace patchy::ui
