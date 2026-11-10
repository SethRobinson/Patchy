#pragma once

// Smart Filter stack editing shared by the Smart Filter settings dialogs, the Filter
// Gallery and the script bindings (script_smart_filters.cpp), so every path builds and
// edits entries the same way (main_window_filters.cpp; docs/smart-objects.md).

#include "core/smart_filter.hpp"
#include "filters/filter_registry.hpp"

#include <cstddef>
#include <optional>

namespace patchy {
class Document;
}

namespace patchy::ui {

class CanvasWidget;

// The shared filter mask a new stack starts with: the canvas selection's alpha, or all
// white without a selection.
[[nodiscard]] SmartFilterMask new_smart_filter_stack_mask(const Document& document,
                                                          const CanvasWidget& canvas);

// The settings of `entry` as the invocation its settings dialog edits; nullopt for an
// entry with no settings dialog (adjustments, a multi-effect Filter Gallery entry, an
// unmodeled filter).
[[nodiscard]] std::optional<FilterInvocation> smart_filter_entry_settings(
    const SmartFilterEntry& entry);

// `stack` with entry `index` taking `settings` (the dialog's Apply): values are clamped
// to the filter's ranges, absent keys keep their value. Throws std::invalid_argument
// when the entry's settings cannot be edited.
[[nodiscard]] SmartFilterStack smart_filter_stack_with_entry_settings(
    SmartFilterStack stack, std::size_t index, const FilterInvocation& settings);

}  // namespace patchy::ui
