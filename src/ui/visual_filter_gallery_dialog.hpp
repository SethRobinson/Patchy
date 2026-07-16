#pragma once

#include "filters/filter_registry.hpp"

#include <QRegion>

#include <functional>
#include <memory>
#include <optional>

class QWidget;

namespace patchy::ui {

class FilterLookLibrary;

enum class VisualFilterGalleryOutcome {
  Cancelled,
  Original,
  Filter,
};

struct VisualFilterGalleryResult {
  VisualFilterGalleryOutcome outcome{VisualFilterGalleryOutcome::Cancelled};
  std::optional<FilterRecipe> recipe;
};

struct VisualFilterGalleryPreview {
  bool canvas_enabled{true};
  std::optional<FilterRecipe> recipe;
};

using VisualFilterGalleryPreviewCallback =
    std::function<void(const VisualFilterGalleryPreview&)>;

// An exact renderer may opt into a recipe by returning its full-resolution,
// document-space result. Returning nullopt keeps the gallery's catalog proxy
// renderer as the fallback. The gallery invokes this callback only from a
// worker thread; implementations should use progress to stop stale work.
using VisualFilterGalleryExactRecipeRenderer =
    std::function<std::optional<FilterRenderResult>(
        const FilterRecipe&, const FilterProgress*)>;

struct VisualFilterGalleryExactPreview {
  bool canvas_enabled{true};
  FilterRecipe recipe;
  std::shared_ptr<const FilterRenderResult> rendered;
};

// Runs on the UI thread after the newest central exact render is accepted.
// The shared result is also emitted when Live Canvas Preview is toggled, so a
// host can reuse it without rendering the recipe a second time.
using VisualFilterGalleryExactPreviewCallback =
    std::function<void(const VisualFilterGalleryExactPreview&)>;

enum class GalleryTargetKind {
  PlainLayer,
  SmartObject,
};

// Describes the layer the accepted recipe will be applied to, so the gallery
// can show the destructive/non-destructive outcome before Apply. For a
// smart-object target, recipe_maps_to_smart_stack must answer exactly what the
// caller's Apply path will do (including caller-side constraints such as the
// 64-entry native stack cap). When it is null, the gallery falls back to
// smart_filter_entries_from_recipe over its own registry.
struct GalleryTargetContext {
  GalleryTargetKind kind{GalleryTargetKind::PlainLayer};
  std::function<bool(const FilterRecipe&)> recipe_maps_to_smart_stack;
};

// Presents every catalogued Filter-menu effect over an immutable, bounded
// proxy of the supplied layer pixels. A null preview recipe means Original;
// the result outcome distinguishes accepting Original from cancelling.
[[nodiscard]] VisualFilterGalleryResult request_visual_filter_gallery(
    QWidget* parent, const PixelBuffer& immutable_original, Rect bounds,
    const QRegion& selection, const FilterRegistry& registry,
    RgbColor foreground, RgbColor background,
    VisualFilterGalleryPreviewCallback preview_changed = {},
    FilterLookLibrary* look_library = nullptr,
    VisualFilterGalleryExactRecipeRenderer exact_renderer = {},
    VisualFilterGalleryExactPreviewCallback exact_preview_ready = {},
    GalleryTargetContext target_context = {});

}  // namespace patchy::ui
