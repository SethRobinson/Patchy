// Aggregator for the selection / marquee / lasso UI test group.
//
// The test bodies live in two part files:
//   selection_marquee_lasso_tests_marquee_geometry.cpp  (part 1: marquee and
//       shape geometry, canvas aids, deep zoom grid, snapping, preferences)
//   selection_marquee_lasso_tests_regions_lasso.cpp     (part 2: region
//       outlines, layer transparency selections, layer locks, lasso,
//       selection moves)
//
// ORDER CONTRACT: registration order is load-bearing (QSettings state and
// saved artifacts leak between tests). The concatenation below must keep
// part 1 followed by part 2, and each part must keep its internal order, so
// the combined vector reproduces the original single-file registration
// exactly.

#include "test_harness.hpp"
#include "ui_test_groups.hpp"

#include <vector>

std::vector<patchy::test::TestCase> selection_marquee_lasso_tests_part1();
std::vector<patchy::test::TestCase> selection_marquee_lasso_tests_part2();

std::vector<patchy::test::TestCase> selection_marquee_lasso_tests() {
  std::vector<patchy::test::TestCase> tests = selection_marquee_lasso_tests_part1();
  auto part2 = selection_marquee_lasso_tests_part2();
  tests.insert(tests.end(), part2.begin(), part2.end());
  return tests;
}
