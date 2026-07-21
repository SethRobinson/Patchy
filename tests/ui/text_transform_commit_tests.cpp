// Aggregator for the text_transform_commit UI test group. The tests live in two
// themed part files:
//
//   text_transform_commit_tests_transform.cpp   (part 1): interactive text
//     transform and re-edit behavior, transformed-preview overlays, character
//     panel, and alignment commit rendering.
//   text_transform_commit_tests_psd_commit.cpp  (part 2): PSD fixture text
//     commit/rasterize parity with Photoshop, re-edit survival, CLI append-text,
//     and rich-text span commits.
//
// Registration ORDER is a load-bearing contract: earlier tests leak QSettings
// state and produce artifacts later tests depend on. The concatenation below
// (part 1 then part 2, each preserving its internal order) must reproduce the
// historical single-file registration vector exactly.

#include "test_harness.hpp"
#include "ui_test_groups.hpp"

#include <vector>

std::vector<patchy::test::TestCase> text_transform_commit_tests_part1();
std::vector<patchy::test::TestCase> text_transform_commit_tests_part2();

std::vector<patchy::test::TestCase> text_transform_commit_tests() {
  auto tests = text_transform_commit_tests_part1();
  auto part2 = text_transform_commit_tests_part2();
  tests.insert(tests.end(), part2.begin(), part2.end());
  return tests;
}
