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
#include <stdexcept>
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

struct LevelsSettings {
  int black_input{0};
  int white_input{255};
  int gamma_percent{100};
  int black_output{0};
  int white_output{255};
  LevelsChannel channel{LevelsChannel::Rgb};
  LevelsRecord red{};
  LevelsRecord green{};
  LevelsRecord blue{};
};

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

struct FilterControlSpec {
  QString label;
  QString object_name;
  int minimum{0};
  int maximum{100};
  int value{100};
  QString suffix;
};

struct FilterDialogSpec {
  QString identifier;
  QString display_name;
  std::vector<FilterControlSpec> controls;
};

struct FilterPreviewSettings {
  bool preview_enabled{true};
  std::vector<int> values;
};

struct FilterProgress {
  std::function<bool(int completed, int total, const QString& detail)> update;
};

class FilterCancelled final : public std::runtime_error {
public:
  FilterCancelled();
};

[[nodiscard]] QString filter_action_object_name(const QString& identifier);
[[nodiscard]] QString filter_display_name(const FilterDefinition& filter);
[[nodiscard]] bool is_adjustment_only_filter(const QString& identifier);
[[nodiscard]] FilterDialogSpec filter_dialog_spec_for(const FilterDefinition& filter);
[[nodiscard]] std::optional<std::vector<int>> request_filter_settings(
    QWidget* parent, const FilterDialogSpec& spec, const std::function<void(FilterPreviewSettings)>& preview_changed);
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
void apply_filter_with_settings(const QString& identifier, const FilterRegistry& registry, PixelBuffer& pixels,
                                const std::vector<int>& values, QColor foreground = QColor(Qt::black),
                                QColor background = QColor(Qt::white), const FilterProgress* progress = nullptr);
// When a blur-family filter grows the layer (see build_filter_preview_pixels), the
// returned buffer is larger than `original` and `result_bounds`, if provided,
// receives the new document-space bounds (origin shifted, size grown). For other
// filters the buffer keeps its size and `result_bounds` is set to `bounds`.
[[nodiscard]] PixelBuffer build_filter_preview_pixels(
    const PixelBuffer& original, const QRegion& selection, Rect bounds, const QString& identifier,
    const FilterRegistry& registry, const FilterPreviewSettings& settings, QColor foreground = QColor(Qt::black),
    QColor background = QColor(Qt::white), const FilterProgress* progress = nullptr, Rect* result_bounds = nullptr);
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
