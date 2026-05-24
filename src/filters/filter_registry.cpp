#include "filters/filter_registry.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace photoslop {

void FilterRegistry::register_filter(FilterDefinition filter) {
  if (filter.identifier.empty()) {
    throw std::invalid_argument("Filter identifier cannot be empty");
  }
  if (!filter.apply) {
    throw std::invalid_argument("Filter implementation cannot be empty");
  }
  if (find(filter.identifier) != nullptr) {
    throw std::invalid_argument("Filter identifier is already registered");
  }
  filters_.push_back(std::move(filter));
}

const FilterDefinition* FilterRegistry::find(std::string_view identifier) const noexcept {
  const auto found = std::find_if(filters_.begin(), filters_.end(), [identifier](const FilterDefinition& filter) {
    return filter.identifier == identifier;
  });
  return found == filters_.end() ? nullptr : &*found;
}

const std::vector<FilterDefinition>& FilterRegistry::filters() const noexcept {
  return filters_;
}

void FilterRegistry::apply(std::string_view identifier, PixelBuffer& pixels) const {
  const auto* filter = find(identifier);
  if (filter == nullptr) {
    throw std::invalid_argument("Unknown filter identifier");
  }
  filter->apply(pixels);
}

}  // namespace photoslop
