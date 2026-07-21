#include "destructive_filters_gallery_test_support.hpp"

#include "ui_test_support.hpp"

namespace patchy::test::ui {

bool filter_invocations_equal(const patchy::FilterInvocation& lhs,
                              const patchy::FilterInvocation& rhs) {
  return lhs.filter_id == rhs.filter_id &&
         lhs.schema_version == rhs.schema_version &&
         lhs.parameters == rhs.parameters &&
         filter_rgb_equal(lhs.foreground, rhs.foreground) &&
         filter_rgb_equal(lhs.background, rhs.background);
}

}  // namespace patchy::test::ui
