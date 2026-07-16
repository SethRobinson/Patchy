// MainWindow's clipboard and layer operations, split out of main_window.cpp:
// the cut/copy/copy-merged/paste flows and system-clipboard plumbing, the
// transform/warp dialogs, layer add/folder/via-copy/via-cut, masks, duplicate,
// rename, layer styles (edit/copy/paste/delete + ensure_patterns_for_style),
// delete/move, the layer context menu, merge-visible, fill/clear/stroke,
// selection expand/contract/border dialogs, layer flips, crop-to-selection and
// canvas rotation, plus the anonymous-namespace helpers only they use
// (clipboard_image_signature, default_new_layer_name, and the edit-options /
// copy-pixels helper block).
// rasterize_active_layers, rasterize_active_layer_styles, and merge_down stay
// in main_window.cpp: they render text layers through the internal text
// pipeline (render_text_layer_pixels_from_metadata), which is deliberately not
// promoted out of that file.
// Pure function moves from main_window.cpp; behavior must stay identical.

#include "ui/main_window.hpp"
#include "ui/main_window_shared.hpp"

#include "core/blend_math.hpp"
#include "core/layer_metadata.hpp"
#include "core/smart_object.hpp"
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
#include "ui/sprite_sheet_dialog.hpp"
#include "ui/start_panel.hpp"
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

QByteArray clipboard_image_signature(const QImage& image) {
  if (image.isNull()) {
    return {};
  }

  const auto converted = image.convertToFormat(QImage::Format_RGBA8888);
  QByteArray signature;
  const qint32 width = converted.width();
  const qint32 height = converted.height();
  const auto pixel_bytes = static_cast<qint64>(std::max<qint32>(0, width)) *
                           static_cast<qint64>(std::max<qint32>(0, height)) * 4;
  signature.reserve(static_cast<qsizetype>(sizeof(width) + sizeof(height) + pixel_bytes));
  signature.append(reinterpret_cast<const char*>(&width), static_cast<qsizetype>(sizeof(width)));
  signature.append(reinterpret_cast<const char*>(&height), static_cast<qsizetype>(sizeof(height)));
  for (int y = 0; y < converted.height(); ++y) {
    signature.append(reinterpret_cast<const char*>(converted.constScanLine(y)),
                     static_cast<qsizetype>(converted.width() * 4));
  }
  return signature;
}

QString default_new_layer_name(const Document& document) {
  std::set<std::string> existing_names;
  std::function<void(const std::vector<Layer>&)> collect_names = [&](const std::vector<Layer>& layers) {
    for (const auto& layer : layers) {
      existing_names.insert(layer.name());
      collect_names(layer.children());
    }
  };
  collect_names(document.layers());

  int suffix = static_cast<int>(document.layers().size()) + 1;
  QString name;
  do {
    name = QObject::tr("Layer %1").arg(suffix++);
  } while (existing_names.contains(name.toStdString()));
  return name;
}

EditOptions edit_options(CanvasWidget& canvas) {
  EditOptions options;
  options.primary = edit_color(canvas.primary_color());
  options.secondary = edit_color(canvas.secondary_color());
  options.brush_size = canvas.brush_size();
  options.brush_softness = canvas.brush_softness();
  options.progress_callback = [&canvas] {
    canvas.tick_processing_operation();
  };
  if (canvas.selected_document_rect().has_value()) {
    options.selection = to_core_rect(*canvas.selected_document_rect());
    const auto region = canvas.selected_document_region();
    if (!canvas.selection_has_partial_alpha()) {
      options.selection_scan_rects.reserve(static_cast<std::size_t>(region.rectCount()));
      for (const auto& rect : region) {
        options.selection_scan_rects.push_back(to_core_rect(rect));
      }
    }
    options.selection_mask = [region](std::int32_t x, std::int32_t y) { return region.contains(QPoint(x, y)); };
    options.selection_coverage = [&canvas](std::int32_t x, std::int32_t y) {
      return static_cast<float>(canvas.selection_alpha_at(QPoint(x, y))) / 255.0F;
    };
  }
  return options;
}

std::int64_t rect_pixel_count(Rect rect) noexcept {
  return static_cast<std::int64_t>(std::max(0, rect.width)) * static_cast<std::int64_t>(std::max(0, rect.height));
}

std::int64_t clear_scan_pixel_count(const Document& document, const Layer& layer, Rect rect, const EditOptions& options) {
  auto affected = intersect_rect(intersect_rect(rect, Rect::from_size(document.width(), document.height())), layer.bounds());
  if (options.selection.has_value()) {
    affected = intersect_rect(affected, *options.selection);
  }
  if (affected.empty()) {
    return 0;
  }
  if (options.selection_scan_rects.empty()) {
    return rect_pixel_count(affected);
  }

  std::int64_t count = 0;
  for (const auto& scan_rect : options.selection_scan_rects) {
    count += rect_pixel_count(intersect_rect(affected, scan_rect));
  }
  return count;
}

Rect intersect_copy_rect(Rect a, Rect b) {
  const auto left = std::max(a.x, b.x);
  const auto top = std::max(a.y, b.y);
  const auto right = std::min(a.x + a.width, b.x + b.width);
  const auto bottom = std::min(a.y + a.height, b.y + b.height);
  return Rect{left, top, std::max(0, right - left), std::max(0, bottom - top)};
}

std::uint8_t layer_mask_value_at(const Layer& layer, std::int32_t x, std::int32_t y) {
  const auto& mask = layer.mask();
  if (!mask.has_value() || mask->disabled) {
    return 255;
  }
  if (mask->pixels.empty() || mask->pixels.format() != PixelFormat::gray8()) {
    return mask->default_color;
  }
  if (!mask->bounds.contains(x, y)) {
    return mask->default_color;
  }
  return *mask->pixels.pixel(x - mask->bounds.x, y - mask->bounds.y);
}

void apply_selection_mask(PixelBuffer& pixels, Rect document_rect, const CanvasWidget& canvas);

PixelBuffer copy_pixels_from_layer(const Layer& layer, Rect document_rect, const CanvasWidget* canvas = nullptr) {
  const auto& source = layer.pixels();
  PixelBuffer copied(document_rect.width, document_rect.height, PixelFormat::rgba8());
  copied.clear(0);
  if (source.empty() || source.format().bit_depth != BitDepth::UInt8 || source.format().channels < 3) {
    return copied;
  }

  const auto bounds = layer.bounds();
  for (std::int32_t y = 0; y < document_rect.height; ++y) {
    for (std::int32_t x = 0; x < document_rect.width; ++x) {
      const auto sx = document_rect.x + x - bounds.x;
      const auto sy = document_rect.y + y - bounds.y;
      if (sx < 0 || sy < 0 || sx >= source.width() || sy >= source.height()) {
        continue;
      }
      const auto* src = source.pixel(sx, sy);
      auto* dst = copied.pixel(x, y);
      dst[0] = src[0];
      dst[1] = src[1];
      dst[2] = src[2];
      const auto source_alpha = source.format().channels >= 4 ? src[3] : 255;
      const QPoint document_point(document_rect.x + x, document_rect.y + y);
      const auto layer_alpha = layer_mask_value_at(layer, document_point.x(), document_point.y());
      dst[3] = static_cast<std::uint8_t>((static_cast<int>(source_alpha) * static_cast<int>(layer_alpha)) / 255);
    }
  }
  if (canvas != nullptr) {
    apply_selection_mask(copied, document_rect, *canvas);
  }
  return copied;
}

void apply_selection_mask(PixelBuffer& pixels, Rect document_rect, const CanvasWidget& canvas) {
  if (!canvas.has_selection() || pixels.empty() || pixels.format().bit_depth != BitDepth::UInt8 ||
      pixels.format().channels < 4) {
    return;
  }

  const QRect local_bounds(0, 0, pixels.width(), pixels.height());
  if (!canvas.selection_has_partial_alpha()) {
    const auto selected =
        canvas.selected_document_region().intersected(QRegion(QRect(document_rect.x, document_rect.y,
                                                                     document_rect.width, document_rect.height)));
    if (selected.isEmpty()) {
      const auto pixel_bytes = bytes_per_pixel(pixels.format());
      for (std::int32_t y = 0; y < pixels.height(); ++y) {
        auto row = pixels.row(y);
        for (std::int32_t x = 0; x < pixels.width(); ++x) {
          row[static_cast<std::size_t>(x) * pixel_bytes + 3U] = 0;
        }
      }
      return;
    }

    auto original = pixels;
    const auto pixel_bytes = bytes_per_pixel(pixels.format());
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      auto row = pixels.row(y);
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        row[static_cast<std::size_t>(x) * pixel_bytes + 3U] = 0;
      }
    }
    for (const auto& rect : selected) {
      const auto local = QRect(rect.x() - document_rect.x, rect.y() - document_rect.y, rect.width(), rect.height())
                             .intersected(local_bounds);
      if (local.isEmpty()) {
        continue;
      }
      for (int y = local.top(); y <= local.bottom(); ++y) {
        const auto* src = original.pixel(local.left(), y);
        auto* dst = pixels.pixel(local.left(), y);
        const auto bytes = static_cast<std::size_t>(local.width()) * pixel_bytes;
        std::copy(src, src + bytes, dst);
      }
    }
    return;
  }

  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* pixel = pixels.pixel(x, y);
      const auto selection_alpha = canvas.selection_alpha_at(QPoint(document_rect.x + x, document_rect.y + y));
      pixel[3] = static_cast<std::uint8_t>((static_cast<int>(pixel[3]) * static_cast<int>(selection_alpha)) / 255);
    }
  }
}

struct LayerCopyPixels {
  PixelBuffer pixels;
  QPoint origin;
  Rect document_rect;
  std::vector<LayerId> source_layer_ids;
};

struct LayerGroupingDestination {
  std::vector<Layer>* siblings{nullptr};
  std::size_t insert_index{0};
};

void collect_layer_names(const std::vector<Layer>& layers, std::set<std::string>& names) {
  for (const auto& layer : layers) {
    names.insert(layer.name());
    collect_layer_names(layer.children(), names);
  }
}

std::optional<LayerGroupingDestination> common_sibling_grouping_destination(
    std::vector<Layer>& layers,
    const std::vector<LayerId>& ids_top_to_bottom) {
  if (ids_top_to_bottom.empty()) {
    return std::nullopt;
  }

  std::vector<LayerSiblingLocation> locations;
  locations.reserve(ids_top_to_bottom.size());
  std::vector<Layer>* siblings = nullptr;
  for (const auto id : ids_top_to_bottom) {
    auto location = find_layer_location(layers, id);
    if (!location.has_value() || location->siblings == nullptr) {
      return std::nullopt;
    }
    if (siblings == nullptr) {
      siblings = location->siblings;
    } else if (siblings != location->siblings) {
      return std::nullopt;
    }
    locations.push_back(*location);
  }

  const auto topmost = std::max_element(locations.begin(), locations.end(), [](const auto& left, const auto& right) {
    return left.index < right.index;
  });
  const auto moved_below_topmost = std::count_if(locations.begin(), locations.end(), [topmost](const auto& location) {
    return location.index < topmost->index;
  });
  return LayerGroupingDestination{siblings, topmost->index - static_cast<std::size_t>(moved_below_topmost)};
}

std::string duplicate_name_stem(std::string_view name) {
  constexpr std::string_view kCopySuffix = " copy";
  constexpr std::string_view kNumberedCopySuffix = " copy ";
  const auto lower = ascii_lower_copy(name);
  if (lower.size() > kCopySuffix.size() && lower.ends_with(kCopySuffix)) {
    return std::string(name.substr(0, name.size() - kCopySuffix.size()));
  }

  const auto suffix_position = lower.rfind(kNumberedCopySuffix);
  if (suffix_position == std::string::npos || suffix_position == 0) {
    return std::string(name);
  }
  const auto number_position = suffix_position + kNumberedCopySuffix.size();
  if (number_position >= lower.size()) {
    return std::string(name);
  }

  bool suffix_is_number = true;
  for (auto index = number_position; index < lower.size(); ++index) {
    if (std::isdigit(static_cast<unsigned char>(lower[index])) == 0) {
      suffix_is_number = false;
      break;
    }
  }
  return suffix_is_number ? std::string(name.substr(0, suffix_position)) : std::string(name);
}

std::string next_duplicate_layer_name(std::string_view source_name, const std::set<std::string>& existing_names) {
  const auto stem = duplicate_name_stem(source_name);
  for (int copy_index = 1;; ++copy_index) {
    auto candidate = stem + " copy";
    if (copy_index > 1) {
      candidate += " " + std::to_string(copy_index);
    }
    if (!existing_names.contains(candidate)) {
      return candidate;
    }
  }
}

void collect_referenced_smart_filter_records(const Layer& layer,
                                             const SmartFilterEffectsStore& store,
                                             std::vector<SmartFilterEffectsRecord>& records) {
  if (layer.smart_filter_stack() != nullptr) {
    const auto placed_uuid = smart_object_placed_uuid(layer);
    const auto already_collected = std::any_of(
        records.begin(), records.end(), [&](const SmartFilterEffectsRecord& record) {
          return record.placed_uuid == placed_uuid;
        });
    if (!already_collected) {
      if (const auto* record = store.find_unique(placed_uuid); record != nullptr) {
        records.push_back(*record);
      }
    }
  }
  for (const auto& child : layer.children()) {
    collect_referenced_smart_filter_records(child, store, records);
  }
}

std::vector<const Layer*> find_layers_top_to_bottom(const std::vector<Layer>& layers,
                                                    const std::vector<LayerId>& ids_top_to_bottom) {
  std::vector<const Layer*> found_layers;
  found_layers.reserve(ids_top_to_bottom.size());
  for (const auto id : ids_top_to_bottom) {
    if (const auto* layer = find_layer_in_tree(layers, id); layer != nullptr) {
      found_layers.push_back(layer);
    }
  }
  return found_layers;
}

bool has_visible_pixels(const PixelBuffer& pixels) {
  if (pixels.empty() || pixels.format().bit_depth != BitDepth::UInt8) {
    return false;
  }
  if (pixels.format().channels < 4) {
    return pixels.format().channels >= 3;
  }
  const auto channels = pixels.format().channels;
  for (std::size_t index = 3; index < pixels.data().size(); index += channels) {
    if (pixels.data()[index] != 0) {
      return true;
    }
  }
  return false;
}

std::optional<LayerCopyPixels> collect_layer_copy_pixels(const Document& document, const std::vector<LayerId>& ids,
                                                         const CanvasWidget& canvas) {
  if (ids.empty()) {
    return std::nullopt;
  }

  const std::set<LayerId> selected(ids.begin(), ids.end());
  std::vector<const Layer*> layers_to_copy;
  for (const auto& layer : document.layers()) {
    if (!selected.contains(layer.id()) || layer.kind() != LayerKind::Pixel || !layer.visible()) {
      continue;
    }
    layers_to_copy.push_back(&layer);
  }
  if (layers_to_copy.empty()) {
    return std::nullopt;
  }

  Rect copy_rect;
  if (canvas.selected_document_rect().has_value()) {
    copy_rect = to_core_rect(*canvas.selected_document_rect());
  } else {
    for (const auto* layer : layers_to_copy) {
      copy_rect = unite_rect(copy_rect, layer->bounds());
    }
  }
  copy_rect = intersect_copy_rect(copy_rect, Rect::from_size(document.width(), document.height()));
  if (copy_rect.empty()) {
    return std::nullopt;
  }

  PixelBuffer copied;
  if (layers_to_copy.size() == 1U) {
    copied = copy_pixels_from_layer(*layers_to_copy.front(), copy_rect, &canvas);
  } else {
    Document selected_document(document.width(), document.height(), document.format());
    for (const auto* layer : layers_to_copy) {
      selected_document.add_layer(*layer);
    }
    const auto image =
        qimage_from_document(selected_document, true).copy(QRect(copy_rect.x, copy_rect.y, copy_rect.width, copy_rect.height));
    copied = pixels_from_image_rgba(image);
    apply_selection_mask(copied, copy_rect, canvas);
  }

  if (!has_visible_pixels(copied)) {
    return std::nullopt;
  }

  LayerCopyPixels payload{std::move(copied), QPoint(copy_rect.x, copy_rect.y), copy_rect, {}};
  payload.source_layer_ids.reserve(layers_to_copy.size());
  for (const auto* layer : layers_to_copy) {
    payload.source_layer_ids.push_back(layer->id());
  }
  return payload;
}

}  // namespace

void MainWindow::clear_system_clipboard() {
  if (auto* clipboard = QApplication::clipboard(); clipboard != nullptr) {
    const QSignalBlocker blocker(clipboard);
    clipboard->clear();
    patchy_system_clipboard_signature_ = clipboard_image_signature(clipboard->image());
  }
}

void MainWindow::set_system_clipboard_image(const QImage& image) {
  if (auto* clipboard = QApplication::clipboard(); clipboard != nullptr) {
    const QSignalBlocker blocker(clipboard);
    clipboard->setImage(image);
    patchy_system_clipboard_signature_ = clipboard_image_signature(clipboard->image());
  }
}

void MainWindow::clear_internal_clipboard_on_external_change() {
  const auto current_signature = clipboard_image_signature(QApplication::clipboard()->image());
  if (patchy_system_clipboard_signature_.has_value() && current_signature == *patchy_system_clipboard_signature_) {
    return;
  }
  clipboard_.reset();
  patchy_system_clipboard_signature_.reset();
}

void MainWindow::cut_selection() {
  // With keyboard focus inside a color picker, Edit > Cut acts on its colors
  // (same routing rationale as the Palette panel below: a parallel picker
  // shortcut would be ambiguous with the application-context hotkeys).
  if (auto* picker = color_picker_ancestor_of(QApplication::focusWidget()); picker != nullptr) {
    bool cleared_custom_slot = false;
    const auto color = picker->cut_color_to_clipboard(cleared_custom_slot);
    statusBar()->showMessage(cleared_custom_slot ? tr("Cut custom color %1").arg(color.name())
                                                 : tr("Copied color %1").arg(color.name()));
    return;
  }
  auto ids = selected_layer_ids();
  if (ids.empty()) {
    const auto active = document().active_layer_id();
    if (active.has_value()) {
      ids.push_back(*active);
    }
  }
  if (ids.empty()) {
    show_status_error(tr("Select a layer to cut"));
    return;
  }

  const std::set<LayerId> selected(ids.begin(), ids.end());
  std::vector<LayerId> layers_to_cut;
  for (const auto& layer : document().layers()) {
    if (!selected.contains(layer.id()) || layer.kind() != LayerKind::Pixel || !layer.visible() ||
        layer_id_locks_image_pixels(layer.id())) {
      continue;
    }
    layers_to_cut.push_back(layer.id());
  }
  if (layers_to_cut.empty()) {
    if (std::any_of(ids.begin(), ids.end(), [this](LayerId id) { return layer_id_locks_image_pixels(id); })) {
      show_status_error(tr("Layer pixels are locked."));
      return;
    }
    clipboard_.reset();
    clear_system_clipboard();
    statusBar()->showMessage(tr("Selected layers are hidden or not editable; nothing cut"));
    return;
  }
  if (std::any_of(layers_to_cut.begin(), layers_to_cut.end(), [this](LayerId id) {
        const auto* layer = std::as_const(document()).find_layer(id);
        return layer != nullptr && layer_pixels_are_procedural(*layer);
      })) {
    show_status_error(
        tr("Rasterize Text, Smart Object, and Shape layers before editing their pixels"));
    return;
  }

  copy_selection();
  if (!clipboard_.has_value() || clipboard_->pixels.empty()) {
    return;
  }

  auto& doc = document();
  push_undo_snapshot(tr("Cut"));
  Rect affected;
  auto options = edit_options(*canvas_);
  for (const auto id : layers_to_cut) {
    auto* layer = doc.find_layer(id);
    if (layer == nullptr || layer->kind() != LayerKind::Pixel || !layer->visible()) {
      continue;
    }
    options.lock_transparent_pixels = layer_locks_transparent_pixels(*layer);
    affected = unite_rect(affected, patchy::clear_rect(doc, id, layer->bounds(), options));
  }
  if (!affected.empty()) {
    canvas_->document_changed(to_qrect(affected));
  }
  refresh_layer_list();
  refresh_layer_controls();
  statusBar()->showMessage(tr("Cut %1 layer(s)").arg(static_cast<qulonglong>(layers_to_cut.size())));
}

void MainWindow::copy_selection() {
  // With keyboard focus inside a color picker, Edit > Copy takes the picker's
  // current color (color mime + hex text).
  if (auto* picker = color_picker_ancestor_of(QApplication::focusWidget()); picker != nullptr) {
    statusBar()->showMessage(tr("Copied color %1").arg(picker->copy_color_to_clipboard().name()));
    return;
  }
  // With keyboard focus inside the Palette panel, Edit > Copy acts on the
  // selected swatch instead of the canvas (a parallel panel shortcut would make
  // Ctrl+C ambiguous and Qt would fire neither).
  if (palette_panel_ != nullptr && QApplication::focusWidget() != nullptr &&
      palette_panel_->isAncestorOf(QApplication::focusWidget())) {
    copy_selected_palette_color();
    return;
  }
  if (canvas_ != nullptr) {
    canvas_->finish_free_transform();
  }
  auto ids = selected_layer_ids();
  if (ids.empty()) {
    const auto active = document().active_layer_id();
    if (active.has_value()) {
      ids.push_back(*active);
    }
  }
  if (ids.empty()) {
    show_status_error(tr("Select a layer to copy"));
    return;
  }

  ids = root_drop_layer_ids(document().layers(), ids);
  if (ids.empty()) {
    show_status_error(tr("Select a layer to copy"));
    return;
  }

  const auto selected_layers = find_layers_top_to_bottom(document().layers(), ids);
  if (selected_layers.empty()) {
    show_status_error(tr("Select a layer to copy"));
    return;
  }

  const auto contains_non_pixel_layer =
      std::any_of(selected_layers.begin(), selected_layers.end(), [](const Layer* layer) {
        return layer != nullptr && layer->kind() != LayerKind::Pixel;
      });
  if (!canvas_->selected_document_rect().has_value() || contains_non_pixel_layer) {
    ClipboardPayload payload;
    payload.layers_top_to_bottom.reserve(selected_layers.size());
    for (const auto* layer : selected_layers) {
      payload.layers_top_to_bottom.push_back(*layer);
      collect_referenced_smart_object_sources(*layer, document().metadata().smart_objects,
                                              payload.smart_object_sources);
      collect_referenced_smart_filter_records(
          *layer, document().metadata().smart_filter_effects,
          payload.smart_filter_effect_records);
      collect_referenced_pattern_resources(*layer, document().metadata().patterns,
                                           payload.pattern_resources);
    }
    clipboard_ = std::move(payload);
    clear_system_clipboard();
    update_history(tr("Copy"));
    statusBar()->showMessage(tr("Copied %1 layer(s)").arg(static_cast<qulonglong>(selected_layers.size())));
    return;
  }

  const std::set<LayerId> selected(ids.begin(), ids.end());
  std::vector<const Layer*> layers_to_copy;
  for (const auto* layer : selected_layers) {
    if (layer == nullptr || !selected.contains(layer->id()) || layer->kind() != LayerKind::Pixel ||
        !layer->visible()) {
      continue;
    }
    layers_to_copy.push_back(layer);
  }
  if (layers_to_copy.empty()) {
    clipboard_.reset();
    clear_system_clipboard();
    statusBar()->showMessage(tr("Selected layers are hidden or not editable; nothing copied"));
    return;
  }

  Rect copy_rect;
  if (canvas_->selected_document_rect().has_value()) {
    copy_rect = to_core_rect(*canvas_->selected_document_rect());
  } else {
    for (const auto* layer : layers_to_copy) {
      copy_rect = unite_rect(copy_rect, layer->bounds());
    }
  }
  copy_rect = intersect_copy_rect(copy_rect, Rect::from_size(document().width(), document().height()));
  if (copy_rect.empty()) {
    clipboard_.reset();
    clear_system_clipboard();
    statusBar()->showMessage(tr("Nothing to copy"));
    return;
  }

  PixelBuffer copied;
  if (layers_to_copy.size() == 1U) {
    copied = copy_pixels_from_layer(*layers_to_copy.front(), copy_rect, canvas_);
  } else {
    Document selected_document(document().width(), document().height(), document().format());
    for (const auto* layer : layers_to_copy) {
      selected_document.add_layer(*layer);
    }
    const auto image =
        qimage_from_document(selected_document, true).copy(QRect(copy_rect.x, copy_rect.y, copy_rect.width, copy_rect.height));
    copied = pixels_from_image_rgba(image);
    apply_selection_mask(copied, copy_rect, *canvas_);
  }

  clipboard_ = ClipboardPayload{std::move(copied), QPoint(copy_rect.x, copy_rect.y)};
  set_system_clipboard_image(qimage_from_pixel_buffer(clipboard_->pixels));
  update_history(tr("Copy"));
  statusBar()->showMessage(
      tr("Copied %1 layer(s), %2 x %3 px")
          .arg(static_cast<qulonglong>(layers_to_copy.size()))
          .arg(copy_rect.width)
          .arg(copy_rect.height));
}

void MainWindow::copy_merged() {
  if (canvas_ != nullptr) {
    canvas_->finish_free_transform();
  }
  auto copy_rect = Rect::from_size(document().width(), document().height());
  if (canvas_->selected_document_rect().has_value()) {
    copy_rect = intersect_copy_rect(copy_rect, to_core_rect(*canvas_->selected_document_rect()));
  }
  if (copy_rect.empty()) {
    statusBar()->showMessage(tr("Nothing to copy"));
    return;
  }

  const auto image = qimage_from_document(document(), true).copy(QRect(copy_rect.x, copy_rect.y, copy_rect.width, copy_rect.height));
  clipboard_ = ClipboardPayload{pixels_from_image_rgba(image), QPoint(copy_rect.x, copy_rect.y)};
  set_system_clipboard_image(image);
  update_history(tr("Copy merged"));
  statusBar()->showMessage(tr("Copied merged %1 x %2 px").arg(copy_rect.width).arg(copy_rect.height));
}

void MainWindow::paste_clipboard() {
  // See copy_selection: focus inside a color picker routes the paste to it (a
  // clipboard color becomes the picker's current color).
  if (auto* picker = color_picker_ancestor_of(QApplication::focusWidget()); picker != nullptr) {
    if (const auto color = picker->paste_color_from_clipboard(); color.has_value()) {
      statusBar()->showMessage(tr("Pasted color %1").arg(color->name()));
    } else {
      show_status_error(tr("The clipboard does not contain a color"));
    }
    return;
  }
  // Focus inside the Palette panel routes the paste to the selected swatch.
  if (palette_panel_ != nullptr && QApplication::focusWidget() != nullptr &&
      palette_panel_->isAncestorOf(QApplication::focusWidget())) {
    paste_clipboard_color_to_palette();
    return;
  }
  if (canvas_ != nullptr) {
    canvas_->finish_free_transform();
  }
  if (clipboard_.has_value() && !clipboard_->layers_top_to_bottom.empty()) {
    auto& doc = document();
    const auto caches_available = std::all_of(
        clipboard_->layers_top_to_bottom.begin(), clipboard_->layers_top_to_bottom.end(),
        [&](const Layer& layer) {
          return smart_filter_records_available_for_clone(
              layer, doc.metadata().smart_filter_effects,
              &clipboard_->smart_filter_effect_records);
        });
    if (!caches_available) {
      show_status_error(
          tr("Smart Filter cache data could not be duplicated safely"));
      return;
    }
    std::set<std::string> existing_names;
    collect_layer_names(doc.layers(), existing_names);

    push_undo_snapshot(tr("Paste"));
    for (const auto& source : clipboard_->smart_object_sources) {
      doc.metadata().smart_objects.adopt(source);
    }
    for (const auto& resource : clipboard_->pattern_resources) {
      PatternResource adopted = resource;
      adopted.provenance = PatternProvenance::Authored;  // target file has no raw block for it
      doc.metadata().patterns.adopt(adopted);
    }
    for (auto it = clipboard_->layers_top_to_bottom.rbegin(); it != clipboard_->layers_top_to_bottom.rend(); ++it) {
      auto pasted = clone_layer_tree_with_document_ids(
          doc, *it, &clipboard_->smart_filter_effect_records);
      if (!pasted.has_value()) {
        undo();
        show_status_error(
            tr("Smart Filter cache data could not be duplicated safely"));
        return;
      }
      pasted->set_name(next_duplicate_layer_name(it->name(), existing_names));
      existing_names.insert(pasted->name());
      doc.add_layer(std::move(*pasted));
    }
    refresh_layer_list();
    refresh_layer_controls();
    canvas_->document_changed();
    statusBar()->showMessage(
        tr("Pasted %1 layer(s)").arg(static_cast<qulonglong>(clipboard_->layers_top_to_bottom.size())));
    return;
  }

  PixelBuffer pixels;
  QPoint origin;
  if (clipboard_.has_value() && !clipboard_->pixels.empty()) {
    pixels = clipboard_->pixels;
    origin = clipboard_->origin;
  } else {
    const auto image = QApplication::clipboard()->image();
    if (image.isNull()) {
      show_status_error(tr("Clipboard does not contain an image"));
      return;
    }
    pixels = pixels_from_image_rgba(image);
    origin = QPoint(std::max(0, (document().width() - pixels.width()) / 2),
                    std::max(0, (document().height() - pixels.height()) / 2));
  }

  push_undo_snapshot(tr("Paste"));
  Layer pasted(document().allocate_layer_id(), "Pasted Layer", std::move(pixels));
  pasted.set_bounds(Rect{origin.x(), origin.y(), pasted.pixels().width(), pasted.pixels().height()});
  document().add_layer(std::move(pasted));
  if (move_tool_action_ != nullptr) {
    move_tool_action_->trigger();
  } else {
    current_tool_ = CanvasTool::Move;
    canvas_->set_tool(current_tool_);
  }
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
  statusBar()->showMessage(tr("Pasted as new layer"));
}

void MainWindow::transform_active_layer_dialog() {
  if (canvas_ == nullptr) {
    show_status_error(tr("Select a pixel layer to transform"));
    return;
  }
  if (canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) != nullptr) {
    finish_active_text_editor();
  }
  if (const auto active = document().active_layer_id();
      active.has_value() && layer_id_locks_position(*active)) {
    show_status_error(tr("Layer position is locked."));
    return;
  }
  if (!canvas_->begin_free_transform()) {
    show_status_error(tr("Select a pixel layer to transform"));
  }
}

void MainWindow::warp_transform_active_layer() {
  if (canvas_ == nullptr) {
    show_status_error(tr("Select a pixel layer to warp"));
    return;
  }
  if (canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) != nullptr) {
    finish_active_text_editor();
  }
  if (const auto active = document().active_layer_id();
      active.has_value() && layer_id_locks_position(*active)) {
    show_status_error(tr("Layer position is locked."));
    return;
  }
  canvas_->begin_warp_transform();  // refusal reasons land in the status bar
  refresh_options_bar();
}

void MainWindow::add_layer() {
  auto& doc = document();
  const auto name = default_new_layer_name(doc);
  auto anchor_id = doc.active_layer_id();
  const auto selected_ids = selected_layer_ids();
  if (!selected_ids.empty()) {
    anchor_id = selected_ids.front();
  }

  push_undo_snapshot(tr("New layer"));
  auto layer_pixels =
      make_solid_pixels(doc.width(), doc.height(), QColor(0, 0, 0, 0), PixelFormat::rgba8());
  Layer layer(doc.allocate_layer_id(), name.toStdString(), std::move(layer_pixels));
  const auto layer_id = layer.id();
  layer.set_opacity(1.0F);
  layer.set_blend_mode(BlendMode::Normal);
  insert_layer_after_anchor(doc, std::move(layer), anchor_id);
  doc.set_active_layer(layer_id);
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
}

void MainWindow::create_layer_folder() {
  create_layer_folder_from_layers(selected_layer_ids());
}

void MainWindow::create_layer_folder_from_layers(std::vector<LayerId> ids) {
  auto& doc = document();
  std::set<std::string> existing_names;
  collect_layer_names(doc.layers(), existing_names);

  int suffix = 1;
  std::string name;
  do {
    name = tr("Folder %1").arg(suffix++).toStdString();
  } while (existing_names.contains(name));

  auto grouped_ids = root_drop_layer_ids(doc.layers(), ids);
  const auto destination = common_sibling_grouping_destination(doc.layers(), grouped_ids);

  push_undo_snapshot(tr("New folder"));
  Layer folder(doc.allocate_layer_id(), name, LayerKind::Group);
  const auto folder_id = folder.id();
  folder.set_blend_mode(BlendMode::PassThrough);
  if (!grouped_ids.empty()) {
    std::vector<Layer> grouped_top_to_bottom;
    grouped_top_to_bottom.reserve(grouped_ids.size());
    for (const auto id : grouped_ids) {
      if (auto grouped = take_layer_from_tree(doc.layers(), id); grouped.has_value()) {
        grouped_top_to_bottom.push_back(std::move(*grouped));
      }
    }
    for (auto it = grouped_top_to_bottom.rbegin(); it != grouped_top_to_bottom.rend(); ++it) {
      folder.add_child(std::move(*it));
    }
  }

  auto* siblings = destination.has_value() && destination->siblings != nullptr ? destination->siblings : &doc.layers();
  const auto insert_index =
      destination.has_value() ? std::min(destination->insert_index, siblings->size()) : siblings->size();
  siblings->insert(siblings->begin() + static_cast<std::ptrdiff_t>(insert_index), std::move(folder));
  doc.set_active_layer(folder_id);
  refresh_layer_list();
  refresh_layer_controls();
  refresh_document_info();
  canvas_->document_changed();
  statusBar()->showMessage(tr("Created folder"));
}

void MainWindow::layer_via_copy() {
  if (canvas_ != nullptr) {
    canvas_->finish_free_transform();
  }
  const auto ids = selected_or_active_layer_ids();
  const auto payload = collect_layer_copy_pixels(document(), ids, *canvas_);
  if (!payload.has_value()) {
    statusBar()->showMessage(tr("Nothing visible to copy to a new layer"));
    return;
  }

  auto& doc = document();
  push_undo_snapshot(tr("Layer via copy"));
  Layer copied(doc.allocate_layer_id(), tr("Layer Via Copy").toStdString(), payload->pixels);
  copied.set_bounds(Rect{payload->origin.x(), payload->origin.y(), copied.pixels().width(), copied.pixels().height()});
  doc.add_layer(std::move(copied));
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed(to_qrect(payload->document_rect));
  statusBar()->showMessage(tr("Copied selection to a new layer"));
}

void MainWindow::layer_via_cut() {
  if (canvas_ != nullptr) {
    canvas_->finish_free_transform();
  }
  const auto ids = selected_or_active_layer_ids();
  auto payload = collect_layer_copy_pixels(document(), ids, *canvas_);
  if (!payload.has_value()) {
    statusBar()->showMessage(tr("Nothing visible to cut to a new layer"));
    return;
  }
  if (std::any_of(payload->source_layer_ids.begin(), payload->source_layer_ids.end(),
                  [this](LayerId id) { return layer_id_locks_image_pixels(id); })) {
    show_status_error(tr("Layer pixels are locked."));
    return;
  }
  if (std::any_of(payload->source_layer_ids.begin(), payload->source_layer_ids.end(), [this](LayerId id) {
        const auto* layer = std::as_const(document()).find_layer(id);
        return layer != nullptr && layer_pixels_are_procedural(*layer);
      })) {
    show_status_error(
        tr("Rasterize Text, Smart Object, and Shape layers before editing their pixels"));
    return;
  }

  auto& doc = document();
  push_undo_snapshot(tr("Layer via cut"));
  auto options = edit_options(*canvas_);
  Rect affected = payload->document_rect;
  const auto selected_rect = canvas_->selected_document_rect();
  for (const auto id : payload->source_layer_ids) {
    auto* layer = doc.find_layer(id);
    if (layer == nullptr || layer->kind() != LayerKind::Pixel || !layer->visible()) {
      continue;
    }
    options.lock_transparent_pixels = layer_locks_transparent_pixels(*layer);
    const auto clear_area = selected_rect.has_value() ? to_core_rect(*selected_rect) : layer->bounds();
    affected = unite_rect(affected, patchy::clear_rect(doc, id, clear_area, options));
  }

  Layer cut_layer(doc.allocate_layer_id(), tr("Layer Via Cut").toStdString(), std::move(payload->pixels));
  cut_layer.set_bounds(Rect{payload->origin.x(), payload->origin.y(), cut_layer.pixels().width(), cut_layer.pixels().height()});
  doc.add_layer(std::move(cut_layer));
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed(to_qrect(affected));
  statusBar()->showMessage(tr("Cut selection to a new layer"));
}

void MainWindow::add_layer_mask() {
  if (canvas_ == nullptr) {
    return;
  }
  auto& doc = document();
  const auto active = doc.active_layer_id();
  if (!active.has_value()) {
    show_status_error(tr("Select a pixel or adjustment layer before adding a mask"));
    return;
  }
  auto* layer = doc.find_layer(*active);
  if (layer == nullptr || (layer->kind() != LayerKind::Pixel && layer->kind() != LayerKind::Adjustment)) {
    show_status_error(tr("Select a pixel or adjustment layer before adding a mask"));
    return;
  }
  if (layer_id_locks_image_pixels(*active)) {
    show_status_error(tr("Layer pixels are locked."));
    return;
  }

  const auto selection = canvas_->selected_document_region();
  const auto selection_rect = selection.boundingRect().intersected(QRect(0, 0, doc.width(), doc.height()));
  const auto from_selection = !selection.isEmpty() && !selection_rect.isEmpty();
  if (!from_selection && layer->mask().has_value()) {
    show_status_error(tr("Layer already has a mask"));
    return;
  }

  push_undo_snapshot(tr("Add layer mask"));
  const auto before = layer_render_bounds(*layer);
  if (from_selection) {
    auto mask_pixels = selection_mask_pixels(*canvas_, selection_rect);
    layer->set_mask(LayerMask{to_core_rect(selection_rect), std::move(mask_pixels), 0, false});
  } else {
    PixelBuffer mask_pixels(doc.width(), doc.height(), PixelFormat::gray8());
    mask_pixels.clear(255);
    layer->set_mask(LayerMask{Rect{0, 0, doc.width(), doc.height()}, std::move(mask_pixels), 255, false});
  }
  const auto after = layer_render_bounds(*layer);
  canvas_->invalidate_mask_display();
  canvas_->document_changed(to_qrect(unite_rect(before, after)));
  refresh_layer_list();
  set_layer_edit_target_ui(CanvasWidget::LayerEditTarget::Mask, false);
  statusBar()->showMessage(from_selection
                               ? tr("Added layer mask from selection")
                               : tr("Added layer mask. Paint with black to hide and white to reveal."));
}

void MainWindow::delete_active_layer_mask() {
  auto& doc = document();
  const auto active = doc.active_layer_id();
  if (!active.has_value()) {
    return;
  }
  auto* layer = doc.find_layer(*active);
  if (layer == nullptr || !layer->mask().has_value()) {
    show_status_error(tr("Active layer has no mask"));
    return;
  }
  if (layer_id_locks_image_pixels(*active)) {
    show_status_error(tr("Layer pixels are locked."));
    return;
  }

  push_undo_snapshot(tr("Delete layer mask"));
  const auto affected = layer_render_bounds(*layer);
  layer->clear_mask();
  layer->metadata().erase(kLayerMetadataMaskLinked);
  canvas_->invalidate_mask_display();
  canvas_->document_changed(to_qrect(affected));
  refresh_layer_list();
  set_layer_edit_target_ui(CanvasWidget::LayerEditTarget::Content, false);
  statusBar()->showMessage(tr("Deleted layer mask"));
}

void MainWindow::set_active_layer_mask_linked(bool linked) {
  auto& doc = document();
  const auto active = doc.active_layer_id();
  if (!active.has_value()) {
    return;
  }
  auto* layer = doc.find_layer(*active);
  if (layer == nullptr || !layer->mask().has_value()) {
    return;
  }
  if (layer_id_locks_image_pixels(*active)) {
    show_status_error(tr("Layer pixels are locked."));
    refresh_layer_controls();
    return;
  }
  if (layer_mask_linked(*layer) == linked) {
    refresh_layer_controls();
    return;
  }

  push_undo_snapshot(linked ? tr("Link layer mask") : tr("Unlink layer mask"));
  set_layer_mask_linked(*layer, linked);
  refresh_layer_list();
  refresh_layer_controls();
  statusBar()->showMessage(linked ? tr("Layer and mask linked") : tr("Layer and mask unlinked"));
}

bool MainWindow::set_smart_filter_mask_edit_target_ui(
    LayerId layer_id, CanvasWidget::MaskDisplayMode mode, bool announce) {
  if (canvas_ == nullptr || !has_active_document()) {
    return false;
  }
  const auto* layer = std::as_const(document()).find_layer(layer_id);
  const auto* stack = layer != nullptr ? layer->smart_filter_stack() : nullptr;
  if (layer == nullptr || stack == nullptr ||
      stack->support != SmartFilterStackSupport::Supported ||
      stack->mask.pixels.empty() ||
      !smart_filter_mask_document_editing_supported(
          document().width(), document().height()) ||
      (!smart_object_lock_reason(*layer).empty() &&
       smart_object_lock_reason(*layer) != "external")) {
    show_status_error(
        tr("This Smart Filter mask can only be preserved, not edited"));
    return false;
  }
  auto pixels = materialize_smart_filter_mask(
      stack->mask, document().width(), document().height());
  if (!pixels.has_value()) {
    show_status_error(tr("Could not prepare this Smart Filter mask"));
    return false;
  }
  document().set_active_layer(layer_id);
  if (!canvas_->set_smart_filter_mask_edit_target(
          layer_id, std::move(*pixels), mode)) {
    show_status_error(tr("Could not edit this Smart Filter mask"));
    return false;
  }
  restyle_layer_rows(layer_list_);
  update_layer_target_styles(layer_list_, document().active_layer_id(),
                             CanvasWidget::LayerEditTarget::SmartFilterMask);
  refresh_layer_controls();
  refresh_channel_panel();
  update_document_action_state();
  if (announce) {
    statusBar()->showMessage(tr("Editing Smart Filter mask"));
  }
  return true;
}

void MainWindow::set_layer_edit_target_ui(CanvasWidget::LayerEditTarget target, bool announce) {
  if (canvas_ == nullptr) {
    return;
  }
  const auto previous_target = canvas_->layer_edit_target();
  if (target == CanvasWidget::LayerEditTarget::Mask) {
    const auto active = document().active_layer_id();
    const auto* layer = active.has_value() ? document().find_layer(*active) : nullptr;
    if (layer == nullptr || !layer->mask().has_value()) {
      target = CanvasWidget::LayerEditTarget::Content;
    }
  } else if (target == CanvasWidget::LayerEditTarget::SmartFilterMask &&
             !canvas_->editing_smart_filter_mask()) {
    target = CanvasWidget::LayerEditTarget::Content;
  } else if (target == CanvasWidget::LayerEditTarget::VectorMask) {
    const auto active = document().active_layer_id();
    const auto* layer = active.has_value() ? document().find_layer(*active) : nullptr;
    if (layer == nullptr || layer->vector_mask() == nullptr) {
      target = CanvasWidget::LayerEditTarget::Content;
    }
  }
  const bool leaving_document_channel = previous_target == CanvasWidget::LayerEditTarget::DocumentChannel ||
                                        previous_target == CanvasWidget::LayerEditTarget::ComponentRed ||
                                        previous_target == CanvasWidget::LayerEditTarget::ComponentGreen ||
                                        previous_target == CanvasWidget::LayerEditTarget::ComponentBlue;
  if (leaving_document_channel ||
      (target == CanvasWidget::LayerEditTarget::Content &&
       canvas_->mask_display_mode() == CanvasWidget::MaskDisplayMode::Grayscale)) {
    canvas_->set_mask_display_mode(CanvasWidget::MaskDisplayMode::None);
  }
  canvas_->set_layer_edit_target(target);
  canvas_->update();
  update_layer_target_styles(layer_list_, document().active_layer_id(), target);
  refresh_layer_controls();
  refresh_channel_panel();
  update_document_action_state();
  if (announce) {
    statusBar()->showMessage(
        target == CanvasWidget::LayerEditTarget::Mask
            ? tr("Editing layer mask")
            : target == CanvasWidget::LayerEditTarget::SmartFilterMask
                  ? tr("Editing Smart Filter mask")
                  : target == CanvasWidget::LayerEditTarget::VectorMask
                        ? tr("Editing vector mask")
                        : tr("Editing layer pixels"));
  }
}

void MainWindow::set_mask_overlay_shown(bool shown) {
  if (canvas_ == nullptr) {
    return;
  }
  const auto smart_target = canvas_->editing_smart_filter_mask();
  if (shown && canvas_->layer_edit_target() != CanvasWidget::LayerEditTarget::Mask &&
      !smart_target) {
    const auto active = std::as_const(document()).active_layer_id();
    const auto* layer = active.has_value()
                            ? std::as_const(document()).find_layer(*active)
                            : nullptr;
    if (layer != nullptr && layer->mask().has_value()) {
      set_layer_edit_target_ui(CanvasWidget::LayerEditTarget::Mask, false);
    } else if (active.has_value()) {
      static_cast<void>(set_smart_filter_mask_edit_target_ui(
          *active, CanvasWidget::MaskDisplayMode::Overlay, false));
    }
  }
  canvas_->set_mask_display_mode(shown ? CanvasWidget::MaskDisplayMode::Overlay
                                       : CanvasWidget::MaskDisplayMode::None);
  refresh_layer_controls();
  statusBar()->showMessage(shown ? tr("Mask overlay shown. Red marks the areas the mask hides.")
                                 : tr("Mask overlay hidden"));
}

// The menu twin of Alt-clicking the mask thumbnail: shows the mask itself in
// grayscale and selects it for editing with the paint tools.
void MainWindow::set_layer_mask_view_shown(bool shown) {
  if (canvas_ == nullptr) {
    return;
  }
  const auto active = document().active_layer_id();
  const auto* layer = active.has_value() ? document().find_layer(*active) : nullptr;
  const auto smart_target = canvas_->editing_smart_filter_mask();
  const auto* smart_filters = layer != nullptr ? layer->smart_filter_stack() : nullptr;
  const auto has_smart_mask = smart_filters != nullptr &&
                              smart_filters->support ==
                                  SmartFilterStackSupport::Supported &&
                              !smart_filters->mask.pixels.empty();
  if (layer == nullptr ||
      (!std::as_const(*layer).mask().has_value() && !has_smart_mask)) {
    refresh_layer_controls();
    show_status_error(tr("Active layer has no mask"));
    return;
  }
  if (shown) {
    if (!smart_target && std::as_const(*layer).mask().has_value()) {
      set_layer_edit_target_ui(CanvasWidget::LayerEditTarget::Mask, false);
    } else if (!smart_target && active.has_value() &&
               !set_smart_filter_mask_edit_target_ui(
                   *active, CanvasWidget::MaskDisplayMode::Grayscale,
                   false)) {
      return;
    }
    canvas_->set_mask_display_mode(CanvasWidget::MaskDisplayMode::Grayscale);
    statusBar()->showMessage(
        smart_target || canvas_->editing_smart_filter_mask()
            ? tr("Showing the Smart Filter mask. Alt-click the mask thumbnail to return.")
            : tr("Showing the layer mask. Alt-click the mask thumbnail to return."));
  } else {
    canvas_->set_mask_display_mode(CanvasWidget::MaskDisplayMode::None);
    statusBar()->showMessage(canvas_->editing_smart_filter_mask()
                                 ? tr("Editing Smart Filter mask")
                                 : tr("Editing layer mask"));
  }
  refresh_layer_controls();
}

void MainWindow::set_active_layer_mask_disabled(bool disabled) {
  auto& doc = document();
  const auto active = doc.active_layer_id();
  auto* layer = active.has_value() ? doc.find_layer(*active) : nullptr;
  if (canvas_ != nullptr && canvas_->editing_smart_filter_mask() &&
      active.has_value() && canvas_->smart_filter_mask_owner_id() == active) {
    set_smart_filter_mask_enabled(*active, !disabled);
    return;
  }
  if (layer == nullptr || !layer->mask().has_value()) {
    show_status_error(tr("Active layer has no mask"));
    refresh_layer_controls();
    return;
  }
  if (active.has_value() && layer_id_locks_image_pixels(*active)) {
    show_status_error(tr("Layer pixels are locked."));
    refresh_layer_controls();
    return;
  }
  if (layer->mask()->disabled == disabled) {
    refresh_layer_controls();
    return;
  }

  push_undo_snapshot(disabled ? tr("Disable layer mask") : tr("Enable layer mask"));
  layer->mask()->disabled = disabled;
  canvas_->document_changed(to_qrect(layer->bounds()));
  // The mask overlay spans the whole canvas, so a partial repaint of the layer
  // bounds is not enough when it appears or disappears.
  canvas_->update();
  refresh_layer_list();
  refresh_layer_controls();
  statusBar()->showMessage(disabled ? tr("Layer mask disabled") : tr("Layer mask enabled"));
}

void MainWindow::invert_active_layer_mask() {
  auto& doc = document();
  const auto active = doc.active_layer_id();
  if (canvas_ != nullptr && canvas_->editing_smart_filter_mask() &&
      active.has_value() && canvas_->smart_filter_mask_owner_id() == active) {
    static_cast<void>(canvas_->invert_smart_filter_mask(
        tr("Invert Smart Filter mask")));
    return;
  }
  auto* layer = active.has_value() ? doc.find_layer(*active) : nullptr;
  if (layer == nullptr || !layer->mask().has_value()) {
    show_status_error(tr("Active layer has no mask"));
    return;
  }
  if (active.has_value() && layer_id_locks_image_pixels(*active)) {
    show_status_error(tr("Layer pixels are locked."));
    return;
  }

  push_undo_snapshot(tr("Invert layer mask"));
  auto& mask = *layer->mask();
  mask.default_color = static_cast<std::uint8_t>(255 - mask.default_color);
  if (!mask.pixels.empty()) {
    for (auto& value : mask.pixels.data()) {
      value = static_cast<std::uint8_t>(255 - value);
    }
  }
  const auto dirty = unite_rect(layer_render_bounds(*layer), mask.bounds.empty() ? layer->bounds() : mask.bounds);
  canvas_->invalidate_mask_display();
  canvas_->document_changed(to_qrect(dirty));
  refresh_layer_list();
  refresh_layer_controls();
  statusBar()->showMessage(tr("Inverted layer mask"));
}

void MainWindow::apply_active_layer_mask() {
  auto& doc = document();
  const auto active = doc.active_layer_id();
  auto* layer = active.has_value() ? doc.find_layer(*active) : nullptr;
  if (layer == nullptr || !layer->mask().has_value()) {
    show_status_error(tr("Active layer has no mask"));
    return;
  }
  if (active.has_value() && layer_id_locks_image_pixels(*active)) {
    show_status_error(tr("Layer pixels are locked."));
    return;
  }
  if (layer_pixels_are_procedural(*layer)) {
    show_status_error(
        tr("Rasterize Text, Smart Object, and Shape layers before editing their pixels"));
    return;
  }
  const auto& source_pixels = std::as_const(*layer).pixels();
  if (layer->kind() != LayerKind::Pixel || source_pixels.format().bit_depth != BitDepth::UInt8 ||
      source_pixels.format().channels < 3) {
    show_status_error(tr("Apply mask supports editable 8-bit pixel layers"));
    return;
  }

  push_undo_snapshot(tr("Apply layer mask"));
  auto& pixels = layer->pixels();
  const auto bounds = layer->bounds();
  const auto channels = pixels.format().channels;
  if (channels >= 4) {
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        auto* px = pixels.pixel(x, y);
        const auto mask_alpha = layer_mask_value_at(*layer, bounds.x + x, bounds.y + y);
        px[3] = static_cast<std::uint8_t>((static_cast<int>(px[3]) * static_cast<int>(mask_alpha)) / 255);
      }
    }
  } else {
    PixelBuffer rgba(pixels.width(), pixels.height(), PixelFormat::rgba8());
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        const auto* src = pixels.pixel(x, y);
        auto* dst = rgba.pixel(x, y);
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
        dst[3] = layer_mask_value_at(*layer, bounds.x + x, bounds.y + y);
      }
    }
    pixels = std::move(rgba);
  }
  layer->clear_mask();
  layer->metadata().erase(kLayerMetadataMaskLinked);
  if (canvas_ != nullptr) {
    canvas_->set_layer_edit_target(CanvasWidget::LayerEditTarget::Content);
    canvas_->invalidate_mask_display();
    canvas_->document_changed(to_qrect(bounds));
  }
  refresh_layer_list();
  refresh_layer_controls();
  statusBar()->showMessage(tr("Applied layer mask"));
}

void MainWindow::duplicate_active_layer() {
  if (canvas_ != nullptr) {
    canvas_->finish_free_transform();
  }
  duplicate_layers(selected_or_active_layer_ids());
}

void MainWindow::duplicate_layers(std::vector<LayerId> ids) {
  if (canvas_ != nullptr) {
    canvas_->finish_free_transform();
  }
  ids = root_drop_layer_ids(document().layers(), ids);
  if (ids.empty()) {
    return;
  }

  auto& doc = document();
  const auto caches_available = std::all_of(ids.begin(), ids.end(), [&](LayerId id) {
    const auto* source = std::as_const(doc).find_layer(id);
    return source == nullptr || smart_filter_records_available_for_clone(
                                    *source, doc.metadata().smart_filter_effects);
  });
  if (!caches_available) {
    show_status_error(
        tr("Smart Filter cache data could not be duplicated safely"));
    return;
  }
  std::set<std::string> existing_names;
  collect_layer_names(doc.layers(), existing_names);

  push_undo_snapshot(tr("Duplicate layer"));
  for (auto it = ids.rbegin(); it != ids.rend(); ++it) {
    const auto id = *it;
    const auto* source = doc.find_layer(id);
    if (source == nullptr) {
      continue;
    }

    auto duplicate = clone_layer_tree_with_document_ids(doc, *source);
    if (!duplicate.has_value()) {
      undo();
      show_status_error(
          tr("Smart Filter cache data could not be duplicated safely"));
      return;
    }
    duplicate->set_name(next_duplicate_layer_name(source->name(), existing_names));
    existing_names.insert(duplicate->name());
    doc.add_layer(std::move(*duplicate));
  }
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
}

void MainWindow::rename_active_layer() {
  auto& doc = document();
  if (!doc.active_layer_id().has_value()) {
    return;
  }
  auto* layer = doc.find_layer(*doc.active_layer_id());
  if (layer == nullptr) {
    return;
  }

  const auto new_name = request_text_input(this, QStringLiteral("patchyRenameLayerDialog"), tr("Rename Layer"),
                                           tr("Name"), QString::fromStdString(layer->name()));
  if (!new_name.has_value() || new_name->trimmed().isEmpty()) {
    return;
  }

  push_undo_snapshot(tr("Rename layer"));
  layer->set_name(new_name->trimmed().toStdString());
  refresh_layer_list();
  refresh_layer_controls();
}

namespace {

// Materializes any user-library or legacy built-in pattern a style references
// into the document store, so previews and saves no longer depend on the
// application library. Store insertions are benign; unreferenced entries prune
// at PSD write.
void ensure_patterns_for_style(Document& doc, const LayerStyle& style,
                               const PatternLibrary& library,
                               const PatternStore* transient_patterns = nullptr) {
  std::vector<std::string> referenced;
  collect_referenced_pattern_ids(style, referenced);
  for (const auto& id : referenced) {
    if (doc.metadata().patterns.find(id) != nullptr) {
      continue;
    }
    if (transient_patterns != nullptr) {
      if (const auto* resource = transient_patterns->find(id); resource != nullptr) {
        auto authored = *resource;
        authored.provenance = PatternProvenance::Authored;
        doc.metadata().patterns.adopt(authored);
        continue;
      }
    }
    if (auto resource = library.resource(QString::fromStdString(id)); resource.has_value()) {
      resource->provenance = PatternProvenance::Authored;
      doc.metadata().patterns.adopt(*resource);
    } else if (auto bundled = bundled_pattern_resource(id); bundled.has_value()) {
      // Repair path when the library entry is gone: generated and photo
      // presets both re-materialize from the application bundle.
      doc.metadata().patterns.adopt(*bundled);
    }
  }
}

}  // namespace

void MainWindow::edit_active_layer_style() {
  // A layer-style dialog is itself a preview dialog. Never open a second one on
  // top of an existing one (e.g. by double-clicking another layer in the list
  // while one is open) -- the stacked nested event loops crash.
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
  if (canvas_ != nullptr &&
      canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) != nullptr) {
    finish_active_text_editor();
  }
  auto& doc = document();
  if (!doc.active_layer_id().has_value()) {
    return;
  }
  const auto layer_id = *doc.active_layer_id();
  auto* layer = doc.find_layer(layer_id);
  if (layer == nullptr) {
    return;
  }

  const auto original_opacity = layer->opacity();
  const auto original_fill_opacity = layer->fill_opacity();
  const auto original_blend_mode = layer->blend_mode();
  const auto original_style = layer->layer_style();
  const auto original_blend_if = layer->blend_if();
  const auto original_blend_if_payload = layer->raw_psd_blending_ranges();
  const auto original_blend_if_rgb_compatible = layer->blend_if_rgb_compatible();
  const auto original_patterns = doc.metadata().patterns;
  auto set_layer_style_settings = [original_blend_if, original_blend_if_payload,
                                   original_blend_if_rgb_compatible](Layer& target,
                                                                     const LayerStyleSettings& settings) {
    target.set_opacity(static_cast<float>(settings.opacity) / 100.0F);
    target.set_fill_opacity(static_cast<float>(settings.fill_opacity) / 100.0F);
    target.set_blend_mode(settings.blend_mode);
    target.layer_style() = settings.style;
    if (!settings.replace_unsupported_blend_if && settings.blend_if == original_blend_if) {
      target.set_blend_if_payload(original_blend_if_payload, original_blend_if_rgb_compatible);
    } else {
      (void)target.set_blend_if(settings.blend_if, settings.replace_unsupported_blend_if);
    }
  };
  auto apply_preview_settings = [this, &doc, layer_id, set_layer_style_settings,
                                 original_patterns](const LayerStyleSettings& settings) {
    auto* target = doc.find_layer(layer_id);
    if (target == nullptr) {
      return;
    }
    // Every preview starts from the original document store. The temporary
    // store may contain a manager-selected collision alias, so use it as a
    // fallback while materializing only patterns referenced by this preview.
    const auto available_patterns = doc.metadata().patterns;
    doc.metadata().patterns = original_patterns;
    ensure_patterns_for_style(doc, settings.style, pattern_library(), &available_patterns);
    set_layer_style_settings(*target, settings);
    if (canvas_ != nullptr) {
      canvas_->document_changed_async_preview();
    }
  };
  auto apply_committed_settings = [this, &doc, layer_id, set_layer_style_settings](
                                      const LayerStyleSettings& settings,
                                      const PatternStore* transient_patterns) {
    auto* target = doc.find_layer(layer_id);
    if (target == nullptr) {
      return;
    }
    ensure_patterns_for_style(doc, settings.style, pattern_library(), transient_patterns);
    const auto before = layer_render_bounds(*target);
    set_layer_style_settings(*target, settings);
    const auto after = layer_render_bounds(*target);
    if (canvas_ != nullptr) {
      canvas_->document_changed(to_qrect(unite_rect(before, after)));
    }
  };
  auto restore_original = [this, &doc, layer_id, original_opacity, original_fill_opacity,
                           original_blend_mode, original_style,
                           original_blend_if_payload, original_blend_if_rgb_compatible,
                           original_patterns] {
    doc.metadata().patterns = original_patterns;
    auto* target = doc.find_layer(layer_id);
    if (target == nullptr) {
      return;
    }
    const auto before = layer_render_bounds(*target);
    target->set_opacity(original_opacity);
    target->set_fill_opacity(original_fill_opacity);
    target->set_blend_mode(original_blend_mode);
    target->layer_style() = original_style;
    target->set_blend_if_payload(original_blend_if_payload, original_blend_if_rgb_compatible);
    const auto after = layer_render_bounds(*target);
    if (canvas_ != nullptr) {
      canvas_->document_changed(to_qrect(unite_rect(before, after)));
    }
  };

  // "Open as Image" requests from the nested Pattern Manager are deferred until the
  // dialog closes: add_document_session must never run while the preview-dialog edit
  // lock is held (its activation tail and the preview lambdas' captured canvas/document
  // would desync — see the comment in add_document_session).
  std::vector<std::pair<QString, PixelBuffer>> pending_pattern_images;
  auto queue_pattern_image = [this, &pending_pattern_images](const QString& name,
                                                             const PixelBuffer& tile) {
    pending_pattern_images.emplace_back(name, tile);
    // The main-window status bar stays visible behind the modal dialogs.
    statusBar()->showMessage(
        tr("\"%1\" will open as a new image when the Layer Style dialog closes").arg(name));
  };
  auto open_pending_pattern_images = [this, &pending_pattern_images] {
    for (auto& [name, tile] : pending_pattern_images) {
      Document image_document(tile.width(), tile.height(), PixelFormat::rgba8());
      image_document.add_pixel_layer(name.toStdString(), std::move(tile));
      add_document_session(std::move(image_document), name);
      auto& new_session = session();
      new_session.undo_stack.clear();
      new_session.redo_stack.clear();
      if (history_list_ != nullptr) {
        history_list_->clear();
      }
      update_history(tr("Open pattern as image"));
      statusBar()->showMessage(tr("Opened pattern \"%1\" as a new image").arg(name));
    }
    pending_pattern_images.clear();
  };

  auto preview_edit_lock = lock_preview_dialog_edits();
  const auto settings =
      request_layer_style_settings(this, *layer, apply_preview_settings, &doc.metadata().patterns,
                                   &pattern_library(), &style_library(), queue_pattern_image,
                                   &gradient_library(),
                                   RgbColor{static_cast<std::uint8_t>(canvas_->primary_color().red()),
                                            static_cast<std::uint8_t>(canvas_->primary_color().green()),
                                            static_cast<std::uint8_t>(canvas_->primary_color().blue())},
                                   RgbColor{static_cast<std::uint8_t>(canvas_->secondary_color().red()),
                                            static_cast<std::uint8_t>(canvas_->secondary_color().green()),
                                            static_cast<std::uint8_t>(canvas_->secondary_color().blue())});
  if (!settings.has_value()) {
    restore_original();
    preview_edit_lock.release();
    refresh_layer_list();
    refresh_layer_controls();
    open_pending_pattern_images();
    return;
  }

  const auto dialog_patterns = doc.metadata().patterns;
  restore_original();
  preview_edit_lock.release();
  push_undo_snapshot(tr("Layer style"));
  if (auto* target = doc.find_layer(layer_id); target != nullptr) {
    clear_layer_psd_style_source(*target);
  }
  apply_committed_settings(*settings, &dialog_patterns);
  refresh_layer_list();
  refresh_layer_controls();
  statusBar()->showMessage(tr("Updated layer style"));
  open_pending_pattern_images();
}

void MainWindow::copy_active_layer_style() {
  if (!has_active_document()) {
    return;
  }

  const auto ids = selected_or_active_layer_ids();
  if (ids.size() != 1U) {
    refresh_layer_style_action_states();
    return;
  }

  const auto* layer = document().find_layer(ids.front());
  if (layer == nullptr) {
    refresh_layer_style_action_states();
    return;
  }

  layer_style_clipboard_ = LayerStyleClipboard{
      layer->layer_style(),
      layer->blend_if_payload_status() == BlendIfPayloadStatus::Unsupported
          ? std::optional<LayerBlendIf>{}
          : std::optional<LayerBlendIf>{layer->blend_if()},
      {}};
  // Carry the referenced pattern tiles so a cross-document paste can embed them.
  std::vector<std::string> referenced_pattern_ids;
  collect_referenced_pattern_ids(layer_style_clipboard_->style, referenced_pattern_ids);
  for (const auto& pattern_id : referenced_pattern_ids) {
    if (const auto* resource = document().metadata().patterns.find(pattern_id); resource != nullptr) {
      layer_style_clipboard_->patterns.push_back(*resource);
    }
  }
  // The style clipboard carries modeled settings, not the source layer's raw
  // lfx2 bytes. Pasted custom Satin curves and contour anti-aliasing are
  // therefore normalized to the Linear contour that Patchy can regenerate.
  for (auto& satin : layer_style_clipboard_->style.satins) {
    satin.unsupported_contour_options = false;
  }
  update_history(tr("Copy layer style"));
  statusBar()->showMessage(tr("Copied layer style"));
  refresh_layer_style_action_states();
}

void MainWindow::paste_layer_style_to_selected_layers() {
  if (!has_active_document() || !layer_style_clipboard_.has_value()) {
    refresh_layer_style_action_states();
    return;
  }

  const auto ids = selected_or_active_layer_ids();
  std::vector<LayerId> targets;
  targets.reserve(ids.size());
  const auto& doc = document();
  for (const auto id : ids) {
    if (doc.find_layer(id) != nullptr) {
      targets.push_back(id);
    }
  }
  if (targets.empty()) {
    refresh_layer_style_action_states();
    return;
  }

  auto& mutable_doc = document();
  push_undo_snapshot(tr("Paste layer style"));
  for (const auto& resource : layer_style_clipboard_->patterns) {
    PatternResource adopted = resource;
    adopted.provenance = PatternProvenance::Authored;  // the target file has no raw block for it
    mutable_doc.metadata().patterns.adopt(adopted);
  }
  ensure_patterns_for_style(mutable_doc, layer_style_clipboard_->style, pattern_library());
  Rect affected;
  std::size_t pasted_count = 0;
  for (const auto id : targets) {
    auto* layer = mutable_doc.find_layer(id);
    if (layer == nullptr) {
      continue;
    }
    affected = unite_rect(affected, layer_render_bounds(*layer));
    clear_layer_psd_style_source(*layer);
    layer->layer_style() = layer_style_clipboard_->style;
    if (layer_style_clipboard_->blend_if.has_value()) {
      (void)layer->set_blend_if(*layer_style_clipboard_->blend_if, true);
    }
    affected = unite_rect(affected, layer_render_bounds(*layer));
    ++pasted_count;
  }

  refresh_layer_list();
  refresh_layer_controls();
  if (canvas_ != nullptr) {
    canvas_->document_changed(affected.empty() ? QRect() : to_qrect(affected));
  }
  statusBar()->showMessage(
      tr("Pasted layer style to %1 layer(s)").arg(static_cast<qulonglong>(pasted_count)));
}

void MainWindow::delete_selected_layer_styles() {
  if (!has_active_document()) {
    return;
  }

  const auto ids = selected_or_active_layer_ids();
  std::vector<LayerId> targets;
  targets.reserve(ids.size());
  const auto& doc = document();
  for (const auto id : ids) {
    const auto* layer = doc.find_layer(id);
    if (layer != nullptr && !layer->layer_style().empty()) {
      targets.push_back(id);
    }
  }
  if (targets.empty()) {
    refresh_layer_style_action_states();
    return;
  }

  auto& mutable_doc = document();
  push_undo_snapshot(tr("Delete layer style"));
  Rect affected;
  std::size_t deleted_count = 0;
  for (const auto id : targets) {
    auto* layer = mutable_doc.find_layer(id);
    if (layer == nullptr || layer->layer_style().empty()) {
      continue;
    }
    affected = unite_rect(affected, layer_render_bounds(*layer));
    clear_layer_psd_style_source(*layer);
    layer->layer_style() = {};
    affected = unite_rect(affected, layer_render_bounds(*layer));
    ++deleted_count;
  }

  refresh_layer_list();
  refresh_layer_controls();
  if (canvas_ != nullptr) {
    canvas_->document_changed(affected.empty() ? QRect() : to_qrect(affected));
  }
  statusBar()->showMessage(
      tr("Deleted layer style from %1 layer(s)").arg(static_cast<qulonglong>(deleted_count)));
}

void MainWindow::refresh_layer_style_action_states() {
  bool can_copy = false;
  bool can_paste = false;
  bool can_delete = false;
  if (has_active_document() && !preview_dialog_edit_locked()) {
    const auto ids = selected_or_active_layer_ids();
    int valid_layer_count = 0;
    for (const auto id : ids) {
      const auto* layer = document().find_layer(id);
      if (layer == nullptr) {
        continue;
      }
      ++valid_layer_count;
      can_delete = can_delete || !layer->layer_style().empty();
    }
    can_copy = ids.size() == 1U && valid_layer_count == 1;
    can_paste = layer_style_clipboard_.has_value() && valid_layer_count > 0;
  }

  if (layer_copy_style_action_ != nullptr) {
    layer_copy_style_action_->setEnabled(can_copy);
  }
  if (layer_paste_style_action_ != nullptr) {
    layer_paste_style_action_->setEnabled(can_paste);
  }
  if (layer_delete_style_action_ != nullptr) {
    layer_delete_style_action_->setEnabled(can_delete);
  }
}

void MainWindow::delete_active_layer() {
  delete_layers(selected_or_active_layer_ids());
}

void MainWindow::delete_layers(std::vector<LayerId> ids) {
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    refresh_layer_list();
    return;
  }
  ids = root_drop_layer_ids(document().layers(), ids);
  if (ids.empty()) {
    return;
  }
  auto& doc = document();
  push_undo_snapshot(tr("Delete layer"));
  for (const auto id : ids) {
    doc.remove_layer(id);
  }
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
}

void MainWindow::move_active_layer(int direction) {
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    refresh_layer_list();
    return;
  }
  const auto ids = selected_or_active_layer_ids();
  if (ids.empty() || direction == 0) {
    return;
  }

  auto& layers = document().layers();
  const std::set<LayerId> selected(ids.begin(), ids.end());
  push_undo_snapshot(tr("Move layer"));
  if (direction > 0) {
    for (int index = static_cast<int>(layers.size()) - 2; index >= 0; --index) {
      if (selected.contains(layers[static_cast<std::size_t>(index)].id()) &&
          !selected.contains(layers[static_cast<std::size_t>(index + 1)].id())) {
        std::iter_swap(layers.begin() + index, layers.begin() + index + 1);
      }
    }
  } else {
    for (int index = 1; index < static_cast<int>(layers.size()); ++index) {
      if (selected.contains(layers[static_cast<std::size_t>(index)].id()) &&
          !selected.contains(layers[static_cast<std::size_t>(index - 1)].id())) {
        std::iter_swap(layers.begin() + index, layers.begin() + index - 1);
      }
    }
  }
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
}

void MainWindow::show_layer_context_menu(QPoint position) {
  if (layer_list_ == nullptr || !has_active_document()) {
    return;
  }

  auto* item = layer_list_->itemAt(position);
  if (item != nullptr && !item->isSelected()) {
    layer_list_->clearSelection();
    layer_list_->setCurrentItem(item);
    item->setSelected(true);
  }

  const auto ids = selected_or_active_layer_ids();
  const auto has_layer = !ids.empty();
  const auto active_id = document().active_layer_id();
  // Const on purpose: the menu only reads the layer, and the non-const
  // mask()/smart_filter_stack() accessors bump revisions on access (AGENTS.md
  // "Reads must not bump layer revisions") — a plain right-click was
  // invalidating the layer's thumbnail and style-mask cache entries.
  const auto* active_layer = active_id.has_value() ? std::as_const(document()).find_layer(*active_id) : nullptr;
  const auto has_rasterizable_layer = std::any_of(ids.begin(), ids.end(), [this](LayerId id) {
    const auto* layer = document().find_layer(id);
    return layer != nullptr && !layer_id_locks_image_pixels(id) && layer_can_rasterize(*layer);
  });
  const auto has_rasterizable_layer_style = std::any_of(ids.begin(), ids.end(), [this](LayerId id) {
    const auto* layer = document().find_layer(id);
    return layer != nullptr && !layer_id_locks_image_pixels(id) && layer_can_rasterize_layer_style(*layer);
  });

  // The flat menu outgrew the screen (July 2026), so related actions live in
  // submenus now. Edit Layer Styles... deliberately stays the FIRST item, always.
  QMenu menu(this);
  menu.setObjectName(QStringLiteral("layerContextMenu"));
  if (layer_blending_options_action_ != nullptr) {
    layer_blending_options_action_->setEnabled(active_layer != nullptr);
    menu.addAction(layer_blending_options_action_);
  }
  QAction* edit_adjustment_action = nullptr;
  if (active_layer != nullptr && active_layer->kind() == LayerKind::Adjustment) {
    edit_adjustment_action = menu.addAction(simple_icon(QStringLiteral("ADJ"), QColor(190, 220, 255)),
                                            tr("Edit Adjustment..."));
  }
  QAction* warp_text_action = nullptr;
  if (active_layer != nullptr && layer_is_text(*active_layer)) {
    warp_text_action = menu.addAction(simple_icon(QStringLiteral("T"), QColor(190, 220, 255)),
                                      tr("Warp Text..."));
    warp_text_action->setObjectName(QStringLiteral("layerContextWarpTextAction"));
  }
  if (layer_clipping_mask_action_ != nullptr) {
    // Kept near the top so Create/Release Clipping Mask stays discoverable.
    refresh_layer_clipping_action_state();
    menu.addAction(layer_clipping_mask_action_);
  }
  refresh_layer_style_action_states();
  auto* style_menu = menu.addMenu(tr("Layer Style"));
  style_menu->setObjectName(QStringLiteral("layerContextStyleMenu"));
  if (layer_copy_style_action_ != nullptr) {
    style_menu->addAction(layer_copy_style_action_);
  }
  if (layer_paste_style_action_ != nullptr) {
    style_menu->addAction(layer_paste_style_action_);
  }
  if (layer_delete_style_action_ != nullptr) {
    style_menu->addAction(layer_delete_style_action_);
  }
  menu.addSeparator();
  auto* new_menu = menu.addMenu(tr("New"));
  new_menu->setObjectName(QStringLiteral("layerContextNewMenu"));
  auto* new_action = new_menu->addAction(simple_icon(QStringLiteral("new")), tr("New Layer"));
  auto* new_folder_action =
      new_menu->addAction(simple_icon(QStringLiteral("dir"), QColor(245, 205, 105)), tr("New Folder"));
  auto* new_adjustment_menu = new_menu->addMenu(simple_icon(QStringLiteral("ADJ"), QColor(190, 220, 255)),
                                                tr("New Adjustment Layer"));
  populate_new_adjustment_layer_menu(new_adjustment_menu);
  auto* duplicate_action = menu.addAction(simple_icon(QStringLiteral("dup")), tr("Duplicate Layer"));
  auto* rename_action = menu.addAction(simple_icon(QStringLiteral("RN")), tr("Rename Layer..."));
  auto* delete_action = menu.addAction(simple_icon(QStringLiteral("trash")), tr("Delete Layer"));
  menu.addSeparator();
  auto* merge_down_action =
      menu.addAction(simple_icon(QStringLiteral("merge"), QColor(160, 220, 255)), tr("Merge Down"));
  auto* merge_visible_action = menu.addAction(simple_icon(QStringLiteral("merge")), tr("Merge Visible to New Layer"));
  if (layer_rasterize_action_ != nullptr) {
    layer_rasterize_action_->setEnabled(has_rasterizable_layer);
    menu.addAction(layer_rasterize_action_);
  }
  if (layer_rasterize_layer_style_action_ != nullptr) {
    layer_rasterize_layer_style_action_->setEnabled(has_rasterizable_layer_style);
    menu.addAction(layer_rasterize_layer_style_action_);
  }
  {
    const bool is_smart_object = active_layer != nullptr && layer_is_smart_object(*active_layer);
    const auto* source = is_smart_object
                             ? document().metadata().smart_objects.find(smart_object_source_uuid(*active_layer))
                             : nullptr;
    const bool has_embedded_bytes = source != nullptr && source->file_bytes != nullptr;
    const auto smart_lock = is_smart_object ? smart_object_lock_reason(*active_layer) : std::string();
    const bool is_external = smart_lock == "external";
    const bool editable = has_embedded_bytes && smart_lock.empty();
    auto* smart_objects_menu = menu.addMenu(tr("Smart Objects"));
    smart_objects_menu->setObjectName(QStringLiteral("layerContextSmartObjectsMenu"));
    if (layer_convert_smart_object_action_ != nullptr) {
      layer_convert_smart_object_action_->setEnabled(has_layer);
      smart_objects_menu->addAction(layer_convert_smart_object_action_);
    }
    if (layer_smart_object_edit_action_ != nullptr) {
      // Linked (external) smart objects open their file from disk.
      layer_smart_object_edit_action_->setEnabled(editable || is_external);
      smart_objects_menu->addAction(layer_smart_object_edit_action_);
    }
    if (layer_smart_object_replace_action_ != nullptr) {
      layer_smart_object_replace_action_->setEnabled(editable);
      smart_objects_menu->addAction(layer_smart_object_replace_action_);
    }
    if (layer_smart_object_update_action_ != nullptr) {
      layer_smart_object_update_action_->setEnabled(is_external);
      smart_objects_menu->addAction(layer_smart_object_update_action_);
    }
    if (layer_smart_object_relink_action_ != nullptr) {
      layer_smart_object_relink_action_->setEnabled(is_external);
      smart_objects_menu->addAction(layer_smart_object_relink_action_);
    }
    if (layer_smart_object_embed_action_ != nullptr) {
      layer_smart_object_embed_action_->setEnabled(is_external);
      smart_objects_menu->addAction(layer_smart_object_embed_action_);
    }
    if (layer_smart_object_export_action_ != nullptr) {
      layer_smart_object_export_action_->setEnabled(has_embedded_bytes);
      smart_objects_menu->addAction(layer_smart_object_export_action_);
    }
    if (layer_smart_object_via_copy_action_ != nullptr) {
      layer_smart_object_via_copy_action_->setEnabled(editable);
      smart_objects_menu->addAction(layer_smart_object_via_copy_action_);
    }
    if (layer_smart_object_to_normal_action_ != nullptr) {
      layer_smart_object_to_normal_action_->setEnabled(is_smart_object && has_rasterizable_layer);
      smart_objects_menu->addSeparator();
      smart_objects_menu->addAction(layer_smart_object_to_normal_action_);
    }
  }
  menu.addSeparator();
  auto* visibility_action = menu.addAction(tr("Visible"));
  visibility_action->setCheckable(true);
  visibility_action->setChecked(active_layer == nullptr || active_layer->visible());
  auto* lock_menu = menu.addMenu(tr("Lock"));
  const auto all_selected_have_lock = [this, &ids](LayerLockFlags flag) {
    return !ids.empty() && std::all_of(ids.begin(), ids.end(), [this, flag](LayerId id) {
      const auto* layer = document().find_layer(id);
      return layer != nullptr && (layer_lock_flags(*layer) & flag) == flag;
    });
  };
  auto* transparent_lock_action = lock_menu->addAction(tr("Lock Transparent Pixels"));
  transparent_lock_action->setCheckable(true);
  transparent_lock_action->setChecked(all_selected_have_lock(kLayerLockTransparentPixels));
  auto* image_lock_action = lock_menu->addAction(tr("Lock Image Pixels"));
  image_lock_action->setCheckable(true);
  image_lock_action->setChecked(all_selected_have_lock(kLayerLockImagePixels));
  auto* position_lock_action = lock_menu->addAction(tr("Lock Position"));
  position_lock_action->setCheckable(true);
  position_lock_action->setChecked(all_selected_have_lock(kLayerLockPosition));
  lock_menu->addSeparator();
  auto* all_lock_action = lock_menu->addAction(tr("Lock All"));
  all_lock_action->setCheckable(true);
  all_lock_action->setChecked(all_selected_have_lock(kLayerLockAll));
  auto* select_opaque_action = menu.addAction(tr("Load Layer Transparency"));
  const auto* active_smart_filters =
      active_layer != nullptr ? active_layer->smart_filter_stack() : nullptr;
  const auto has_editable_smart_mask =
      active_smart_filters != nullptr &&
      active_smart_filters->support == SmartFilterStackSupport::Supported &&
      !active_smart_filters->mask.pixels.empty() &&
      smart_filter_mask_document_editing_supported(
          document().width(), document().height());
  const auto editing_smart_mask =
      active_layer != nullptr && canvas_ != nullptr &&
      canvas_->editing_smart_filter_mask() &&
      canvas_->smart_filter_mask_owner_id() == active_layer->id();
  const auto smart_mask_context =
      editing_smart_mask ||
      (active_layer != nullptr && !active_layer->mask().has_value() &&
       has_editable_smart_mask);
  auto* mask_menu = menu.addMenu(tr("Layer Mask"));
  mask_menu->setObjectName(QStringLiteral("layerContextMaskMenu"));
  auto* add_mask_action = mask_menu->addAction(simple_icon(QStringLiteral("mask"), QColor(210, 220, 230)),
                                               tr("Add Layer Mask"));
  auto* edit_mask_action = mask_menu->addAction(simple_icon(QStringLiteral("mask"), QColor(150, 205, 255)),
                                                tr("Edit Layer Mask"));
  edit_mask_action->setCheckable(true);
  edit_mask_action->setChecked(
      canvas_ != nullptr &&
      ((active_layer != nullptr && active_layer->mask().has_value() &&
        canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::Mask) ||
       editing_smart_mask));
  auto* overlay_mask_action = mask_menu->addAction(simple_icon(QStringLiteral("mask"), QColor(255, 120, 120)),
                                                   tr("Show Mask Overlay"));
  overlay_mask_action->setCheckable(true);
  overlay_mask_action->setChecked(
      canvas_ != nullptr &&
      (canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::Mask ||
       editing_smart_mask) &&
      canvas_->mask_display_mode() == CanvasWidget::MaskDisplayMode::Overlay);
  auto* view_mask_action = mask_menu->addAction(simple_icon(QStringLiteral("mask"), QColor(235, 235, 235)),
                                                tr("View Layer Mask"));
  view_mask_action->setCheckable(true);
  view_mask_action->setChecked(
      canvas_ != nullptr &&
      (canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::Mask ||
       editing_smart_mask) &&
      canvas_->mask_display_mode() == CanvasWidget::MaskDisplayMode::Grayscale);
  mask_menu->addSeparator();
  auto* link_mask_action = mask_menu->addAction(simple_icon(QStringLiteral("link"), QColor(210, 220, 230)),
                                                tr("Link Layer Mask"));
  link_mask_action->setCheckable(true);
  link_mask_action->setChecked(active_layer == nullptr || layer_mask_linked(*active_layer));
  auto* disable_mask_action = mask_menu->addAction(simple_icon(QStringLiteral("off"), QColor(220, 185, 120)),
                                                   tr("Disable Layer Mask"));
  disable_mask_action->setCheckable(true);
  disable_mask_action->setChecked(
      smart_mask_context ? !active_smart_filters->mask.enabled
                         : active_layer != nullptr &&
                               active_layer->mask().has_value() &&
                               active_layer->mask()->disabled);
  auto* invert_mask_action = mask_menu->addAction(simple_icon(QStringLiteral("inv"), QColor(210, 220, 230)),
                                                  tr("Invert Layer Mask"));
  mask_menu->addSeparator();
  auto* apply_mask_action = mask_menu->addAction(simple_icon(QStringLiteral("ok"), QColor(150, 220, 170)),
                                                 tr("Apply Layer Mask"));
  auto* delete_mask_action = mask_menu->addAction(simple_icon(QStringLiteral("mask"), QColor(255, 150, 150)),
                                                  tr("Delete Layer Mask"));

  duplicate_action->setEnabled(has_layer);
  rename_action->setEnabled(active_layer != nullptr);
  delete_action->setEnabled(has_layer);
  merge_down_action->setEnabled(has_layer);
  visibility_action->setEnabled(has_layer);
  lock_menu->setEnabled(has_layer);
  select_opaque_action->setEnabled(active_layer != nullptr && canvas_ != nullptr);
  const auto active_pixels_locked = active_layer != nullptr && layer_id_locks_image_pixels(active_layer->id());
  add_mask_action->setEnabled(active_layer != nullptr && !active_pixels_locked &&
                              (active_layer->kind() == LayerKind::Pixel ||
                               active_layer->kind() == LayerKind::Adjustment) &&
                              canvas_ != nullptr &&
                              (canvas_->has_selection() || !active_layer->mask().has_value()));
  edit_mask_action->setEnabled(
      active_layer != nullptr &&
      (active_layer->mask().has_value() || has_editable_smart_mask));
  overlay_mask_action->setEnabled(
      active_layer != nullptr &&
      (active_layer->mask().has_value() || has_editable_smart_mask));
  view_mask_action->setEnabled(
      active_layer != nullptr &&
      (active_layer->mask().has_value() || has_editable_smart_mask));
  delete_mask_action->setEnabled(active_layer != nullptr && !active_pixels_locked && active_layer->mask().has_value());
  link_mask_action->setEnabled(active_layer != nullptr && !active_pixels_locked && active_layer->mask().has_value());
  disable_mask_action->setEnabled(
      active_layer != nullptr && !active_pixels_locked &&
      (active_layer->mask().has_value() || has_editable_smart_mask));
  invert_mask_action->setEnabled(
      active_layer != nullptr && !active_pixels_locked &&
      (active_layer->mask().has_value() || has_editable_smart_mask));
  apply_mask_action->setEnabled(active_layer != nullptr && !active_pixels_locked && active_layer->mask().has_value() &&
                                active_layer->kind() == LayerKind::Pixel);

  hide_menu_action_icons(&menu);
  auto* chosen = menu.exec(layer_list_->viewport()->mapToGlobal(position));
  if (chosen == nullptr) {
    return;
  }
  if (chosen == layer_blending_options_action_ || chosen == layer_copy_style_action_ ||
      chosen == layer_paste_style_action_ || chosen == layer_delete_style_action_ ||
      chosen == layer_rasterize_action_ || chosen == layer_rasterize_layer_style_action_ ||
      chosen == layer_clipping_mask_action_) {
    return;
  }
  if (chosen == edit_adjustment_action) {
    edit_active_adjustment_layer();
  } else if (chosen == warp_text_action && warp_text_action != nullptr) {
    request_warp_text_dialog();
  } else if (chosen == new_action) {
    add_layer();
  } else if (chosen == new_folder_action) {
    create_layer_folder();
  } else if (chosen == duplicate_action) {
    duplicate_active_layer();
  } else if (chosen == rename_action) {
    rename_active_layer();
  } else if (chosen == delete_action) {
    delete_active_layer();
  } else if (chosen == merge_down_action) {
    merge_down();
  } else if (chosen == merge_visible_action) {
    merge_visible_to_new_layer();
  } else if (chosen == visibility_action) {
    set_active_layer_visible(visibility_action->isChecked());
  } else if (chosen == transparent_lock_action) {
    set_active_layer_lock_flag(kLayerLockTransparentPixels, transparent_lock_action->isChecked());
  } else if (chosen == image_lock_action) {
    set_active_layer_lock_flag(kLayerLockImagePixels, image_lock_action->isChecked());
  } else if (chosen == position_lock_action) {
    set_active_layer_lock_flag(kLayerLockPosition, position_lock_action->isChecked());
  } else if (chosen == all_lock_action) {
    set_active_layer_lock_all(all_lock_action->isChecked());
  } else if (chosen == select_opaque_action && active_layer != nullptr && canvas_ != nullptr) {
    canvas_->select_layer_opaque_pixels(active_layer->id());
  } else if (chosen == add_mask_action) {
    add_layer_mask();
  } else if (chosen == edit_mask_action) {
    if (!edit_mask_action->isChecked()) {
      set_layer_edit_target_ui(CanvasWidget::LayerEditTarget::Content, true);
    } else if (smart_mask_context && active_layer != nullptr) {
      static_cast<void>(set_smart_filter_mask_edit_target_ui(
          active_layer->id(), CanvasWidget::MaskDisplayMode::Overlay, true));
    } else {
      set_layer_edit_target_ui(CanvasWidget::LayerEditTarget::Mask, true);
    }
  } else if (chosen == overlay_mask_action) {
    set_mask_overlay_shown(overlay_mask_action->isChecked());
  } else if (chosen == view_mask_action) {
    set_layer_mask_view_shown(view_mask_action->isChecked());
  } else if (chosen == delete_mask_action) {
    delete_active_layer_mask();
  } else if (chosen == link_mask_action) {
    set_active_layer_mask_linked(link_mask_action->isChecked());
  } else if (chosen == disable_mask_action) {
    if (smart_mask_context && active_layer != nullptr) {
      set_smart_filter_mask_enabled(active_layer->id(),
                                    !disable_mask_action->isChecked());
    } else {
      set_active_layer_mask_disabled(disable_mask_action->isChecked());
    }
  } else if (chosen == invert_mask_action) {
    if (smart_mask_context && active_layer != nullptr &&
        (!canvas_->editing_smart_filter_mask() ||
         canvas_->smart_filter_mask_owner_id() != active_layer->id())) {
      static_cast<void>(set_smart_filter_mask_edit_target_ui(
          active_layer->id(), CanvasWidget::MaskDisplayMode::Overlay, false));
    }
    invert_active_layer_mask();
  } else if (chosen == apply_mask_action) {
    apply_active_layer_mask();
  }
}

void MainWindow::merge_visible_to_new_layer() {
  auto& doc = document();
  push_undo_snapshot(tr("Merge visible"));
  auto merged = Compositor{}.flatten_rgb8(doc);
  doc.add_pixel_layer(tr("Merged Visible").toStdString(), std::move(merged));
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
  statusBar()->showMessage(tr("Merged visible layers to a new layer"));
}

void MainWindow::fill_active_layer() {
  fill_active_layer_with_color(canvas_->primary_color(), tr("Fill"));
}

void MainWindow::fill_active_layer_with_color(QColor color, QString label) {
  if (canvas_ != nullptr && canvas_->quick_mask_active()) {
    canvas_->begin_processing_operation();
    const auto finish_processing = qScopeGuard([this] {
      if (canvas_ != nullptr) {
        canvas_->end_processing_operation();
      }
    });
    (void)canvas_->fill_quick_mask(color, std::move(label));
    return;
  }
  if (canvas_ != nullptr && canvas_->editing_smart_filter_mask()) {
    const auto dirty =
        canvas_->fill_smart_filter_mask(color, std::move(label));
    if (!dirty.isEmpty()) {
      statusBar()->showMessage(tr("Filled Smart Filter mask"));
    }
    return;
  }
  const auto edit_target = canvas_ != nullptr ? canvas_->layer_edit_target() : CanvasWidget::LayerEditTarget::Content;
  const bool document_channel = edit_target == CanvasWidget::LayerEditTarget::DocumentChannel;
  const bool component_channel = edit_target == CanvasWidget::LayerEditTarget::ComponentRed ||
                                 edit_target == CanvasWidget::LayerEditTarget::ComponentGreen ||
                                 edit_target == CanvasWidget::LayerEditTarget::ComponentBlue;
  if (component_channel || (document_channel && !canvas_->document_channel_is_editable())) {
    show_status_error(tr("This channel is read-only"));
    return;
  }
  if (canvas_ != nullptr && (edit_target == CanvasWidget::LayerEditTarget::Mask || document_channel)) {
    if (!document_channel) {
      const auto active = document().active_layer_id();
      if (active.has_value() && layer_id_locks_image_pixels(*active)) {
        show_status_error(tr("Layer pixels are locked."));
        return;
      }
    }
    canvas_->begin_processing_operation();
    const auto finish_processing = qScopeGuard([this] {
      if (canvas_ != nullptr) {
        canvas_->end_processing_operation();
      }
    });
    push_undo_snapshot(label);
    const auto dirty = canvas_->fill_active_layer_mask(color);
    if (!dirty.isEmpty()) {
      if (document_channel) {
        canvas_->grayscale_target_changed(dirty);
        refresh_channel_panel();
      } else {
        canvas_->document_changed(dirty);
        refresh_layer_thumbnails();
      }
      refresh_document_info();
      statusBar()->showMessage(document_channel ? tr("Filled channel") : tr("Filled layer mask"));
    }
    return;
  }

  const auto ids = selected_or_active_layer_ids();
  if (ids.empty()) {
    return;
  }
  const auto editable_ids = layer_ids_without_image_pixel_lock(ids);
  if (show_pixel_lock_message_if_all_locked(ids, editable_ids)) {
    return;
  }

  auto& doc = document();
  std::vector<LayerId> fillable_ids;
  fillable_ids.reserve(editable_ids.size());
  for (const auto id : editable_ids) {
    const auto* layer = std::as_const(doc).find_layer(id);
    if (layer != nullptr && layer->kind() == LayerKind::Pixel &&
        !layer_pixels_are_procedural(*layer)) {
      fillable_ids.push_back(id);
    }
  }
  if (fillable_ids.empty()) {
    show_status_error(
        tr("Text, Smart Object, and Shape pixels cannot be filled. Rasterize the layer first."));
    return;
  }
  canvas_->begin_processing_operation();
  const auto finish_processing = qScopeGuard([this] {
    if (canvas_ != nullptr) {
      canvas_->end_processing_operation();
    }
  });
  push_undo_snapshot(label);
  auto options = edit_options(*canvas_);
  options.primary = edit_color(color);
  // Fill honors its own Opacity and Soft settings (Fill tool options bar; default 100% / 0). Opacity
  // scales the fill alpha; Soft feathers the fill inward from the selection edge.
  constexpr double kFillMaxFeatherPixels = 50.0;
  options.primary.a = static_cast<std::uint8_t>(
      std::clamp(std::lround(static_cast<double>(options.primary.a) * canvas_->fill_opacity() / 100.0), 0L, 255L));
  options.fill_softness_feather = std::clamp(canvas_->fill_softness(), 0, 100) / 100.0 * kFillMaxFeatherPixels;
  Rect affected;
  for (const auto id : fillable_ids) {
    auto* layer = doc.find_layer(id);
    if (layer == nullptr || layer->kind() != LayerKind::Pixel) {
      continue;
    }
    options.lock_transparent_pixels = layer_locks_transparent_pixels(*layer);
    const auto target = canvas_->has_selection() && canvas_->selected_document_rect().has_value()
                            ? to_core_rect(*canvas_->selected_document_rect())
                            : layer->bounds();
    affected = unite_rect(affected, patchy::fill_rect(doc, id, target, options));
  }
  if (!affected.empty()) {
    canvas_->document_changed(to_qrect(affected));
  }
}

void MainWindow::clear_active_layer() {
  if (canvas_ != nullptr && canvas_->quick_mask_active()) {
    canvas_->begin_processing_operation();
    const auto finish_processing = qScopeGuard([this] {
      if (canvas_ != nullptr) {
        canvas_->end_processing_operation();
      }
    });
    (void)canvas_->fill_quick_mask(Qt::black, tr("Clear Quick Mask"));
    return;
  }
  if (canvas_ != nullptr && canvas_->editing_smart_filter_mask()) {
    const auto dirty = canvas_->fill_smart_filter_mask(
        Qt::black, tr("Clear Smart Filter mask"));
    if (!dirty.isEmpty()) {
      statusBar()->showMessage(tr("Cleared Smart Filter mask"));
    }
    return;
  }
  const auto edit_target = canvas_ != nullptr ? canvas_->layer_edit_target() : CanvasWidget::LayerEditTarget::Content;
  const bool document_channel = edit_target == CanvasWidget::LayerEditTarget::DocumentChannel;
  const bool component_channel = edit_target == CanvasWidget::LayerEditTarget::ComponentRed ||
                                 edit_target == CanvasWidget::LayerEditTarget::ComponentGreen ||
                                 edit_target == CanvasWidget::LayerEditTarget::ComponentBlue;
  if (component_channel || (document_channel && !canvas_->document_channel_is_editable())) {
    show_status_error(tr("This channel is read-only"));
    return;
  }
  if (canvas_ != nullptr && (edit_target == CanvasWidget::LayerEditTarget::Mask || document_channel)) {
    if (!document_channel) {
      const auto active = document().active_layer_id();
      if (active.has_value() && layer_id_locks_image_pixels(*active)) {
        show_status_error(tr("Layer pixels are locked."));
        return;
      }
    }
    canvas_->begin_processing_operation();
    const auto finish_processing = qScopeGuard([this] {
      if (canvas_ != nullptr) {
        canvas_->end_processing_operation();
      }
    });
    push_undo_snapshot(document_channel ? tr("Clear channel") : tr("Clear layer mask"));
    const auto dirty = canvas_->clear_active_layer_mask();
    if (!dirty.isEmpty()) {
      if (document_channel) {
        canvas_->grayscale_target_changed(dirty);
        refresh_channel_panel();
      } else {
        canvas_->document_changed(dirty);
        refresh_layer_thumbnails();
      }
      refresh_document_info();
      statusBar()->showMessage(document_channel ? tr("Cleared channel") : tr("Cleared layer mask"));
    }
    return;
  }

  const auto ids = selected_or_active_layer_ids();
  if (ids.empty()) {
    return;
  }
  const auto editable_ids = layer_ids_without_image_pixel_lock(ids);
  if (show_pixel_lock_message_if_all_locked(ids, editable_ids)) {
    return;
  }

  auto& doc = document();

  // Delete on a text, smart-object, or shape layer removes the whole object,
  // matching Photoshop. Clearing its pixels would leave an invisible layer whose
  // source data (text metadata, placed-layer data, vector content) still exists,
  // so the "erased" content comes back the next time that data is used. Such
  // layers are left untouched while an inline text edit is in progress (Delete
  // belongs to typing) or while a selection is active (Photoshop refuses to
  // Clear these layers).
  std::vector<LayerId> object_layer_ids;
  for (const auto id : editable_ids) {
    if (const auto* layer = doc.find_layer(id);
        layer != nullptr && layer_pixels_are_procedural(*layer)) {
      object_layer_ids.push_back(id);
    }
  }
  const auto text_editing_active = canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) != nullptr;
  std::vector<LayerId> object_delete_ids;
  if (!text_editing_active && !canvas_->has_selection()) {
    object_delete_ids = object_layer_ids;
  }

  canvas_->begin_processing_operation();
  const auto finish_processing = qScopeGuard([this] {
    if (canvas_ != nullptr) {
      canvas_->end_processing_operation();
    }
  });
  struct ClearCandidate {
    LayerId id{};
    Rect bounds{};
    bool lock_transparent_pixels{false};
  };
  struct ClearTarget {
    LayerId id{};
    Rect bounds{};
    bool lock_transparent_pixels{false};
  };
  std::vector<ClearCandidate> candidates;
  std::int64_t scan_pixels = 0;
  auto options = edit_options(*canvas_);
  for (const auto id : editable_ids) {
    const auto* layer = doc.find_layer(id);
    if (layer == nullptr || layer->kind() != LayerKind::Pixel || layer_pixels_are_procedural(*layer)) {
      continue;
    }
    options.lock_transparent_pixels = layer_locks_transparent_pixels(*layer);
    const auto pixels_to_scan = clear_scan_pixel_count(doc, *layer, layer->bounds(), options);
    scan_pixels += pixels_to_scan;
    if (pixels_to_scan > 0) {
      candidates.push_back(ClearCandidate{id, layer->bounds(), options.lock_transparent_pixels});
    }
  }

  constexpr std::int64_t kClearProgressPixelThreshold = 250'000;
  std::unique_ptr<QProgressDialog> progress;
  if (scan_pixels >= kClearProgressPixelThreshold) {
    progress = std::make_unique<QProgressDialog>(tr("Clearing..."), QString(), 0, 0, this);
    progress->setObjectName(QStringLiteral("clearProgressDialog"));
    progress->setWindowTitle(tr("Clearing"));
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setCancelButton(nullptr);
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    remember_dialog_position(*progress);
    progress->show();
    progress->raise();
    progress->activateWindow();
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  }
  const auto close_progress = qScopeGuard([&progress] {
    if (progress != nullptr) {
      progress->close();
      QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }
  });
  Q_UNUSED(close_progress);

  std::vector<ClearTarget> targets;
  for (const auto& candidate : candidates) {
    options.lock_transparent_pixels = candidate.lock_transparent_pixels;
    const auto changed = patchy::clear_rect_change_bounds(doc, candidate.id, candidate.bounds, options);
    if (!changed.empty()) {
      targets.push_back(ClearTarget{candidate.id, changed, candidate.lock_transparent_pixels});
    }
  }

  if (targets.empty() && object_delete_ids.empty()) {
    if (object_layer_ids.empty()) {
      statusBar()->showMessage(tr("Nothing to clear"));
    } else if (!text_editing_active) {
      show_status_error(
          tr("Text and smart object layers can't be cleared. Deselect first, then Delete removes the layer."));
    }
    return;
  }

  if (targets.empty()) {
    const auto deleted_count = object_delete_ids.size();
    delete_layers(std::move(object_delete_ids));
    statusBar()->showMessage(deleted_count == 1 ? tr("Deleted layer") : tr("Deleted %1 layers").arg(deleted_count));
    return;
  }

  push_undo_snapshot(tr("Clear"));
  Rect affected;
  for (const auto& target : targets) {
    options.lock_transparent_pixels = target.lock_transparent_pixels;
    affected = unite_rect(affected, patchy::clear_rect(doc, target.id, target.bounds, options));
  }
  for (const auto id : object_delete_ids) {
    doc.remove_layer(id);
  }
  if (!object_delete_ids.empty()) {
    refresh_layer_list();
    refresh_layer_controls();
    canvas_->document_changed();
  } else if (!affected.empty()) {
    canvas_->document_changed(to_qrect(affected));
  }
}

void MainWindow::stroke_selection() {
  auto& doc = document();
  const auto active = doc.active_layer_id();
  if (!active.has_value()) {
    return;
  }
  const auto selection = canvas_->selected_document_region();
  if (selection.isEmpty()) {
    show_status_error(tr("Make a selection before stroking"));
    return;
  }
  auto* layer = doc.find_layer(*active);
  if (layer == nullptr || layer->kind() != LayerKind::Pixel) {
    show_status_error(tr("Select an editable pixel layer first"));
    return;
  }
  if (layer_pixels_are_procedural(*layer)) {
    show_status_error(
        tr("Rasterize Text, Smart Object, and Shape layers before editing their pixels"));
    return;
  }
  if (layer_id_locks_image_pixels(*active)) {
    show_status_error(tr("Layer pixels are locked."));
    return;
  }

  canvas_->begin_processing_operation();
  const auto finish_processing = qScopeGuard([this] {
    if (canvas_ != nullptr) {
      canvas_->end_processing_operation();
    }
  });
  push_undo_snapshot(tr("Stroke selection"));
  auto options = edit_options(*canvas_);
  options.lock_transparent_pixels = layer_locks_transparent_pixels(*layer);
  const QRect canvas_rect(0, 0, doc.width(), doc.height());
  const auto stroke_region = selection_outline_region(selection, canvas_->brush_size(), canvas_rect);
  if (stroke_region.isEmpty()) {
    return;
  }
  options.selection = to_core_rect(stroke_region.boundingRect());
  options.selection_mask = [stroke_region](std::int32_t x, std::int32_t y) { return stroke_region.contains(QPoint(x, y)); };
  const auto affected = patchy::fill_rect(doc, *active, to_core_rect(stroke_region.boundingRect()), options);
  if (!affected.empty()) {
    canvas_->document_changed(to_qrect(affected));
  }
  statusBar()->showMessage(tr("Stroked selection"));
}

void MainWindow::expand_selection_dialog() {
  if (!canvas_->has_selection()) {
    show_status_error(tr("Make a selection before expanding"));
    return;
  }
  const auto pixels = request_integer_input(this, QStringLiteral("patchyExpandSelectionDialog"),
                                            tr("Expand Selection"), tr("Expand by"), 4, 1, 250, 1);
  if (pixels.has_value()) {
    canvas_->run_selection_command(tr("Expand Selection"), [this, pixels] { canvas_->expand_selection(*pixels); });
  }
}

void MainWindow::contract_selection_dialog() {
  if (!canvas_->has_selection()) {
    show_status_error(tr("Make a selection before contracting"));
    return;
  }
  const auto pixels = request_integer_input(this, QStringLiteral("patchyContractSelectionDialog"),
                                            tr("Contract Selection"), tr("Contract by"), 4, 1, 250, 1);
  if (pixels.has_value()) {
    canvas_->run_selection_command(tr("Contract Selection"), [this, pixels] { canvas_->contract_selection(*pixels); });
  }
}

void MainWindow::border_selection_dialog() {
  if (!canvas_->has_selection()) {
    show_status_error(tr("Make a selection before selecting a border"));
    return;
  }
  const auto pixels = request_integer_input(this, QStringLiteral("patchyBorderSelectionDialog"),
                                            tr("Border Selection"), tr("Width"), 4, 1, 250, 1);
  if (pixels.has_value()) {
    canvas_->run_selection_command(tr("Border Selection"), [this, pixels] { canvas_->border_selection(*pixels); });
  }
}

void MainWindow::flip_active_layer_horizontal() {
  if (canvas_ != nullptr) {
    const auto target = canvas_->layer_edit_target();
    if (target == CanvasWidget::LayerEditTarget::DocumentChannel ||
        target == CanvasWidget::LayerEditTarget::ComponentRed ||
        target == CanvasWidget::LayerEditTarget::ComponentGreen ||
        target == CanvasWidget::LayerEditTarget::ComponentBlue) {
      return;
    }
  }
  const auto ids = selected_or_active_layer_ids();
  if (ids.empty()) {
    return;
  }
  if (std::any_of(ids.begin(), ids.end(), [this](LayerId id) {
        const auto* layer = std::as_const(document()).find_layer(id);
        return layer != nullptr && layer_tree_contains_smart_object(*layer);
      })) {
    show_status_error(
        tr("Use Free Transform or rasterize Smart Objects before flipping"));
    return;
  }
  const auto editable_ids = layer_ids_without_image_pixel_lock(ids);
  if (show_pixel_lock_message_if_all_locked(ids, editable_ids)) {
    return;
  }

  auto& doc = document();
  push_undo_snapshot(tr("Flip horizontal"));
  Rect affected;
  for (const auto id : editable_ids) {
    affected = unite_rect(affected, patchy::flip_layer_horizontal(doc, id));
  }
  canvas_->document_changed(to_qrect(affected));
  refresh_layer_list();
  refresh_layer_controls();
}

void MainWindow::flip_active_layer_vertical() {
  if (canvas_ != nullptr) {
    const auto target = canvas_->layer_edit_target();
    if (target == CanvasWidget::LayerEditTarget::DocumentChannel ||
        target == CanvasWidget::LayerEditTarget::ComponentRed ||
        target == CanvasWidget::LayerEditTarget::ComponentGreen ||
        target == CanvasWidget::LayerEditTarget::ComponentBlue) {
      return;
    }
  }
  const auto ids = selected_or_active_layer_ids();
  if (ids.empty()) {
    return;
  }
  if (std::any_of(ids.begin(), ids.end(), [this](LayerId id) {
        const auto* layer = std::as_const(document()).find_layer(id);
        return layer != nullptr && layer_tree_contains_smart_object(*layer);
      })) {
    show_status_error(
        tr("Use Free Transform or rasterize Smart Objects before flipping"));
    return;
  }
  const auto editable_ids = layer_ids_without_image_pixel_lock(ids);
  if (show_pixel_lock_message_if_all_locked(ids, editable_ids)) {
    return;
  }

  auto& doc = document();
  push_undo_snapshot(tr("Flip vertical"));
  Rect affected;
  for (const auto id : editable_ids) {
    affected = unite_rect(affected, patchy::flip_layer_vertical(doc, id));
  }
  canvas_->document_changed(to_qrect(affected));
}

void MainWindow::crop_to_selection() {
  const auto selection = canvas_->selected_document_rect();
  if (!selection.has_value() || selection->isEmpty()) {
    show_status_error(tr("Make a rectangular selection before cropping"));
    return;
  }
  if (document_contains_smart_objects(std::as_const(document()))) {
    show_status_error(tr("Rasterize Smart Objects before changing document geometry"));
    return;
  }

  push_undo_snapshot(tr("Crop"));
  auto& doc = document();
  if (!patchy::crop_document(doc, to_core_rect(*selection))) {
    return;
  }
  canvas_->clear_selection();
  const auto previous_channel_target = canvas_->layer_edit_target();
  const auto previous_channel_id = canvas_->active_document_channel_id();
  const auto previous_channel_display = canvas_->mask_display_mode();
  canvas_->set_document(&doc);
  restore_channel_target_after_document_reset(previous_channel_target, previous_channel_id,
                                              previous_channel_display);
  // The old pan is meaningless for the smaller document and can leave it
  // mostly off screen, so recenter at the current zoom.
  canvas_->center_document_in_view();
  refresh_layer_list();
  refresh_layer_controls();
  refresh_document_info();
  statusBar()->showMessage(tr("Cropped to selection"));
}

void MainWindow::rotate_canvas_clockwise() {
  auto& doc = document();
  if (document_contains_smart_objects(std::as_const(doc))) {
    show_status_error(tr("Rasterize Smart Objects before changing document geometry"));
    return;
  }
  push_undo_snapshot(tr("Rotate canvas"));
  patchy::rotate_document_clockwise(doc);
  canvas_->clear_selection();
  const auto previous_channel_target = canvas_->layer_edit_target();
  const auto previous_channel_id = canvas_->active_document_channel_id();
  const auto previous_channel_display = canvas_->mask_display_mode();
  canvas_->set_document(&doc);
  restore_channel_target_after_document_reset(previous_channel_target, previous_channel_id,
                                              previous_channel_display);
  refresh_layer_list();
  refresh_layer_controls();
  refresh_document_info();
  statusBar()->showMessage(tr("Rotated canvas clockwise"));
}

void MainWindow::rotate_canvas_counterclockwise() {
  auto& doc = document();
  if (document_contains_smart_objects(std::as_const(doc))) {
    show_status_error(tr("Rasterize Smart Objects before changing document geometry"));
    return;
  }
  push_undo_snapshot(tr("Rotate canvas"));
  patchy::rotate_document_counterclockwise(doc);
  canvas_->clear_selection();
  const auto previous_channel_target = canvas_->layer_edit_target();
  const auto previous_channel_id = canvas_->active_document_channel_id();
  const auto previous_channel_display = canvas_->mask_display_mode();
  canvas_->set_document(&doc);
  restore_channel_target_after_document_reset(previous_channel_target, previous_channel_id,
                                              previous_channel_display);
  refresh_layer_list();
  refresh_layer_controls();
  refresh_document_info();
  statusBar()->showMessage(tr("Rotated canvas counterclockwise"));
}

}  // namespace patchy::ui
