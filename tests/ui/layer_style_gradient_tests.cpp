// Aggregator for the layer_style_gradient UI test group, split across
// layer_style_gradient_tests_effects.cpp (part 1) and
// layer_style_gradient_tests_presets.cpp (part 2) to keep TU sizes down.
//
// ORDER CONTRACT: the concatenated part1 + part2 vector must reproduce the
// original single-TU registration order exactly. Suite order is load-bearing:
// QSettings state intentionally crosses tests and later groups consume
// artifacts written earlier. Append new tests to the correct part's
// registration vector; never reorder entries across the part boundary.

#include "test_harness.hpp"
#include "ui_test_groups.hpp"

#include <vector>

std::vector<patchy::test::TestCase> layer_style_gradient_tests_part1();
std::vector<patchy::test::TestCase> layer_style_gradient_tests_part2();

std::vector<patchy::test::TestCase> layer_style_gradient_tests() {
  auto tests = layer_style_gradient_tests_part1();
  auto part2 = layer_style_gradient_tests_part2();
  tests.insert(tests.end(), part2.begin(), part2.end());
  return tests;
}
