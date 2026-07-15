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
#include "ui/liquify_dialog.hpp"
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
  const auto master = levels_master_record(settings);
  return LevelsAdjustment{master.black_input, master.white_input, master.gamma_percent, master.black_output,
                          master.white_output, settings.channel, clamp_levels_record(settings.red),
                          clamp_levels_record(settings.green), clamp_levels_record(settings.blue)};
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

double smart_filter_radius_from_invocation(const FilterInvocation& invocation,
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

std::int32_t smart_filter_integer_from_invocation(
    const FilterInvocation& invocation, std::string_view key,
    std::int32_t fallback, std::int32_t minimum, std::int32_t maximum) {
  const auto found = invocation.parameters.find(std::string(key));
  if (found == invocation.parameters.end()) {
    return fallback;
  }
  const auto* value = std::get_if<std::int64_t>(&found->second);
  return value == nullptr
             ? fallback
             : static_cast<std::int32_t>(std::clamp(
                   *value, static_cast<std::int64_t>(minimum),
                   static_cast<std::int64_t>(maximum)));
}

FilterInvocation editable_smart_filter_invocation(SmartFilterKind kind) {
  FilterInvocation invocation;
  invocation.schema_version = 1U;
  if (kind == SmartFilterKind::SurfaceBlur) {
    invocation.filter_id = "patchy.smart_filters.surface_blur";
    invocation.parameters["radius"] = 5.0;
    invocation.parameters["threshold"] = std::int64_t{15};
  } else if (kind == SmartFilterKind::DustAndScratches) {
    invocation.filter_id = "patchy.smart_filters.dust_and_scratches";
    invocation.parameters["radius"] = std::int64_t{1};
    invocation.parameters["threshold"] = std::int64_t{0};
  } else if (kind == SmartFilterKind::Median) {
    invocation.filter_id = "patchy.smart_filters.median";
    invocation.parameters["radius"] = 1.0;
  } else if (kind == SmartFilterKind::HighPass) {
    invocation.filter_id = "patchy.smart_filters.high_pass";
    invocation.parameters["radius"] = 10.0;
  } else if (kind == SmartFilterKind::UnsharpMask) {
    invocation.filter_id = "patchy.smart_filters.unsharp_mask";
    invocation.parameters["amount"] = std::int64_t{150};
    invocation.parameters["radius"] = 2.0;
    invocation.parameters["threshold"] = std::int64_t{8};
  } else if (kind == SmartFilterKind::MotionBlur) {
    invocation.filter_id = "patchy.smart_filters.motion_blur";
    invocation.parameters["angle"] = std::int64_t{0};
    invocation.parameters["distance"] = std::int64_t{12};
  } else if (kind == SmartFilterKind::PlasticWrap) {
    invocation.filter_id = "patchy.smart_filters.plastic_wrap";
    invocation.parameters["highlight_strength"] = std::int64_t{9};
    invocation.parameters["detail"] = std::int64_t{7};
    invocation.parameters["smoothness"] = std::int64_t{5};
  } else {
    invocation.filter_id = "patchy.smart_filters.gaussian_blur";
    invocation.parameters["radius"] = 2.0;
  }
  return invocation;
}

FilterDialogSpec editable_smart_filter_dialog_spec(
    SmartFilterKind kind, const FilterInvocation& initial_invocation) {
  const auto high_pass = kind == SmartFilterKind::HighPass;
  const auto median = kind == SmartFilterKind::Median;
  const auto dust = kind == SmartFilterKind::DustAndScratches;
  const auto surface = kind == SmartFilterKind::SurfaceBlur;
  const auto unsharp = kind == SmartFilterKind::UnsharpMask;
  const auto motion = kind == SmartFilterKind::MotionBlur;
  const auto plastic = kind == SmartFilterKind::PlasticWrap;
  FilterDialogSpec spec;
  spec.identifier =
      QString::fromStdString(editable_smart_filter_invocation(kind).filter_id);
  spec.display_name =
      plastic  ? QObject::tr("Plastic Wrap")
      : unsharp  ? QObject::tr("Unsharp Mask")
      : motion ? QObject::tr("Motion Blur")
      : surface
          ? QObject::tr("Surface Blur")
          : (dust ? QObject::tr("Dust & Scratches")
                  : (median ? QObject::tr("Median")
                            : (high_pass ? QObject::tr("High Pass")
                                         : QObject::tr("Gaussian Blur"))));
  if (plastic) {
    FilterControlSpec highlight;
    highlight.label = QObject::tr("Highlight Strength");
    highlight.object_name = QStringLiteral("filterHighlightStrength");
    highlight.minimum = 0;
    highlight.maximum = 20;
    highlight.value = 9;
    highlight.parameter_key = "highlight_strength";
    highlight.kind = FilterParameterKind::Integer;
    highlight.default_value = std::int64_t{9};
    highlight.typed_minimum = 0.0;
    highlight.typed_maximum = 20.0;
    highlight.step = 1.0;
    spec.controls.push_back(std::move(highlight));

    FilterControlSpec detail_control;
    detail_control.label = QObject::tr("Detail");
    detail_control.object_name = QStringLiteral("filterDetail");
    detail_control.minimum = 1;
    detail_control.maximum = 15;
    detail_control.value = 7;
    detail_control.parameter_key = "detail";
    detail_control.kind = FilterParameterKind::Integer;
    detail_control.default_value = std::int64_t{7};
    detail_control.typed_minimum = 1.0;
    detail_control.typed_maximum = 15.0;
    detail_control.step = 1.0;
    spec.controls.push_back(std::move(detail_control));

    FilterControlSpec smoothness_control;
    smoothness_control.label = QObject::tr("Smoothness");
    smoothness_control.object_name = QStringLiteral("filterSmoothness");
    smoothness_control.minimum = 1;
    smoothness_control.maximum = 15;
    smoothness_control.value = 5;
    smoothness_control.parameter_key = "smoothness";
    smoothness_control.kind = FilterParameterKind::Integer;
    smoothness_control.default_value = std::int64_t{5};
    smoothness_control.typed_minimum = 1.0;
    smoothness_control.typed_maximum = 15.0;
    smoothness_control.step = 1.0;
    spec.controls.push_back(std::move(smoothness_control));
    return spec;
  }
  if (unsharp) {
    FilterControlSpec amount;
    amount.label = QObject::tr("Amount");
    amount.object_name = QStringLiteral("filterAmount");
    amount.minimum = 1;
    amount.maximum = 500;
    amount.value = 150;
    amount.suffix = QObject::tr("%");
    amount.parameter_key = "amount";
    amount.kind = FilterParameterKind::Integer;
    amount.default_value = std::int64_t{150};
    amount.typed_minimum = 1.0;
    amount.typed_maximum = 500.0;
    amount.step = 1.0;
    spec.controls.push_back(std::move(amount));
  }
  if (motion) {
    FilterControlSpec angle;
    angle.label = QObject::tr("Angle");
    angle.object_name = QStringLiteral("filterAngle");
    angle.minimum = -180;
    angle.maximum = 180;
    angle.value = 0;
    angle.suffix = QObject::tr(" degrees");
    angle.parameter_key = "angle";
    angle.kind = FilterParameterKind::Integer;
    angle.default_value = std::int64_t{0};
    angle.typed_minimum = -360.0;
    angle.typed_maximum = 360.0;
    angle.step = 1.0;
    spec.controls.push_back(std::move(angle));

    FilterControlSpec distance;
    distance.label = QObject::tr("Distance");
    distance.object_name = QStringLiteral("filterDistance");
    distance.minimum = 1;
    distance.maximum = 64;
    distance.value = 12;
    distance.suffix = QObject::tr(" px");
    distance.parameter_key = "distance";
    distance.kind = FilterParameterKind::Integer;
    distance.default_value = std::int64_t{12};
    distance.typed_minimum = 1.0;
    distance.typed_maximum = 999.0;
    distance.step = 1.0;
    spec.controls.push_back(std::move(distance));
    return spec;
  }

  FilterControlSpec radius;
  radius.label = QObject::tr("Radius");
  radius.object_name = QStringLiteral("filterRadius");
  radius.minimum = dust || median || surface ? 1 : 0;
  radius.maximum = surface || dust || median ? 25 : 12;
  radius.value = surface ? 5 : (dust || median ? 1 : (high_pass ? 10 : 2));
  radius.suffix = QObject::tr(" px");
  radius.parameter_key = "radius";
  radius.kind = dust ? FilterParameterKind::Integer
                     : FilterParameterKind::Double;
  radius.default_value = dust
                             ? FilterParameterValue{std::int64_t{1}}
                             : FilterParameterValue{
                                   surface ? 5.0
                                           : (median ? 1.0 : (high_pass ? 10.0 : 2.0))};
  radius.typed_minimum = dust || median || surface ? 1.0 : 0.1;
  radius.typed_maximum =
      surface
          ? 100.0
          : (dust ? 100.0
                  : (median
                         ? 500.0
                         : (high_pass || unsharp
                                ? 1000.0
                                : std::max(12.0,
                                           smart_filter_radius_from_invocation(
                                               initial_invocation, 2.0)))));
  // Native descriptors retain fractional radius values. Gaussian Blur changes
  // at hundredths; Median currently floors for rendering but must not round a
  // value merely because the dialog was opened and accepted.
  radius.step = dust ? 1.0 : 0.01;
  spec.controls.push_back(std::move(radius));
  if (dust || surface || unsharp) {
    FilterControlSpec threshold;
    threshold.label = QObject::tr("Threshold");
    threshold.object_name = QStringLiteral("filterThreshold");
    threshold.minimum = surface ? 2 : 0;
    threshold.maximum = 255;
    threshold.value = surface ? 15 : (unsharp ? 8 : 0);
    threshold.parameter_key = "threshold";
    threshold.kind = FilterParameterKind::Integer;
    threshold.default_value =
        surface ? FilterParameterValue{std::int64_t{15}}
                : (unsharp ? FilterParameterValue{std::int64_t{8}}
                           : FilterParameterValue{std::int64_t{0}});
    threshold.typed_minimum = surface ? 2.0 : 0.0;
    threshold.typed_maximum = 255.0;
    threshold.step = 1.0;
    spec.controls.push_back(std::move(threshold));
  }
  return spec;
}

SmartFilterStack smart_filter_stack_with_invocation(
    SmartFilterStack stack, std::size_t index,
    const FilterInvocation& invocation) {
  auto& entry = stack.entries.at(index);
  const auto radius = smart_filter_radius_from_invocation(invocation, 1.0);
  if (entry.kind == SmartFilterKind::GaussianBlur) {
    auto* gaussian =
        std::get_if<GaussianBlurSmartFilter>(&entry.parameters);
    if (gaussian == nullptr) {
      throw std::invalid_argument("Smart Filter radius is unavailable");
    }
    gaussian->radius_pixels = std::clamp(radius, 0.1, 1000.0);
  } else if (entry.kind == SmartFilterKind::HighPass) {
    auto* high_pass = std::get_if<HighPassSmartFilter>(&entry.parameters);
    if (high_pass == nullptr) {
      throw std::invalid_argument("Smart Filter radius is unavailable");
    }
    high_pass->radius_pixels = std::clamp(radius, 0.1, 1000.0);
  } else if (entry.kind == SmartFilterKind::Median) {
    auto* median = std::get_if<MedianSmartFilter>(&entry.parameters);
    if (median == nullptr) {
      throw std::invalid_argument("Smart Filter radius is unavailable");
    }
    median->radius_pixels = std::clamp(radius, 1.0, 500.0);
  } else if (entry.kind == SmartFilterKind::DustAndScratches) {
    auto* dust =
        std::get_if<DustAndScratchesSmartFilter>(&entry.parameters);
    if (dust == nullptr) {
      throw std::invalid_argument("Smart Filter settings are unavailable");
    }
    dust->radius_pixels = smart_filter_integer_from_invocation(
        invocation, "radius", dust->radius_pixels, 1, 100);
    dust->threshold = smart_filter_integer_from_invocation(
        invocation, "threshold", dust->threshold, 0, 255);
  } else if (entry.kind == SmartFilterKind::SurfaceBlur) {
    auto* surface = std::get_if<SurfaceBlurSmartFilter>(&entry.parameters);
    if (surface == nullptr) {
      throw std::invalid_argument("Smart Filter settings are unavailable");
    }
    surface->radius_pixels = std::clamp(radius, 1.0, 100.0);
    surface->threshold = smart_filter_integer_from_invocation(
        invocation, "threshold", surface->threshold, 2, 255);
  } else if (entry.kind == SmartFilterKind::UnsharpMask) {
    auto *unsharp = std::get_if<UnsharpMaskSmartFilter>(&entry.parameters);
    if (unsharp == nullptr) {
      throw std::invalid_argument("Smart Filter settings are unavailable");
    }
    unsharp->amount_percent = smart_filter_integer_from_invocation(
        invocation, "amount",
        static_cast<std::int32_t>(unsharp->amount_percent), 1, 500);
    unsharp->radius_pixels = std::clamp(radius, 0.1, 1000.0);
    unsharp->threshold = smart_filter_integer_from_invocation(
        invocation, "threshold", unsharp->threshold, 0, 255);
  } else if (entry.kind == SmartFilterKind::MotionBlur) {
    auto *motion = std::get_if<MotionBlurSmartFilter>(&entry.parameters);
    if (motion == nullptr) {
      throw std::invalid_argument("Smart Filter settings are unavailable");
    }
    motion->angle_degrees = smart_filter_integer_from_invocation(
        invocation, "angle", motion->angle_degrees, -360, 360);
    motion->distance_pixels = smart_filter_integer_from_invocation(
        invocation, "distance", motion->distance_pixels, 1, 999);
  } else if (entry.kind == SmartFilterKind::PlasticWrap) {
    auto *plastic = std::get_if<PlasticWrapSmartFilter>(&entry.parameters);
    if (plastic == nullptr) {
      throw std::invalid_argument("Smart Filter settings are unavailable");
    }
    plastic->highlight_strength = smart_filter_integer_from_invocation(
        invocation, "highlight_strength", plastic->highlight_strength, 0,
        20);
    plastic->detail = smart_filter_integer_from_invocation(
        invocation, "detail", plastic->detail, 1, 15);
    plastic->smoothness = smart_filter_integer_from_invocation(
        invocation, "smoothness", plastic->smoothness, 1, 15);
  } else {
    throw std::invalid_argument("Smart Filter radius is unavailable");
  }
  return stack;
}

SmartFilterEntry make_editable_smart_filter_entry(
    SmartFilterKind kind, const FilterInvocation& invocation,
    RgbColor foreground,
    RgbColor background) {
  const auto high_pass = kind == SmartFilterKind::HighPass;
  const auto median = kind == SmartFilterKind::Median;
  const auto dust = kind == SmartFilterKind::DustAndScratches;
  const auto surface = kind == SmartFilterKind::SurfaceBlur;
  const auto unsharp = kind == SmartFilterKind::UnsharpMask;
  const auto motion = kind == SmartFilterKind::MotionBlur;
  const auto plastic = kind == SmartFilterKind::PlasticWrap;
  const auto radius = smart_filter_radius_from_invocation(invocation, 1.0);
  SmartFilterEntry entry;
  entry.kind = kind;
  entry.native_name =
      plastic  ? "Plastic Wrap..."
      : unsharp  ? "Unsharp Mask..."
      : motion ? "Motion Blur..."
      : surface
          ? "Surface Blur..."
          : (dust ? "Dust && Scratches..."
                  : (median
                         ? "Median..."
                         : (high_pass ? "High Pass..." : "Gaussian Blur...")));
  entry.native_class_id =
      plastic  ? "PlsW"
      : unsharp  ? "UnsM"
      : motion ? "MtnB"
      : surface
          ? "surfaceBlur"
          : (dust ? "DstS" : (median ? "Mdn " : (high_pass ? "HghP" : "GsnB")));
  entry.native_filter_id =
      plastic   ? 0x506c7357U
      : unsharp   ? 0x556e734dU
      : motion  ? 0x4d746e42U
      : surface ? 854U
                : (dust ? 0x44737453U
                        : (median ? 0x4d646e20U
                                  : (high_pass ? 0x48676850U : 0x47736e42U)));
  entry.enabled = true;
  entry.has_options = true;
  entry.opacity = 1.0;
  entry.blend_mode = BlendMode::Normal;
  entry.foreground = foreground;
  entry.background = background;
  if (plastic) {
    entry.parameters = PlasticWrapSmartFilter{
        smart_filter_integer_from_invocation(
            invocation, "highlight_strength", 9, 0, 20),
        smart_filter_integer_from_invocation(invocation, "detail", 7, 1, 15),
        smart_filter_integer_from_invocation(invocation, "smoothness", 5, 1,
                                             15)};
  } else if (unsharp) {
    entry.parameters = UnsharpMaskSmartFilter{
        static_cast<double>(smart_filter_integer_from_invocation(
            invocation, "amount", 150, 1, 500)),
        std::clamp(radius, 0.1, 1000.0),
        smart_filter_integer_from_invocation(invocation, "threshold", 8, 0,
                                             255)};
  } else if (motion) {
    entry.parameters = MotionBlurSmartFilter{
        smart_filter_integer_from_invocation(invocation, "angle", 0, -360, 360),
        smart_filter_integer_from_invocation(invocation, "distance", 12, 1,
                                             999)};
  } else if (surface) {
    entry.parameters =
        SurfaceBlurSmartFilter{std::clamp(radius, 1.0, 100.0),
                               smart_filter_integer_from_invocation(
                                   invocation, "threshold", 15, 2, 255)};
  } else if (dust) {
    entry.parameters = DustAndScratchesSmartFilter{
        smart_filter_integer_from_invocation(invocation, "radius", 1, 1, 100),
        smart_filter_integer_from_invocation(invocation, "threshold", 0, 0,
                                             255)};
  } else if (median) {
    entry.parameters = MedianSmartFilter{std::clamp(radius, 1.0, 500.0)};
  } else if (high_pass) {
    entry.parameters =
        HighPassSmartFilter{std::clamp(radius, 0.1, 1000.0)};
  } else {
    entry.parameters =
        GaussianBlurSmartFilter{std::clamp(radius, 0.1, 1000.0)};
  }
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
  editable_smart_filter_dialog(layer_id, SmartFilterKind::GaussianBlur,
                               execution_index);
}

void MainWindow::high_pass_smart_filter_dialog(
    LayerId layer_id, std::optional<std::size_t> execution_index) {
  editable_smart_filter_dialog(layer_id, SmartFilterKind::HighPass,
                               execution_index);
}

void MainWindow::median_smart_filter_dialog(
    LayerId layer_id, std::optional<std::size_t> execution_index) {
  editable_smart_filter_dialog(layer_id, SmartFilterKind::Median,
                               execution_index);
}

void MainWindow::dust_and_scratches_smart_filter_dialog(
    LayerId layer_id, std::optional<std::size_t> execution_index) {
  editable_smart_filter_dialog(layer_id, SmartFilterKind::DustAndScratches,
                               execution_index);
}

void MainWindow::surface_blur_smart_filter_dialog(
    LayerId layer_id, std::optional<std::size_t> execution_index) {
  editable_smart_filter_dialog(layer_id, SmartFilterKind::SurfaceBlur,
                               execution_index);
}

void MainWindow::unsharp_mask_smart_filter_dialog(
    LayerId layer_id, std::optional<std::size_t> execution_index) {
  editable_smart_filter_dialog(layer_id, SmartFilterKind::UnsharpMask,
                               execution_index);
}

void MainWindow::motion_blur_smart_filter_dialog(
    LayerId layer_id, std::optional<std::size_t> execution_index) {
  editable_smart_filter_dialog(layer_id, SmartFilterKind::MotionBlur,
                               execution_index);
}

void MainWindow::plastic_wrap_smart_filter_dialog(
    LayerId layer_id, std::optional<std::size_t> execution_index) {
  editable_smart_filter_dialog(layer_id, SmartFilterKind::PlasticWrap,
                               execution_index);
}

void MainWindow::editable_smart_filter_dialog(
    LayerId layer_id, SmartFilterKind kind,
    std::optional<std::size_t> execution_index) {
  if (kind != SmartFilterKind::GaussianBlur &&
      kind != SmartFilterKind::HighPass && kind != SmartFilterKind::Median &&
      kind != SmartFilterKind::DustAndScratches &&
      kind != SmartFilterKind::SurfaceBlur &&
      kind != SmartFilterKind::UnsharpMask &&
      kind != SmartFilterKind::MotionBlur &&
      kind != SmartFilterKind::PlasticWrap) {
    return;
  }
  const auto high_pass = kind == SmartFilterKind::HighPass;
  const auto median = kind == SmartFilterKind::Median;
  const auto dust = kind == SmartFilterKind::DustAndScratches;
  const auto surface = kind == SmartFilterKind::SurfaceBlur;
  const auto unsharp = kind == SmartFilterKind::UnsharpMask;
  const auto motion = kind == SmartFilterKind::MotionBlur;
  const auto plastic = kind == SmartFilterKind::PlasticWrap;
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
  auto initial_invocation = editable_smart_filter_invocation(kind);
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
    stack.entries.push_back(make_editable_smart_filter_entry(
        kind, initial_invocation, to_rgb(canvas_->primary_color()),
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
    if (stack.entries[filter_index].kind != kind) {
      statusBar()->showMessage(
          tr("This Smart Filter can only be preserved, not edited"));
      return;
    }
    if (kind == SmartFilterKind::GaussianBlur) {
      const auto* gaussian = std::get_if<GaussianBlurSmartFilter>(
          &stack.entries[filter_index].parameters);
      if (gaussian == nullptr) {
        statusBar()->showMessage(
            tr("This Smart Filter can only be preserved, not edited"));
        return;
      }
      initial_invocation.parameters["radius"] = gaussian->radius_pixels;
    } else if (kind == SmartFilterKind::HighPass) {
      const auto* radius = std::get_if<HighPassSmartFilter>(
          &stack.entries[filter_index].parameters);
      if (radius == nullptr) {
        statusBar()->showMessage(
            tr("This Smart Filter can only be preserved, not edited"));
        return;
      }
      initial_invocation.parameters["radius"] = radius->radius_pixels;
    } else if (kind == SmartFilterKind::Median) {
      const auto* radius = std::get_if<MedianSmartFilter>(
          &stack.entries[filter_index].parameters);
      if (radius == nullptr) {
        statusBar()->showMessage(
            tr("This Smart Filter can only be preserved, not edited"));
        return;
      }
      initial_invocation.parameters["radius"] = radius->radius_pixels;
    } else if (kind == SmartFilterKind::DustAndScratches) {
      const auto* settings = std::get_if<DustAndScratchesSmartFilter>(
          &stack.entries[filter_index].parameters);
      if (settings == nullptr) {
        statusBar()->showMessage(
            tr("This Smart Filter can only be preserved, not edited"));
        return;
      }
      initial_invocation.parameters["radius"] =
          static_cast<std::int64_t>(settings->radius_pixels);
      initial_invocation.parameters["threshold"] =
          static_cast<std::int64_t>(settings->threshold);
    } else if (kind == SmartFilterKind::SurfaceBlur) {
      const auto *settings = std::get_if<SurfaceBlurSmartFilter>(
          &stack.entries[filter_index].parameters);
      if (settings == nullptr) {
        statusBar()->showMessage(
            tr("This Smart Filter can only be preserved, not edited"));
        return;
      }
      initial_invocation.parameters["radius"] = settings->radius_pixels;
      initial_invocation.parameters["threshold"] =
          static_cast<std::int64_t>(settings->threshold);
    } else if (kind == SmartFilterKind::UnsharpMask) {
      const auto *settings = std::get_if<UnsharpMaskSmartFilter>(
          &stack.entries[filter_index].parameters);
      if (settings == nullptr) {
        statusBar()->showMessage(
            tr("This Smart Filter can only be preserved, not edited"));
        return;
      }
      initial_invocation.parameters["amount"] =
          static_cast<std::int64_t>(std::lround(settings->amount_percent));
      initial_invocation.parameters["radius"] = settings->radius_pixels;
      initial_invocation.parameters["threshold"] =
          static_cast<std::int64_t>(settings->threshold);
    } else if (kind == SmartFilterKind::MotionBlur) {
      const auto *settings = std::get_if<MotionBlurSmartFilter>(
          &stack.entries[filter_index].parameters);
      if (settings == nullptr) {
        statusBar()->showMessage(
            tr("This Smart Filter can only be preserved, not edited"));
        return;
      }
      initial_invocation.parameters["angle"] =
          static_cast<std::int64_t>(settings->angle_degrees);
      initial_invocation.parameters["distance"] =
          static_cast<std::int64_t>(settings->distance_pixels);
    } else {
      const auto *settings = std::get_if<PlasticWrapSmartFilter>(
          &stack.entries[filter_index].parameters);
      if (settings == nullptr) {
        statusBar()->showMessage(
            tr("This Smart Filter can only be preserved, not edited"));
        return;
      }
      initial_invocation.parameters["highlight_strength"] =
          static_cast<std::int64_t>(settings->highlight_strength);
      initial_invocation.parameters["detail"] =
          static_cast<std::int64_t>(settings->detail);
      initial_invocation.parameters["smoothness"] =
          static_cast<std::int64_t>(settings->smoothness);
    }
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
       unfiltered_pixels, unfiltered_bounds, original_pixels,
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

        const auto candidate = smart_filter_stack_with_invocation(
            stack, filter_index, settings.invocation);
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

  auto preview_edit_lock = lock_preview_dialog_edits();
  const auto dialog_spec =
      editable_smart_filter_dialog_spec(kind, initial_invocation);
  const auto settings = request_filter_settings(
      this, dialog_spec, preview_changed, std::move(initial_invocation));
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
    statusBar()->showMessage(
        plastic  ? tr("Cancelled Plastic Wrap")
        : unsharp  ? tr("Cancelled Unsharp Mask")
        : motion ? tr("Cancelled Motion Blur")
        : surface
            ? tr("Cancelled Surface Blur")
            : (dust ? tr("Cancelled Dust & Scratches")
                    : (median ? tr("Cancelled Median")
                              : (high_pass ? tr("Cancelled High Pass")
                                           : tr("Cancelled Gaussian Blur")))));
    return;
  }

  auto candidate = smart_filter_stack_with_invocation(
      std::move(stack), filter_index, *settings);
  const auto undo_text =
      plastic     ? (adding ? tr("Add Plastic Wrap Smart Filter")
                            : tr("Edit Plastic Wrap Smart Filter"))
      : unsharp     ? (adding ? tr("Add Unsharp Mask Smart Filter")
                            : tr("Edit Unsharp Mask Smart Filter"))
      : motion    ? (adding ? tr("Add Motion Blur Smart Filter")
                            : tr("Edit Motion Blur Smart Filter"))
      : surface   ? (adding ? tr("Add Surface Blur Smart Filter")
                            : tr("Edit Surface Blur Smart Filter"))
      : dust      ? (adding ? tr("Add Dust & Scratches Smart Filter")
                            : tr("Edit Dust & Scratches Smart Filter"))
      : median    ? (adding ? tr("Add Median Smart Filter")
                            : tr("Edit Median Smart Filter"))
      : high_pass ? (adding ? tr("Add High Pass Smart Filter")
                            : tr("Edit High Pass Smart Filter"))
                  : (adding ? tr("Add Gaussian Blur Smart Filter")
                            : tr("Edit Gaussian Blur Smart Filter"));
  const auto status_text =
      plastic  ? (adding ? (creating_stack
                                ? tr("Added Plastic Wrap as a Smart Filter")
                                : tr("Added another Plastic Wrap Smart Filter"))
                         : tr("Updated Plastic Wrap Smart Filter"))
      : unsharp  ? (adding ? (creating_stack
                                ? tr("Added Unsharp Mask as a Smart Filter")
                                : tr("Added another Unsharp Mask Smart Filter"))
                         : tr("Updated Unsharp Mask Smart Filter"))
      : motion ? (adding ? (creating_stack
                                ? tr("Added Motion Blur as a Smart Filter")
                                : tr("Added another Motion Blur Smart Filter"))
                         : tr("Updated Motion Blur Smart Filter"))
      : surface
          ? (adding ? (creating_stack
                           ? tr("Added Surface Blur as a Smart Filter")
                           : tr("Added another Surface Blur Smart Filter"))
                    : tr("Updated Surface Blur Smart Filter"))
      : dust
          ? (adding ? (creating_stack
                           ? tr("Added Dust & Scratches as a Smart Filter")
                           : tr("Added another Dust & Scratches Smart Filter"))
                    : tr("Updated Dust & Scratches Smart Filter"))
      : median
          ? (adding ? (creating_stack ? tr("Added Median as a Smart Filter")
                                      : tr("Added another Median Smart Filter"))
                    : tr("Updated Median Smart Filter"))
      : high_pass
          ? (adding
                 ? (creating_stack
                        ? tr("Added Median as a Smart Filter")
                        : tr("Added another Median Smart Filter"))
                 : tr("Updated Median Smart Filter"))
          : high_pass
          ? (adding
                 ? (creating_stack
                        ? tr("Added High Pass as a Smart Filter")
                        : tr("Added another High Pass Smart Filter"))
                 : tr("Updated High Pass Smart Filter"))
          : (adding
                 ? (creating_stack
                        ? tr("Added Gaussian Blur as a Smart Filter")
                        : tr("Added another Gaussian Blur Smart Filter"))
                 : tr("Updated Gaussian Blur Smart Filter"));
  if (!commit_smart_filter_stack_edit(
          layer_id, std::move(candidate), std::move(entry_sources),
          undo_text, status_text)) {
    statusBar()->showMessage(
        tr("This Smart Filter descriptor cannot be edited safely"));
  }
}

void MainWindow::edit_smart_filter(LayerId layer_id,
                                   std::size_t execution_index) {
  const auto* layer = has_active_document()
                          ? std::as_const(document()).find_layer(layer_id)
                          : nullptr;
  const auto* stack = layer != nullptr ? layer->smart_filter_stack() : nullptr;
  if (stack == nullptr || execution_index >= stack->entries.size()) {
    return;
  }
  const auto kind = stack->entries[execution_index].kind;
  if (kind == SmartFilterKind::GaussianBlur ||
      kind == SmartFilterKind::HighPass || kind == SmartFilterKind::Median ||
      kind == SmartFilterKind::DustAndScratches ||
      kind == SmartFilterKind::SurfaceBlur ||
      kind == SmartFilterKind::UnsharpMask ||
      kind == SmartFilterKind::MotionBlur ||
      kind == SmartFilterKind::PlasticWrap) {
    editable_smart_filter_dialog(layer_id, kind, execution_index);
  }
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
    if (identifier == QStringLiteral("patchy.filters.high_pass")) {
      high_pass_smart_filter_dialog(*active);
      return;
    }
    if (identifier == QStringLiteral("patchy.filters.median")) {
      median_smart_filter_dialog(*active);
      return;
    }
    if (identifier ==
        QStringLiteral("patchy.filters.dust_and_scratches")) {
      dust_and_scratches_smart_filter_dialog(*active);
      return;
    }
    if (identifier == QStringLiteral("patchy.filters.surface_blur")) {
      surface_blur_smart_filter_dialog(*active);
      return;
    }
    if (identifier == QStringLiteral("patchy.filters.unsharp_mask")) {
      unsharp_mask_smart_filter_dialog(*active);
      return;
    }
    if (identifier == QStringLiteral("patchy.filters.motion_blur")) {
      motion_blur_smart_filter_dialog(*active);
      return;
    }
    if (identifier == QStringLiteral("patchy.filters.plastic_wrap")) {
      plastic_wrap_smart_filter_dialog(*active);
      return;
    }
    statusBar()->showMessage(
        tr("This filter is not currently editable as a Smart Filter"));
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

void MainWindow::liquify_dialog() {
  if (!has_active_document()) {
    return;
  }
  if (canvas_ != nullptr && canvas_->quick_mask_active()) {
    statusBar()->showMessage(
        tr("Liquify is unavailable in Quick Mask mode"));
    return;
  }
  if (canvas_ != nullptr &&
      (canvas_->layer_edit_target() ==
           CanvasWidget::LayerEditTarget::DocumentChannel ||
       canvas_->layer_edit_target() ==
           CanvasWidget::LayerEditTarget::ComponentRed ||
       canvas_->layer_edit_target() ==
           CanvasWidget::LayerEditTarget::ComponentGreen ||
       canvas_->layer_edit_target() ==
           CanvasWidget::LayerEditTarget::ComponentBlue)) {
    statusBar()->showMessage(
        tr("Liquify is unavailable while viewing a document channel"));
    return;
  }

  auto& doc = document();
  const auto active = doc.active_layer_id();
  if (!active.has_value()) {
    return;
  }
  const auto& read_only_doc = std::as_const(doc);
  const auto* source_layer = read_only_doc.find_layer(*active);
  if (!editable_rgb8_layer(source_layer)) {
    statusBar()->showMessage(tr("Select an editable RGB pixel layer"));
    return;
  }
  if (layer_is_smart_object(*source_layer)) {
    statusBar()->showMessage(
        tr("Rasterize the Smart Object before using Liquify"));
    return;
  }
  if (layer_id_locks_image_pixels(*active)) {
    statusBar()->showMessage(tr("Layer pixels are locked."));
    return;
  }

  const auto original_pixels = source_layer->pixels();
  const auto bounds = source_layer->bounds();
  const auto selection = canvas_->selected_document_region();
  const auto mesh = request_liquify(this, original_pixels, bounds, selection);
  if (!mesh.has_value()) {
    statusBar()->showMessage(tr("Cancelled Liquify"));
    return;
  }
  if (mesh->is_identity()) {
    statusBar()->showMessage(tr("Liquify made no changes"));
    return;
  }

  canvas_->begin_processing_operation();
  const auto finish_processing = qScopeGuard([this] {
    if (canvas_ != nullptr) {
      canvas_->end_processing_operation();
    }
  });
  QProgressDialog progress(tr("Applying Liquify..."), tr("Cancel"), 0, 100,
                           this);
  progress.setObjectName(QStringLiteral("liquifyProgressDialog"));
  progress.setWindowModality(Qt::WindowModal);
  progress.setMinimumDuration(kFilterProgressMinimumDurationMs);
  remember_dialog_position(progress);
  int last_value = -1;
  auto rendered = mesh->render(
      original_pixels, [&](int completed, int total) {
        const int value = total <= 0
                              ? 100
                              : std::clamp(completed * 100 / total, 0, 100);
        if (value != last_value) {
          progress.setValue(value);
          last_value = value;
          QApplication::processEvents();
        }
        canvas_->tick_processing_operation();
        return !progress.wasCanceled();
      });
  if (!rendered.has_value()) {
    statusBar()->showMessage(tr("Cancelled Liquify"));
    return;
  }

  if (!selection.isEmpty()) {
    const auto bytes_per_pixel_value =
        static_cast<int>(bytes_per_pixel(rendered->format()));
    for (int y = 0; y < rendered->height(); ++y) {
      for (int x = 0; x < rendered->width(); ++x) {
        if (!selection.contains(QPoint(bounds.x + x, bounds.y + y))) {
          std::copy_n(original_pixels.pixel(x, y), bytes_per_pixel_value,
                      rendered->pixel(x, y));
        }
      }
    }
  }
  if (pixel_buffers_equal(*rendered, original_pixels)) {
    statusBar()->showMessage(tr("Liquify made no changes"));
    return;
  }

  push_undo_snapshot(tr("Liquify"));
  auto* layer = doc.find_layer(*active);
  if (layer == nullptr) {
    return;
  }
  set_layer_pixels_preserving_origin(*layer, std::move(*rendered), bounds);
  canvas_->document_changed(to_qrect(bounds));
  schedule_palette_compliance_check();
  statusBar()->showMessage(tr("Applied Liquify"));
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
