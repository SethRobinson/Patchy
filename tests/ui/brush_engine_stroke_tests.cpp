// Aggregator for the brush-engine stroke UI test group, split by TU size into:
//   brush_engine_stroke_tests_strokes.cpp       (part 1: brush/eraser/clone/heal/
//     smudge/mixer stroke behavior, cursors, spacebar pan)
//   brush_engine_stroke_tests_healing_patch.cpp (part 2: Spot Healing, Patch tool,
//     and the retouch Sample All Layers option)
//
// Registration ORDER is a load-bearing contract: cleanup_after_visual_test restores
// only language, so QSettings state leaks between tests by construction, and later
// tests depend on artifacts written by earlier ones. The concatenation below must
// reproduce the pre-split registration vector entry for entry. Never use static
// self-registration: cross-TU initialization order would reorder the suite.

#include "test_harness.hpp"
#include "ui_test_groups.hpp"

#include <vector>

std::vector<patchy::test::TestCase> brush_engine_stroke_tests_part1();
std::vector<patchy::test::TestCase> brush_engine_stroke_tests_part2();

std::vector<patchy::test::TestCase> brush_engine_stroke_tests() {
  auto tests = brush_engine_stroke_tests_part1();
  auto part2 = brush_engine_stroke_tests_part2();
  for (auto& test : part2) {
    tests.push_back(std::move(test));
  }
  return tests;
}
