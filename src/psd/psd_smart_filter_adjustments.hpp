#pragma once

#include "core/smart_filter.hpp"
#include "psd/psd_descriptor.hpp"

#include <cstdint>
#include <optional>

// Image > Adjustments commands stored as native Smart Filters
// (docs/smart-filters-adjustments.md). Photoshop keeps the applying action descriptor
// verbatim as the entry's Fltr object, so these read Photoshop's action forms (Lvls,
// Crvs, HStr, BrgC, ClrB, ChnM, Exps, Pstr, Thrs, photoFilter, BanW) into the
// adjustment-layer model, failing closed on any key they do not model.
namespace patchy::psd {

// The identity Photoshop gives a filterFX entry for one modeled adjustment kind.
struct AdjustmentSmartFilterIds {
  const char* class_id;
  bool class_id_long_form;
  const char* filter_name;  // the Fltr object's name
  const char* entry_name;   // filterFX 'Nm  '
  // filterFX 'filterID': the class's charID as an integer; 0 for a stringID class,
  // whose number Photoshop assigns at run time (Photo Filter stored 33).
  std::uint32_t filter_id;
};
[[nodiscard]] std::optional<AdjustmentSmartFilterIds> adjustment_smart_filter_ids(
    AdjustmentKind kind) noexcept;

// Reads `filter` when it is an adjustment command's Fltr object. Returns false when it
// is not one (the caller tries other filters); true when it is, with `entry` set to the
// Adjustment kind, or `entry_supported` cleared when a key or value is outside the
// modeled subset.
bool parse_adjustment_smart_filter(const DescriptorObject* filter,
                                   SmartFilterEntry& entry, bool& entry_supported);

// Writes `settings` into an adjustment command's Fltr object. Leaves the object byte for
// byte as it is when it already holds those settings; otherwise rewrites its values in
// Photoshop's dialog form, keeping the class, name and presetKind. False when the
// settings cannot be expressed (an edited per-hue band, a CMYK record).
bool write_adjustment_smart_filter(DescriptorObject& filter,
                                   const AdjustmentSettings& settings);

}  // namespace patchy::psd
