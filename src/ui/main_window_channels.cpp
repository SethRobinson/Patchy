#include "ui/main_window.hpp"
#include "ui/main_window_shared.hpp"

#include "core/layer_metadata.hpp"
#include "core/smart_object.hpp"
#include "core/text_warp.hpp"
#include "core/warp_mesh.hpp"
#include "core/layer_render_utils.hpp"
#include "core/layer_tree.hpp"
#include "core/palette_presets.hpp"
#include "core/pixel_tools.hpp"
#include "formats/palette_io.hpp"
#include "filters/builtin_filters.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/bmp_document_io.hpp"
#include "plugins/legacy_photoshop_adapter.hpp"
#include "psd/psd_document_io.hpp"
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
#include "ui/filter_workflows.hpp"
#include "ui/gradient_stops_editor.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/document_float_window.hpp"
#include "ui/font_picker.hpp"
#include "ui/hotkey_editor.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/color_panel.hpp"
#include "ui/layer_style_dialog.hpp"
#include "ui/layer_list_widget.hpp"
#include "ui/localization.hpp"
#include "ui/palette_convert_dialog.hpp"
#include "ui/palette_panel.hpp"
#include "ui/print_dialog.hpp"
#include "ui/smart_object_render.hpp"
#include "ui/scanner_import.hpp"
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

QPixmap component_channel_thumbnail(QColor color) {
  QPixmap result(42, 30);
  result.fill(QColor(30, 32, 35));
  QPainter painter(&result);
  const QRect swatch(3, 3, result.width() - 6, result.height() - 6);
  QLinearGradient gradient(swatch.topLeft(), swatch.topRight());
  gradient.setColorAt(0.0, Qt::black);
  gradient.setColorAt(1.0, color);
  painter.fillRect(swatch, gradient);
  painter.setPen(QColor(92, 96, 102));
  painter.drawRect(swatch.adjusted(0, 0, -1, -1));
  return result;
}

QPixmap grayscale_channel_thumbnail(const PixelBuffer& pixels) {
  if (pixels.empty() || pixels.format() != PixelFormat::gray8()) {
    return {};
  }
  QImage image(pixels.width(), pixels.height(), QImage::Format_Grayscale8);
  for (int y = 0; y < pixels.height(); ++y) {
    const auto source = pixels.row(y);
    std::copy(source.begin(), source.end(), image.scanLine(y));
  }
  const auto scaled = image.scaled(QSize(42, 30), Qt::KeepAspectRatio, Qt::SmoothTransformation);
  QPixmap result(42, 30);
  result.fill(QColor(30, 32, 35));
  QPainter painter(&result);
  painter.drawImage(QPoint((result.width() - scaled.width()) / 2, (result.height() - scaled.height()) / 2), scaled);
  painter.setPen(QColor(92, 96, 102));
  painter.drawRect(result.rect().adjusted(0, 0, -1, -1));
  return result;
}

std::uint8_t white_backed_component(std::uint8_t component, std::uint8_t alpha) {
  return static_cast<std::uint8_t>((static_cast<int>(component) * static_cast<int>(alpha) +
                                    255 * (255 - static_cast<int>(alpha))) /
                                   255);
}

std::uint8_t channel_selection_luminance(std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
  return static_cast<std::uint8_t>((static_cast<int>(red) * 30 + static_cast<int>(green) * 59 +
                                    static_cast<int>(blue) * 11) /
                                   100);
}

}  // namespace

QPixmap MainWindow::cached_channel_thumbnail(const DocumentChannel& channel) {
  auto& cached = channel_thumbnail_cache_[channel.id()];
  if (cached.content_revision != channel.content_revision() || cached.thumbnail.isNull()) {
    cached.content_revision = channel.content_revision();
    cached.thumbnail = grayscale_channel_thumbnail(channel.pixels());
  }
  return cached.thumbnail;
}

void MainWindow::refresh_channel_panel() {
  if (channel_panel_ == nullptr) {
    return;
  }
  if (!has_active_document() || canvas_ == nullptr) {
    channel_thumbnail_cache_.clear();
    channel_panel_->set_rows({});
    channel_panel_->set_document_available(false);
    refresh_edit_target_chip();
    return;
  }

  const auto& doc = std::as_const(document());
  std::vector<ChannelPanel::Row> rows;
  rows.reserve(4U + doc.channels().size());
  rows.push_back({ChannelPanel::RowKind::Composite, 0, tr("Composite"),
                  component_channel_thumbnail(QColor(230, 230, 230)), false});
  rows.push_back({ChannelPanel::RowKind::Red, 0, tr("Red"), component_channel_thumbnail(QColor(235, 70, 70)),
                  false});
  rows.push_back({ChannelPanel::RowKind::Green, 0, tr("Green"),
                  component_channel_thumbnail(QColor(65, 210, 90)), false});
  rows.push_back({ChannelPanel::RowKind::Blue, 0, tr("Blue"),
                  component_channel_thumbnail(QColor(70, 120, 245)), false});

  std::set<ChannelId> current_ids;
  const auto selected_channel_id = canvas_->active_document_channel_id();
  for (const auto& channel : doc.channels()) {
    current_ids.insert(channel.id());
    const auto kind = channel.kind() == DocumentChannelKind::Spot ? ChannelPanel::RowKind::Spot
                                                                  : ChannelPanel::RowKind::Alpha;
    rows.push_back({kind, channel.id(), QString::fromStdString(channel.name()), cached_channel_thumbnail(channel),
                    selected_channel_id == channel.id() &&
                        canvas_->mask_display_mode() == CanvasWidget::MaskDisplayMode::Overlay});
  }
  for (auto it = channel_thumbnail_cache_.begin(); it != channel_thumbnail_cache_.end();) {
    if (!current_ids.contains(it->first)) {
      it = channel_thumbnail_cache_.erase(it);
    } else {
      ++it;
    }
  }

  ChannelPanel::Row selected = rows.front();
  switch (canvas_->layer_edit_target()) {
    case CanvasWidget::LayerEditTarget::ComponentRed:
      selected = rows[1];
      break;
    case CanvasWidget::LayerEditTarget::ComponentGreen:
      selected = rows[2];
      break;
    case CanvasWidget::LayerEditTarget::ComponentBlue:
      selected = rows[3];
      break;
    case CanvasWidget::LayerEditTarget::DocumentChannel:
      if (selected_channel_id.has_value()) {
        if (const auto found = std::find_if(rows.begin() + 4, rows.end(), [&](const ChannelPanel::Row& row) {
              return row.id == *selected_channel_id;
            });
            found != rows.end()) {
          selected = *found;
        }
      }
      break;
    case CanvasWidget::LayerEditTarget::Content:
    case CanvasWidget::LayerEditTarget::Mask:
      break;
  }
  channel_panel_->set_rows(std::move(rows), selected);
  const bool editing_available = !preview_dialog_edit_locked();
  channel_panel_->set_document_available(editing_available);
  channel_panel_->set_channel_creation_available(
      editing_available && doc.channels().size() < doc.maximum_saved_channel_count());
  refresh_edit_target_chip();
}

void MainWindow::set_channel_edit_target(ChannelPanel::RowKind kind, ChannelId id, bool overlay, bool announce) {
  if (canvas_ == nullptr || !has_active_document()) {
    return;
  }
  finish_active_text_editor();
  canvas_->finish_free_transform();
  canvas_->finish_warp_transform();
  QString message;
  switch (kind) {
    case ChannelPanel::RowKind::Composite:
      set_layer_edit_target_ui(CanvasWidget::LayerEditTarget::Content, false);
      canvas_->set_mask_display_mode(CanvasWidget::MaskDisplayMode::None);
      message = tr("Showing composite");
      break;
    case ChannelPanel::RowKind::Red:
      canvas_->set_component_channel_preview(CanvasWidget::LayerEditTarget::ComponentRed);
      message = tr("Previewing Red channel (read-only)");
      break;
    case ChannelPanel::RowKind::Green:
      canvas_->set_component_channel_preview(CanvasWidget::LayerEditTarget::ComponentGreen);
      message = tr("Previewing Green channel (read-only)");
      break;
    case ChannelPanel::RowKind::Blue:
      canvas_->set_component_channel_preview(CanvasWidget::LayerEditTarget::ComponentBlue);
      message = tr("Previewing Blue channel (read-only)");
      break;
    case ChannelPanel::RowKind::Alpha:
    case ChannelPanel::RowKind::Spot: {
      const auto* channel = static_cast<const Document&>(document()).find_channel(id);
      if (channel == nullptr) {
        set_layer_edit_target_ui(CanvasWidget::LayerEditTarget::Content, false);
        break;
      }
      canvas_->set_document_channel_edit_target(
          id, overlay ? CanvasWidget::MaskDisplayMode::Overlay : CanvasWidget::MaskDisplayMode::Grayscale);
      message = overlay ? tr("Showing channel overlay: %1").arg(QString::fromStdString(channel->name()))
                        : channel->kind() == DocumentChannelKind::Spot
                              ? tr("Previewing spot channel (read-only): %1").arg(QString::fromStdString(channel->name()))
                              : tr("Editing channel: %1").arg(QString::fromStdString(channel->name()));
      break;
    }
  }
  refresh_layer_controls();
  refresh_options_bar();
  update_document_action_state();
  refresh_channel_panel();
  refresh_document_info();
  if (announce && !message.isEmpty()) {
    statusBar()->showMessage(message);
  }
}

DocumentChannel* MainWindow::selected_panel_channel() noexcept {
  if (!has_active_document() || channel_panel_ == nullptr) {
    return nullptr;
  }
  const auto selected = channel_panel_->selected_row();
  if (!selected.has_value() || (selected->kind != ChannelPanel::RowKind::Alpha &&
                                selected->kind != ChannelPanel::RowKind::Spot)) {
    return nullptr;
  }
  return document().find_channel(selected->id);
}

const DocumentChannel* MainWindow::selected_panel_channel() const noexcept {
  if (!has_active_document() || channel_panel_ == nullptr) {
    return nullptr;
  }
  const auto selected = channel_panel_->selected_row();
  if (!selected.has_value() || (selected->kind != ChannelPanel::RowKind::Alpha &&
                                selected->kind != ChannelPanel::RowKind::Spot)) {
    return nullptr;
  }
  return static_cast<const Document&>(document()).find_channel(selected->id);
}

void MainWindow::create_alpha_channel() {
  if (!has_active_document()) {
    return;
  }
  auto& doc = document();
  if (doc.channels().size() >= doc.maximum_saved_channel_count()) {
    statusBar()->showMessage(tr("This document has reached the Photoshop channel limit"));
    return;
  }
  try {
    PixelBuffer pixels(doc.width(), doc.height(), PixelFormat::gray8());
    pixels.clear(0);
    const auto id = doc.allocate_channel_id();
    const auto name = doc.next_alpha_channel_name();
    push_undo_snapshot(tr("New channel"));
    doc.add_channel(DocumentChannel(id, name, DocumentChannelKind::Alpha, std::move(pixels)));
    set_channel_edit_target(ChannelPanel::RowKind::Alpha, id, false, false);
    statusBar()->showMessage(tr("Created channel %1").arg(QString::fromStdString(name)));
  } catch (const std::exception& error) {
    QMessageBox::warning(this, tr("New Channel"), QString::fromUtf8(error.what()));
  }
}

void MainWindow::save_selection_as_channel() {
  if (!has_active_document() || canvas_ == nullptr) {
    return;
  }
  auto& doc = document();
  if (doc.channels().size() >= doc.maximum_saved_channel_count()) {
    statusBar()->showMessage(tr("This document has reached the Photoshop channel limit"));
    return;
  }
  try {
    auto pixels = canvas_->selection_as_grayscale();
    const auto id = doc.allocate_channel_id();
    const auto name = doc.next_alpha_channel_name();
    push_undo_snapshot(tr("Save selection as channel"));
    doc.add_channel(DocumentChannel(id, name, DocumentChannelKind::Alpha, std::move(pixels)));
    set_channel_edit_target(ChannelPanel::RowKind::Alpha, id, false, false);
    statusBar()->showMessage(tr("Saved selection as %1").arg(QString::fromStdString(name)));
  } catch (const std::exception& error) {
    QMessageBox::warning(this, tr("Save Selection as Channel"), QString::fromUtf8(error.what()));
  }
}

void MainWindow::load_channel_as_selection() {
  if (channel_panel_ == nullptr) {
    return;
  }
  const auto selected = channel_panel_->selected_row();
  if (!selected.has_value()) {
    return;
  }
  load_channel_as_selection(selected->kind, selected->id);
}

void MainWindow::load_channel_as_selection(ChannelPanel::RowKind kind, ChannelId id) {
  if (canvas_ == nullptr || !has_active_document()) {
    return;
  }

  const PixelBuffer* selection_pixels = nullptr;
  PixelBuffer derived_pixels;
  QString name;
  if (kind == ChannelPanel::RowKind::Alpha || kind == ChannelPanel::RowKind::Spot) {
    const auto* channel = static_cast<const Document&>(document()).find_channel(id);
    if (channel == nullptr) {
      return;
    }
    selection_pixels = &channel->pixels();
    name = QString::fromStdString(channel->name());
  } else {
    const auto& doc = static_cast<const Document&>(document());
    std::vector<std::uint8_t> merged_alpha;
    const auto flattened = Compositor{}.flatten_rgb8(doc, &merged_alpha);
    derived_pixels = PixelBuffer(doc.width(), doc.height(), PixelFormat::gray8());
    for (int y = 0; y < doc.height(); ++y) {
      const auto source = flattened.row(y);
      auto destination = derived_pixels.row(y);
      for (int x = 0; x < doc.width(); ++x) {
        const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(doc.width()) +
                           static_cast<std::size_t>(x);
        const auto* pixel = source.data() + static_cast<std::size_t>(x) * 3U;
        const auto red = white_backed_component(pixel[0], merged_alpha[index]);
        const auto green = white_backed_component(pixel[1], merged_alpha[index]);
        const auto blue = white_backed_component(pixel[2], merged_alpha[index]);
        switch (kind) {
          case ChannelPanel::RowKind::Composite:
            destination[static_cast<std::size_t>(x)] = channel_selection_luminance(red, green, blue);
            break;
          case ChannelPanel::RowKind::Red:
            destination[static_cast<std::size_t>(x)] = red;
            break;
          case ChannelPanel::RowKind::Green:
            destination[static_cast<std::size_t>(x)] = green;
            break;
          case ChannelPanel::RowKind::Blue:
            destination[static_cast<std::size_t>(x)] = blue;
            break;
          case ChannelPanel::RowKind::Alpha:
          case ChannelPanel::RowKind::Spot:
            break;
        }
      }
    }
    selection_pixels = &derived_pixels;
    switch (kind) {
      case ChannelPanel::RowKind::Composite:
        name = tr("Composite");
        break;
      case ChannelPanel::RowKind::Red:
        name = tr("Red");
        break;
      case ChannelPanel::RowKind::Green:
        name = tr("Green");
        break;
      case ChannelPanel::RowKind::Blue:
        name = tr("Blue");
        break;
      case ChannelPanel::RowKind::Alpha:
      case ChannelPanel::RowKind::Spot:
        break;
    }
  }

  if (selection_pixels == nullptr) {
    return;
  }
  canvas_->replace_selection_from_grayscale(*selection_pixels, tr("Load channel as selection"));
  statusBar()->showMessage(tr("Loaded %1 as selection").arg(name));
}

void MainWindow::rename_active_channel() {
  auto* channel = selected_panel_channel();
  if (channel == nullptr || channel->kind() != DocumentChannelKind::Alpha) {
    return;
  }
  bool accepted = false;
  const auto old_name = QString::fromStdString(channel->name());
  const auto name = QInputDialog::getText(this, tr("Rename Channel"), tr("Name:"), QLineEdit::Normal,
                                          old_name, &accepted).trimmed();
  if (!accepted || name.isEmpty() || name == old_name) {
    return;
  }
  const auto id = channel->id();
  push_undo_snapshot(tr("Rename channel"));
  document().rename_channel(id, name.toStdString());
  refresh_channel_panel();
  statusBar()->showMessage(tr("Renamed channel to %1").arg(name));
}

void MainWindow::invert_active_channel() {
  auto* channel = selected_panel_channel();
  if (channel == nullptr || channel->kind() != DocumentChannelKind::Alpha || canvas_ == nullptr) {
    return;
  }
  push_undo_snapshot(tr("Invert channel"));
  auto& pixels = channel->pixels();
  auto bytes = pixels.data();
  for (auto& value : bytes) {
    value = static_cast<std::uint8_t>(255U - value);
  }
  canvas_->grayscale_target_changed(QRect(0, 0, document().width(), document().height()));
  refresh_channel_panel();
  statusBar()->showMessage(tr("Inverted channel"));
}

void MainWindow::delete_active_channel() {
  auto* channel = selected_panel_channel();
  if (channel == nullptr || channel->kind() != DocumentChannelKind::Alpha) {
    return;
  }
  const auto id = channel->id();
  const auto name = QString::fromStdString(channel->name());
  push_undo_snapshot(tr("Delete channel"));
  document().remove_channel(id);
  set_channel_edit_target(ChannelPanel::RowKind::Composite, 0, false, false);
  statusBar()->showMessage(tr("Deleted channel %1").arg(name));
}

void MainWindow::reorder_channels_from_panel(std::vector<ChannelId> order) {
  if (!has_active_document()) {
    return;
  }
  auto& doc = document();
  const auto& current = std::as_const(doc).channels();
  if (order.size() != current.size()) {
    refresh_channel_panel();
    return;
  }
  std::set<ChannelId> unique(order.begin(), order.end());
  if (unique.size() != order.size()) {
    refresh_channel_panel();
    return;
  }
  bool changed = false;
  for (std::size_t index = 0; index < current.size(); ++index) {
    const auto* desired = static_cast<const Document&>(doc).find_channel(order[index]);
    if (desired == nullptr || (current[index].kind() == DocumentChannelKind::Spot && current[index].id() != order[index]) ||
        (desired->kind() == DocumentChannelKind::Spot && desired->id() != current[index].id())) {
      refresh_channel_panel();
      return;
    }
    changed = changed || current[index].id() != order[index];
  }
  if (!changed) {
    return;
  }
  push_undo_snapshot(tr("Reorder channels"));
  for (std::size_t index = 0; index < order.size(); ++index) {
    const auto& channels = static_cast<const Document&>(doc).channels();
    if (channels[index].id() != order[index]) {
      doc.reorder_channel(order[index], index);
    }
  }
  refresh_channel_panel();
  statusBar()->showMessage(tr("Reordered channels"));
}

void MainWindow::refresh_edit_target_chip() {
  if (mask_edit_mode_chip_ == nullptr || canvas_ == nullptr || !has_active_document()) {
    if (mask_edit_mode_chip_ != nullptr) {
      mask_edit_mode_chip_->hide();
    }
    return;
  }
  if (canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::Mask) {
    mask_edit_mode_chip_->setText(tr("Editing layer mask (click to exit)"));
    mask_edit_mode_chip_->setToolTip(
        tr("Paint tools are editing the layer mask. Click to edit the layer pixels again."));
    mask_edit_mode_chip_->show();
    return;
  }
  QString component_name;
  switch (canvas_->layer_edit_target()) {
    case CanvasWidget::LayerEditTarget::ComponentRed:
      component_name = tr("Red");
      break;
    case CanvasWidget::LayerEditTarget::ComponentGreen:
      component_name = tr("Green");
      break;
    case CanvasWidget::LayerEditTarget::ComponentBlue:
      component_name = tr("Blue");
      break;
    case CanvasWidget::LayerEditTarget::Content:
    case CanvasWidget::LayerEditTarget::Mask:
    case CanvasWidget::LayerEditTarget::DocumentChannel:
      break;
  }
  if (!component_name.isEmpty()) {
    mask_edit_mode_chip_->setText(tr("Viewing channel: %1 (click to exit)").arg(component_name));
    mask_edit_mode_chip_->setToolTip(tr("Click to return to the composite image."));
    mask_edit_mode_chip_->show();
    return;
  }
  if (canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::DocumentChannel) {
    const auto* channel = canvas_->active_document_channel_id().has_value()
                              ? static_cast<const Document&>(document()).find_channel(*canvas_->active_document_channel_id())
                              : nullptr;
    if (channel != nullptr) {
      mask_edit_mode_chip_->setText(
          channel->kind() == DocumentChannelKind::Spot
              ? tr("Viewing channel: %1 (click to exit)").arg(QString::fromStdString(channel->name()))
              : tr("Editing channel: %1 (click to exit)").arg(QString::fromStdString(channel->name())));
      mask_edit_mode_chip_->setToolTip(tr("Click to return to the composite image."));
      mask_edit_mode_chip_->show();
      return;
    }
  }
  mask_edit_mode_chip_->hide();
}

void MainWindow::restore_channel_target_after_document_reset(CanvasWidget::LayerEditTarget target,
                                                             std::optional<ChannelId> channel_id,
                                                             CanvasWidget::MaskDisplayMode display_mode) {
  if (canvas_ == nullptr || !has_active_document()) {
    return;
  }
  if (target == CanvasWidget::LayerEditTarget::DocumentChannel && channel_id.has_value() &&
      static_cast<const Document&>(document()).find_channel(*channel_id) != nullptr) {
    canvas_->set_document_channel_edit_target(*channel_id, display_mode);
  } else if (target == CanvasWidget::LayerEditTarget::ComponentRed ||
             target == CanvasWidget::LayerEditTarget::ComponentGreen ||
             target == CanvasWidget::LayerEditTarget::ComponentBlue) {
    canvas_->set_component_channel_preview(target);
  }
  refresh_channel_panel();
  update_document_action_state();
}

}  // namespace patchy::ui
