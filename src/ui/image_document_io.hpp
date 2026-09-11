#pragma once

#include "core/document.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/rttex_document_io.hpp"

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

enum class IcoResample {
  Auto,     // nearest for palette-mode or small (<= 64 px) documents, smooth otherwise
  Nearest,
  Smooth,
};

struct ImageSaveOptions {
  int jpeg_quality{95};
  bmp::BmpEncoding bmp_encoding{bmp::BmpEncoding::Rgba32};
  bmp::BmpPaletteMode bmp_palette_mode{bmp::BmpPaletteMode::Exact};
  QString bmp_palette_path;
  std::vector<int> ico_sizes{16, 24, 32, 48, 64, 128, 256};
  IcoResample ico_resample{IcoResample::Auto};
  int cur_hotspot_x{0};
  int cur_hotspot_y{0};
  // PDF: lossless keeps the composite pixel-exact (Flate); unchecked hands the page to
  // Qt's fixed JPEG quality-94 encode. See PdfExportOptions in ui/pdf_export.hpp.
  bool pdf_lossless{true};
  // PDF: keep layers as editable objects (paths, real text, images) instead of one
  // flattened image. Per save, never a persisted default: MainWindow::resolve_pdf_layer_choice
  // sets it from the saveOptions/pdfLayerPolicy preference or the flatten-or-keep question.
  // See PdfExportOptions::editable_layers.
  bool pdf_editable_layers{false};
  // PDF, editable layers only: text whose font is not installed is embedded as the layer's
  // pixels instead of drawn as real text in a substitute face. Persists as
  // saveOptions/pdfMissingFontsAsImages. See PdfExportOptions::missing_fonts_as_images.
  bool pdf_missing_fonts_as_images{false};
  // Export-only transforms, offered by the Export Flat Image flow (never Save/Save As:
  // rescaling a save would silently mutate the file the session points at). Deliberately
  // not part of the persisted option defaults; the export dialog persists its own
  // saveOptions/export* keys. write_flat_image_file applies them in this order: trim,
  // resize, nearest-neighbor scale, background fill. With all of them at their defaults
  // the writer path is byte-identical to a plain save.
  // Nearest-neighbor pixel-art scale (1x/2x/4x/8x).
  int export_scale{1};
  // Crop to the bounding box of alpha != 0 before anything else. A fully transparent image
  // keeps its size and adds a writer notice.
  bool export_trim_transparent{false};
  // Smooth (bilinear) resize target for the FULL canvas, 0 = no resize. A trimmed export
  // scales by the same factors so it never distorts; one zero side follows the aspect.
  int export_width{0};
  int export_height{0};
  // Composite the final pixels over export_background_color (straight alpha). The result
  // is opaque and drops the document-alpha mask structure.
  bool export_fill_transparent{false};
  QColor export_background_color{Qt::white};
  // UI-only: MainWindow reveals the written file afterwards; the writers ignore it.
  bool export_reveal_in_file_explorer{false};
  // WebP: 0-100 through QImageWriter::setQuality (75 is Qt's own default, so an unset
  // save keeps today's bytes). Persists as saveOptions/webpQuality.
  int webp_quality{75};
  // WebP: lossless, sent as quality 100 (Qt's WebP plugin encodes losslessly at 100).
  // Persists as saveOptions/webpLossless.
  bool webp_lossless{false};
  // GIF: write the visible top-level layers as a looping animation (top layer = frame 1)
  // instead of one flattened image. Per save, like pdf_editable_layers: only the GIF
  // options dialog and the Export Layers as Animated GIF action set it, so CLI/scripted
  // saves and every non-dialog path keep writing today's single-frame bytes.
  bool gif_animate{false};
  // GIF animation: default per-frame delay in centiseconds (the wire unit); a trailing
  // "0.25s" token in a layer name overrides it per frame. Persists as
  // saveOptions/gifFrameDelayCs.
  int gif_frame_delay_cs{10};
  // JPEG XR: 1-100, mapped onto the WIC encoder's ImageQuality (0.0-1.0). Persists as
  // saveOptions/jxrQuality.
  int jxr_quality{90};
  // JPEG XR: lossless compression, which the codec treats as overriding the quality value.
  // Persists as saveOptions/jxrLossless.
  bool jxr_lossless{false};
  // Proton texture (.rttex): pixel encoding, JPEG quality (Jpeg only), power-of-two handling,
  // and the RTPack-style flags. Persist as saveOptions/rttexEncoding, rttexJpegQuality,
  // rttexPowerOfTwo, rttexForceSquare, rttexForceAlpha, and rttexCompress.
  rttex::Encoding rttex_encoding{rttex::Encoding::Rgba8};
  int rttex_jpeg_quality{90};
  rttex::PowerOfTwo rttex_power_of_two{rttex::PowerOfTwo::Pad};
  bool rttex_force_square{false};
  bool rttex_force_alpha{false};
  bool rttex_compress{true};
};

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
[[nodiscard]] PixelBuffer pixels_from_image_rgba(const QImage& image);
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
[[nodiscard]] bool image_format_preserves_alpha(std::string_view extension) noexcept;
// The flattened image every flat-file writer starts from. With preserve_alpha, a single
// masked layer exports non-destructively: the original colors are kept and the mask
// becomes the alpha channel, because compositing would erase the colors wherever the
// mask is transparent. Shared by write_flat_image_file and the PDF writer.
[[nodiscard]] QImage flat_export_qimage(const Document& document, bool preserve_alpha);
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

}  // namespace patchy::ui
