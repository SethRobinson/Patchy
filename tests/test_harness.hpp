#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <functional>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#ifndef __EMSCRIPTEN__
#include <thread>
#endif

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

// Thrown by skip(). Deliberately not a std::exception, so a test's own
// catch (const std::exception&) cannot swallow it.
struct TestSkipped {
  std::string reason;
};

// Ends the running test as skipped: the runner prints "[SKIP] <name>: <reason>" instead
// of [PASS], because the test checked nothing. For an absent optional input (an untracked
// local fixture, a font the machine lacks, a platform or build without the feature), never
// to hide a failure. A test that checked part of its work before running out of inputs
// passes instead.
[[noreturn]] inline void skip(std::string reason) {
  throw TestSkipped{std::move(reason)};
}

// A fixture path for a skip reason: from its test-fixtures or local-test-fixtures folder
// on, so the line does not carry the checkout's location.
inline std::string fixture_display_path(const std::filesystem::path& path) {
  std::filesystem::path shown;
  bool found = false;
  for (const auto& part : path) {
    found = found || part == "local-test-fixtures" || part == "test-fixtures";
    if (found) {
      shown /= part;
    }
  }
  const auto text = (found ? shown : path).generic_u8string();
  return std::string(text.begin(), text.end());
}

// Skips the test unless the optional fixture at `path` exists.
inline void skip_unless_exists(const std::filesystem::path& path) {
  if (!std::filesystem::exists(path)) {
    skip("optional fixture missing: " + fixture_display_path(path));
  }
}

inline void check(bool condition, const char* expression, const char* file, int line) {
  if (!condition) {
    throw std::runtime_error(std::string(file) + ":" + std::to_string(line) + " check failed: " + expression);
  }
}

// Failures recorded without throwing, for code that runs inside Qt's event dispatch
// (a capture inside run_non_modal_dialog, a driver lambda): an exception there aborts
// the process on macOS. The runner fails the test with these messages once it returns.
inline std::vector<std::string>& deferred_failures() {
  static std::vector<std::string> failures;
  return failures;
}

inline void report_failure(std::string message) {
  deferred_failures().push_back(std::move(message));
}

inline void suppress_crash_dialogs() noexcept {
#ifdef _WIN32
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
}

// Exit code of a run a test timeout ended (the same value as coreutils `timeout`).
inline constexpr int kTestTimeoutExitCode = 124;

// An environment variable's value, empty when unset (MSVC deprecates std::getenv).
inline std::string environment_value(const char* name) {
#ifdef _MSC_VER
  char* buffer = nullptr;
  std::size_t length = 0;
  std::string value;
  if (_dupenv_s(&buffer, &length, name) == 0 && buffer != nullptr) {
    value = buffer;
  }
  std::free(buffer);
  return value;
#else
  const char* value = std::getenv(name);
  return value != nullptr ? std::string(value) : std::string();
#endif
}

// The per-test limit in seconds: PATCHY_TEST_TIMEOUT_SECONDS when set (0 turns the
// watchdog off), else `fallback`. Debug builds get four times the fallback.
inline long long test_timeout_seconds(long long fallback) {
  if (const auto text = environment_value("PATCHY_TEST_TIMEOUT_SECONDS"); !text.empty()) {
    char* end = nullptr;
    const long long value = std::strtoll(text.c_str(), &end, 10);
    if (end != text.c_str() && value >= 0) {
      return value;
    }
  }
#ifndef NDEBUG
  fallback *= 4;
#endif
  return fallback;
}

// Fails a test that runs past its limit instead of letting the run hang (a modal
// dialog nobody answers sat for an hour on a build machine, October 2026). A thread
// waits on a deadline the runner arms per test; on expiry it prints a [FAIL] line
// naming the test, runs `report` for extra context, and ends the process at once
// with kTestTimeoutExitCode: the stuck test owns the main thread, so nothing could
// unwind it. `report` runs on the watchdog thread and must not touch GUI objects
// directly. Off under Emscripten (no threads there) and when the limit is 0.
class TestWatchdog {
public:
  using Report = std::function<void()>;

  TestWatchdog(long long limit_seconds, Report report = {}) : limit_seconds_(limit_seconds), report_(std::move(report)) {
#ifndef __EMSCRIPTEN__
    if (limit_seconds_ > 0) {
      thread_ = std::thread([this] { watch(); });
    }
#endif
  }

  ~TestWatchdog() {
#ifndef __EMSCRIPTEN__
    {
      std::lock_guard lock(mutex_);
      stopping_ = true;
    }
    changed_.notify_all();
    if (thread_.joinable()) {
      thread_.join();
    }
#endif
  }

  TestWatchdog(const TestWatchdog&) = delete;
  TestWatchdog& operator=(const TestWatchdog&) = delete;

  void begin(const std::string& name) {
    {
      std::lock_guard lock(mutex_);
      name_ = name;
      deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(limit_seconds_);
      armed_ = true;
    }
    changed_.notify_all();
  }

  void end() {
    {
      std::lock_guard lock(mutex_);
      armed_ = false;
    }
    changed_.notify_all();
  }

private:
#ifndef __EMSCRIPTEN__
  void watch() {
    std::unique_lock lock(mutex_);
    while (!stopping_) {
      if (!armed_) {
        changed_.wait(lock);
        continue;
      }
      const auto deadline = deadline_;
      if (changed_.wait_until(lock, deadline) != std::cv_status::timeout || !armed_ || deadline_ != deadline) {
        continue;
      }
      const auto name = name_;
      lock.unlock();
      std::fprintf(stderr, "[FAIL] %s: timed out after %lld s (PATCHY_TEST_TIMEOUT_SECONDS; 0 disables)\n",
                   name.c_str(), limit_seconds_);
      std::fflush(stderr);
      if (report_) {
        report_();
      }
      std::fflush(stdout);
      std::fflush(stderr);
      std::_Exit(kTestTimeoutExitCode);
    }
  }

  std::thread thread_;
#endif
  long long limit_seconds_;
  Report report_;
  std::mutex mutex_;
  std::condition_variable changed_;
  std::chrono::steady_clock::time_point deadline_{};
  std::string name_;
  bool armed_ = false;
  bool stopping_ = false;
};

// Exit code of a run whose command line is wrong or whose filter selects no test.
inline constexpr int kTestUsageExitCode = 2;

struct TestSuiteOptions {
  // Used when the command line names no filter (the UI suite's PATCHY_UI_TEST_FILTER).
  std::string default_filter;
  long long timeout_seconds = 300;
  TestWatchdog::Report timeout_report;
  // Runs before and after every test (the UI suite's window and settings cleanup).
  std::function<void()> around_each;
};

inline void print_test_suite_usage(const char* program) {
  std::cerr << "usage: " << (program != nullptr ? program : "tests") << " [--list] [--exact] [filter]\n"
            << "  filter   comma-separated name substrings; a test runs when its name contains any of them\n"
            << "  --exact  each filter item is a whole test name instead of a substring\n"
            << "  --list   print the selected test names instead of running them\n"
            << "A filter item that selects no test fails the run (exit " << kTestUsageExitCode << ").\n";
}

// The runner loop shared by patchy_core_tests (native and wasm) and patchy_ui_visual_tests.
// Prints [PASS] on stdout, [FAIL] on stderr, [SKIP] for skip(), and a closing [SUMMARY]
// line. Returns 0 when nothing failed (skips included), 1 when a test failed, and
// kTestUsageExitCode for a bad command line or a filter item that selects no test.
inline int run_test_suite(const std::vector<TestCase>& tests, int argc, char** argv,
                          const TestSuiteOptions& options) {
  bool list = false;
  bool exact = false;
  bool have_filter = false;
  std::string filter = options.default_filter;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument = argv[index] != nullptr ? argv[index] : "";
    if (argument == "--list") {
      list = true;
    } else if (argument == "--exact") {
      exact = true;
    } else if (argument == "--help" || argument == "-h") {
      print_test_suite_usage(argv[0]);
      return 0;
    } else if (argument.starts_with("--") || have_filter) {
      std::cerr << "[FAIL] command line: unexpected argument \"" << argument << "\"\n";
      print_test_suite_usage(argv[0]);
      return kTestUsageExitCode;
    } else {
      filter = std::string(argument);
      have_filter = true;
    }
  }

  std::vector<std::string> items;
  for (std::size_t start = 0; start <= filter.size();) {
    const auto comma = filter.find(',', start);
    const auto end = comma == std::string::npos ? filter.size() : comma;
    if (end > start) {
      items.push_back(filter.substr(start, end - start));
    }
    if (comma == std::string::npos) {
      break;
    }
    start = comma + 1;
  }
  if (exact && items.empty()) {
    std::cerr << "[FAIL] command line: --exact needs a filter naming the tests\n";
    return kTestUsageExitCode;
  }

  std::vector<const TestCase*> selected;
  std::vector<bool> item_used(items.size(), false);
  for (const auto& test : tests) {
    bool chosen = items.empty();
    for (std::size_t item = 0; item < items.size(); ++item) {
      if (exact ? test.name == items[item] : test.name.find(items[item]) != std::string::npos) {
        item_used[item] = true;
        chosen = true;
      }
    }
    if (chosen) {
      selected.push_back(&test);
    }
  }
  // A filter that selects nothing used to run nothing and exit 0, which reads as a pass.
  bool unmatched = false;
  for (std::size_t item = 0; item < items.size(); ++item) {
    if (!item_used[item]) {
      std::cerr << "[FAIL] filter: " << (exact ? "no test is named \"" : "no test name contains \"") << items[item]
                << "\" (--list prints the test names)\n";
      unmatched = true;
    }
  }
  if (unmatched) {
    return kTestUsageExitCode;
  }

  if (list) {
    for (const auto* test : selected) {
      std::cout << test->name << '\n';
    }
    std::cout.flush();
    return 0;
  }

  std::size_t passed = 0;
  std::size_t skipped = 0;
  std::vector<std::string> failed;
  const auto started = std::chrono::steady_clock::now();
  TestWatchdog watchdog(options.timeout_seconds, options.timeout_report);
  for (const auto* test : selected) {
    watchdog.begin(test->name);
    if (options.around_each) {
      options.around_each();
    }
    // Flushed per test so the log shows how far a run got if it is killed.
    deferred_failures().clear();
    try {
      test->run();
      if (deferred_failures().empty()) {
        ++passed;
        std::cout << "[PASS] " << test->name << std::endl;
      } else {
        failed.push_back(test->name);
        for (const auto& message : deferred_failures()) {
          std::cerr << "[FAIL] " << test->name << ": " << message << std::endl;
        }
      }
    } catch (const TestSkipped& skipped_test) {
      ++skipped;
      std::cout << "[SKIP] " << test->name << ": " << skipped_test.reason << std::endl;
    } catch (const std::exception& error) {
      failed.push_back(test->name);
      std::cerr << "[FAIL] " << test->name << ": " << error.what() << std::endl;
      for (const auto& message : deferred_failures()) {
        std::cerr << "[FAIL] " << test->name << ": " << message << std::endl;
      }
    }
    deferred_failures().clear();
    if (options.around_each) {
      options.around_each();
    }
    watchdog.end();
  }
  const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - started;

  char seconds[32] = {};
  std::snprintf(seconds, sizeof(seconds), "%.1f", elapsed.count());
  std::cout << "[SUMMARY] executed " << selected.size() << ", passed " << passed << ", failed " << failed.size()
            << ", skipped " << skipped << " in " << seconds << " s" << std::endl;
  if (!selected.empty() && passed == 0 && failed.empty()) {
    std::cout << "[SUMMARY] every selected test skipped: nothing was checked" << std::endl;
  }
  // Names only, without the [FAIL] tag: scripts count [FAIL] lines.
  for (const auto& name : failed) {
    std::cerr << "  failed: " << name << '\n';
  }
  std::cerr.flush();
  return failed.empty() ? 0 : 1;
}

}  // namespace patchy::test

#define CHECK(expression) ::patchy::test::check((expression), #expression, __FILE__, __LINE__)
