#pragma once

#include "core/smart_filter.hpp"
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

// Maps a destructive-gallery recipe to the verified native Smart Filter
// subset. The mapping is all-or-nothing: one unknown or Patchy-only entry
// rejects the complete recipe instead of partially changing its meaning.
[[nodiscard]] std::optional<std::vector<SmartFilterEntry>>
smart_filter_entries_from_recipe(const FilterRecipe& recipe,
                                 const FilterRegistry& registry);

}  // namespace patchy
