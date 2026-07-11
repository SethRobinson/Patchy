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
#include "core/palette_presets.hpp"
#include "core/pixel_tools.hpp"
#include "formats/palette_io.hpp"
#include "filters/builtin_filters.hpp"
#include "formats/bmp_document_io.hpp"
#include "plugins/legacy_photoshop_adapter.hpp"
#include "psd/psd_document_io.hpp"
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

FilterProgress progress_dialog_filter_progress(QProgressDialog& progress,
                                               std::function<QString(const QString&)> label_text,
                                               QEventLoop::ProcessEventsFlags event_flags,
                                               std::function<void()> tick_processing = {}) {
  auto last_progress_value = std::make_shared<int>(-1);
  return FilterProgress{[&progress, label_text = std::move(label_text), event_flags,
                         tick_processing = std::move(tick_processing),
                         last_progress_value](int completed, int total, const QString& detail) {
    const auto value = total <= 0 ? 100 : std::clamp((completed * 100) / total, 0, 100);
    if (value != *last_progress_value) {
      progress.setValue(value);
      if (!detail.isEmpty()) {
        progress.setLabelText(label_text(detail));
      }
      *last_progress_value = value;
      QApplication::processEvents(event_flags);
    }
    if (tick_processing) {
      tick_processing();
    }
    return !progress.wasCanceled();
  }};
}


}  // namespace

void MainWindow::apply_filter(const QString& identifier) {
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
  if (layer == nullptr || layer->kind() != LayerKind::Pixel || layer->pixels().format().bit_depth != BitDepth::UInt8 ||
      layer->pixels().format().channels < 3) {
    statusBar()->showMessage(tr("Select an editable RGB pixel layer"));
    return;
  }
  if (layer_id_locks_image_pixels(*active)) {
    statusBar()->showMessage(tr("Layer pixels are locked."));
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
    auto original_pixels = std::make_shared<const PixelBuffer>(layer->pixels());
    // Tracks the bounds the layer currently shows in the preview. Blur-family
    // filters grow the layer, so each swap must repaint the union of the previous
    // and new bounds to erase any stale halo left behind when the layer shrinks.
    auto last_preview_bounds = std::make_shared<Rect>(bounds);
    const auto foreground = canvas_->primary_color();
    const auto background = canvas_->secondary_color();
    auto preview_registry = std::make_shared<FilterRegistry>(filters_);
    auto preview_state = std::make_shared<AsyncPixelPreviewState<FilterPreviewSettings>>();
    preview_state->start =
        [this, preview_state, active, original_pixels, last_preview_bounds, selection, bounds, identifier, foreground,
         background, preview_registry](const FilterPreviewSettings& settings) {
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
                       selection, bounds, identifier, settings, foreground, background, preview_registry, active] {
            auto result = std::make_shared<PixelBuffer>();
            auto error = std::make_shared<QString>();
            try {
              *result = build_filter_preview_pixels(*original_pixels, selection, bounds, identifier, *preview_registry,
                                                    settings, foreground, background, nullptr, &*result_bounds);
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
    const auto settings = request_filter_settings(this, dialog_spec, preview_changed);
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
    FilterProgress filter_progress{[&](int completed, int total, const QString& detail) {
      const auto value = total <= 0 ? 100 : std::clamp((completed * 100) / total, 0, 100);
      if (value != last_progress_value) {
        progress.setValue(value);
        if (!detail.isEmpty()) {
          progress.setLabelText(tr("Applying %1...\n%2").arg(display_name, detail));
        }
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
      final_pixels = build_filter_preview_pixels(*original_pixels, selection, bounds, identifier, filters_,
                                                 FilterPreviewSettings{true, *settings}, foreground, background,
                                                 &filter_progress, &final_bounds);
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
    if (auto* layer = document().find_layer(*restore_active_layer); editable_rgb8_layer(layer)) {
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
  if (layer_id_locks_image_pixels(*active)) {
    statusBar()->showMessage(tr("Layer pixels are locked."));
    return;
  }
  const auto active_id = *active;
  const auto bounds = layer->bounds();
  auto original_pixels = std::make_shared<const PixelBuffer>(layer->pixels());
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
    settings.curves = CurvesAdjustment{std::clamp(curves.shadow_output, 0, 255),
                                       std::clamp(curves.midtone_output, 0, 255),
                                       std::clamp(curves.highlight_output, 0, 255)};
    update_adjustment_layer_preview(tr("Curves"), settings, enabled, preview_id, restore_active_layer);
  };

  auto preview_edit_lock = lock_preview_dialog_edits();
  const auto settings = request_curves_settings(this, preview_changed);
  remove_adjustment_layer_preview(preview_id, restore_active_layer);
  preview_edit_lock.release();
  if (!settings.has_value()) {
    statusBar()->showMessage(tr("Cancelled Curves"));
    return;
  }
  apply_curves_adjustment(settings->shadow_output, settings->midtone_output, settings->highlight_output, true);
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
  const auto active_id = *active;
  const auto bounds = layer->bounds();
  auto original_pixels = std::make_shared<const PixelBuffer>(layer->pixels());
  const auto selection = canvas_->selected_document_region();
  using CurvesPreviewRequest = AdjustmentPixelPreviewRequest<CurvesSettings>;
  const auto curves_has_effect = [](const CurvesSettings& settings) {
    return !(settings.shadow_output == 0 && settings.midtone_output == 128 && settings.highlight_output == 255);
  };
  auto preview_state = std::make_shared<AsyncPixelPreviewState<CurvesPreviewRequest>>();
  preview_state->start = [this, preview_state, active_id, bounds, original_pixels, selection,
                          curves_has_effect](const CurvesPreviewRequest& request) {
    if (!request.enabled || !curves_has_effect(request.settings)) {
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
  const auto preview_changed = [preview_state, curves_has_effect](bool enabled, const CurvesSettings& settings) {
    enqueue_async_pixel_preview(preview_state, CurvesPreviewRequest{enabled, settings},
                                !enabled || !curves_has_effect(settings));
  };

  auto preview_edit_lock = lock_preview_dialog_edits();
  const auto settings = request_curves_settings(this, preview_changed);
  close_async_pixel_preview(preview_state);
  layer = doc.find_layer(active_id);
  if (layer == nullptr) {
    return;
  }
  set_layer_pixels_preserving_origin(*layer, *original_pixels, bounds);
  canvas_->document_changed(to_qrect(bounds));
  preview_edit_lock.release();
  if (!settings.has_value()) {
    statusBar()->showMessage(tr("Cancelled Curves"));
    return;
  }
  apply_curves_adjustment(settings->shadow_output, settings->midtone_output, settings->highlight_output);
}

void MainWindow::apply_curves_adjustment(int shadow_output, int midtone_output, int highlight_output,
                                        bool allow_identity) {
  AdjustmentSettings settings;
  settings.kind = AdjustmentKind::Curves;
  settings.curves = CurvesAdjustment{std::clamp(shadow_output, 0, 255), std::clamp(midtone_output, 0, 255),
                                     std::clamp(highlight_output, 0, 255)};
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
  const auto active_id = *active;
  const auto bounds = layer->bounds();
  auto original_pixels = std::make_shared<const PixelBuffer>(layer->pixels());
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
  const auto active_id = *active;
  const auto bounds = layer->bounds();
  auto original_pixels = std::make_shared<const PixelBuffer>(layer->pixels());
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

  std::optional<AdjustmentSettings> accepted_settings;
  auto preview_edit_lock = lock_preview_dialog_edits();
  switch (original_settings->kind) {
    case AdjustmentKind::Levels: {
      const auto preview_changed = [apply_settings, original_settings](bool enabled, const LevelsSettings& levels) {
        auto settings = *original_settings;
        settings.levels = sanitized_levels_adjustment(levels);
        apply_settings(enabled ? settings : *original_settings);
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
      const auto preview_changed = [apply_settings, original_settings](bool enabled, const CurvesSettings& curves) {
        auto settings = *original_settings;
        settings.curves = CurvesAdjustment{std::clamp(curves.shadow_output, 0, 255),
                                           std::clamp(curves.midtone_output, 0, 255),
                                           std::clamp(curves.highlight_output, 0, 255)};
        apply_settings(enabled ? settings : *original_settings);
      };
      const auto result = request_curves_settings(this, preview_changed,
                                                  CurvesSettings{original_settings->curves.shadow_output,
                                                                 original_settings->curves.midtone_output,
                                                                 original_settings->curves.highlight_output});
      if (result.has_value()) {
        accepted_settings = *original_settings;
        accepted_settings->curves =
            CurvesAdjustment{std::clamp(result->shadow_output, 0, 255),
                             std::clamp(result->midtone_output, 0, 255),
                             std::clamp(result->highlight_output, 0, 255)};
      }
      break;
    }
    case AdjustmentKind::HueSaturation: {
      const auto preview_changed = [apply_settings, original_settings](bool enabled,
                                                                       const HueSaturationSettings& hue_saturation) {
        auto settings = *original_settings;
        settings.hue_saturation = to_hue_saturation_adjustment(hue_saturation);
        apply_settings(enabled ? settings : *original_settings);
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
      const auto preview_changed = [apply_settings, original_settings](bool enabled,
                                                                       const ColorBalanceSettings& color_balance) {
        auto settings = *original_settings;
        settings.color_balance =
            ColorBalanceAdjustment{std::clamp(color_balance.cyan_red, -100, 100),
                                   std::clamp(color_balance.magenta_green, -100, 100),
                                   std::clamp(color_balance.yellow_blue, -100, 100)};
        apply_settings(enabled ? settings : *original_settings);
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

  apply_settings(*original_settings);
  preview_edit_lock.release();
  if (!accepted_settings.has_value()) {
    refresh_layer_list();
    refresh_layer_controls();
    statusBar()->showMessage(tr("Cancelled adjustment edit"));
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
