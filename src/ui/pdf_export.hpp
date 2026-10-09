#pragma once

#include "core/document.hpp"
#include "formats/pdf_image_writer.hpp"
#include "support/atomic_file_write.hpp"

#include <QFile>
#include <QPageSize>
#include <QString>

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

class QImage;
class QPainter;
class QPdfWriter;

namespace patchy::ui {

// Single-page PDF export sized to the document itself, not to a sheet of paper: the
// page is pixels / document PPI inches per axis, matching Photoshop's Save As PDF.
// The paper-relative flow (page layout, margins, crop marks, scale-to-fit) stays in
// print_dialog.hpp's write_print_pdf.
//
// Image data. A page that is one image (flat mode always, editable mode when the page
// holds a single pixel layer) goes through Patchy's own writer (formats/pdf_image_writer):
// Flate when `lossless`, else JPEG at `jpeg_quality`, in one channel when the pixels are
// gray. A page with real vector or text objects goes through Qt's PDF engine, which
// knows only Flate RGB (lossless) or JPEG at a fixed quality 94 and ignores the quality.
// The struct default is lossless because scripts and the command line must not degrade
// pixels unasked; the dialogs default to the "high" preset (pdf_image_quality_presets).
struct PdfExportOptions {
  bool lossless{true};
  // Keep layers as editable objects instead of one flattened image: shape layers become
  // PDF paths, text layers real text with embedded fonts, pixel and smart-object layers
  // images. What Qt's PDF engine cannot composite per object (blend modes, adjustment
  // layers, group opacity, raster masks on vectors, layer styles) flattens into an
  // image chunk with a notice, so the page can look different from the canvas.
  bool editable_layers{false};
  // Editable mode only: a text layer whose font is not installed is embedded as its pixels
  // instead of being drawn as real text in a substitute face (the default keeps it text,
  // with a notice naming the missing font). Persists as saveOptions/pdfMissingFontsAsImages.
  bool missing_fonts_as_images{false};
  // JPEG quality 1..100 for the lossy choice. New fields go after the three bools:
  // callers aggregate-initialize them positionally.
  int jpeg_quality{90};
  // Write one-channel image data when every visible pixel is gray (R == G == B).
  bool auto_grayscale{true};
  // A page imported from a PDF that was one image, and whose composite still hashes to
  // what the import produced, is written with that image's ORIGINAL encoded bytes
  // (DocumentMetadata::pdf_source_page): no re-encode, no quality loss, the source's
  // size. Off forces every page through the chosen quality. Persists as
  // saveOptions/pdfKeepOriginalImages.
  bool keep_original_image_data{true};
};

// The "did this page change since import" fingerprint: the size and RGBA bytes of
// flat_export_qimage(document, true). Import stamps it, export compares it, so an undo
// back to the imported pixels passes through again and any visible edit does not.
[[nodiscard]] std::uint64_t pdf_composite_hash(const QImage& composite);
// Stamps the document's size, resolution, and composite hash onto `source` (a
// PageProbe::source_page) and stores it as the document's pdf_source_page.
void attach_pdf_source_page(Document& document, const std::shared_ptr<const PdfSourcePage>& source);
// True when the document still is what its pdf_source_page stands for. `composite` is
// flat_export_qimage(document, true), passed in because the caller already has it.
[[nodiscard]] bool document_matches_pdf_source(const Document& document, const QImage& composite);

// The image-quality choices the PDF dialogs and app.exportPdf offer. The ids are
// persisted (saveOptions/pdfImageQuality) and scripted, so they never change.
struct PdfImageQualityPreset {
  const char* id;
  bool lossless;
  int jpeg_quality;
};
[[nodiscard]] std::span<const PdfImageQualityPreset> pdf_image_quality_presets();
inline constexpr const char* kDefaultPdfImageQualityId = "high";
// Sets lossless and jpeg_quality from a preset id; false (options untouched) when the
// id is unknown.
bool apply_pdf_image_quality(const QString& id, PdfExportOptions& options);
// The preset an option pair corresponds to (lossless wins; else the nearest quality).
[[nodiscard]] QString pdf_image_quality_id(bool lossless, int jpeg_quality);
// The stored dialog preference. saveOptions/pdfLossless is older and was rewritten to
// true after every flat save of any format, so only a stored FALSE there says anything
// (and it says "JPEG"); with the new key absent the answer is the default preset.
[[nodiscard]] QString stored_pdf_image_quality_id();
void store_pdf_image_quality_id(const QString& id);

// Writes a one-page PDF of the document. Flat mode holds the flattened composite
// (document alpha becomes a PDF /SMask); editable mode walks the layer stack (see
// pdf_export_editable.cpp). `notices` receives one line per structural loss in editable
// mode. Throws std::runtime_error when the file cannot be written. Both writers go
// through a sibling temporary file, so a failed export never truncates an existing
// file at `path` (AGENTS.md, docs/document-recovery.md).
void write_pdf_document_file(const Document& document, const QString& path, const PdfExportOptions& options = {},
                             std::vector<std::string>* notices = nullptr);

// A multi-page PDF, one page per document in order, each page sized from its own
// pixels and PPI exactly as the single-page writer sizes its page (File > Export
// Multi-Page PDF and app.exportPdf). Flat or editable per `options`, the same way per
// page; editable-mode losses land in `notices`. `progress` is called before each page
// (1-based page, page count); returning false cancels: the partial file is removed, an
// existing file at `path` is left as it was, and the function returns false. Returns
// true when the file was written. Throws std::runtime_error on an empty list, a null
// or empty document, or a file that cannot be written.
using PdfPageProgress = std::function<bool(int page, int page_count)>;
bool write_multipage_pdf_file(std::span<const Document* const> pages, const QString& path,
                              const PdfExportOptions& options = {}, std::vector<std::string>* notices = nullptr,
                              const PdfPageProgress& progress = {});

// "Print a folder as a page": one copy of the document per visible top-level layer
// group, with every other top-level layer hidden except, when
// `include_ungrouped_layers`, the non-group root layers (a shared background). Top of
// the layer stack first, so groups named Page 1, Page 2, ... in the panel come out in
// reading order. Empty when the document has no visible top-level group.
[[nodiscard]] std::vector<Document> documents_for_top_level_groups(const Document& document,
                                                                   bool include_ungrouped_layers);

namespace pdf_detail {
// The Qt engine's side of the atomic-write rule: QPdfWriter draws onto device(), a
// sibling temporary file, and commit() replaces the destination only once the whole
// file is on disk and, when `merge_text`, the text-merge pass has rewritten it.
// Destroyed without commit() (a cancel, a throw), the temporary is removed and the
// destination keeps what it held. Declare it before the QPdfWriter and QPainter that
// use device(), so they are gone before the file closes.
class QtPdfOutput {
public:
  // Throws std::runtime_error when the temporary file cannot be created.
  explicit QtPdfOutput(const QString& path);
  [[nodiscard]] QIODevice* device() noexcept { return &file_; }
  // Call once the painter on device() has ended. Throws std::runtime_error when a write
  // to the temporary failed or the rename does.
  void commit(bool merge_text);

private:
  // Declared before file_ so the file closes before the temporary is removed.
  AtomicFileReplacement replacement_;
  QFile file_;
};
// The page a document exports to: pixels / PPI inches per axis, exact match, shrunk
// to the 14400 pt cap when larger.
[[nodiscard]] QPageSize document_page_size(const Document& document);
// Page sized from the document (pixels / PPI inches per axis, exact-match size, zero
// margins, the 14400 pt cap) and the device resolution pinned to the document PPI so the
// painter's logical grid is one unit per document pixel. Shared by both export modes.
void configure_document_page(QPdfWriter& writer, const Document& document);
// The editable-layers writer.
void write_editable_pdf_document_file(const Document& document, const QString& path, const PdfExportOptions& options,
                                      std::vector<std::string>* notices);
// The editable walk onto a painter that is already begun on a PDF device: sets the
// window to the document's pixel grid and draws every layer. One page's worth; the
// multi-page writer calls it per page.
// `text_drawn` (optional) is set, never cleared, when a layer went out as real text.
void paint_editable_document(QPainter& painter, const Document& document, const PdfExportOptions& options,
                             std::vector<std::string>* notices, bool* text_drawn = nullptr);
// True when the document's visible content is exactly one pixel layer (no shape, text,
// or second layer anywhere): an editable export of it could only ever be one image, so
// it takes the image writer and its codecs instead of Qt's engine.
[[nodiscard]] bool document_is_single_raster_layer(const Document& document);
// One page for the image writer: the composite encoded per `options` (gray detection,
// JPEG or Flate, an /SMask only when some pixel is not opaque), sized like
// document_page_size. Safe on a worker thread.
[[nodiscard]] pdf::ImagePage encode_page_image(const QImage& composite, double width_points, double height_points,
                                               const PdfExportOptions& options);
// The page an unchanged import writes: the source's bytes, page box, /Rotate, and placement.
[[nodiscard]] pdf::ImagePage source_image_page(const PdfSourcePage& source);
// The glyph-run merge (formats/pdf_text_merge.hpp) over a file Qt just wrote, so
// importers see words rather than one object per letter. A file the pass cannot
// handle is left as written. Run after an editable export that drew text, on the
// temporary file (QtPdfOutput::commit), never on the destination: it rewrites in place.
void apply_text_merge_post_pass(const QString& path);
}  // namespace pdf_detail

}  // namespace patchy::ui
