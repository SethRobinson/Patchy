#pragma once

// Helpers shared by the main_window_*.cpp translation units. MainWindow's
// implementation is split across several files (see AGENTS.md "MainWindow's
// implementation is split across main_window_*.cpp"); helpers used by more than one of
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

#include <QRect>
#include <QString>

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace patchy {
class Document;
}

class QAction;
class QComboBox;
class QObject;
class QListWidget;

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
