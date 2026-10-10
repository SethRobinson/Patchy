#pragma once

#include "core/document.hpp"
#include "core/smart_object.hpp"
#include "filters/filter_registry.hpp"
#include "ui/canvas_widget.hpp"

#include <QImage>
#include <QString>

#include <optional>
#include <string>
#include <unordered_map>

class QFileInfo;

// Decoding embedded smart-object sources and re-rendering layer previews through the
// placement quad (M2). Decode fidelity note: PSD/PSB sources render from the child
// file's own flattened composite when present (Photoshop's pixels, not a Patchy
// re-composite), so an untouched child renders exactly as Photoshop would show it.
namespace patchy::ui {

struct SmartObjectLayerPreview {
  // Photoshop's FEid cache stores this unfiltered placed/warped result.
  FilterRenderResult unfiltered;
  // Cached pixels displayed by the layer after its Smart Filter stack.
  FilterRenderResult rendered;
};

// What one operation learned about each source it rendered, keyed by source uuid,
// so a geometry change that re-renders several layers placed from one file (Image
// Size over six linked logos) reads and decodes that file once, not once per layer.
// An entry with `resolved` set and no `linked_contents` records a linked file that
// could not be found or read; the render then fails the same way for every layer.
// The image is converted for the owning document; do not share this operation
// cache between documents with different color profiles.
struct SmartObjectSourceRenderCache {
  struct Entry {
    bool resolved{false};
    std::optional<SmartObjectSource> linked_contents;  // the file's bytes (linked sources only)
    std::optional<QImage> image;                       // natural-size decode in the parent's display space
  };
  std::unordered_map<std::string, Entry> entries;
};

// How Patchy can round-trip an embedded source's format; this decides the Edit
// Contents guard. PsdDocument children keep their layer stack through DocumentIo;
// QtImage formats re-encode flattened through Qt; ReadOnly formats decode for
// preview rendering but refuse content edits; Undecodable sources keep Photoshop's
// stored preview forever.
enum class SmartObjectContentsFormat {
  PsdDocument,
  QtImage,
  ReadOnly,
  Undecodable,
};

[[nodiscard]] SmartObjectContentsFormat classify_smart_object_contents(const SmartObjectSource& source);

// Decoded flat pixels of the embedded source (RGBA8888), for preview rendering.
[[nodiscard]] std::optional<QImage> decode_smart_object_source_image(const SmartObjectSource& source);

// A grayscale PSD can embed an RGB source. Its placed raster must pass through the
// parent's gray profile before resampling/filtering, while the source stays RGB.
// Other documents keep the source unchanged. Call once per source/operation, not
// from painting; the returned image shares storage when no conversion is needed.
[[nodiscard]] QImage smart_object_image_for_document(const QImage& image, const Document& document);

// Decodes either embedded bytes or a linked file resolved relative to the
// owning document. Alias sources remain preservation-only.
[[nodiscard]] std::optional<QImage> decode_smart_object_source_image(
    const SmartObjectSource& source, const QString& parent_document_dir);

// Full child document for Edit Contents: PSD/PSB keep their layer stack, Qt image
// formats arrive as a single-layer document named after the source file.
[[nodiscard]] std::optional<Document> decode_smart_object_source_document(const SmartObjectSource& source);

// Pixel density of the source contents, for Replace Contents' physical-scale rule
// (Photoshop preserves the content-inch to document-pixel map; see docs/smart-objects.md, E5).
// PSD/PSB report their document resolution; images report their embedded density;
// unknown densities fall back to 72 dpi like Photoshop.
[[nodiscard]] double smart_object_source_dpi(const SmartObjectSource& source);

// Resolves an ExternalFile source to an existing file on disk: the stored relative
// path against the owning document's folder first (Photoshop's same-folder rule),
// then the stored absolute path, then the file:// URI. Returns nullopt when none
// exist (the caller offers Relink to File...).
[[nodiscard]] std::optional<QString> resolve_smart_object_external_path(const SmartObjectSource& source,
                                                                        const QString& parent_document_dir);

// The link-element filetype OSType for a file extension (lower case, no dot), pinned
// from Photoshop 2026 captures: "8BPB", "8BPS", "JPEG", "TIFF", "BMP ", "SVG ", and
// "png " for everything else.
[[nodiscard]] std::string smart_object_filetype_for_extension(const QString& extension);

// Linked-file bookkeeping (docs/smart-objects.md, "Place Linked ground truth").
//
// Photoshop stamps a link with the file's modification time in UTC, truncated to
// whole seconds, plus its byte size, and compares both to decide the link changed.
void stamp_smart_object_link(SmartObjectSource& source, const QFileInfo& file);
// True when the file on disk no longer matches the stored stamp. A stamp written in
// local time (Patchy before October 2026) still counts as unchanged.
[[nodiscard]] bool smart_object_link_changed_on_disk(const SmartObjectSource& source, const QFileInfo& file);
// Points an ExternalFile source at `file`: name, filetype, the file:// URI, the native
// absolute path, the path relative to `document_dir` (the bare file name while the
// document has no folder yet), and a fresh stamp. Marks the source dirty.
void set_smart_object_link_target(SmartObjectSource& source, const QFileInfo& file, const QString& document_dir);
// Photoshop computes each link's relative path against the document's folder when it
// saves, so a document that had no folder at placement time, or is saved somewhere
// else, still finds files that travel with it. Every link that resolves from
// `current_document_dir` gets its path relative to `saved_document_dir`; unresolved
// links and unchanged paths are left alone (clean elements keep their bytes).
// Returns true when any link changed.
bool refresh_smart_object_link_relative_paths(SmartObjectStore& store, const QString& current_document_dir,
                                              const QString& saved_document_dir);
// The linked source in `store` whose file resolves to `file` (the same file on disk),
// or nullptr. Several layers placing one file share that element, so one Update
// Smart Object Content refreshes all of them.
[[nodiscard]] SmartObjectSource* find_smart_object_link_for_file(SmartObjectStore& store, const QFileInfo& file,
                                                                const QString& document_dir);
// Reads `path` into an Embedded-kind probe (the decode helpers only read embedded
// bytes) named and typed after the file. Returns nullopt when the file cannot be
// read or is empty.
[[nodiscard]] std::optional<SmartObjectSource> load_smart_object_file_probe(const QString& path);

// True when the contents are vector artwork (SVG) that rasterizes at any size.
[[nodiscard]] bool smart_object_contents_are_vector(const SmartObjectSource& source);
// Rasterizes vector contents at the placement's own scale, so a placement larger or
// smaller than the artwork's natural size stays sharp (Photoshop re-renders vector
// smart objects the same way). The image covers the whole artwork and maps onto the
// placement quad like the natural-size image does. Returns nullopt for raster
// contents, a degenerate quad, or a decode failure; callers then use the natural image.
[[nodiscard]] std::optional<QImage> render_smart_object_vector_contents(const SmartObjectSource& source,
                                                                        const SmartObjectPlacement& placement);

// Resamples `source_image` through the placement quad (full image rect -> Trnf
// corners). Returns nullopt when the quad cannot be mapped.
[[nodiscard]] std::optional<TransformedImage> render_smart_object_pixels(
    const QImage& source_image, const SmartObjectPlacement& placement,
    CanvasWidget::TransformInterpolation interpolation);

// Warp-aware variant: with a warp (custom envelope mesh) the render goes through the
// warp surface grid; without one it falls back to the plain quad mapping.
[[nodiscard]] std::optional<TransformedImage> render_smart_object_pixels(
    const QImage& source_image, const SmartObjectPlacement& placement,
    const std::optional<SmartObjectWarp>& warp, CanvasWidget::TransformInterpolation interpolation);

// Renders one editable embedded or resolved linked Smart Object from its
// immutable source. Passing an override stack is used by the Gaussian dialog
// preview; nullptr uses the layer's current stack. No layer or document state
// is changed. A linked source resolves against `parent_document_dir` (the stored
// relative path first, like Photoshop); `cache`, when given, is consulted and filled
// so an operation over many layers reads each file once.
[[nodiscard]] std::optional<SmartObjectLayerPreview>
render_smart_object_layer_preview(
    const Document& document, const Layer& layer,
    CanvasWidget::TransformInterpolation interpolation,
    const SmartFilterStack* override_stack = nullptr,
    const QString& parent_document_dir = {},
    SmartObjectSourceRenderCache* cache = nullptr);

// Renders only the immutable placed/warped source. Dialog setup and filter
// deletion use this path so they never execute the existing filter merely to
// obtain the FEid source raster.
[[nodiscard]] std::optional<FilterRenderResult>
render_smart_object_unfiltered_layer_preview(
    const Document& document, const Layer& layer,
    CanvasWidget::TransformInterpolation interpolation,
    const QString& parent_document_dir = {},
    SmartObjectSourceRenderCache* cache = nullptr);

// Same pipeline for callers that already decoded fresh embedded/linked bytes.
// This is used by Edit/Replace/Relink Contents before the source store settles.
[[nodiscard]] std::optional<SmartObjectLayerPreview>
render_smart_object_image_preview(
    const QImage& source_image, const SmartObjectPlacement& placement,
    const std::optional<SmartObjectWarp>& warp,
    CanvasWidget::TransformInterpolation interpolation,
    const SmartFilterStack* stack, const Document& document);

// Atomically installs a pre-rendered preview and, when requested, regenerates
// the associated FEid cache before touching layer pixels.
bool install_smart_object_layer_preview(Document& document, Layer& layer,
                                        SmartObjectLayerPreview preview,
                                        bool refresh_native_cache = true);

// Re-renders `layer`'s preview from its embedded or resolved linked source in
// `document`'s store: decode + resample, then replace the layer's pixels/bounds and mark
// raster_status=patchy_raster. Returns false (layer untouched) when the layer is not
// an editable embedded or linked smart object, its linked file is missing, or its
// source cannot be decoded.
bool refresh_smart_object_layer_preview(Document& document, Layer& layer,
                                        CanvasWidget::TransformInterpolation interpolation,
                                        bool refresh_native_cache = true,
                                        const QString& parent_document_dir = {},
                                        SmartObjectSourceRenderCache* cache = nullptr);

// Why a linked layer's re-render would fail before any pixels are touched: the file
// could not be found, or it exists but cannot be read or decoded. nullopt for an
// embedded layer or a linked file that decodes. Geometry commits use this to keep
// the resampled preview and report the file instead of a generic render error.
enum class SmartObjectLinkProblem { missing, unreadable };
[[nodiscard]] std::optional<SmartObjectLinkProblem> smart_object_link_problem(
    const Document& document, const Layer& layer, const QString& parent_document_dir);

}  // namespace patchy::ui
