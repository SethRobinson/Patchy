#pragma once

// Shared machinery for the byte-identity corpus tests
// (tests/core/composite_corpus_tests.cpp and tests/ui/composite_render_tests.cpp).
//
// Each corpus directory carries its own baseline file next to its PSDs:
//   test-fixtures/psd/<kind>-digests.txt              tracked in git; covers the
//                                                     committed fixtures on every
//                                                     machine, remote snapshots included
//   local-test-fixtures/composite-corpus/<kind>-digests.txt
//                                                     untracked overlay for documents
//                                                     dropped into that directory
// A missing baseline is written on the first run (the local overlay's goes under
// test-artifacts instead while PATCHY_LOCAL_FIXTURES_DIR names another checkout's
// fixtures). To add a committed fixture or
// re-pin after a deliberate rendering change, delete the tracked file, rerun the
// test, review the git diff, and commit it (or copy the .actual file the failing
// run writes under test-artifacts/composite-corpus). Never re-pin per machine:
// the CPU compositor is byte-identical across Windows, macOS, Linux, and wasm.

#include "local_psd_fixtures.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace patchy::test::corpus {

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
  ScopedSingleThreadedRender(const ScopedSingleThreadedRender&) = delete;
  ScopedSingleThreadedRender& operator=(const ScopedSingleThreadedRender&) = delete;
};

struct CorpusDirectory {
  std::filesystem::path dir;
  // Committed fixtures must decode and their baseline is tracked; the local
  // overlay may skip unreadable files and its baseline stays machine-local.
  bool committed = false;
  const char* tag = "";
};

inline std::vector<CorpusDirectory> corpus_directories() {
  return {
      {source_root_path() / "test-fixtures" / "psd", true, "committed"},
      {local_fixtures_root() / "composite-corpus", false, "local"},
  };
}

inline std::vector<std::filesystem::path> psd_files_in(const std::filesystem::path& directory) {
  std::vector<std::filesystem::path> files;
  if (!std::filesystem::exists(directory)) {
    return files;
  }
  for (const auto& entry : std::filesystem::directory_iterator(directory)) {
    if (entry.is_regular_file() && entry.path().extension() == ".psd") {
      files.push_back(entry.path());
    }
  }
  std::sort(files.begin(), files.end(), [](const std::filesystem::path& lhs, const std::filesystem::path& rhs) {
    return lhs.filename().string() < rhs.filename().string();
  });
  return files;
}

inline std::string digest_hex(std::uint64_t digest) {
  char buffer[17] = {};
  std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(digest));
  return buffer;
}

inline std::filesystem::path baseline_path(const CorpusDirectory& directory, std::string_view kind) {
  return directory.dir / (std::string(kind) + "-digests.txt");
}

// Baseline line format: "<digest>[ <digest>...] <file name>" with digest_columns
// space-separated digest fields; the file name may itself contain spaces.
inline std::map<std::string, std::string> read_baseline(const std::filesystem::path& path, int digest_columns) {
  std::map<std::string, std::string> baseline;
  std::ifstream in(path);
  std::string line;
  while (std::getline(in, line)) {
    // The reader tolerates a CRLF file (older overlays were written in text
    // mode on Windows) because the wasm/musl and Linux runners read without
    // text-mode translation, and a '\r' glued to the name fails every lookup.
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    std::size_t split = 0;
    for (int column = 0; column < digest_columns; ++column) {
      split = line.find(' ', split);
      if (split == std::string::npos) {
        break;
      }
      ++split;
    }
    if (split == std::string::npos || split == 0 || split >= line.size()) {
      continue;
    }
    baseline[line.substr(split)] = line.substr(0, split - 1);
  }
  return baseline;
}

// Written in binary mode so every platform produces LF line endings: the
// tracked baseline is normalized to LF by .gitattributes and a re-pin must
// diff cleanly.
inline void write_baseline(const std::filesystem::path& path, const std::map<std::string, std::string>& digests) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  for (const auto& [name, hex] : digests) {
    out << hex << ' ' << name << '\n';
  }
}

// Compares one directory's digests against its baseline and returns the number
// of problems. A missing baseline is written and counts as zero problems. On any
// problem the actual digests are also written under test-artifacts so a
// deliberate re-pin is a copy.
inline std::filesystem::path actual_digests_path(const CorpusDirectory& directory, std::string_view kind) {
  return std::filesystem::path("test-artifacts") / "composite-corpus" /
         (std::string(directory.tag) + '-' + std::string(kind) + "-digests.txt");
}

inline int compare_corpus_baseline(std::string_view kind, const CorpusDirectory& directory, int digest_columns,
                                   const std::map<std::string, std::string>& digests) {
  const auto path = baseline_path(directory, kind);
  if (!std::filesystem::exists(path)) {
    if (!directory.committed && local_fixtures_overridden()) {
      // PATCHY_LOCAL_FIXTURES_DIR points at another checkout's fixtures: never write there.
      const auto actual = actual_digests_path(directory, kind);
      write_baseline(actual, digests);
      std::cout << "[INFO] no " << directory.tag << ' ' << kind << " corpus baseline in PATCHY_LOCAL_FIXTURES_DIR; "
                << "digests written to " << actual.string() << " (copy to " << path.string() << " to pin them)\n";
      return 0;
    }
    write_baseline(path, digests);
    std::cout << "[INFO] wrote " << directory.tag << ' ' << kind << " corpus baseline (" << digests.size()
              << " documents): " << path.string() << '\n';
    return 0;
  }

  const auto baseline = read_baseline(path, digest_columns);
  int problems = 0;
  for (const auto& [name, hex] : digests) {
    const auto pinned = baseline.find(name);
    if (pinned == baseline.end()) {
      std::cerr << "[DIGEST] new document not in " << directory.tag << " baseline (re-pin deliberately): " << name
                << '\n';
      ++problems;
    } else if (pinned->second != hex) {
      std::cerr << "[DIGEST] " << kind << " bytes changed: " << name << " baseline=" << pinned->second
                << " actual=" << hex << '\n';
      ++problems;
    }
  }
  const auto committed_dir = source_root_path() / "test-fixtures" / "psd";
  for (const auto& [name, hex] : baseline) {
    if (digests.find(name) != digests.end()) {
      continue;
    }
    // Overlays written before the committed baseline moved into test-fixtures/psd
    // still list every committed fixture; those entries are stale, not missing.
    if (!directory.committed && std::filesystem::exists(committed_dir / name)) {
      std::cout << "[INFO] stale overlay entry (now pinned in test-fixtures/psd): " << name << '\n';
      continue;
    }
    std::cerr << "[DIGEST] " << directory.tag << " baseline document missing from corpus: " << name << '\n';
    ++problems;
  }
  if (problems > 0) {
    const auto actual = actual_digests_path(directory, kind);
    write_baseline(actual, digests);
    std::cerr << "[DIGEST] actual " << directory.tag << ' ' << kind << " digests written to " << actual.string()
              << " (copy over " << path.string() << " to re-pin deliberately)\n";
  }
  return problems;
}

}  // namespace patchy::test::corpus
