#pragma once

// Helper shared by both destructive_filters_gallery part TUs, moved verbatim
// (never copied) from the pre-split tests/ui/destructive_filters_gallery_tests.cpp:
// filter_recipe_entries_equal (part 1) and the generated-controls test (part 2)
// both compare FilterInvocations.

#include "filters/filter_registry.hpp"

namespace patchy::test::ui {

bool filter_invocations_equal(const patchy::FilterInvocation& lhs,
                              const patchy::FilterInvocation& rhs);

}  // namespace patchy::test::ui
