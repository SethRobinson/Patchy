#include "test_harness.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <crtdbg.h>
#include <windows.h>
#else
#include <time.h>
#endif

namespace patchy::test {

void suppress_crash_dialogs() noexcept {
#ifdef _WIN32
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
}

double current_thread_cpu_ms() noexcept {
#ifdef _WIN32
  FILETIME created{};
  FILETIME exited{};
  FILETIME kernel{};
  FILETIME user{};
  if (GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user) == 0) {
    return -1.0;
  }
  const auto ticks = [](const FILETIME& time) {
    return (static_cast<unsigned long long>(time.dwHighDateTime) << 32U) | time.dwLowDateTime;
  };
  return static_cast<double>(ticks(kernel) + ticks(user)) / 1.0e4;  // 100 ns units
#elif defined(CLOCK_THREAD_CPUTIME_ID)
  timespec now{};
  if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now) != 0) {
    return -1.0;
  }
  return static_cast<double>(now.tv_sec) * 1000.0 + static_cast<double>(now.tv_nsec) / 1.0e6;
#else
  return -1.0;
#endif
}

}  // namespace patchy::test
