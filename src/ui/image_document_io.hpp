#pragma once

#include "core/document.hpp"
#include "ui/image_save_options.hpp"

#include <QColor>
#include <QImage>
#include <QRect>
#include <QRegion>
#include <QString>

#include <cstdint>
#include <functional>
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
// A flat image's per-pixel alpha on open (GitHub issue 534). For a single flat pixel layer:
// alpha that is uniform (all opaque, or all zero, which flat formats use as unused padding)
// is stripped to opaque RGB. Meaningful alpha stays plain layer transparency, unless
// `alpha_as_layer_mask` (the imports/flatAlphaAsLayerMask preference) moves it into an
// editable grayscale layer mask with the document-alpha marker over opaque RGB. Returns
// true when a mask was created. Multi-layer documents are left intact.
bool prepare_flat_image_alpha(Document& document, bool alpha_as_layer_mask);
// prepare_flat_image_alpha with the layer-mask option on.
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
// The style-mask cache (docs/performance.md) keys a layer's effect masks on its content
// revision, which every style edit changes, so an edit normally leaves all of them to be
// computed again. An edit that only shows or hides effects (an effect eye, the Layer Styles
// eye) changes none of them: each mask is made from the layer's pixels and masks and its
// own effect's settings. Take style_mask_cache_revision before such an edit and hand it to
// carry_style_masks_across_effect_visibility_edit after it: the masks cached under the old
// revision are filed under the new one as well, so the next render reuses them and an undo
// still finds the old ones. Never call it for an edit that changes a setting, adds, removes
// or reorders an effect, or touches pixels or masks. The one mask that reads other effects'
// switches (a Stroke Emboss bevel's) is left out.
[[nodiscard]] std::uint64_t style_mask_cache_revision(const Layer& layer) noexcept;
void carry_style_masks_across_effect_visibility_edit(std::uint64_t revision_before, const Layer& layer);
// Moves what the cache holds for `layer` under `revision_before` to its revision now, for a
// revision that moved while the pixels, masks and effects those entries were made from did
// not (the end of a paint stroke: the release reaches the painted layer through mutable
// accessors after the last patch was rendered). The caller vouches for that.
void rekey_style_masks(std::uint64_t revision_before, const Layer& layer);
// Masks the cache has had to compute since the process started (tests: a reuse adds none).
[[nodiscard]] std::uint64_t style_mask_cache_misses();
// Group silhouettes a stroke's patch render took from a cached base and patched under the
// dab, instead of flattening the group (StrokePatchWindows::GroupBase; tests and perf runs).
[[nodiscard]] std::uint64_t group_silhouette_patches();
// Region patches under the full-render size (0.5 to 4 Mpx) that composited in strips across
// the workers, and those that fell back to one pass because the render read an effect mask
// windowed to its clip (tests and perf runs).
[[nodiscard]] std::uint64_t patch_strip_renders();
[[nodiscard]] std::uint64_t patch_strip_fallbacks();
// Renders one layer alone at the document's size, so opacity/blend/styles come out
// exactly as the compositor draws them against an empty backdrop (sprite-sheet and
// image-sequence exports share this).
[[nodiscard]] QImage render_layer_isolated(const Document& document, const Layer& layer);
// Renders `stack` (top-level layers, bottom first, drawn as their own visibility says) alone on
// a transparent canvas the document's size: the animation frames' renderer.
[[nodiscard]] QImage render_layers_isolated(const Document& document, std::vector<Layer> stack);
[[nodiscard]] QImage qimage_from_document_rect(const Document& document, QRect document_rect, bool preserve_alpha);
// View-only composite in document coordinates, including negative coordinates outside the canvas.
[[nodiscard]] QImage render_pasteboard_patch(const Document& document, QRect rect,
                                             const std::vector<LayerId>& hidden);
// Native child coverage used to anchor paints in a clipped viewport render.
// The caller budgets the native silhouette and compositor workspace first.
[[nodiscard]] Rect group_visible_alpha_bounds(const Layer& group, Rect bounds, const PatternStore& patterns);
// What a paint stroke's patch render knows that an ordinary render does not
// (docs/performance.md, "Smart invalidation and style-mask caching").
struct StrokePatchWindows {
  // The painted layer and the styled groups around it: their effect masks are computed
  // over the patch's own draw-clipped windows and none is cached, since the next dab
  // changes them again.
  std::vector<LayerId> layers;
  // A group around the painted layer (or the painted group itself, for its mask) whose
  // flattened children differ from the silhouette cached under `base_revision`
  // (style_mask_cache_revision before the edits) only inside `changed`: the render patches
  // that silhouette instead of flattening every child over the whole group. `consume`
  // takes the base out of the cache, for a base that was one of this stroke's own steps.
  struct GroupBase {
    LayerId group{};
    std::uint64_t base_revision{0};
    bool consume{false};
    std::vector<Rect> changed;
  };
  std::vector<GroupBase> groups;
};
inline const StrokePatchWindows kNoStrokePatchWindows{};
[[nodiscard]] std::vector<RenderedDocumentPatch> qimage_patches_from_document_region(
    const Document& document, const QRegion& document_region, bool preserve_alpha,
    const StrokePatchWindows& windows = kNoStrokePatchWindows);
// The region split into row bands rendered across workers. PREVIEW-ONLY (the
// canvas's region preview refreshes under a dialog, GitHub #601 follow-up): a
// band's windowed style masks can differ slightly from the unbanded render, so
// the result must never reach a commit or a settled render cache.
[[nodiscard]] std::vector<RenderedDocumentPatch> qimage_patches_from_document_region_banded(
    const Document& document, const QRegion& document_region, bool preserve_alpha);
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
// QImage::save for files the user keeps (palette strips, script icons, brush tip masks):
// encodes into a QSaveFile, so the target is replaced by a rename only after a complete
// write and a failure keeps the old file (QImage::save(path) truncates it in place first).
// `format` is a QImageWriter format such as "PNG"; null derives it from the suffix.
// Returns false when the file cannot be created, encoded, or committed.
[[nodiscard]] bool save_qimage_atomically(const QImage& image, const QString& path, const char* format = nullptr);
// A save's thumbnail as JFIF bytes (PSD resource 1036, JPEG EXIF IFD1; docs/file-info-save-stamp.md,
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
// The flat 8-bit stand-in write_flat_image_file writes when an export transform (trim,
// resize, pixel-art scale, background fill) is requested: those applied, one pixel layer
// (or a document-alpha masked layer), the metadata, profile and palette carried over.
// Writing it with without_export_transforms(options) gives the same bytes as writing
// `document` with `options` (File > Export for Web prepares it once per size).
[[nodiscard]] Document export_transformed_document(const Document& document, const ImageSaveOptions& options,
                                                   std::vector<std::string>* notices = nullptr);
[[nodiscard]] ImageSaveOptions without_export_transforms(ImageSaveOptions options);
// The bytes write_flat_image_file writes for `extension` (encoded through a private
// temporary file), for previews that report and decode the real output.
[[nodiscard]] std::vector<std::uint8_t> encode_flat_image_bytes(const Document& document, const QString& extension,
                                                                const ImageSaveOptions& options,
                                                                std::vector<std::string>* notices = nullptr);
// Writes already encoded image bytes to `path` atomically (QSaveFile).
void write_encoded_image_file(const QString& path, std::span<const std::uint8_t> bytes);
// Writes the visible top-level layers as a looping animated GIF, top layer = frame 1
// (write_flat_image_file dispatches here when options.gif_animate). Each layer or group
// renders through render_layer_isolated; a trailing "0.25s" layer-name token overrides
// options.gif_frame_delay_cs. The export transforms apply per frame; trim uses the union
// of every frame's visible bounds so the frames keep one size. `notices` receives the
// trim notice. Throws when no top-level layer is visible.
// The frames every animated writer encodes (GIF, WebP, JPEG XL, APNG): the visible top-level
// layers, top layer first, with the "all" rules of animation::animation_frames (shared layers
// drawn in every frame, "all" groups expanded into one frame per child), each frame rendered
// alone (render_layers_isolated) at 8 bits with the export transforms applied per frame (trim to the union of every frame's visible bounds,
// resize, pixel-art scale, background fill). The writers take each frame's delay from its
// layer name's time token (animation::parse_layer_name_delay_ms). Export for Web's preview
// plays the same frames beside the decoded file (docs/web-export.md).
// animation_export_layers lists each frame's base layer, top first.
[[nodiscard]] std::vector<const Layer*> animation_export_layers(const Document& document);
// True when some animation frame (as for_each_animation_export_frame draws it, before any
// export transform) has a pixel that is not fully opaque. Renders frames until it finds one.
[[nodiscard]] bool animation_export_frames_have_transparency(const Document& document);
void for_each_animation_export_frame(const Document& document, const ImageSaveOptions& options,
                                     std::vector<std::string>* notices,
                                     const std::function<void(PixelBuffer rgba, const Layer& layer)>& frame);
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
