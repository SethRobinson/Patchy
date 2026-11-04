#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
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

namespace patchy::test {

using TestFn = std::function<void()>;

struct TestCase {
  std::string name;
  TestFn run;
};

// Why a test skipped, for the [SUMMARY] split (docs/testing-skips.md). `fixture` is an
// absent untracked input (local-test-fixtures, staged models or plug-ins); PATCHY_REQUIRE_FIXTURES=1
// fails those instead. `platform` is the host (OS, window system, installed fonts, codecs);
// `feature` is a build without the feature.
enum class SkipKind { fixture, platform, feature, other };

inline const char* skip_kind_name(SkipKind kind) {
  switch (kind) {
    case SkipKind::fixture:
      return "fixture";
    case SkipKind::platform:
      return "platform";
    case SkipKind::feature:
      return "feature";
    case SkipKind::other:
      break;
  }
  return "other";
}

// The class of a plain skip(reason), from the wording the suites use. Tests that know
// better pass the kind to skip(kind, reason).
inline SkipKind classify_skip_reason(std::string_view reason) {
  std::string text(reason);
  for (auto& character : text) {
    if (character >= 'A' && character <= 'Z') {
      character = static_cast<char>(character - 'A' + 'a');
    }
  }
  const auto has = [&text](std::initializer_list<std::string_view> needles) {
    for (const auto needle : needles) {
      if (text.find(needle) != std::string::npos) {
        return true;
      }
    }
    return false;
  };
  if (has({"local-test-fixtures", "optional fixture", "fixture missing", "fixture font missing", "not staged",
           "fetch-test-plugins"})) {
    return SkipKind::fixture;
  }
  if (has({"build without", "this build", "build includes", "qt build", "not built", "patchy_enable"})) {
    return SkipKind::feature;
  }
  if (has({"windows", "macos", "linux", "unix", "wayland", "flatpak", "directwrite", "fontconfig", "platform",
           "64-bit", "interactive desktop", "codec", "decoder", "symlink", "font", "face", "installed", "registered",
           "this machine", "this host", "arial", "verdana", "helvetica", "calibri"})) {
    return SkipKind::platform;
  }
  return SkipKind::other;
}

// Thrown by skip(). Deliberately not a std::exception, so a test's own
// catch (const std::exception&) cannot swallow it.
struct TestSkipped {
  std::string reason;
  SkipKind kind = SkipKind::other;
};

// Ends the running test as skipped: the runner prints "[SKIP] <name>: <reason>" instead
// of [PASS], because the test checked nothing. For an absent optional input (an untracked
// local fixture, a font the machine lacks, a platform or build without the feature), never
// to hide a failure. A test that checked part of its work before running out of inputs
// passes instead.
[[noreturn]] inline void skip(SkipKind kind, std::string reason) {
  throw TestSkipped{std::move(reason), kind};
}

[[noreturn]] inline void skip(std::string reason) {
  const auto kind = classify_skip_reason(reason);
  throw TestSkipped{std::move(reason), kind};
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

// PATCHY_REQUIRE_FIXTURES=1: a machine that is meant to hold every local fixture (the
// release box) fails a fixture skip instead of passing over it.
inline bool fixtures_required() {
  const auto value = environment_value("PATCHY_REQUIRE_FIXTURES");
  return !value.empty() && value != "0";
}

// Skips the test unless the optional fixture at `path` exists (fails it under
// PATCHY_REQUIRE_FIXTURES=1).
inline void skip_unless_exists(const std::filesystem::path& path) {
  if (!std::filesystem::exists(path)) {
    if (fixtures_required()) {
      throw std::runtime_error("required fixture missing (PATCHY_REQUIRE_FIXTURES=1): " + fixture_display_path(path));
    }
    skip(SkipKind::fixture, "optional fixture missing: " + fixture_display_path(path));
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

// Defined in test_harness.cpp: it is the one place the harness needs <windows.h>, which
// would otherwise ride into every test translation unit.
void suppress_crash_dialogs() noexcept;

// Exit code of a run a test timeout ended (the same value as coreutils `timeout`).
inline constexpr int kTestTimeoutExitCode = 124;

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

// The wall-clock limit a test may assert: a hang guard, never a performance bound
// (docs/testing.md, "Failure and lifetime traps"). Throughput budgets live in
// patchy_perf_tests (its "toolbudgets" scenario holds the core tool strokes).
inline constexpr long long kWallClockBackstopMs = 120000;

// PATCHY_TEST_TIME_SCALE (a positive number; default 1, and 4 in Debug builds) multiplies
// time_budget_ms. Only the throughput budgets that predate scripts/lint-test-sources.py use
// it: new tests assert structure and keep minutes as the backstop (docs/testing.md).
inline double test_time_scale() {
  if (const auto text = environment_value("PATCHY_TEST_TIME_SCALE"); !text.empty()) {
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end != text.c_str() && value > 0.0) {
      return value;
    }
  }
#ifndef NDEBUG
  return 4.0;
#else
  return 1.0;
#endif
}

inline long long time_budget_ms(double budget_ms) {
  return static_cast<long long>(budget_ms * test_time_scale() + 0.5);
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
  // Only the watcher thread reads it; wasm runs no watcher.
  bool stopping_ = false;
#endif
  long long limit_seconds_;
  Report report_;
  std::mutex mutex_;
  std::condition_variable changed_;
  std::chrono::steady_clock::time_point deadline_{};
  std::string name_;
  bool armed_ = false;
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
  // Names test-artifacts/skipped-<suite_name>.txt; empty takes argv[0]'s stem.
  std::string suite_name;
};

struct SkippedTestRecord {
  std::string name;
  std::string reason;
};

// Writes test-artifacts/skipped-<suite>.txt (under the working directory): one
// "<test name>\t<reason>" line per skipped test, sorted by name, empty when nothing
// skipped. The release verifiers diff these names against an expected list per host
// (docs/testing-skips.md); the file is a valid expected list as it stands.
inline void write_skipped_tests_file(const std::string& suite, std::vector<SkippedTestRecord> records) {
  std::sort(records.begin(), records.end(),
            [](const SkippedTestRecord& left, const SkippedTestRecord& right) { return left.name < right.name; });
  std::error_code error;
  const auto folder = std::filesystem::current_path(error) / "test-artifacts";
  std::filesystem::create_directories(folder, error);
  const auto path = folder / ("skipped-" + suite + ".txt");
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  for (const auto& record : records) {
    std::string reason = record.reason;
    for (auto& character : reason) {
      if (character == '\n' || character == '\r' || character == '\t') {
        character = ' ';
      }
    }
    out << record.name << '\t' << reason << '\n';
  }
  if (!out) {
    std::cout << "  note: could not write test-artifacts/skipped-" << suite << ".txt\n";
  }
}

inline void print_test_suite_usage(const char* program) {
  std::cerr << "usage: " << (program != nullptr ? program : "tests") << " [--list] [--exact] [filter]\n"
            << "  filter   comma-separated name substrings; a test runs when its name contains any of them\n"
            << "  --exact  each filter item is a whole test name instead of a substring\n"
            << "  --list   print the selected test names instead of running them\n"
            << "A filter item that selects no test fails the run (exit " << kTestUsageExitCode << ").\n";
}

// The runner loop shared by patchy_core_tests (native and wasm) and patchy_ui_visual_tests.
// Prints [PASS] on stdout, [FAIL] on stderr, [SKIP] for skip(), and a closing [SUMMARY]
// line whose skip count is split by SkipKind; then writes test-artifacts/skipped-<suite>.txt.
// A test that recorded a failure (report_failure) and then skipped fails; under
// PATCHY_REQUIRE_FIXTURES=1 a fixture skip fails too. Returns 0 when nothing failed (skips
// included), 1 when a test failed, and kTestUsageExitCode for a bad command line or a filter
// item that selects no test.
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
  std::vector<SkippedTestRecord> skipped;
  std::array<std::size_t, 4> skipped_by_kind{};
  std::vector<std::string> failed;
  const bool require_fixtures = fixtures_required();
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
      if (!deferred_failures().empty()) {
        // A skip after a recorded failure must not hide it: the test checked something and
        // it failed.
        failed.push_back(test->name);
        for (const auto& message : deferred_failures()) {
          std::cerr << "[FAIL] " << test->name << ": " << message << std::endl;
        }
        std::cerr << "  then skipped: " << skipped_test.reason << std::endl;
      } else if (require_fixtures && skipped_test.kind == SkipKind::fixture) {
        failed.push_back(test->name);
        std::cerr << "[FAIL] " << test->name << ": required fixture missing (PATCHY_REQUIRE_FIXTURES=1): "
                  << skipped_test.reason << std::endl;
      } else {
        skipped.push_back({test->name, skipped_test.reason});
        ++skipped_by_kind[static_cast<std::size_t>(skipped_test.kind)];
        std::cout << "[SKIP] " << test->name << ": " << skipped_test.reason << std::endl;
      }
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
  // The skip split: "skipped 7 (fixture 3, platform 2, feature 1, other 1)", zero classes left out.
  std::string skip_classes;
  for (std::size_t kind = 0; kind < skipped_by_kind.size(); ++kind) {
    if (skipped_by_kind[kind] != 0) {
      skip_classes += (skip_classes.empty() ? " (" : ", ");
      skip_classes += std::string(skip_kind_name(static_cast<SkipKind>(kind))) + " " +
                      std::to_string(skipped_by_kind[kind]);
    }
  }
  if (!skip_classes.empty()) {
    skip_classes += ")";
  }
  std::cout << "[SUMMARY] executed " << selected.size() << ", passed " << passed << ", failed " << failed.size()
            << ", skipped " << skipped.size() << skip_classes << " in " << seconds << " s" << std::endl;
  std::string suite = options.suite_name;
  if (suite.empty() && argc > 0 && argv[0] != nullptr) {
    suite = std::filesystem::path(argv[0]).stem().string();
  }
  if (!suite.empty()) {
    write_skipped_tests_file(suite, skipped);
  }
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
