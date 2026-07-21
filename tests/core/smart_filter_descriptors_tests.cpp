// Aggregator for the smart_filter_descriptors core test group, split into
// tests/core/smart_filter_descriptors_tests_canonical.cpp (part1) and
// tests/core/smart_filter_descriptors_tests_fixtures.cpp (part2).
//
// Registration order is a contract: main() concatenates the group vectors in a
// fixed order, and this function must reproduce the pre-split registration
// vector entry for entry, name strings byte-identical: part1's run followed by
// part2's run. Never static self-registration.

#include "test_groups.hpp"

#include <iterator>
#include <vector>

std::vector<patchy::test::TestCase> smart_filter_descriptors_tests_part1();
std::vector<patchy::test::TestCase> smart_filter_descriptors_tests_part2();

std::vector<patchy::test::TestCase> smart_filter_descriptors_tests() {
  std::vector<patchy::test::TestCase> tests = smart_filter_descriptors_tests_part1();
  std::vector<patchy::test::TestCase> part2 = smart_filter_descriptors_tests_part2();
  tests.insert(tests.end(), std::make_move_iterator(part2.begin()),
               std::make_move_iterator(part2.end()));
  return tests;
}
