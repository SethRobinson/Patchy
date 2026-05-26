#pragma once

#include <functional>
#include <stdexcept>
#include <string>

namespace photoslop::test {

using TestFn = std::function<void()>;

struct TestCase {
  std::string name;
  TestFn run;
};

inline void check(bool condition, const char* expression, const char* file, int line) {
  if (!condition) {
    throw std::runtime_error(std::string(file) + ":" + std::to_string(line) + " check failed: " + expression);
  }
}

}  // namespace photoslop::test

#define CHECK(expression) ::photoslop::test::check((expression), #expression, __FILE__, __LINE__)
