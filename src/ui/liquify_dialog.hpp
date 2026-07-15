#pragma once

#include "core/liquify.hpp"
#include "core/layer.hpp"

#include <QRegion>

#include <optional>

class QWidget;

namespace patchy::ui {

// Runs a manual, proxy-backed Liquify workspace. The returned mesh is normalized
// and can be rendered against the original full-resolution layer.
[[nodiscard]] std::optional<LiquifyMesh> request_liquify(
    QWidget* parent, const PixelBuffer& source, Rect bounds,
    const QRegion& selection);

}  // namespace patchy::ui
