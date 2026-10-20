#pragma once

#include "core/smart_filter.hpp"
#include "psd/psd_descriptor.hpp"

#include <cstdint>

// Descriptor I/O for the table-described native Smart Filters
// (core/smart_filter_table.hpp, docs/smart-filters-pixelate-render.md).
namespace patchy::psd {

// Reads `filter` (an entry's Fltr object, null when the entry has none) when
// its class id and the entry's filterID name a table spec; a spec without a
// class id claims an entry WITHOUT a Fltr object by filterID alone. Returns
// false when no spec claims the entry, so the caller keeps looking. When one
// does, it returns true and, if every field is present with the right type and
// in range, sets entry.kind and entry.parameters; otherwise clears
// `entry_supported` (the entry then stays an unmodeled, preview-locking
// filter).
bool parse_table_smart_filter(const DescriptorObject *filter,
                              std::uint32_t filter_id, SmartFilterEntry &entry,
                              bool &entry_supported);

// Appends the spec's fields to a freshly authored Fltr object, in table order.
void append_table_smart_filter_fields(DescriptorObject &filter,
                                      const SmartFilterTableSpec &spec,
                                      const TableSmartFilter &values);

// Rewrites the spec's known leaves of an imported Fltr object in place,
// leaving every other key, id form and order untouched. False when a field is
// missing or has another type (the edit is then refused).
[[nodiscard]] bool patch_table_smart_filter_fields(
    DescriptorObject &filter, const SmartFilterTableSpec &spec,
    const TableSmartFilter &values);

}  // namespace patchy::psd
