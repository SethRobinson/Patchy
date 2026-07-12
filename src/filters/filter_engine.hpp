#pragma once

#include "filters/filter_registry.hpp"

#include <string_view>

namespace patchy {

// Returns the built-in catalog entry for identifier, or an empty metadata
// record when identifier is not a built-in filter.
[[nodiscard]] FilterCatalogMetadata
builtin_filter_catalog(std::string_view identifier);

} // namespace patchy
