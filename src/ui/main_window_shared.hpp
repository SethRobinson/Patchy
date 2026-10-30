#pragma once

// Helpers shared by the main_window_*.cpp translation units. MainWindow's
// implementation is split across several files (see docs/code-organization.md);
// helpers used by more than one of
// those files are promoted out of the per-file anonymous namespaces into this
// header. Internal to the MainWindow implementation - do not include this from
// outside the main_window_*.cpp family.

#include "ui/text_font_service.hpp"
#include "core/adjustment_layer.hpp"
#include "core/layer.hpp"
#include "core/layer_metadata.hpp"
#include "core/pixel_buffer.hpp"
#include "core/rect_utils.hpp"
#include "core/smart_filter.hpp"
#include "core/smart_filter_effects.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/layer_style_dialog.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/ui_profile.hpp"

#include <QEventLoop>
#include <QRect>
#include <QString>
#include <QStringList>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace patchy {
class Document;
}

class QAction;
class QComboBox;
class QObject;
class QListWidget;
class QProgressDialog;

namespace patchy::ai {
struct AiMask;
}

namespace patchy::ui {

class CanvasWidget;
enum class CanvasTool;
struct BrushPreset;

// The tool's hotkey command id ("tools.brush"): a permanent identifier that also
// names the active tool in the settings store (`tools/activeTool`, GitHub issue
// 254). Never reuse or rename one; new tools append (docs/tool-options.md).
[[nodiscard]] QString tool_hotkey_id(CanvasTool tool);

// Recursive content checks shared by the split MainWindow translation units.
// These deliberately inspect groups as well as the selected root because a
// whole-document operation can otherwise rewrite a nested Smart Object's
// cached preview without updating its native source data.
[[nodiscard]] bool layer_tree_contains_smart_filters(const Layer& layer);
[[nodiscard]] bool layer_tree_contains_smart_object(const Layer& layer);
[[nodiscard]] bool document_contains_smart_objects(const Document& document);
// Whole-document geometry (Image Size, Canvas Size, Crop, Rotate) remaps document
// space, and smart-object placement quads ride that remap (core/document_geometry.cpp).
// These two shapes cannot follow, so the operations still refuse them. A native Smart
// Filter stack keeps its FEid cache and shared filter mask in DOCUMENT space as
// preserved blobs Patchy re-emits rather than re-canvases (an empty store is also
// checked, so a cache whose stack failed to model still counts). A smart object whose
// SoLd never parsed has no quad to map at all. Either one would leave the saved PSD
// describing the pre-operation canvas.
[[nodiscard]] bool document_contains_smart_filters(const Document& document);
[[nodiscard]] bool document_contains_unparsed_smart_objects(const Document& document);
// Any layer carrying a vector shape or vector mask (geometry-op guard until
// the vector geometry integration lands).
[[nodiscard]] bool document_contains_vector_content(const Document& document);

// Localized display name for an adjustment layer kind ("Levels", "Curves", ...).
[[nodiscard]] QString localized_adjustment_display_name(AdjustmentKind kind);

// Levels dialog settings -> sanitized core adjustment record (clamps the
// per-channel records and re-derives the master record). Shared by the
// adjustment-layer flows in main_window_adjustments.cpp and the destructive
// Levels dialog in main_window_destructive_adjustments.cpp.
[[nodiscard]] LevelsAdjustment sanitized_levels_adjustment(LevelsSettings settings);

// Canvas hooks wiring the Curves dialog's targeted-adjustment and
// black/gray/white-point pickers plus the clipping preview to a canvas.
// Shared by the adjustment-layer flows in main_window_adjustments.cpp and the
// destructive Curves dialog in main_window_destructive_adjustments.cpp.
[[nodiscard]] CurvesDialogHooks curves_canvas_hooks(CanvasWidget* canvas,
                                                    std::function<QColor(QPoint)> sample_input_color);

// Minimum duration before the modal filter/adjustment progress dialogs show
// (main_window_filters.cpp and main_window_destructive_adjustments.cpp).
constexpr int kFilterProgressMinimumDurationMs = 1000;

// FilterProgress adapter driving a modal QProgressDialog: updates the value
// and label when the integer percentage changes, pumps the event loop with
// event_flags, runs the optional per-tick callback, and reports cancellation
// through the dialog's Cancel button.
[[nodiscard]] FilterProgress progress_dialog_filter_progress(
    QProgressDialog& progress, std::function<QString(const QString&)> label_text,
    QEventLoop::ProcessEventsFlags event_flags, std::function<void()> tick_processing = {});

// Runs a cancellable filter/adjustment compute under the given progress
// dialog and returns when it finishes. On desktop the compute runs on the
// calling thread with the dialog driven from the progress callback (the
// progress_dialog_filter_progress shape, unchanged behavior). On threaded
// wasm the compute runs on a worker while this thread waits in an event
// loop: a processEvents pump never returns control to the browser, so a
// main-thread compute would keep the dialog invisible and get the tab
// flagged unresponsive, while an idle event loop suspends through Asyncify
// and lets the browser present frames. The worker's progress callback only
// writes atomics; a timer feeds them into the dialog on this thread, and the
// dialog's Cancel reaches the worker through an atomic flag. Whatever the
// compute throws (FilterCancelled included) is rethrown on the calling
// thread, so caller catch blocks work identically on every platform. The
// compute must not touch widgets or other UI state on the worker path;
// capture what it reads up front.
void run_filter_compute_with_progress(QProgressDialog& progress,
                                      std::function<QString(const QString&)> label_text,
                                      std::function<void()> tick_processing,
                                      const std::function<void(FilterProgress&)>& compute);

// Remove Background's mask step, shared by the menu command and layer.removeBackground():
// sets `layer`'s mask to the subject matte (0 outside it), multiplied into an existing mask's
// painted pixels so earlier hiding stays and its density and feather settings are kept.
void apply_subject_matte_to_layer_mask(const Document& document, Layer& layer, const patchy::ai::AiMask& matte);

// Points the automatic-selection engine at the bundled models ("ai-models" beside the
// executable, the macOS bundle's Resources, or the Linux share directory) unless a folder
// was already set. Idempotent; call before checking patchy::ai::segmentation_available().
void configure_automatic_selection_models();

// Rasterize the canvas selection into an 8-bit coverage mask covering
// selection_rect (document coordinates); 255 = fully selected.
[[nodiscard]] PixelBuffer selection_mask_pixels(const CanvasWidget& canvas, QRect selection_rect);

// An RGBA8 copy of `pixels` (an 8-bit gray/RGB/RGBA layer buffer positioned
// at `bounds`) with alpha 0 wherever the canvas selection covers the pixel
// below 50%. Features that limit their input to the selection (Trace Image
// to Shapes) trace this copy; deep buffers return unchanged.
[[nodiscard]] PixelBuffer pixels_limited_to_selection(const CanvasWidget& canvas, const PixelBuffer& pixels,
                                                      Rect bounds);

// Drop the verbatim PSD effect blocks ('lfx2'/'lrFX'/'plFX') a layer carried in
// from import. Must be called whenever code replaces layer_style(), or the next
// PSD save would resurrect the imported effects over the new ones.
void clear_layer_psd_style_source(Layer& layer);

// The Layers panel's effect rows (GitHub issue 256): every instance of `style`, shown or
// hidden, in the Layer Style dialog's category order, with its position in that kind's list.
struct LayerEffectRow {
  LayerStyleEffect effect{LayerStyleEffect::DropShadow};
  std::size_t index{0};
  bool enabled{true};
};
[[nodiscard]] std::vector<LayerEffectRow> layer_effect_rows(const LayerStyle& style);
// The instance's `enabled` flag, or null when `index` is past that kind's list.
[[nodiscard]] bool* layer_effect_enabled_flag(LayerStyle& style, LayerStyleEffect effect, std::size_t index);
// The effect's name as the Layer Style dialog lists it ("Drop Shadow").
[[nodiscard]] QString layer_effect_display_name(LayerStyleEffect effect);
// Erases that instance from its kind's list; false when `index` is past it.
bool remove_layer_effect_instance(LayerStyle& style, LayerStyleEffect effect, std::size_t index);

// Text sizes are shown in points but rendered in whole document pixels through the
// document's print PPI (default 300 when unset). Every point value within half a pixel
// names the same size, so text_pixels_to_points answers the one with the fewest decimals
// (closest to the exact value among those): 50 pt typed at 96 ppi is 67 px and shows as
// 50, not 50.25 (GitHub issue 371). text_points_to_pixels of the answer is always `pixels`.
[[nodiscard]] double text_size_ppi(const Document& document) noexcept;
[[nodiscard]] double text_pixels_to_points(int pixels, const Document& document) noexcept;
[[nodiscard]] int text_points_to_pixels(double points, const Document& document) noexcept;
// What a points field showing `shown_points` should show for `pixels`: the shown value when it
// already names that size (the user's own 12.5 stays 12.5), else text_pixels_to_points.
[[nodiscard]] double text_points_for_display(double shown_points, int pixels, const Document& document) noexcept;

// Doubles ampersands so a display name shows literally as QAction text.
[[nodiscard]] QString escape_qaction_ampersands(QString text);

// Replace a layer's pixels, keeping the old bounds origin.
void set_layer_pixels_preserving_origin(Layer& layer, PixelBuffer pixels, Rect original_bounds);

// Like set_layer_pixels_preserving_origin, but takes the new origin from
// new_bounds instead of preserving the old one. Used when a filter grows the
// layer (e.g. a blur bleeding into transparency) and the origin must shift.
void set_layer_pixels_with_bounds(Layer& layer, PixelBuffer pixels, Rect new_bounds);

// SVG <image> elements arrive from the Qt-free reader as data-URI strings in
// layer metadata (kLayerMetadataSvgPendingImage); this decodes them where
// QImage lives. Thread-safe (QImage decoding needs no GUI thread); the file
// open path runs it on the load worker, clipboard paste on the main thread.
void decode_pending_svg_images(std::vector<Layer>& layers, QStringList& import_notices);

// Object-property-based translation binding: bind_* stores the source string on the
// object and applies it; retranslate_bound_children re-applies every binding after a
// language switch (apply_bound_translation dispatches on the object's type).
constexpr auto kTranslationContextProperty = "patchy.translationContext";
constexpr auto kTranslationTextProperty = "patchy.translationText";
constexpr auto kTranslationToolTipProperty = "patchy.translationToolTip";
constexpr auto kTranslationStatusTipProperty = "patchy.translationStatusTip";
// A QLabel with this property set shows its bound text without a trailing colon
// (ASCII or fullwidth). Options-bar labels share their "Name:" source strings
// with dialog forms, where the colon stays; the bar itself reads cleaner
// without it (GitHub issue 127).
constexpr auto kTranslationStripsTrailingColonProperty = "patchy.translationStripsTrailingColon";
constexpr auto kMainWindowTranslationContext = "patchy::ui::MainWindow";

// The application-wide dark QSS theme (defined in main_window_theme.cpp);
// applied once by the MainWindow constructor.
// tool_palette_factor is the toolbar icon size's (toolbar_icon_size_factor); 1 is Normal.
[[nodiscard]] QString photoshop_style(double tool_palette_factor = 1.0);

QString translate_source(const QObject* object, const char* property_name);
void bind_translated_text(QObject* object, const char* source, const char* context = kMainWindowTranslationContext);
void bind_translated_tooltip(QObject* object, const char* source,
                             const char* context = kMainWindowTranslationContext);
void apply_bound_translation(QObject* object);
void bind_action_text(QAction* action, const char* source);
void bind_widget_text(QObject* object, const char* source);
// "Opacity:" / "不透明度：" without the colon; text without one is returned as is.
[[nodiscard]] QString strip_trailing_colon(QString text);
void bind_tooltip(QObject* object, const char* source);

// Property naming the recent-folders submenu so rebuilds can find it.
constexpr auto kRecentFoldersMenuProperty = "patchy.recentFoldersMenu";
// Property naming the recent-files submenu pages so the event filter can find them.
constexpr auto kRecentFilesMenuProperty = "patchy.recentFilesMenu";
// A File > Scripts menu or one of its folder submenus: a right-click on a script entry
// opens the script context menu (MainWindow::show_script_context_menu) instead of
// triggering the entry.
constexpr auto kScriptsMenuProperty = "patchy.scriptsMenu";

// Photoshop-style brush resize: the step scales with the current size so big
// brushes resize fast while small brushes keep 1-px precision. Growing scales
// by (1+f); shrinking scales by 1/(1+f) so ] then [ lands back on the same size.
[[nodiscard]] int proportional_brush_step(int size, int direction, bool coarse);

// Shift+[ / Shift+] (GitHub issue 522): the next quarter-step hardness (0, 25, 50, 75, 100%)
// below (direction < 0) or above `hardness`, clamped to 0..100. A value between stops moves
// to the nearest stop in that direction.
[[nodiscard]] int stepped_brush_hardness(int hardness, int direction);
// The tools whose options bar shows the Soft field (brush hardness): the painting and
// retouch brushes, and the shape tools while they draw pixels.
[[nodiscard]] bool tool_shows_brush_softness(CanvasTool tool) noexcept;

// The Round brush preset every launch starts from (the active tip is deliberately
// not persisted).
[[nodiscard]] QString default_startup_brush_preset_id();
void apply_brush_preset(CanvasWidget& canvas, const BrushPreset& preset);

// Localized display name of a tool for the status bar / info panel.
[[nodiscard]] QString tool_name(CanvasTool tool);

// Default anti-alias strength for the Type tool's Smoothing combo.
constexpr int kDefaultTextAntiAlias = kTextAntiAliasSmooth;
// Select the combo row whose data matches value (falls back to the default row).
void set_text_smoothing_combo_value(QComboBox* combo, int value);
// Current anti-alias strength from the Smoothing combo (default when unset).
[[nodiscard]] int text_smoothing_combo_value(const QComboBox* combo);

// Property set to true on an inline text editor once its session is committed
// or cancelled; shared by the text-tool plumbing in main_window.cpp and
// refresh_options_bar in main_window_tool_options.cpp.
constexpr auto kTextEditorFinishedProperty = "patchy.textEditorFinished";

// Layer-list row styling and edit-target highlighting, shared by the
// layer-panel TU and the document/session code that stayed in main_window.cpp.
void restyle_layer_rows(QListWidget* list);
void update_layer_target_styles(QListWidget* list, std::optional<LayerId> active_layer,
                                CanvasWidget::LayerEditTarget edit_target);

// Smart Filter mask editing support: whether a document is small enough for
// editable masks, and the full-canvas materialization of a stored mask.
[[nodiscard]] bool smart_filter_mask_document_editing_supported(std::int32_t document_width,
                                                                std::int32_t document_height) noexcept;
[[nodiscard]] std::optional<PixelBuffer> materialize_smart_filter_mask(const SmartFilterMask& mask,
                                                                       std::int32_t document_width,
                                                                       std::int32_t document_height);

// Rasterize eligibility checks shared by layer-panel refresh and layer commands.
[[nodiscard]] bool layer_has_rasterizable_content(const Layer& layer);
[[nodiscard]] bool layer_can_rasterize(const Layer& layer);
[[nodiscard]] bool layer_can_rasterize_layer_style(const Layer& layer);

// PATCHY_UI_PROFILE timing (log_ui_profile, UiProfileScope) lives in ui/ui_profile.hpp.

// File-dialog directory memory, shared by the open/save flows in
// main_window_files.cpp and the smart-object export/relink/replace/place
// code in main_window.cpp.
QString default_file_dialog_directory();
QString last_save_directory();
void remember_save_directory_for_path(const QString& path);
QString file_dialog_initial_path(const QString& existing_path, const QString& filename);

// Layer-tree duplication with fresh document layer ids, rekeyed smart-object
// placed uuids, and fresh native Photoshop layer ids, shared by the paste and
// duplicate flows in main_window.cpp and new_smart_object_via_copy in
// main_window_smart_objects.cpp. PhotoshopLayerIdAllocator is defined in
// main_window_shared.cpp; callers pass the null default.
class PhotoshopLayerIdAllocator;
bool smart_filter_records_available_for_clone(
    const Layer& source, const SmartFilterEffectsStore& store,
    const std::vector<SmartFilterEffectsRecord>* transferred_records = nullptr);
std::optional<Layer> clone_layer_tree_with_document_ids(
    Document& document, const Layer& source,
    const std::vector<SmartFilterEffectsRecord>* transferred_records = nullptr,
    PhotoshopLayerIdAllocator* native_layer_ids = nullptr);

// Insert a layer as the sibling directly above anchor_id (append at top level
// when the anchor is absent). Shared by the add-layer flow in
// main_window_layer_ops.cpp and the text-editor preview plumbing in
// main_window.cpp.
void insert_layer_after_anchor(Document& document, Layer layer, std::optional<LayerId> anchor_id);
// The sibling directly below anchor_id instead (the bottom of the top level when the
// anchor is absent).
void insert_layer_below_anchor(Document& document, Layer layer, std::optional<LayerId> anchor_id);

// Moves a copied subtree by (dx, dy) in its new document: bounds, the
// text/vector/smart-object metadata the Move tool shifts too (linked masks ride
// along inside translate_moved_layer_metadata, so callers never shift one by
// hand), and unlinked raster and vector masks, which a Move leaves behind but a
// copy carries as one unit. Shared by the cross-document copy in
// main_window_layer_ops.cpp and Convert to Smart Object in
// main_window_smart_objects.cpp.
void offset_copied_layer_tree(Layer& layer, std::int32_t dx, std::int32_t dy, std::int32_t document_width,
                              std::int32_t document_height);

// Re-rasterizes a text layer through its stored (already composed) transform; false
// keeps the caller's raster (missing font, imported warped text). Defined in
// main_window.cpp; shared by the free-transform commit callback, Image Size, and
// Convert to Layers.
bool rerender_text_layer_through_stored_transform(Layer& layer);
// Imported type whose font is missing: a transform keeps its resampled saved pixels (the
// rerender above refuses it) instead of redrawing it in a substitute. Exactly the layers the
// Free Transform missing-font warning asks about (GitHub issue 185). Patchy-made type with a
// substituted font re-renders and is not one of them.
bool text_transform_keeps_missing_font_pixels(const Layer& layer);

// Photoshop-style "<name> copy" / "<name> copy N" naming (an existing
// " copy"/" copy N" stem is stripped first so "X copy" duplicates to
// "X copy 2", not "X copy copy"). Shared by layer duplication in
// main_window_layer_ops.cpp and path duplication in main_window_paths.cpp.
[[nodiscard]] std::string duplicate_name_stem(std::string_view name);
// The artboard background choices (White, Black, Transparent, Other...) with their
// permanent tokens, and the size presets (Custom first), for every artboard control
// (main_window_artboards.cpp).
void populate_artboard_background_combo(QComboBox* combo);
void populate_artboard_size_presets(QComboBox* combo);
[[nodiscard]] std::string next_duplicate_name(std::string_view source_name,
                                              const std::set<std::string>& existing_names);

// Coalescing async preview plumbing shared by the adjustment/filter dialogs
// (main_window_adjustments.cpp) and the shape-appearance dialog
// (main_window_vector.cpp): `start` launches a background render for the
// request; while one is in flight further requests replace `pending` and the
// completion handler chains the newest. All fields are confined to the UI
// thread; workers compare `generation` after marshaling back.
template <typename Request>
struct AsyncPixelPreviewState {
  bool closed{false};
  bool in_flight{false};
  std::uint64_t generation{0};
  std::optional<Request> pending;
  std::function<void(const Request&)> start;
};

template <typename Request>
void enqueue_async_pixel_preview(const std::shared_ptr<AsyncPixelPreviewState<Request>>& state,
                                 Request request, bool immediate = false) {
  if (state == nullptr || state->closed || !state->start) {
    return;
  }
  if (!immediate && state->in_flight) {
    state->pending = std::move(request);
    return;
  }
  state->start(request);
}

template <typename Request>
void close_async_pixel_preview(const std::shared_ptr<AsyncPixelPreviewState<Request>>& state) {
  if (state == nullptr) {
    return;
  }
  state->closed = true;
  ++state->generation;
  state->pending.reset();
  state->start = {};
}

// Async preview launcher for the destructive adjustment dialogs (Levels /
// Curves / Hue-Saturation / Color Balance in
// main_window_destructive_adjustments.cpp), deduplicating their formerly
// per-dialog worker lambdas. The dialogs predate cooperative cancellation:
// they coalesce requests through AsyncPixelPreviewState and discard stale
// results, but let the current render finish.
struct DestructiveAdjustmentPreviewRequest {
  // True when the request should restore the original layer content instead
  // of rendering (preview disabled, or settings with no effect). The
  // per-dialog predicates are pure functions of the dialog settings, so the
  // value is computed once at enqueue time.
  bool identity{false};
  // Renders the adjustment into `pixels`, which arrives seeded with a copy of
  // the original snapshot. Runs on a background worker thread; empty when
  // `identity` is true.
  std::function<void(PixelBuffer& pixels)> render;
};

// UI-thread callbacks for make_destructive_adjustment_preview_state, built
// inside the dialog member functions because they touch private MainWindow
// state.
struct DestructiveAdjustmentPreviewHooks {
  // Snapshot of the target layer's pixels taken when the dialog opened; every
  // render starts from a copy of it.
  std::shared_ptr<const PixelBuffer> original_pixels;
  // Restores the original layer content and repaints. Only runs while the
  // dialog is open (requests stop at close_async_pixel_preview), so it may
  // capture the MainWindow raw.
  std::function<void()> restore_identity;
  // Applies a finished render to the preview layer and repaints. Runs from a
  // queued invocation that may fire during teardown, so it must guard the
  // MainWindow lifetime itself (QPointer).
  std::function<void(PixelBuffer pixels)> apply_result;
  // Optional canvas busy-badge hook (CanvasWidget::begin/end_preview_render):
  // called with true on the UI thread when a render worker starts and false
  // from its queued completion. Same teardown caveat as apply_result.
  std::function<void(bool active)> preview_render_active;
};

// Builds the latest-wins preview state machine the four dialogs share:
// identity requests drop pending work, bump the generation (so an in-flight
// result lands stale and is discarded), and restore synchronously; render
// requests bump the generation, copy the snapshot and render on a tracked
// background worker, marshal back to the UI thread with a queued invocation,
// apply only when still the latest and not closed, then chain the newest
// pending request. Drive it with enqueue_async_pixel_preview (pass
// immediate = request.identity, matching the pre-launcher call sites) and end
// it with close_async_pixel_preview.
[[nodiscard]] std::shared_ptr<AsyncPixelPreviewState<DestructiveAdjustmentPreviewRequest>>
make_destructive_adjustment_preview_state(DestructiveAdjustmentPreviewHooks hooks);

// Solid-color pixel buffer for new layers/documents.
// A coverage plane (mask, channel, smart filter mask) built at 8 bits, at `document`'s
// depth: masks and channels follow the document (docs/high-bit-depth.md).
[[nodiscard]] PixelBuffer coverage_at_document_depth(const Document& document, PixelBuffer coverage);
[[nodiscard]] PixelBuffer make_solid_pixels(std::int32_t width, std::int32_t height, QColor color,
                                            PixelFormat format);
// Straight-alpha RGBA over white, opaque RGB at the same depth out: Flatten Image and a Merge
// Down into the Background (defined in main_window_layer_ops.cpp).
[[nodiscard]] PixelBuffer composite_onto_white(const PixelBuffer& rgba);

// Small modal input dialogs (single text line / single integer spin box).
[[nodiscard]] std::optional<QString> request_text_input(QWidget* parent, const QString& object_name,
                                                        const QString& title, const QString& label,
                                                        const QString& initial);
[[nodiscard]] std::optional<int> request_integer_input(QWidget* parent, const QString& object_name,
                                                       const QString& title, const QString& label, int value,
                                                       int minimum, int maximum, int step);

// Why a Smart Filter change is refused when Patchy cannot render the filters that would
// then run (GitHub issue 243). The Layers panel tooltips and the status messages share them.
[[nodiscard]] QString smart_filter_unmodeled_show_message();
[[nodiscard]] QString smart_filter_unmodeled_left_running_message();
[[nodiscard]] QString smart_filter_unmodeled_left_after_delete_message();
[[nodiscard]] QString smart_filter_unmodeled_settings_message();
[[nodiscard]] QString smart_filter_unmodeled_stack_settings_message();

}  // namespace patchy::ui
