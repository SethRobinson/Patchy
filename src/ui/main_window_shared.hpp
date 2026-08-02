#pragma once

// Helpers shared by the main_window_*.cpp translation units. MainWindow's
// implementation is split across several files (see docs/code-organization.md);
// helpers used by more than one of
// those files are promoted out of the per-file anonymous namespaces into this
// header. Internal to the MainWindow implementation - do not include this from
// outside the main_window_*.cpp family.

#include "core/adjustment_layer.hpp"
#include "core/layer.hpp"
#include "core/pixel_buffer.hpp"
#include "core/rect_utils.hpp"
#include "core/smart_filter.hpp"
#include "core/smart_filter_effects.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/filter_workflows.hpp"

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

namespace patchy::ui {

class CanvasWidget;
enum class CanvasTool;
struct BrushPreset;

// Recursive content checks shared by the split MainWindow translation units.
// These deliberately inspect groups as well as the selected root because a
// whole-document operation can otherwise rewrite a nested Smart Object's
// cached preview without updating its native source data.
[[nodiscard]] bool layer_tree_contains_smart_filters(const Layer& layer);
[[nodiscard]] bool layer_tree_contains_smart_object(const Layer& layer);
[[nodiscard]] bool document_contains_smart_objects(const Document& document);
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

// Rasterize the canvas selection into an 8-bit coverage mask covering
// selection_rect (document coordinates); 255 = fully selected.
[[nodiscard]] PixelBuffer selection_mask_pixels(const CanvasWidget& canvas, QRect selection_rect);

// Drop the verbatim PSD effect blocks ('lfx2'/'lrFX'/'plFX') a layer carried in
// from import. Must be called whenever code replaces layer_style(), or the next
// PSD save would resurrect the imported effects over the new ones.
void clear_layer_psd_style_source(Layer& layer);

// Text sizes are shown in points but rendered in document pixels through the
// document's print PPI (default 300 when unset).
[[nodiscard]] double text_size_ppi(const Document& document) noexcept;
[[nodiscard]] double text_pixels_to_points(int pixels, const Document& document) noexcept;
[[nodiscard]] int text_points_to_pixels(double points, const Document& document) noexcept;

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
constexpr auto kMainWindowTranslationContext = "patchy::ui::MainWindow";

// The application-wide dark QSS theme (defined in main_window_theme.cpp);
// applied once by the MainWindow constructor.
[[nodiscard]] QString photoshop_style();

QString translate_source(const QObject* object, const char* property_name);
void bind_translated_text(QObject* object, const char* source, const char* context = kMainWindowTranslationContext);
void bind_translated_tooltip(QObject* object, const char* source,
                             const char* context = kMainWindowTranslationContext);
void apply_bound_translation(QObject* object);
void bind_action_text(QAction* action, const char* source);
void bind_widget_text(QObject* object, const char* source);
void bind_tooltip(QObject* object, const char* source);

// Property naming the recent-folders submenu so rebuilds can find it.
constexpr auto kRecentFoldersMenuProperty = "patchy.recentFoldersMenu";
// Property naming the recent-files submenu pages so the event filter can find them.
constexpr auto kRecentFilesMenuProperty = "patchy.recentFilesMenu";

// Photoshop-style brush resize: the step scales with the current size so big
// brushes resize fast while small brushes keep 1-px precision. Growing scales
// by (1+f); shrinking scales by 1/(1+f) so ] then [ lands back on the same size.
[[nodiscard]] int proportional_brush_step(int size, int direction, bool coarse);

// The Round brush preset every launch starts from (the active tip is deliberately
// not persisted).
[[nodiscard]] QString default_startup_brush_preset_id();
void apply_brush_preset(CanvasWidget& canvas, const BrushPreset& preset);

// Localized display name of a tool for the status bar / info panel.
[[nodiscard]] QString tool_name(CanvasTool tool);

// Default anti-alias strength for the Type tool's Smoothing combo.
constexpr int kDefaultTextAntiAlias = 3;
// Select the combo row whose data matches value (falls back to the default row).
void set_text_smoothing_combo_value(QComboBox* combo, int value);
// Current anti-alias strength from the Smoothing combo (default when unset).
[[nodiscard]] int text_smoothing_combo_value(const QComboBox* combo);

// Property set to true on an inline text editor once its session is committed
// or cancelled; shared by the text-tool plumbing in main_window.cpp and
// refresh_options_bar in main_window_tool_options.cpp.
constexpr auto kTextEditorFinishedProperty = "patchy.textEditorFinished";

// Font families a text layer names that cannot draw its own glyphs: absent from the font
// database, or present with no coverage for the characters they are asked for (Patchy's bundled
// Noto Naskh Arabic resolves but holds no Latin letters, so a Latin layer set in it used to come
// up "font available" and then render entirely in the Latin fallback). Empty for a non-text or
// empty layer. Defined in main_window.cpp beside the rest of the font resolution; the layer
// panel uses it for the missing-font badge and the Type tool for its substitution warning.
[[nodiscard]] QStringList missing_text_families_for_layer(const Layer& layer);


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

// PATCHY_UI_PROFILE stderr timing lines (no-op unless the env var is set).
void log_ui_profile(std::string_view stage, double elapsed_ms, std::string_view detail = {});

// Scoped variant for functions with several returns: logs on destruction.
class UiProfileScope {
 public:
  explicit UiProfileScope(std::string_view stage)
      : stage_(stage), started_(std::chrono::steady_clock::now()) {}
  UiProfileScope(const UiProfileScope&) = delete;
  UiProfileScope& operator=(const UiProfileScope&) = delete;
  ~UiProfileScope() {
    log_ui_profile(stage_, std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now() - started_)
                               .count());
  }

 private:
  std::string_view stage_;
  std::chrono::steady_clock::time_point started_;
};

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

// Photoshop-style "<name> copy" / "<name> copy N" naming (an existing
// " copy"/" copy N" stem is stripped first so "X copy" duplicates to
// "X copy 2", not "X copy copy"). Shared by layer duplication in
// main_window_layer_ops.cpp and path duplication in main_window_paths.cpp.
[[nodiscard]] std::string duplicate_name_stem(std::string_view name);
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
[[nodiscard]] PixelBuffer make_solid_pixels(std::int32_t width, std::int32_t height, QColor color,
                                            PixelFormat format);

// Small modal input dialogs (single text line / single integer spin box).
[[nodiscard]] std::optional<QString> request_text_input(QWidget* parent, const QString& object_name,
                                                        const QString& title, const QString& label,
                                                        const QString& initial);
[[nodiscard]] std::optional<int> request_integer_input(QWidget* parent, const QString& object_name,
                                                       const QString& title, const QString& label, int value,
                                                       int minimum, int maximum, int step);

}  // namespace patchy::ui
