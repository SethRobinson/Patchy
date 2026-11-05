#pragma once

// Self-tests of run_test_suite's skip judging (docs/testing-skips.md), registered by
// tests/core/main.cpp. Each runs a small inner suite with its output captured, so the inner
// [FAIL] lines never reach the outer log.

#include "local_psd_fixtures.hpp"
#include "test_harness.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace patchy::test::selftest {

// Sets an environment variable for one scope and restores the old value.
class ScopedEnvironment {
public:
  ScopedEnvironment(const char* name, const char* value) : name_(name), old_(environment_value(name)) {
    set(value);
  }
  ~ScopedEnvironment() { set(old_.empty() ? nullptr : old_.c_str()); }
  ScopedEnvironment(const ScopedEnvironment&) = delete;
  ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

private:
  void set(const char* value) {
#ifdef _WIN32
    _putenv_s(name_.c_str(), value != nullptr ? value : "");
#else
    if (value != nullptr) {
      setenv(name_.c_str(), value, 1);
    } else {
      unsetenv(name_.c_str());
    }
#endif
  }

  std::string name_;
  std::string old_;
};

struct InnerRun {
  int exit_code = -1;
  std::string out;
  std::string err;
};

inline InnerRun run_inner_suite(const std::vector<TestCase>& tests) {
  std::ostringstream out;
  std::ostringstream err;
  auto* old_out = std::cout.rdbuf(out.rdbuf());
  auto* old_err = std::cerr.rdbuf(err.rdbuf());
  TestSuiteOptions options;
  options.timeout_seconds = 0;
  options.suite_name = "harness_selftest";
  char program[] = "harness_selftest";
  char* argv[] = {program, nullptr};
  InnerRun run;
  try {
    run.exit_code = run_test_suite(tests, 1, argv, options);
  } catch (...) {
    std::cout.rdbuf(old_out);
    std::cerr.rdbuf(old_err);
    throw;
  }
  std::cout.rdbuf(old_out);
  std::cerr.rdbuf(old_err);
  run.out = out.str();
  run.err = err.str();
  return run;
}

inline bool contains(const std::string& text, const std::string& part) {
  return text.find(part) != std::string::npos;
}

inline std::string read_artifact(const std::string& file_name) {
  std::ifstream in(std::filesystem::path("test-artifacts") / file_name, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

inline std::vector<std::string> split_lines(const std::string& text) {
  std::vector<std::string> lines;
  std::istringstream in(text);
  for (std::string line; std::getline(in, line);) {
    lines.push_back(line);
  }
  return lines;
}

// Spins (no sleep: wasm has no threads) so one inner test is clearly the slowest.
inline void spin_for_milliseconds(int milliseconds) {
  const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
  while (std::chrono::steady_clock::now() < until) {
  }
}

inline std::vector<TestCase> test_harness_selftests() {
  return {
      {"test_harness_fails_a_skip_after_a_recorded_failure",
       [] {
         const auto run = run_inner_suite({
             {"records_then_skips",
              [] {
                report_failure("found a bad pixel");
                skip("optional fixture missing: local-test-fixtures/x.psd");
              }},
             {"plain_skip", [] { skip("optional fixture missing: local-test-fixtures/y.psd"); }},
             {"passes", [] {}},
         });
         CHECK(run.exit_code == 1);
         CHECK(contains(run.err, "[FAIL] records_then_skips: found a bad pixel"));
         CHECK(contains(run.err, "  then skipped: optional fixture missing: local-test-fixtures/x.psd"));
         CHECK(!contains(run.out, "[SKIP] records_then_skips"));
         CHECK(contains(run.out, "[SKIP] plain_skip: optional fixture missing"));
         CHECK(contains(run.out, "[SUMMARY] executed 3, passed 1, failed 1, skipped 1 (fixture 1) in "));
       }},
      {"test_harness_records_skips_by_class_and_name",
       [] {
         const auto run = run_inner_suite({
             {"zeta_feature", [] { skip("build without the G'MIC engine (PATCHY_ENABLE_GMIC off)"); }},
             {"alpha_platform", [] { skip("Windows only (one-click update)"); }},
             {"mid_fixture", [] { skip_unless_exists(local_fixtures_root() / "no-such-folder" / "absent.psd"); }},
             {"other_reason", [] { skip(SkipKind::other, "the corpus sweep runs with PATCHY_KRA_CORPUS=1"); }},
         });
         CHECK(run.exit_code == 0);
         CHECK(contains(run.out, "skipped 4 (fixture 1, platform 1, feature 1, other 1) in "));
         const auto record = read_artifact("skipped-harness_selftest.txt");
         CHECK(record ==
               "alpha_platform\tWindows only (one-click update)\n"
               "mid_fixture\toptional fixture missing: local-test-fixtures/no-such-folder/absent.psd\n"
               "other_reason\tthe corpus sweep runs with PATCHY_KRA_CORPUS=1\n"
               "zeta_feature\tbuild without the G'MIC engine (PATCHY_ENABLE_GMIC off)\n");
         CHECK(classify_skip_reason("no .dds files in local-test-fixtures/dds") == SkipKind::fixture);
         CHECK(classify_skip_reason("the watermark models are not staged (setup-ai-deps provisions them)") ==
               SkipKind::fixture);
         CHECK(classify_skip_reason("this build has no AVIF codec") == SkipKind::feature);
         CHECK(classify_skip_reason("Arial is not installed") == SkipKind::platform);
       }},
      {"test_harness_writes_test_timings_slowest_first",
       [] {
         const auto run = run_inner_suite({
             {"quick_pass", [] {}},
             {"quick_skip", [] { skip(SkipKind::platform, "macOS only"); }},
             {"slow_pass", [] { spin_for_milliseconds(60); }},
             {"quick_fail", [] { throw std::runtime_error("broke"); }},
         });
         CHECK(run.exit_code == 1);
         CHECK(contains(run.out, "[SUMMARY] executed 4, passed 2, failed 1, skipped 1 (platform 1) in "));
         CHECK(contains(run.out, " s\n  slowest: slow_pass 0."));
         const auto lines = split_lines(read_artifact("timings-harness_selftest.tsv"));
         CHECK(lines.size() == 5);
         CHECK(lines[0] == "test\tseconds\tresult");
         CHECK(lines[1].starts_with("slow_pass\t0.") && lines[1].ends_with("\tpass"));
         std::string results;
         for (std::size_t index = 2; index < lines.size(); ++index) {
           results += lines[index].substr(lines[index].rfind('\t') + 1) + " ";
         }
         CHECK(contains(results, "pass ") && contains(results, "skip ") && contains(results, "fail "));
       }},
      {"test_harness_timing_log_keeps_a_timeout_and_sorts_on_finish",
       [] {
         TestTimingLog log("harness_selftest_timeout");
         log.begin("stuck_test");
         log.record_timeout();
         // What a killed or timed-out run leaves: the rows so far, in run order.
         const auto partial = split_lines(read_artifact("timings-harness_selftest_timeout.tsv"));
         CHECK(partial.size() == 2);
         CHECK(partial[1].starts_with("stuck_test\t") && partial[1].ends_with("\ttimeout"));
         log.record({"two_seconds", 2.0, "pass"});
         log.record({"five_seconds", 5.0, "fail"});
         const auto sorted = log.finish();
         CHECK(sorted.size() == 3 && sorted[0].name == "five_seconds" && sorted[2].name == "stuck_test");
         CHECK(TestTimingLog::slowest_text(sorted, 2) == "five_seconds 5.0 s, two_seconds 2.0 s");
         const auto lines = split_lines(read_artifact("timings-harness_selftest_timeout.tsv"));
         CHECK(lines.size() == 4);
         CHECK(lines[1] == "five_seconds\t5.000\tfail");
         CHECK(lines[2] == "two_seconds\t2.000\tpass");
       }},
      {"test_harness_require_fixtures_fails_fixture_skips",
       [] {
         const ScopedEnvironment require("PATCHY_REQUIRE_FIXTURES", "1");
         const auto run = run_inner_suite({
             {"lost_fixture", [] { skip_unless_exists(local_fixtures_root() / "no-such-folder" / "absent.psd"); }},
             {"named_fixture", [] { skip("optional fixture missing: local-test-fixtures/dds"); }},
             {"platform_skip", [] { skip(SkipKind::platform, "macOS only"); }},
         });
         CHECK(run.exit_code == 1);
         CHECK(contains(run.err, "[FAIL] lost_fixture: required fixture missing (PATCHY_REQUIRE_FIXTURES=1): "));
         CHECK(contains(run.err, "[FAIL] named_fixture: required fixture missing (PATCHY_REQUIRE_FIXTURES=1): "));
         CHECK(contains(run.out, "[SKIP] platform_skip: macOS only"));
       }},
      {"test_harness_local_fixtures_dir_and_time_scale_overrides",
       [] {
         const auto root_before = local_fixtures_root();
         {
           const ScopedEnvironment root("PATCHY_LOCAL_FIXTURES_DIR", "patchy-selftest-fixtures");
           CHECK(local_fixtures_overridden());
           CHECK(local_fixtures_root() == std::filesystem::path("patchy-selftest-fixtures"));
           CHECK(local_psd_fixture_path("a.psd") == std::filesystem::path("patchy-selftest-fixtures") / "psd" / "a.psd");
         }
         CHECK(local_fixtures_root() == root_before);
         {
           const ScopedEnvironment scale("PATCHY_TEST_TIME_SCALE", "2.5");
           CHECK(time_budget_ms(1000) == 2500);
         }
       }},
  };
}

}  // namespace patchy::test::selftest
