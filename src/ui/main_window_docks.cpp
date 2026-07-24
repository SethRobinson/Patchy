// MainWindow's dock construction, split out of main_window.cpp: create_docks
// (the right dock stack of layers/channels/history/properties/info panels),
// create_palette_dock, the right-dock-stack resize plumbing
// (update_right_dock_resize_handle_geometry, set_right_dock_stack_width,
// handle_right_dock_resize_event), and the collapsible dock title helper.
// Pure function moves from main_window.cpp; behavior must stay identical.

#include "ui/main_window.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/paths_panel.hpp"

#include "core/blend_math.hpp"
#include "core/layer_metadata.hpp"
#include "core/smart_object.hpp"
#include "core/vector_shape.hpp"
#include "core/text_warp.hpp"
#include "core/warp_mesh.hpp"
#include "core/layer_render_utils.hpp"
#include "core/layer_tree.hpp"
#include "core/palette_presets.hpp"
#include "core/pattern_presets.hpp"
#include "core/pixel_tools.hpp"
#include "formats/palette_io.hpp"
#include "filters/builtin_filters.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/heif_document_io.hpp"
#include "formats/raw_document_io.hpp"
#include "plugins/legacy_photoshop_adapter.hpp"
#include "psd/psd_document_io.hpp"
#include "psd/psd_filter_effects.hpp"
#include "psd/psd_smart_objects.hpp"
#include "ui/action_icons.hpp"
#include "ui/app_settings.hpp"
#include "render/compositor.hpp"
#include "ui/blend_mode_ui.hpp"
#include "ui/brush_dynamics_popup.hpp"
#include "ui/brush_presets.hpp"
#include "ui/brush_tip_library.hpp"
#include "ui/brush_tip_manager_dialog.hpp"
#include "ui/brush_tip_picker.hpp"
#include "ui/default_brush_tips.hpp"
#include "ui/compatibility_report.hpp"
#include "ui/image_document_io.hpp"
#include "ui/image_save_options_dialog.hpp"
#include "ui/raw_develop_dialog.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/gradient_stops_editor.hpp"
#include "ui/gradient_library.hpp"
#include "ui/gradient_manager_dialog.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/document_float_window.hpp"
#include "ui/font_picker.hpp"
#include "ui/hotkey_editor.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/color_panel.hpp"
#include "ui/layer_style_dialog.hpp"
#include "ui/layer_list_widget.hpp"
#include "ui/localization.hpp"
#include "ui/measurement_units.hpp"
#include "ui/palette_convert_dialog.hpp"
#include "ui/palette_panel.hpp"
#include "ui/pattern_library.hpp"
#include "ui/photo_pattern_presets.hpp"
#include "ui/style_library.hpp"
#include "ui/print_dialog.hpp"
#include "ui/smart_object_render.hpp"
#include "ui/scanner_import.hpp"
#include "ui/image_sequence_dialog.hpp"
#include "ui/sprite_sheet_dialog.hpp"
#include "ui/tile_preview_window.hpp"
#include "ui/warp_text_dialog.hpp"
#include "ui/qt_geometry.hpp"
#include "ui/splash_dialog.hpp"
#include "ui/update_checker.hpp"
#include "ui/zoom_status_bar.hpp"
#include "support/string_utils.hpp"

#include <QAbstractItemView>
#include <QAbstractItemModel>
#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QBrush>
#include <QBuffer>
#include <QButtonGroup>
#include <QByteArray>
#include <QDateTime>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QCursor>
#include <QColorSpace>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLayout>
#include <QResizeEvent>
#include <QIcon>
#include <QImageReader>
#include <QInputDialog>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLabel>
#include <QKeySequence>
#include <QListWidget>
#include <QLinearGradient>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QMessageBox>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPushButton>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPolygon>
#include <QPointer>
#include <QProcess>
#include <QProgressDialog>
#include <QRegion>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QScopeGuard>
#include <QSettings>
#include <QShowEvent>
#include <QStandardPaths>
#include <QStandardItem>
#include <QStyledItemDelegate>
#include <QMutex>
#include <QRawFont>
#include <QTextCharFormat>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextLayout>
#include <QTextOption>
#include <QSignalBlocker>
#include <QSize>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QStringList>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleOption>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QToolTip>
#include <QTransform>
#include <QUrl>
#include <QVariant>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <functional>
#include <future>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <tchar.h>
#include <tpcshrd.h>
#endif

#ifndef PATCHY_VERSION
#define PATCHY_VERSION "0.0.0"
#endif

// Icon resources live in the static patchy_ui library; force registration before first use.
int qInitResources_icons();

namespace patchy::ui {

namespace {

constexpr int kRightDockMinimumWidth = 280;
constexpr int kRightDockResizeHandleWidth = 7;
constexpr int kHistoryDockExpandedMinimumHeight = 190;

void install_collapsible_dock_title(QDockWidget* dock,
                                    QWidget* content,
                                    const QString& object_prefix,
                                    int expanded_minimum_height = 0,
                                    int expanded_maximum_height = QWIDGETSIZE_MAX,
                                    bool initially_expanded = true) {
  dock->setMinimumWidth(kRightDockMinimumWidth);
  content->setMinimumWidth(kRightDockMinimumWidth - 18);
  if (expanded_minimum_height > 0) {
    dock->setMinimumHeight(expanded_minimum_height);
  }
  dock->setMaximumHeight(expanded_maximum_height);

  auto* title = new QWidget(dock);
  title->setObjectName(object_prefix + QStringLiteral("DockTitle"));
  title->setMinimumWidth(kRightDockMinimumWidth - 18);
  auto* layout = new QHBoxLayout(title);
  layout->setContentsMargins(7, 3, 7, 3);
  layout->setSpacing(6);

  auto* toggle = new QToolButton(title);
  toggle->setObjectName(object_prefix + QStringLiteral("DockCollapseButton"));
  toggle->setProperty("dockCollapseButton", true);
  toggle->setAutoRaise(false);
  toggle->setCheckable(true);
  toggle->setChecked(initially_expanded);
  toggle->setText(initially_expanded ? QStringLiteral("v") : QStringLiteral(">"));
  toggle->setFixedSize(18, 18);
  toggle->setToolTip(initially_expanded ? QObject::tr("Collapse panel") : QObject::tr("Expand panel"));
  layout->addWidget(toggle);

  auto* label = new QLabel(dock->windowTitle(), title);
  label->setObjectName(object_prefix + QStringLiteral("DockTitleLabel"));
  if (dock->property(kTranslationTextProperty).isValid()) {
    label->setProperty(kTranslationContextProperty, dock->property(kTranslationContextProperty));
    label->setProperty(kTranslationTextProperty, dock->property(kTranslationTextProperty));
    apply_bound_translation(label);
  }
  layout->addWidget(label, 1);

  const auto apply_expanded_state = [dock, content, toggle, expanded_minimum_height,
                                     expanded_maximum_height](bool expanded) {
    content->setVisible(expanded);
    toggle->setText(expanded ? QStringLiteral("v") : QStringLiteral(">"));
    toggle->setToolTip(expanded ? QObject::tr("Collapse panel") : QObject::tr("Expand panel"));
    const auto collapsed_height = dock->titleBarWidget()->sizeHint().height() + 8;
    if (expanded_minimum_height > 0) {
      dock->setMinimumHeight(expanded ? expanded_minimum_height : collapsed_height);
    }
    dock->setMaximumHeight(expanded ? expanded_maximum_height : collapsed_height);
    dock->updateGeometry();
  };

  QObject::connect(toggle, &QToolButton::toggled, dock, apply_expanded_state);

  dock->setTitleBarWidget(title);
  apply_expanded_state(initially_expanded);
}

}  // namespace

void MainWindow::update_right_dock_resize_handle_geometry(QWidget* host) {
  if (host == nullptr) {
    return;
  }
  auto* handle = host->findChild<QWidget*>(QStringLiteral("rightDockResizeHandle"), Qt::FindDirectChildrenOnly);
  if (handle == nullptr) {
    return;
  }
  handle->setGeometry(0, 0, kRightDockResizeHandleWidth, host->height());
  handle->raise();
}

void MainWindow::set_right_dock_stack_width(int width) {
  const auto max_width = std::max(kRightDockMinimumWidth, this->width() - 260);
  const auto target_width = std::clamp(width, kRightDockMinimumWidth, max_width);
  for (const auto& object_name : {QStringLiteral("layersDock"), QStringLiteral("channelsDock"),
                                  QStringLiteral("pathsDock"), QStringLiteral("historyDock"),
                                  QStringLiteral("propertiesDock"), QStringLiteral("infoDock")}) {
    auto* dock = findChild<QDockWidget*>(object_name);
    if (dock == nullptr) {
      continue;
    }
    dock->setFixedWidth(target_width);
    dock->updateGeometry();
  }
}

bool MainWindow::handle_right_dock_resize_event(QObject* watched, QEvent* event) {
  auto* widget = qobject_cast<QWidget*>(watched);
  if (widget == nullptr) {
    return false;
  }

  if (widget->property("patchy.rightDockResizeHost").toBool()) {
    if (event->type() == QEvent::Resize || event->type() == QEvent::Show) {
      update_right_dock_resize_handle_geometry(widget);
    }
    return false;
  }

  if (!widget->property("patchy.rightDockResizeHandle").toBool()) {
    return false;
  }

  switch (event->type()) {
    case QEvent::MouseButtonPress: {
      auto* mouse_event = static_cast<QMouseEvent*>(event);
      if (mouse_event->button() != Qt::LeftButton) {
        return false;
      }
      auto* dock = qobject_cast<QDockWidget*>(widget->parentWidget());
      if (dock == nullptr) {
        return false;
      }
      right_dock_resizing_ = true;
      right_dock_resize_start_global_ = mouse_event->globalPosition().toPoint();
      right_dock_resize_start_width_ = dock->width();
      widget->grabMouse();
      mouse_event->accept();
      return true;
    }
    case QEvent::MouseMove: {
      auto* mouse_event = static_cast<QMouseEvent*>(event);
      if (!right_dock_resizing_ || (mouse_event->buttons() & Qt::LeftButton) == 0) {
        return false;
      }
      const auto delta = right_dock_resize_start_global_.x() - mouse_event->globalPosition().toPoint().x();
      set_right_dock_stack_width(right_dock_resize_start_width_ + delta);
      mouse_event->accept();
      return true;
    }
    case QEvent::MouseButtonRelease: {
      auto* mouse_event = static_cast<QMouseEvent*>(event);
      if (!right_dock_resizing_ || mouse_event->button() != Qt::LeftButton) {
        return false;
      }
      const auto delta = right_dock_resize_start_global_.x() - mouse_event->globalPosition().toPoint().x();
      set_right_dock_stack_width(right_dock_resize_start_width_ + delta);
      right_dock_resizing_ = false;
      widget->releaseMouse();
      mouse_event->accept();
      return true;
    }
    default:
      break;
  }

  return false;
}

void MainWindow::create_docks() {
  setTabPosition(Qt::RightDockWidgetArea, QTabWidget::North);
  auto* layers_dock = new QDockWidget(tr("Layers"), this);
  layers_dock->setObjectName(QStringLiteral("layersDock"));
  bind_widget_text(layers_dock, "Layers");
  layers_dock->setMinimumHeight(300);
  auto* layers_panel = new QWidget(layers_dock);
  layers_panel->setObjectName(QStringLiteral("layersPanel"));
  layers_panel->setMinimumHeight(240);
  auto* layers_layout = new QVBoxLayout(layers_panel);
  layers_layout->setContentsMargins(6, 6, 6, 6);
  layers_layout->setSpacing(6);

  auto* layer_list = new LayerListWidget(layers_panel);
  layer_list->set_drop_finished_callback([this] { handle_layer_drop(); });
  layer_list->set_drag_blocked_callback([this] {
    show_status_error(tr("Clear the layer name filter to reorder layers"));
  });
  layer_list->set_clip_boundary_callbacks(
      [this](LayerId upper, LayerId lower) {
        if (!has_active_document()) {
          return false;
        }
        const auto& doc = std::as_const(document());
        const auto location = find_layer_location(doc.layers(), upper);
        if (!location.has_value() || location->siblings == nullptr) {
          return false;
        }
        const auto& siblings = *location->siblings;
        const auto index = location->index;
        const auto& layer = siblings[index];
        if (layer.kind() == LayerKind::Group) {
          return false;
        }
        // Only the boundary between adjacent siblings is a clip boundary; the
        // row visually below can also be a group header or another parent's layer.
        if (index == 0 || siblings[index - 1].id() != lower) {
          return false;
        }
        return layer.clipped() || effective_clip_base(siblings, index) != nullptr;
      },
      [this](LayerId upper) {
        if (!has_active_document() || document().find_layer(upper) == nullptr) {
          return;
        }
        reveal_layer_in_layer_list(upper);
        if (document().active_layer_id() != upper) {
          document().set_active_layer(upper);
        }
        toggle_active_layer_clipping();
      });
  layer_list->set_ctrl_click_callback([this](QListWidgetItem* item, LayerCtrlClickTarget target) {
    if (canvas_ == nullptr || item == nullptr) {
      return;
    }
    const auto id = static_cast<LayerId>(item->data(kLayerIdRole).toULongLong());
    if (target == LayerCtrlClickTarget::MaskThumbnail) {
      canvas_->select_layer_mask_pixels(id);
    } else if (target == LayerCtrlClickTarget::VectorMaskThumbnail) {
      const auto* layer = std::as_const(document()).find_layer(id);
      const auto* mask = layer != nullptr ? layer->vector_mask() : nullptr;
      if (mask != nullptr) {
        // Materialize the coverage cache onto the whole canvas (zero outside
        // cache_bounds) so the selection matches the rendered mask.
        PixelBuffer coverage(document().width(), document().height(), PixelFormat::gray8());
        for (int y = 0; y < coverage.height(); ++y) {
          for (int x = 0; x < coverage.width(); ++x) {
            const auto local_x = x - mask->cache_bounds.x;
            const auto local_y = y - mask->cache_bounds.y;
            std::uint8_t value = 0;
            if (!mask->cache.empty() && local_x >= 0 && local_y >= 0 &&
                local_x < mask->cache.width() && local_y < mask->cache.height()) {
              value = *mask->cache.pixel(local_x, local_y);
            }
            *coverage.pixel(x, y) = value;
          }
        }
        canvas_->replace_selection_from_grayscale(coverage, tr("Load vector mask selection"));
        statusBar()->showMessage(tr("Loaded the vector mask as a selection"));
      }
    } else if (target == LayerCtrlClickTarget::SmartFilterMaskThumbnail) {
      const auto* layer = std::as_const(document()).find_layer(id);
      const auto* stack = layer != nullptr ? layer->smart_filter_stack() : nullptr;
      const auto pixels = stack != nullptr
                              ? materialize_smart_filter_mask(
                                    stack->mask, document().width(),
                                    document().height())
                              : std::nullopt;
      if (pixels.has_value()) {
        canvas_->replace_selection_from_grayscale(
            *pixels, tr("Load Smart Filter mask selection"));
        statusBar()->showMessage(
            tr("Loaded the Smart Filter mask as a selection"));
      }
    } else {
      canvas_->select_layer_opaque_pixels(id);
    }
  });
  layer_list->set_thumbnail_click_callback([this](QListWidgetItem* item, LayerCtrlClickTarget target,
                                                  Qt::KeyboardModifiers modifiers) {
    if (canvas_ == nullptr || item == nullptr) {
      return;
    }
    const auto id = static_cast<LayerId>(item->data(kLayerIdRole).toULongLong());
    auto* layer = document().find_layer(id);
    if (layer == nullptr) {
      return;
    }
    if (target == LayerCtrlClickTarget::MaskThumbnail && (modifiers & Qt::ShiftModifier) != 0) {
      const auto disabled = std::as_const(*layer).mask().has_value() && std::as_const(*layer).mask()->disabled;
      document().set_active_layer(id);
      set_active_layer_mask_disabled(!disabled);
      return;
    }
    if (target == LayerCtrlClickTarget::VectorMaskThumbnail && (modifiers & Qt::ShiftModifier) != 0) {
      const auto* mask = std::as_const(*layer).vector_mask();
      if (mask != nullptr) {
        document().set_active_layer(id);
        set_active_layer_vector_mask_disabled(!mask->disabled);
      }
      return;
    }
    if (target == LayerCtrlClickTarget::SmartFilterMaskThumbnail &&
        (modifiers & Qt::ShiftModifier) != 0) {
      const auto* stack = std::as_const(*layer).smart_filter_stack();
      if (stack != nullptr) {
        document().set_active_layer(id);
        set_smart_filter_mask_enabled(id, !stack->mask.enabled);
      }
      return;
    }
    const auto was_active = document().active_layer_id().has_value() && *document().active_layer_id() == id;
    document().set_active_layer(id);
    restyle_layer_rows(layer_list_);
    if (target == LayerCtrlClickTarget::SmartFilterMaskThumbnail) {
      const auto editing_mask =
          was_active && canvas_->editing_smart_filter_mask() &&
          canvas_->smart_filter_mask_owner_id() == id;
      if ((modifiers & Qt::AltModifier) != 0) {
        const auto showing_mask =
            editing_mask && canvas_->mask_display_mode() ==
                                CanvasWidget::MaskDisplayMode::Grayscale;
        if (!editing_mask &&
            !set_smart_filter_mask_edit_target_ui(
                id, CanvasWidget::MaskDisplayMode::Grayscale, false)) {
          return;
        }
        canvas_->set_mask_display_mode(
            showing_mask ? CanvasWidget::MaskDisplayMode::None
                         : CanvasWidget::MaskDisplayMode::Grayscale);
        refresh_layer_controls();
        statusBar()->showMessage(
            showing_mask
                ? tr("Editing Smart Filter mask")
                : tr("Showing the Smart Filter mask. Alt-click the mask thumbnail to return."));
        return;
      }
      if (editing_mask) {
        set_layer_edit_target_ui(CanvasWidget::LayerEditTarget::Content, true);
      } else {
        static_cast<void>(set_smart_filter_mask_edit_target_ui(
            id, CanvasWidget::MaskDisplayMode::Overlay, true));
      }
      return;
    }
    if (target == LayerCtrlClickTarget::MaskThumbnail && (modifiers & Qt::AltModifier) != 0) {
      const auto showing_mask =
          was_active && canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::Mask &&
          canvas_->mask_display_mode() == CanvasWidget::MaskDisplayMode::Grayscale;
      set_layer_edit_target_ui(CanvasWidget::LayerEditTarget::Mask, false);
      canvas_->set_mask_display_mode(showing_mask ? CanvasWidget::MaskDisplayMode::None
                                                  : CanvasWidget::MaskDisplayMode::Grayscale);
      refresh_layer_controls();
      statusBar()->showMessage(showing_mask
                                   ? tr("Editing layer mask")
                                   : tr("Showing the layer mask. Alt-click the mask thumbnail to return."));
      return;
    }
    if (target == LayerCtrlClickTarget::VectorMaskThumbnail && (modifiers & Qt::AltModifier) != 0) {
      const auto showing_mask =
          was_active && canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::VectorMask &&
          canvas_->mask_display_mode() == CanvasWidget::MaskDisplayMode::Grayscale;
      set_layer_edit_target_ui(CanvasWidget::LayerEditTarget::VectorMask, false);
      canvas_->set_mask_display_mode(showing_mask ? CanvasWidget::MaskDisplayMode::None
                                                  : CanvasWidget::MaskDisplayMode::Grayscale);
      refresh_layer_controls();
      statusBar()->showMessage(
          showing_mask ? tr("Editing vector mask")
                       : tr("Showing the vector mask. Alt-click the thumbnail to return."));
      return;
    }
    if (target == LayerCtrlClickTarget::VectorMaskThumbnail) {
      const auto editing_vector_mask_already =
          was_active && canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::VectorMask;
      set_layer_edit_target_ui(editing_vector_mask_already
                                   ? CanvasWidget::LayerEditTarget::Content
                                   : CanvasWidget::LayerEditTarget::VectorMask,
                               true);
      if (!editing_vector_mask_already) {
        statusBar()->showMessage(tr("Editing the vector mask path with the pen and path tools"));
      }
      return;
    }
    const auto editing_mask_already =
        was_active && canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::Mask;
    set_layer_edit_target_ui(target == LayerCtrlClickTarget::MaskThumbnail && !editing_mask_already
                                 ? CanvasWidget::LayerEditTarget::Mask
                                 : CanvasWidget::LayerEditTarget::Content,
                             true);
  });
  layer_list_ = layer_list;
  layer_list_->setObjectName(QStringLiteral("layerList"));
  layer_list_->setMinimumWidth(250);
  layer_list_->setMinimumHeight(120);
  layer_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  layer_list_->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
  layer_list_->setTextElideMode(Qt::ElideNone);
  layer_list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  layer_list_->setDragEnabled(true);
  layer_list_->setAcceptDrops(true);
  layer_list_->setDropIndicatorShown(true);
  layer_list_->setDragDropOverwriteMode(false);
  layer_list_->setDefaultDropAction(Qt::MoveAction);
  layer_list_->setDragDropMode(QAbstractItemView::InternalMove);
  layer_list_->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(layer_list_, &QListWidget::itemSelectionChanged, this, [this] { set_active_layer_from_selection(); });
  layer_list->set_item_double_click_callback([this](QListWidgetItem*) {
    auto& doc = document();
    const auto active = doc.active_layer_id();
    const auto* layer = active.has_value() ? doc.find_layer(*active) : nullptr;
    if (layer != nullptr && layer->kind() == LayerKind::Group) {
      // Layer styles only render for pixel content, so the blending dialog is
      // useless on a folder.
      return;
    }
    if (layer != nullptr && layer->kind() == LayerKind::Adjustment) {
      edit_active_adjustment_layer();
      return;
    }
    if (layer != nullptr && layer_is_vector_shape(*layer) && vector_lock_reason(*layer).empty()) {
      // Shape and fill layers open their appearance editor (the adjustment-
      // layer precedent); layer styles stay reachable from the context menu.
      edit_active_shape_appearance();
      return;
    }
    // Smart objects deliberately fall through to the layer styles dialog too:
    // their contents open via the row's smart-object badge button (or the
    // Smart Objects menus), so double-click stays consistent for every layer.
    edit_active_layer_style();
  });
  layer_list->set_smart_filter_double_click_callback(
      [this](QListWidgetItem* item, std::size_t execution_index) {
        const auto layer_id = static_cast<LayerId>(item->data(kLayerIdRole).toULongLong());
        if (layer_id == 0) {
          return;
        }
        // Deferred like the entry buttons: opening the dialog rebuilds the
        // layer list, which would delete the row mid-event otherwise.
        QTimer::singleShot(0, this, [this, layer_id, execution_index] {
          edit_smart_filter(layer_id, execution_index);
        });
      });
  connect(layer_list_, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
    set_layer_visibility_from_item(item);
  });
  connect(layer_list_, &QListWidget::customContextMenuRequested, this,
          [this](const QPoint& position) { show_layer_context_menu(position); });
  connect(layer_list_->model(), &QAbstractItemModel::rowsMoved, this, [this] {
    if (updating_layer_list_) {
      return;
    }
    if (const auto* list = dynamic_cast<const LayerListWidget*>(layer_list_); list != nullptr && list->drop_in_progress()) {
      return;
    }
    QTimer::singleShot(0, this, [this] { reorder_layers_from_list(); });
  });

  // One compact row (Photoshop-style): the blend combo hugs its longest mode
  // name and Opacity/Fill are prefixed spin boxes with the toolbar popup
  // slider, so the panel spends one line here instead of three. The 6 px side
  // insets sit inside the panel margin so the rows do not read as flush
  // against the dock edge.
  auto* blend_opacity_row = new QHBoxLayout();
  blend_opacity_row->setContentsMargins(6, 0, 6, 0);
  blend_opacity_row->setSpacing(6);
  blend_combo_ = new QComboBox(layers_panel);
  add_blend_mode_items(blend_combo_);
  blend_combo_->setObjectName(QStringLiteral("layerBlendModeCombo"));
  blend_combo_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  blend_combo_->setToolTip(tr("Blend mode"));
  bind_tooltip(blend_combo_, "Blend mode");
  blend_opacity_row->addWidget(blend_combo_);
  connect(blend_combo_, &QComboBox::currentIndexChanged, this, [this](int index) { set_active_layer_blend(index); });
  register_document_widget(blend_combo_);

  opacity_spin_ = new QSpinBox(layers_panel);
  opacity_spin_->setObjectName(QStringLiteral("layerOpacitySpin"));
  opacity_spin_->setRange(0, 100);
  opacity_spin_->setValue(100);
  opacity_spin_->setPrefix(tr("Opacity: "));
  opacity_spin_->setSuffix(QStringLiteral("%"));
  configure_toolbar_spinbox(opacity_spin_, 52);
  blend_opacity_row->addWidget(opacity_spin_);
  connect(opacity_spin_, &QSpinBox::valueChanged, this, [this](int value) { set_active_layer_opacity(value); });
  connect(opacity_spin_, &QSpinBox::editingFinished, this, [this] { finish_pending_layer_opacity_edit(); });
  register_document_widget(opacity_spin_);

  fill_opacity_spin_ = new QSpinBox(layers_panel);
  fill_opacity_spin_->setObjectName(QStringLiteral("layerFillOpacitySpin"));
  fill_opacity_spin_->setRange(0, 100);
  fill_opacity_spin_->setValue(100);
  fill_opacity_spin_->setPrefix(tr("Fill: "));
  fill_opacity_spin_->setSuffix(QStringLiteral("%"));
  configure_toolbar_spinbox(fill_opacity_spin_, 52);
  blend_opacity_row->addWidget(fill_opacity_spin_);
  connect(fill_opacity_spin_, &QSpinBox::valueChanged, this,
          [this](int value) { set_active_layer_fill_opacity(value); });
  connect(fill_opacity_spin_, &QSpinBox::editingFinished, this,
          [this] { finish_pending_layer_fill_opacity_edit(); });
  register_document_widget(fill_opacity_spin_);
  blend_opacity_row->addStretch(1);
  register_retranslation([this] {
    if (opacity_spin_ == nullptr || fill_opacity_spin_ == nullptr) {
      return;
    }
    opacity_spin_->setPrefix(tr("Opacity: "));
    fill_opacity_spin_->setPrefix(tr("Fill: "));
    // Re-measures the fixed width for the translated prefixes; the popup
    // install inside is a guarded no-op on repeat calls.
    configure_toolbar_spinbox(opacity_spin_, 52);
    configure_toolbar_spinbox(fill_opacity_spin_, 52);
  });
  layers_layout->addLayout(blend_opacity_row);

  // Plain left-packed row: the label, buttons, and filter hug the left edge
  // with the filter absorbing the leftover width (a grid here split the extra
  // space across its columns and stranded the buttons mid-panel).
  auto* lock_row = new QHBoxLayout();
  lock_row->setContentsMargins(6, 0, 6, 0);
  lock_row->setSpacing(6);
  auto* lock_label = new QLabel(tr("Lock"), layers_panel);
  bind_widget_text(lock_label, "Lock");
  lock_row->addWidget(lock_label);
  auto* lock_controls = new QHBoxLayout();
  lock_controls->setContentsMargins(0, 0, 0, 0);
  lock_controls->setSpacing(4);
  const auto make_lock_button = [this, layers_panel](const QString& object_name, const QString& icon_key,
                                                     const QString& tooltip, LayerLockFlags flag) {
    auto* button = new QToolButton(layers_panel);
    button->setObjectName(object_name);
    button->setProperty("layerLockControl", true);
    button->setCheckable(true);
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setIcon(simple_icon(icon_key, QColor(226, 232, 240)));
    button->setIconSize(QSize(15, 15));
    button->setToolTip(tooltip);
    button->setFixedSize(24, 24);
    connect(button, &QToolButton::toggled, this, [this, flag](bool checked) {
      set_active_layer_lock_flag(flag, checked);
    });
    register_document_widget(button);
    return button;
  };
  lock_transparent_pixels_button_ =
      make_lock_button(QStringLiteral("layerLockTransparentButton"), QStringLiteral("AL"),
                       tr("Lock transparent pixels"), kLayerLockTransparentPixels);
  lock_image_pixels_button_ =
      make_lock_button(QStringLiteral("layerLockPixelsButton"), QStringLiteral("fill"),
                       tr("Lock image pixels"), kLayerLockImagePixels);
  lock_position_button_ =
      make_lock_button(QStringLiteral("layerLockPositionButton"), QStringLiteral("TR"),
                       tr("Lock position"), kLayerLockPosition);
  lock_all_button_ = new QToolButton(layers_panel);
  lock_all_button_->setObjectName(QStringLiteral("layerLockAllButton"));
  lock_all_button_->setProperty("layerLockControl", true);
  lock_all_button_->setCheckable(true);
  lock_all_button_->setToolButtonStyle(Qt::ToolButtonIconOnly);
  lock_all_button_->setIcon(simple_icon(QStringLiteral("lock"), QColor(226, 232, 240)));
  lock_all_button_->setIconSize(QSize(15, 15));
  lock_all_button_->setToolTip(tr("Lock all"));
  lock_all_button_->setFixedSize(24, 24);
  connect(lock_all_button_, &QToolButton::toggled, this, [this](bool checked) { set_active_layer_lock_all(checked); });
  register_document_widget(lock_all_button_);
  lock_controls->addWidget(lock_transparent_pixels_button_);
  lock_controls->addWidget(lock_image_pixels_button_);
  lock_controls->addWidget(lock_position_button_);
  lock_controls->addWidget(lock_all_button_);
  layer_name_filter_edit_ = new QLineEdit(layers_panel);
  layer_name_filter_edit_->setObjectName(QStringLiteral("layerNameFilterEdit"));
  layer_name_filter_edit_->setClearButtonEnabled(true);
  layer_name_filter_edit_->setFixedHeight(24);
  bind_widget_text(layer_name_filter_edit_, "Filter layers by name...");
  connect(layer_name_filter_edit_, &QLineEdit::textChanged, this, [this] { refresh_layer_list(); });
  register_document_widget(layer_name_filter_edit_);
  // Rides the otherwise-empty stretch space beside the lock buttons instead of
  // spending a panel row of its own.
  lock_controls->addWidget(layer_name_filter_edit_, 1);
  lock_row->addLayout(lock_controls, 1);
  layers_layout->addLayout(lock_row);
  layers_layout->addWidget(layer_list_, 1);

  auto* layer_buttons = new QHBoxLayout();
  layer_buttons->setContentsMargins(0, 0, 0, 0);
  layer_buttons->setSpacing(10);
  auto* add_button = new QPushButton(layers_panel);
  auto* add_folder_button = new QPushButton(layers_panel);
  auto* adjustment_button = new QToolButton(layers_panel);
  auto* duplicate_button = new QPushButton(layers_panel);
  auto* rename_button = new QPushButton(layers_panel);
  auto* delete_button = new QPushButton(layers_panel);
  add_button->setObjectName(QStringLiteral("layerNewButton"));
  adjustment_button->setObjectName(QStringLiteral("layerNewAdjustmentButton"));
  add_folder_button->setObjectName(QStringLiteral("layerNewFolderButton"));
  duplicate_button->setObjectName(QStringLiteral("layerDuplicateButton"));
  rename_button->setObjectName(QStringLiteral("layerRenameButton"));
  delete_button->setObjectName(QStringLiteral("layerDeleteButton"));
  add_button->setIcon(simple_icon(QStringLiteral("new")));
  add_folder_button->setIcon(simple_icon(QStringLiteral("dir"), QColor(245, 205, 105)));
  adjustment_button->setIcon(simple_icon(QStringLiteral("ADJ"), QColor(190, 220, 255)));
  duplicate_button->setIcon(simple_icon(QStringLiteral("dup")));
  rename_button->setIcon(simple_icon(QStringLiteral("RN")));
  delete_button->setIcon(simple_icon(QStringLiteral("trash")));
  add_button->setToolTip(tr("New Layer"));
  add_folder_button->setToolTip(tr("New Folder"));
  adjustment_button->setToolTip(tr("New Adjustment Layer"));
  duplicate_button->setToolTip(tr("Duplicate Layer"));
  rename_button->setToolTip(tr("Rename Layer"));
  delete_button->setToolTip(tr("Delete Layer"));
  for (auto* button : {add_button, add_folder_button, duplicate_button, rename_button, delete_button}) {
    button->setProperty("layerActionButton", true);
    button->setIconSize(QSize(24, 24));
    button->setFixedSize(40, 34);
  }
  add_button->setProperty("layerDropAction", QStringLiteral("duplicate"));
  add_folder_button->setProperty("layerDropAction", QStringLiteral("folder"));
  duplicate_button->setProperty("layerDropAction", QStringLiteral("duplicate"));
  delete_button->setProperty("layerDropAction", QStringLiteral("delete"));
  for (auto* button : {add_button, add_folder_button, duplicate_button, delete_button}) {
    button->setAcceptDrops(true);
    button->installEventFilter(this);
  }
  adjustment_button->setProperty("layerActionButton", true);
  adjustment_button->setIconSize(QSize(24, 24));
  adjustment_button->setFixedSize(40, 34);
  auto* adjustment_button_menu = new QMenu(adjustment_button);
  adjustment_button_menu->setObjectName(QStringLiteral("layerNewAdjustmentButtonMenu"));
  populate_new_adjustment_layer_menu(adjustment_button_menu);
  hide_menu_action_icons(adjustment_button_menu);
  adjustment_button->setMenu(adjustment_button_menu);
  adjustment_button->setPopupMode(QToolButton::InstantPopup);
  layer_buttons->addWidget(add_button);
  layer_buttons->addWidget(add_folder_button);
  layer_buttons->addWidget(adjustment_button);
  layer_buttons->addWidget(duplicate_button);
  layer_buttons->addWidget(rename_button);
  layer_buttons->addWidget(delete_button);
  layer_buttons->addStretch(1);
  layers_layout->addLayout(layer_buttons);
  connect(add_button, &QPushButton::clicked, this, [this] { add_layer(); });
  connect(add_folder_button, &QPushButton::clicked, this, [this] { create_layer_folder(); });
  connect(duplicate_button, &QPushButton::clicked, this, [this] { duplicate_active_layer(); });
  connect(rename_button, &QPushButton::clicked, this, [this] { rename_active_layer(); });
  connect(delete_button, &QPushButton::clicked, this, [this] { delete_active_layer(); });
  for (auto* widget : {static_cast<QWidget*>(add_button), static_cast<QWidget*>(add_folder_button),
                       static_cast<QWidget*>(adjustment_button), static_cast<QWidget*>(duplicate_button),
                       static_cast<QWidget*>(rename_button), static_cast<QWidget*>(delete_button)}) {
    register_document_widget(widget);
  }

  layers_dock->setWidget(layers_panel);
  install_collapsible_dock_title(layers_dock, layers_panel, QStringLiteral("layers"), 300);
  layers_dock->setProperty("patchy.rightDockResizeHost", true);
  layers_dock->installEventFilter(this);
  auto* right_dock_resize_handle = new QWidget(layers_dock);
  right_dock_resize_handle->setObjectName(QStringLiteral("rightDockResizeHandle"));
  right_dock_resize_handle->setProperty("patchy.rightDockResizeHandle", true);
  right_dock_resize_handle->setAttribute(Qt::WA_StyledBackground, true);
  right_dock_resize_handle->setCursor(Qt::SplitHCursor);
  right_dock_resize_handle->installEventFilter(this);
  addDockWidget(Qt::RightDockWidgetArea, layers_dock);
  update_right_dock_resize_handle_geometry(layers_dock);

  channel_dock_ = new QDockWidget(tr("Channels"), this);
  channel_dock_->setObjectName(QStringLiteral("channelsDock"));
  bind_widget_text(channel_dock_, "Channels");
  channel_panel_ = new ChannelPanel(channel_dock_);
  channel_dock_->setWidget(channel_panel_);
  install_collapsible_dock_title(channel_dock_, channel_panel_, QStringLiteral("channels"), 190);
  channel_dock_->setProperty("patchy.rightDockResizeHost", true);
  channel_dock_->installEventFilter(this);
  auto* channel_dock_resize_handle = new QWidget(channel_dock_);
  channel_dock_resize_handle->setObjectName(QStringLiteral("rightDockResizeHandle"));
  channel_dock_resize_handle->setProperty("patchy.rightDockResizeHandle", true);
  channel_dock_resize_handle->setAttribute(Qt::WA_StyledBackground, true);
  channel_dock_resize_handle->setCursor(Qt::SplitHCursor);
  channel_dock_resize_handle->installEventFilter(this);
  addDockWidget(Qt::RightDockWidgetArea, channel_dock_);
  tabifyDockWidget(layers_dock, channel_dock_);
  layers_dock->raise();
  update_right_dock_resize_handle_geometry(channel_dock_);

  const auto make_channel_action = [this](QAction*& target, const char* text, const char* object_name,
                                          const char* hotkey_id, auto callback) {
    target = new QAction(tr(text), this);
    target->setObjectName(QLatin1String(object_name));
    bind_action_text(target, text);
    register_hotkey(target, QLatin1String(hotkey_id), QKeySequence(), QStringLiteral("channels"));
    register_document_action(target);
    connect(target, &QAction::triggered, this, callback);
  };
  make_channel_action(channel_new_action_, QT_TR_NOOP("New Channel"), "channelNewAction", "channel.new",
                      [this] { create_alpha_channel(); });
  make_channel_action(channel_save_selection_action_, QT_TR_NOOP("Save Selection as Channel"),
                      "channelSaveSelectionAction", "channel.save_selection",
                      [this] { save_selection_as_channel(); });
  make_channel_action(channel_load_selection_action_, QT_TR_NOOP("Load Channel as Selection"),
                      "channelLoadSelectionAction", "channel.load_selection",
                      [this] { load_channel_as_selection(); });
  make_channel_action(channel_rename_action_, QT_TR_NOOP("Rename Channel"), "channelRenameAction", "channel.rename",
                      [this] { rename_active_channel(); });
  make_channel_action(channel_invert_action_, QT_TR_NOOP("Invert Channel"), "channelInvertAction", "channel.invert",
                      [this] { invert_active_channel(); });
  make_channel_action(channel_delete_action_, QT_TR_NOOP("Delete Channel"), "channelDeleteAction", "channel.delete",
                      [this] { delete_active_channel(); });
  channel_new_action_->setIcon(simple_icon(QStringLiteral("new")));
  channel_save_selection_action_->setIcon(simple_icon(QStringLiteral("channel-save-selection")));
  channel_load_selection_action_->setIcon(simple_icon(QStringLiteral("channel-load-selection")));
  channel_rename_action_->setIcon(simple_icon(QStringLiteral("RN")));
  channel_invert_action_->setIcon(simple_icon(QStringLiteral("inv")));
  channel_delete_action_->setIcon(simple_icon(QStringLiteral("trash")));
  channel_panel_->set_actions(channel_new_action_, channel_save_selection_action_,
                              channel_load_selection_action_, channel_rename_action_, channel_invert_action_,
                              channel_delete_action_);
  channel_panel_->set_target_callback([this](ChannelPanel::RowKind kind, ChannelId id, bool overlay) {
    set_channel_edit_target(kind, id, overlay);
  });
  channel_panel_->set_load_selection_callback(
      [this](ChannelPanel::RowKind kind, ChannelId id) { load_channel_as_selection(kind, id); });
  channel_panel_->set_reorder_callback(
      [this](std::vector<ChannelId> order) { reorder_channels_from_panel(std::move(order)); });
  register_document_widget(channel_panel_);
  refresh_channel_panel();

  paths_dock_ = new QDockWidget(tr("Paths"), this);
  paths_dock_->setObjectName(QStringLiteral("pathsDock"));
  bind_widget_text(paths_dock_, "Paths");
  paths_panel_ = new PathsPanel(paths_dock_);
  paths_dock_->setWidget(paths_panel_);
  install_collapsible_dock_title(paths_dock_, paths_panel_, QStringLiteral("paths"), 190);
  paths_dock_->setProperty("patchy.rightDockResizeHost", true);
  paths_dock_->installEventFilter(this);
  auto* paths_dock_resize_handle = new QWidget(paths_dock_);
  paths_dock_resize_handle->setObjectName(QStringLiteral("rightDockResizeHandle"));
  paths_dock_resize_handle->setProperty("patchy.rightDockResizeHandle", true);
  paths_dock_resize_handle->setAttribute(Qt::WA_StyledBackground, true);
  paths_dock_resize_handle->setCursor(Qt::SplitHCursor);
  paths_dock_resize_handle->installEventFilter(this);
  addDockWidget(Qt::RightDockWidgetArea, paths_dock_);
  tabifyDockWidget(channel_dock_, paths_dock_);
  layers_dock->raise();
  update_right_dock_resize_handle_geometry(paths_dock_);

  const auto make_path_action = [this](QAction*& target, const char* text, const char* object_name,
                                       const char* hotkey_id, auto callback) {
    target = new QAction(tr(text), this);
    target->setObjectName(QLatin1String(object_name));
    bind_action_text(target, text);
    register_hotkey(target, QLatin1String(hotkey_id), QKeySequence(), QStringLiteral("paths"));
    register_document_action(target);
    connect(target, &QAction::triggered, this, callback);
  };
  make_path_action(path_new_action_, QT_TR_NOOP("New Path"), "pathNewAction", "path.new",
                   [this] { new_saved_path(); });
  make_path_action(path_fill_action_, QT_TR_NOOP("Fill Path"), "pathFillAction", "path.fill",
                   [this] { fill_active_path(); });
  make_path_action(path_stroke_action_, QT_TR_NOOP("Stroke Path"), "pathStrokeAction", "path.stroke",
                   [this] { stroke_active_path(); });
  make_path_action(path_make_selection_action_, QT_TR_NOOP("Make Selection"),
                   "pathMakeSelectionAction", "path.make_selection",
                   [this] { make_selection_from_path(); });
  make_path_action(path_from_selection_action_, QT_TR_NOOP("Make Work Path from Selection"),
                   "pathFromSelectionAction", "path.from_selection",
                   [this] { make_work_path_from_selection(); });
  make_path_action(path_duplicate_action_, QT_TR_NOOP("Duplicate Path"), "pathDuplicateAction",
                   "path.duplicate", [this] { duplicate_selected_path(); });
  make_path_action(path_clipping_action_, QT_TR_NOOP("Clipping Path"), "pathClippingAction",
                   "path.clipping", [this] { toggle_selected_path_clipping(); });
  path_clipping_action_->setCheckable(true);
  make_path_action(path_delete_action_, QT_TR_NOOP("Delete Path"), "pathDeleteAction", "path.delete",
                   [this] { delete_selected_path(); });
  path_new_action_->setIcon(simple_icon(QStringLiteral("new")));
  path_fill_action_->setIcon(simple_icon(QStringLiteral("fill")));
  path_stroke_action_->setIcon(simple_icon(QStringLiteral("stroke-path")));
  path_make_selection_action_->setIcon(simple_icon(QStringLiteral("channel-load-selection")));
  path_from_selection_action_->setIcon(simple_icon(QStringLiteral("channel-save-selection")));
  path_duplicate_action_->setIcon(simple_icon(QStringLiteral("dup")));
  path_delete_action_->setIcon(simple_icon(QStringLiteral("trash")));
  paths_panel_->set_actions(path_new_action_, path_fill_action_, path_stroke_action_,
                            path_make_selection_action_, path_from_selection_action_,
                            path_duplicate_action_, path_clipping_action_, path_delete_action_);
  paths_panel_->set_target_callback([this](PathsPanel::RowKind kind, DocumentPathId id) {
    handle_paths_panel_target(static_cast<int>(kind), id);
  });
  paths_panel_->set_deselect_callback([this] { handle_paths_panel_deselect(); });
  paths_panel_->set_rename_callback(
      [this](DocumentPathId id, QString name) { rename_document_path(id, name); });
  paths_panel_->set_save_work_path_callback([this] { save_work_path_as_named(); });
  paths_panel_->set_load_selection_callback([this](PathsPanel::RowKind kind, DocumentPathId id) {
    load_path_as_selection(static_cast<int>(kind), id);
  });
  paths_panel_->set_reorder_callback(
      [this](std::vector<DocumentPathId> order) { reorder_paths_from_panel(std::move(order)); });
  register_document_widget(paths_panel_);
  refresh_paths_panel();

  auto* history_dock = new QDockWidget(tr("History"), this);
  history_dock->setObjectName(QStringLiteral("historyDock"));
  bind_widget_text(history_dock, "History");
  history_list_ = new QListWidget(history_dock);
  history_list_->setObjectName(QStringLiteral("historyList"));
  history_dock->setWidget(history_list_);
  install_collapsible_dock_title(history_dock, history_list_, QStringLiteral("history"),
                                 kHistoryDockExpandedMinimumHeight, QWIDGETSIZE_MAX, false);
  addDockWidget(Qt::RightDockWidgetArea, history_dock);

  auto* properties_dock = new QDockWidget(tr("Properties"), this);
  properties_dock->setObjectName(QStringLiteral("propertiesDock"));
  bind_widget_text(properties_dock, "Properties");
  auto* properties_scroll = new QScrollArea(properties_dock);
  properties_scroll->setObjectName(QStringLiteral("propertiesScrollArea"));
  properties_scroll->setFrameShape(QFrame::NoFrame);
  properties_scroll->setWidgetResizable(true);
  properties_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto* properties_panel = new QWidget(properties_dock);
  properties_panel->setObjectName(QStringLiteral("propertiesPanel"));
  auto* properties_layout = new QVBoxLayout(properties_panel);
  properties_layout->setContentsMargins(6, 6, 6, 6);
  properties_layout->setSpacing(4);
  const auto add_properties_label = [properties_panel, properties_layout](const QString& object_name) {
    auto* label = new QLabel(properties_panel);
    label->setObjectName(object_name);
    label->setWordWrap(false);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    label->hide();
    properties_layout->addWidget(label);
    return label;
  };
  document_info_label_ = new QLabel(properties_panel);
  document_info_label_->setObjectName(QStringLiteral("documentInfoLabel"));
  document_info_label_->setWordWrap(false);
  document_info_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  document_info_label_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
  properties_layout->addWidget(document_info_label_);
  active_layer_info_label_ = add_properties_label(QStringLiteral("activeLayerInfoLabel"));
  active_layer_geometry_label_ = add_properties_label(QStringLiteral("activeLayerGeometryLabel"));
  active_layer_mask_label_ = add_properties_label(QStringLiteral("activeLayerMaskLabel"));
  active_layer_adjustment_label_ = add_properties_label(QStringLiteral("activeLayerAdjustmentLabel"));
  active_layer_text_label_ = add_properties_label(QStringLiteral("activeLayerTextLabel"));
  active_tool_info_label_ = add_properties_label(QStringLiteral("activeToolInfoLabel"));
  properties_layout->addStretch(0);
  properties_scroll->setWidget(properties_panel);
  properties_dock->setWidget(properties_scroll);
  install_collapsible_dock_title(properties_dock, properties_scroll, QStringLiteral("properties"), 0, 230, false);
  addDockWidget(Qt::RightDockWidgetArea, properties_dock);

  auto* info_dock = new QDockWidget(tr("Info"), this);
  info_dock->setObjectName(QStringLiteral("infoDock"));
  bind_widget_text(info_dock, "Info");
  auto* info_panel = new QWidget(info_dock);
  auto* info_layout = new QVBoxLayout(info_panel);
  info_layout->setContentsMargins(8, 8, 8, 8);
  canvas_info_label_ = new QLabel(info_panel);
  canvas_info_label_->setObjectName(QStringLiteral("canvasInfoLabel"));
  canvas_info_label_->setText(tr("X: -\nY: -\nRGB: -\nRect: -"));
  canvas_info_label_->setWordWrap(true);
  canvas_info_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  info_layout->addWidget(canvas_info_label_);
  info_layout->addStretch(1);
  info_dock->setWidget(info_panel);
  install_collapsible_dock_title(info_dock, info_panel, QStringLiteral("info"), 0, QWIDGETSIZE_MAX, false);
  addDockWidget(Qt::RightDockWidgetArea, info_dock);

  create_palette_dock();
}

void MainWindow::create_palette_dock() {
  palette_dock_ = new QDockWidget(tr("Palette"), this);
  palette_dock_->setObjectName(QStringLiteral("paletteDock"));
  bind_widget_text(palette_dock_, "Palette");
  palette_panel_ = new PalettePanel(palette_dock_);
  connect(palette_panel_, &PalettePanel::entry_clicked, this, [this](int index) {
    if (canvas_ == nullptr) {
      return;
    }
    const auto colors = displayed_palette_colors();
    if (index < 0 || index >= static_cast<int>(colors.size())) {
      return;
    }
    const QColor color(colors[static_cast<std::size_t>(index)].red, colors[static_cast<std::size_t>(index)].green,
                       colors[static_cast<std::size_t>(index)].blue);
    canvas_->set_primary_color(color);
    apply_primary_color_to_active_text_editor(color);
    // A swatch click also lands in any open color picker: the transient request
    // picker (layer-style colors, gradient stops, ...) takes it live through its
    // callback, and the persistent Foreground/Text color panel mirrors the new
    // state (blocked: set_primary_color above already applied it).
    apply_color_to_open_color_picker(color);
    if (color_dialog_ != nullptr) {
      const auto target = color_dialog_->property("patchy.colorTarget").toString();
      if (target == QStringLiteral("foreground") || target == QStringLiteral("text")) {
        if (auto* picker = color_dialog_->findChild<PatchyColorPicker*>(
                QStringLiteral("patchyAdvancedColorPicker"))) {
          const QSignalBlocker blocker(picker);
          picker->setCurrentColor(color);
        }
      }
    }
    refresh_color_buttons();
    refresh_palette_panel();
    statusBar()->showMessage(tr("Foreground: palette index %1 (%2)").arg(index).arg(color.name()));
  });
  connect(palette_panel_, &PalettePanel::entry_edit_requested, this, [this](int index) { edit_palette_entry(index); });
  connect(palette_panel_, &PalettePanel::entry_swap_requested, this,
          [this](int from_index, int to_index) { swap_palette_entries(from_index, to_index); });
  connect(palette_panel_, &PalettePanel::add_from_foreground_requested, this,
          [this] { add_palette_entry_from_foreground(); });
  connect(palette_panel_, &PalettePanel::remove_entry_requested, this,
          [this](int index) { remove_palette_entry(index); });
  connect(palette_panel_, &PalettePanel::copy_color_requested, this, [this](int index) {
    const auto colors = displayed_palette_colors();
    if (index < 0 || index >= static_cast<int>(colors.size())) {
      return;
    }
    const auto name = QColor(colors[static_cast<std::size_t>(index)].red,
                             colors[static_cast<std::size_t>(index)].green,
                             colors[static_cast<std::size_t>(index)].blue)
                          .name();
    QApplication::clipboard()->setText(name);
    statusBar()->showMessage(tr("Copied palette color %1").arg(name));
  });
  connect(palette_panel_, &PalettePanel::preset_requested, this, [this](const QString& id) {
    if (!has_active_document()) {
      return;
    }
    const auto id_utf8 = id.toStdString();
    const auto* preset = patchy::find_builtin_palette_preset(id_utf8);
    if (preset == nullptr) {
      return;
    }
    {
      // Shared with the color picker's palette dropdown: it re-opens on the
      // last chosen palette while the document is not in palette mode.
      auto settings = app_settings();
      settings.setValue(QLatin1String(kColorPickerPaletteChoiceKey), id);
    }
    set_document_palette(std::vector<RgbColor>(preset->colors.begin(), preset->colors.end()), tr("Set palette"),
                         tr("Palette set to %1").arg(tr(preset->english_name)));
  });
  connect(palette_panel_, &PalettePanel::load_from_file_requested, this, [this] { load_palette_from_file(); });
  connect(palette_panel_, &PalettePanel::save_to_file_requested, this, [this] { save_palette_to_file(); });
  connect(palette_panel_, &PalettePanel::extract_from_image_requested, this,
          [this] { extract_palette_from_image(); });
  connect(palette_panel_, &PalettePanel::convert_requested, this, [this] { convert_document_to_indexed(); });
  // Editing the "Current palette" from a color picker (dropping or pasting a
  // color onto a palette cell) routes through the same undoable document edit
  // as the panel's own entry editing.
  set_color_picker_document_palette_editor([this](int index, QColor color) {
    apply_palette_entry_color(index,
                              RgbColor{static_cast<std::uint8_t>(color.red()),
                                       static_cast<std::uint8_t>(color.green()),
                                       static_cast<std::uint8_t>(color.blue())},
                              true, tr("Edit palette entry"));
    statusBar()->showMessage(tr("Palette index %1 set to %2").arg(index).arg(color.name()));
  });
  palette_dock_->setWidget(palette_panel_);
  install_collapsible_dock_title(palette_dock_, palette_panel_, QStringLiteral("palette"), 0, QWIDGETSIZE_MAX, false);
  addDockWidget(Qt::RightDockWidgetArea, palette_dock_);
}

}  // namespace patchy::ui
