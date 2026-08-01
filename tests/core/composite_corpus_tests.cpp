// Byte-identity corpus for the CPU compositor. Flattens every committed PSD
// fixture (test-fixtures/psd) plus any documents dropped into
// local-test-fixtures/composite-corpus, single-threaded, and compares FNV-1a
// digests against a machine-local baseline. The baseline pins compositor
// OUTPUT BYTES across optimization work: a digest change means rendering
// changed, not just speed. The baseline lives in local-test-fixtures (never
// committed) because digests cover machine-local corpus files; the first run
// on a machine writes it. After a deliberate rendering change, re-pin by
// deleting the baseline file and rerunning.

#include "core_test_support.hpp"
#include "local_psd_fixtures.hpp"
#include "test_groups.hpp"
#include "test_harness.hpp"

#include "psd/psd_document_io.hpp"
#include "render/compositor.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

// The digest run must be byte-stable, and only the sequential compositor walk
// is (the strip-parallel path has a documented divergence class near styled
// strip boundaries).
struct ScopedSingleThreadedRender {
  ScopedSingleThreadedRender() {
#ifdef _WIN32
    _putenv_s("PATCHY_RENDER_SINGLE_THREADED", "1");
#else
    setenv("PATCHY_RENDER_SINGLE_THREADED", "1", 1);
#endif
  }
  ~ScopedSingleThreadedRender() {
#ifdef _WIN32
    _putenv_s("PATCHY_RENDER_SINGLE_THREADED", "");
#else
    unsetenv("PATCHY_RENDER_SINGLE_THREADED");
#endif
  }
};

std::vector<std::filesystem::path> corpus_documents() {
  std::vector<std::filesystem::path> files;
  const auto add_from = [&files](const std::filesystem::path& directory) {
    if (!std::filesystem::exists(directory)) {
      return;
    }
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
      if (entry.is_regular_file() && entry.path().extension() == ".psd") {
        files.push_back(entry.path());
      }
    }
  };
  add_from(patchy::test::source_root_path() / "test-fixtures" / "psd");
  add_from(patchy::test::source_root_path() / "local-test-fixtures" / "composite-corpus");
  std::sort(files.begin(), files.end(), [](const std::filesystem::path& lhs, const std::filesystem::path& rhs) {
    return lhs.filename().string() < rhs.filename().string();
  });
  return files;
}

std::string digest_hex(std::uint64_t digest) {
  char buffer[17] = {};
  std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(digest));
  return buffer;
}

// Baseline line format: "<hex digest> <file name>" (name may contain spaces).
std::map<std::string, std::string> read_baseline(const std::filesystem::path& path) {
  std::map<std::string, std::string> baseline;
  std::ifstream in(path);
  std::string line;
  while (std::getline(in, line)) {
    const auto space = line.find(' ');
    if (space == std::string::npos || space == 0 || space + 1 >= line.size()) {
      continue;
    }
    baseline[line.substr(space + 1)] = line.substr(0, space);
  }
  return baseline;
}

void write_baseline(const std::filesystem::path& path, const std::map<std::string, std::string>& digests) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::trunc);
  for (const auto& [name, hex] : digests) {
    out << hex << ' ' << name << '\n';
  }
}

void composite_corpus_flatten_digests_are_stable() {
  const auto files = corpus_documents();
  if (files.empty()) {
    std::cout << "[SKIP] no corpus documents found\n";
    return;
  }
  ScopedSingleThreadedRender single_threaded;
  std::map<std::string, std::string> digests;
  for (const auto& file : files) {
    std::optional<patchy::Document> document;
    try {
      document.emplace(patchy::psd::DocumentIo::read_file(file));
    } catch (const std::exception& error) {
      std::cout << "[INFO] skipping unreadable " << file.filename().string() << ": " << error.what() << '\n';
      continue;
    }
    const auto flattened = patchy::Compositor{}.flatten_rgb8(*document);
    digests[file.filename().string()] = digest_hex(patchy::test::fnv1a_hash_bytes(flattened.data()));
  }
  CHECK(!digests.empty());

  const auto baseline_path =
      patchy::test::source_root_path() / "local-test-fixtures" / "composite-corpus" / "flatten-digests.txt";
  if (!std::filesystem::exists(baseline_path)) {
    write_baseline(baseline_path, digests);
    std::cout << "[INFO] wrote composite corpus baseline (" << digests.size()
              << " documents): " << baseline_path.string() << '\n';
    return;
  }

  const auto baseline = read_baseline(baseline_path);
  int problems = 0;
  for (const auto& [name, hex] : digests) {
    const auto pinned = baseline.find(name);
    if (pinned == baseline.end()) {
      std::cerr << "[DIGEST] new document not in baseline (re-pin deliberately): " << name << '\n';
      ++problems;
    } else if (pinned->second != hex) {
      std::cerr << "[DIGEST] flatten bytes changed: " << name << " baseline=" << pinned->second << " actual=" << hex
                << '\n';
      ++problems;
    }
  }
  for (const auto& [name, hex] : baseline) {
    if (digests.find(name) == digests.end()) {
      std::cerr << "[DIGEST] baseline document missing from corpus: " << name << '\n';
      ++problems;
    }
  }
  CHECK(problems == 0);
}

}  // namespace

std::vector<patchy::test::TestCase> composite_corpus_tests() {
  return {
      {"composite_corpus_flatten_digests_are_stable", composite_corpus_flatten_digests_are_stable},
  };
}
