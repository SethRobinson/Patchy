#pragma once

#include "core/smart_filter.hpp"
#include "filters/filter_registry.hpp"

namespace patchy {

// Renders one table-described native Smart Filter entry
// (core/smart_filter_table.hpp). `canvas` is the stack's current result
// already embedded in the document-space FEid filter canvas; the result keeps
// those bounds (the stack alpha-trims afterwards). Calibration and the
// per-filter contracts: docs/smart-filters-pixelate-render.md. Invalid
// settings throw std::invalid_argument; cancellation throws FilterCancelled.
[[nodiscard]] FilterRenderResult render_table_smart_filter(
    const FilterRenderResult &canvas, const SmartFilterEntry &entry,
    const FilterProgress *progress = nullptr);

}  // namespace patchy
