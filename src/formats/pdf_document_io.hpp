#pragma once

#include "core/document.hpp"
#include "core/pdf_source_page.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
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
//     kLayerMetadataPdfPendingImage, rendered by
//     MainWindow::render_pending_pdf_image_layers.
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
  // The Import PDF dialog's Grayscale mode: every colour the page paints (fills,
  // strokes, text, shading stops, image samples) becomes its gray through
  // core/gray_conversion.hpp. JPEG images are decoded and re-encoded as gray PNG, so
  // the smart object holds gray pixels too.
  bool grayscale{false};
  // Puts an opaque white "Background" pixel layer under the page's artwork, the paper a
  // PDF viewer shows (PDF itself paints no page background).
  bool white_background{false};
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

// What a page is made of, found without decoding any image (a full-page scan is tens
// of megabytes decoded, and this runs once per page of an import).
struct PageProbe {
  // Visible primitives. When an opaque image covers the whole page, only what is
  // painted AFTER it counts: a background rectangle under a scan is not content.
  int painted_paths{0};
  int text_runs{0};
  int shadings{0};
  int images{0};
  // Images in a codec the editable reader cannot decode (JPEG 2000, CCITT, JBIG2) and
  // the share of the page they cover, 0..1. A page that is mostly such an image has to
  // be rasterized: importing it "editable" would silently drop the scan.
  int undecodable_images{0};
  double undecodable_coverage{0.0};
  bool has_annotations{false};
  // Set when the page is exactly one opaque image covering the whole page whose encoded
  // stream an export can carry verbatim (see core/pdf_source_page.hpp). The document
  // fields and the composite hash are left for the importer, which owns the pixels.
  std::shared_ptr<const PdfSourcePage> source_page;

  // True when nothing but undecodable images is visible.
  [[nodiscard]] bool only_undecodable_images() const noexcept {
    return undecodable_images > 0 && undecodable_images == images && painted_paths == 0 && text_runs == 0 &&
           shadings == 0;
  }
};

class File;

// One opened PDF serving many page reads. Opening parses the cross-reference data and
// the page tree, and read_page_as_vectors below pays that (plus a copy of the whole
// file) on every call, which is what made an 85-page import quadratic in spirit.
// Not for concurrent use: the file's object caches are unlocked. One thread at a time,
// any thread.
class PageReader {
public:
  // Takes the file bytes. Throws std::runtime_error when they are not a readable PDF
  // or the password does not unlock them.
  explicit PageReader(std::vector<std::uint8_t> bytes, std::string_view password = {});
  ~PageReader();
  PageReader(PageReader&&) noexcept;
  PageReader& operator=(PageReader&&) noexcept;

  [[nodiscard]] int page_count() const noexcept;
  // As read_page_as_vectors; options.password is ignored (the file is already open).
  [[nodiscard]] VectorReadResult read_page(const VectorReadOptions& options) const;
  // Never throws for page content it cannot follow: an unreadable page probes as empty.
  [[nodiscard]] PageProbe probe_page(int page) const;

private:
  std::unique_ptr<File> file_;
  std::vector<std::string> open_notices_;
};

// Reads one page. Throws std::runtime_error when the file is not a readable PDF,
// the page index is out of range, or the file is encrypted. Opens the file for this
// one call; use a PageReader for more than one page.
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
