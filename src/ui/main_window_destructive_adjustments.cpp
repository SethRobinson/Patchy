// MainWindow destructive adjustment dialogs, split out of
// main_window_adjustments.cpp: the Levels / Curves / Hue-Saturation /
// Color Balance dialogs that rewrite layer pixels in place. Their formerly
// per-dialog async preview workers now run through the shared launcher in
// main_window_shared.{hpp,cpp} (make_destructive_adjustment_preview_state);
// everything else is a pure function move from the pre-split code.

#include "core/adjustment_layer.hpp"
#include "core/smart_object.hpp"
#include "core/pixel_buffer.hpp"
#include "ui/curves_editor.hpp"
#include "ui/main_window.hpp"
#include <QStatusBar>
#include "ui/canvas_widget.hpp"
#include "ui/main_window_shared.hpp"

#include "core/palette.hpp"
#include "ui/image_document_io.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/camera_raw_filter_dialog.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/qt_geometry.hpp"

#include <QColor>
#include <QPoint>
#include <QPointer>
#include <QProgressDialog>
#include <QRect>
#include <QScopeGuard>
#include <QSize>
#include <QString>
#include <QStringLiteral>
#include <Qt>
#include <QtGlobal>

#include <functional>
#include <memory>
#include <optional>
#include <utility>


// Icon resources live in the static patchy_ui library; force registration before first use.
int qInitResources_icons();

namespace patchy::ui {

namespace {

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

}  // namespace

void MainWindow::levels_dialog() {
  auto& doc = document();
  select_only_layer_if_none_active();
  const auto active = doc.active_layer_id();
  if (!active.has_value()) {
    return;
  }
  auto* layer = doc.find_layer(*active);
  if (!editable_rgb_layer_any_depth(layer)) {
    show_status_error(tr("Select an editable RGB pixel layer"));
    return;
  }
  if (layer_is_smart_object(*layer)) {
    show_status_error(tr(
        "Rasterize the Smart Object before applying destructive filters or adjustments"));
    return;
  }
  if (layer_id_locks_image_pixels(*active)) {
    show_status_error(tr("Layer pixels are locked."));
    return;
  }
  if (!prompt_rasterize_procedural_layer(*active, tr("Levels"), false)) {
    return;
  }
  layer = doc.find_layer(*active);
  if (!editable_rgb_layer_any_depth(layer)) {
    show_status_error(tr("Select an editable RGB pixel layer"));
    return;
  }
  const auto active_id = *active;
  const auto bounds = layer->bounds();
  auto original_pixels =
      std::make_shared<const PixelBuffer>(std::as_const(*layer).pixels());
  const auto selection = filter_selection(*canvas_);
  DestructiveAdjustmentPreviewHooks preview_hooks;
  preview_hooks.original_pixels = original_pixels;
  preview_hooks.restore_identity = [this, active_id, bounds, original_pixels] {
    if (auto* preview_layer = document().find_layer(active_id); preview_layer != nullptr) {
      set_layer_pixels_preserving_origin(*preview_layer, *original_pixels, bounds);
      if (canvas_ != nullptr) {
        canvas_->document_changed(to_qrect(bounds));
      }
    }
  };
  preview_hooks.apply_result = [window = QPointer<MainWindow>(this), active_id,
                                bounds](PixelBuffer result) {
    if (window == nullptr) {
      return;
    }
    if (auto* preview_layer = window->document().find_layer(active_id); preview_layer != nullptr) {
      set_layer_pixels_preserving_origin(*preview_layer, std::move(result), bounds);
      if (window->canvas_ != nullptr) {
        window->canvas_->document_changed(to_qrect(bounds));
      }
    }
  };
  preview_hooks.preview_render_active = [window = QPointer<MainWindow>(this)](bool active) {
    if (window != nullptr && window->canvas_ != nullptr) {
      if (active) {
        window->canvas_->begin_preview_render();
      } else {
        window->canvas_->end_preview_render();
      }
    }
  };
  auto preview_state = make_destructive_adjustment_preview_state(std::move(preview_hooks));
  const auto preview_changed = [preview_state, bounds, selection](bool enabled,
                                                                  const LevelsSettings& settings) {
    const auto identity = !enabled || !levels_settings_have_effect(settings);
    DestructiveAdjustmentPreviewRequest request;
    request.identity = identity;
    if (!identity) {
      request.render = [bounds, selection, settings](PixelBuffer& pixels) {
        apply_levels_to_pixels(pixels, bounds, selection, settings, nullptr);
      };
    }
    enqueue_async_pixel_preview(preview_state, std::move(request), identity);
  };

  auto preview_edit_lock = lock_preview_dialog_edits();
  auto preview_cleanup = qScopeGuard([this, &doc, preview_state, original = *layer] {
    close_async_pixel_preview(preview_state);
    if (auto* target = doc.find_layer(original.id()); target != nullptr) {
      *target = original;
      canvas_->document_changed();
    }
  });
  const auto settings = request_levels_settings(this, preview_changed, {}, original_pixels.get());
  close_async_pixel_preview(preview_state);
  layer = doc.find_layer(active_id);
  if (layer == nullptr) {
    return;
  }
  set_layer_pixels_preserving_origin(*layer, *original_pixels, bounds);
  canvas_->document_changed(to_qrect(bounds));
  preview_cleanup.dismiss();
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
    try {
      run_filter_compute_with_progress(
          progress,
          [display_name](const QString& detail) { return tr("Applying %1...\n%2").arg(display_name, detail); },
          [this] {
            if (canvas_ != nullptr) {
              canvas_->tick_processing_operation();
            }
          },
          [&](FilterProgress& filter_progress) {
            apply_levels_to_pixels(final_pixels, bounds, selection, *settings, &filter_progress);
          });
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

void MainWindow::curves_dialog() {
  auto& doc = document();
  select_only_layer_if_none_active();
  const auto active = doc.active_layer_id();
  if (!active.has_value()) {
    return;
  }
  auto* layer = doc.find_layer(*active);
  if (!editable_rgb_layer_any_depth(layer)) {
    show_status_error(tr("Select an editable RGB pixel layer"));
    return;
  }
  if (layer_is_smart_object(*layer)) {
    show_status_error(tr(
        "Rasterize the Smart Object before applying destructive filters or adjustments"));
    return;
  }
  if (layer_id_locks_image_pixels(*active)) {
    show_status_error(tr("Layer pixels are locked."));
    return;
  }
  if (!prompt_rasterize_procedural_layer(*active, tr("Curves"), false)) {
    return;
  }
  layer = doc.find_layer(*active);
  if (!editable_rgb_layer_any_depth(layer)) {
    show_status_error(tr("Select an editable RGB pixel layer"));
    return;
  }
  const auto active_id = *active;
  const auto bounds = layer->bounds();
  auto original_layer = std::make_shared<const Layer>(*layer);
  auto original_pixels = std::make_shared<const PixelBuffer>(original_layer->pixels());
  const auto selection = filter_selection(*canvas_);
  DestructiveAdjustmentPreviewHooks preview_hooks;
  preview_hooks.original_pixels = original_pixels;
  // Unlike the other destructive dialogs, the identity restore puts back the
  // whole original Layer (not just its pixels), exactly as the pre-launcher
  // worker did.
  preview_hooks.restore_identity = [this, active_id, bounds, original_layer] {
    if (auto* preview_layer = document().find_layer(active_id); preview_layer != nullptr) {
      *preview_layer = *original_layer;
      if (canvas_ != nullptr) {
        canvas_->document_changed(to_qrect(bounds));
      }
    }
  };
  preview_hooks.apply_result = [window = QPointer<MainWindow>(this), active_id,
                                bounds](PixelBuffer result) {
    if (window == nullptr) {
      return;
    }
    if (auto* preview_layer = window->document().find_layer(active_id); preview_layer != nullptr) {
      set_layer_pixels_preserving_origin(*preview_layer, std::move(result), bounds);
      if (window->canvas_ != nullptr) {
        window->canvas_->document_changed(to_qrect(bounds));
      }
    }
  };
  preview_hooks.preview_render_active = [window = QPointer<MainWindow>(this)](bool active) {
    if (window != nullptr && window->canvas_ != nullptr) {
      if (active) {
        window->canvas_->begin_preview_render();
      } else {
        window->canvas_->end_preview_render();
      }
    }
  };
  auto preview_state = make_destructive_adjustment_preview_state(std::move(preview_hooks));
  const auto preview_changed = [preview_state, bounds, selection](bool enabled,
                                                                  const CurvesSettings& settings) {
    const auto identity = !enabled || !curves_settings_have_effect(settings);
    DestructiveAdjustmentPreviewRequest request;
    request.identity = identity;
    if (!identity) {
      request.render = [bounds, selection, settings](PixelBuffer& pixels) {
        apply_curves_to_pixels(pixels, bounds, selection, settings, nullptr);
      };
    }
    enqueue_async_pixel_preview(preview_state, std::move(request), identity);
  };

  const auto histograms = curves_histograms_from_pixels(original_pixels.get());
  const auto hooks = curves_canvas_hooks(
      canvas_, [this, active_id, original_pixels, bounds](QPoint point) {
        const auto image = qimage_from_document_rect_with_layer_pixels(
            std::as_const(document()), QRect(point, QSize(1, 1)), true, active_id, *original_pixels, bounds);
        return image.isNull() ? QColor{} : image.pixelColor(0, 0);
      });
  auto preview_edit_lock = lock_preview_dialog_edits();
  auto preview_cleanup = qScopeGuard([this, &doc, preview_state, original = *layer] {
    close_async_pixel_preview(preview_state);
    if (auto* target = doc.find_layer(original.id()); target != nullptr) {
      *target = original;
      canvas_->document_changed();
    }
  });
  const auto settings = request_curves_settings(this, preview_changed, {}, histograms, hooks);
  close_async_pixel_preview(preview_state);
  layer = doc.find_layer(active_id);
  if (layer == nullptr) {
    return;
  }
  *layer = *original_layer;
  canvas_->document_changed(to_qrect(bounds));
  preview_cleanup.dismiss();
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
    try {
      run_filter_compute_with_progress(
          progress,
          [display_name](const QString& detail) { return tr("Applying %1...\n%2").arg(display_name, detail); },
          [this] {
            if (canvas_ != nullptr) {
              canvas_->tick_processing_operation();
            }
          },
          [&](FilterProgress& filter_progress) {
            apply_curves_to_pixels(final_pixels, bounds, selection, *settings, &filter_progress);
          });
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

void MainWindow::hue_saturation_dialog() {
  auto& doc = document();
  select_only_layer_if_none_active();
  const auto active = doc.active_layer_id();
  if (!active.has_value()) {
    return;
  }
  auto* layer = doc.find_layer(*active);
  if (!editable_rgb_layer_any_depth(layer)) {
    show_status_error(tr("Select an editable RGB pixel layer"));
    return;
  }
  if (layer_is_smart_object(*layer)) {
    show_status_error(tr(
        "Rasterize the Smart Object before applying destructive filters or adjustments"));
    return;
  }
  if (!prompt_rasterize_procedural_layer(*active, tr("Hue/Saturation"), false)) {
    return;
  }
  layer = doc.find_layer(*active);
  if (!editable_rgb_layer_any_depth(layer)) {
    show_status_error(tr("Select an editable RGB pixel layer"));
    return;
  }
  const auto active_id = *active;
  const auto bounds = layer->bounds();
  auto original_pixels =
      std::make_shared<const PixelBuffer>(std::as_const(*layer).pixels());
  const auto selection = filter_selection(*canvas_);
  const auto hue_saturation_has_effect = [](const HueSaturationSettings& settings) {
    return settings.colorize || settings.hue_shift != 0 || settings.saturation_delta != 0 ||
           settings.lightness_delta != 0;
  };
  DestructiveAdjustmentPreviewHooks preview_hooks;
  preview_hooks.original_pixels = original_pixels;
  preview_hooks.restore_identity = [this, active_id, bounds, original_pixels] {
    if (auto* preview_layer = document().find_layer(active_id); preview_layer != nullptr) {
      set_layer_pixels_preserving_origin(*preview_layer, *original_pixels, bounds);
      if (canvas_ != nullptr) {
        canvas_->document_changed(to_qrect(bounds));
      }
    }
  };
  preview_hooks.apply_result = [window = QPointer<MainWindow>(this), active_id,
                                bounds](PixelBuffer result) {
    if (window == nullptr) {
      return;
    }
    if (auto* preview_layer = window->document().find_layer(active_id); preview_layer != nullptr) {
      set_layer_pixels_preserving_origin(*preview_layer, std::move(result), bounds);
      if (window->canvas_ != nullptr) {
        window->canvas_->document_changed(to_qrect(bounds));
      }
    }
  };
  preview_hooks.preview_render_active = [window = QPointer<MainWindow>(this)](bool active) {
    if (window != nullptr && window->canvas_ != nullptr) {
      if (active) {
        window->canvas_->begin_preview_render();
      } else {
        window->canvas_->end_preview_render();
      }
    }
  };
  auto preview_state = make_destructive_adjustment_preview_state(std::move(preview_hooks));
  const auto preview_changed = [preview_state, bounds, selection, hue_saturation_has_effect](
                                   bool enabled, const HueSaturationSettings& settings) {
    const auto identity = !enabled || !hue_saturation_has_effect(settings);
    DestructiveAdjustmentPreviewRequest request;
    request.identity = identity;
    if (!identity) {
      request.render = [bounds, selection, settings](PixelBuffer& pixels) {
        apply_hue_saturation_to_pixels(pixels, bounds, selection, settings, nullptr);
      };
    }
    enqueue_async_pixel_preview(preview_state, std::move(request), identity);
  };

  auto preview_edit_lock = lock_preview_dialog_edits();
  auto preview_cleanup = qScopeGuard([this, &doc, preview_state, original = *layer] {
    close_async_pixel_preview(preview_state);
    if (auto* target = doc.find_layer(original.id()); target != nullptr) {
      *target = original;
      canvas_->document_changed();
    }
  });
  const auto settings = request_hue_saturation_settings(this, preview_changed);
  close_async_pixel_preview(preview_state);
  layer = doc.find_layer(active_id);
  if (layer == nullptr) {
    return;
  }
  set_layer_pixels_preserving_origin(*layer, *original_pixels, bounds);
  canvas_->document_changed(to_qrect(bounds));
  preview_cleanup.dismiss();
  preview_edit_lock.release();
  if (!settings.has_value()) {
    statusBar()->showMessage(tr("Cancelled Hue/Saturation"));
    return;
  }
  apply_hue_saturation_adjustment(*settings);
}

void MainWindow::color_balance_dialog() {
  auto& doc = document();
  select_only_layer_if_none_active();
  const auto active = doc.active_layer_id();
  if (!active.has_value()) {
    return;
  }
  auto* layer = doc.find_layer(*active);
  if (!editable_rgb_layer_any_depth(layer)) {
    show_status_error(tr("Select an editable RGB pixel layer"));
    return;
  }
  if (layer_is_smart_object(*layer)) {
    show_status_error(tr(
        "Rasterize the Smart Object before applying destructive filters or adjustments"));
    return;
  }
  if (!prompt_rasterize_procedural_layer(*active, tr("Color Balance"), false)) {
    return;
  }
  layer = doc.find_layer(*active);
  if (!editable_rgb_layer_any_depth(layer)) {
    show_status_error(tr("Select an editable RGB pixel layer"));
    return;
  }
  const auto active_id = *active;
  const auto bounds = layer->bounds();
  auto original_pixels =
      std::make_shared<const PixelBuffer>(std::as_const(*layer).pixels());
  const auto selection = filter_selection(*canvas_);
  const auto color_balance_has_effect = [](const ColorBalanceSettings& settings) { return settings.has_effect(); };
  DestructiveAdjustmentPreviewHooks preview_hooks;
  preview_hooks.original_pixels = original_pixels;
  preview_hooks.restore_identity = [this, active_id, bounds, original_pixels] {
    if (auto* preview_layer = document().find_layer(active_id); preview_layer != nullptr) {
      set_layer_pixels_preserving_origin(*preview_layer, *original_pixels, bounds);
      if (canvas_ != nullptr) {
        canvas_->document_changed(to_qrect(bounds));
      }
    }
  };
  preview_hooks.apply_result = [window = QPointer<MainWindow>(this), active_id,
                                bounds](PixelBuffer result) {
    if (window == nullptr) {
      return;
    }
    if (auto* preview_layer = window->document().find_layer(active_id); preview_layer != nullptr) {
      set_layer_pixels_preserving_origin(*preview_layer, std::move(result), bounds);
      if (window->canvas_ != nullptr) {
        window->canvas_->document_changed(to_qrect(bounds));
      }
    }
  };
  preview_hooks.preview_render_active = [window = QPointer<MainWindow>(this)](bool active) {
    if (window != nullptr && window->canvas_ != nullptr) {
      if (active) {
        window->canvas_->begin_preview_render();
      } else {
        window->canvas_->end_preview_render();
      }
    }
  };
  auto preview_state = make_destructive_adjustment_preview_state(std::move(preview_hooks));
  const auto preview_changed = [preview_state, bounds, selection, color_balance_has_effect](
                                   bool enabled, const ColorBalanceSettings& settings) {
    const auto identity = !enabled || !color_balance_has_effect(settings);
    DestructiveAdjustmentPreviewRequest request;
    request.identity = identity;
    if (!identity) {
      request.render = [bounds, selection, settings](PixelBuffer& pixels) {
        apply_color_balance_to_pixels(pixels, bounds, selection, settings, nullptr);
      };
    }
    enqueue_async_pixel_preview(preview_state, std::move(request), identity);
  };

  auto preview_edit_lock = lock_preview_dialog_edits();
  auto preview_cleanup = qScopeGuard([this, &doc, preview_state, original = *layer] {
    close_async_pixel_preview(preview_state);
    if (auto* target = doc.find_layer(original.id()); target != nullptr) {
      *target = original;
      canvas_->document_changed();
    }
  });
  const auto settings = request_color_balance_settings(this, preview_changed, new_color_balance_settings());
  close_async_pixel_preview(preview_state);
  layer = doc.find_layer(active_id);
  if (layer == nullptr) {
    return;
  }
  set_layer_pixels_preserving_origin(*layer, *original_pixels, bounds);
  canvas_->document_changed(to_qrect(bounds));
  preview_cleanup.dismiss();
  preview_edit_lock.release();
  if (!settings.has_value()) {
    statusBar()->showMessage(tr("Cancelled Color Balance"));
    return;
  }
  apply_color_balance_adjustment(*settings);
}

// The shared body of the Image > Adjustments commands that reuse an adjustment layer's
// dialog (#528): the Levels flow (checks, async preview on the active layer, progress,
// one undo step) around apply_adjustment_settings_to_pixels, the adjustment layer's own
// transfer, so the result matches a layer of the same settings.
void MainWindow::direct_adjustment_dialog(
    const QString& display_name,
    const std::function<std::optional<AdjustmentSettings>(const DirectAdjustmentPreview&, const PixelBuffer&)>&
        request) {
  if (!has_active_document()) {
    return;
  }
  auto& doc = document();
  select_only_layer_if_none_active();
  const auto active = doc.active_layer_id();
  if (!active.has_value()) {
    return;
  }
  auto* layer = doc.find_layer(*active);
  if (!editable_rgb_layer_any_depth(layer)) {
    show_status_error(tr("Select an editable RGB pixel layer"));
    return;
  }
  if (layer_is_smart_object(*layer)) {
    show_status_error(tr(
        "Rasterize the Smart Object before applying destructive filters or adjustments"));
    return;
  }
  if (layer_id_locks_image_pixels(*active)) {
    show_status_error(tr("Layer pixels are locked."));
    return;
  }
  if (!prompt_rasterize_procedural_layer(*active, display_name, false)) {
    return;
  }
  layer = doc.find_layer(*active);
  if (!editable_rgb_layer_any_depth(layer)) {
    show_status_error(tr("Select an editable RGB pixel layer"));
    return;
  }
  const auto active_id = *active;
  const auto bounds = layer->bounds();
  auto original_pixels =
      std::make_shared<const PixelBuffer>(std::as_const(*layer).pixels());
  const auto selection = filter_selection(*canvas_);
  DestructiveAdjustmentPreviewHooks preview_hooks;
  preview_hooks.original_pixels = original_pixels;
  preview_hooks.restore_identity = [this, active_id, bounds, original_pixels] {
    if (auto* preview_layer = document().find_layer(active_id); preview_layer != nullptr) {
      set_layer_pixels_preserving_origin(*preview_layer, *original_pixels, bounds);
      if (canvas_ != nullptr) {
        canvas_->document_changed(to_qrect(bounds));
      }
    }
  };
  preview_hooks.apply_result = [window = QPointer<MainWindow>(this), active_id,
                                bounds](PixelBuffer result) {
    if (window == nullptr) {
      return;
    }
    if (auto* preview_layer = window->document().find_layer(active_id); preview_layer != nullptr) {
      set_layer_pixels_preserving_origin(*preview_layer, std::move(result), bounds);
      if (window->canvas_ != nullptr) {
        window->canvas_->document_changed(to_qrect(bounds));
      }
    }
  };
  preview_hooks.preview_render_active = [window = QPointer<MainWindow>(this)](bool active) {
    if (window != nullptr && window->canvas_ != nullptr) {
      if (active) {
        window->canvas_->begin_preview_render();
      } else {
        window->canvas_->end_preview_render();
      }
    }
  };
  auto preview_state = make_destructive_adjustment_preview_state(std::move(preview_hooks));
  const DirectAdjustmentPreview preview_changed = [preview_state, bounds, selection](
                                                      bool enabled, const AdjustmentSettings& settings) {
    const auto identity = !enabled || !adjustment_has_effect(settings);
    DestructiveAdjustmentPreviewRequest preview_request;
    preview_request.identity = identity;
    if (!identity) {
      preview_request.render = [bounds, selection, settings](PixelBuffer& pixels) {
        apply_adjustment_settings_to_pixels(pixels, bounds, selection, settings, nullptr);
      };
    }
    enqueue_async_pixel_preview(preview_state, std::move(preview_request), identity);
  };

  auto preview_edit_lock = lock_preview_dialog_edits();
  auto preview_cleanup = qScopeGuard([this, &doc, preview_state, original = *layer] {
    close_async_pixel_preview(preview_state);
    if (auto* target = doc.find_layer(original.id()); target != nullptr) {
      *target = original;
      canvas_->document_changed();
    }
  });
  const auto settings = request(preview_changed, *original_pixels);
  close_async_pixel_preview(preview_state);
  layer = doc.find_layer(active_id);
  if (layer == nullptr) {
    return;
  }
  set_layer_pixels_preserving_origin(*layer, *original_pixels, bounds);
  canvas_->document_changed(to_qrect(bounds));
  preview_cleanup.dismiss();
  preview_edit_lock.release();
  if (!settings.has_value()) {
    statusBar()->showMessage(tr("Cancelled %1").arg(display_name));
    return;
  }

  auto final_pixels = *original_pixels;
  if (adjustment_has_effect(*settings)) {
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
    try {
      run_filter_compute_with_progress(
          progress,
          [display_name](const QString& detail) { return tr("Applying %1...\n%2").arg(display_name, detail); },
          [this] {
            if (canvas_ != nullptr) {
              canvas_->tick_processing_operation();
            }
          },
          [&](FilterProgress& filter_progress) {
            apply_adjustment_settings_to_pixels(final_pixels, bounds, selection, *settings, &filter_progress);
          });
      progress.setValue(100);
    } catch (const FilterCancelled&) {
      layer = doc.find_layer(active_id);
      if (layer != nullptr) {
        set_layer_pixels_preserving_origin(*layer, *original_pixels, bounds);
        canvas_->document_changed(to_qrect(bounds));
      }
      statusBar()->showMessage(tr("Cancelled %1").arg(display_name));
      return;
    }
  }
  if (pixel_buffers_equal(final_pixels, *original_pixels)) {
    statusBar()->showMessage(tr("%1 made no changes").arg(display_name));
    return;
  }
  push_undo_snapshot(display_name);
  layer = doc.find_layer(active_id);
  if (layer == nullptr) {
    return;
  }
  set_layer_pixels_preserving_origin(*layer, std::move(final_pixels), bounds);
  canvas_->document_changed(to_qrect(bounds));
  statusBar()->showMessage(tr("Applied %1").arg(display_name));
}

namespace {

// Wraps one kind's settings as the AdjustmentSettings its adjustment layer would hold.
template <typename Settings>
std::optional<AdjustmentSettings> wrap_adjustment(AdjustmentKind kind, Settings AdjustmentSettings::*member,
                                                  const std::optional<Settings>& settings) {
  if (!settings.has_value()) {
    return std::nullopt;
  }
  AdjustmentSettings adjustment;
  adjustment.kind = kind;
  adjustment.*member = *settings;
  return adjustment;
}

template <typename Settings>
std::function<void(bool, const Settings&)> wrap_adjustment_preview(
    AdjustmentKind kind, Settings AdjustmentSettings::*member,
    const std::function<void(bool, const AdjustmentSettings&)>& preview) {
  return [kind, member, preview](bool enabled, const Settings& settings) {
    AdjustmentSettings adjustment;
    adjustment.kind = kind;
    adjustment.*member = settings;
    preview(enabled, adjustment);
  };
}

RgbColor direct_adjustment_rgb_of(const QColor& color) {
  return RgbColor{static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()),
                  static_cast<std::uint8_t>(color.blue())};
}

}  // namespace

void MainWindow::black_white_dialog() {
  direct_adjustment_dialog(tr("Black & White"), [this](const DirectAdjustmentPreview& preview,
                                                       const PixelBuffer& source) {
    constexpr auto kind = AdjustmentKind::BlackWhite;
    constexpr auto member = &AdjustmentSettings::black_white;
    return wrap_adjustment(
        kind, member,
        request_black_white_settings(this, wrap_adjustment_preview(kind, member, preview), {}, &source));
  });
}

void MainWindow::vibrance_dialog() {
  direct_adjustment_dialog(tr("Vibrance"), [this](const DirectAdjustmentPreview& preview, const PixelBuffer&) {
    constexpr auto kind = AdjustmentKind::Vibrance;
    constexpr auto member = &AdjustmentSettings::vibrance;
    return wrap_adjustment(kind, member,
                           request_vibrance_settings(this, wrap_adjustment_preview(kind, member, preview)));
  });
}

void MainWindow::exposure_dialog() {
  direct_adjustment_dialog(tr("Exposure"), [this](const DirectAdjustmentPreview& preview, const PixelBuffer&) {
    // Clamped as the adjustment layer clamps it (new_exposure_adjustment_layer).
    const auto clamped_preview = [preview](bool enabled, const ExposureSettings& exposure) {
      AdjustmentSettings adjustment;
      adjustment.kind = AdjustmentKind::Exposure;
      adjustment.exposure = clamp_exposure(exposure);
      preview(enabled, adjustment);
    };
    const auto settings = request_exposure_settings(this, clamped_preview);
    if (!settings.has_value()) {
      return std::optional<AdjustmentSettings>{};
    }
    return wrap_adjustment(AdjustmentKind::Exposure, &AdjustmentSettings::exposure,
                           std::optional<ExposureSettings>(clamp_exposure(*settings)));
  });
}

void MainWindow::channel_mixer_dialog() {
  direct_adjustment_dialog(tr("Channel Mixer"), [this](const DirectAdjustmentPreview& preview, const PixelBuffer&) {
    constexpr auto kind = AdjustmentKind::ChannelMixer;
    constexpr auto member = &AdjustmentSettings::channel_mixer;
    return wrap_adjustment(kind, member,
                           request_channel_mixer_settings(this, wrap_adjustment_preview(kind, member, preview)));
  });
}

void MainWindow::photo_filter_dialog() {
  direct_adjustment_dialog(tr("Photo Filter"), [this](const DirectAdjustmentPreview& preview, const PixelBuffer&) {
    constexpr auto kind = AdjustmentKind::PhotoFilter;
    constexpr auto member = &AdjustmentSettings::photo_filter;
    return wrap_adjustment(kind, member,
                           request_photo_filter_settings(this, wrap_adjustment_preview(kind, member, preview)));
  });
}

void MainWindow::gradient_map_dialog() {
  direct_adjustment_dialog(tr("Gradient Map"), [this](const DirectAdjustmentPreview& preview, const PixelBuffer&) {
    constexpr auto kind = AdjustmentKind::GradientMap;
    constexpr auto member = &AdjustmentSettings::gradient_map;
    return wrap_adjustment(
        kind, member,
        request_gradient_map_settings(this, wrap_adjustment_preview(kind, member, preview), {}, &gradient_library(),
                                      direct_adjustment_rgb_of(canvas_->primary_color()),
                                      direct_adjustment_rgb_of(canvas_->secondary_color())));
  });
}

void MainWindow::selective_color_dialog() {
  direct_adjustment_dialog(tr("Selective Color"), [this](const DirectAdjustmentPreview& preview, const PixelBuffer&) {
    constexpr auto kind = AdjustmentKind::SelectiveColor;
    constexpr auto member = &AdjustmentSettings::selective_color;
    return wrap_adjustment(kind, member,
                           request_selective_color_settings(this, wrap_adjustment_preview(kind, member, preview)));
  });
}

void MainWindow::color_lookup_dialog() {
  direct_adjustment_dialog(tr("Color Lookup"), [this](const DirectAdjustmentPreview& preview, const PixelBuffer&) {
    constexpr auto kind = AdjustmentKind::ColorLookup;
    constexpr auto member = &AdjustmentSettings::color_lookup;
    return wrap_adjustment(kind, member,
                           request_color_lookup_settings(this, wrap_adjustment_preview(kind, member, preview)));
  });
}

// Filter > Camera Raw Filter (GitHub issue 392; docs/camera-raw.md): the dialog previews in
// its own window and returns the filtered layer, applied as one undo step.
void MainWindow::camera_raw_filter_dialog() {
  if (!has_active_document()) {
    return;
  }
  auto& doc = document();
  select_only_layer_if_none_active();
  const auto active = doc.active_layer_id();
  if (!active.has_value()) {
    return;
  }
  auto* layer = doc.find_layer(*active);
  if (!editable_rgb_layer_any_depth(layer)) {
    show_status_error(tr("Select an editable RGB pixel layer"));
    return;
  }
  if (layer_is_smart_object(*layer)) {
    show_status_error(tr(
        "Rasterize the Smart Object before applying destructive filters or adjustments"));
    return;
  }
  if (layer_id_locks_image_pixels(*active)) {
    show_status_error(tr("Layer pixels are locked."));
    return;
  }
  if (!prompt_rasterize_procedural_layer(*active, tr("Camera Raw Filter"), false)) {
    return;
  }
  layer = doc.find_layer(*active);
  if (!editable_rgb_layer_any_depth(layer)) {
    show_status_error(tr("Select an editable RGB pixel layer"));
    return;
  }
  const auto active_id = *active;
  CameraRawFilterRequest request;
  request.source = std::make_shared<const PixelBuffer>(std::as_const(*layer).pixels());
  request.bounds = layer->bounds();
  request.selection = filter_selection(*canvas_);
  auto preview_edit_lock = lock_preview_dialog_edits();
  const auto outcome = run_camera_raw_filter_dialog(this, request);
  preview_edit_lock.release();
  if (!outcome.has_value()) {
    statusBar()->showMessage(tr("Cancelled %1").arg(tr("Camera Raw Filter")));
    return;
  }
  layer = doc.find_layer(active_id);
  if (layer == nullptr || outcome->pixels == nullptr) {
    return;
  }
  push_undo_snapshot(tr("Camera Raw Filter"));
  layer = doc.find_layer(active_id);
  if (layer == nullptr) {
    return;
  }
  set_layer_pixels_preserving_origin(*layer, *outcome->pixels, request.bounds);
  canvas_->document_changed(to_qrect(request.bounds));
  statusBar()->showMessage(tr("Applied %1").arg(tr("Camera Raw Filter")));
}

}  // namespace patchy::ui
