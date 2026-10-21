#pragma once

#include "filters/filter_registry.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace patchy {

// Built-in Looks are authored against an image whose longer side is this many
// pixels; loading one scales its pixel-distance parameters to the target.
inline constexpr std::int32_t kBuiltinLookReferenceLongEdge = 4000;

// A read-only Look shipped with Patchy (docs/builtin-looks.md).
// The id is a persisted contract: never renamed, reused or removed once
// shipped. Name and description are English translation sources in the
// "QObject" context.
struct BuiltinFilterLook {
  std::string_view id;
  const char* name;
  const char* description;
  FilterRecipe recipe;  // normalized, at the reference long edge
};

// Every built-in Look in display order.
[[nodiscard]] const std::vector<BuiltinFilterLook>& builtin_filter_looks();
[[nodiscard]] const BuiltinFilterLook* find_builtin_filter_look(std::string_view id);

// The Look's recipe with pixel distances scaled to a width x height image
// (FilterRegistry::scale); nullopt when the registry cannot run it.
[[nodiscard]] std::optional<FilterRecipe> builtin_look_recipe_for_size(
    const FilterRegistry& registry, const BuiltinFilterLook& look,
    std::int32_t width, std::int32_t height);

}  // namespace patchy
