#pragma once

#include "core/document.hpp"
#include "core/smart_object.hpp"
#include "ui/canvas_widget.hpp"

#include <QImage>

#include <optional>

// Decoding embedded smart-object sources and re-rendering layer previews through the
// placement quad (M2). Decode fidelity note: PSD/PSB sources render from the child
// file's own flattened composite when present (Photoshop's pixels, not a Patchy
// re-composite), so an untouched child renders exactly as Photoshop would show it.
namespace patchy::ui {

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

// Full child document for Edit Contents: PSD/PSB keep their layer stack, Qt image
// formats arrive as a single-layer document named after the source file.
[[nodiscard]] std::optional<Document> decode_smart_object_source_document(const SmartObjectSource& source);

// Pixel density of the source contents, for Replace Contents' physical-scale rule
// (Photoshop preserves the content-inch to document-pixel map; see AGENTS.md E5).
// PSD/PSB report their document resolution; images report their embedded density;
// unknown densities fall back to 72 dpi like Photoshop.
[[nodiscard]] double smart_object_source_dpi(const SmartObjectSource& source);

// Resolves an ExternalFile source to an existing file on disk: the stored relative
// path against the owning document's folder first (Photoshop's same-folder rule),
// then the stored absolute path, then the file:// URI. Returns nullopt when none
// exist (the caller offers Relink to File...).
[[nodiscard]] std::optional<QString> resolve_smart_object_external_path(const SmartObjectSource& source,
                                                                        const QString& parent_document_dir);

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

// Re-renders `layer`'s preview from its embedded source in `document`'s store:
// decode + resample, then replace the layer's pixels/bounds and mark
// raster_status=patchy_raster. Returns false (layer untouched) when the layer is not
// an editable embedded smart object or its source cannot be decoded.
bool refresh_smart_object_layer_preview(Document& document, Layer& layer,
                                        CanvasWidget::TransformInterpolation interpolation);

}  // namespace patchy::ui
