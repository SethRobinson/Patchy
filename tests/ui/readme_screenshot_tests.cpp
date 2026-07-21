// README screenshots (shot_readme_*) plus the visual contact sheet.
//
// These scenes produce the marketing screenshots embedded in README.md. They
// are ordinary offscreen visual tests (deterministic; [SKIP] when a local
// fixture is absent) whose PNGs land in test-artifacts/ like any artifact.
// Regenerate with scripts/make-readme-screenshots.ps1, which reruns the
// "shot_readme" filter and copies the PNGs into docs/images/screenshots/.
//
// readme_screenshot_tests() is split across two part TUs purely for
// translation-unit size (readme_screenshot_tests_classic.cpp,
// readme_screenshot_tests_scripting.cpp). The registration ORDER is a
// load-bearing contract: the contact sheet consumes artifacts written by
// earlier groups and QSettings state leaks between scenes by construction,
// so part1 + part2 concatenated below must keep the historical registration
// vector's order, entry for entry.

#include "test_harness.hpp"
#include "ui_test_groups.hpp"

#include <vector>

std::vector<patchy::test::TestCase> readme_screenshot_tests_part1();
std::vector<patchy::test::TestCase> readme_screenshot_tests_part2();

std::vector<patchy::test::TestCase> readme_screenshot_tests() {
  std::vector<patchy::test::TestCase> tests;
  for (const auto& part : {&readme_screenshot_tests_part1, &readme_screenshot_tests_part2}) {
    auto cases = (*part)();
    tests.insert(tests.end(), cases.begin(), cases.end());
  }
  return tests;
}
