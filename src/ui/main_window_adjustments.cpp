// MainWindow filters and adjustment-layer implementation, split out of main_window.cpp:
// Filter menu apply flow, the Levels / Curves / Hue-Saturation / Color Balance dialogs
// (with their shared async pixel-preview machinery), adjustment-layer creation/preview/
// editing, and the filter progress-dialog helper. Pure function moves; behavior must
// stay identical to the pre-split code.

#include "ui/main_window.hpp"
#include "ui/main_window_shared.hpp"

#include "core/layer_metadata.hpp"
#include "core/layer_render_utils.hpp"
#include "core/layer_tree.hpp"
#include "core/palette.hpp"
#include "core/palette_presets.hpp"
#include "core/pixel_tools.hpp"
#include "formats/palette_io.hpp"
#include "filters/builtin_filters.hpp"
#include "filters/smart_filter_recipe_mapping.hpp"
#include "filters/smart_filter_renderer.hpp"
#include "formats/bmp_document_io.hpp"
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
#include "ui/filter_workflows.hpp"
#include "ui/gradient_stops_editor.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/hotkey_editor.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/color_panel.hpp"
#include "ui/layer_style_dialog.hpp"
#include "ui/layer_list_widget.hpp"
#include "ui/localization.hpp"
#include "ui/palette_convert_dialog.hpp"
#include "ui/palette_panel.hpp"
#include "ui/print_dialog.hpp"
#include "ui/qt_geometry.hpp"
#include "ui/splash_dialog.hpp"
#include "ui/smart_object_render.hpp"
#include "ui/update_checker.hpp"
#include "ui/visual_filter_gallery_dialog.hpp"
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
#include <QButtonGroup>
#include <QByteArray>
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

constexpr int kFilterProgressMinimumDurationMs = 1000;

template <typename Request>
struct AsyncPixelPreviewState {
  bool closed{false};
  bool in_flight{false};
  std::uint64_t generation{0};
  std::optional<Request> pending;
  std::function<void(const Request&)> start;
};

template <typename Request>
void enqueue_async_pixel_preview(const std::shared_ptr<AsyncPixelPreviewState<Request>>& state, Request request,
                                 bool immediate = false) {
  if (state == nullptr || state->closed || !state->start) {
    return;
  }
  if (!immediate && state->in_flight) {
    state->pending = std::move(request);
    return;
  }
  state->start(request);
}

template <typename Request>
void close_async_pixel_preview(const std::shared_ptr<AsyncPixelPreviewState<Request>>& state) {
  if (state == nullptr) {
    return;
  }
  state->closed = true;
  ++state->generation;
  state->pending.reset();
  state->start = {};
}

// The direct adjustment dialogs above predate cooperative cancellation: they
// coalesce requests and discard stale results, but let the current render finish.
// The gallery can generate several full-resolution looks in quick succession, so
// it also invalidates the running worker through FilterProgress. All fields other
// than `generation` are confined to the UI thread.
template <typename Request>
struct LatestCancellablePixelPreviewState {
  struct Work {
    std::uint64_t generation{0};
    Request request;
  };

  bool closed{false};
  bool in_flight{false};
  std::atomic<std::uint64_t> generation{0};
  std::optional<Work> pending;
  std::function<void(Work)> start;
};

template <typename Request>
void enqueue_latest_cancellable_pixel_preview(
    const std::shared_ptr<LatestCancellablePixelPreviewState<Request>>& state, Request request) {
  if (state == nullptr || state->closed || !state->start) {
    return;
  }
  const auto generation = state->generation.fetch_add(1, std::memory_order_acq_rel) + 1;
  typename LatestCancellablePixelPreviewState<Request>::Work work{generation, std::move(request)};
  if (state->in_flight) {
    state->pending = std::move(work);
    return;
  }
  state->start(std::move(work));
}

template <typename Request>
void cancel_latest_cancellable_pixel_preview(
    const std::shared_ptr<LatestCancellablePixelPreviewState<Request>>& state) {
  if (state == nullptr || state->closed) {
    return;
  }
  state->generation.fetch_add(1, std::memory_order_acq_rel);
  state->pending.reset();
}

template <typename Request>
void close_latest_cancellable_pixel_preview(
    const std::shared_ptr<LatestCancellablePixelPreviewState<Request>>& state) {
  if (state == nullptr) {
    return;
  }
  state->closed = true;
  state->generation.fetch_add(1, std::memory_order_acq_rel);
  state->pending.reset();
  state->start = {};
}

template <typename Settings>
struct AdjustmentPixelPreviewRequest {
  bool enabled{true};
  Settings settings{};
};

LevelsAdjustment sanitized_levels_adjustment(LevelsSettings settings) {
  const auto clamp_record = [](LevelsRecord record) {
    record.black_input = std::clamp(record.black_input, 0, 254);
    record.white_input = std::clamp(record.white_input, record.black_input + 1, 255);
    record.gamma_percent = std::clamp(record.gamma_percent, 10, 999);
    record.black_output = std::clamp(record.black_output, 0, 255);
    record.white_output = std::clamp(record.white_output, record.black_output, 255);
    return record;
  };
  const auto master = clamp_record(LevelsRecord{settings.black_input, settings.white_input, settings.gamma_percent,
                                                settings.black_output, settings.white_output});
  return LevelsAdjustment{master.black_input, master.white_input, master.gamma_percent, master.black_output,
                          master.white_output, settings.channel, clamp_record(settings.red), clamp_record(settings.green),
                          clamp_record(settings.blue)};
}

bool levels_settings_have_effect(LevelsSettings settings) {
  AdjustmentSettings adjustment;
  adjustment.kind = AdjustmentKind::Levels;
  adjustment.levels = sanitized_levels_adjustment(settings);
  return adjustment_has_effect(adjustment);
}

bool curves_settings_have_effect(const CurvesSettings& curves) {
  AdjustmentSettings adjustment;
  adjustment.kind = AdjustmentKind::Curves;
  adjustment.curves = curves;
  return adjustment_has_effect(adjustment);
}

CurvesHistograms curves_histograms_from_composite(const Document& document) {
  std::vector<std::uint8_t> merged_alpha;
  const auto rgb = Compositor{}.flatten_rgb8(document, &merged_alpha);
  return curves_histograms_from_pixels(&rgb, merged_alpha);
}

bool truncate_layer_tree_before(std::vector<Layer>& siblings, LayerId target_id) {
  for (std::size_t index = 0; index < siblings.size(); ++index) {
    if (siblings[index].id() == target_id) {
      siblings.erase(siblings.begin() + static_cast<std::ptrdiff_t>(index), siblings.end());
      return true;
    }
    if (!std::as_const(siblings[index]).children().empty()) {
      auto& children = siblings[index].children();
      if (truncate_layer_tree_before(children, target_id)) {
        siblings.erase(siblings.begin() + static_cast<std::ptrdiff_t>(index + 1U), siblings.end());
        return true;
      }
    }
  }
  return false;
}

CurvesHistograms curves_histograms_before_adjustment(const Document& document, LayerId adjustment_id) {
  auto input_document = document;
  if (!truncate_layer_tree_before(input_document.layers(), adjustment_id)) {
    return curves_histograms_from_composite(document);
  }
  return curves_histograms_from_composite(input_document);
}

bool collect_layers_at_or_above(const std::vector<Layer>& siblings, LayerId target_id,
                                std::vector<LayerId>& hidden) {
  for (std::size_t index = 0; index < siblings.size(); ++index) {
    if (siblings[index].id() == target_id) {
      for (std::size_t hidden_index = index; hidden_index < siblings.size(); ++hidden_index) {
        hidden.push_back(siblings[hidden_index].id());
      }
      return true;
    }
    if (!siblings[index].children().empty() &&
        collect_layers_at_or_above(siblings[index].children(), target_id, hidden)) {
      for (std::size_t hidden_index = index + 1U; hidden_index < siblings.size(); ++hidden_index) {
        hidden.push_back(siblings[hidden_index].id());
      }
      return true;
    }
  }
  return false;
}

QColor curves_sample_before_layer(const Document& document, LayerId layer_id, QPoint point) {
  if (point.x() < 0 || point.y() < 0 || point.x() >= document.width() || point.y() >= document.height()) {
    return {};
  }
  std::vector<LayerId> hidden;
  collect_layers_at_or_above(document.layers(), layer_id, hidden);
  const auto image = qimage_from_document_rect_with_hidden_layers(
      document, QRect(point, QSize(1, 1)), true, hidden);
  return image.isNull() ? QColor{} : image.pixelColor(0, 0);
}

CurvesDialogHooks curves_canvas_hooks(CanvasWidget* canvas,
                                      std::function<QColor(QPoint)> sample_input_color) {
  CurvesDialogHooks hooks;
  if (canvas == nullptr || !sample_input_color) {
    return hooks;
  }
  hooks.set_canvas_mode = [canvas, sample_input_color = std::move(sample_input_color)](
                              CurvesCanvasMode mode,
                              std::function<void(const CurvesCanvasSample&)> sample_changed) {
    canvas->clear_transient_read_interaction();
    if (mode == CurvesCanvasMode::None || !sample_changed) {
      return;
    }
    canvas->set_transient_read_interaction(
        [sample_input_color, sample_changed = std::move(sample_changed)](const CanvasReadGesture& gesture) {
          auto color = gesture.phase == CanvasReadPhase::Press
                           ? sample_input_color(gesture.document_position)
                           : QColor{};
          sample_changed(CurvesCanvasSample{color, gesture});
        },
        Qt::CrossCursor);
  };
  hooks.clear_canvas_mode = [canvas] { canvas->clear_transient_read_interaction(); };
  hooks.clipping_changed = [canvas](std::optional<CurvesClippingMode> mode, CurvesChannel channel) {
    canvas->set_curves_clipping_preview(mode, channel);
  };
  return hooks;
}

FilterProgress progress_dialog_filter_progress(QProgressDialog& progress,
                                               std::function<QString(const QString&)> label_text,
                                               QEventLoop::ProcessEventsFlags event_flags,
                                               std::function<void()> tick_processing = {}) {
  auto last_progress_value = std::make_shared<int>(-1);
  return FilterProgress{[&progress, label_text = std::move(label_text), event_flags,
                         tick_processing = std::move(tick_processing),
                         last_progress_value](int completed, int total, FilterProgressStage stage) {
    const auto value = total <= 0 ? 100 : std::clamp((completed * 100) / total, 0, 100);
    if (value != *last_progress_value) {
      progress.setValue(value);
      progress.setLabelText(label_text(filter_progress_stage_text(stage)));
      *last_progress_value = value;
      QApplication::processEvents(event_flags);
    }
    if (tick_processing) {
      tick_processing();
    }
    return !progress.wasCanceled();
  }};
}

void snap_filter_result_to_palette(PixelBuffer& pixels, Rect bounds, const QRegion& selection,
                                   const PaletteSnapContext* palette_snap) {
  if (palette_snap == nullptr || palette_snap->lut == nullptr || palette_snap->lut->empty() || pixels.empty() ||
      pixels.format().bit_depth != BitDepth::UInt8 || pixels.format().channels < 3) {
    return;
  }
  const auto channels = pixels.format().channels;
  const auto snap_local_rect = [&](QRect local) {
    local = local.intersected(QRect(0, 0, pixels.width(), pixels.height()));
    for (int y = local.top(); y <= local.bottom(); ++y) {
      for (int x = local.left(); x <= local.right(); ++x) {
        snap_pixel_to_palette(pixels.pixel(x, y), channels, *palette_snap);
      }
    }
  };
  if (selection.isEmpty()) {
    snap_local_rect(QRect(0, 0, pixels.width(), pixels.height()));
    return;
  }
  const auto selected = selection.intersected(QRegion(to_qrect(bounds)));
  for (const auto& rect : selected) {
    snap_local_rect(rect.translated(-bounds.x, -bounds.y));
  }
}

double gaussian_radius_from_invocation(const FilterInvocation& invocation,
                                       double fallback) {
  const auto found = invocation.parameters.find("radius");
  if (found == invocation.parameters.end()) {
    return fallback;
  }
  if (const auto* value = std::get_if<double>(&found->second); value != nullptr) {
    return std::clamp(*value, 0.1, 1000.0);
  }
  if (const auto* value = std::get_if<std::int64_t>(&found->second);
      value != nullptr) {
    return std::clamp(static_cast<double>(*value), 0.1, 1000.0);
  }
  return fallback;
}

FilterDialogSpec gaussian_smart_filter_dialog_spec() {
  FilterDialogSpec spec;
  spec.identifier = QStringLiteral("patchy.smart_filters.gaussian_blur");
  spec.display_name = QObject::tr("Gaussian Blur");
  FilterControlSpec radius;
  radius.label = QObject::tr("Radius");
  radius.object_name = QStringLiteral("filterRadius");
  radius.minimum = 0;
  radius.maximum = 12;
  radius.value = 2;
  radius.suffix = QObject::tr(" px");
  radius.parameter_key = "radius";
  radius.kind = FilterParameterKind::Double;
  radius.default_value = 2.0;
  radius.typed_minimum = 0.1;
  radius.typed_maximum = 1000.0;
  // Photoshop keeps hundredth-pixel native radii distinct (0.49, 0.50, and
  // 0.51 produce different rasters), so accepting the dialog must not round an
  // imported value merely because the spin box was opened.
  radius.step = 0.01;
  spec.controls.push_back(std::move(radius));
  return spec;
}

SmartFilterStack gaussian_stack_with_radius(SmartFilterStack stack,
                                            std::size_t index,
                                            double radius) {
  auto* gaussian =
      std::get_if<GaussianBlurSmartFilter>(&stack.entries.at(index).parameters);
  if (gaussian == nullptr) {
    throw std::invalid_argument("Smart Filter is not Gaussian Blur");
  }
  gaussian->radius_pixels = std::clamp(radius, 0.1, 1000.0);
  return stack;
}

SmartFilterEntry make_gaussian_smart_filter_entry(
    double radius, RgbColor foreground, RgbColor background) {
  SmartFilterEntry entry;
  entry.kind = SmartFilterKind::GaussianBlur;
  entry.native_name = "Gaussian Blur...";
  entry.native_class_id = "GsnB";
  entry.native_filter_id = 0x47736e42U;
  entry.enabled = true;
  entry.has_options = true;
  entry.opacity = 1.0;
  entry.blend_mode = BlendMode::Normal;
  entry.foreground = foreground;
  entry.background = background;
  entry.parameters = GaussianBlurSmartFilter{
      std::clamp(radius, 0.1, 1000.0)};
  return entry;
}

std::optional<SmartFilterStack> smart_filter_stack_with_recipe(
    const std::optional<SmartFilterStack>& base,
    const SmartFilterMask& new_stack_mask, const FilterRecipe& recipe,
    const FilterRegistry& registry) {
  const auto mapped = smart_filter_entries_from_recipe(recipe, registry);
  if (!mapped.has_value() || mapped->empty()) {
    return std::nullopt;
  }
  constexpr std::size_t kMaximumEditableEntries = 64U;
  if ((base.has_value() ? base->entries.size() : 0U) + mapped->size() >
      kMaximumEditableEntries) {
    return std::nullopt;
  }
  SmartFilterStack candidate;
  if (base.has_value()) {
    if (base->support != SmartFilterStackSupport::Supported) {
      return std::nullopt;
    }
    candidate = *base;
  } else {
    candidate.enabled = true;
    candidate.valid_at_position = true;
    candidate.mask = new_stack_mask;
    candidate.support = SmartFilterStackSupport::Supported;
  }
  candidate.entries.insert(candidate.entries.end(), mapped->begin(),
                           mapped->end());
  return candidate;
}


}  // namespace

bool MainWindow::commit_smart_filter_stack_edit(
    LayerId layer_id, std::optional<SmartFilterStack> candidate,
    std::vector<std::optional<std::size_t>> entry_sources,
    const QString& undo_text, const QString& status_text,
    SmartFilterCacheEdit cache_edit) {
  if (!has_active_document() || canvas_ == nullptr) {
    return false;
  }
  return commit_smart_filter_stack_edit(
      session(), canvas_, layer_id, std::move(candidate),
      std::move(entry_sources), undo_text, status_text, cache_edit);
}

bool MainWindow::commit_smart_filter_stack_edit(
    DocumentSession& target_session, CanvasWidget* target_canvas,
    LayerId layer_id, std::optional<SmartFilterStack> candidate,
    std::vector<std::optional<std::size_t>> entry_sources,
    const QString& undo_text, const QString& status_text,
    SmartFilterCacheEdit cache_edit) {
  if (target_canvas == nullptr ||
      (patchy::layer_effective_lock_flags(target_session.document.layers(),
                                          layer_id) &
       kLayerLockImagePixels) != kLayerLockNone) {
    return false;
  }
  auto& doc = target_session.document;
  auto* layer = doc.find_layer(layer_id);
  if (layer == nullptr || !layer_is_smart_object(*layer)) {
    return false;
  }
  const auto lock = smart_object_lock_reason(*layer);
  if (!lock.empty() && lock != "external") {
    return false;
  }
  const auto* current = layer->smart_filter_stack();
  if (current != nullptr &&
      current->support != SmartFilterStackSupport::Supported) {
    return false;
  }
  if (candidate.has_value() &&
      (candidate->support != SmartFilterStackSupport::Supported ||
       candidate->entries.empty())) {
    return false;
  }

  const auto parent_document_dir =
      target_session.path.isEmpty()
          ? QString()
          : QFileInfo(target_session.path).absolutePath();
  std::optional<SmartObjectLayerPreview> preview;
  std::optional<FilterRenderResult> unfiltered_only;
  if (candidate.has_value()) {
    preview = render_smart_object_layer_preview(
        std::as_const(doc), std::as_const(*layer),
        target_canvas->transform_interpolation(), &*candidate,
        parent_document_dir);
    if (!preview.has_value()) {
      return false;
    }
  } else {
    unfiltered_only = render_smart_object_unfiltered_layer_preview(
        std::as_const(doc), std::as_const(*layer),
        target_canvas->transform_interpolation(), parent_document_dir);
    if (!unfiltered_only.has_value()) {
      return false;
    }
  }

  const auto placement = smart_object_placement_from_layer(*layer);
  if (!placement.has_value()) {
    return false;
  }
  if (current != nullptr && cache_edit != SmartFilterCacheEdit::Rebuild) {
    const auto* record = std::as_const(doc)
                             .metadata()
                             .smart_filter_effects.find_unique(
                                 smart_object_placed_uuid(*layer));
    if (record == nullptr || !record->semantic_supported()) {
      return false;
    }
  }
  const auto warp = smart_object_warp_from_layer(*layer);
  psd::SmartFilterDescriptorEdit descriptor_edit;
  descriptor_edit.action = candidate.has_value()
                               ? psd::SmartFilterDescriptorAction::Replace
                               : psd::SmartFilterDescriptorAction::Remove;
  descriptor_edit.stack = candidate.has_value() ? &*candidate : nullptr;
  descriptor_edit.entry_sources = std::move(entry_sources);
  std::vector<std::pair<std::size_t, std::vector<std::uint8_t>>>
      regenerated_blocks;
  const auto& original_blocks = std::as_const(*layer).unknown_psd_blocks();
  for (std::size_t index = 0; index < original_blocks.size(); ++index) {
    const auto& block = original_blocks[index];
    if (block.key != "SoLd" && block.key != "SoLE") {
      continue;
    }
    auto regenerated = psd::regenerate_placed_layer_payload(
        block.key, block.payload, *placement,
        warp.has_value() ? &*warp : nullptr,
        smart_object_placed_uuid(*layer), descriptor_edit);
    if (!regenerated.has_value()) {
      return false;
    }
    regenerated_blocks.emplace_back(index, std::move(*regenerated));
  }
  if (regenerated_blocks.empty()) {
    return false;
  }

  auto filter_effects = doc.metadata().smart_filter_effects;
  const auto creating_stack = current == nullptr && candidate.has_value();
  const auto removing_stack = current != nullptr && !candidate.has_value();
  if (creating_stack || cache_edit == SmartFilterCacheEdit::Rebuild) {
    const auto& unfiltered = preview->unfiltered;
    auto record = psd::author_filter_effects_record(
        smart_object_placed_uuid(*layer),
        Rect::from_size(doc.width(), doc.height()), unfiltered.pixels,
        unfiltered.bounds, candidate->mask);
    if (!record.has_value() ||
        !filter_effects.upsert_authored(std::move(*record))) {
      return false;
    }
  } else if (cache_edit == SmartFilterCacheEdit::ReplaceMask) {
    if (!candidate.has_value() ||
        !psd::replace_filter_effects_mask(
            filter_effects, smart_object_placed_uuid(*layer),
            candidate->mask)) {
      return false;
    }
  } else if (removing_stack &&
             !filter_effects.remove(smart_object_placed_uuid(*layer))) {
    return false;
  }

  const auto old_bounds = layer->bounds();
  push_undo_snapshot(target_session, undo_text);
  layer = doc.find_layer(layer_id);
  if (layer == nullptr) {
    return false;
  }
  auto& mutable_blocks = layer->unknown_psd_blocks();
  for (auto& [index, payload] : regenerated_blocks) {
    if (index >= mutable_blocks.size()) {
      return false;
    }
    mutable_blocks[index].payload = std::move(payload);
  }
  doc.metadata().smart_filter_effects = std::move(filter_effects);
  if (candidate.has_value()) {
    layer->set_smart_filter_stack(std::move(*candidate));
    layer->set_pixels(std::move(preview->rendered.pixels));
    layer->set_bounds(preview->rendered.bounds);
  } else {
    layer->clear_smart_filter_stack();
    layer->set_pixels(std::move(unfiltered_only->pixels));
    layer->set_bounds(unfiltered_only->bounds);
  }
  mark_layer_smart_object_block_dirty(*layer);
  layer->metadata()[kLayerMetadataSmartObjectRasterStatus] =
      kSmartObjectRasterStatusPatchy;
  const auto new_bounds = layer->bounds();
  if (target_canvas == canvas_) {
    refresh_layer_list();
    refresh_layer_controls();
  }
  target_canvas->document_changed(
      to_qrect(old_bounds).united(to_qrect(new_bounds)));
  statusBar()->showMessage(status_text);
  return true;
}

void MainWindow::convert_for_smart_filters() {
  if (!has_active_document()) {
    return;
  }
  const auto active = std::as_const(document()).active_layer_id();
  const auto* layer = active.has_value()
                          ? std::as_const(document()).find_layer(*active)
                          : nullptr;
  if (layer == nullptr || layer->kind() != LayerKind::Pixel ||
      layer_is_smart_object(*layer)) {
    statusBar()->showMessage(
        tr("Select a normal pixel layer to convert for Smart Filters"));
    return;
  }
  convert_to_smart_object();
}

void MainWindow::gaussian_smart_filter_dialog(
    LayerId layer_id, std::optional<std::size_t> execution_index) {
  if (!has_active_document() || canvas_ == nullptr) {
    return;
  }
  if (layer_id_locks_image_pixels(layer_id)) {
    statusBar()->showMessage(tr("Layer pixels are locked."));
    return;
  }
  const auto document_pixels =
      static_cast<std::uint64_t>(document().width()) *
      static_cast<std::uint64_t>(document().height());
  if (document_pixels > psd::kMaximumEditableSmartFilterMaskPixels) {
    statusBar()->showMessage(tr(
        "Editable Smart Filters currently support documents up to 64 megapixels"));
    return;
  }
  auto& doc = document();
  auto* layer = doc.find_layer(layer_id);
  const auto lock = layer != nullptr ? smart_object_lock_reason(*layer)
                                     : std::string{};
  if (layer == nullptr || !layer_is_smart_object(*layer) ||
      (!lock.empty() && lock != "external")) {
    statusBar()->showMessage(
        tr("This Smart Object can only preserve its imported filters"));
    return;
  }

  const auto parent_document_dir =
      session().path.isEmpty() ? QString()
                               : QFileInfo(session().path).absolutePath();

  SmartFilterStack stack;
  std::size_t filter_index = 0;
  double initial_radius = 2.0;
  const bool adding = !execution_index.has_value();
  bool creating_stack = false;
  std::vector<std::optional<std::size_t>> entry_sources;
  if (adding) {
    if (const auto* current = layer->smart_filter_stack(); current != nullptr) {
      if (current->support != SmartFilterStackSupport::Supported) {
        statusBar()->showMessage(
            tr("This Smart Filter can only be preserved, not edited"));
        return;
      }
      stack = *current;
      filter_index = stack.entries.size();
      entry_sources.reserve(filter_index + 1U);
      for (std::size_t index = 0; index < filter_index; ++index) {
        entry_sources.emplace_back(index);
      }
      entry_sources.emplace_back(std::nullopt);
    } else {
      creating_stack = true;
      stack.enabled = true;
      stack.valid_at_position = true;
      stack.support = SmartFilterStackSupport::Supported;
      stack.mask.bounds = Rect::from_size(doc.width(), doc.height());
      stack.mask.enabled = true;
      stack.mask.linked = false;
      if (canvas_->has_selection()) {
        stack.mask.pixels = canvas_->selection_as_grayscale();
        stack.mask.default_color = 0U;
        stack.mask.extend_with_white = false;
      } else {
        stack.mask.pixels =
            PixelBuffer(doc.width(), doc.height(), PixelFormat::gray8());
        stack.mask.pixels.clear(255U);
        stack.mask.default_color = 255U;
        stack.mask.extend_with_white = true;
      }
      entry_sources.emplace_back(std::nullopt);
    }
    const auto to_rgb = [](const QColor& color) {
      return RgbColor{static_cast<std::uint8_t>(color.red()),
                      static_cast<std::uint8_t>(color.green()),
                      static_cast<std::uint8_t>(color.blue())};
    };
    stack.entries.push_back(make_gaussian_smart_filter_entry(
        initial_radius, to_rgb(canvas_->primary_color()),
        to_rgb(canvas_->secondary_color())));
  } else {
    const auto* current = layer->smart_filter_stack();
    if (current == nullptr ||
        current->support != SmartFilterStackSupport::Supported ||
        *execution_index >= current->entries.size()) {
      statusBar()->showMessage(
          tr("This Smart Filter can only be preserved, not edited"));
      return;
    }
    stack = *current;
    filter_index = *execution_index;
    const auto* gaussian = std::get_if<GaussianBlurSmartFilter>(
        &stack.entries[filter_index].parameters);
    if (stack.entries[filter_index].kind != SmartFilterKind::GaussianBlur ||
        gaussian == nullptr) {
      statusBar()->showMessage(
          tr("This Smart Filter can only be preserved, not edited"));
      return;
    }
    initial_radius = gaussian->radius_pixels;
  }

  const auto interpolation = canvas_->transform_interpolation();
  const auto unfiltered_preview = render_smart_object_unfiltered_layer_preview(
      std::as_const(doc), std::as_const(*layer), interpolation,
      parent_document_dir);
  if (!unfiltered_preview.has_value()) {
    statusBar()->showMessage(tr("Could not render this Smart Object"));
    return;
  }
  auto unfiltered_pixels =
      std::make_shared<const PixelBuffer>(unfiltered_preview->pixels);
  const auto unfiltered_bounds = unfiltered_preview->bounds;
  const auto filter_canvas_bounds =
      Rect::from_size(doc.width(), doc.height());
  auto original_pixels =
      std::make_shared<const PixelBuffer>(std::as_const(*layer).pixels());
  const auto original_bounds = layer->bounds();
  auto last_preview_bounds = std::make_shared<Rect>(original_bounds);
  auto preview_state =
      std::make_shared<AsyncPixelPreviewState<FilterPreviewSettings>>();
  preview_state->start =
      [this, preview_state, layer_id, stack, filter_index,
       initial_radius, unfiltered_pixels, unfiltered_bounds, original_pixels,
       original_bounds, filter_canvas_bounds,
       last_preview_bounds](const FilterPreviewSettings& settings) {
        if (!settings.preview_enabled) {
          preview_state->pending.reset();
          ++preview_state->generation;
          if (auto* preview_layer = document().find_layer(layer_id);
              preview_layer != nullptr) {
            preview_layer->set_pixels(*original_pixels);
            preview_layer->set_bounds(original_bounds);
            if (canvas_ != nullptr) {
              canvas_->document_changed(
                  to_qrect(*last_preview_bounds).united(
                      to_qrect(original_bounds)));
            }
            *last_preview_bounds = original_bounds;
          }
          return;
        }

        const auto radius = gaussian_radius_from_invocation(
            settings.invocation, initial_radius);
        const auto candidate =
            gaussian_stack_with_radius(stack, filter_index, radius);
        preview_state->in_flight = true;
        const auto generation = ++preview_state->generation;
        auto* app = QCoreApplication::instance();
        auto window = QPointer<MainWindow>(this);
        std::thread([app, window, preview_state, generation, layer_id,
                     unfiltered_pixels, unfiltered_bounds, last_preview_bounds,
                     filter_canvas_bounds, candidate] {
          auto result = std::make_shared<FilterRenderResult>();
          auto error = std::make_shared<QString>();
          try {
            *result = render_smart_filter_stack(
                *unfiltered_pixels, unfiltered_bounds, filter_canvas_bounds,
                candidate);
          } catch (const std::exception& caught) {
            *error = QString::fromUtf8(caught.what());
          }
          if (app == nullptr) {
            return;
          }
          QMetaObject::invokeMethod(
              app,
              [window, preview_state, generation, layer_id, last_preview_bounds,
               result, error]() mutable {
                preview_state->in_flight = false;
                const bool has_pending = preview_state->pending.has_value();
                if (!preview_state->closed && !has_pending &&
                    generation == preview_state->generation &&
                    window != nullptr) {
                  if (error->isEmpty()) {
                    if (auto* preview_layer =
                            window->document().find_layer(layer_id);
                        preview_layer != nullptr) {
                      preview_layer->set_pixels(std::move(result->pixels));
                      preview_layer->set_bounds(result->bounds);
                      if (window->canvas_ != nullptr) {
                        window->canvas_->document_changed(
                            to_qrect(*last_preview_bounds)
                                .united(to_qrect(result->bounds)));
                      }
                      *last_preview_bounds = result->bounds;
                    }
                  } else {
                    window->statusBar()->showMessage(
                        window->tr("Smart Filter preview failed: %1")
                            .arg(*error));
                  }
                }
                if (!preview_state->closed &&
                    preview_state->pending.has_value() &&
                    preview_state->start) {
                  auto next = *preview_state->pending;
                  preview_state->pending.reset();
                  preview_state->start(next);
                }
              },
              Qt::QueuedConnection);
        }).detach();
      };
  const auto preview_changed =
      [preview_state](FilterPreviewSettings settings) {
        enqueue_async_pixel_preview(preview_state, std::move(settings),
                                    !settings.preview_enabled);
      };

  auto invocation = FilterInvocation{};
  invocation.filter_id = "patchy.smart_filters.gaussian_blur";
  invocation.schema_version = 1U;
  invocation.parameters["radius"] = initial_radius;
  auto preview_edit_lock = lock_preview_dialog_edits();
  const auto settings = request_filter_settings(
      this, gaussian_smart_filter_dialog_spec(), preview_changed,
      std::move(invocation));
  close_async_pixel_preview(preview_state);
  layer = doc.find_layer(layer_id);
  if (layer == nullptr) {
    return;
  }
  layer->set_pixels(*original_pixels);
  layer->set_bounds(original_bounds);
  canvas_->document_changed(to_qrect(*last_preview_bounds)
                                .united(to_qrect(original_bounds)));
  preview_edit_lock.release();
  if (!settings.has_value()) {
    statusBar()->showMessage(tr("Cancelled Gaussian Blur"));
    return;
  }

  const auto radius =
      gaussian_radius_from_invocation(*settings, initial_radius);
  auto candidate =
      gaussian_stack_with_radius(std::move(stack), filter_index, radius);
  if (!commit_smart_filter_stack_edit(
          layer_id, std::move(candidate), std::move(entry_sources),
          adding ? tr("Add Gaussian Blur Smart Filter")
                 : tr("Edit Gaussian Blur Smart Filter"),
          adding ? (creating_stack
                        ? tr("Added Gaussian Blur as a Smart Filter")
                        : tr("Added another Gaussian Blur Smart Filter"))
                 : tr("Updated Gaussian Blur Smart Filter"))) {
    statusBar()->showMessage(
        tr("This Smart Filter descriptor cannot be edited safely"));
  }
}

void MainWindow::edit_smart_filter(LayerId layer_id,
                                   std::size_t execution_index) {
  gaussian_smart_filter_dialog(layer_id, execution_index);
}

void MainWindow::edit_smart_filter_blending(
    LayerId layer_id, std::size_t execution_index) {
  if (!has_active_document() || canvas_ == nullptr ||
      layer_id_locks_image_pixels(layer_id)) {
    return;
  }
  auto& doc = document();
  auto* layer = doc.find_layer(layer_id);
  const auto* current = layer != nullptr ? layer->smart_filter_stack() : nullptr;
  if (layer == nullptr || current == nullptr ||
      current->support != SmartFilterStackSupport::Supported ||
      execution_index >= current->entries.size() ||
      (!smart_object_lock_reason(*layer).empty() &&
       smart_object_lock_reason(*layer) != "external")) {
    statusBar()->showMessage(
        tr("This Smart Filter can only be preserved, not edited"));
    return;
  }

  const auto parent_document_dir =
      session().path.isEmpty() ? QString()
                               : QFileInfo(session().path).absolutePath();
  const auto interpolation = canvas_->transform_interpolation();
  const auto unfiltered_preview = render_smart_object_unfiltered_layer_preview(
      std::as_const(doc), std::as_const(*layer), interpolation,
      parent_document_dir);
  if (!unfiltered_preview.has_value()) {
    statusBar()->showMessage(tr("Could not render this Smart Object"));
    return;
  }

  const auto original_stack = *current;
  const auto initial = SmartFilterBlendingSettings{
      original_stack.entries[execution_index].blend_mode,
      original_stack.entries[execution_index].opacity};
  auto unfiltered_pixels =
      std::make_shared<const PixelBuffer>(unfiltered_preview->pixels);
  const auto unfiltered_bounds = unfiltered_preview->bounds;
  const auto filter_canvas_bounds = Rect::from_size(doc.width(), doc.height());
  auto original_pixels =
      std::make_shared<const PixelBuffer>(std::as_const(*layer).pixels());
  const auto original_bounds = layer->bounds();
  auto last_preview_bounds = std::make_shared<Rect>(original_bounds);
  using PreviewRequest =
      AdjustmentPixelPreviewRequest<SmartFilterBlendingSettings>;
  auto preview_state =
      std::make_shared<AsyncPixelPreviewState<PreviewRequest>>();
  preview_state->start =
      [this, preview_state, layer_id, original_stack, execution_index,
       unfiltered_pixels, unfiltered_bounds, original_pixels, original_bounds,
       filter_canvas_bounds,
       last_preview_bounds](const PreviewRequest& request) {
        if (!request.enabled) {
          preview_state->pending.reset();
          ++preview_state->generation;
          if (auto* preview_layer = document().find_layer(layer_id);
              preview_layer != nullptr) {
            preview_layer->set_pixels(*original_pixels);
            preview_layer->set_bounds(original_bounds);
            if (canvas_ != nullptr) {
              canvas_->document_changed(
                  to_qrect(*last_preview_bounds).united(
                      to_qrect(original_bounds)));
            }
            *last_preview_bounds = original_bounds;
          }
          return;
        }

        auto candidate = original_stack;
        candidate.entries[execution_index].blend_mode =
            request.settings.blend_mode;
        candidate.entries[execution_index].opacity =
            std::clamp(request.settings.opacity, 0.0, 1.0);
        preview_state->in_flight = true;
        const auto generation = ++preview_state->generation;
        auto* app = QCoreApplication::instance();
        auto window = QPointer<MainWindow>(this);
        std::thread([app, window, preview_state, generation, layer_id,
                     unfiltered_pixels, unfiltered_bounds, last_preview_bounds,
                     filter_canvas_bounds, candidate = std::move(candidate)] {
          auto result = std::make_shared<FilterRenderResult>();
          auto error = std::make_shared<QString>();
          try {
            *result = render_smart_filter_stack(
                *unfiltered_pixels, unfiltered_bounds, filter_canvas_bounds,
                candidate);
          } catch (const std::exception& caught) {
            *error = QString::fromUtf8(caught.what());
          }
          if (app == nullptr) {
            return;
          }
          QMetaObject::invokeMethod(
              app,
              [window, preview_state, generation, layer_id,
               last_preview_bounds, result, error]() mutable {
                preview_state->in_flight = false;
                const bool has_pending = preview_state->pending.has_value();
                if (!preview_state->closed && !has_pending &&
                    generation == preview_state->generation &&
                    window != nullptr) {
                  if (error->isEmpty()) {
                    if (auto* preview_layer =
                            window->document().find_layer(layer_id);
                        preview_layer != nullptr) {
                      preview_layer->set_pixels(std::move(result->pixels));
                      preview_layer->set_bounds(result->bounds);
                      if (window->canvas_ != nullptr) {
                        window->canvas_->document_changed(
                            to_qrect(*last_preview_bounds)
                                .united(to_qrect(result->bounds)));
                      }
                      *last_preview_bounds = result->bounds;
                    }
                  } else {
                    window->statusBar()->showMessage(
                        window->tr("Smart Filter preview failed: %1")
                            .arg(*error));
                  }
                }
                if (!preview_state->closed &&
                    preview_state->pending.has_value() &&
                    preview_state->start) {
                  auto next = *preview_state->pending;
                  preview_state->pending.reset();
                  preview_state->start(next);
                }
              },
              Qt::QueuedConnection);
        }).detach();
      };
  const auto preview_changed =
      [preview_state](bool enabled,
                      const SmartFilterBlendingSettings& settings) {
        enqueue_async_pixel_preview(
            preview_state, PreviewRequest{enabled, settings}, !enabled);
      };

  auto preview_edit_lock = lock_preview_dialog_edits();
  const auto settings = request_smart_filter_blending_settings(
      this, preview_changed, initial);
  close_async_pixel_preview(preview_state);
  layer = doc.find_layer(layer_id);
  if (layer == nullptr) {
    return;
  }
  layer->set_pixels(*original_pixels);
  layer->set_bounds(original_bounds);
  canvas_->document_changed(
      to_qrect(*last_preview_bounds).united(to_qrect(original_bounds)));
  preview_edit_lock.release();
  if (!settings.has_value()) {
    statusBar()->showMessage(tr("Cancelled Smart Filter blending options"));
    return;
  }
  if (settings->blend_mode == initial.blend_mode &&
      std::abs(settings->opacity - initial.opacity) < 0.0000001) {
    statusBar()->showMessage(tr("Smart Filter blending options unchanged"));
    return;
  }

  auto candidate = original_stack;
  candidate.entries[execution_index].blend_mode = settings->blend_mode;
  candidate.entries[execution_index].opacity =
      std::clamp(settings->opacity, 0.0, 1.0);
  if (!commit_smart_filter_stack_edit(
          layer_id, std::move(candidate), {},
          tr("Edit Smart Filter Blending Options"),
          tr("Updated Smart Filter blending options"))) {
    statusBar()->showMessage(
        tr("This Smart Filter descriptor cannot be edited safely"));
  }
}

void MainWindow::set_smart_filter_stack_enabled(LayerId layer_id,
                                                bool enabled) {
  if (!has_active_document() || canvas_ == nullptr) {
    return;
  }
  if (layer_id_locks_image_pixels(layer_id)) {
    statusBar()->showMessage(tr("Layer pixels are locked."));
    refresh_layer_list();
    return;
  }
  auto& doc = document();
  auto* layer = doc.find_layer(layer_id);
  const auto* current = layer != nullptr ? layer->smart_filter_stack() : nullptr;
  if (layer == nullptr || current == nullptr ||
      current->support != SmartFilterStackSupport::Supported ||
      current->enabled == enabled ||
      (!smart_object_lock_reason(*layer).empty() &&
       smart_object_lock_reason(*layer) != "external")) {
    return;
  }
  auto candidate = *current;
  candidate.enabled = enabled;
  if (!commit_smart_filter_stack_edit(
          layer_id, std::move(candidate), {},
          enabled ? tr("Show Smart Filters") : tr("Hide Smart Filters"),
          enabled ? tr("Smart Filters shown") : tr("Smart Filters hidden"))) {
    statusBar()->showMessage(
        tr("This Smart Filter descriptor cannot be edited safely"));
    refresh_layer_list();
  }
}

void MainWindow::set_smart_filter_enabled(LayerId layer_id,
                                          std::size_t execution_index,
                                          bool enabled) {
  if (!has_active_document() || canvas_ == nullptr) {
    return;
  }
  if (layer_id_locks_image_pixels(layer_id)) {
    statusBar()->showMessage(tr("Layer pixels are locked."));
    refresh_layer_list();
    return;
  }
  auto& doc = document();
  auto* layer = doc.find_layer(layer_id);
  const auto* current = layer != nullptr ? layer->smart_filter_stack() : nullptr;
  if (layer == nullptr || current == nullptr ||
      current->support != SmartFilterStackSupport::Supported ||
      execution_index >= current->entries.size() ||
      current->entries[execution_index].enabled == enabled ||
      (!smart_object_lock_reason(*layer).empty() &&
       smart_object_lock_reason(*layer) != "external")) {
    return;
  }
  auto candidate = *current;
  candidate.entries[execution_index].enabled = enabled;
  if (!commit_smart_filter_stack_edit(
          layer_id, std::move(candidate), {},
          enabled ? tr("Show Smart Filter") : tr("Hide Smart Filter"),
          enabled ? tr("Smart Filter shown") : tr("Smart Filter hidden"))) {
    statusBar()->showMessage(
        tr("This Smart Filter descriptor cannot be edited safely"));
    refresh_layer_list();
  }
}

void MainWindow::set_smart_filter_mask_enabled(LayerId layer_id,
                                               bool enabled) {
  if (!has_active_document() || canvas_ == nullptr ||
      layer_id_locks_image_pixels(layer_id)) {
    return;
  }
  const auto* layer = std::as_const(document()).find_layer(layer_id);
  const auto* current = layer != nullptr ? layer->smart_filter_stack() : nullptr;
  if (current == nullptr ||
      current->support != SmartFilterStackSupport::Supported ||
      current->mask.pixels.empty() || current->mask.enabled == enabled ||
      (!smart_object_lock_reason(*layer).empty() &&
       smart_object_lock_reason(*layer) != "external")) {
    return;
  }
  auto candidate = *current;
  candidate.mask.enabled = enabled;
  if (!commit_smart_filter_stack_edit(
          layer_id, std::move(candidate), {},
          enabled ? tr("Enable Smart Filter Mask")
                  : tr("Disable Smart Filter Mask"),
          enabled ? tr("Smart Filter mask enabled")
                  : tr("Smart Filter mask disabled"))) {
    statusBar()->showMessage(
        tr("This Smart Filter descriptor cannot be edited safely"));
    refresh_layer_list();
  }
}

bool MainWindow::commit_smart_filter_mask_edit(
    CanvasWidget* source_canvas, LayerId layer_id, QString undo_text,
    PixelBuffer pixels) {
  auto* target_session = session_for_canvas(source_canvas);
  if (target_session == nullptr || source_canvas == nullptr || pixels.empty() ||
      pixels.format() != PixelFormat::gray8() ||
      pixels.width() != target_session->document.width() ||
      pixels.height() != target_session->document.height()) {
    return false;
  }
  const auto* layer =
      std::as_const(target_session->document).find_layer(layer_id);
  const auto* current = layer != nullptr ? layer->smart_filter_stack() : nullptr;
  if (current == nullptr ||
      current->support != SmartFilterStackSupport::Supported ||
      current->mask.pixels.empty() ||
      (!smart_object_lock_reason(*layer).empty() &&
       smart_object_lock_reason(*layer) != "external")) {
    return false;
  }
  auto candidate = *current;
  candidate.mask.bounds = Rect::from_size(target_session->document.width(),
                                          target_session->document.height());
  candidate.mask.pixels = std::move(pixels);
  if (!commit_smart_filter_stack_edit(
          *target_session, source_canvas, layer_id, std::move(candidate), {},
          undo_text, tr("Updated Smart Filter mask"),
          SmartFilterCacheEdit::ReplaceMask)) {
    statusBar()->showMessage(
        tr("This Smart Filter mask cannot be edited safely"));
    return false;
  }
  return true;
}

void MainWindow::duplicate_smart_filter(LayerId layer_id,
                                        std::size_t execution_index) {
  if (!has_active_document()) {
    return;
  }
  const auto* layer = std::as_const(document()).find_layer(layer_id);
  const auto* current = layer != nullptr ? layer->smart_filter_stack() : nullptr;
  if (current == nullptr ||
      current->support != SmartFilterStackSupport::Supported ||
      execution_index >= current->entries.size()) {
    return;
  }
  auto candidate = *current;
  candidate.entries.insert(
      candidate.entries.begin() +
          static_cast<std::ptrdiff_t>(execution_index + 1U),
      candidate.entries[execution_index]);
  std::vector<std::optional<std::size_t>> sources;
  sources.reserve(candidate.entries.size());
  for (std::size_t index = 0; index < current->entries.size(); ++index) {
    sources.emplace_back(index);
    if (index == execution_index) {
      sources.emplace_back(index);
    }
  }
  if (!commit_smart_filter_stack_edit(
          layer_id, std::move(candidate), std::move(sources),
          tr("Duplicate Smart Filter"), tr("Duplicated Smart Filter"))) {
    statusBar()->showMessage(
        tr("This Smart Filter descriptor cannot be edited safely"));
    refresh_layer_list();
  }
}

void MainWindow::move_smart_filter(LayerId layer_id,
                                   std::size_t execution_index,
                                   int visual_direction) {
  if (!has_active_document() || visual_direction == 0) {
    return;
  }
  const auto* layer = std::as_const(document()).find_layer(layer_id);
  const auto* current = layer != nullptr ? layer->smart_filter_stack() : nullptr;
  if (current == nullptr ||
      current->support != SmartFilterStackSupport::Supported ||
      execution_index >= current->entries.size()) {
    return;
  }
  const auto target = visual_direction < 0
                          ? execution_index + 1U
                          : execution_index == 0U
                                ? current->entries.size()
                                : execution_index - 1U;
  if (target >= current->entries.size()) {
    return;
  }
  auto candidate = *current;
  std::swap(candidate.entries[execution_index], candidate.entries[target]);
  std::vector<std::optional<std::size_t>> sources;
  sources.reserve(current->entries.size());
  for (std::size_t index = 0; index < current->entries.size(); ++index) {
    sources.emplace_back(index);
  }
  std::swap(sources[execution_index], sources[target]);
  if (!commit_smart_filter_stack_edit(
          layer_id, std::move(candidate), std::move(sources),
          tr("Reorder Smart Filters"), tr("Reordered Smart Filters"))) {
    statusBar()->showMessage(
        tr("This Smart Filter descriptor cannot be edited safely"));
    refresh_layer_list();
  }
}

void MainWindow::delete_smart_filter(LayerId layer_id,
                                     std::size_t execution_index) {
  if (!has_active_document() || canvas_ == nullptr) {
    return;
  }
  if (layer_id_locks_image_pixels(layer_id)) {
    statusBar()->showMessage(tr("Layer pixels are locked."));
    refresh_layer_list();
    return;
  }
  auto& doc = document();
  auto* layer = doc.find_layer(layer_id);
  const auto* current = layer != nullptr ? layer->smart_filter_stack() : nullptr;
  if (layer == nullptr || current == nullptr ||
      current->support != SmartFilterStackSupport::Supported ||
      execution_index >= current->entries.size() ||
      (!smart_object_lock_reason(*layer).empty() &&
       smart_object_lock_reason(*layer) != "external")) {
    return;
  }
  if (current->entries.size() == 1U) {
    if (!commit_smart_filter_stack_edit(
            layer_id, std::nullopt, {}, tr("Delete Smart Filter"),
            tr("Deleted Smart Filter"))) {
      statusBar()->showMessage(
          tr("This Smart Filter descriptor cannot be edited safely"));
      refresh_layer_list();
    }
    return;
  }

  auto candidate = *current;
  candidate.entries.erase(
      candidate.entries.begin() +
      static_cast<std::ptrdiff_t>(execution_index));
  std::vector<std::optional<std::size_t>> sources;
  sources.reserve(candidate.entries.size());
  for (std::size_t index = 0; index < current->entries.size(); ++index) {
    if (index != execution_index) {
      sources.emplace_back(index);
    }
  }
  if (!commit_smart_filter_stack_edit(
          layer_id, std::move(candidate), std::move(sources),
          tr("Delete Smart Filter"), tr("Deleted Smart Filter"))) {
    statusBar()->showMessage(
        tr("This Smart Filter descriptor cannot be edited safely"));
    refresh_layer_list();
  }
}

void MainWindow::apply_filter(const QString& identifier) {
  if (canvas_ != nullptr && canvas_->quick_mask_active()) {
    statusBar()->showMessage(
        tr("Filters are unavailable in Quick Mask mode"));
    return;
  }
  if (canvas_ != nullptr &&
      (canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::DocumentChannel ||
       canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::ComponentRed ||
       canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::ComponentGreen ||
       canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::ComponentBlue)) {
    statusBar()->showMessage(tr("Filters are unavailable while viewing a document channel"));
    return;
  }
  auto& doc = document();
  auto active = doc.active_layer_id();
  if (!active.has_value()) {
    return;
  }
  auto* layer = doc.find_layer(*active);
  if (layer == nullptr || layer->kind() != LayerKind::Pixel ||
      std::as_const(*layer).pixels().format().bit_depth != BitDepth::UInt8 ||
      std::as_const(*layer).pixels().format().channels < 3) {
    statusBar()->showMessage(tr("Select an editable RGB pixel layer"));
    return;
  }
  if (layer_id_locks_image_pixels(*active)) {
    statusBar()->showMessage(tr("Layer pixels are locked."));
    return;
  }
  if (layer_is_smart_object(*layer)) {
    const auto lock = smart_object_lock_reason(*layer);
    if ((!lock.empty() && lock != "external") ||
        (layer->smart_filter_stack() != nullptr &&
         layer->smart_filter_stack()->support !=
             SmartFilterStackSupport::Supported)) {
      statusBar()->showMessage(
          tr("This Smart Object can only preserve its imported filters"));
      return;
    }
    if (identifier == QStringLiteral("patchy.filters.gaussian_blur")) {
      gaussian_smart_filter_dialog(*active);
      return;
    }
    statusBar()->showMessage(
        tr("Only Gaussian Blur is currently editable as a Smart Filter"));
    return;
  }
  try {
    const auto identifier_text = identifier.toStdString();
    const auto* filter = filters_.find(identifier_text);
    if (filter == nullptr) {
      throw std::invalid_argument("Unknown filter identifier");
    }
    const auto display_name = filter_display_name(*filter);
    const auto dialog_spec = filter_dialog_spec_for(*filter);
    const auto selection = canvas_->selected_document_region();
    const auto bounds = layer->bounds();
    auto original_pixels =
        std::make_shared<const PixelBuffer>(std::as_const(*layer).pixels());
    // Tracks the bounds the layer currently shows in the preview. Blur-family
    // filters grow the layer, so each swap must repaint the union of the previous
    // and new bounds to erase any stale halo left behind when the layer shrinks.
    auto last_preview_bounds = std::make_shared<Rect>(bounds);
    const auto foreground_color = canvas_->primary_color();
    const auto background_color = canvas_->secondary_color();
    const auto to_rgb = [](const QColor& color) {
      return RgbColor{static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()),
                      static_cast<std::uint8_t>(color.blue())};
    };
    auto initial_invocation = filters_.default_invocation(identifier_text, to_rgb(foreground_color),
                                                          to_rgb(background_color));
    auto preview_registry = std::make_shared<FilterRegistry>(filters_);
    auto preview_state = std::make_shared<AsyncPixelPreviewState<FilterPreviewSettings>>();
    preview_state->start =
        [this, preview_state, active, original_pixels, last_preview_bounds, selection, bounds,
         preview_registry](const FilterPreviewSettings& settings) {
          if (!settings.preview_enabled) {
            preview_state->pending.reset();
            ++preview_state->generation;
            if (auto* preview_layer = document().find_layer(*active); preview_layer != nullptr) {
              set_layer_pixels_preserving_origin(*preview_layer, *original_pixels, bounds);
              if (canvas_ != nullptr) {
                canvas_->document_changed(to_qrect(*last_preview_bounds).united(to_qrect(bounds)));
              }
              *last_preview_bounds = bounds;
            }
            return;
          }

          preview_state->in_flight = true;
          const auto generation = ++preview_state->generation;
          auto result_bounds = std::make_shared<Rect>(bounds);
          auto* app = QCoreApplication::instance();
          auto window = QPointer<MainWindow>(this);
          std::thread([app, window, preview_state, generation, original_pixels, result_bounds, last_preview_bounds,
                       selection, bounds, settings, preview_registry, active] {
            auto result = std::make_shared<PixelBuffer>();
            auto error = std::make_shared<QString>();
            try {
              *result = build_filter_preview_pixels(*original_pixels, selection, bounds, *preview_registry, settings,
                                                    nullptr, &*result_bounds);
            } catch (const std::exception& caught) {
              *error = QString::fromUtf8(caught.what());
            }
            if (app == nullptr) {
              return;
            }
            QMetaObject::invokeMethod(
                app,
                [window, preview_state, generation, active, result_bounds, last_preview_bounds, result,
                 error]() mutable {
                  preview_state->in_flight = false;
                  const auto has_pending = preview_state->pending.has_value();
                  if (!preview_state->closed && !has_pending && generation == preview_state->generation &&
                      window != nullptr) {
                    if (error->isEmpty()) {
                      if (auto* layer = window->document().find_layer(*active); layer != nullptr) {
                        set_layer_pixels_with_bounds(*layer, std::move(*result), *result_bounds);
                        if (window->canvas_ != nullptr) {
                          window->canvas_->document_changed(
                              to_qrect(*last_preview_bounds).united(to_qrect(*result_bounds)));
                        }
                        *last_preview_bounds = *result_bounds;
                      }
                    } else {
                      window->statusBar()->showMessage(
                          window->tr("Filter preview failed: %1").arg(*error));
                    }
                  }
                  if (!preview_state->closed && preview_state->pending.has_value() && preview_state->start) {
                    auto next = *preview_state->pending;
                    preview_state->pending.reset();
                    preview_state->start(next);
                  }
                },
                Qt::QueuedConnection);
          }).detach();
        };
    const auto preview_changed = [preview_state](FilterPreviewSettings settings) {
      enqueue_async_pixel_preview(preview_state, std::move(settings), !settings.preview_enabled);
    };

    auto preview_edit_lock = lock_preview_dialog_edits();
    const auto settings = request_filter_settings(this, dialog_spec, preview_changed, std::move(initial_invocation));
    close_async_pixel_preview(preview_state);
    layer = doc.find_layer(*active);
    if (layer == nullptr) {
      return;
    }
    set_layer_pixels_preserving_origin(*layer, *original_pixels, bounds);
    canvas_->document_changed(to_qrect(*last_preview_bounds).united(to_qrect(bounds)));
    *last_preview_bounds = bounds;
    preview_edit_lock.release();
    if (!settings.has_value()) {
      statusBar()->showMessage(tr("Cancelled %1").arg(display_name));
      return;
    }

    if (canvas_ != nullptr) {
      canvas_->begin_processing_operation();
    }
    const auto finish_processing = qScopeGuard([this] {
      if (canvas_ != nullptr) {
        canvas_->end_processing_operation();
      }
    });
    QProgressDialog progress(tr("Applying %1...").arg(display_name), tr("Cancel"), 0, 100, this);
    progress.setObjectName(QStringLiteral("filterProgressDialog"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(kFilterProgressMinimumDurationMs);
    remember_dialog_position(progress);
    progress.setValue(0);
    int last_progress_value = -1;
    FilterProgress filter_progress{[&](int completed, int total, FilterProgressStage stage) {
      const auto value = total <= 0 ? 100 : std::clamp((completed * 100) / total, 0, 100);
      if (value != last_progress_value) {
        progress.setValue(value);
        progress.setLabelText(tr("Applying %1...\n%2").arg(display_name, filter_progress_stage_text(stage)));
        last_progress_value = value;
        QApplication::processEvents();
      }
      if (canvas_ != nullptr) {
        canvas_->tick_processing_operation();
      }
      return !progress.wasCanceled();
    }};

    PixelBuffer final_pixels;
    Rect final_bounds = bounds;
    try {
      final_pixels = build_filter_preview_pixels(*original_pixels, selection, bounds, filters_,
                                                 FilterPreviewSettings{true, *settings}, &filter_progress,
                                                 &final_bounds);
      progress.setValue(100);
    } catch (const FilterCancelled&) {
      layer = doc.find_layer(*active);
      if (layer != nullptr) {
        set_layer_pixels_preserving_origin(*layer, *original_pixels, bounds);
        canvas_->document_changed(to_qrect(*last_preview_bounds).united(to_qrect(bounds)));
        *last_preview_bounds = bounds;
      }
      statusBar()->showMessage(tr("Cancelled %1").arg(display_name));
      return;
    }
    if (pixel_buffers_equal(final_pixels, *original_pixels)) {
      statusBar()->showMessage(tr("%1 made no changes").arg(display_name));
      return;
    }

    push_undo_snapshot(tr("Filter: %1").arg(display_name));
    layer = doc.find_layer(*active);
    if (layer == nullptr) {
      return;
    }
    set_layer_pixels_with_bounds(*layer, std::move(final_pixels), final_bounds);
    canvas_->document_changed(to_qrect(*last_preview_bounds).united(to_qrect(final_bounds)));
    statusBar()->showMessage(tr("Applied %1").arg(display_name));
  } catch (const std::exception& error) {
    if (active.has_value()) {
      if (auto* restore_layer = doc.find_layer(*active); restore_layer != nullptr) {
        canvas_->document_changed(to_qrect(restore_layer->bounds()));
      }
    }
    show_critical_message(this, tr("Filter failed"), QString::fromUtf8(error.what()),
                          QStringLiteral("filterFailedMessageBox"));
  }
}

void MainWindow::visual_filter_gallery_dialog() {
  if (canvas_ != nullptr && canvas_->quick_mask_active()) {
    statusBar()->showMessage(
        tr("Filters are unavailable in Quick Mask mode"));
    return;
  }
  if (canvas_ != nullptr &&
      (canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::DocumentChannel ||
       canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::ComponentRed ||
       canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::ComponentGreen ||
       canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::ComponentBlue)) {
    statusBar()->showMessage(tr("Filters are unavailable while viewing a document channel"));
    return;
  }

  auto* target_session = active_session();
  if (target_session == nullptr || target_session->canvas == nullptr) {
    return;
  }
  const auto active = target_session->document.active_layer_id();
  if (!active.has_value()) {
    return;
  }
  // Reads through the const layer are important here: mutable Layer accessors
  // bump content revisions on access, which would dirty render caches even when
  // the user only opened and cancelled the gallery.
  const auto& source_document = std::as_const(target_session->document);
  const auto* source_layer = source_document.find_layer(*active);
  if (!editable_rgb8_layer(source_layer)) {
    statusBar()->showMessage(tr("Select an editable RGB pixel layer"));
    return;
  }
  const bool smart_object_target = layer_is_smart_object(*source_layer);
  if (smart_object_target) {
    const auto lock = smart_object_lock_reason(*source_layer);
    const auto* stack = source_layer->smart_filter_stack();
    const auto document_pixels =
        static_cast<std::uint64_t>(source_document.width()) *
        static_cast<std::uint64_t>(source_document.height());
    if ((!lock.empty() && lock != "external") ||
        (stack != nullptr &&
         stack->support != SmartFilterStackSupport::Supported)) {
      statusBar()->showMessage(
          tr("This Smart Object can only preserve its imported filters"));
      return;
    }
    if (document_pixels > psd::kMaximumEditableSmartFilterMaskPixels) {
      statusBar()->showMessage(tr(
          "Editable Smart Filters currently support documents up to 64 megapixels"));
      return;
    }
  }
  if (layer_id_locks_image_pixels(*active)) {
    statusBar()->showMessage(tr("Layer pixels are locked."));
    return;
  }

  const auto session_id = target_session->session_id;
  const auto layer_id = *active;
  const auto bounds = source_layer->bounds();
  auto original_pixels = std::make_shared<const PixelBuffer>(source_layer->pixels());
  const auto selection = target_session->canvas->selected_document_region();
  const auto foreground_color = target_session->canvas->primary_color();
  const auto background_color = target_session->canvas->secondary_color();
  const auto to_rgb = [](const QColor& color) {
    return RgbColor{static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()),
                    static_cast<std::uint8_t>(color.blue())};
  };
  const auto foreground = to_rgb(foreground_color);
  const auto background = to_rgb(background_color);
  std::optional<SmartFilterStack> native_base_stack;
  SmartFilterMask native_new_stack_mask;
  std::shared_ptr<const PixelBuffer> native_unfiltered_pixels;
  Rect native_unfiltered_bounds{};
  if (smart_object_target) {
    if (source_layer->smart_filter_stack() != nullptr) {
      native_base_stack = *source_layer->smart_filter_stack();
    }
    native_new_stack_mask.bounds =
        Rect::from_size(source_document.width(), source_document.height());
    native_new_stack_mask.enabled = true;
    native_new_stack_mask.linked = false;
    if (target_session->canvas->has_selection()) {
      native_new_stack_mask.pixels =
          target_session->canvas->selection_as_grayscale();
      native_new_stack_mask.default_color = 0U;
      native_new_stack_mask.extend_with_white = false;
    } else {
      native_new_stack_mask.pixels = PixelBuffer(
          source_document.width(), source_document.height(),
          PixelFormat::gray8());
      native_new_stack_mask.pixels.clear(255U);
      native_new_stack_mask.default_color = 255U;
      native_new_stack_mask.extend_with_white = true;
    }
    const auto parent_document_dir =
        target_session->path.isEmpty()
            ? QString()
            : QFileInfo(target_session->path).absolutePath();
    const auto unfiltered = render_smart_object_unfiltered_layer_preview(
        source_document, *source_layer,
        target_session->canvas->transform_interpolation(),
        parent_document_dir);
    if (!unfiltered.has_value()) {
      statusBar()->showMessage(tr("Could not render this Smart Object"));
      return;
    }
    native_unfiltered_pixels =
        std::make_shared<const PixelBuffer>(unfiltered->pixels);
    native_unfiltered_bounds = unfiltered->bounds;
  }
  auto last_preview_bounds = std::make_shared<Rect>(bounds);
  auto preview_shows_original = std::make_shared<bool>(true);
  QPointer<CanvasWidget> target_canvas(target_session->canvas);

  const auto restore_original = [this, session_id, layer_id, original_pixels, bounds, last_preview_bounds,
                                 preview_shows_original, target_canvas] {
    if (*preview_shows_original) {
      *last_preview_bounds = bounds;
      return;
    }
    auto* live_session = session_with_id(session_id);
    if (live_session == nullptr) {
      return;
    }
    auto* live_layer = live_session->document.find_layer(layer_id);
    if (live_layer == nullptr) {
      return;
    }
    const auto dirty = to_qrect(*last_preview_bounds).united(to_qrect(bounds));
    set_layer_pixels_preserving_origin(*live_layer, *original_pixels, bounds);
    *last_preview_bounds = bounds;
    *preview_shows_original = true;
    if (target_canvas != nullptr) {
      target_canvas->document_changed(dirty);
    }
  };

  try {
    auto preview_registry = std::make_shared<const FilterRegistry>(filters_);
    const auto native_filter_canvas_bounds =
        Rect::from_size(source_document.width(), source_document.height());
    using PreviewState = LatestCancellablePixelPreviewState<FilterRecipe>;
    auto preview_state = std::make_shared<PreviewState>();
    preview_state->start =
        [this, preview_state, preview_registry, original_pixels, selection,
         bounds, session_id, layer_id, last_preview_bounds,
         preview_shows_original, target_canvas](PreviewState::Work work) {
          preview_state->in_flight = true;
          auto result = std::make_shared<PixelBuffer>();
          auto result_bounds = std::make_shared<Rect>(bounds);
          auto error = std::make_shared<QString>();
          auto cancelled = std::make_shared<bool>(false);
          auto* app = QCoreApplication::instance();
          auto window = QPointer<MainWindow>(this);
          const auto generation = work.generation;
          std::thread([app, window, preview_state, preview_registry,
                       original_pixels, selection, bounds, session_id,
                       layer_id, last_preview_bounds, preview_shows_original,
                       target_canvas, generation,
                       recipe = std::move(work.request), result,
                       result_bounds, error, cancelled] {
            FilterProgress cancellation_progress{
                [preview_state, generation](int, int, FilterProgressStage) {
                  return preview_state->generation.load(std::memory_order_acquire) == generation;
                }};
            try {
              *result = build_filter_preview_pixels(
                  *original_pixels, selection, bounds, *preview_registry,
                  recipe, &cancellation_progress, &*result_bounds);
            } catch (const FilterCancelled&) {
              *cancelled = true;
            } catch (const std::exception& caught) {
              *error = QString::fromUtf8(caught.what());
            }
            if (app == nullptr) {
              return;
            }
            QMetaObject::invokeMethod(
                app,
                [window, preview_state, session_id, layer_id, last_preview_bounds, preview_shows_original,
                 target_canvas, generation, result, result_bounds, error, cancelled]() mutable {
                  preview_state->in_flight = false;
                  const auto has_pending = preview_state->pending.has_value();
                  const auto is_latest =
                      generation == preview_state->generation.load(std::memory_order_acquire);
                  if (!preview_state->closed && !has_pending && is_latest && !*cancelled && window != nullptr) {
                    if (error->isEmpty()) {
                      if (auto* live_session = window->session_with_id(session_id); live_session != nullptr) {
                        if (auto* live_layer = live_session->document.find_layer(layer_id); live_layer != nullptr) {
                          const auto dirty =
                              to_qrect(*last_preview_bounds).united(to_qrect(*result_bounds));
                          set_layer_pixels_with_bounds(*live_layer, std::move(*result), *result_bounds);
                          *last_preview_bounds = *result_bounds;
                          *preview_shows_original = false;
                          if (target_canvas != nullptr) {
                            target_canvas->document_changed(dirty);
                          }
                        }
                      }
                    } else {
                      window->statusBar()->showMessage(
                          window->tr("Filter preview failed: %1").arg(*error));
                    }
                  }
                  if (!preview_state->closed && preview_state->pending.has_value() && preview_state->start) {
                    auto next = std::move(*preview_state->pending);
                    preview_state->pending.reset();
                    preview_state->start(std::move(next));
                  }
                },
                Qt::QueuedConnection);
          }).detach();
        };

    const auto preview_changed =
        [preview_state, restore_original, preview_registry,
         smart_object_target, native_base_stack,
         native_new_stack_mask](const VisualFilterGalleryPreview& preview) {
      if (!preview.canvas_enabled || !preview.recipe.has_value()) {
        cancel_latest_cancellable_pixel_preview(preview_state);
        restore_original();
        return;
      }
      if (smart_object_target &&
          !smart_filter_stack_with_recipe(native_base_stack,
                                          native_new_stack_mask,
                                          *preview.recipe,
                                          *preview_registry)
               .has_value()) {
        cancel_latest_cancellable_pixel_preview(preview_state);
        restore_original();
        return;
      }
      if (smart_object_target) {
        return;
      }
      enqueue_latest_cancellable_pixel_preview(preview_state, *preview.recipe);
    };

    VisualFilterGalleryExactRecipeRenderer exact_recipe_renderer;
    VisualFilterGalleryExactPreviewCallback exact_preview_ready;
    if (smart_object_target) {
      exact_recipe_renderer =
          [preview_registry, native_base_stack, native_new_stack_mask,
           native_unfiltered_pixels, native_unfiltered_bounds,
           native_filter_canvas_bounds](
              const FilterRecipe& recipe,
              const FilterProgress* progress)
              -> std::optional<FilterRenderResult> {
        const auto candidate = smart_filter_stack_with_recipe(
            native_base_stack, native_new_stack_mask, recipe,
            *preview_registry);
        if (!candidate.has_value() || native_unfiltered_pixels == nullptr) {
          return std::nullopt;
        }
        return render_smart_filter_stack(
            *native_unfiltered_pixels, native_unfiltered_bounds,
            native_filter_canvas_bounds, *candidate, progress);
      };
      exact_preview_ready =
          [this, session_id, layer_id, last_preview_bounds,
           preview_shows_original, target_canvas,
           restore_original](const VisualFilterGalleryExactPreview& preview) {
        if (!preview.canvas_enabled || preview.rendered == nullptr) {
          restore_original();
          return;
        }
        auto* live_session = session_with_id(session_id);
        if (live_session == nullptr) {
          return;
        }
        auto* live_layer = live_session->document.find_layer(layer_id);
        if (live_layer == nullptr) {
          return;
        }
        const auto dirty =
            to_qrect(*last_preview_bounds)
                .united(to_qrect(preview.rendered->bounds));
        set_layer_pixels_with_bounds(*live_layer, preview.rendered->pixels,
                                     preview.rendered->bounds);
        *last_preview_bounds = preview.rendered->bounds;
        *preview_shows_original = false;
        if (target_canvas != nullptr) {
          target_canvas->document_changed(dirty);
        }
      };
    }

    auto preview_edit_lock = lock_preview_dialog_edits();
    VisualFilterGalleryResult result;
    try {
      result = request_visual_filter_gallery(
          this, *original_pixels, bounds, selection, filters_, foreground,
          background, preview_changed, nullptr,
          std::move(exact_recipe_renderer), std::move(exact_preview_ready));
    } catch (...) {
      close_latest_cancellable_pixel_preview(preview_state);
      restore_original();
      throw;
    }
    close_latest_cancellable_pixel_preview(preview_state);
    restore_original();

    if (result.outcome == VisualFilterGalleryOutcome::Cancelled) {
      statusBar()->showMessage(tr("Cancelled Visual Filters & Looks"));
      return;
    }
    if (result.outcome == VisualFilterGalleryOutcome::Original || !result.recipe.has_value()) {
      statusBar()->showMessage(tr("No visual filter applied"));
      return;
    }
    if (!filters_.supports(*result.recipe)) {
      throw std::invalid_argument("Unsupported visual filter recipe");
    }
    const auto enabled_effect_count = std::count_if(
        result.recipe->entries.begin(), result.recipe->entries.end(),
        [](const FilterRecipeEntry& entry) {
          return entry.enabled && entry.opacity > 0.0;
        });
    if (enabled_effect_count == 0) {
      statusBar()->showMessage(tr("No visual filter applied"));
      return;
    }
    QString display_name;
    if (result.recipe->entries.size() == 1) {
      if (const auto* filter =
              filters_.find(result.recipe->entries.front().invocation.filter_id);
          filter != nullptr) {
        display_name = filter_display_name(*filter);
      }
    }
    if (display_name.isEmpty()) {
      display_name = tr("Visual Filter Stack");
    }

    bool rasterize_smart_object_for_recipe = false;
    if (smart_object_target) {
      auto candidate = smart_filter_stack_with_recipe(
          native_base_stack, native_new_stack_mask, *result.recipe,
          filters_);
      if (!candidate.has_value()) {
        const auto answer = show_warning_message(
            this, tr("Rasterize Smart Object?"),
            tr("This Look includes effects without an editable Photoshop Smart Filter mapping. Rasterize the Smart Object and apply the complete Look destructively?"),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel,
            QStringLiteral("filterGalleryRasterizeMessageBox"));
        if (answer != QMessageBox::Yes) {
          statusBar()->showMessage(tr("Cancelled Visual Filters & Looks"));
          return;
        }
        rasterize_smart_object_for_recipe = true;
      } else {
        std::vector<std::optional<std::size_t>> entry_sources;
        const auto existing_count =
            native_base_stack.has_value()
                ? native_base_stack->entries.size()
                : 0U;
        entry_sources.reserve(candidate->entries.size());
        for (std::size_t index = 0; index < existing_count; ++index) {
          entry_sources.emplace_back(index);
        }
        for (std::size_t index = existing_count;
             index < candidate->entries.size(); ++index) {
          entry_sources.emplace_back(std::nullopt);
        }
        if (!commit_smart_filter_stack_edit(
                layer_id, std::move(*candidate), std::move(entry_sources),
                tr("Add Smart Filter Stack"),
                tr("Added %1 as editable Smart Filters").arg(display_name))) {
          statusBar()->showMessage(tr(
              "This Smart Filter descriptor cannot be edited safely"));
        }
        return;
      }
    }

    if (target_canvas != nullptr) {
      target_canvas->begin_processing_operation();
    }
    const auto finish_processing = qScopeGuard([target_canvas] {
      if (target_canvas != nullptr) {
        target_canvas->end_processing_operation();
      }
    });
    QProgressDialog progress(tr("Applying %1...").arg(display_name), tr("Cancel"), 0, 100, this);
    progress.setObjectName(QStringLiteral("filterGalleryProgressDialog"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(kFilterProgressMinimumDurationMs);
    remember_dialog_position(progress);
    progress.setValue(0);
    int last_progress_value = -1;
    FilterProgress filter_progress{[&](int completed, int total, FilterProgressStage stage) {
      const auto value = total <= 0 ? 100 : std::clamp((completed * 100) / total, 0, 100);
      if (value != last_progress_value) {
        progress.setValue(value);
        progress.setLabelText(tr("Applying %1...\n%2").arg(display_name, filter_progress_stage_text(stage)));
        last_progress_value = value;
        QApplication::processEvents();
      }
      if (target_canvas != nullptr) {
        target_canvas->tick_processing_operation();
      }
      return !progress.wasCanceled();
    }};

    FilterRenderResult final_result;
    try {
      final_result.bounds = bounds;
      final_result.pixels = build_filter_preview_pixels(
          *original_pixels, selection, bounds, filters_, *result.recipe,
          &filter_progress, &final_result.bounds);
      snap_filter_result_to_palette(final_result.pixels, final_result.bounds, selection,
                                    target_canvas != nullptr ? target_canvas->palette_snap_context() : nullptr);
      progress.setValue(100);
    } catch (const FilterCancelled&) {
      restore_original();
      statusBar()->showMessage(tr("Cancelled %1").arg(display_name));
      return;
    }

    const auto bounds_unchanged = final_result.bounds.x == bounds.x && final_result.bounds.y == bounds.y &&
                                  final_result.bounds.width == bounds.width &&
                                  final_result.bounds.height == bounds.height;
    if (bounds_unchanged && pixel_buffers_equal(final_result.pixels, *original_pixels)) {
      statusBar()->showMessage(tr("%1 made no changes").arg(display_name));
      return;
    }

    auto* live_session = session_with_id(session_id);
    if (live_session == nullptr || live_session->document.find_layer(layer_id) == nullptr) {
      return;
    }
    push_undo_snapshot(*live_session, tr("Filter: %1").arg(display_name));
    live_session = session_with_id(session_id);
    if (live_session == nullptr) {
      return;
    }
    auto* live_layer = live_session->document.find_layer(layer_id);
    if (live_layer == nullptr) {
      return;
    }
    const auto dirty = to_qrect(bounds).united(to_qrect(final_result.bounds));
    if (rasterize_smart_object_for_recipe) {
      strip_layer_smart_object_data(live_session->document, *live_layer);
    }
    set_layer_pixels_with_bounds(*live_layer, std::move(final_result.pixels), final_result.bounds);
    if (rasterize_smart_object_for_recipe) {
      refresh_layer_list();
      refresh_layer_controls();
    }
    if (target_canvas != nullptr) {
      target_canvas->document_changed(dirty);
    }
    statusBar()->showMessage(
        rasterize_smart_object_for_recipe
            ? tr("Rasterized Smart Object and applied %1").arg(display_name)
            : tr("Applied %1").arg(display_name));
  } catch (const std::exception& error) {
    restore_original();
    show_critical_message(this, tr("Filter failed"), QString::fromUtf8(error.what()),
                          QStringLiteral("filterFailedMessageBox"));
  }
}

void MainWindow::populate_new_adjustment_layer_menu(QMenu* menu, const QString& object_name_prefix) {
  if (menu == nullptr) {
    return;
  }

  const auto add_adjustment = [this, menu, &object_name_prefix](const QString& label, const QString& object_key,
                                                               const QString& icon_label, auto callback) {
    auto* action = menu->addAction(simple_icon(icon_label), label);
    if (!object_name_prefix.isEmpty()) {
      action->setObjectName(object_name_prefix + object_key + QStringLiteral("Action"));
      register_document_action(action);
    }
    connect(action, &QAction::triggered, this, callback);
    return action;
  };
  add_adjustment(tr("&Levels..."), QStringLiteral("LevelsAdjustment"), QStringLiteral("LVL"),
                 [this] { new_levels_adjustment_layer(); });
  add_adjustment(tr("&Curves..."), QStringLiteral("CurvesAdjustment"), QStringLiteral("CRV"),
                 [this] { new_curves_adjustment_layer(); });
  add_adjustment(tr("&Hue/Saturation..."), QStringLiteral("HueSaturationAdjustment"), QStringLiteral("HSL"),
                 [this] { new_hue_saturation_adjustment_layer(); });
  add_adjustment(tr("Color &Balance..."), QStringLiteral("ColorBalanceAdjustment"), QStringLiteral("CB"),
                 [this] { new_color_balance_adjustment_layer(); });
}

void MainWindow::new_levels_adjustment_layer() {
  std::optional<LayerId> preview_id;
  const auto restore_active_layer = document().active_layer_id();
  const auto preview_changed = [this, &preview_id, restore_active_layer](bool enabled,
                                                                         const LevelsSettings& levels) {
    AdjustmentSettings settings;
    settings.kind = AdjustmentKind::Levels;
    settings.levels = sanitized_levels_adjustment(levels);
    update_adjustment_layer_preview(tr("Levels"), settings, enabled, preview_id, restore_active_layer);
  };

  const PixelBuffer* histogram_source = nullptr;
  if (restore_active_layer.has_value()) {
    const auto& read_only_document = std::as_const(document());
    if (const auto* layer = read_only_document.find_layer(*restore_active_layer); editable_rgb8_layer(layer)) {
      histogram_source = &layer->pixels();
    }
  }
  auto preview_edit_lock = lock_preview_dialog_edits();
  const auto settings = request_levels_settings(this, preview_changed, {}, histogram_source);
  remove_adjustment_layer_preview(preview_id, restore_active_layer);
  preview_edit_lock.release();
  if (!settings.has_value()) {
    statusBar()->showMessage(tr("Cancelled Levels"));
    return;
  }
  apply_levels_adjustment(*settings, true);
}

void MainWindow::levels_dialog() {
  auto& doc = document();
  const auto active = doc.active_layer_id();
  if (!active.has_value()) {
    return;
  }
  auto* layer = doc.find_layer(*active);
  if (!editable_rgb8_layer(layer)) {
    statusBar()->showMessage(tr("Select an editable RGB pixel layer"));
    return;
  }
  if (layer_is_smart_object(*layer)) {
    statusBar()->showMessage(tr(
        "Rasterize the Smart Object before applying destructive filters or adjustments"));
    return;
  }
  if (layer_id_locks_image_pixels(*active)) {
    statusBar()->showMessage(tr("Layer pixels are locked."));
    return;
  }
  const auto active_id = *active;
  const auto bounds = layer->bounds();
  auto original_pixels =
      std::make_shared<const PixelBuffer>(std::as_const(*layer).pixels());
  const auto selection = canvas_->selected_document_region();
  using LevelsPreviewRequest = AdjustmentPixelPreviewRequest<LevelsSettings>;
  auto preview_state = std::make_shared<AsyncPixelPreviewState<LevelsPreviewRequest>>();
  preview_state->start = [this, preview_state, active_id, bounds, original_pixels,
                          selection](const LevelsPreviewRequest& request) {
    if (!request.enabled || !levels_settings_have_effect(request.settings)) {
      preview_state->pending.reset();
      ++preview_state->generation;
      if (auto* preview_layer = document().find_layer(active_id); preview_layer != nullptr) {
        set_layer_pixels_preserving_origin(*preview_layer, *original_pixels, bounds);
        if (canvas_ != nullptr) {
          canvas_->document_changed(to_qrect(bounds));
        }
      }
      return;
    }

    preview_state->in_flight = true;
    const auto generation = ++preview_state->generation;
    auto* app = QCoreApplication::instance();
    auto window = QPointer<MainWindow>(this);
    std::thread([app, window, preview_state, generation, active_id, bounds, original_pixels, selection, request] {
      auto result = std::make_shared<PixelBuffer>(*original_pixels);
      try {
        apply_levels_to_pixels(*result, bounds, selection, request.settings, nullptr);
      } catch (const std::exception&) {
        result.reset();
      }
      if (app == nullptr) {
        return;
      }
      QMetaObject::invokeMethod(
          app,
          [window, preview_state, generation, active_id, bounds, result]() mutable {
            preview_state->in_flight = false;
            const auto has_pending = preview_state->pending.has_value();
            if (!preview_state->closed && !has_pending && generation == preview_state->generation &&
                window != nullptr && result != nullptr) {
              if (auto* preview_layer = window->document().find_layer(active_id); preview_layer != nullptr) {
                set_layer_pixels_preserving_origin(*preview_layer, std::move(*result), bounds);
                if (window->canvas_ != nullptr) {
                  window->canvas_->document_changed(to_qrect(bounds));
                }
              }
            }
            if (!preview_state->closed && preview_state->pending.has_value() && preview_state->start) {
              auto next = *preview_state->pending;
              preview_state->pending.reset();
              preview_state->start(next);
            }
          },
          Qt::QueuedConnection);
    }).detach();
  };
  const auto preview_changed = [preview_state](bool enabled, const LevelsSettings& settings) {
    enqueue_async_pixel_preview(preview_state, LevelsPreviewRequest{enabled, settings},
                                !enabled || !levels_settings_have_effect(settings));
  };

  auto preview_edit_lock = lock_preview_dialog_edits();
  const auto settings = request_levels_settings(this, preview_changed, {}, original_pixels.get());
  close_async_pixel_preview(preview_state);
  layer = doc.find_layer(active_id);
  if (layer == nullptr) {
    return;
  }
  set_layer_pixels_preserving_origin(*layer, *original_pixels, bounds);
  canvas_->document_changed(to_qrect(bounds));
  preview_edit_lock.release();
  if (!settings.has_value()) {
    statusBar()->showMessage(tr("Cancelled Levels"));
    return;
  }

  auto final_pixels = *original_pixels;
  if (levels_settings_have_effect(*settings)) {
    const auto display_name = tr("Levels");
    if (canvas_ != nullptr) {
      canvas_->begin_processing_operation();
    }
    const auto finish_processing = qScopeGuard([this] {
      if (canvas_ != nullptr) {
        canvas_->end_processing_operation();
      }
    });
    QProgressDialog progress(tr("Applying %1...").arg(display_name), tr("Cancel"), 0, 100, this);
    progress.setObjectName(QStringLiteral("adjustmentProgressDialog"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(kFilterProgressMinimumDurationMs);
    remember_dialog_position(progress);
    progress.setValue(0);
    auto filter_progress = progress_dialog_filter_progress(
        progress, [this, display_name](const QString& detail) { return tr("Applying %1...\n%2").arg(display_name, detail); },
        QEventLoop::AllEvents, [this] {
          if (canvas_ != nullptr) {
            canvas_->tick_processing_operation();
          }
        });
    try {
      apply_levels_to_pixels(final_pixels, bounds, selection, *settings, &filter_progress);
      progress.setValue(100);
    } catch (const FilterCancelled&) {
      layer = doc.find_layer(active_id);
      if (layer != nullptr) {
        set_layer_pixels_preserving_origin(*layer, *original_pixels, bounds);
        canvas_->document_changed(to_qrect(bounds));
      }
      statusBar()->showMessage(tr("Cancelled Levels"));
      return;
    }
  }
  if (pixel_buffers_equal(final_pixels, *original_pixels)) {
    statusBar()->showMessage(tr("%1 made no changes").arg(tr("Levels")));
    return;
  }
  push_undo_snapshot(tr("Levels"));
  layer = doc.find_layer(active_id);
  if (layer == nullptr) {
    return;
  }
  set_layer_pixels_preserving_origin(*layer, std::move(final_pixels), bounds);
  canvas_->document_changed(to_qrect(bounds));
  statusBar()->showMessage(tr("Applied %1").arg(tr("Levels")));
}

void MainWindow::apply_levels_adjustment(const LevelsSettings& levels, bool allow_identity) {
  AdjustmentSettings settings;
  settings.kind = AdjustmentKind::Levels;
  settings.levels = sanitized_levels_adjustment(levels);
  if (!allow_identity && !adjustment_has_effect(settings)) {
    return;
  }
  create_adjustment_layer(tr("Levels"), settings);
}

void MainWindow::new_curves_adjustment_layer() {
  std::optional<LayerId> preview_id;
  const auto restore_active_layer = document().active_layer_id();
  const auto preview_changed = [this, &preview_id, restore_active_layer](bool enabled,
                                                                         const CurvesSettings& curves) {
    AdjustmentSettings settings;
    settings.kind = AdjustmentKind::Curves;
    settings.curves = curves;
    update_adjustment_layer_preview(tr("Curves"), settings, enabled, preview_id, restore_active_layer);
  };

  const auto histograms = curves_histograms_from_composite(std::as_const(document()));
  const auto hooks = curves_canvas_hooks(canvas_, [this, &preview_id](QPoint point) {
    if (preview_id.has_value() && document().find_layer(*preview_id) != nullptr) {
      return curves_sample_before_layer(std::as_const(document()), *preview_id, point);
    }
    const auto image = qimage_from_document_rect(
        std::as_const(document()), QRect(point, QSize(1, 1)), true);
    return image.isNull() ? QColor{} : image.pixelColor(0, 0);
  });
  auto preview_edit_lock = lock_preview_dialog_edits();
  const auto settings = request_curves_settings(this, preview_changed, {}, histograms, hooks);
  remove_adjustment_layer_preview(preview_id, restore_active_layer);
  preview_edit_lock.release();
  if (!settings.has_value()) {
    statusBar()->showMessage(tr("Cancelled Curves"));
    return;
  }
  apply_curves_adjustment(*settings, true);
}

void MainWindow::curves_dialog() {
  auto& doc = document();
  const auto active = doc.active_layer_id();
  if (!active.has_value()) {
    return;
  }
  auto* layer = doc.find_layer(*active);
  if (!editable_rgb8_layer(layer)) {
    statusBar()->showMessage(tr("Select an editable RGB pixel layer"));
    return;
  }
  if (layer_is_smart_object(*layer)) {
    statusBar()->showMessage(tr(
        "Rasterize the Smart Object before applying destructive filters or adjustments"));
    return;
  }
  if (layer_id_locks_image_pixels(*active)) {
    statusBar()->showMessage(tr("Layer pixels are locked."));
    return;
  }
  const auto active_id = *active;
  const auto bounds = layer->bounds();
  auto original_layer = std::make_shared<const Layer>(*layer);
  auto original_pixels = std::make_shared<const PixelBuffer>(original_layer->pixels());
  const auto selection = canvas_->selected_document_region();
  using CurvesPreviewRequest = AdjustmentPixelPreviewRequest<CurvesSettings>;
  auto preview_state = std::make_shared<AsyncPixelPreviewState<CurvesPreviewRequest>>();
  preview_state->start = [this, preview_state, active_id, bounds, original_layer, original_pixels,
                          selection](const CurvesPreviewRequest& request) {
    if (!request.enabled || !curves_settings_have_effect(request.settings)) {
      preview_state->pending.reset();
      ++preview_state->generation;
      if (auto* preview_layer = document().find_layer(active_id); preview_layer != nullptr) {
        *preview_layer = *original_layer;
        if (canvas_ != nullptr) {
          canvas_->document_changed(to_qrect(bounds));
        }
      }
      return;
    }

    preview_state->in_flight = true;
    const auto generation = ++preview_state->generation;
    auto* app = QCoreApplication::instance();
    auto window = QPointer<MainWindow>(this);
    std::thread([app, window, preview_state, generation, active_id, bounds, original_pixels, selection, request] {
      auto result = std::make_shared<PixelBuffer>(*original_pixels);
      try {
        apply_curves_to_pixels(*result, bounds, selection, request.settings, nullptr);
      } catch (const std::exception&) {
        result.reset();
      }
      if (app == nullptr) {
        return;
      }
      QMetaObject::invokeMethod(
          app,
          [window, preview_state, generation, active_id, bounds, result]() mutable {
            preview_state->in_flight = false;
            const auto has_pending = preview_state->pending.has_value();
            if (!preview_state->closed && !has_pending && generation == preview_state->generation &&
                window != nullptr && result != nullptr) {
              if (auto* preview_layer = window->document().find_layer(active_id); preview_layer != nullptr) {
                set_layer_pixels_preserving_origin(*preview_layer, std::move(*result), bounds);
                if (window->canvas_ != nullptr) {
                  window->canvas_->document_changed(to_qrect(bounds));
                }
              }
            }
            if (!preview_state->closed && preview_state->pending.has_value() && preview_state->start) {
              auto next = *preview_state->pending;
              preview_state->pending.reset();
              preview_state->start(next);
            }
          },
          Qt::QueuedConnection);
    }).detach();
  };
  const auto preview_changed = [preview_state](bool enabled, const CurvesSettings& settings) {
    enqueue_async_pixel_preview(preview_state, CurvesPreviewRequest{enabled, settings},
                                !enabled || !curves_settings_have_effect(settings));
  };

  const auto histograms = curves_histograms_from_pixels(original_pixels.get());
  const auto hooks = curves_canvas_hooks(
      canvas_, [this, active_id, original_pixels, bounds](QPoint point) {
        const auto image = qimage_from_document_rect_with_layer_pixels(
            std::as_const(document()), QRect(point, QSize(1, 1)), true, active_id, *original_pixels, bounds);
        return image.isNull() ? QColor{} : image.pixelColor(0, 0);
      });
  auto preview_edit_lock = lock_preview_dialog_edits();
  const auto settings = request_curves_settings(this, preview_changed, {}, histograms, hooks);
  close_async_pixel_preview(preview_state);
  layer = doc.find_layer(active_id);
  if (layer == nullptr) {
    return;
  }
  *layer = *original_layer;
  canvas_->document_changed(to_qrect(bounds));
  preview_edit_lock.release();
  if (!settings.has_value()) {
    statusBar()->showMessage(tr("Cancelled Curves"));
    return;
  }

  auto final_pixels = *original_pixels;
  if (curves_settings_have_effect(*settings)) {
    const auto display_name = tr("Curves");
    if (canvas_ != nullptr) {
      canvas_->begin_processing_operation();
    }
    const auto finish_processing = qScopeGuard([this] {
      if (canvas_ != nullptr) {
        canvas_->end_processing_operation();
      }
    });
    QProgressDialog progress(tr("Applying %1...").arg(display_name), tr("Cancel"), 0, 100, this);
    progress.setObjectName(QStringLiteral("adjustmentProgressDialog"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(kFilterProgressMinimumDurationMs);
    remember_dialog_position(progress);
    progress.setValue(0);
    auto filter_progress = progress_dialog_filter_progress(
        progress, [this, display_name](const QString& detail) { return tr("Applying %1...\n%2").arg(display_name, detail); },
        QEventLoop::AllEvents, [this] {
          if (canvas_ != nullptr) {
            canvas_->tick_processing_operation();
          }
        });
    try {
      apply_curves_to_pixels(final_pixels, bounds, selection, *settings, &filter_progress);
      progress.setValue(100);
    } catch (const FilterCancelled&) {
      statusBar()->showMessage(tr("Cancelled Curves"));
      return;
    }
  }
  if (pixel_buffers_equal(final_pixels, *original_pixels)) {
    statusBar()->showMessage(tr("%1 made no changes").arg(tr("Curves")));
    return;
  }
  push_undo_snapshot(tr("Curves"));
  layer = doc.find_layer(active_id);
  if (layer == nullptr) {
    return;
  }
  set_layer_pixels_preserving_origin(*layer, std::move(final_pixels), bounds);
  canvas_->document_changed(to_qrect(bounds));
  statusBar()->showMessage(tr("Applied %1").arg(tr("Curves")));
}

void MainWindow::apply_curves_adjustment(const CurvesAdjustment& curves, bool allow_identity) {
  AdjustmentSettings settings;
  settings.kind = AdjustmentKind::Curves;
  settings.curves = curves;
  if (!allow_identity && !adjustment_has_effect(settings)) {
    return;
  }
  create_adjustment_layer(tr("Curves"), settings);
}

void MainWindow::new_hue_saturation_adjustment_layer() {
  std::optional<LayerId> preview_id;
  const auto restore_active_layer = document().active_layer_id();
  const auto preview_changed = [this, &preview_id, restore_active_layer](
                                   bool enabled, const HueSaturationSettings& hue_saturation) {
    AdjustmentSettings settings;
    settings.kind = AdjustmentKind::HueSaturation;
    settings.hue_saturation = to_hue_saturation_adjustment(hue_saturation);
    update_adjustment_layer_preview(tr("Hue/Saturation"), settings, enabled, preview_id, restore_active_layer);
  };

  auto preview_edit_lock = lock_preview_dialog_edits();
  const auto settings = request_hue_saturation_settings(this, preview_changed);
  remove_adjustment_layer_preview(preview_id, restore_active_layer);
  preview_edit_lock.release();
  if (!settings.has_value()) {
    statusBar()->showMessage(tr("Cancelled Hue/Saturation"));
    return;
  }
  apply_hue_saturation_adjustment(*settings, true);
}

void MainWindow::hue_saturation_dialog() {
  auto& doc = document();
  const auto active = doc.active_layer_id();
  if (!active.has_value()) {
    return;
  }
  auto* layer = doc.find_layer(*active);
  if (!editable_rgb8_layer(layer)) {
    statusBar()->showMessage(tr("Select an editable RGB pixel layer"));
    return;
  }
  if (layer_is_smart_object(*layer)) {
    statusBar()->showMessage(tr(
        "Rasterize the Smart Object before applying destructive filters or adjustments"));
    return;
  }
  const auto active_id = *active;
  const auto bounds = layer->bounds();
  auto original_pixels =
      std::make_shared<const PixelBuffer>(std::as_const(*layer).pixels());
  const auto selection = canvas_->selected_document_region();
  using HueSaturationPreviewRequest = AdjustmentPixelPreviewRequest<HueSaturationSettings>;
  const auto hue_saturation_has_effect = [](const HueSaturationSettings& settings) {
    return settings.colorize || settings.hue_shift != 0 || settings.saturation_delta != 0 ||
           settings.lightness_delta != 0;
  };
  auto preview_state = std::make_shared<AsyncPixelPreviewState<HueSaturationPreviewRequest>>();
  preview_state->start = [this, preview_state, active_id, bounds, original_pixels, selection,
                          hue_saturation_has_effect](const HueSaturationPreviewRequest& request) {
    if (!request.enabled || !hue_saturation_has_effect(request.settings)) {
      preview_state->pending.reset();
      ++preview_state->generation;
      if (auto* preview_layer = document().find_layer(active_id); preview_layer != nullptr) {
        set_layer_pixels_preserving_origin(*preview_layer, *original_pixels, bounds);
        if (canvas_ != nullptr) {
          canvas_->document_changed(to_qrect(bounds));
        }
      }
      return;
    }

    preview_state->in_flight = true;
    const auto generation = ++preview_state->generation;
    auto* app = QCoreApplication::instance();
    auto window = QPointer<MainWindow>(this);
    std::thread([app, window, preview_state, generation, active_id, bounds, original_pixels, selection, request] {
      auto result = std::make_shared<PixelBuffer>(*original_pixels);
      try {
        apply_hue_saturation_to_pixels(*result, bounds, selection, request.settings, nullptr);
      } catch (const std::exception&) {
        result.reset();
      }
      if (app == nullptr) {
        return;
      }
      QMetaObject::invokeMethod(
          app,
          [window, preview_state, generation, active_id, bounds, result]() mutable {
            preview_state->in_flight = false;
            const auto has_pending = preview_state->pending.has_value();
            if (!preview_state->closed && !has_pending && generation == preview_state->generation &&
                window != nullptr && result != nullptr) {
              if (auto* preview_layer = window->document().find_layer(active_id); preview_layer != nullptr) {
                set_layer_pixels_preserving_origin(*preview_layer, std::move(*result), bounds);
                if (window->canvas_ != nullptr) {
                  window->canvas_->document_changed(to_qrect(bounds));
                }
              }
            }
            if (!preview_state->closed && preview_state->pending.has_value() && preview_state->start) {
              auto next = *preview_state->pending;
              preview_state->pending.reset();
              preview_state->start(next);
            }
          },
          Qt::QueuedConnection);
    }).detach();
  };
  const auto preview_changed = [preview_state, hue_saturation_has_effect](bool enabled,
                                                                         const HueSaturationSettings& settings) {
    enqueue_async_pixel_preview(preview_state, HueSaturationPreviewRequest{enabled, settings},
                                !enabled || !hue_saturation_has_effect(settings));
  };

  auto preview_edit_lock = lock_preview_dialog_edits();
  const auto settings = request_hue_saturation_settings(this, preview_changed);
  close_async_pixel_preview(preview_state);
  layer = doc.find_layer(active_id);
  if (layer == nullptr) {
    return;
  }
  set_layer_pixels_preserving_origin(*layer, *original_pixels, bounds);
  canvas_->document_changed(to_qrect(bounds));
  preview_edit_lock.release();
  if (!settings.has_value()) {
    statusBar()->showMessage(tr("Cancelled Hue/Saturation"));
    return;
  }
  apply_hue_saturation_adjustment(*settings);
}

void MainWindow::apply_hue_saturation_adjustment(const HueSaturationSettings& hue_saturation, bool allow_identity) {
  AdjustmentSettings settings;
  settings.kind = AdjustmentKind::HueSaturation;
  settings.hue_saturation = to_hue_saturation_adjustment(hue_saturation);
  if (!allow_identity && !adjustment_has_effect(settings)) {
    return;
  }
  create_adjustment_layer(tr("Hue/Saturation"), settings);
}

void MainWindow::new_color_balance_adjustment_layer() {
  std::optional<LayerId> preview_id;
  const auto restore_active_layer = document().active_layer_id();
  const auto preview_changed = [this, &preview_id, restore_active_layer](bool enabled,
                                                                         const ColorBalanceSettings& color_balance) {
    AdjustmentSettings settings;
    settings.kind = AdjustmentKind::ColorBalance;
    settings.color_balance = ColorBalanceAdjustment{std::clamp(color_balance.cyan_red, -100, 100),
                                                    std::clamp(color_balance.magenta_green, -100, 100),
                                                    std::clamp(color_balance.yellow_blue, -100, 100)};
    update_adjustment_layer_preview(tr("Color Balance"), settings, enabled, preview_id, restore_active_layer);
  };

  auto preview_edit_lock = lock_preview_dialog_edits();
  const auto settings = request_color_balance_settings(this, preview_changed);
  remove_adjustment_layer_preview(preview_id, restore_active_layer);
  preview_edit_lock.release();
  if (!settings.has_value()) {
    statusBar()->showMessage(tr("Cancelled Color Balance"));
    return;
  }
  apply_color_balance_adjustment(settings->cyan_red, settings->magenta_green, settings->yellow_blue, true);
}

void MainWindow::color_balance_dialog() {
  auto& doc = document();
  const auto active = doc.active_layer_id();
  if (!active.has_value()) {
    return;
  }
  auto* layer = doc.find_layer(*active);
  if (!editable_rgb8_layer(layer)) {
    statusBar()->showMessage(tr("Select an editable RGB pixel layer"));
    return;
  }
  if (layer_is_smart_object(*layer)) {
    statusBar()->showMessage(tr(
        "Rasterize the Smart Object before applying destructive filters or adjustments"));
    return;
  }
  const auto active_id = *active;
  const auto bounds = layer->bounds();
  auto original_pixels =
      std::make_shared<const PixelBuffer>(std::as_const(*layer).pixels());
  const auto selection = canvas_->selected_document_region();
  using ColorBalancePreviewRequest = AdjustmentPixelPreviewRequest<ColorBalanceSettings>;
  const auto color_balance_has_effect = [](const ColorBalanceSettings& settings) {
    return !(settings.cyan_red == 0 && settings.magenta_green == 0 && settings.yellow_blue == 0);
  };
  auto preview_state = std::make_shared<AsyncPixelPreviewState<ColorBalancePreviewRequest>>();
  preview_state->start = [this, preview_state, active_id, bounds, original_pixels, selection,
                          color_balance_has_effect](const ColorBalancePreviewRequest& request) {
    if (!request.enabled || !color_balance_has_effect(request.settings)) {
      preview_state->pending.reset();
      ++preview_state->generation;
      if (auto* preview_layer = document().find_layer(active_id); preview_layer != nullptr) {
        set_layer_pixels_preserving_origin(*preview_layer, *original_pixels, bounds);
        if (canvas_ != nullptr) {
          canvas_->document_changed(to_qrect(bounds));
        }
      }
      return;
    }

    preview_state->in_flight = true;
    const auto generation = ++preview_state->generation;
    auto* app = QCoreApplication::instance();
    auto window = QPointer<MainWindow>(this);
    std::thread([app, window, preview_state, generation, active_id, bounds, original_pixels, selection, request] {
      auto result = std::make_shared<PixelBuffer>(*original_pixels);
      try {
        apply_color_balance_to_pixels(*result, bounds, selection, request.settings, nullptr);
      } catch (const std::exception&) {
        result.reset();
      }
      if (app == nullptr) {
        return;
      }
      QMetaObject::invokeMethod(
          app,
          [window, preview_state, generation, active_id, bounds, result]() mutable {
            preview_state->in_flight = false;
            const auto has_pending = preview_state->pending.has_value();
            if (!preview_state->closed && !has_pending && generation == preview_state->generation &&
                window != nullptr && result != nullptr) {
              if (auto* preview_layer = window->document().find_layer(active_id); preview_layer != nullptr) {
                set_layer_pixels_preserving_origin(*preview_layer, std::move(*result), bounds);
                if (window->canvas_ != nullptr) {
                  window->canvas_->document_changed(to_qrect(bounds));
                }
              }
            }
            if (!preview_state->closed && preview_state->pending.has_value() && preview_state->start) {
              auto next = *preview_state->pending;
              preview_state->pending.reset();
              preview_state->start(next);
            }
          },
          Qt::QueuedConnection);
    }).detach();
  };
  const auto preview_changed = [preview_state, color_balance_has_effect](bool enabled,
                                                                         const ColorBalanceSettings& settings) {
    enqueue_async_pixel_preview(preview_state, ColorBalancePreviewRequest{enabled, settings},
                                !enabled || !color_balance_has_effect(settings));
  };

  auto preview_edit_lock = lock_preview_dialog_edits();
  const auto settings = request_color_balance_settings(this, preview_changed);
  close_async_pixel_preview(preview_state);
  layer = doc.find_layer(active_id);
  if (layer == nullptr) {
    return;
  }
  set_layer_pixels_preserving_origin(*layer, *original_pixels, bounds);
  canvas_->document_changed(to_qrect(bounds));
  preview_edit_lock.release();
  if (!settings.has_value()) {
    statusBar()->showMessage(tr("Cancelled Color Balance"));
    return;
  }
  apply_color_balance_adjustment(settings->cyan_red, settings->magenta_green, settings->yellow_blue);
}

void MainWindow::apply_color_balance_adjustment(int cyan_red, int magenta_green, int yellow_blue, bool allow_identity) {
  AdjustmentSettings settings;
  settings.kind = AdjustmentKind::ColorBalance;
  settings.color_balance = ColorBalanceAdjustment{std::clamp(cyan_red, -100, 100),
                                                  std::clamp(magenta_green, -100, 100),
                                                  std::clamp(yellow_blue, -100, 100)};
  if (!allow_identity && !adjustment_has_effect(settings)) {
    return;
  }
  create_adjustment_layer(tr("Color Balance"), settings);
}

Layer MainWindow::build_adjustment_layer(QString label, const AdjustmentSettings& settings) {
  auto& doc = document();
  Layer layer(doc.allocate_layer_id(), label.toStdString(), LayerKind::Adjustment);
  layer.set_bounds(Rect::from_size(doc.width(), doc.height()));
  configure_adjustment_layer(layer, settings);

  const auto selection = canvas_->selected_document_region();
  const auto selection_rect = selection.boundingRect().intersected(QRect(0, 0, doc.width(), doc.height()));
  if (!selection.isEmpty() && !selection_rect.isEmpty()) {
    layer.set_mask(LayerMask{to_core_rect(selection_rect), selection_mask_pixels(*canvas_, selection_rect), 0, false});
  }
  return layer;
}

void MainWindow::update_adjustment_layer_preview(QString label, const AdjustmentSettings& settings, bool enabled,
                                                 std::optional<LayerId>& preview_id,
                                                 std::optional<LayerId> restore_active_layer) {
  if (canvas_ == nullptr || !enabled || !adjustment_has_effect(settings)) {
    remove_adjustment_layer_preview(preview_id, restore_active_layer);
    return;
  }

  auto& doc = document();
  if (preview_id.has_value()) {
    if (auto* layer = doc.find_layer(*preview_id); layer != nullptr) {
      layer->set_name(label.toStdString());
      layer->set_bounds(Rect::from_size(doc.width(), doc.height()));
      configure_adjustment_layer(*layer, settings);
      canvas_->document_changed();
      return;
    }
    preview_id.reset();
  }

  auto preview = build_adjustment_layer(label, settings);
  preview_id = preview.id();
  doc.add_layer(std::move(preview));
  if (restore_active_layer.has_value() && doc.find_layer(*restore_active_layer) != nullptr) {
    doc.set_active_layer(*restore_active_layer);
  }
  canvas_->document_changed();
}

void MainWindow::remove_adjustment_layer_preview(std::optional<LayerId>& preview_id,
                                                 std::optional<LayerId> restore_active_layer) {
  if (!preview_id.has_value()) {
    return;
  }

  auto& doc = document();
  const auto removed = doc.remove_layer(*preview_id);
  preview_id.reset();
  if (restore_active_layer.has_value() && doc.find_layer(*restore_active_layer) != nullptr) {
    doc.set_active_layer(*restore_active_layer);
  }
  if (removed && canvas_ != nullptr) {
    canvas_->document_changed();
  }
}

void MainWindow::create_adjustment_layer(QString label, const AdjustmentSettings& settings) {
  if (canvas_ == nullptr) {
    return;
  }

  auto& doc = document();
  push_undo_snapshot(tr("%1 adjustment layer").arg(label));
  auto layer = build_adjustment_layer(label, settings);

  doc.add_layer(std::move(layer));
  refresh_layer_list();
  refresh_layer_controls();
  refresh_document_info();
  canvas_->document_changed();
  statusBar()->showMessage(tr("Added %1 adjustment layer").arg(label));
}

void MainWindow::edit_active_adjustment_layer() {
  // Adjustment dialogs are preview dialogs; opening one on top of another
  // preview dialog (e.g. by double-clicking a layer row while one is open)
  // stacks nested event loops and crashes.
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
  auto& doc = document();
  const auto active = doc.active_layer_id();
  auto* layer = active.has_value() ? doc.find_layer(*active) : nullptr;
  if (layer == nullptr || layer->kind() != LayerKind::Adjustment) {
    statusBar()->showMessage(tr("Select an adjustment layer to edit its settings"));
    return;
  }
  if (layer_id_locks_image_pixels(*active)) {
    statusBar()->showMessage(tr("Layer pixels are locked."));
    return;
  }

  const auto original_settings = adjustment_settings_from_layer(*layer);
  if (!original_settings.has_value()) {
    statusBar()->showMessage(tr("This adjustment layer has no editable settings"));
    return;
  }

  const auto layer_id = layer->id();
  const auto original_layer = *layer;
  auto apply_settings = [this, &doc, layer_id](const AdjustmentSettings& settings) {
    auto* target = doc.find_layer(layer_id);
    if (target == nullptr) {
      return;
    }
    configure_adjustment_layer(*target, settings);
    if (canvas_ != nullptr) {
      canvas_->document_changed();
      refresh_layer_thumbnails();
    }
  };
  auto restore_original_layer = [this, &doc, layer_id, &original_layer] {
    auto* target = doc.find_layer(layer_id);
    if (target == nullptr) {
      return;
    }
    *target = original_layer;
    if (canvas_ != nullptr) {
      canvas_->document_changed();
      refresh_layer_thumbnails();
    }
  };

  std::optional<AdjustmentSettings> accepted_settings;
  auto preview_edit_lock = lock_preview_dialog_edits();
  switch (original_settings->kind) {
    case AdjustmentKind::Levels: {
      const auto preview_changed = [apply_settings, restore_original_layer,
                                    original_settings](bool enabled, const LevelsSettings& levels) {
        if (!enabled) {
          restore_original_layer();
          return;
        }
        auto settings = *original_settings;
        settings.levels = sanitized_levels_adjustment(levels);
        apply_settings(settings);
      };
      const auto result = request_levels_settings(this, preview_changed,
                                                  LevelsSettings{original_settings->levels.black_input,
                                                                 original_settings->levels.white_input,
                                                                 original_settings->levels.gamma_percent,
                                                                 original_settings->levels.black_output,
                                                                 original_settings->levels.white_output,
                                                                 original_settings->levels.channel,
                                                                 original_settings->levels.red,
                                                                 original_settings->levels.green,
                                                                 original_settings->levels.blue});
      if (result.has_value()) {
        accepted_settings = *original_settings;
        accepted_settings->levels = sanitized_levels_adjustment(*result);
      }
      break;
    }
    case AdjustmentKind::Curves: {
      const auto preview_changed = [apply_settings, restore_original_layer,
                                    original_settings](bool enabled, const CurvesSettings& curves) {
        if (!enabled) {
          restore_original_layer();
          return;
        }
        auto settings = *original_settings;
        settings.curves = curves;
        apply_settings(settings);
      };
      // A clipped adjustment runs inside an isolated clipping buffer. Until the
      // histogram renderer can expose that buffer directly, leave Auto disabled
      // instead of presenting a backdrop-mixed histogram as the adjustment input.
      const auto histograms = original_layer.clipped()
                                  ? CurvesHistograms{}
                                  : curves_histograms_before_adjustment(std::as_const(doc), layer_id);
      auto hooks = curves_canvas_hooks(canvas_, [this, layer_id](QPoint point) {
        return curves_sample_before_layer(std::as_const(document()), layer_id, point);
      });
      if (original_layer.clipped()) {
        hooks.set_canvas_mode = {};
      }
      const auto result =
          request_curves_settings(this, preview_changed, original_settings->curves, histograms, hooks);
      if (result.has_value()) {
        accepted_settings = *original_settings;
        accepted_settings->curves = *result;
      }
      break;
    }
    case AdjustmentKind::HueSaturation: {
      const auto preview_changed = [apply_settings, restore_original_layer, original_settings](
                                       bool enabled, const HueSaturationSettings& hue_saturation) {
        if (!enabled) {
          restore_original_layer();
          return;
        }
        auto settings = *original_settings;
        settings.hue_saturation = to_hue_saturation_adjustment(hue_saturation);
        apply_settings(settings);
      };
      const auto result = request_hue_saturation_settings(
          this, preview_changed, to_hue_saturation_settings(original_settings->hue_saturation));
      if (result.has_value()) {
        accepted_settings = *original_settings;
        accepted_settings->hue_saturation = to_hue_saturation_adjustment(*result);
      }
      break;
    }
    case AdjustmentKind::ColorBalance: {
      const auto preview_changed = [apply_settings, restore_original_layer, original_settings](
                                       bool enabled, const ColorBalanceSettings& color_balance) {
        if (!enabled) {
          restore_original_layer();
          return;
        }
        auto settings = *original_settings;
        settings.color_balance =
            ColorBalanceAdjustment{std::clamp(color_balance.cyan_red, -100, 100),
                                   std::clamp(color_balance.magenta_green, -100, 100),
                                   std::clamp(color_balance.yellow_blue, -100, 100)};
        apply_settings(settings);
      };
      const auto result = request_color_balance_settings(
          this, preview_changed,
          ColorBalanceSettings{original_settings->color_balance.cyan_red,
                               original_settings->color_balance.magenta_green,
                               original_settings->color_balance.yellow_blue});
      if (result.has_value()) {
        accepted_settings = *original_settings;
        accepted_settings->color_balance =
            ColorBalanceAdjustment{std::clamp(result->cyan_red, -100, 100),
                                   std::clamp(result->magenta_green, -100, 100),
                                   std::clamp(result->yellow_blue, -100, 100)};
      }
      break;
    }
  }

  restore_original_layer();
  preview_edit_lock.release();
  if (!accepted_settings.has_value()) {
    refresh_layer_list();
    refresh_layer_controls();
    statusBar()->showMessage(tr("Cancelled adjustment edit"));
    return;
  }

  if (original_settings->kind == AdjustmentKind::Curves &&
      accepted_settings->curves == original_settings->curves) {
    refresh_layer_list();
    refresh_layer_controls();
    statusBar()->showMessage(tr("Updated adjustment layer"));
    return;
  }

  push_undo_snapshot(tr("Edit %1 adjustment").arg(localized_adjustment_display_name(original_settings->kind)));
  apply_settings(*accepted_settings);
  refresh_layer_list();
  refresh_layer_controls();
  refresh_document_info();
  statusBar()->showMessage(tr("Updated adjustment layer"));
}


}  // namespace patchy::ui
