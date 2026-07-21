// Aggregator for the brush/pattern/palette UI test group, split by TU size into:
//   brush_pattern_palette_tests_patterns_palette.cpp (part 1: brush tip library and
//     import, pattern library, palette/indexed-color tests)
//   brush_pattern_palette_tests_brush_dynamics.cpp   (part 2: brush stamping, cursor,
//     picker popups, and brush dynamics tests)
//
// Registration ORDER is a load-bearing contract: cleanup_after_visual_test restores
// only language, so QSettings state leaks between tests by construction, and later
// tests depend on artifacts written by earlier ones. The concatenation below must
// reproduce the pre-split registration vector entry for entry. Never use static
// self-registration: cross-TU initialization order would reorder the suite.

#include "test_harness.hpp"
#include "ui_test_groups.hpp"

#include <vector>

std::vector<patchy::test::TestCase> brush_pattern_palette_tests_part1();
std::vector<patchy::test::TestCase> brush_pattern_palette_tests_part2();

std::vector<patchy::test::TestCase> brush_pattern_palette_tests() {
  auto tests = brush_pattern_palette_tests_part1();
  auto part2 = brush_pattern_palette_tests_part2();
  for (auto& test : part2) {
    tests.push_back(std::move(test));
  }
  return tests;
}
