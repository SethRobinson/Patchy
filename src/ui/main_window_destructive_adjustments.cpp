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

}  // namespace patchy::ui
