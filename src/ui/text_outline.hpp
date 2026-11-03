#pragma once
// Text as vector outlines (GitHub issue 598; docs/text-tool.md, "Convert to Shape"): a paint
// device that keeps what the text renderer draws as outline paths instead of pixels, and the
// conversion of those outlines into Patchy path subpaths and shape-layer content.
#include "core/pattern_resource.hpp"
#include "core/vector_shape.hpp"

#include <QColor>
#include <QPaintDevice>
#include <QPainterPath>
#include <QPointF>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace patchy::ui {

// One fill the renderer drew: a nonzero-winding outline in device coordinates and its colour.
struct TextOutlinePaint {
  QPainterPath path;
  QColor color;
};

class OutlineRecordingEngine;

// A paint device whose engine records every fill as an outline path. Glyphs reach it through
// QPaintEngine's own outline fallback for engines without glyph caches (the font engine's
// glyph outlines, at the positions the layout gave them), rectangles are the underline and
// strikethrough bars, and a stroked path (faux bold's pen) is kept as the stroke's outline.
// Images (colour emoji glyphs) have no outline and are only counted. Clipping is ignored.
class OutlineRecordingDevice final : public QPaintDevice {
 public:
  OutlineRecordingDevice();
  ~OutlineRecordingDevice() override;
  OutlineRecordingDevice(const OutlineRecordingDevice&) = delete;
  OutlineRecordingDevice& operator=(const OutlineRecordingDevice&) = delete;

  [[nodiscard]] QPaintEngine* paintEngine() const override;
  [[nodiscard]] const std::vector<TextOutlinePaint>& paints() const;
  [[nodiscard]] int skipped_images() const;

 protected:
  [[nodiscard]] int metric(PaintDeviceMetric metric) const override;

 private:
  std::unique_ptr<OutlineRecordingEngine> engine_;
};

// Maps every point of `path` through a (possibly nonlinear) map, such as a Warp Text surface.
// Each line and curve is split into pieces no longer than about `max_piece_length` units; each
// piece's ends are mapped exactly and its handles follow the map's local derivative, so the
// result keeps the warped shape as a smooth path rather than a polygon.
[[nodiscard]] QPainterPath map_outline_path(const QPainterPath& path, const std::function<QPointF(QPointF)>& map,
                                            double max_piece_length = 8.0);

// Appends the closed contours of a nonzero-winding outline to `target` as shape groups that
// fill the same area under Patchy's rule (even-odd within a group, groups united): contours
// that nest share their outermost contour's group, a contour that does not change the nonzero
// fill (a solid contour inside another) is dropped, and crossing contours (a variable font's
// overlapping strokes) become separate united groups. Group numbers continue from
// target.next_shape_group(); the new groups are appended to `groups` when it is not null.
void append_outline_subpaths(const QPainterPath& path, VectorPath& target, std::vector<std::int32_t>* groups);

// All the outlines as one path (Create Work Path).
[[nodiscard]] VectorPath vector_path_from_text_outlines(const std::vector<TextOutlinePaint>& paints);

// A shape layer's content for the outlines: a solid fill in the text colour, no stroke. Text in
// several colours keeps them as paint parts (one per colour, in the order the colours first
// appear), the representation merged shape layers use. nullopt when no outline has an area.
[[nodiscard]] std::optional<VectorShapeContent> shape_content_from_text_outlines(
    const std::vector<TextOutlinePaint>& paints);

// Turns a text layer into a shape layer in place: the text keys go (the rasterize rule,
// clear_layer_text_metadata), the shape content and markers arrive, and the pixels are baked
// from the path. Everything else the layer carries stays: id, name, opacity, Fill, blend mode,
// visibility, locks, clipping, masks and layer style.
void convert_text_layer_to_shape(Layer& layer, VectorShapeContent content, Rect canvas, const PatternStore* patterns);

}  // namespace patchy::ui
