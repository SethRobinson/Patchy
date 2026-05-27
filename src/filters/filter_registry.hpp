#pragma once

#include "core/pixel_buffer.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace patchy {

using PixelFilterFn = std::function<void(PixelBuffer&)>;

struct FilterDefinition {
  std::string identifier;
  std::string display_name;
  PixelFilterFn apply;
};

class FilterRegistry {
public:
  void register_filter(FilterDefinition filter);
  [[nodiscard]] const FilterDefinition* find(std::string_view identifier) const noexcept;
  [[nodiscard]] const std::vector<FilterDefinition>& filters() const noexcept;
  void apply(std::string_view identifier, PixelBuffer& pixels) const;

private:
  std::vector<FilterDefinition> filters_;
};

void register_builtin_filters(FilterRegistry& registry);

}  // namespace patchy
