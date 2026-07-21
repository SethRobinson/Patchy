// smart_filter_tests() is split across three part TUs purely for
// translation-unit size (smart_filter_tests_flows.cpp, _guards.cpp,
// _kinds.cpp). The registration ORDER is a load-bearing contract:
// QSettings state and on-disk artifacts leak between tests by
// construction, so part1 + part2 + part3 concatenated below must
// reproduce the historical monolith's registration vector exactly,
// entry for entry.

#include "test_harness.hpp"
#include "ui_test_groups.hpp"

#include <vector>

std::vector<patchy::test::TestCase> smart_filter_tests_part1();
std::vector<patchy::test::TestCase> smart_filter_tests_part2();
std::vector<patchy::test::TestCase> smart_filter_tests_part3();

std::vector<patchy::test::TestCase> smart_filter_tests() {
  std::vector<patchy::test::TestCase> tests;
  for (const auto& part : {&smart_filter_tests_part1, &smart_filter_tests_part2,
                           &smart_filter_tests_part3}) {
    auto cases = (*part)();
    tests.insert(tests.end(), cases.begin(), cases.end());
  }
  return tests;
}
