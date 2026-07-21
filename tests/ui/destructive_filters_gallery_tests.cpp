// Aggregator for the destructive_filters_gallery UI test group, split into
// two part TUs (destructive_filters_gallery_tests_dialogs_recipes.cpp and
// destructive_filters_gallery_tests_browse_controls.cpp) purely for
// object-size/compile-time reasons.
//
// Registration ORDER is a load-bearing contract (QSettings leakage between
// neighbouring tests, artifact dependencies): part1 + part2 concatenated must
// reproduce the pre-split registration vector entry for entry, name strings
// byte-identical. In particular ui_all_builtin_filters_render_stroke_contact_sheet
// (writes the SHA-pinned canary test-artifacts/
// ui_all_builtin_filters_stroke_contact_sheet.png, see docs/filters.md) must
// stay the final entry of part2. Never reorder, and never switch to static
// self-registration.

#include "test_harness.hpp"
#include "ui_test_groups.hpp"

#include <vector>

std::vector<patchy::test::TestCase> destructive_filters_gallery_tests_part1();
std::vector<patchy::test::TestCase> destructive_filters_gallery_tests_part2();

std::vector<patchy::test::TestCase> destructive_filters_gallery_tests() {
  auto tests = destructive_filters_gallery_tests_part1();
  const auto part2 = destructive_filters_gallery_tests_part2();
  tests.insert(tests.end(), part2.begin(), part2.end());
  return tests;
}
