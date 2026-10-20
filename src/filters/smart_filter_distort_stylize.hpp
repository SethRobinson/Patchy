#pragma once

#include "core/smart_filter.hpp"
#include "filters/filter_registry.hpp"

namespace patchy {

// Renders one table-described native Smart Filter (core/smart_filter_schema.hpp;
// calibration record in docs/smart-filters-distort-stylize.md) over `current`,
// the stack result so far in document space. `filter_canvas_bounds` is the
// document-space FEid canvas the stack runs in. Throws std::invalid_argument
// for a kind without a schema or out-of-range settings, FilterCancelled when
// `progress` cancels.
[[nodiscard]] FilterRenderResult render_schema_smart_filter(
    const FilterRenderResult& current, Rect filter_canvas_bounds, SmartFilterKind kind,
    const SchemaSmartFilter& settings, const FilterProgress* progress = nullptr);

}  // namespace patchy
