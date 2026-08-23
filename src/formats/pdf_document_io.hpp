#pragma once

#include "core/document.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

// Reads a PDF page into an editable Patchy document: paths become shape layers,
// text-showing operators become text layers, and images become smart objects.
// Qt-free, like the SVG and Affinity readers it follows.
//
// Two things are finished on the Qt side, because they need font and image
// machinery this library deliberately does not link:
//   - text layers carry patchy.text.* metadata plus kLayerMetadataPdfPendingText,
//     rendered by MainWindow::render_pending_pdf_text_layers;
//   - image layers carry their bytes in the document's SmartObjectStore plus
//     kLayerMetadataPdfPendingImage, rendered by MainWindow::render_pending_pdf_images.
//
// The rasterizing importer in src/ui/pdf_import.* is the other half of the feature:
// this one keeps structure, that one keeps fidelity, and the Import PDF dialog
// chooses. Anything this reader cannot model is reported through `notices` so the
// caller can offer the raster path instead.

namespace patchy::pdf {

struct VectorReadOptions {
  // 0-based page index.
  int page{0};
  // Document pixels per PDF point. 72 points to the inch, so 1.0 is 72 ppi and
  // 300 ppi is 300/72. Shapes and text stay resolution-independent; this only sets
  // the canvas size and the scale their coordinates are baked at.
  double pixels_per_point{1.0};
  // Drop layers that fall entirely outside the page. Producers routinely park
  // artwork off-canvas, and importing it as hundreds of invisible layers helps
  // nobody.
  bool discard_offscreen{true};
  // Tried as the user and then the owner password. Empty works for the common
  // owner-locked files every viewer opens without prompting.
  std::string password;
};

struct VectorReadResult {
  Document document;
  std::vector<std::string> notices;
  // Counts for the caller's summary and for deciding whether the editable import
  // actually produced anything worth keeping.
  int shape_layers{0};
  int text_layers{0};
  int image_layers{0};
  // Set when the page held content the reader could not model at all (a shading, a
  // tiling pattern), so the caller can suggest rasterizing instead.
  bool has_unmodelled_content{false};
};

// Reads one page. Throws std::runtime_error when the file is not a readable PDF,
// the page index is out of range, or the file is encrypted.
[[nodiscard]] VectorReadResult read_page_as_vectors(std::span<const std::uint8_t> bytes,
                                                    const VectorReadOptions& options);

// Page count without building any document, for the page picker.
[[nodiscard]] int page_count(std::span<const std::uint8_t> bytes);

// Page size in document pixels at the given scale, for the picker's size label.
[[nodiscard]] std::array<int, 2> page_size_in_pixels(std::span<const std::uint8_t> bytes, int page,
                                                     double pixels_per_point);

[[nodiscard]] std::vector<std::string> pdf_extensions();
[[nodiscard]] bool sniff(std::span<const std::uint8_t> bytes);

}  // namespace patchy::pdf
