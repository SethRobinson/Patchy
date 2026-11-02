#include "test_harness.hpp"
#include <iterator>
#include <vector>

std::vector<patchy::test::TestCase> scripting_tests_part1();
std::vector<patchy::test::TestCase> scripting_tests_part2();
std::vector<patchy::test::TestCase> scripting_tests_part3();
std::vector<patchy::test::TestCase> script_pixel_io_tests();

// Keep the original registration order: later tests consume settings and artifacts.
std::vector<patchy::test::TestCase> scripting_tests() {
  std::vector<patchy::test::TestCase> tests;
  {
    auto part = scripting_tests_part1();
    tests.insert(tests.end(), std::make_move_iterator(part.begin()), std::make_move_iterator(part.end()));
  }
  {
    auto part = scripting_tests_part2();
    tests.insert(tests.end(), std::make_move_iterator(part.begin()), std::make_move_iterator(part.end()));
  }
  {
    auto part = scripting_tests_part3();
    tests.insert(tests.end(), std::make_move_iterator(part.begin()), std::make_move_iterator(part.end()));
  }
  {
    auto part = script_pixel_io_tests();
    tests.insert(tests.end(), std::make_move_iterator(part.begin()), std::make_move_iterator(part.end()));
  }
  return tests;
}
