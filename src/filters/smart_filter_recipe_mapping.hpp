#pragma once

#include "core/smart_filter.hpp"
#include "filters/filter_registry.hpp"

#include <optional>
#include <vector>

namespace patchy {

// Maps a destructive-gallery recipe to the verified native Smart Filter
// subset. The mapping is all-or-nothing: one unknown or Patchy-only entry
// rejects the complete recipe instead of partially changing its meaning.
[[nodiscard]] std::optional<std::vector<SmartFilterEntry>>
smart_filter_entries_from_recipe(const FilterRecipe& recipe,
                                 const FilterRegistry& registry);

}  // namespace patchy
