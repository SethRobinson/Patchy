#pragma once

#include "core/smart_filter.hpp"
#include "core/smart_filter_schema.hpp"
#include "core/smart_filter_table.hpp"
#include "filters/filter_registry.hpp"

#include <optional>
#include <string_view>
#include <vector>

namespace patchy {

// The single decision point for which built-in filter IDs have a verified
// native Photoshop Smart Filter mapping. UI routing queries these instead of
// duplicating the ID list; adding a calibrated filter means extending this
// table (and the value gates in smart_filter_entries_from_recipe), never a
// second list.
[[nodiscard]] std::optional<SmartFilterKind> native_smart_filter_kind_for(
    std::string_view filter_id);
[[nodiscard]] bool native_smart_filter_kind_supported(SmartFilterKind kind);

// The settings of a table-described native filter (core/smart_filter_schema.hpp,
// core/smart_filter_table.hpp) for a catalog invocation whose parameter keys are
// the fields' parameter keys: absent values take the field's default, numbers
// are clamped, hidden fields keep their defaults. The direct Filter menu
// commands that reuse those kernels (Solarize, Find Edges, Trace Contour,
// Diffuse, Fibers) render and map through these, so both agree.
[[nodiscard]] SchemaSmartFilter schema_smart_filter_from_invocation(
    const SmartFilterSchema& schema, const FilterInvocation& invocation);
[[nodiscard]] TableSmartFilter table_smart_filter_from_catalog_invocation(
    const SmartFilterTableSpec& spec, const FilterInvocation& invocation);

// Maps a destructive-gallery recipe to the verified native Smart Filter
// subset. The mapping is all-or-nothing: one unknown or Patchy-only entry
// rejects the complete recipe instead of partially changing its meaning.
[[nodiscard]] std::optional<std::vector<SmartFilterEntry>>
smart_filter_entries_from_recipe(const FilterRecipe& recipe,
                                 const FilterRegistry& registry);

}  // namespace patchy
