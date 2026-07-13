#include "filters/smart_filter_recipe_mapping.hpp"

#include <cmath>
#include <cstdint>
#include <utility>

namespace patchy {

std::optional<std::vector<SmartFilterEntry>>
smart_filter_entries_from_recipe(const FilterRecipe& recipe,
                                 const FilterRegistry& registry) {
  if (recipe.entries.empty() || !registry.supports(recipe)) {
    return std::nullopt;
  }
  std::vector<SmartFilterEntry> mapped;
  mapped.reserve(recipe.entries.size());
  for (const auto& recipe_entry : recipe.entries) {
    const auto normalized = registry.normalize(recipe_entry.invocation);
    if (!normalized.has_value() ||
        normalized->filter_id != "patchy.filters.gaussian_blur" ||
        normalized->schema_version != 1U) {
      return std::nullopt;
    }
    const auto radius_value = normalized->parameters.find("radius");
    if (radius_value == normalized->parameters.end()) {
      return std::nullopt;
    }
    double radius = 0.0;
    if (const auto* integer =
            std::get_if<std::int64_t>(&radius_value->second);
        integer != nullptr) {
      radius = static_cast<double>(*integer);
    } else if (const auto* real = std::get_if<double>(&radius_value->second);
               real != nullptr) {
      radius = *real;
    } else {
      return std::nullopt;
    }
    if (!std::isfinite(radius) || radius < 0.1 || radius > 1000.0) {
      return std::nullopt;
    }

    SmartFilterEntry entry;
    entry.kind = SmartFilterKind::GaussianBlur;
    entry.native_name = "Gaussian Blur...";
    entry.native_class_id = "GsnB";
    entry.native_filter_id = 0x47736e42U;
    entry.enabled = recipe_entry.enabled;
    entry.has_options = true;
    entry.opacity = recipe_entry.opacity;
    entry.blend_mode = recipe_entry.blend_mode;
    entry.foreground = normalized->foreground;
    entry.background = normalized->background;
    entry.parameters = GaussianBlurSmartFilter{radius};
    mapped.push_back(std::move(entry));
  }
  return mapped;
}

}  // namespace patchy
