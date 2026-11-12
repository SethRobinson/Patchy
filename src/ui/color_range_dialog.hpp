#pragma once

// Select > Color Range... (GitHub issue 709; docs/color-range.md): the dialog and the
// QImage side of the mask. MainWindow::color_range_dialog runs it and commits through
// CanvasWidget::apply_selection_mask.

#include "core/color_range.hpp"
#include "ui/canvas_types.hpp"

#include <QImage>
#include <QPoint>
#include <QRect>
#include <Qt>

#include <functional>
#include <optional>

class QWidget;

namespace patchy::ui {

struct ColorRangeSettings {
  ColorRangeOptions range;
  // How the result combines with the selection there is.
  SelectionMode result{SelectionMode::Replace};
  // The merged image (true) or the active layer alone, like the Magic Wand's option.
  bool sample_all_layers{true};
};

// Settings keys (permanent): the last accepted Fuzziness and Sample All Layers.
inline constexpr const char* kColorRangeFuzzinessKey = "tools/colorRangeFuzziness";
inline constexpr const char* kColorRangeSampleAllLayersKey = "tools/colorRangeSampleAllLayers";

struct ColorRangeDialogHooks {
  // The picture the range is taken from, RGBA8888 at document size: the merged image or the
  // active layer alone. Null when there is nothing to sample. Without the hook the dialog
  // previews nothing (the dialog sweep).
  std::function<QImage(bool sample_all_layers)> source_image;
  // Arms canvas clicks while the dialog is open (an empty `pick` disarms): each click
  // reports its document point and the modifiers held. `dismiss` runs when Escape is
  // pressed on the canvas, which is a sibling window whose keys never reach the dialog.
  std::function<void(std::function<void(QPoint, Qt::KeyboardModifiers)> pick, std::function<void()> dismiss)>
      set_canvas_picker;
  // False when the active layer has nothing to sample: Sample All Layers stays on.
  bool layer_source_available{true};
};

// The mask of an RGBA8888 image as a Grayscale8 image of the same size (the core
// classifier, row by row). `tick` runs once per row when set.
[[nodiscard]] QImage color_range_mask_image(const QImage& rgba8888, const ColorRangeOptions& options,
                                            const std::function<void()>& tick = {});

// The smallest rectangle holding every selected (non-zero) pixel of a Grayscale8 mask; empty
// when nothing is selected.
[[nodiscard]] QRect color_range_mask_bounds(const QImage& mask);

// The color a click at `point` samples: the mean of the 3 x 3 square around it (the Magic
// Wand's averaging). Nothing outside the image or on a clear pixel.
[[nodiscard]] std::optional<ColorRangeSample> color_range_sample_at(const QImage& rgba8888, QPoint point);

// Runs the dialog (object name patchyColorRangeDialog), non-modal so the canvas takes the
// sampling clicks. Returns the accepted settings, or nullopt on Cancel.
[[nodiscard]] std::optional<ColorRangeSettings> request_color_range_settings(QWidget* parent,
                                                                             const ColorRangeSettings& initial,
                                                                             const ColorRangeDialogHooks& hooks);

}  // namespace patchy::ui
