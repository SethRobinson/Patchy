// Image > Adjustments stored as native Smart Filters (docs/smart-filters-adjustments.md):
// the entry's settings open the adjustment's own dialog, preview the re-rendered stack
// live, and commit as one undo step through commit_smart_filter_stack_edit.

#include "core/smart_filter.hpp"
#include "core/smart_object.hpp"
#include "filters/smart_filter_renderer.hpp"
#include "psd/psd_filter_effects.hpp"
#include "ui/background_workers.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/main_window.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/qt_geometry.hpp"
#include "ui/smart_object_render.hpp"

#include <QCoreApplication>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QScopeGuard>
#include <QStatusBar>
#include <QString>

#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <variant>

namespace patchy::ui {

void MainWindow::edit_adjustment_smart_filter(LayerId layer_id, std::size_t execution_index) {
  if (!has_active_document() || canvas_ == nullptr) {
    return;
  }
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
  if (layer_id_locks_image_pixels(layer_id)) {
    show_status_error(tr("Layer pixels are locked."));
    return;
  }
  const auto document_pixels =
      static_cast<std::uint64_t>(document().width()) * static_cast<std::uint64_t>(document().height());
  if (document_pixels > psd::kMaximumEditableSmartFilterMaskPixels) {
    show_status_error(tr("Editable Smart Filters currently support documents up to 64 megapixels"));
    return;
  }
  auto& doc = document();
  auto* layer = doc.find_layer(layer_id);
  const auto lock = layer != nullptr ? smart_object_lock_reason(*layer) : std::string{};
  const auto* current = layer != nullptr ? layer->smart_filter_stack() : nullptr;
  const auto* stored = current != nullptr && execution_index < current->entries.size()
                           ? std::get_if<AdjustmentSmartFilter>(&current->entries[execution_index].parameters)
                           : nullptr;
  if (layer == nullptr || !layer_is_smart_object(*layer) || (!lock.empty() && lock != "external") ||
      current->support != SmartFilterStackSupport::Supported || stored == nullptr) {
    show_status_error(tr("This Smart Filter can only be preserved, not edited"));
    return;
  }
  if (stored->settings.kind == AdjustmentKind::Invert) {
    statusBar()->showMessage(tr("Invert has no settings to edit"));
    return;
  }

  const auto stack = *current;
  const auto original_settings = stored->settings;
  const auto parent_document_dir =
      session().path.isEmpty() ? QString() : QFileInfo(session().path).absolutePath();
  const auto unfiltered_preview = render_smart_object_unfiltered_layer_preview(
      std::as_const(doc), std::as_const(*layer), canvas_->transform_interpolation(), parent_document_dir);
  if (!unfiltered_preview.has_value()) {
    show_status_error(tr("Could not render this Smart Object"));
    return;
  }
  auto unfiltered_pixels = std::make_shared<const PixelBuffer>(unfiltered_preview->pixels);
  const auto unfiltered_bounds = unfiltered_preview->bounds;
  const auto filter_canvas_bounds = Rect::from_size(doc.width(), doc.height());
  auto original_pixels = std::make_shared<const PixelBuffer>(std::as_const(*layer).pixels());
  const auto original_bounds = layer->bounds();
  auto last_preview_bounds = std::make_shared<Rect>(original_bounds);

  const auto stack_with = [stack, execution_index](const AdjustmentSettings& settings) {
    auto candidate = stack;
    candidate.entries[execution_index].parameters = AdjustmentSmartFilter{settings};
    return candidate;
  };
  // Shows the layer's stored pixels again: the original settings may be Photoshop's own
  // preview of this stack, which a Patchy re-render of the same settings must not replace.
  const auto show_original = [this, layer_id, original_pixels, original_bounds, last_preview_bounds] {
    if (auto* preview_layer = document().find_layer(layer_id); preview_layer != nullptr) {
      preview_layer->set_pixels(*original_pixels);
      preview_layer->set_bounds(original_bounds);
      if (canvas_ != nullptr) {
        canvas_->document_changed(to_qrect(*last_preview_bounds).united(to_qrect(original_bounds)));
      }
      *last_preview_bounds = original_bounds;
    }
  };

  using Request = std::optional<AdjustmentSettings>;
  auto preview_state = std::make_shared<AsyncPixelPreviewState<Request>>();
  preview_state->start = [this, preview_state, layer_id, original_settings, stack_with, show_original,
                          unfiltered_pixels, unfiltered_bounds, filter_canvas_bounds,
                          last_preview_bounds](const Request& request) {
    if (!request.has_value() || AdjustmentSmartFilter{*request} == AdjustmentSmartFilter{original_settings}) {
      preview_state->pending.reset();
      ++preview_state->generation;
      show_original();
      return;
    }
    preview_state->in_flight = true;
    const auto generation = ++preview_state->generation;
    auto* app = QCoreApplication::instance();
    auto window = QPointer<MainWindow>(this);
    if (canvas_ != nullptr) {
      canvas_->begin_preview_render();
    }
    run_tracked_background_worker([app, window, preview_state, generation, layer_id, unfiltered_pixels,
                                   unfiltered_bounds, filter_canvas_bounds, last_preview_bounds,
                                   candidate = stack_with(*request)] {
      auto result = std::make_shared<FilterRenderResult>();
      auto error = std::make_shared<QString>();
      try {
        *result = render_smart_filter_stack(*unfiltered_pixels, unfiltered_bounds, filter_canvas_bounds,
                                            candidate);
      } catch (const std::exception& caught) {
        *error = QString::fromUtf8(caught.what());
      }
      if (app == nullptr) {
        return;
      }
      QMetaObject::invokeMethod(
          app,
          [window, preview_state, generation, layer_id, last_preview_bounds, result, error]() mutable {
            preview_state->in_flight = false;
            if (window != nullptr && window->canvas_ != nullptr) {
              window->canvas_->end_preview_render();
            }
            if (!preview_state->closed && !preview_state->pending.has_value() &&
                generation == preview_state->generation && window != nullptr) {
              if (error->isEmpty()) {
                if (auto* preview_layer = window->document().find_layer(layer_id); preview_layer != nullptr) {
                  preview_layer->set_pixels(std::move(result->pixels));
                  preview_layer->set_bounds(result->bounds);
                  if (window->canvas_ != nullptr) {
                    window->canvas_->document_changed(
                        to_qrect(*last_preview_bounds).united(to_qrect(result->bounds)));
                  }
                  *last_preview_bounds = result->bounds;
                }
              } else {
                window->show_status_error(MainWindow::tr("Smart Filter preview failed: %1").arg(*error));
              }
            }
            if (!preview_state->closed && preview_state->pending.has_value() && preview_state->start) {
              auto next = *preview_state->pending;
              preview_state->pending.reset();
              preview_state->start(next);
            }
          },
          Qt::QueuedConnection);
    });
  };

  auto preview_edit_lock = lock_preview_dialog_edits();
  auto preview_cleanup = qScopeGuard([this, &doc, preview_state, original = *layer] {
    close_async_pixel_preview(preview_state);
    if (auto* target = doc.find_layer(original.id()); target != nullptr) {
      *target = original;
      canvas_->document_changed();
    }
  });
  const std::function<void(const Request&)> preview = [preview_state](const Request& request) {
    enqueue_async_pixel_preview(preview_state, request, !request.has_value());
  };
  const auto accepted = request_adjustment_settings(original_settings, preview);
  close_async_pixel_preview(preview_state);
  show_original();
  preview_cleanup.dismiss();
  preview_edit_lock.release();

  const auto name = localized_adjustment_display_name(original_settings.kind);
  if (!accepted.has_value() || AdjustmentSmartFilter{*accepted} == AdjustmentSmartFilter{original_settings}) {
    statusBar()->showMessage(accepted.has_value() ? tr("Updated %1 Smart Filter").arg(name)
                                                  : tr("Cancelled %1").arg(name));
    return;
  }
  // Same entries in the same order: the descriptor is patched in place.
  if (!commit_smart_filter_stack_edit(layer_id, stack_with(*accepted), {},
                                      tr("Edit %1 Smart Filter").arg(name),
                                      tr("Updated %1 Smart Filter").arg(name))) {
    show_status_error(tr("This Smart Filter descriptor cannot be edited safely"));
  }
}

}  // namespace patchy::ui
