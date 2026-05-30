#pragma once

#include <functional>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <crtdbg.h>
#include <windows.h>
#endif

namespace patchy::test {

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

inline void suppress_crash_dialogs() noexcept {
#ifdef _WIN32
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
}

}  // namespace patchy::test

#define CHECK(expression) ::patchy::test::check((expression), #expression, __FILE__, __LINE__)
