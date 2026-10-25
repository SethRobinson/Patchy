#pragma once

#include "core/document.hpp"
#include "ui/image_save_options.hpp"

#include <QColor>
#include <QImage>
#include <QRect>
#include <QRegion>
#include <QString>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace patchy::ui {

// Photoshop's convention for raster files that record no physical density: they open
// at 72 PPI. Import paths must use this, never Qt's screen-derived QImage default.
inline constexpr double kUntaggedImportPpi = 72.0;

struct RenderedDocumentPatch {
  QRect document_rect;
  QImage image;
};

[[nodiscard]] Document document_from_qimage(const QImage& image, std::string layer_name);
// Density the QImage's format handler explicitly recorded, or nullopt when the image
// still carries Qt's screen-derived constructor default (i.e. the file was untagged).
[[nodiscard]] std::optional<std::pair<double, double>> explicit_qimage_density_ppi(const QImage& image);
// Sets the document's print PPI from the source file's recorded density: an explicit
// PNG/JPEG density (formats::probe_image_density) wins with its exact values; a
// PNG/JPEG without one is untagged and gets kUntaggedImportPpi; any other container
// adopts the decoded QImage's dotsPerMeter only when the handler set a real value
// (explicit_qimage_density_ppi), else kUntaggedImportPpi.
void apply_imported_image_density(Document& document, std::span<const std::uint8_t> file_bytes,
                                  const QImage& image);
// If the document is a single flat pixel layer whose alpha channel carries a meaningful
// mask, move that alpha into an editable grayscale layer mask and make the layer pixels
// opaque RGB. Returns true when a mask was created. Multi-layer documents are left intact.
bool promote_flat_alpha_to_layer_mask(Document& document);
// An rgba8 PixelBuffer from any image (deep images narrow through Qt's conversion).
[[nodiscard]] PixelBuffer pixels_from_image_rgba(const QImage& image);
// An RGBA PixelBuffer at the image's own depth: 16-bit QImage formats give rgba16
// pixels, float formats rgbaf32 (their values taken as linear), everything else rgba8
// (docs/high-bit-depth.md). The inverse of qimage_from_pixel_buffer.
[[nodiscard]] PixelBuffer pixels_from_image_native(const QImage& image);
// pixels_from_image_native converted to `depth` (8-bit images widen exactly; deep
// images narrow by the document conversion rules).
[[nodiscard]] PixelBuffer pixels_from_image_at_depth(const QImage& image, BitDepth depth);
// While one is alive on this thread, renders of 16/32-bit documents return images at
// the document's depth (Format_RGBA64 / Format_RGBA32FPx4 with linear values) instead
// of the 8-bit display image, for callers that turn a render back into layer pixels.
class ScopedDocumentDepthRender {
public:
  ScopedDocumentDepthRender() noexcept;
  ~ScopedDocumentDepthRender();
  ScopedDocumentDepthRender(const ScopedDocumentDepthRender&) = delete;
  ScopedDocumentDepthRender& operator=(const ScopedDocumentDepthRender&) = delete;

private:
  bool previous_;
};
[[nodiscard]] QImage qimage_from_document(const Document& document, bool preserve_alpha);
// Renders one layer alone at the document's size, so opacity/blend/styles come out
// exactly as the compositor draws them against an empty backdrop (sprite-sheet and
// image-sequence exports share this).
[[nodiscard]] QImage render_layer_isolated(const Document& document, const Layer& layer);
[[nodiscard]] QImage qimage_from_document_rect(const Document& document, QRect document_rect, bool preserve_alpha);
// Native child coverage used to anchor paints in a clipped viewport render.
// The caller budgets the native silhouette and compositor workspace first.
[[nodiscard]] Rect group_visible_alpha_bounds(const Layer& group, Rect bounds, const PatternStore& patterns);
[[nodiscard]] std::vector<RenderedDocumentPatch> qimage_patches_from_document_region(const Document& document,
                                                                                     const QRegion& document_region,
                                                                                     bool preserve_alpha);
[[nodiscard]] QImage qimage_from_document_rect_with_layer_bounds(
    const Document& document, QRect document_rect, bool preserve_alpha,
    const std::vector<std::pair<LayerId, Rect>>& layer_bounds);
[[nodiscard]] std::vector<RenderedDocumentPatch> qimage_patches_from_document_region_with_layer_bounds(
    const Document& document, const QRegion& document_region, bool preserve_alpha,
    const std::vector<std::pair<LayerId, Rect>>& layer_bounds);
[[nodiscard]] QImage qimage_from_document_rect_with_layer_bounds(const Document& document, QRect document_rect,
                                                                 bool preserve_alpha, LayerId layer_id,
                                                                 Rect layer_bounds);
[[nodiscard]] QImage qimage_from_document_rect_with_layer_pixels(const Document& document, QRect document_rect,
                                                                 bool preserve_alpha, LayerId layer_id,
                                                                 const PixelBuffer& layer_pixels, Rect layer_bounds);
[[nodiscard]] std::vector<RenderedDocumentPatch> qimage_patches_from_document_region_with_layer_pixels(
    const Document& document, const QRegion& document_region, bool preserve_alpha, LayerId layer_id,
    const PixelBuffer& layer_pixels, Rect layer_bounds);
// One-rect form rendered in horizontal bands across workers. PREVIEW-ONLY
// (live Free Transform drags): band-windowed style blurs can differ ~1-2/255
// from the unbanded render, so the result must never reach a commit or the
// render cache.
[[nodiscard]] std::vector<RenderedDocumentPatch> qimage_patch_from_document_rect_with_layer_pixels_banded(
    const Document& document, QRect document_rect, bool preserve_alpha, LayerId layer_id,
    const PixelBuffer& layer_pixels, Rect layer_bounds);
// N-layer variant of the pixel-substituting region render (multi-target Free
// Transform preview). Same override semantics per entry: the mask, when
// present, stays at its document position. Pixel pointers must outlive the call.
struct LayerPixelsOverrideSpec {
  LayerId layer_id{};
  Rect bounds{};
  const PixelBuffer* pixels{nullptr};
};
[[nodiscard]] std::vector<RenderedDocumentPatch> qimage_patches_from_document_region_with_layer_pixel_overrides(
    const Document& document, const QRegion& document_region, bool preserve_alpha,
    const std::vector<LayerPixelsOverrideSpec>& layer_overrides);
[[nodiscard]] QImage qimage_from_document_rect_with_hidden_layers(
    const Document& document, QRect document_rect, bool preserve_alpha,
    const std::vector<LayerId>& hidden_layer_ids);
// PREVIEW-ONLY: renders in horizontal bands across workers so a
// small-but-expensive rect does not serialize under the 4 Mpx strip gate.
// Bands window the style-mask blurs, so bytes can differ from the unbanded
// render by ~1-2/255 near styled layers; never feed the result into a commit
// or render-cache patch path.
[[nodiscard]] QImage qimage_from_document_rect_with_hidden_layers_banded(
    const Document& document, QRect document_rect, bool preserve_alpha,
    const std::vector<LayerId>& hidden_layer_ids);
// Same PREVIEW-ONLY banded render with bounds overrides on top of the hidden
// set: the Move proxy snapshot shifts a moving set that hangs off the canvas
// back onto it, because the compositor clips every render to the canvas.
[[nodiscard]] QImage qimage_from_document_rect_with_hidden_layers_and_layer_bounds_banded(
    const Document& document, QRect document_rect, bool preserve_alpha, const std::vector<LayerId>& hidden_layer_ids,
    const std::vector<std::pair<LayerId, Rect>>& layer_bounds);
[[nodiscard]] bool image_format_preserves_alpha(std::string_view extension) noexcept;
// A save's thumbnail as JFIF bytes (PSD resource 1036, JPEG EXIF IFD1; docs/file-info.md,
// "Thumbnails"): `image` fit to psd::kThumbnailMaxSide pixels (never enlarged), over white.
[[nodiscard]] std::vector<std::uint8_t> encode_thumbnail_jpeg(const QImage& image);
// The flattened image every flat-file writer starts from. With preserve_alpha, a single
// masked layer exports non-destructively: the original colors are kept and the mask
// becomes the alpha channel, because compositing would erase the colors wherever the
// mask is transparent. Shared by write_flat_image_file and the PDF writer.
[[nodiscard]] QImage flat_export_qimage(const Document& document, bool preserve_alpha);
// The document an 8-bit-only writer receives: `document` itself at 8 bits per channel,
// otherwise a copy in `narrowed` converted like Image > Mode > 8 Bits/Channel. The flat
// writers below call it themselves; the layered SVG and Aseprite writers go through it
// at their call sites.
[[nodiscard]] const Document& document_for_8bit_writer(const Document& document, std::optional<Document>& narrowed);
// `notices` (optional) receives the structural losses of a writer that keeps layers
// (today: editable PDF), one line each, for the save/export status message.
void write_flat_image_file(const Document& document, const QString& path, const QString& extension,
                           const ImageSaveOptions& options = {}, std::vector<std::string>* notices = nullptr);
// Writes the visible top-level layers as a looping animated GIF, top layer = frame 1
// (write_flat_image_file dispatches here when options.gif_animate). Each layer or group
// renders through render_layer_isolated; a trailing "0.25s" layer-name token overrides
// options.gif_frame_delay_cs. The export transforms apply per frame; trim uses the union
// of every frame's visible bounds so the frames keep one size. `notices` receives the
// trim notice. Throws when no top-level layer is visible.
void write_animated_gif_file(const Document& document, const QString& path, const ImageSaveOptions& options,
                             std::vector<std::string>* notices = nullptr);
// Installs the Qt-backed PNG codec used for the PNG-compressed entries inside .ico/.cur
// files (the formats library is Qt-free). Idempotent; called from the MainWindow
// constructor so every app and test path has it.
void install_ico_png_codec();
// Installs the Qt-backed JPEG encoder the Proton texture writer embeds for its JPEG
// encoding (the formats library is Qt-free). Idempotent; installed beside the ICO codec.
void install_rttex_jpeg_codec();

void write_animated_webp_file(const Document& document, const QString& path, const ImageSaveOptions& options,
                              std::vector<std::string>* notices = nullptr);
// Animated JPEG XL from the visible top-level layers, under the same frame rules as WebP;
// frames are 8-bit (docs/jxl.md).
void write_animated_jxl_file(const Document& document, const QString& path, const ImageSaveOptions& options,
                             std::vector<std::string>* notices = nullptr);
// Animated PNG (APNG) from the visible top-level layers, under the same frame rules as
// WebP; 8-bit RGBA frames, play count options.apng_loop_count (docs/animation.md).
void write_animated_png_file(const Document& document, const QString& path, const ImageSaveOptions& options,
                             std::vector<std::string>* notices = nullptr);

}  // namespace patchy::ui
