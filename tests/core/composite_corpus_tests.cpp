#include "core/document.hpp"
#include "formats/acv_curves_io.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/ico_document_io.hpp"
#include "formats/ilbm_document_io.hpp"
#include "formats/pcx_document_io.hpp"
#include "formats/tga_document_io.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iterator>
#include <limits>
#include <numeric>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>
// Byte-identity corpus for the CPU compositor. Flattens every committed PSD
// fixture (test-fixtures/psd) plus any documents dropped into
// local-test-fixtures/composite-corpus, single-threaded, and compares FNV-1a
// digests against per-directory baselines. The baseline pins compositor
// OUTPUT BYTES across optimization work: a digest change means rendering
// changed, not just speed. The committed fixtures' baseline
// (test-fixtures/psd/flatten-digests.txt) is tracked so every machine, remote
// snapshots included, checks the same bytes; the local corpus keeps an
// untracked overlay beside its files. See tests/composite_corpus_support.hpp
// for the re-pin procedure.
#include "composite_corpus_support.hpp"
#include "core_test_support.hpp"
#include "test_groups.hpp"
#include "test_harness.hpp"
#include "psd/psd_document_io.hpp"
#include "render/compositor.hpp"
#include <exception>
#include <iostream>
#include <map>
#include <optional>
#include <string>

namespace {

using namespace patchy::test::corpus;

void composite_corpus_flatten_digests_are_stable() {
  ScopedSingleThreadedRender single_threaded;
  int problems = 0;
  std::size_t documents = 0;
  for (const auto& directory : corpus_directories()) {
    const auto files = psd_files_in(directory.dir);
    if (files.empty()) {
      continue;
    }
    std::map<std::string, std::string> digests;
    for (const auto& file : files) {
      std::optional<patchy::Document> document;
      try {
        document.emplace(patchy::psd::DocumentIo::read_file(file));
      } catch (const std::exception& error) {
        if (directory.committed) {
          throw;
        }
        std::cout << "[INFO] skipping unreadable " << file.filename().string() << ": " << error.what() << '\n';
        continue;
      }
      const auto flattened = patchy::Compositor{}.flatten_rgb8(*document);
      digests[file.filename().string()] = digest_hex(patchy::test::fnv1a_hash_bytes(flattened.data()));
    }
    if (digests.empty()) {
      continue;
    }
    documents += digests.size();
    problems += compare_corpus_baseline("flatten", directory, 1, digests);
  }
  if (documents == 0) {
    patchy::test::skip("no corpus documents found");
  }
  CHECK(problems == 0);
}

}  // namespace

std::vector<patchy::test::TestCase> composite_corpus_tests() {
  return {
      {"composite_corpus_flatten_digests_are_stable", composite_corpus_flatten_digests_are_stable},
  };
}
