#pragma once

#include "core/smart_filter.hpp"
#include "psd/psd_descriptor.hpp"

#include <cstdint>

// Photoshop's Filter Gallery Smart Filter entry (docs/smart-filters-native.md, "Filter
// Gallery"): class and filterID 'GEfc'. One effect sits inline (GEfk enum GEft plus that
// effect's settings); two or more sit in a GEfs list of GEfc objects. Only effects Patchy
// renders are modeled (Plastic Wrap); any other effect, key or shape keeps the entry
// unmodeled and its stack on Photoshop's preview.
namespace patchy::psd {

inline constexpr std::uint32_t kFilterGalleryFilterId = 0x47456663U;

// Reads `filter` when it is a GEfc object. Returns false for any other class (the caller
// tries other filters); true otherwise, with `entry` set to the FilterGallery kind or
// `entry_supported` cleared.
bool parse_filter_gallery_smart_filter(const DescriptorObject* filter, SmartFilterEntry& entry,
                                       bool& entry_supported);

// Writes edited effect settings into a GEfc object in place. False when `gallery` does not
// have the stored object's effects in the same form and order (Patchy never changes them).
bool write_filter_gallery_smart_filter(DescriptorObject& filter, const FilterGallerySmartFilter& gallery);

}  // namespace patchy::psd
