#include "core/smart_filter.hpp"

#include <utility>

namespace patchy {

const PlasticWrapSmartFilter* editable_plastic_wrap(const SmartFilterEntry& entry) noexcept {
  if (entry.kind == SmartFilterKind::PlasticWrap) {
    return std::get_if<PlasticWrapSmartFilter>(&entry.parameters);
  }
  if (entry.kind == SmartFilterKind::FilterGallery) {
    const auto* gallery = std::get_if<FilterGallerySmartFilter>(&entry.parameters);
    if (gallery != nullptr && gallery->effects.size() == 1U) {
      return std::get_if<PlasticWrapSmartFilter>(&gallery->effects.front().settings);
    }
  }
  return nullptr;
}

PlasticWrapSmartFilter* editable_plastic_wrap(SmartFilterEntry& entry) noexcept {
  return const_cast<PlasticWrapSmartFilter*>(editable_plastic_wrap(std::as_const(entry)));
}

SmartFilterKind smart_filter_dialog_kind(const SmartFilterEntry& entry) noexcept {
  if (entry.kind == SmartFilterKind::FilterGallery && editable_plastic_wrap(entry) != nullptr) {
    return SmartFilterKind::PlasticWrap;
  }
  return entry.kind;
}

}  // namespace patchy
