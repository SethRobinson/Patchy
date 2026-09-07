#pragma once
#include "core/vector_shape.hpp"
#include <QPointF>
#include <QRegion>
#include <vector>

namespace patchy::ui {
// Shared by native path commands and scripts. No UI targeting or history side effects.
std::vector<std::vector<QPointF>> stroke_polylines_for_path(const VectorPath& path);
PixelBuffer path_selection_coverage(const VectorPath& path, int width, int height);
PixelBuffer make_path_selection_coverage(const VectorPath& path, int width, int height,
    double feather, bool antialias, int operation_index, const PixelBuffer* existing);
VectorPath fit_selection_vector_path(const QRegion& region, double tolerance);
void bake_vector_mask(Layer& layer, int width, int height);
}  // namespace patchy::ui
