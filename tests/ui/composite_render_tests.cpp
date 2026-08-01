// Byte-identity corpus for the QImage render path (render_document_rect and
// friends), the UI-side twin of tests/core/composite_corpus_tests.cpp. Renders
// every committed PSD fixture plus local-test-fixtures/composite-corpus
// documents single-threaded in both alpha modes and compares FNV-1a digests
// against a machine-local baseline. A digest change means rendered bytes
// changed, not just speed. Re-pin deliberately by deleting the baseline file
// and rerunning.

#include "local_psd_fixtures.hpp"
#include "test_harness.hpp"
#include "ui_test_groups.hpp"

#include "core/document.hpp"
#include "core/layer.hpp"
#include "psd/psd_document_io.hpp"
#include "ui/image_document_io.hpp"

#include <QImage>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace {

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

std::uint64_t fnv1a_hash(std::uint64_t hash, std::span<const std::uint8_t> bytes) {
  for (const auto byte : bytes) {
    hash ^= byte;
    hash *= 1099511628211ULL;
  }
  return hash;
}

// Hashes row payloads only: QImage scanlines are padded to 4-byte boundaries
// and the padding bytes are uninitialized for RGB888.
std::uint64_t image_digest(const QImage& image) {
  auto hash = 14695981039346656037ULL;
  const auto row_bytes =
      static_cast<std::size_t>(image.width()) * static_cast<std::size_t>(image.depth() / 8);
  for (int y = 0; y < image.height(); ++y) {
    hash = fnv1a_hash(hash, std::span<const std::uint8_t>(image.constScanLine(y), row_bytes));
  }
  return hash;
}

std::string digest_hex(std::uint64_t digest) {
  char buffer[17] = {};
  std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(digest));
  return buffer;
}

// Baseline line format: "<rgba digest> <rgb digest> <file name>".
std::map<std::string, std::string> read_baseline(const std::filesystem::path& path) {
  std::map<std::string, std::string> baseline;
  std::ifstream in(path);
  std::string line;
  while (std::getline(in, line)) {
    // Tolerate a CRLF baseline read without text-mode translation (see the
    // core corpus test: a '\r' glued to the name fails every lookup).
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    const auto second_space = line.find(' ', line.find(' ') + 1);
    if (second_space == std::string::npos || second_space + 1 >= line.size()) {
      continue;
    }
    baseline[line.substr(second_space + 1)] = line.substr(0, second_space);
  }
  return baseline;
}

void composite_corpus_render_digests_are_stable() {
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
    const auto rgba = patchy::ui::qimage_from_document(*document, true);
    const auto rgb = patchy::ui::qimage_from_document(*document, false);
    digests[file.filename().string()] = digest_hex(image_digest(rgba)) + ' ' + digest_hex(image_digest(rgb));
  }
  CHECK(!digests.empty());

  const auto baseline_path =
      patchy::test::source_root_path() / "local-test-fixtures" / "composite-corpus" / "render-digests.txt";
  if (!std::filesystem::exists(baseline_path)) {
    std::filesystem::create_directories(baseline_path.parent_path());
    std::ofstream out(baseline_path, std::ios::trunc);
    for (const auto& [name, hex] : digests) {
      out << hex << ' ' << name << '\n';
    }
    std::cout << "[INFO] wrote render corpus baseline (" << digests.size()
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
      std::cerr << "[DIGEST] rendered bytes changed: " << name << " baseline=" << pinned->second
                << " actual=" << hex << '\n';
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

patchy::PixelBuffer solid_rgba(std::int32_t width, std::int32_t height, std::uint8_t red, std::uint8_t green,
                               std::uint8_t blue, std::uint8_t alpha) {
  patchy::PixelBuffer pixels(width, height, patchy::PixelFormat::rgba8());
  auto data = pixels.data();
  for (std::size_t index = 0; index + 3 < data.size(); index += 4) {
    data[index] = red;
    data[index + 1] = green;
    data[index + 2] = blue;
    data[index + 3] = alpha;
  }
  return pixels;
}

patchy::PixelBuffer solid_rgb(std::int32_t width, std::int32_t height, std::uint8_t red, std::uint8_t green,
                              std::uint8_t blue) {
  patchy::PixelBuffer pixels(width, height, patchy::PixelFormat::rgb8());
  auto data = pixels.data();
  for (std::size_t index = 0; index + 2 < data.size(); index += 3) {
    data[index] = red;
    data[index + 1] = green;
    data[index + 2] = blue;
  }
  return pixels;
}

// Rendering with a LayerBoundsOverride (the move/transform preview path) must
// produce the same bytes as rendering a document whose layer actually sits at
// the overridden bounds. Guards the override-aware isolated-group bounds
// (layer_render_bounds_for_render): the group here isolates (non-PassThrough,
// group opacity), the moved child carries a sized drop shadow so effect
// padding participates, and the override rect is disjoint from the original
// so a buffer sized to the wrong rect clips content and fails the compare.
void group_isolation_override_bounds_match_actual_layer_move() {
  patchy::Document document(300, 200, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Backdrop", solid_rgb(300, 200, 90, 120, 150));

  patchy::Layer group(document.allocate_layer_id(), "Group", patchy::LayerKind::Group);
  group.set_blend_mode(patchy::BlendMode::Normal);  // non-pass-through: isolates
  group.set_opacity(0.6F);

  patchy::Layer moved(document.allocate_layer_id(), "Moved", solid_rgba(60, 40, 255, 64, 32, 200));
  const auto moved_id = moved.id();
  moved.set_bounds(patchy::Rect{20, 20, 60, 40});
  moved.set_blend_mode(patchy::BlendMode::Multiply);
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.blend_mode = patchy::BlendMode::Normal;
  shadow.color = patchy::RgbColor{0, 0, 0};
  shadow.opacity = 1.0F;
  shadow.angle_degrees = 135.0F;
  shadow.distance = 4.0F;
  shadow.size = 6.0F;
  moved.layer_style().drop_shadows.push_back(shadow);
  group.add_child(std::move(moved));

  patchy::Layer sibling(document.allocate_layer_id(), "Sibling", solid_rgba(50, 50, 32, 200, 96, 180));
  sibling.set_bounds(patchy::Rect{60, 30, 50, 50});
  sibling.set_blend_mode(patchy::BlendMode::Screen);
  group.add_child(std::move(sibling));

  document.add_layer(std::move(group));

  const patchy::Rect override_bounds{190, 120, 60, 40};
  const auto override_rgba = patchy::ui::qimage_from_document_rect_with_layer_bounds(
      document, QRect(0, 0, 300, 200), true, moved_id, override_bounds);
  const auto override_rgb = patchy::ui::qimage_from_document_rect_with_layer_bounds(
      document, QRect(0, 0, 300, 200), false, moved_id, override_bounds);

  auto reference = document;
  auto* moved_layer = reference.find_layer(moved_id);
  CHECK(moved_layer != nullptr);
  moved_layer->set_bounds(override_bounds);
  const auto actual_rgba = patchy::ui::qimage_from_document(reference, true);
  const auto actual_rgb = patchy::ui::qimage_from_document(reference, false);

  CHECK(image_digest(override_rgba) == image_digest(actual_rgba));
  CHECK(image_digest(override_rgb) == image_digest(actual_rgb));
}

}  // namespace

std::vector<patchy::test::TestCase> composite_render_tests() {
  return {
      {"composite_corpus_render_digests_are_stable", composite_corpus_render_digests_are_stable},
      {"group_isolation_override_bounds_match_actual_layer_move",
       group_isolation_override_bounds_match_actual_layer_move},
  };
}
