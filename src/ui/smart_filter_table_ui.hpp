#pragma once

#include "core/smart_filter.hpp"
#include "filters/filter_registry.hpp"
#include "ui/filter_workflows.hpp"

#include <QString>

// Settings dialog, invocation mapping and layer-row text for the
// table-described native Smart Filters (core/smart_filter_table.hpp).
namespace patchy::ui {

// The dialog's invocation for `values` (every field with a control).
[[nodiscard]] FilterInvocation table_smart_filter_invocation(
    const SmartFilterTableSpec &spec, const TableSmartFilter &values);

[[nodiscard]] FilterDialogSpec table_smart_filter_dialog_spec(
    const SmartFilterTableSpec &spec);

// `base` with every field the invocation carries replaced (clamped to the
// field's range); fields without a control keep their `base` value.
[[nodiscard]] TableSmartFilter table_smart_filter_from_invocation(
    const SmartFilterTableSpec &spec, const FilterInvocation &invocation,
    const TableSmartFilter &base);

[[nodiscard]] QString table_smart_filter_display_name(
    const SmartFilterTableSpec &spec);

// " (Label value, ...)" for the layer-row tooltip; empty without fields.
[[nodiscard]] QString table_smart_filter_tooltip_details(
    const SmartFilterTableSpec &spec, const TableSmartFilter &values);

}  // namespace patchy::ui
