#pragma once

#include <QPainterPath>
#include <QPointF>
#include <QPolygonF>
#include <QRect>
#include <QRectF>
#include <QRegion>

#include <vector>

namespace patchy::ui {

// One closed boundary loop of a selection, in pixel-corner coordinates of
// whatever space it was traced in (pixel (x, y) spans corners
// (x, y)..(x+1, y+1), so a loop around the single pixel (3, 4) is
// {(3,4), (4,4), (4,5), (3,5)}). The polygon is implicitly closed: the first
// point is not repeated at the end. Outer boundaries wind clockwise in Qt's
// y-down coordinates, holes counterclockwise. `bounds` is the loop's pixel
// bounding box (whose corner extent is [left, left+width] x [top, top+height]).
struct OutlineLoop {
  QPolygonF points;
  QRect bounds;
};

// Traces the boundary of `region` into closed loops (outer contours and hole
// contours) in document space. Diagonally-touching selected pixels are kept
// 4-connected: they produce separate loops that share a corner without
// crossing. Output is deterministic: loops appear in row-major order of their
// topmost boundary edge, each rotated to start at its topmost-then-leftmost
// vertex, and collinear runs are collapsed.
[[nodiscard]] std::vector<OutlineLoop> trace_selection_outlines(const QRegion& region);

// Traces the selection as it is actually resolved on screen below 100% zoom:
// the region is rasterised with antialiased coverage at device resolution
// (clipped to `device_viewport`), thresholded at 50%, and traced. Sub-pixel
// staircase steps, holes, and islands merge or vanish exactly as they do in
// the scaled-down artwork — the Photoshop look — instead of strobing as
// hundreds of sub-pixel loops. Loops are in device coordinates (pan and zoom
// already applied). If the whole visible selection resolves below the
// threshold, its >= 1x1 px device bounding rect is emitted instead so a tiny
// selection never silently disappears.
[[nodiscard]] std::vector<OutlineLoop> trace_device_selection_outlines(const QRegion& region, double zoom,
                                                                       QPointF pan,
                                                                       const QRectF& device_viewport);

// Stroke-ready device-space paths for the marching ants overlay, split by
// loop size: `marching` holds loops long enough for the standard 4-on/4-off
// dash pattern; `pinpoint` holds loops shorter than one dash period (single
// pixels at 100% zoom, dust at far zoom-out), which would spend entire
// animation phases fully covered by the white dash and blink — they are
// drawn with a finer 2-2 pattern instead so a dark edge always remains.
struct SelectionOutlineScreenPaths {
  QPainterPath marching;
  QPainterPath pinpoint;
};

// Transforms loops by device = pan + point * zoom, culls loops entirely
// outside `device_viewport`, and closes every subpath so the dash pattern
// flows continuously around each contour. Pass zoom = 1 and pan = (0, 0) for
// loops already in device coordinates (trace_device_selection_outlines).
[[nodiscard]] SelectionOutlineScreenPaths build_selection_outline_screen_paths(
    const std::vector<OutlineLoop>& loops, double zoom, QPointF pan, const QRectF& device_viewport);

}  // namespace patchy::ui
