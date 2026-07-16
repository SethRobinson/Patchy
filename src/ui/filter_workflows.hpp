#pragma once

#include "core/adjustment_layer.hpp"
#include "core/layer.hpp"
#include "filters/filter_registry.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/curves_editor.hpp"

#include <QColor>
#include <QRegion>
#include <QString>

#include <functional>
#include <optional>
#include <string>
#include <vector>

class QWidget;

namespace patchy::ui {

struct HueSaturationSettings {
  int hue_shift{0};
  int saturation_delta{0};
  int lightness_delta{0};
  bool colorize{false};
  int colorize_hue{0};          // 0..360
  int colorize_saturation{25};  // 0..100
  int colorize_lightness{0};    // -100..100
};

[[nodiscard]] HueSaturationAdjustment to_hue_saturation_adjustment(const HueSaturationSettings& settings);
[[nodiscard]] HueSaturationSettings to_hue_saturation_settings(const HueSaturationAdjustment& adjustment);

// The dialog settings are the core model types; the record math
// (clamp_levels_record, levels_master_record, ...) lives in
// core/adjustment_layer.hpp. main_window.hpp re-declares this alias.
using LevelsSettings = LevelsAdjustment;

using CurvesSettings = CurvesAdjustment;

enum class CurvesCanvasMode {
  None,
  Targeted,
  BlackPoint,
  GrayPoint,
  WhitePoint
};

struct CurvesCanvasSample {
  QColor input_color{};
  CanvasReadGesture gesture{};
};

struct CurvesDialogHooks {
  std::function<void(CurvesCanvasMode mode,
                     std::function<void(const CurvesCanvasSample& sample)> sample_changed)>
      set_canvas_mode;
  std::function<void()> clear_canvas_mode;
  std::function<void(std::optional<CurvesClippingMode> mode, CurvesChannel channel)> clipping_changed;
};

struct ColorBalanceSettings {
  int cyan_red{0};
  int magenta_green{0};
  int yellow_blue{0};
};

using PosterizeSettings = PosterizeAdjustment;
using ThresholdSettings = ThresholdAdjustment;
using BrightnessContrastSettings = BrightnessContrastAdjustment;

struct FilterControlSpec {
  // The first six fields retain the legacy aggregate shape used by existing UI
  // tests and helpers. Catalog-generated specs also fill the typed fields below.
  QString label;
  QString object_name;
  int minimum{0};
  int maximum{100};
  int value{100};
  QString suffix;
  std::string parameter_key;
  FilterParameterKind kind{FilterParameterKind::Integer};
  FilterParameterValue default_value{std::int64_t{100}};
  std::optional<double> typed_minimum;
  std::optional<double> typed_maximum;
  std::optional<double> step;
  std::vector<FilterParameterOption> options;
  FilterParameterPresentation presentation{
      FilterParameterPresentation::Standard};
};

struct FilterDialogSpec {
  QString identifier;
  QString display_name;
  std::vector<FilterControlSpec> controls;
  std::uint32_t schema_version{1};
};

struct FilterPreviewSettings {
  bool preview_enabled{true};
  FilterInvocation invocation;
};

// Optional in-dialog preview source for request_filter_settings. When
// provided, the dialog shows a bounded zoomable proxy preview with the
// gallery's draggable spatial overlays; the pixels are consumed once at
// dialog open to build the proxy and must stay alive until the call returns.
struct FilterDialogPreviewSource {
  const PixelBuffer* pixels{nullptr};
  Rect bounds{};
  QRegion selection;
  const FilterRegistry* registry{nullptr};
};

struct SmartFilterBlendingSettings {
  BlendMode blend_mode{BlendMode::Normal};
  double opacity{1.0};
};

using FilterProgress = ::patchy::FilterProgress;
using FilterCancelled = ::patchy::FilterCancelled;

[[nodiscard]] QString filter_action_object_name(const QString& identifier);
[[nodiscard]] QString filter_display_name(const FilterDefinition& filter);
[[nodiscard]] QString filter_category_display_name(FilterCategory category);
[[nodiscard]] QString filter_progress_stage_text(FilterProgressStage stage);
[[nodiscard]] bool is_adjustment_only_filter(const FilterDefinition& filter);
[[nodiscard]] FilterDialogSpec filter_dialog_spec_for(const FilterDefinition& filter);
[[nodiscard]] std::optional<FilterInvocation> request_filter_settings(
    QWidget* parent, const FilterDialogSpec& spec, const std::function<void(FilterPreviewSettings)>& preview_changed,
    FilterInvocation initial = {},
    const FilterDialogPreviewSource* preview_source = nullptr);
[[nodiscard]] std::optional<SmartFilterBlendingSettings> request_smart_filter_blending_settings(
    QWidget* parent, std::function<void(bool, const SmartFilterBlendingSettings&)> preview_changed = {},
    SmartFilterBlendingSettings initial = {});
[[nodiscard]] std::optional<LevelsSettings> request_levels_settings(
    QWidget* parent, std::function<void(bool, const LevelsSettings&)> preview_changed = {},
    LevelsSettings initial = {}, const PixelBuffer* histogram_source = nullptr);
[[nodiscard]] std::optional<CurvesSettings> request_curves_settings(
    QWidget* parent, std::function<void(bool, const CurvesSettings&)> preview_changed = {},
    CurvesSettings initial = {}, CurvesHistograms histograms = {}, CurvesDialogHooks hooks = {});
[[nodiscard]] std::optional<HueSaturationSettings> request_hue_saturation_settings(
    QWidget* parent, std::function<void(bool, const HueSaturationSettings&)> preview_changed = {},
    HueSaturationSettings initial = {});
[[nodiscard]] std::optional<ColorBalanceSettings> request_color_balance_settings(
    QWidget* parent, std::function<void(bool, const ColorBalanceSettings&)> preview_changed = {},
    ColorBalanceSettings initial = {});
[[nodiscard]] std::optional<PosterizeSettings> request_posterize_settings(
    QWidget* parent, std::function<void(bool, const PosterizeSettings&)> preview_changed = {},
    PosterizeSettings initial = {});
[[nodiscard]] std::optional<ThresholdSettings> request_threshold_settings(
    QWidget* parent, std::function<void(bool, const ThresholdSettings&)> preview_changed = {},
    ThresholdSettings initial = {});
[[nodiscard]] std::optional<BrightnessContrastSettings> request_brightness_contrast_settings(
    QWidget* parent, std::function<void(bool, const BrightnessContrastSettings&)> preview_changed = {},
    BrightnessContrastSettings initial = {});
// When a blur-family filter grows the layer (see build_filter_preview_pixels), the
// returned buffer is larger than `original` and `result_bounds`, if provided,
// receives the new document-space bounds (origin shifted, size grown). For other
// filters the buffer keeps its size and `result_bounds` is set to `bounds`.
[[nodiscard]] PixelBuffer build_filter_preview_pixels(
    const PixelBuffer& original, const QRegion& selection, Rect bounds, const FilterRegistry& registry,
    const FilterPreviewSettings& settings, const FilterProgress* progress = nullptr, Rect* result_bounds = nullptr);
[[nodiscard]] PixelBuffer build_filter_preview_pixels(
    const PixelBuffer& original, const QRegion& selection, Rect bounds, const FilterRegistry& registry,
    const FilterRecipe& recipe, const FilterProgress* progress = nullptr, Rect* result_bounds = nullptr);
[[nodiscard]] bool pixel_buffers_equal(const PixelBuffer& lhs, const PixelBuffer& rhs);
[[nodiscard]] bool editable_rgb8_layer(const Layer* layer);
void apply_levels_to_pixels(PixelBuffer& pixels, Rect bounds, const QRegion& selection, LevelsSettings settings,
                            const FilterProgress* progress = nullptr);
void apply_curves_to_pixels(PixelBuffer& pixels, Rect bounds, const QRegion& selection, CurvesSettings settings,
                            const FilterProgress* progress = nullptr);
void apply_hue_saturation_to_pixels(PixelBuffer& pixels, Rect bounds, const QRegion& selection,
                                    HueSaturationSettings settings, const FilterProgress* progress = nullptr);
void apply_color_balance_to_pixels(PixelBuffer& pixels, Rect bounds, const QRegion& selection,
                                   ColorBalanceSettings settings, const FilterProgress* progress = nullptr);

}  // namespace patchy::ui
