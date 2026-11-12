// Select > Color Range... (GitHub issue 709; docs/color-range.md): the command behind the
// dialog. The canvas takes the sampling clicks through its dialog-owned read gesture, one
// sample per press; the mask shows only in the dialog, and OK commits one selection step.
#include "ui/main_window.hpp"

#include "core/color_range.hpp"
#include "ui/app_settings.hpp"
#include "ui/canvas_types.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/color_range_dialog.hpp"
#include "ui/tool_cursors.hpp"

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QPointer>
#include <QRect>
#include <QScopeGuard>
#include <QStatusBar>
#include <QString>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <utility>

namespace patchy::ui {

void MainWindow::color_range_dialog() {
  if (canvas_ == nullptr || !has_active_document() || preview_dialog_edit_locked() ||
      canvas_->quick_mask_active()) {
    return;
  }
  if (canvas_->pointer_gesture_active()) {
    show_status_error(tr("Finish the current drag before using Color Range"));
    return;
  }
  auto& doc = document();
  auto settings = app_settings();

  ColorRangeSettings initial;
  initial.range.fuzziness =
      std::clamp(settings.value(QString::fromLatin1(kColorRangeFuzzinessKey), kDefaultColorRangeFuzziness).toInt(),
                 kMinColorRangeFuzziness, kMaxColorRangeFuzziness);
  initial.sample_all_layers = settings.value(QString::fromLatin1(kColorRangeSampleAllLayersKey), true).toBool();
  // The foreground color is the first sample, so the preview shows something at once and a
  // color picked with the Eyedropper before the command carries over.
  const auto foreground = canvas_->primary_color();
  initial.range.samples = {ColorRangeSample{static_cast<std::uint8_t>(foreground.red()),
                                            static_cast<std::uint8_t>(foreground.green()),
                                            static_cast<std::uint8_t>(foreground.blue()), false}};

  const QPointer<CanvasWidget> canvas(canvas_);
  ColorRangeDialogHooks hooks;
  const auto active = std::as_const(doc).active_layer_id();
  const auto* active_layer = active.has_value() ? std::as_const(doc).find_layer(*active) : nullptr;
  hooks.layer_source_available = active_layer != nullptr && active_layer->visible();
  hooks.source_image = [canvas](bool sample_all_layers) {
    QString error;
    return canvas != nullptr ? canvas->automatic_selection_source(sample_all_layers, &error) : QImage();
  };
  hooks.set_canvas_picker = [canvas](std::function<void(QPoint, Qt::KeyboardModifiers)> pick,
                                     std::function<void()> dismiss) {
    if (canvas == nullptr) {
      return;
    }
    if (!pick) {
      canvas->clear_transient_read_interaction();
      return;
    }
    // One sample per press. A drag adds nothing and nothing is drawn on the canvas.
    // Escape on the canvas closes the dialog, as it does for the Curves eyedroppers.
    canvas->set_transient_read_interaction(
        [pick = std::move(pick), dismiss = std::move(dismiss)](const CanvasReadGesture& gesture) {
          if (gesture.phase == CanvasReadPhase::Press) {
            pick(gesture.document_position, gesture.modifiers);
          } else if (gesture.phase == CanvasReadPhase::Dismiss && dismiss) {
            dismiss();
          }
        },
        eyedropper_cursor());
  };

  // The document stays as it is while the dialog holds its picture: zoom and pan only.
  auto edit_lock = lock_preview_dialog_edits();
  const auto chosen = request_color_range_settings(this, initial, hooks);
  edit_lock.release();
  if (!chosen.has_value() || canvas_ == nullptr || canvas_ != canvas || !has_active_document() ||
      &document() != &doc) {
    return;
  }
  settings.setValue(QString::fromLatin1(kColorRangeFuzzinessKey), chosen->range.fuzziness);
  settings.setValue(QString::fromLatin1(kColorRangeSampleAllLayersKey), chosen->sample_all_layers);

  canvas_->begin_processing_operation();
  const auto finish_processing = qScopeGuard([this] {
    if (canvas_ != nullptr) {
      canvas_->end_processing_operation();
    }
  });
  QString error;
  const auto source = canvas_->automatic_selection_source(chosen->sample_all_layers, &error);
  if (source.isNull()) {
    show_status_error(error.isEmpty() ? tr("Color Range found nothing to sample") : error);
    return;
  }
  auto mask = color_range_mask_image(source, chosen->range, [this] { canvas_->tick_processing_operation(); });
  const auto bounds = color_range_mask_bounds(mask);
  canvas_->apply_selection_mask(bounds, bounds.isEmpty() ? QImage() : mask.copy(bounds), chosen->result,
                                tr("Color Range"));
  statusBar()->showMessage(bounds.isEmpty() ? tr("Color Range found no matching pixels")
                                            : tr("Selected a color range"));
}

}  // namespace patchy::ui
